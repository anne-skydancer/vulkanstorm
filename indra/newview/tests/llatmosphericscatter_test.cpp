// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of the scattering core in llatmosphericlight.h (stage AT2-0; no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llatmosphericscatter_test.cpp
//   (also passes at -O0, slower)
//
//   - the bounds: every legacy sky is brought into a physical one (albedos <= 1 and 0.95, the multiple-scattering scale <= 4, the
//     series amplification <= 10, the aerosol phase <= 1e4, the lights' irradiance <= 3 x scale), each continuous across its bound
//     and flagged by a clamp bit; hostile (non-finite, negative, huge) input stays finite;
//   - the phase functions are normalised over the sphere;
//   - the transmittance table: its parameterisation round-trips, its texels are LLAtmosphere's (the slab and every kink cut),
//     and a lookup is within 1e-3 of the exact value; the sun's colour from the table is AT1's within 1 % above 2 degrees;
//   - the multiple-scattering texel's amplification is bounded and the table is smooth;
//   - the GPU vectors: llatmosphere_scatter_vectors.inc, from class1/windlight/atmosphereCommonF.glsl run on a GPU
//     (scripts/light/gen_atmosphere_scatter_vectors.py), are reproduced to 1e-4 relative (the GLSL is float);
//   - R-ACC: the aerial-perspective march at 12 steps (split at the haze slab's top, quadratic beyond) is within 1 % of a converged
//     4096-step march at 2, 10 and 40 km on Default, Foggy and Sunset, over the clouds' domain, with AT1's moon.

#include "llatmosphericlight.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace LLAtmosphericLight;

namespace
{
int failures = 0;

void check(bool cond, const char* what, double a = 0.0, double b = 0.0)
{
    if (!cond)
    {
        std::printf("FAIL %s (%g, %g)\n", what, a, b);
        ++failures;
    }
}

bool near(double a, double b, double rel = 1e-4, double abs_tol = 1e-9)
{
    return std::fabs(a - b) <= rel * std::fmax(std::fabs(a), std::fabs(b)) + abs_tol;
}

constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;

// shipped Windlight skies (indra/newview/app_settings/windlight/skies): the legacy fields the physical atmosphere reads
struct SkyEx
{
    const char* name;
    double sun[3], blue[3], haze, dm, my, dist, bh[3], hh, amb[3], gx, gz, sun_angle;
};

const SkyEx kSkies[] = {
    { "Default", { 0.7342105507850647, 0.7815789580345154, 0.8999999761581421 }, { 0.24475815892219543, 0.44872328639030457, 0.7599999904632568 }, 0.699999988079071, 0.00017999998817685992, 1605.0, 0.800000011920929, { 0.4954838156700134, 0.4954838156700134, 0.6399999856948853 }, 0.1899999976158142, { 1.0499999523162842, 1.0499999523162842, 1.0499999523162842 }, 5.0, -0.47999998927116394, 1.991769790649414 },
    { "Foggy", { 0.5399999618530273, 0.5399999618530273, 0.5399999618530273 }, { 0.14522500336170197, 0.3999969959259033, 0.8000019788742065 }, 4.0, 0.0003499999875202775, 600.0, 4.0, { 0.4307021498680115, 0.8539466857910156, 1.0 }, 0.6499999761581421, { 0.32999998331069946, 0.32999998331069946, 0.32999998331069946 }, 3.799999952316284, -0.5, 1.5707963705062866 },
    { "Sunset", { 2.8385701179504395, 2.8385701179504395, 2.8385701179504395 }, { 0.14522500336170197, 0.3999969959259033, 0.8000019788742065 }, 0.699999988079071, 0.0004600000102072954, 562.5, 1.0, { 0.10767599940299988, 0.21348699927330017, 0.25 }, 0.1599999964237213, { 1.0199999809265137, 0.809999942779541, 0.809999942779541 }, 5.0, -0.47999998927116394, 3.066194772720337 },
    { "[TOR] SUNSET - Bangko 1", { 1.6800000667572021, 1.3160001039505005, 0.9520000219345093 }, { 0.04999999701976776, 0.29999998211860657, 1.0 }, 0.09999999403953552, 0.00018999999156221747, 732.4000244140625, 16.100000381469727, { 0.11999999731779099, 0.6700000166893005, 1.0 }, 0.23999999463558197, { 0.0, 0.0, 0.0 }, 1.2000012397766113, -0.7499999403953552, 3.1101765632629395 },
    { "Night", { 0.34876692389196384, 0.3557424864638451, 0.6599999489332593 }, { 0.4499999936983082, 0.4499999939833595, 0.4499999944309046 }, 3.9999999963077992, 0.00030000001824737163, 906.200039575949, 3.0208916693946743e-09, { 0.23999999567946317, 0.23999999579784967, 0.2399999958387022 }, 1.790157954679478e-10, { 0.20405027105862608, 0.24246673976617727, 0.32999997392212316 }, 5.0, -0.47999999165502744, 4.7123589515686035 },
    { "[EUPHORIA] sunset 1 (a black channel)", { 1.2300000190734863, 0.8200001120567322, 0.0 }, { 0.29999998211860657, 0.29999998211860657, 0.29999998211860657 }, 0.9199999570846558, 0.0001399999891873449, 1396.0, 37.79999923706055, { 0.47999998927116394, 0.17142856121063232, 0.0 }, 0.14999999105930328, { 0.21000000834465027, 0.029999997466802597, 0.0 }, 8.80000114440918, -0.949999988079071, 0.0 },
    { "Phototools- Lo Light 01 (clamped rig)", { 1.5, 1.5, 3.0 }, { 2.0, 2.0, 2.0 }, 4.0, 0.0006399999838322401, 4000.0, 100.0, { 2.0, 2.0, 2.0 }, 1.0, { 0.0, 0.0, 0.0 }, 0.19999980926513672, -2.5, 0.9817466735839844 },
    { "[TOR] SPECIAL - Dreamwalker (clamped rig)", { 3.0, 3.0, 3.0 }, { 2.0, 2.0, 2.0 }, 4.0, 0.0008999999845400453, 4000.0, 1.0, { 2.0, 2.0, 2.0 }, 1.0, { 3.0, 3.0, 3.0 }, 0.19999980926513672, -2.5, 1.5707963705062866 },
    { "[TOR] SPECIAL - Puur (zero density)", { 1.5, 1.5, 1.5 }, { 0.0, 0.0, 0.0 }, 0.14999999105930328, 0.0, 0.0, 0.0, { 0.0, 0.0, 0.0 }, 0.0, { 0.5999999642372131, 0.5999999642372131, 0.5999999642372131 }, 20.0, -0.0, 3.2861061096191406 },
    { "Phototools- Yellow Stars 01 (negative distance)", { 3.0, 3.0, 1.5 }, { 1.0, 1.0, 2.0 }, 4.779999732971191, 0.00033069998607970774, 5691.0, -13761.5, { 1.28000009059906, 1.2799999713897705, 0.0 }, 0.3499999940395355, { 0.0, 0.0, 0.0 }, 3.000001907348633, 10.0, 0.7592089176177979 },
    { "[TOR] NIGHT - Under a yellow moon", { 0.5999999642372131, 0.5999999642372131, 0.44999998807907104 }, { 0.08105260878801346, 0.12989357113838196, 0.2199999988079071 }, 1.0799999237060547, 3.9999998989515007e-05, 1181.0, 1.399999976158142, { 0.2849999666213989, 0.2941935062408447, 0.3799999952316284 }, 0.10999999940395355, { 0.37132078409194946, 0.416000097990036, 0.47999998927116394 }, 6.600000858306885, -0.7999999523162842, 2.8148672580718994 },
};
enum { SKY_DEFAULT, SKY_FOGGY, SKY_SUNSET, SKY_BANGKO, SKY_NIGHT, SKY_BLACK_CHANNEL, SKY_RIG_LO, SKY_RIG_DREAM, SKY_ZERO, SKY_YELLOW_STARS, SKY_YELLOW_MOON };

// sunlight_color as bindDeferredShader sends it (pipeline.cpp:6427-6442): normalised to a maximum of 1, clamped
LegacySky legacy_of(const SkyEx& s)
{
    LegacySky sky;
    const double m = std::fmax(s.sun[0], std::fmax(s.sun[1], s.sun[2]));
    const double k = m > 1.0 ? 1.0 / m : 1.0;
    sky.source = { std::min(std::max(s.sun[0] * k, 0.0), 1.0), std::min(std::max(s.sun[1] * k, 0.0), 1.0), std::min(std::max(s.sun[2] * k, 0.0), 1.0) };
    sky.blue_density = { s.blue[0], s.blue[1], s.blue[2] };
    sky.haze_density = s.haze;
    sky.density_multiplier = s.dm;
    sky.max_y = s.my;
    sky.distance_multiplier = s.dist;
    // the validated stored ozone (see llatmosphericlight_test.cpp); fromLegacy replaces it by Bruneton's
    sky.absorption.count = 2;
    sky.absorption.layer[0].width_m = 25000.0;
    sky.absorption.layer[1].constant = 1.0;
    return sky;
}

ScatterFields fields_of(const SkyEx& s)
{
    ScatterFields f;
    f.blue_horizon = { s.bh[0], s.bh[1], s.bh[2] };
    f.haze_horizon = s.hh;
    f.ambient = { s.amb[0], s.amb[1], s.amb[2] };
    f.glow_x = s.gx;
    f.glow_z = s.gz;
    return f;
}

Vec3 dir_of(double elevation_deg, double azimuth_deg = 0.0)
{
    const double e = elevation_deg * DEG, a = azimuth_deg * DEG;
    return { std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e) };
}

// AT1's moon: mb x moon_compensation(2^-3, sun_z, sin 6 degrees)
double moon_gain_at(double sun_elevation_deg)
{
    return 0.5 * moon_compensation(std::exp2(-3.0), std::sin(sun_elevation_deg * DEG), std::sin(6.0 * DEG));
}

// the sky's scattering with the sun at `sun_el` degrees (azimuth 0) and the moon opposite, AT1's gain
LLScatter scatter_of(const SkyEx& s, double sun_el)
{
    LLScatter sc = LLScatter::fromLegacy(legacy_of(s), fields_of(s), moon_gain_at(sun_el));
    const Vec3 sun = dir_of(sun_el);
    sc.set_light_dirs(sun, { -sun.x, -sun.y, -sun.z });
    return sc;
}

double sun_elevation_of(const SkyEx& s) // the file's sun angle, degrees (Windlight: the sin of it is the light's vertical component)
{
    return std::asin(std::sin(s.sun_angle)) / DEG;
}

double lum(const Vec3& c) { return luminance(c); }

struct Tables
{
    TransmittanceLUT t;
    MsLUT ms;
};

void bind_tables(LLScatter& sc, Tables& tb, int ms_w = 32, int ms_h = 32)
{
    sc.t_lut = nullptr;
    sc.ms_lut = nullptr;
    sc.build_t_lut(tb.t);
    sc.t_lut = &tb.t;
    sc.build_ms_lut(tb.ms, ms_w, ms_h);
    sc.ms_lut = &tb.ms;
}

// ---------------------------------------------------------------------------------------------------
// vectors from the GPU: atmosphereCommonF.glsl evaluated on random atmospheres (scripts/light/gen_atmosphere_scatter_vectors.py)
// ---------------------------------------------------------------------------------------------------
const double kScAtm[][43] = {
#define SC_ATM(...) { __VA_ARGS__ },
#define SC_VEC(...)
#include "llatmosphere_scatter_vectors.inc"
#undef SC_ATM
#undef SC_VEC
};

const double kScVec[][18] = {
#define SC_ATM(...)
#define SC_VEC(...) { __VA_ARGS__ },
#include "llatmosphere_scatter_vectors.inc"
#undef SC_ATM
#undef SC_VEC
};

// the multiple-scattering stand-in the vectors use (the generator's MS_STUB)
Vec3 ms_stand_in(double rg, double rt, double r, double mu_s)
{
    const double x = std::min(std::max((r - rg) / (rt - rg), 0.0), 1.0);
    return Vec3{ 0.02, 0.03, 0.05 } * ((0.5 + 0.5 * mu_s) * (1.0 - 0.5 * x));
}

LLScatter scatter_of_row(const double* v)
{
    LLScatter sc;
    LLAtmosphere& a = sc.atm;
    a.rayleigh_ext = { v[0], v[1], v[2] };
    a.aerosol_ext = v[3];
    a.slab_ext = v[4];
    a.slab_top_km = v[5];
    a.ozone_ext = { v[6], v[7], v[8] };
    a.rayleigh = rayleigh_profile();
    a.rayleigh.layer[0].exp_scale = v[9];
    a.mie = mie_profile();
    a.mie.layer[0].exp_scale = v[10];
    a.absorption = ozone_profile();
    a.planet_radius_km = v[11];
    a.top_radius_km = v[12];
    a.excess = { v[13], v[14], v[15] };
    a.finalize();
    sc.rayleigh_scat = { v[16], v[17], v[18] };
    sc.aerosol_scat = v[19];
    sc.slab_scat = v[20];
    sc.ms_scale = { v[21], v[22], v[23] };
    sc.glow_x = v[24];
    sc.glow_z = v[25];
    sc.glow_norm = v[26];
    sc.anisotropy = v[27];
    sc.max_phase = 1e4;
    sc.lights.dir[0] = { v[28], v[29], v[30] };
    sc.lights.dir[1] = { v[31], v[32], v[33] };
    sc.lights.E[0] = { v[34], v[35], v[36] };
    sc.lights.E[1] = { v[37], v[38], v[39] };
    sc.lights.disc_sin[0] = v[40];
    sc.lights.disc_sin[1] = v[41];
    sc.lights.count = static_cast<int>(v[42]);
    const double rg = a.planet_radius_km, rt = a.top_radius_km;
    sc.ms_override = [rg, rt](double r, double mu_s) { return ms_stand_in(rg, rt, r, mu_s); };
    return sc;
}
} // namespace

int main()
{
    // 1. the phase functions are normalised over the sphere; the glow lobe's closed-form integral equals the numbers
    {
        for (const SkyEx& s : kSkies)
        {
            const LLScatter sc = scatter_of(s, 30.0);
            // 2 pi integral of p(cos) d(cos), in x = 1 - cos, log-spaced from 1e-9 to 2 (the lobes peak at x = 0)
            const int n = 200000;
            double im = 0.0, ir = 0.0;
            double prev_x = 0.0;
            for (int i = 1; i <= n; ++i)
            {
                const double x = 2.0 * std::pow(10.0, -9.0 + 9.0 * i / n), xm = 0.5 * (x + prev_x);
                im += sc.phase_m(1.0 - xm) * (x - prev_x);
                ir += LLScatter::phase_r(1.0 - xm) * (x - prev_x);
                prev_x = x;
            }
            // the cap at 1e4 removes a sliver of the integral: only checked when the peak is not capped
            if (!(sc.scatter_clamped & SCLAMP_PHASE_PEAK))
            {
                check(near(2.0 * PI * im, 1.0, 2e-3, 1e-9), "aerosol phase integrates to 1", 2.0 * PI * im);
            }
            else
            {
                check(2.0 * PI * im > 0.9 && 2.0 * PI * im <= 1.0 + 2e-3, "capped aerosol phase integrates to at most 1", 2.0 * PI * im);
            }
            check(near(2.0 * PI * ir, 1.0, 1e-3, 1e-9), "Rayleigh phase integrates to 1", 2.0 * PI * ir);
            check(sc.phase_m(1.0) <= sc.max_phase && sc.phase_m(1.0) > 0.0, "aerosol phase is positive and capped", sc.phase_m(1.0));
        }
        std::printf("phase functions: normalised on %d skies\n", static_cast<int>(sizeof(kSkies) / sizeof(kSkies[0])));
    }

    // 2. the translation of the Default sky (the reference): albedos 1 and 0.9, unit multiple scattering, E_sun = 3 lin(source)
    {
        const LLScatter sc = scatter_of(kSkies[SKY_DEFAULT], 60.0);
        check(sc.scatter_clamped == 0, "Default clamps nothing", sc.scatter_clamped);
        check(near(sc.rayleigh_albedo.x, 1.0, 1e-12) && near(sc.rayleigh_albedo.z, 1.0, 1e-12), "Default's Rayleigh albedo is 1", sc.rayleigh_albedo.x);
        check(near(sc.rayleigh_scat.x, sc.atm.rayleigh_ext.x, 1e-12) && near(sc.rayleigh_scat.z, sc.atm.rayleigh_ext.z, 1e-12), "Rayleigh scattering = extinction at albedo 1");
        check(near(sc.aerosol_albedo, 0.9, 1e-12), "Default's aerosol albedo is 0.9", sc.aerosol_albedo);
        check(near(sc.aerosol_scat, 0.9 * sc.atm.aerosol_ext, 1e-12) && near(sc.slab_scat, 0.9 * sc.atm.slab_ext, 1e-12), "aerosol and slab scatter at the aerosol albedo");
        check(near(sc.ms_scale.x, 1.0, 1e-12) && near(sc.ms_scale.y, 1.0, 1e-12), "Default's multiple-scattering scale is 1", sc.ms_scale.x);
        const Vec3 src = srgb_to_linear(legacy_of(kSkies[SKY_DEFAULT]).source);
        check(near(sc.E_sun.x, 3.0 * src.x, 1e-12) && near(sc.E_sun.z, 3.0 * src.z, 1e-12), "E_sun = 3 lin(source)", sc.E_sun.x);
        check(near(sc.E_moon.x, sc.E_sun.x * moon_gain_at(60.0), 1e-12) && sc.E_moon.x < 1e-5 * sc.E_sun.x, "E_moon = E_sun x AT1's gain (2.5e-6 x mb by day)", sc.E_moon.x);
        check(sc.glow_norm > 0.0 && near(sc.glow_x, 5.0, 1e-12), "Default's glow is a usable lobe", sc.glow_norm);
        for (const SkyEx& s : kSkies)
        {
            const LLScatter t = scatter_of(s, 30.0);
            check(t.rayleigh_albedo.x >= 0.0 && t.rayleigh_albedo.x <= 1.0 && t.rayleigh_albedo.y <= 1.0 && t.rayleigh_albedo.z <= 1.0, "Rayleigh albedo in [0, 1]", t.rayleigh_albedo.x);
            check(t.aerosol_albedo >= 0.0 && t.aerosol_albedo <= 0.95, "aerosol albedo in [0, 0.95]", t.aerosol_albedo);
            check(t.ms_scale.x <= 4.0 && t.ms_scale.y <= 4.0 && t.ms_scale.z <= 4.0 && t.ms_scale.x >= 0.0, "multiple-scattering scale in [0, 4]", t.ms_scale.x);
            check(t.E_sun.x <= 3.0 * t.bounds.max_sunlight_scale && t.E_sun.x >= 0.0, "E_sun <= 3 x scale", t.E_sun.x);
            check(t.rayleigh_scat.x <= t.atm.rayleigh_ext.x * (1.0 + 1e-12) && t.aerosol_scat <= 0.95 * t.atm.aerosol_ext * (1.0 + 1e-12), "scattering never exceeds extinction");
            check(t.E_moon.x <= 0.25 * t.E_sun.x * (1.0 + 1e-12), "the moon is at most a quarter of the sun");
        }
    }

    // 3. the bounds: each clamps, is continuous across the bound and sets its bit (sweeps of one field of the Default sky)
    {
        auto sweep = [&](const char* what, unsigned bit, double lo, double hi, auto set, auto get) {
            const int n = 400;
            double worst_jump = 0.0, prev = 0.0, last = 0.0;
            bool set_at_end = false, clear_at_start = false;
            for (int i = 0; i <= n; ++i)
            {
                SkyEx s = kSkies[SKY_DEFAULT];
                const double x = lo + (hi - lo) * i / n;
                set(s, x);
                const LLScatter sc = LLScatter::fromLegacy(legacy_of(s), fields_of(s), 0.0);
                const double v = get(sc);
                if (i > 0)
                {
                    worst_jump = std::max(worst_jump, std::fabs(v - prev));
                }
                prev = v;
                last = v;
                if (i == 0)
                {
                    clear_at_start = !(sc.scatter_clamped & bit);
                }
                if (i == n)
                {
                    set_at_end = (sc.scatter_clamped & bit) != 0;
                }
                check(std::isfinite(v), what, x);
            }
            check(clear_at_start && set_at_end, what, clear_at_start, set_at_end);
            return std::make_pair(worst_jump, last);
        };
        // the field changes by (hi - lo) / 400 per step; the bounded value's step is at most that over the reference value
        auto r1 = sweep("Rayleigh albedo bound", SCLAMP_RAYLEIGH_ALBEDO, 0.1, 0.4954838156700134 * 3.0, [](SkyEx& s, double x) { s.bh[0] = x; }, [](const LLScatter& sc) { return sc.rayleigh_albedo.x; });
        check(r1.first < (0.4954838156700134 * 3.0 - 0.1) / 400 / 0.4954838156700134 * 1.001 && r1.second == 1.0, "Rayleigh albedo: continuous, ends at 1", r1.first, r1.second);
        auto r2 = sweep("aerosol albedo bound", SCLAMP_AEROSOL_ALBEDO, 0.0, 0.19 * 3.0, [](SkyEx& s, double x) { s.hh = x; }, [](const LLScatter& sc) { return sc.aerosol_albedo; });
        check(r2.first < 0.9 * (0.19 * 3.0) / 400 / 0.19 * 1.001 && r2.second == 0.95, "aerosol albedo: continuous, ends at 0.95", r2.first, r2.second);
        auto r3 = sweep("multiple-scattering scale bound", SCLAMP_MS_SCALE, 0.0, 1.05 * 6.0, [](SkyEx& s, double x) { s.amb[1] = x; }, [](const LLScatter& sc) { return sc.ms_scale.y; });
        check(r3.first < 1.05 * 6.0 / 400 / 1.05 * 1.001 && r3.second == 4.0, "multiple-scattering scale: continuous, ends at 4", r3.first, r3.second);

        // the lights: the sunlight scale bound and the moon's gain bound
        const SkyEx& d = kSkies[SKY_DEFAULT];
        ScatterFields f = fields_of(d);
        f.sky_sunlight_scale = 100.0;
        LLScatter big = LLScatter::fromLegacy(legacy_of(d), f, 0.0);
        check((big.scatter_clamped & SCLAMP_SUNLIGHT_SCALE) && near(big.E_sun.z / big.E_sun.x, LLScatter::fromLegacy(legacy_of(d), fields_of(d), 0.0).E_sun.z / LLScatter::fromLegacy(legacy_of(d), fields_of(d), 0.0).E_sun.x, 1e-9),
              "sunlight scale is bounded");
        check(big.E_sun.x <= 3.0 * 4.0, "E_sun <= 3 x max scale", big.E_sun.x);
        LLScatter moon = LLScatter::fromLegacy(legacy_of(d), fields_of(d), 5.0);
        check((moon.scatter_clamped & SCLAMP_MOON_GAIN) && near(moon.E_moon.x, 0.25 * moon.E_sun.x, 1e-12), "the moon's gain is bounded at 1/4", moon.E_moon.x);
    }

    // 4. the aerosol phase: the glow lobe where usable, Cornette-Shanks (a physical phase) where not; the peak is capped
    {
        SkyEx s = kSkies[SKY_DEFAULT];
        const LLScatter lobe = LLScatter::fromLegacy(legacy_of(s), fields_of(s), 0.0);
        check(!(lobe.scatter_clamped & SCLAMP_GLOW_LOBE) && lobe.glow_norm > 0.0, "Default has a glow lobe");
        for (double gx : { 0.0, -1.0 })
        {
            s.gx = gx;
            const LLScatter cs = LLScatter::fromLegacy(legacy_of(s), fields_of(s), 0.0);
            check((cs.scatter_clamped & SCLAMP_GLOW_LOBE) && cs.glow_norm == 0.0, "glow.x <= 0 gives Cornette-Shanks", gx);
            check(cs.phase_m(1.0) > cs.phase_m(0.0) && cs.phase_m(0.0) > cs.phase_m(-1.0) * 0.5, "Cornette-Shanks peaks forward", cs.phase_m(1.0));
        }
        s = kSkies[SKY_DEFAULT];
        for (double gz : { -3.5, 0.6, 1e9, -1e300 })
        {
            s.gz = gz;
            const LLScatter cs = LLScatter::fromLegacy(legacy_of(s), fields_of(s), 0.0);
            check((cs.scatter_clamped & SCLAMP_GLOW_LOBE) && cs.glow_norm == 0.0 && std::isfinite(cs.phase_m(0.3)), "glow.z outside [-3, 0.5] gives Cornette-Shanks", gz);
        }
        s = kSkies[SKY_DEFAULT];
        // the glow's lobe is at most 1 / (2 pi x 0.001) = 159 / sr (the 1 - cos is clamped at 0.001) and Cornette-Shanks at 0.95 about 64:
        // the cap of 1e4 is a guard, exercised here with a cap of 1 (Default peaks at 1.2)
        ScatterBounds low;
        low.max_phase = 1.0;
        const LLScatter peak = LLScatter::fromLegacy(legacy_of(s), fields_of(s), 0.0, {}, low);
        check((peak.scatter_clamped & SCLAMP_PHASE_PEAK) && peak.phase_m(1.0) == 1.0 && peak.phase_m_uncapped(1.0) > 1.0 && std::isfinite(peak.phase_m(0.0)), "phase peak is capped", peak.phase_m(1.0));
        double max_lobe = 0.0;
        for (const SkyEx& k : kSkies)
        {
            max_lobe = std::max(max_lobe, scatter_of(k, 30.0).phase_m_uncapped(1.0));
        }
        check(max_lobe < 200.0, "the glow lobes' peaks are below 200 / sr", max_lobe);
        s.hh = 0.19;
        s.gx = 0.0;
        ScatterFields cs_f = fields_of(s);
        cs_f.mie_anisotropy = 0.95;
        check(LLScatter::fromLegacy(legacy_of(s), cs_f, 0.0).phase_m_uncapped(1.0) < 100.0, "Cornette-Shanks at g = 0.95 peaks below 100 / sr");
        s.gx = 5.0;
        s.gz = -3.0;
        // the phase never exceeds the cap anywhere, whatever the lobe
        for (double gz : { -3.0, -1.0, -0.48, 0.5 })
        {
            for (double gx : { 0.01, 1.0, 5.0, 400.0 })
            {
                s.gx = gx;
                s.gz = gz;
                const LLScatter t = LLScatter::fromLegacy(legacy_of(s), fields_of(s), 0.0);
                double m = 0.0;
                for (int i = 0; i <= 2000; ++i)
                {
                    m = std::max(m, t.phase_m(1.0 - 2.0 * i / 2000.0));
                }
                check(m <= 1e4 && m > 0.0 && std::isfinite(m), "phase <= 1e4 everywhere", m, gx);
            }
        }
        // the closed-form glow integral at gz = -1 (its logarithm) against its neighbours
        const double i0 = LLScatter::glow_integral(5.0, -1.0), i1 = LLScatter::glow_integral(5.0, -1.0 + 1e-6), i2 = LLScatter::glow_integral(5.0, -1.0 - 1e-6);
        check(near(i0, i1, 1e-4) && near(i0, i2, 1e-4), "glow integral is continuous across glow.z = -1", i0, i1);
    }

    // 5. hostile input stays finite and bounded
    {
        SkyEx base = kSkies[SKY_DEFAULT];
        const double bad[] = { std::nan(""), INFINITY, -INFINITY, -5.0, 1e30 };
        int cases = 0;
        for (double v : bad)
        {
            for (int field = 0; field < 8; ++field)
            {
                SkyEx s = base;
                ScatterFields f = fields_of(s);
                double moon = 0.1;
                switch (field)
                {
                    case 0: f.blue_horizon.x = v; break;
                    case 1: f.haze_horizon = v; break;
                    case 2: f.ambient.y = v; break;
                    case 3: f.glow_x = v; break;
                    case 4: f.glow_z = v; break;
                    case 5: f.sky_sunlight_scale = v; break;
                    case 6: moon = v; break;
                    default: f.mie_anisotropy = v; f.glow_x = 0.0; break;
                }
                LLScatter sc = LLScatter::fromLegacy(legacy_of(s), f, moon);
                sc.set_light_dirs(dir_of(30.0), dir_of(-30.0, 180.0));
                const Vec3 x = { 0.0, 0.0, sc.atm.planet_radius_km };
                const bool ok = std::isfinite(sc.rayleigh_scat.x + sc.aerosol_scat + sc.ms_scale.x + sc.E_sun.x + sc.E_moon.x + sc.phase_m(0.3) + sc.phase_m(1.0))
                             && sc.rayleigh_albedo.x <= 1.0 && sc.aerosol_albedo <= 0.95 && sc.ms_scale.x <= 4.0 && sc.phase_m(1.0) <= 1e4
                             && std::isfinite(lum(sc.light_T(x, sc.lights, 0)));
                ++cases;
                check(ok, "hostile scattering input stays finite and bounded", v, field);
            }
        }
        std::printf("hostile inputs: %d cases finite and bounded\n", cases);
        // the extinction's bounds (AT0) hold for every embedded sky under the scattering too
        for (const SkyEx& s : kSkies)
        {
            const LLScatter sc = scatter_of(s, 20.0);
            Vec3 sr, ext;
            double sm;
            sc.medium(0.0, sr, sm, ext);
            check(std::isfinite(ext.x + ext.y + ext.z + sm + sr.x) && sr.x <= ext.x + 1e-12 && sm <= ext.y + 1e-12 + 1.0, "medium is finite, scattering <= extinction at the ground", sr.x);
        }
    }

    // 6. the transmittance table: parameterisation, texels, interpolation, the sun's colour against AT1's
    {
        double worst_rt = 0.0;
        for (int i = 0; i <= 40; ++i)
        {
            for (int j = 0; j <= 40; ++j)
            {
                const double u = i / 40.0, v = j / 40.0;
                double r, mu, u2, v2;
                TransmittanceLUT::rmu_of(6360.0, 6420.0, u, v, r, mu);
                TransmittanceLUT::uv_of(6360.0, 6420.0, r, mu, u2, v2);
                if (v > 0.0 && u > 1e-4) // v = 0: the ground, r = Rg exactly; u = 0 is the zenith whose mu rounds
                {
                    worst_rt = std::max(worst_rt, std::max(std::fabs(u - u2), std::fabs(v - v2)));
                }
            }
        }
        check(worst_rt < 1e-6, "transmittance parameterisation round-trips", worst_rt);

        double worst_texel = 0.0, worst_lookup = 0.0, worst_sun = 0.0;
        for (const SkyEx& s : kSkies)
        {
            LLScatter sc = scatter_of(s, 30.0);
            Tables tb;
            sc.build_t_lut(tb.t);
            // a texel is the physical transmittance at its ray
            for (int j = 0; j < TransmittanceLUT::H; j += 9)
            {
                for (int i = 0; i < TransmittanceLUT::W; i += 17)
                {
                    double r, mu;
                    TransmittanceLUT::texel_ray(sc.atm, i, j, r, mu);
                    const Vec3 e = sc.atm.physical_transmittance(r - sc.atm.planet_radius_km, mu, 0.0), t = tb.t.texel[j * TransmittanceLUT::W + i];
                    worst_texel = std::max(worst_texel, std::fabs(e.x - t.x) + std::fabs(e.y - t.y) + std::fabs(e.z - t.z));
                }
            }
            // an interpolated lookup at the ground and at 1.5 km (the cloud layer), against the exact value
            for (double h : { 0.0, 0.05, 1.5, 20.0 })
            {
                for (int k = 0; k < 120; ++k)
                {
                    const double mu = -0.05 + 1.05 * k / 119.0;
                    const double mu_h = sc.atm.horizon_mu(h);
                    if (mu < mu_h + 1e-3)
                    {
                        continue;
                    }
                    const Vec3 e = sc.atm.physical_transmittance(h, mu, 0.0), t = tb.t.lookup(sc.atm.planet_radius_km + h, mu);
                    worst_lookup = std::max(worst_lookup, std::max(std::fabs(e.x - t.x), std::max(std::fabs(e.y - t.y), std::fabs(e.z - t.z))));
                }
            }
            // the sun's colour from the table (light_T: table x disc x excess) against AT1's CPU transmittance, above 2 degrees
            sc.t_lut = &tb.t;
            for (double el : { 2.0, 3.0, 5.0, 10.0, 20.0, 45.0, 70.0, 90.0 })
            {
                const double mu = std::sin(el * DEG);
                const Vec3 x = { 0.0, 0.0, sc.atm.planet_radius_km };
                ScatterLights lt = sc.lights;
                lt.dir[0] = { std::cos(el * DEG), 0.0, mu };
                const Vec3 t = sc.light_T(x, lt, 0), e = sc.atm.transmittance(0.0, mu, lt.disc_sin[0], 1.0);
                const double a[3] = { t.x, t.y, t.z }, b[3] = { e.x, e.y, e.z };
                for (int k = 0; k < 3; ++k)
                {
                    if (b[k] > 1e-2) // below that (a light rig's sun, optical depth > 4.6) the bilinear table is relative-inaccurate and the colour is dark
                    {
                        worst_sun = std::max(worst_sun, std::fabs(a[k] - b[k]) / b[k]);
                    }
                }
            }
        }
        std::printf("transmittance table: texels |dT| %.2g, lookup |dT| %.2g, sun colour vs AT1 %.3g relative (>= 2 degrees, T > 1e-2)\n", worst_texel, worst_lookup, worst_sun);
        check(worst_texel < 1e-12, "table texels are physical_transmittance", worst_texel);
        check(worst_lookup < 2e-3, "table lookup within 2e-3 of the exact transmittance", worst_lookup);
        check(worst_sun < 0.01, "the sun's colour from the table is AT1's within 1 % above 2 degrees", worst_sun);
    }

    // 7. multiple scattering: the series amplification is bounded, the table is smooth and positive
    {
        // a thick, nearly white fog: aerosol albedo at its 0.95 bound, Rayleigh albedo 1, a heavy column
        SkyEx fog = kSkies[SKY_FOGGY];
        fog.hh = 10.0;
        fog.bh[0] = fog.bh[1] = fog.bh[2] = 5.0;
        LLScatter sc = LLScatter::fromLegacy(legacy_of(fog), fields_of(fog), 0.0);
        const Vec3 sun = dir_of(40.0);
        sc.set_light_dirs(sun, { -sun.x, -sun.y, -sun.z });
        double worst_amp = 0.0, max_f_ms = 0.0;
        for (double h : { 0.0, 0.1, 1.0, 5.0 })
        {
            for (double mu : { -0.2, 0.0, 0.3, 0.8, 1.0 })
            {
                double f_ms = 0.0;
                const Vec3 psi = sc.ms_texel(sc.atm.planet_radius_km + h, mu, 6, 20, &f_ms);
                // L2 / (1 - min(f, 0.9)) <= 10 L2: recover L2 by the unbounded series
                const double amp = 1.0 / (1.0 - std::min(f_ms, 0.9));
                worst_amp = std::max(worst_amp, amp);
                max_f_ms = std::max(max_f_ms, f_ms);
                check(psi.x >= 0.0 && psi.y >= 0.0 && psi.z >= 0.0 && std::isfinite(psi.x + psi.y + psi.z), "multiple scattering is finite and non-negative", psi.x);
            }
        }
        check(worst_amp <= 10.0 + 1e-9, "the multiple-scattering series amplifies by at most 10", worst_amp);
        std::printf("multiple scattering in thick fog: largest f_ms %.3f, amplification %.2f (bound 10)\n", max_f_ms, worst_amp);

        // Default: f_ms well below the bound (the series is ~1.1), a smooth table
        LLScatter d = scatter_of(kSkies[SKY_DEFAULT], 60.0);
        Tables tb;
        bind_tables(d, tb);
        double f_ms = 0.0;
        const Vec3 psi = d.ms_texel(d.atm.planet_radius_km + 0.01, 0.5, 8, 20, &f_ms);
        check(f_ms > 0.01 && f_ms < 0.5, "Default's f_ms is a few tenths at most", f_ms);
        check(psi.x > 0.0 && psi.z > 0.0, "multiple scattering is positive", psi.x, psi.z);
        double worst_jump = 0.0;
        for (int j = 0; j < tb.ms.h; ++j)
        {
            for (int i = 0; i + 1 < tb.ms.w; ++i)
            {
                const Vec3 a = tb.ms.texel[j * tb.ms.w + i], b = tb.ms.texel[j * tb.ms.w + i + 1];
                double row_max = 0.0;
                for (int q = 0; q < tb.ms.w; ++q)
                {
                    row_max = std::max(row_max, tb.ms.texel[j * tb.ms.w + q].y);
                }
                worst_jump = std::max(worst_jump, std::fabs(a.y - b.y) / std::max(row_max, 1e-12));
            }
        }
        check(worst_jump < 0.25, "the multiple-scattering table is smooth in mu_s", worst_jump);
    }

    // 8. the sky irradiance texel: positive, rises with the sun, a regression number for the noon sky (not a gate)
    {
        const LLScatter sc = scatter_of(kSkies[SKY_DEFAULT], sun_elevation_of(kSkies[SKY_DEFAULT]));
        Tables tb;
        LLScatter d = sc;
        bind_tables(d, tb, 16, 16);
        const double mu_s = std::sin(sun_elevation_of(kSkies[SKY_DEFAULT]) * DEG);
        const Vec3 e_lo = d.sky_irradiance_texel(0.0, 0.2), e_hi = d.sky_irradiance_texel(0.0, mu_s), e_conv = d.sky_irradiance_texel(0.0, mu_s, 64, 512);
        check(e_lo.x > 0.0 && e_hi.x > e_lo.x, "sky irradiance rises with the sun", e_lo.x, e_hi.x);
        const double err = std::fabs(lum(e_hi) - lum(e_conv)) / lum(e_conv);
        // lum(E_sky x E_sun) / pi is the "capture irradiance" of the study (doc/light/atmosphere-study.md): 0.096 for Default there, against
        // 0.115 for today's probe sky. A regression number, not a gate.
        const double capture = lum(e_hi * d.E_sun) / PI;
        std::printf("sky irradiance on a horizontal surface, Default at %.1f degrees: E_sky %.4f %.4f %.4f per unit sun; capture irradiance lum(E_sky E_sun)/pi = %.4f (study 0.096, today 0.115); 8x8 dirs x 16 steps vs 64x64 x 512: %.3g\n",
                    sun_elevation_of(kSkies[SKY_DEFAULT]), e_hi.x, e_hi.y, e_hi.z, capture, err);
        check(capture > 0.085 && capture < 0.11, "Default's capture irradiance stays near the study's 0.096", capture);
        check(err < 0.02, "the sky irradiance texel (8x8 directions, 16 steps) is within 2 % of a converged one", err);
    }

    // 9. no whiteout: outside 10 degrees of the sun, no embedded sky's radiance exceeds the white-card limit E_sun / pi
    {
        double worst = 0.0;
        for (const SkyEx& s : kSkies)
        {
            for (double sun_el : { 5.0, 30.0, 70.0 })
            {
                LLScatter sc = scatter_of(s, sun_el);
                Tables tb;
                bind_tables(sc, tb, 16, 16);
                sc.lights.count = 1;
                const double limit = lum(sc.E_sun) / PI;
                for (double el : { -3.0, 0.0, 3.0, 10.0, 30.0, 60.0, 90.0 })
                {
                    for (double az : { 0.0, 60.0, 120.0, 180.0 })
                    {
                        const Vec3 d = dir_of(el, az);
                        if (std::acos(std::min(1.0, detail::dot(d, sc.lights.dir[0]))) < 10.0 * DEG)
                        {
                            continue;
                        }
                        const Vec3 L = sc.sky_radiance(d, 0.0, 24);
                        if (limit > 0.0)
                        {
                            worst = std::max(worst, lum(L) / limit);
                        }
                        check(std::isfinite(L.x + L.y + L.z) && L.x >= 0.0, "sky radiance is finite", L.x);
                    }
                }
            }
        }
        std::printf("no whiteout: embedded skies, largest sky luminance / (E_sun lum / pi) outside 10 degrees of the sun: %.3f\n", worst);
        check(worst <= 1.0, "sky radiance stays under the white-card limit", worst);
    }

    // 10. the GPU vectors: atmosphereCommonF.glsl (float) against the C++ (double): 1e-4 to 1e-3 relative, 4e-3 for the march (see case 2)
    {
        const int natm = static_cast<int>(sizeof(kScAtm) / sizeof(kScAtm[0])), nvec = static_cast<int>(sizeof(kScVec) / sizeof(kScVec[0]));
        std::vector<LLScatter> scs;
        for (int i = 0; i < natm; ++i)
        {
            scs.push_back(scatter_of_row(kScAtm[i]));
        }
        double worst[9] = {};
        int counts[9] = {};
        auto cmp = [&](int kind, int row, const char* what, double cpp, double gl, double rel, double abs_tol) {
            const double e = std::fabs(cpp - gl) / (std::fabs(gl) + abs_tol / rel);
            worst[kind] = std::max(worst[kind], e);
            if (!near(cpp, gl, rel, abs_tol))
            {
                std::printf("scatter vector %d (kind %d) %s: C++ %.9g GLSL %.9g\n", row, kind, what, cpp, gl);
                check(false, "scatter vector equals the GLSL");
            }
        };
        for (int i = 0; i < nvec; ++i)
        {
            const double* v = kScVec[i];
            const LLScatter& sc = scs[static_cast<int>(v[0])];
            const int kind = static_cast<int>(v[1]);
            const double* in0 = v + 2;
            const double* in1 = v + 6;
            const double* oa = v + 10;
            const double* ob = v + 14;
            const double rg = sc.atm.planet_radius_km, rt = sc.atm.top_radius_km;
            ++counts[kind];
            switch (kind)
            {
                case 0:
                {
                    double u, w, r, mu;
                    TransmittanceLUT::uv_of(rg, rt, in0[0], in0[1], u, w);
                    TransmittanceLUT::rmu_of(rg, rt, in0[2], in0[3], r, mu);
                    cmp(kind, i, "u", u, oa[0], 1e-3, 1e-3);
                    cmp(kind, i, "v", w, oa[1], 1e-3, 1e-3);
                    cmp(kind, i, "r", r, oa[2], 1e-5, 1e-3);
                    cmp(kind, i, "mu", mu, oa[3], 1e-3, 1e-3);
                    break;
                }
                case 1:
                {
                    double r, mu;
                    TransmittanceLUT::texel_ray(sc.atm, static_cast<int>(in0[0]), static_cast<int>(in0[1]), r, mu);
                    const Vec3 t = sc.atm.physical_transmittance(r - rg, mu, 0.0);
                    cmp(kind, i, "T.r", t.x, oa[0], 1e-3, 1e-5);
                    cmp(kind, i, "T.g", t.y, oa[1], 1e-3, 1e-5);
                    cmp(kind, i, "T.b", t.z, oa[2], 1e-3, 1e-5);
                    break;
                }
                case 2:
                { // 4e-3: the float march positions have 0.5 m resolution at the planet's radius (6360 km), and the slab's density changes over 190 m
                    const Vec3 d = Vec3{ in0[0], in0[1], in0[2] } * (1.0 / detail::len(Vec3{ in0[0], in0[1], in0[2] }));
                    Vec3 L, T;
                    sc.atmosphere_ap(d, in0[3], in1[0], static_cast<int>(in1[1]), L, T);
                    cmp(kind, i, "L.r", L.x, oa[0], 4e-3, 1e-7);
                    cmp(kind, i, "L.g", L.y, oa[1], 4e-3, 1e-7);
                    cmp(kind, i, "L.b", L.z, oa[2], 4e-3, 1e-7);
                    cmp(kind, i, "T.r", T.x, ob[0], 4e-3, 1e-6);
                    cmp(kind, i, "T.b", T.z, ob[2], 4e-3, 1e-6);
                    break;
                }
                case 3:
                {
                    double f = 0.0;
                    const Vec3 psi = sc.ms_texel(in0[0], in0[1], static_cast<int>(in0[2]), static_cast<int>(in0[3]), &f);
                    cmp(kind, i, "psi.r", psi.x, oa[0], 1e-3, 1e-7);
                    cmp(kind, i, "psi.b", psi.z, oa[2], 1e-3, 1e-7);
                    cmp(kind, i, "f_ms", f, oa[3], 1e-3, 1e-7);
                    break;
                }
                case 4:
                {
                    const Vec3 e = sc.sky_irradiance_texel(in0[0], in0[1], static_cast<int>(in0[2]), static_cast<int>(in0[3]));
                    cmp(kind, i, "E.r", e.x, oa[0], 1e-3, 1e-7);
                    cmp(kind, i, "E.b", e.z, oa[2], 1e-3, 1e-7);
                    break;
                }
                case 5:
                {
                    const Vec3 d = Vec3{ in0[0], in0[1], in0[2] } * (1.0 / detail::len(Vec3{ in0[0], in0[1], in0[2] }));
                    const Vec3 L = sc.sky_radiance(d, in0[3], static_cast<int>(in1[0]));
                    cmp(kind, i, "L.r", L.x, oa[0], 1e-3, 1e-7);
                    cmp(kind, i, "L.g", L.y, oa[1], 1e-3, 1e-7);
                    cmp(kind, i, "L.b", L.z, oa[2], 1e-3, 1e-7);
                    break;
                }
                case 6:
                    cmp(kind, i, "phase_r", LLScatter::phase_r(in0[0]), oa[0], 1e-4, 1e-7);
                    cmp(kind, i, "phase_m", sc.phase_m(in0[0]), oa[1], 1e-4, 1e-7);
                    break;
                case 7:
                {
                    Vec3 s_r, ext;
                    double s_m;
                    sc.medium(in0[0], s_r, s_m, ext);
                    cmp(kind, i, "s_r", s_r.x, oa[0], 1e-4, 1e-8);
                    cmp(kind, i, "s_r.b", s_r.z, oa[2], 1e-4, 1e-8);
                    cmp(kind, i, "s_m", s_m, oa[3], 1e-4, 1e-8);
                    cmp(kind, i, "ext", ext.x, ob[0], 1e-4, 1e-8);
                    cmp(kind, i, "ext.b", ext.z, ob[2], 1e-4, 1e-8);
                    break;
                }
                default:
                {
                    const Vec3 t = sc.light_T({ in0[0], in0[1], in0[2] }, sc.lights, static_cast<int>(in0[3]));
                    cmp(kind, i, "T.r", t.x, oa[0], 1e-3, 1e-6);
                    cmp(kind, i, "T.b", t.z, oa[2], 1e-3, 1e-6);
                    break;
                }
            }
        }
        for (int k = 0; k < 9; ++k)
        {
            check(counts[k] > 0, "every scatter vector kind is present", k);
        }
        std::printf("scatter vectors: %d atmospheres, %d GPU vectors; worst relative |C++ - GLSL| by kind (uv 0, texel 1, AP 2, MS 3, E 4, sky 5, phase 6, medium 7, light T 8):", natm, nvec);
        for (int k = 0; k < 9; ++k)
        {
            std::printf(" %.2g", worst[k]);
        }
        std::printf("\n");
    }

    // 11. R-ACC: the 12-step slab-split march against a converged 4096-step march, with AT1's moon
    {
        struct Case
        {
            const SkyEx* sky;
            double sun_el;
            const char* label;
        };
        const Case cases[] = {
            { &kSkies[SKY_DEFAULT], sun_elevation_of(kSkies[SKY_DEFAULT]), "Default (sun 65.9)" },
            { &kSkies[SKY_FOGGY], 89.99, "Foggy (sun 90)" },
            { &kSkies[SKY_SUNSET], sun_elevation_of(kSkies[SKY_SUNSET]), "Sunset (sun 4.3)" },
            { &kSkies[SKY_SUNSET], -8.0, "Sunset, twilight (sun -8, moon 8)" },
            { &kSkies[SKY_DEFAULT], -30.0, "Default, night (sun -30, moon 30)" },
        };
        for (const Case& c : cases)
        {
            LLScatter sc = scatter_of(*c.sky, c.sun_el);
            Tables tb;
            bind_tables(sc, tb, 16, 16);
            double worst[3] = { 0.0, 0.0, 0.0 }, worst8[3] = { 0.0, 0.0, 0.0 };
            const double dists[3] = { 2.0, 10.0, 40.0 };
            double dt_worst = 0.0;
            for (double el : { 0.0, 2.0, 5.0, 10.0, 30.0, 90.0 })
            {
                for (double az : { 0.0, 90.0, 180.0 })
                {
                    for (int k = 0; k < 3; ++k)
                    {
                        const double dist = dists[k];
                        const double end_alt = std::sqrt(6360.0 * 6360.0 + dist * dist + 2.0 * 6360.0 * dist * std::sin(el * DEG)) - 6360.0;
                        if (end_alt > 12.0)
                        {
                            continue; // outside the clouds' domain
                        }
                        const Vec3 d = dir_of(el, az);
                        Vec3 L12, T12, L8, T8, Lr, Tr;
                        sc.atmosphere_ap(d, dist, 0.0, 12, L12, T12);
                        sc.atmosphere_ap(d, dist, 0.0, 8, L8, T8);
                        sc.atmosphere_ap(d, dist, 0.0, 4096, Lr, Tr);
                        auto rel = [&](const Vec3& a, const Vec3& r) {
                            const double mx = std::max(detail::vmax(r), 1e-12), av[3] = { a.x, a.y, a.z }, rv[3] = { r.x, r.y, r.z };
                            double e = 0.0;
                            for (int q = 0; q < 3; ++q)
                            {
                                e = std::max(e, std::fabs(av[q] - rv[q]) / std::max(std::fabs(rv[q]), 1e-3 * mx));
                            }
                            return e;
                        };
                        worst[k] = std::max(worst[k], rel(L12, Lr));
                        worst8[k] = std::max(worst8[k], rel(L8, Lr));
                        dt_worst = std::max(dt_worst, std::max(std::fabs(T12.x - Tr.x), std::max(std::fabs(T12.y - Tr.y), std::fabs(T12.z - Tr.z))));
                    }
                }
            }
            std::printf("R-ACC %-36s 12 steps, max rel in-scatter at 2 / 10 / 40 km: %.2f / %.2f / %.2f %%  (8 steps: %.2f / %.2f / %.2f %%), max |dT| %.4f\n", c.label,
                        100.0 * worst[0], 100.0 * worst[1], 100.0 * worst[2], 100.0 * worst8[0], 100.0 * worst8[1], 100.0 * worst8[2], dt_worst);
            for (int k = 0; k < 3; ++k)
            {
                check(worst[k] <= 0.01, "R-ACC: 12-step march within 1 % of the converged march", worst[k], dists[k]);
            }
            check(dt_worst <= 0.01, "R-ACC: transmittance within 0.01", dt_worst);
        }
    }

    if (failures)
    {
        std::printf("llatmosphericscatter_test: %d FAILED\n", failures);
        return 1;
    }
    std::printf("llatmosphericscatter_test: OK\n");
    return 0;
}
