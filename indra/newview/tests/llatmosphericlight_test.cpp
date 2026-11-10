// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llatmosphericlight.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llatmosphericlight_test.cpp
// The vectors in llatmosphericlight_vectors.inc come from atmosphericsFuncs.glsl run on a GPU
// (scripts/light/gen_atmosphere_vectors.py); the header must reproduce them to 1e-4 relative
// (the GLSL is float). Since stage L3 the GLSL's ground light is the primary_light_color uniform (the sun's or the
// moon's colour with its path through the atmosphere already taken, by LLEnvironment::updatePrimaryLight); the old
// airmass and the sun/moon pick live on as legacy_transmittance and the handover below. Also checked:
//   - clouds do not change `sunlit` (L2's map dims it in the GLSL, not here) but lift the ambient;
//   - the ambient keeps some light opposite the sun (ambientLighting) and is grey outside classic mode.
//
// Stage AT0 (the physical atmosphere, llatmosphericlight.h's LLAtmosphere and primary_light):
//   - LLAtmosphere::transmittance (the physical model and the excess factor of the capped columns) equals
//     physicalAtmosphereF.glsl on the GPU (llatmosphere_vectors.inc, from scripts/light/gen_transmittance_vectors.py)
//     to 1e-4 relative, and a 200000-step integral to 1e-6;
//   - noon identity: lin(source) T(0, 1) = lin(source e^-tau) for the embedded Windlight skies, the light rigs included;
//   - the cap on the column (max_column): T is continuous in it at every altitude, partial caps act per channel;
//   - ozone: the validated stored default forms are recognised and replaced inside the model; the bounds on hostile input, monotonicity and the disc;
//   - the stateless sun/moon handover (primary_light), its phases, fallback and a 0.01 degree sunset sweep.
//
// Stage AT1/L3 (compute_primary_light: the chain LLEnvironment::updatePrimaryLight runs once per frame):
//   - noon identity: the ground light at noon equals today's (physical and A/B), for every embedded sky, the light rigs included;
//   - the clouds hand over by their own light (primary_light on the cloud-layer transmittances): the clouds' sun term
//     (1 - t_cloud) Y(cloud_T) is continuous per 0.01 degree;
//   - the handover sweeps through the whole chain (physical and A/B, moon high / opposite / none): unit finite directions,
//     jumps of direction, shadow or glow only where the ground light is dark, continuity across the cut ratios;
//   - the moon never exceeds its cap (a quarter of the sun's colour), a sky with no moon has no direct light at night,
//     nothing is NaN at the horizon or with hostile settings.

#include "llatmosphericlight.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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

bool near(double a, double b, double rel = 1e-4, double abs_tol = 1e-6)
{
    return std::fabs(a - b) <= rel * std::fmax(std::fabs(a), std::fabs(b)) + abs_tol;
}

const double kVectors[][17] = {
#include "llatmosphericlight_vectors.inc"
};

// {primary_light_color rgb, ambient rgb, cloud_shadow, sky_sunlight_scale, sky_ambient_scale, classic_mode, n_dot_l,
//  expected sunlit rgb, expected amblit rgb}
LLAtmosphericLight::Inputs inputs_of(const double* v)
{
    LLAtmosphericLight::Inputs in;
    in.primary_light_color = { v[0], v[1], v[2] };
    in.ambient_color = { v[3], v[4], v[5] };
    in.cloud_shadow = v[6];
    in.sky_sunlight_scale = v[7];
    in.sky_ambient_scale = v[8];
    in.classic_mode = static_cast<int>(v[9]);
    return in;
}

LLAtmosphericLight::Inputs reference()
{
    LLAtmosphericLight::Inputs in;
    in.primary_light_color = { 0.7, 0.6, 0.5 };
    in.ambient_color = { 0.3, 0.3, 0.4 };
    return in;
}

// --- stage AT0: the physical atmosphere ---

// {rayleigh_ext rgb, aerosol_ext, slab_ext, slab_top_km, ozone_ext rgb, rayleigh exp_scale, mie exp_scale,
//  absorption variant, planet radius km, top radius km, h km, mu, disc_sin, excess rgb, path_factor, expected transmittance rgb}
// from physicalAtmosphereF.glsl on the GPU (scripts/light/gen_transmittance_vectors.py)
const double kTransmittanceVectors[][24] = {
#include "llatmosphere_vectors.inc"
};

LLAtmosphericLight::DensityProfile ozone_variant(int variant)
{
    LLAtmosphericLight::DensityProfile p;
    if (variant == 0)
    {
        return LLAtmosphericLight::ozone_profile();
    }
    if (variant == 1) // a ramp, then a lower exponential layer (a jump at 12 km)
    {
        p.count = 2;
        p.layer[0].width_m = 12000.0;
        p.layer[0].linear = 1.0 / 12000.0;
        p.layer[1].exp_term = 0.8;
        p.layer[1].exp_scale = -1.0 / 6000.0;
        return p;
    }
    if (variant == 3) // exp + linear + constant: 1.1 at the ground, crosses 1 at 0.33 km and 0 at 6.2 km
    {
        p.layer[0].exp_term = 1.0;
        p.layer[0].exp_scale = -1.0 / 4000.0;
        p.layer[0].linear = -1.0 / 20000.0;
        p.layer[0].constant = 0.1;
        return p;
    }
    if (variant == 4) // a valley: 0.8 at the ground, below 0 around its minimum at 5 km, 1 again at 17.6 km
    {
        p.layer[0].exp_term = 2.0;
        p.layer[0].exp_scale = -1.0 / 3000.0;
        p.layer[0].linear = 1.0 / 8000.0;
        p.layer[0].constant = -1.2;
        return p;
    }
    if (variant == 5) // a negative exponential term: -e^(-h/3 km) + h/10 km + 0.5 crosses 0 and rises towards 1
    {
        p.layer[0].exp_term = -1.0;
        p.layer[0].exp_scale = -1.0 / 3000.0;
        p.layer[0].linear = 1.0 / 10000.0;
        p.layer[0].constant = 0.5;
        return p;
    }
    if (variant == 6) // two layers: a ramp to 0.5 at 3 km, then a mixed exponential layer
    {
        p.count = 2;
        p.layer[0].width_m = 3000.0;
        p.layer[0].linear = 1.0 / 6000.0;
        p.layer[1].exp_term = 1.6;
        p.layer[1].exp_scale = -1.0 / 7000.0;
        p.layer[1].linear = 1.0 / 30000.0;
        p.layer[1].constant = -0.2;
        return p;
    }
    p.layer[0].linear = 1.0 / 20000.0; // one linear layer that clamps at 1 at 20 km
    return p;
}

LLAtmosphericLight::LLAtmosphere atmosphere_of(const double* v)
{
    LLAtmosphericLight::LLAtmosphere a;
    a.rayleigh_ext = { v[0], v[1], v[2] };
    a.aerosol_ext = v[3];
    a.slab_ext = v[4];
    a.slab_top_km = v[5];
    a.ozone_ext = { v[6], v[7], v[8] };
    a.rayleigh = LLAtmosphericLight::rayleigh_profile();
    a.rayleigh.layer[0].exp_scale = v[9];
    a.mie = LLAtmosphericLight::mie_profile();
    a.mie.layer[0].exp_scale = v[10];
    a.absorption = ozone_variant(static_cast<int>(v[11]));
    a.planet_radius_km = v[12];
    a.top_radius_km = v[13];
    a.excess = { v[17], v[18], v[19] };
    a.finalize();
    return a;
}

// The transmittance by brute force: a 200000-step Simpson integral of the extinction along the path, with the
// altitude taken straight from the radius and no cuts, as the definition in LLAtmosphere::transmittance says, and the
// excess factor of the capped columns, written out from that definition.
LLAtmosphericLight::Vec3 reference_transmittance(const LLAtmosphericLight::LLAtmosphere& a, double h_km, double mu, double disc_sin, double path_factor = 1.0)
{
    using namespace LLAtmosphericLight;
    const double rg = a.planet_radius_km, top = a.top_radius_km - rg;
    const double h0 = std::min(std::max(h_km, 0.0), top);
    const double r0 = rg + h0;
    const double mu_h = -std::sqrt(std::max(0.0, 1.0 - (rg / r0) * (rg / r0)));
    const double vis = disc_sin > 0.0 ? smoothstep(mu_h - disc_sin, mu_h + disc_sin, mu) : (mu >= mu_h ? 1.0 : 0.0);
    if (vis <= 0.0)
    {
        return {};
    }
    const double b = r0 * mu;
    const double path = -b + std::sqrt(b * b - (r0 * r0 - a.top_radius_km * a.top_radius_km));
    const int steps = 200000; // even
    double tau[3] = { 0.0, 0.0, 0.0 };
    for (int i = 0; i <= steps; ++i)
    {
        const double s = path * i / steps;
        const double h = std::max(std::sqrt(r0 * r0 + 2.0 * b * s + s * s) - rg, 0.0);
        const double dr = a.rayleigh.density(h * 1e3), dm = a.mie.density(h * 1e3), dz = a.absorption.density(h * 1e3);
        const double ds = a.slab_top_km > 0.0 ? 1.0 - smoothstep(0.75 * a.slab_top_km, a.slab_top_km, h) : 0.0;
        const double grey = a.aerosol_ext * dm + a.slab_ext * ds;
        const double w = (i == 0 || i == steps) ? 1.0 : (i % 2 ? 4.0 : 2.0);
        tau[0] += w * (a.rayleigh_ext.x * dr + grey + a.ozone_ext.x * dz);
        tau[1] += w * (a.rayleigh_ext.y * dr + grey + a.ozone_ext.y * dz);
        tau[2] += w * (a.rayleigh_ext.z * dr + grey + a.ozone_ext.z * dz);
    }
    const double f = path / steps / 3.0;
    const double air = path_factor / std::max(mu - mu_h, 1e-6);
    return { std::exp(-tau[0] * f - a.excess.x * air) * vis, std::exp(-tau[1] * f - a.excess.y * air) * vis, std::exp(-tau[2] * f - a.excess.z * air) * vis };
}

struct Sky
{
    const char* name;
    double sun[3];   // the file's sunlight_color (raw)
    double blue[3];  // blue_density
    double haze, density_multiplier, max_y, distance_multiplier;
};

// shipped Windlight skies (indra/newview/app_settings/windlight/skies), chosen for their corners
const Sky kSkies[] = {
    { "Default", { 0.7342105507850647, 0.7815789580345154, 0.8999999761581421 }, { 0.24475815892219543, 0.44872328639030457, 0.7599999904632568 }, 0.699999988079071, 0.00017999998817685992, 1605.0, 0.800000011920929 },
    { "Sunset", { 2.8385701179504395, 2.8385701179504395, 2.8385701179504395 }, { 0.14522500336170197, 0.3999969959259033, 0.8000019788742065 }, 0.699999988079071, 0.0004600000102072954, 562.5, 1.0 },
    { "[TOR] SUNSET - Bangko 1", { 1.6800000667572021, 1.3160001039505005, 0.9520000219345093 }, { 0.04999999701976776, 0.29999998211860657, 1.0 }, 0.09999999403953552, 0.00018999999156221747, 732.4000244140625, 16.100000381469727 },
    { "Night", { 0.34876692389196384, 0.3557424864638451, 0.6599999489332593 }, { 0.4499999936983082, 0.4499999939833595, 0.4499999944309046 }, 3.9999999963077992, 0.00030000001824737163, 906.200039575949, 3.0208916693946743e-09 },
    { "Foggy", { 0.5399999618530273, 0.5399999618530273, 0.5399999618530273 }, { 0.14522500336170197, 0.3999969959259033, 0.8000019788742065 }, 4.0, 0.0003499999875202775, 600.0, 4.0 },
    { "(SS) Atmospheric 06:00cloudy2 (sunlight > 1)", { 2.258823871612549, 1.9647059440612793, 2.2705883979797363 }, { 0.14522500336170197, 0.3999969959259033, 0.8000019788742065 }, 0.07000000029802322, 0.00028569999267347157, 3037.0, 7.5 },
    { "[EUPHORIA] sunset 1 (a black channel)", { 1.2300000190734863, 0.8200001120567322, 0.0 }, { 0.29999998211860657, 0.29999998211860657, 0.29999998211860657 }, 0.9199999570846558, 0.0001399999891873449, 1396.0, 37.79999923706055 },
    { "Phototools- Lo Light 01 (clamped rig)", { 1.5, 1.5, 3.0 }, { 2.0, 2.0, 2.0 }, 4.0, 0.0006399999838322401, 4000.0, 100.0 },
    { "[TOR] SPECIAL - Dreamwalker (clamped rig)", { 3.0, 3.0, 3.0 }, { 2.0, 2.0, 2.0 }, 4.0, 0.0008999999845400453, 4000.0, 1.0 },
    { "[TOR] SPECIAL - Puur (zero density)", { 1.5, 1.5, 1.5 }, { 0.0, 0.0, 0.0 }, 0.14999999105930328, 0.0, 0.0, 0.0 },
    { "Annan Adored Dark Red sky (zero column)", { 1.56097412109375, 1.4059410095214844, 1.34527587890625 }, { 0.078125, 0.078125, 0.078125 }, 0.5799999833106995, 0.0, 880.0, 0.0 },
    { "Phototools- Yellow Stars 01 (negative distance)", { 3.0, 3.0, 1.5 }, { 1.0, 1.0, 2.0 }, 4.779999732971191, 0.00033069998607970774, 5691.0, -13761.5 },
};

// The absorption layers of a real sky: absorptionConfigDefault() is a = (25000, 0, 0, -1/25000, -2/3),
// b = (0, 0, 0, -1/15000, 8/3), and the validator clamps a layer's linear term to [0,2] and its constant to [0,1]
// in place (llsettingssky.cpp:211-232 via llsettingsbase.cpp:709-720 verifyFloatRange; every construction path
// validates: llsettingsvo.cpp:461, 477, 529-541, 1564-1568). So the skies in use hold these. llinventory is not
// linked in this standalone test, hence the literals.
LLAtmosphericLight::DensityProfile validated_ozone(double rising_linear)
{
    LLAtmosphericLight::DensityProfile p;
    p.count = 2;
    p.layer[0].width_m = 25000.0;
    p.layer[0].linear = rising_linear; // -1/25000 clamped to 0; or 1/15000 for the changed default
    p.layer[0].constant = 0.0;         // -2/3 clamped to 0
    p.layer[1].linear = 0.0;           // -1/15000 clamped to 0
    p.layer[1].constant = 1.0;         // 8/3 clamped to 1
    return p;
}

LLAtmosphericLight::DensityProfile validated_stock_ozone()
{
    return validated_ozone(0.0);
}

// sunlight_color as bindDeferredShader sends it (pipeline.cpp:6427-6442): normalised to a maximum of 1, clamped
LLAtmosphericLight::LegacySky legacy_of(const Sky& s)
{
    LLAtmosphericLight::LegacySky sky;
    const double m = std::fmax(s.sun[0], std::fmax(s.sun[1], s.sun[2]));
    const double k = m > 1.0 ? 1.0 / m : 1.0;
    sky.source = { std::min(std::max(s.sun[0] * k, 0.0), 1.0), std::min(std::max(s.sun[1] * k, 0.0), 1.0),
                   std::min(std::max(s.sun[2] * k, 0.0), 1.0) };
    sky.blue_density = { s.blue[0], s.blue[1], s.blue[2] };
    sky.haze_density = s.haze;
    sky.density_multiplier = s.density_multiplier;
    sky.max_y = s.max_y;
    sky.distance_multiplier = s.distance_multiplier;
    sky.absorption = validated_stock_ozone();
    return sky;
}

// atmosphericsFuncs.glsl:102-112 on the source colour, in linear: lin(source e^-tau) per channel
LLAtmosphericLight::Vec3 legacy_noon_light(const LLAtmosphericLight::LegacySky& sky)
{
    using namespace LLAtmosphericLight;
    const double s[3] = { sky.source.x, sky.source.y, sky.source.z };
    const double bd[3] = { sky.blue_density.x, sky.blue_density.y, sky.blue_density.z };
    double l[3];
    for (int k = 0; k < 3; ++k)
    {
        l[k] = srgb_to_linear(s[k] * std::exp(-(bd[k] + sky.haze_density * 0.25) * (sky.density_multiplier * sky.max_y)));
    }
    return { l[0], l[1], l[2] };
}

LLAtmosphericLight::Vec3 dir_of(double elevation_deg, double azimuth_deg = 0.0)
{
    const double e = elevation_deg * 3.14159265358979323846 / 180.0, a = azimuth_deg * 3.14159265358979323846 / 180.0;
    return { std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e) };
}

double angle_between_deg(const LLAtmosphericLight::Vec3& a, const LLAtmosphericLight::Vec3& b)
{
    const double c = std::min(1.0, std::max(-1.0, a.x * b.x + a.y * b.y + a.z * b.z));
    return std::acos(c) * 180.0 / 3.14159265358979323846;
}

bool finite_unit(const LLAtmosphericLight::Vec3& v)
{
    const double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return std::isfinite(l) && std::fabs(l - 1.0) < 1e-9;
}

bool in_unit(const LLAtmosphericLight::Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && v.x >= 0.0 && v.y >= 0.0 && v.z >= 0.0 && v.x <= 1.0 && v.y <= 1.0
        && v.z <= 1.0;
}

// A 0.01 degree sunset sweep: the sun goes from +10 to -10 degrees. `moon` 0: high in the sky (30 degrees), 1: opposite
// the sun (a full moon rising as the sun sets), 2: no moon (moon_brightness 0).
struct SweepResult
{
    double max_angle_step = 0.0;      // degrees of light direction per step
    double max_shadow_step = 0.0;     // |d shadow_strength|
    double max_light_step = 0.0;      // relative step of the ground light's luminance, where it is above 1e-3 of the source's
    double max_light_step_sun_up = 0.0; // the same, while the sun disc is not yet set (sun_z > -disc)
    double min_dir_z = 1.0;
    double fade_start_at = 99.0;      // the sun elevation (degrees) where the shadow strength first falls below 0.99
    double jump_light_level = 0.0;    // the ground light (share of the source's luminance) at the biggest jumps of direction or shadow
    bool all_unit = true, all_finite = true;
};

SweepResult sunset_sweep(const LLAtmosphericLight::LLAtmosphere& atm, const LLAtmosphericLight::Vec3& source_srgb, int moon)
{
    using namespace LLAtmosphericLight;
    const Vec3 source = srgb_to_linear(source_srgb);
    const double mb = moon == 2 ? 0.0 : 0.5;
    const double disc_sun = 0.0047, disc_moon = 0.0045;
    const double depth = std::sin(6.0 * 3.14159265358979323846 / 180.0);
    SweepResult r;
    Vec3 last_dir = {}, last_light = {};
    double last_shadow = 1.0;
    bool have_last = false;
    const int steps = 2000;
    for (int i = 0; i <= steps; ++i)
    {
        const double e = 10.0 - 0.01 * i;
        PrimaryLightInputs in;
        in.sun_dir = dir_of(e);
        in.moon_dir = moon == 1 ? Vec3{ -in.sun_dir.x, -in.sun_dir.y, -in.sun_dir.z } : dir_of(30.0, 100.0);
        in.sun_T = atm.transmittance(0.0, in.sun_dir.z, disc_sun);
        in.moon_T = atm.transmittance(0.0, in.moon_dir.z, disc_moon);
        in.source = source;
        in.moon_gain = mb * moon_compensation(0.125, in.sun_dir.z, depth);
        in.sun_disc_sin = disc_sun;
        const PrimaryLight pl = primary_light(in, PrimaryLightParams{});
        const Vec3 light = { source.x * pl.T.x, source.y * pl.T.y, source.z * pl.T.z };
        r.all_unit = r.all_unit && finite_unit(pl.dir);
        r.all_finite = r.all_finite && std::isfinite(light.x + light.y + light.z + pl.shadow_strength + pl.moon_weight);
        if (moon != 2)
        {
            r.min_dir_z = std::min(r.min_dir_z, pl.dir.z);
        }
        if (r.fade_start_at == 99.0 && pl.shadow_strength < 0.99)
        {
            r.fade_start_at = e;
        }
        if (have_last)
        {
            r.max_angle_step = std::max(r.max_angle_step, angle_between_deg(last_dir, pl.dir));
            r.max_shadow_step = std::max(r.max_shadow_step, std::fabs(pl.shadow_strength - last_shadow));
            if (angle_between_deg(last_dir, pl.dir) > 5.0 || std::fabs(pl.shadow_strength - last_shadow) > 0.25)
            {
                r.jump_light_level = std::max(r.jump_light_level, std::max(luminance(last_light), luminance(light)) / luminance(source));
            }
            const double y0 = luminance(last_light), y1 = luminance(light), ys = luminance(source);
            if (std::max(y0, y1) > 1e-3 * ys)
            {
                const double step = std::fabs(y1 - y0) / std::max(y0, y1);
                r.max_light_step = std::max(r.max_light_step, step);
                if (in.sun_dir.z > disc_sun)
                {
                    r.max_light_step_sun_up = std::max(r.max_light_step_sun_up, step);
                }
            }
        }
        last_dir = pl.dir;
        last_light = light;
        last_shadow = pl.shadow_strength;
        have_last = true;
    }
    return r;
}

// limits of the sunset sweep's steps (per 0.01 degree of sun travel, i.e. 0.4 s of a 4-hour day cycle)
const double SWEEP_LIGHT_STEP = 0.06;  // relative step of the ground light while the sun disc is up (measured at most 0.028)
const double SWEEP_CLOUD_STEP = 0.05;  // absolute step of the clouds' sun term (1 - t_cloud) Y(cloud_T) per 0.01 degree (Y(cloud_T) is at most 1)
const double SWEEP_JUMP_LIGHT = 0.005; // the ground light at a jump of direction (> 5 degrees) or shadow (> 0.25) (measured at most 0.0020)
} // namespace

int main()
{
    using namespace LLAtmosphericLight;

    const int count = static_cast<int>(sizeof(kVectors) / sizeof(kVectors[0]));
    check(count >= 64, "enough GLSL vectors", count);
    for (int i = 0; i < count; ++i)
    {
        const double* v = kVectors[i];
        const Result r = calc_sunlit_amblit(inputs_of(v), v[10]);
        const double got[6] = { r.sunlit.x, r.sunlit.y, r.sunlit.z, r.amblit.x, r.amblit.y, r.amblit.z };
        for (int k = 0; k < 6; ++k)
        {
            if (!near(got[k], v[11 + k]))
            {
                std::printf("vector %d value %d: C++ %.9g GLSL %.9g\n", i, k, got[k], v[11 + k]);
                check(false, "C++ equals the GLSL");
            }
        }
    }

    // the ground light is the primary light's colour: sunlit follows it (linear outside classic mode, sRGB in it)
    {
        Inputs in = reference();
        in.sky_sunlight_scale = 1.0;
        in.classic_mode = 1;
        const Result c = calc_sunlit_amblit(in, 1.0);
        check(c.sunlit.x == 0.7 && c.sunlit.y == 0.6 && c.sunlit.z == 0.5, "classic sunlit is the primary light's colour", c.sunlit.x, c.sunlit.z);
        in.classic_mode = 0;
        const Result p = calc_sunlit_amblit(in, 1.0);
        check(near(p.sunlit.x, srgb_to_linear(0.7), 1e-12, 1e-15) && near(p.sunlit.z, srgb_to_linear(0.5), 1e-12, 1e-15),
              "PBR sunlit is the primary light's colour in linear", p.sunlit.x, p.sunlit.z);
        in.primary_light_color = { 0.0, 0.0, 0.0 };
        const Result d = calc_sunlit_amblit(in, 1.0);
        check(d.sunlit.x == 0.0 && d.sunlit.y == 0.0 && d.sunlit.z == 0.0, "no primary light, no sunlit");
    }

    // clouds: sunlit unchanged here (L2's map dims it in the GLSL), ambient lifted
    {
        Inputs clear = reference(), cloudy = reference();
        cloudy.cloud_shadow = 0.8;
        const Result a = calc_sunlit_amblit(clear, 1.0), b = calc_sunlit_amblit(cloudy, 1.0);
        check(a.sunlit.x == b.sunlit.x && a.sunlit.y == b.sunlit.y && a.sunlit.z == b.sunlit.z, "clouds leave sunlit");
        check(b.amblit.x > a.amblit.x, "clouds lift the ambient", a.amblit.x, b.amblit.x);
    }

    // ambientLighting: facing the sun or away it is 1 at n.l = 0 and 0.75 at |n.l| = 1; outside
    // classic mode the ambient is grey
    {
        check(near(ambient_lighting(0.0), 1.0), "ambientLighting 0");
        check(near(ambient_lighting(1.0), 0.75), "ambientLighting +1", ambient_lighting(1.0));
        check(near(ambient_lighting(-1.0), 0.75), "ambientLighting -1");
        Inputs in = reference();
        in.classic_mode = 0;
        const Result r = calc_sunlit_amblit(in, 0.3);
        check(r.amblit.x == r.amblit.y && r.amblit.y == r.amblit.z, "grey ambient outside classic mode");
        in.classic_mode = 1;
        const Result c = calc_sunlit_amblit(in, 0.3);
        check(c.amblit.z > c.amblit.x, "classic ambient keeps its colour");
    }


    // ================= stage AT0 =================

    // 1. GLSL vectors: LLAtmosphere::transmittance equals physicalAtmosphereF.glsl on the GPU (float) to 1e-4 relative
    //    + 1e-9 absolute; 2. and the brute-force integral to 1e-6 absolute
    const int tcount = static_cast<int>(sizeof(kTransmittanceVectors) / sizeof(kTransmittanceVectors[0]));
    check(tcount >= 128, "enough transmittance vectors", tcount);
    double worst_gpu = 0.0, worst_ref = 0.0, worst_ref_rel = 0.0, worst_smooth = 0.0;
    int zero_cases = 0, partial_cases = 0, excess_cases = 0, half_path_cases = 0, variant_cases[7] = {};
    for (int i = 0; i < tcount; ++i)
    {
        const double* v = kTransmittanceVectors[i];
        const LLAtmosphere a = atmosphere_of(v);
        const Vec3 t = a.transmittance(v[14], v[15], v[16], v[20]);
        const Vec3 ref = reference_transmittance(a, v[14], v[15], v[16], v[20]);
        const double got[3] = { t.x, t.y, t.z }, gl[3] = { v[21], v[22], v[23] }, rf[3] = { ref.x, ref.y, ref.z };
        for (int k = 0; k < 3; ++k)
        {
            if (!near(got[k], gl[k], 1e-4, 1e-9))
            {
                std::printf("transmittance vector %d channel %d: C++ %.9g GLSL %.9g\n", i, k, got[k], gl[k]);
                check(false, "transmittance equals the GLSL");
            }
            worst_gpu = std::fmax(worst_gpu, std::fabs(got[k] - gl[k]) / (1e-9 / 1e-4 + std::fabs(gl[k])));
            if (static_cast<int>(v[11]) != 1 && static_cast<int>(v[11]) != 6) // variants 1 and 6 jump at their layer boundary, which the brute-force integral itself cannot resolve to 1e-6
            {
                worst_smooth = std::fmax(worst_smooth, std::fabs(got[k] - rf[k]));
            }
            if (std::fabs(got[k] - rf[k]) > 1e-6)
            {
                std::printf("transmittance vector %d channel %d: segmented %.12g reference %.12g\n", i, k, got[k], rf[k]);
                check(false, "segmented quadrature equals the reference integral");
            }
            worst_ref = std::fmax(worst_ref, std::fabs(got[k] - rf[k]));
            if (rf[k] > 1e-3)
            {
                worst_ref_rel = std::fmax(worst_ref_rel, std::fabs(got[k] - rf[k]) / rf[k]);
            }
        }
        zero_cases += (t.x == 0.0 && t.y == 0.0 && t.z == 0.0);
        partial_cases += (t.x > 0.0 && t.x < 1e-3);
        excess_cases += (v[17] > 0.0 || v[19] > 0.0);
        half_path_cases += (v[20] == 0.5 && (v[17] > 0.0 || v[19] > 0.0));
        variant_cases[static_cast<int>(v[11])] += (t.y > 1e-3 && t.y < 0.99);
    }
    check(excess_cases >= tcount / 4 && half_path_cases >= tcount / 16, "vectors include excess and the half path factor", excess_cases);
    for (int variant = 0; variant < 7; ++variant)
    {
        check(variant_cases[variant] >= 8, "every ozone variant has informative vectors (1e-3 < T < 0.99)", variant, variant_cases[variant]);
    }
    check(zero_cases > 0 && zero_cases < tcount / 2, "vectors include rays under the horizon", zero_cases);
    check(partial_cases > 0, "vectors include faint light", partial_cases);
    check(worst_smooth < 5e-8, "segmented quadrature equals the reference to 5e-8 where the profile is continuous", worst_smooth);
    std::printf("transmittance: %d GPU vectors, worst |C++ - GLSL| %.3g (scaled), worst |segmented - 200000-step reference| %.3g absolute, %.3g relative (T > 1e-3), %.3g where the profiles are continuous\n",
                tcount, worst_gpu, worst_ref, worst_ref_rel, worst_smooth);

    // the reference integral also holds on the real skies: noon to the horizon, from the ground and from 1.5 km
    {
        double worst = 0.0;
        for (const Sky& s : kSkies)
        {
            const LLAtmosphere a = LLAtmosphere::fromLegacy(legacy_of(s));
            for (double h : { 0.0, 1.5 })
            {
                for (double mu : { 1.0, 0.5, 0.2, 0.05, 0.01, 0.0, -0.01 })
                {
                    const Vec3 t = a.transmittance(h, mu, 0.0047), r = reference_transmittance(a, h, mu, 0.0047);
                    worst = std::fmax(worst, std::fmax(std::fabs(t.x - r.x), std::fmax(std::fabs(t.y - r.y), std::fabs(t.z - r.z))));
                }
            }
        }
        check(worst < 1e-6, "segmented quadrature equals the reference on the shipped skies", worst);
        std::printf("transmittance on the %d embedded skies: worst |segmented - reference| %.3g\n", static_cast<int>(sizeof(kSkies) / sizeof(kSkies[0])), worst);
    }

    // 3. noon identity: lin(source) T(0, 1) = lin(source e^-tau) per channel, for the colour the deferred pass receives
    {
        std::printf("noon identity (luminance of the light at the zenith: today / physical):\n");
        for (const Sky& s : kSkies)
        {
            const LegacySky sky = legacy_of(s);
            const LLAtmosphere a = LLAtmosphere::fromLegacy(sky);
            const Vec3 t = a.transmittance(0.0, 1.0, 0.0047);
            const Vec3 src = srgb_to_linear(sky.source);
            const Vec3 today = legacy_noon_light(sky);
            const Vec3 phys = { src.x * t.x, src.y * t.y, src.z * t.z };
            const bool column_capped = (a.clamped & LLAtmosphere::CLAMP_COLUMN) != 0;
            const double want[3] = { today.x, today.y, today.z }, got[3] = { phys.x, phys.y, phys.z };
            for (int k = 0; k < 3; ++k)
            {
                if (!near(got[k], want[k], 1e-6, 1e-9))
                {
                    std::printf("noon identity %s channel %d: physical %.9g today %.9g\n", s.name, k, got[k], want[k]);
                    check(false, "noon identity");
                }
            }
            const double yt = luminance(today), yp = luminance(phys);
            std::printf("  %-52s %.6f / %.6f  (%.4f)  column %.4f %.4f %.4f  excess %.4f %.4f %.4f%s\n", s.name, yt, yp, yt > 0.0 ? yp / yt : 1.0,
                        a.column.x, a.column.y, a.column.z, a.excess.x, a.excess.y, a.excess.z, column_capped ? "  (column capped, the excess carries the rest)" : "");
            check(in_unit(t), "noon T in [0,1]");
            check(a.slab_ext >= 0.0 && a.aerosol_ext >= 0.0, "non-negative coefficients");
            // the legacy airmass mirror equals the same at the zenith
            const Vec3 lt = legacy_transmittance(sky, 1.0, 1.0);
            check(near(lt.x * src.x, today.x, 1e-9, 1e-12) && near(lt.y * src.y, today.y, 1e-9, 1e-12) && near(lt.z * src.z, today.z, 1e-9, 1e-12),
                  "legacy_transmittance at the zenith", lt.x * src.x, today.x);
        }
        // the two light rigs among the embedded skies (kSkies[7], kSkies[8]) ask for almost no sun: their columns are capped, the excess keeps the noon sun
        const LLAtmosphere rig = LLAtmosphere::fromLegacy(legacy_of(kSkies[7])), rig2 = LLAtmosphere::fromLegacy(legacy_of(kSkies[8]));
        check((rig.clamped & LLAtmosphere::CLAMP_COLUMN) != 0 && rig.column.x == 8.0, "a light rig hits the column limit", rig.column.x);
        check(rig.excess.x > 0.0 && rig.excess.y > 0.0 && rig.excess.z > 0.0 && rig2.excess.x > 0.0 && rig2.excess.y > 0.0 && rig2.excess.z > 0.0, "the light rigs have excess in every channel", rig.excess.x);
        const LLAtmosphere def = LLAtmosphere::fromLegacy(legacy_of(kSkies[0]));
        check(def.clamped == 0 && def.excess.x == 0.0 && def.excess.y == 0.0 && def.excess.z == 0.0, "Default is not capped", def.clamped);
        // legacy_transmittance at half the path (the clouds): e^(-tau/2) at the zenith
        const LegacySky ds = legacy_of(kSkies[0]);
        const Vec3 half = legacy_transmittance(ds, 1.0, 0.5), full = legacy_transmittance(ds, 1.0, 1.0);
        check(half.x > full.x && half.z > full.z, "half the path is brighter", half.x, full.x);
        check(legacy_transmittance(ds, 0.1, 1.0).x < full.x, "legacy airmass falls towards the horizon");
    }

    // 4. ozone
    {
        const DensityProfile oz = ozone_profile();
        check(near(oz.density(5000.0), 0.0) && near(oz.density(10000.0), 0.0) && near(oz.density(17500.0), 0.5)
                  && near(oz.density(25000.0), 1.0) && near(oz.density(32500.0), 0.5) && near(oz.density(40000.0), 0.0) && near(oz.density(55000.0), 0.0),
              "Bruneton's ozone layer");
        const double column = detail::profile_column_km(oz, 60.0);
        check(std::fabs(column - 15.0) < 1e-9, "ozone column is 15 km x density", column);

        DensityProfile stored; // the default before AT0
        stored.count = 2;
        stored.layer[0].width_m = 25000.0;
        stored.layer[0].linear = -1.0f / 25000.0f;
        stored.layer[0].constant = -2.0f / 3.0f;
        stored.layer[1].linear = -1.0f / 15000.0f;
        stored.layer[1].constant = 8.0f / 3.0f;
        check(is_stored_ozone_default(stored), "the stored ozone default is recognised");
        check(stored.density(17500.0) == 0.0 && stored.density(24999.0) == 0.0, "the stored default is 0 in its rising zone");
        check(!is_stored_ozone_default(oz), "the fixed default is not the stored one");
        DensityProfile custom = oz;
        custom.layer[0].linear = 1.0 / 20000.0;
        check(!is_stored_ozone_default(custom), "a custom ozone layer is not the stored default");

        // the forms real skies hold (see validated_ozone): all are recognised and replaced by Bruneton's
        const DensityProfile validated = validated_stock_ozone(), validated_new = validated_ozone(1.0f / 15000.0f);
        check(is_stored_ozone_default(validated) && is_stored_ozone_default(validated_new), "the validated stock forms are recognised");
        check(validated.density(17500.0) == 0.0 && validated.density(30000.0) == 1.0, "a validated sky's ozone is flat (the premise of the replacement)");
        check(!is_stored_ozone_default(oz), "Bruneton's is not a stored form");
        DensityProfile near_custom = validated;
        near_custom.layer[0].linear = 1.0 / 15000.0 * 1.001; // a custom slope 0.1 % off the changed default
        check(!is_stored_ozone_default(near_custom), "a custom layer close to a stored form is kept");
        {
            // Default at 5 and 2 degrees: a sky holding any stored form gives the same light as the Bruneton profile
            const Vec3 want5[1] = { { 0.0574, 0.0155, 0.00183 } }, want2[1] = { { 0.00310, 0.00030, 5e-6 } };
            LegacySky base = legacy_of(kSkies[0]);
            base.source = { 0.8160, 0.8686, 1.0 }; // Default's sunlight normalised to a maximum of 1, as the deferred pass receives it
            base.absorption = oz;
            const LLAtmosphere ref = LLAtmosphere::fromLegacy(base);
            for (const DensityProfile* form : std::initializer_list<const DensityProfile*>{ &stored, &validated, &validated_new })
            {
                LegacySky s2 = base;
                s2.absorption = *form;
                const LLAtmosphere got = LLAtmosphere::fromLegacy(s2);
                for (double deg : { 5.0, 2.0, 0.0 })
                {
                    const double mu = std::sin(deg * 3.14159265358979323846 / 180.0);
                    const Vec3 a = got.transmittance(0.0, mu, 0.0047), b = ref.transmittance(0.0, mu, 0.0047);
                    check(near(a.x, b.x, 1e-12, 1e-15) && near(a.y, b.y, 1e-12, 1e-15) && near(a.z, b.z, 1e-12, 1e-15), "a stored ozone form lights like Bruneton's", a.y, b.y);
                }
            }
            const double s5 = std::sin(5.0 * 3.14159265358979323846 / 180.0), s2 = std::sin(2.0 * 3.14159265358979323846 / 180.0);
            const Vec3 t5 = ref.transmittance(0.0, s5, 0.0047), t2 = ref.transmittance(0.0, s2, 0.0047);
            std::printf("Default, Bruneton ozone: T at 5 deg %.4g %.4g %.4g, at 2 deg %.4g %.4g %.4g\n", t5.x, t5.y, t5.z, t2.x, t2.y, t2.z);
            check(near(t5.x, want5[0].x, 1e-2, 0.0) && near(t5.y, want5[0].y, 1e-2, 0.0) && near(t5.z, want5[0].z, 1e-2, 0.0), "Default T at 5 degrees", t5.y);
            check(near(t2.x, want2[0].x, 1e-2, 0.0) && near(t2.y, want2[0].y, 1e-2, 0.0) && near(t2.z, want2[0].z, 3e-1, 0.0), "Default T at 2 degrees", t2.y);
        }
        LegacySky sky = legacy_of(kSkies[0]);
        sky.absorption = stored;
        const LLAtmosphere replaced = LLAtmosphere::fromLegacy(sky);
        check(!is_stored_ozone_default(replaced.absorption) && near(replaced.absorption.density(17500.0), 0.5), "fromLegacy replaces the stored default");
        sky.absorption = custom;
        const LLAtmosphere kept = LLAtmosphere::fromLegacy(sky);
        check(kept.absorption.layer[0].linear == custom.layer[0].linear, "fromLegacy keeps a custom ozone layer");
        // ozone's share of the column: Earth's values times 15 km, never more than the column
        const LLAtmosphere atm = LLAtmosphere::fromLegacy(legacy_of(kSkies[0]));
        check(near(atm.ozone_ext.x * 15.0, 0.00065 * 15.0, 1e-9) && near(atm.ozone_ext.y, 0.001881, 1e-9), "Default carries Earth's ozone", atm.ozone_ext.x);
        const LLAtmosphere none = LLAtmosphere::fromLegacy(legacy_of(kSkies[9])); // zero density: no column, no ozone
        check(none.ozone_ext.x == 0.0 && (none.clamped & LLAtmosphere::CLAMP_OZONE) != 0, "ozone never exceeds the column", none.ozone_ext.x);
        check(near(none.transmittance(0.0, 1.0, 0.0047).y, 1.0, 1e-9), "zero density: the zenith is clear");
        // a zero exp_term with an overflowing exponent is 0, not NaN (0 * inf)
        DensityProfile overflow;
        overflow.layer[0].exp_scale = 1e-3;
        overflow.layer[0].constant = 0.25;
        check(overflow.density(1e6) == 0.25, "exp_term 0 ignores its exponent", overflow.density(1e6));
        overflow.layer[0].exp_term = 1.0;
        check(overflow.density(1e6) == 1.0 && overflow.density(0.0) == 1.0, "a huge exponent clamps to 1");
        overflow.layer[0].exp_scale = -1e-3;
        check(near(overflow.density(1e6), 0.25, 1e-12, 1e-12), "a huge negative exponent leaves the constant");
        // non-finite guards
        check(std::isfinite(moon_compensation(0.125, std::nan(""), 0.1)) && std::isfinite(moon_compensation(0.125, -0.2, std::nan(""))), "moon_compensation: NaN guards");
        check(std::isfinite(legacy_transmittance(legacy_of(kSkies[0]), 0.5, std::nan("")).x), "legacy_transmittance: NaN path_scale");
        AtmosphereBounds badb;
        badb.max_column = std::nan("");
        badb.max_ground_haze = -3.0;
        const LLAtmosphere bounded = LLAtmosphere::fromLegacy(legacy_of(kSkies[0]), badb);
        check(near(bounded.column.x, LLAtmosphere::fromLegacy(legacy_of(kSkies[0])).column.x, 1e-12, 1e-12) && std::isfinite(bounded.slab_ext), "bad bounds become the defaults", bounded.column.x);
    }

    // 5. bounds: hostile inputs give a finite T in [0, 1]
    {
        const double nan = std::nan(""), inf = HUGE_VAL;
        LegacySky bad = legacy_of(kSkies[0]);
        bad.blue_density = { nan, -1.0, inf };
        bad.haze_density = -3.0;
        bad.density_multiplier = 1e30;
        bad.max_y = nan;
        bad.distance_multiplier = inf;
        bad.source = { nan, -2.0, inf };
        const LLAtmosphere a = LLAtmosphere::fromLegacy(bad);
        check((a.clamped & LLAtmosphere::CLAMP_NON_FINITE) != 0, "non-finite inputs are noted");
        for (double mu : { -1.0, -0.1, 0.0, 0.3, 1.0, nan, inf })
        {
            for (double h : { -5.0, 0.0, 2.0, 70.0, 1e9, nan })
            {
                for (double d : { 0.0, 0.0047, nan, -1.0, 5.0 })
                {
                    const Vec3 t = a.transmittance(h, mu, d);
                    check(in_unit(t), "hostile arguments give T in [0,1]", t.x, mu);
                }
            }
        }
        LegacySky huge = legacy_of(kSkies[1]);
        huge.density_multiplier = 1e9;
        huge.max_y = 1e9;
        huge.distance_multiplier = 1e9;
        huge.haze_density = 1e9;
        const LLAtmosphere h = LLAtmosphere::fromLegacy(huge);
        check(h.column.x == 8.0 && (h.clamped & LLAtmosphere::CLAMP_COLUMN) && (h.clamped & LLAtmosphere::CLAMP_GROUND_HAZE) && std::isfinite(h.excess.x), "huge densities hit the bounds");
        check(in_unit(h.transmittance(0.0, 1.0, 0.0047)) && in_unit(h.transmittance(0.0, 0.0, 0.0047)), "huge densities: T in [0,1]");
        check(h.physical_transmittance(0.0, 1.0, 0.0047).x >= std::exp(-8.0) * (1.0 - 1e-6), "the column limit bounds the physical zenith", h.physical_transmittance(0.0, 1.0, 0.0047).x);
        check(h.transmittance(0.0, 1.0, 0.0047).x == 0.0 && h.excess.x > 1e6 && h.transmittance(0.0, 1.0, 0.0047, 0.0).x > 0.0, "huge densities: dark, with a finite excess", h.transmittance(0.0, 1.0, 0.0047).x);
        LegacySky planet = legacy_of(kSkies[0]);
        planet.planet_radius_km = 1e9;
        planet.top_radius_km = 1.0;
        const LLAtmosphere p = LLAtmosphere::fromLegacy(planet);
        check(p.planet_radius_km == 100000.0 && p.top_radius_km - p.planet_radius_km == 10.0 && (p.clamped & LLAtmosphere::CLAMP_PLANET_RADIUS)
                  && (p.clamped & LLAtmosphere::CLAMP_ATMOSPHERE_HEIGHT),
              "planet radius and atmosphere height are clamped", p.planet_radius_km);
        check(in_unit(p.transmittance(0.0, 0.5, 0.0047)), "a clamped planet: T in [0,1]");
        planet.planet_radius_km = 1.0;
        planet.top_radius_km = 1e9;
        const LLAtmosphere q = LLAtmosphere::fromLegacy(planet);
        check(q.planet_radius_km == 1000.0 && q.top_radius_km - q.planet_radius_km == 1000.0, "a small planet and a thick atmosphere are clamped");
        check(in_unit(q.transmittance(0.0, 0.5, 0.0047)) && in_unit(q.transmittance(500.0, -0.3, 0.0047)), "a small planet: T in [0,1]");
        // an empty profile falls back to the default
        const LLAtmosphere reference_default = LLAtmosphere::fromLegacy(legacy_of(kSkies[0]));
        LegacySky empty = legacy_of(kSkies[0]);
        empty.rayleigh = DensityProfile{};
        empty.mie = DensityProfile{};
        const LLAtmosphere e = LLAtmosphere::fromLegacy(empty);
        check((e.clamped & LLAtmosphere::CLAMP_PROFILE) != 0 && near(e.column.x, reference_default.column.x, 1e-9), "empty profiles are replaced", e.column.x);
    }

    // 6. monotonicity and the disc
    {
        const LLAtmosphere a = LLAtmosphere::fromLegacy(legacy_of(kSkies[0]));
        double last = 0.0;
        for (double mu = 0.0; mu <= 1.0; mu += 0.01) // falls towards the horizon: T rises with mu
        {
            const Vec3 t = a.transmittance(0.0, mu, 0.0);
            check(t.x >= last - 1e-12, "T rises with mu", t.x, last);
            last = t.x;
        }
        last = 0.0;
        for (double h = 0.0; h <= 60.0; h += 0.5) // rises with altitude
        {
            const Vec3 t = a.transmittance(h, 0.2, 0.0);
            check(t.y >= last - 1e-12, "T rises with altitude", t.y, last);
            last = t.y;
        }
        check(a.transmittance(60.0, 0.5, 0.0047).x == 1.0, "no air above the top");
        const double d = 0.0047;
        check(a.transmittance(0.0, -d - 1e-6, d).x == 0.0 && a.transmittance(0.0, -0.5, d).x == 0.0, "T is 0 below the horizon and the disc");
        const double mu_h = -std::sqrt(1.0 - (6360.0 / 6361.0) * (6360.0 / 6361.0));
        check(a.transmittance(1.0, mu_h - d - 1e-9, d).x == 0.0, "T is 0 below the horizon and the disc, at altitude");
        // continuity across the disc and the horizon, as a step relative to the largest T of the scan, where T is
        // large: thin air and a clear sky (T near 1), from the ground and from 3 km. A smooth disc of half width d
        // steps at most 1.5 x 1e-5 / (2 d) = 1.6e-3 per 1e-5 of mu; a jump of 1e-2 of T fails.
        {
            LLAtmosphere thin = LLAtmosphere::fromLegacy(legacy_of(kSkies[0]));
            thin.rayleigh_ext = { thin.rayleigh_ext.x * 1e-3, thin.rayleigh_ext.y * 1e-3, thin.rayleigh_ext.z * 1e-3 };
            thin.ozone_ext = { thin.ozone_ext.x * 1e-3, thin.ozone_ext.y * 1e-3, thin.ozone_ext.z * 1e-3 };
            thin.aerosol_ext *= 1e-3;
            thin.slab_ext *= 1e-3;
            thin.finalize();
            const LLAtmosphere clear = LLAtmosphere::fromLegacy(legacy_of(kSkies[9])); // zero density
            const LLAtmosphere* atms[3] = { &thin, &clear, &a };
            double worst_rel = 0.0, scan_max_thin = 0.0;
            for (int ai = 0; ai < 3; ++ai)
            {
                for (double h : { 0.0, 3.0 })
                {
                    const double mu_hor = -std::sqrt(h * (2.0 * 6360.0 + h)) / (6360.0 + h);
                    double prev = 0.0, tmax = 0.0, step_max = 0.0;
                    bool first = true;
                    for (double mu = mu_hor - d - 0.002; mu <= mu_hor + d + 0.002; mu += 1e-5)
                    {
                        const double t = atms[ai]->transmittance(h, mu, d).y;
                        if (!first)
                        {
                            step_max = std::fmax(step_max, std::fabs(t - prev));
                        }
                        tmax = std::fmax(tmax, t);
                        prev = t;
                        first = false;
                    }
                    if (ai < 2)
                    {
                        check(tmax > 0.5, "the continuity scan runs where T is large", tmax);
                        scan_max_thin = std::fmax(scan_max_thin, tmax);
                        worst_rel = std::fmax(worst_rel, step_max / tmax);
                    }
                    else
                    {
                        check(tmax > 0.0 && step_max / tmax < 0.02, "T is continuous across the disc (Default, relative to the scan's maximum)", step_max / tmax);
                    }
                }
            }
            std::printf("disc continuity: worst relative step %.3g per 1e-5 in mu (thin air and clear sky, scan max T %.3f)\n", worst_rel, scan_max_thin);
            check(worst_rel < 2.5e-3, "T is continuous across the disc", worst_rel);
        }
        check(a.transmittance(0.0, 1.0, 0.0).x > 0.0 && a.transmittance(0.0, 0.0, 0.0).x > 0.0 && a.transmittance(0.0, -1e-9, 0.0).x == 0.0,
              "a point disc: a step at the horizon");
    }

    // 6b. the review of #441: zero ozone column, clamp crossings of mixed profiles, the cap on the column ("sun off" light rigs)
    {
        // a zero ozone column (the 10 km height clamp over Bruneton's ozone, which starts at 10 km) has no ozone: no 0/0
        LegacySky planet = legacy_of(kSkies[0]);
        planet.planet_radius_km = 6360.0;
        planet.top_radius_km = 6360.0 + 10.0;
        const LLAtmosphere p = LLAtmosphere::fromLegacy(planet);
        check(p.ozone_ext.x == 0.0 && p.ozone_ext.y == 0.0 && p.ozone_ext.z == 0.0, "a zero ozone column gives zero ozone extinction", p.ozone_ext.x);
        for (double mu : { 1.0, 0.3, 0.0, -0.05 })
        {
            check(in_unit(p.transmittance(0.0, mu, 0.0047)) && in_unit(p.physical_transmittance(2.0, mu, 0.0047)), "zero ozone column: T finite in [0,1]", p.transmittance(0.0, mu, 0.0047).x, mu);
        }

        // stock profiles keep their cuts: no clamp crossings for the exponential ones, Bruneton's three kinks for ozone
        double kinks_m[16]; // metres
        check(rayleigh_profile().kinks(kinks_m, 16, 60000.0) == 0 && mie_profile().kinks(kinks_m, 16, 60000.0) == 0, "stock exponential profiles have no kinks");
        check(ozone_profile().kinks(kinks_m, 16, 60000.0) == 3, "Bruneton's ozone has three kinks", ozone_profile().kinks(kinks_m, 16, 60000.0));
        check(validated_stock_ozone().kinks(kinks_m, 16, 60000.0) == 1, "the validated stock ozone has its boundary only");

        // mixed profiles: exponential + linear + constant layers that cross density 0 or 1 inside a segment.
        // The segmented result equals the 200000-step reference to 1e-6 and the cuts sit on the crossings.
        auto layer_profile = [](double exp_term, double exp_scale, double linear, double constant) {
            DensityProfile q;
            q.layer[0].exp_term = exp_term;
            q.layer[0].exp_scale = exp_scale;
            q.layer[0].linear = linear;
            q.layer[0].constant = constant;
            return q;
        };
        // crossings: the clamp crossings (density 0 or 1) inside a layer; a two-layer profile also has its boundary as a cut
        struct Mixed { const char* name; DensityProfile profile; int crossings; } mixed[] = {
            { "crosses 1 near the ground (1.5 e^(-h/3 km))", layer_profile(1.5, -1.0 / 3000.0, 0.0, 0.0), 1 },
            { "crosses 0 (0.9 e^(-h/4 km) - h/20 km + 0.05)", layer_profile(0.9, -1.0 / 4000.0, -1.0 / 20000.0, 0.05), 1 },
            { "crosses 1 and 0 (ozone variant 3)", ozone_variant(3), 2 },
            { "a valley, two crossings of 0 and one of 1 (variant 4)", ozone_variant(4), 3 },
            { "a negative exponential term (-e^(-h/3 km) + h/10 km + 0.5; variant 5)", ozone_variant(5), 2 },
            { "two layers, the upper one mixed (variant 6)", ozone_variant(6), 1 },
        };
        double worst_mixed_all = 0.0;
        for (const Mixed& mp : mixed)
        {
            double cuts_m[16]; // metres
            const int found = mp.profile.kinks(cuts_m, 16, 60000.0);
            check(found == mp.crossings + (mp.profile.count > 1 ? 1 : 0), "the mixed profile's cuts are its crossings (and its boundary)", found, mp.crossings);
            for (int i = 0; i < found; ++i) // every cut is the boundary or a place where the unclamped expression is 0 or 1
            {
                const double h = cuts_m[i];
                const bool boundary = mp.profile.count > 1 && std::fabs(h - mp.profile.layer[0].width_m) < 1e-6;
                const DensityLayer& l = mp.profile.layer[(mp.profile.count > 1 && h >= mp.profile.layer[0].width_m) ? 1 : 0];
                const double v = DensityProfile::layer_value(l, h);
                check(boundary || std::fmin(std::fabs(v), std::fabs(v - 1.0)) < 1e-6, "a cut sits where the unclamped expression is 0 or 1", h, v);
            }
            LLAtmosphere a = LLAtmosphere::fromLegacy(legacy_of(kSkies[0]));
            a.absorption = mp.profile;
            a.ozone_ext = { 0.3, 0.2, 0.1 };
            a.finalize();
            double worst = 0.0;
            for (double h : { 0.0, 1.5 })
            {
                for (double mu : { 1.0, 0.5, 0.2, 0.05, 0.01, 0.0, -0.01 })
                {
                    const Vec3 t = a.transmittance(h, mu, 0.0047), r = reference_transmittance(a, h, mu, 0.0047);
                    check(in_unit(t), "mixed profile: T in [0,1]", t.x, mu);
                    worst = std::fmax(worst, std::fmax(std::fabs(t.x - r.x), std::fmax(std::fabs(t.y - r.y), std::fabs(t.z - r.z))));
                }
            }
            std::printf("mixed profile %-72s: %d cuts, worst |segmented - reference| %.3g\n", mp.name, found, worst);
            check(worst < 1e-6, "mixed profile: segmented equals the reference to 1e-6", worst);
            worst_mixed_all = std::fmax(worst_mixed_all, worst);
        }
        std::printf("mixed profiles: worst |segmented - reference| %.3g\n", worst_mixed_all);

        // "sun off": a channel whose column would exceed max_column keeps the sun it asks for through `excess`, so T is
        // continuous in the cap at every altitude, and nothing switches as the sun moves.
        {
            const double pi = 3.14159265358979323846;
            const double d = 0.0047;
            const LegacySky base = legacy_of(kSkies[0]);
            AtmosphereBounds wide;
            wide.max_column = 1e6;
            const LLAtmosphere real = LLAtmosphere::fromLegacy(base, wide);
            check(real.excess.x == 0.0 && real.excess.y == 0.0 && real.excess.z == 0.0 && real.clamped == 0u, "a wide cap: no excess");
            const double cols[3] = { real.column.x, real.column.y, real.column.z };
            const double lo_col = std::fmin(cols[0], std::fmin(cols[1], cols[2])), hi_col = std::fmax(cols[0], std::fmax(cols[1], cols[2]));
            check(hi_col > 1.2 * lo_col, "Default's channels have different columns", hi_col / lo_col);
            auto capped = [&](double cap) {
                AtmosphereBounds b;
                b.max_column = cap;
                return LLAtmosphere::fromLegacy(base, b);
            };

            // (a) continuity in the cap: a cap just over / just under a channel's column (1e-6 relative), at every altitude, along
            // the sun's whole path from the zenith to below the disc (0.01 degree steps). The two skies are the same sky: |dT|
            // below 1e-5 in every channel (T is relative to the source); and T has no jump along the sweep: a step of
            // under 1e-3 per 0.01 degree while the disc is not yet setting.
            std::printf("cap continuity (Default; caps at each channel's column x (1 -/+ 1e-6); sweep 90 degrees to the disc's lower edge, 0.01 degree steps):\n");
            std::printf("  altitude km   worst |T over - T under|   worst step of T per 0.01 deg (over / under)   scan max T\n");
            for (double h : { 0.0, 0.5, 1.0, 2.0, 4.0 })
            {
                const double mu_h = -std::sqrt(h * (2.0 * 6360.0 + h)) / (6360.0 + h);
                const double e_end = std::asin(std::fmax(mu_h - d, -1.0)) * 180.0 / pi;
                double worst_diff = 0.0, step_over = 0.0, step_under = 0.0, tmax = 0.0;
                for (double col : cols)
                {
                    const LLAtmosphere over = capped(col * (1.0 - 1e-6)), under = capped(col * (1.0 + 1e-6));
                    check(over.excess.x + over.excess.y + over.excess.z > 0.0 && (over.clamped & LLAtmosphere::CLAMP_COLUMN) != 0, "a cap just under a column leaves an excess");
                    Vec3 lo_t = {}, lu_t = {};
                    for (int i = 0;; ++i)
                    {
                        const double e = 90.0 - 0.01 * i;
                        if (e < e_end)
                        {
                            break;
                        }
                        const double mu = std::sin(e * pi / 180.0);
                        const Vec3 to = over.transmittance(h, mu, d), tu = under.transmittance(h, mu, d);
                        check(in_unit(to) && in_unit(tu), "cap sweep: T in [0,1]", to.x, e);
                        worst_diff = std::fmax(worst_diff, std::fmax(std::fabs(to.x - tu.x), std::fmax(std::fabs(to.y - tu.y), std::fabs(to.z - tu.z))));
                        tmax = std::fmax(tmax, std::fmax(tu.x, std::fmax(tu.y, tu.z)));
                        if (i > 0 && mu > mu_h + d)
                        {
                            step_over = std::fmax(step_over, std::fmax(std::fabs(to.x - lo_t.x), std::fmax(std::fabs(to.y - lo_t.y), std::fabs(to.z - lo_t.z))));
                            step_under = std::fmax(step_under, std::fmax(std::fabs(tu.x - lu_t.x), std::fmax(std::fabs(tu.y - lu_t.y), std::fabs(tu.z - lu_t.z))));
                        }
                        lo_t = to;
                        lu_t = tu;
                    }
                }
                std::printf("  %9.1f   %24.3g   %24.3g / %.3g   %.3f\n", h, worst_diff, step_over, step_under, tmax);
                check(worst_diff < 1e-5, "over vs under the cap is the same sky", worst_diff);
                check(step_over < 1e-3 && step_under < 1e-3, "no jump along the sweep", std::fmax(step_over, step_under));
            }

            // (b) a cap between Default's lowest and highest channel column: some channels capped, the others not. Per channel:
            // an uncapped channel is as the wide cap has it (the cap does not move the aerosol/Rayleigh split of the others),
            // a capped one keeps the noon sun the sky asks for, is never brighter than the wide cap's, and is dark at the horizon.
            std::printf("partial caps (Default, columns %.4f %.4f %.4f):\n", cols[0], cols[1], cols[2]);
            for (double cap : { 0.5 * (lo_col + hi_col), std::sqrt(lo_col * hi_col), 0.5 * (cols[0] + cols[1]), lo_col * 0.999, lo_col * 0.5 })
            {
                const LLAtmosphere part = capped(cap);
                int capped_channels = 0;
                double worst_free = 0.0, worst_noon = 0.0;
                const double exc[3] = { part.excess.x, part.excess.y, part.excess.z };
                for (int k = 0; k < 3; ++k)
                {
                    check(near(part.column.x, std::fmin(cols[0], cap), 1e-12, 1e-12) && near(part.column.z, std::fmin(cols[2], cap), 1e-12, 1e-12), "the capped column", part.column.x);
                    const bool is_capped = cols[k] > cap;
                    check((exc[k] > 0.0) == is_capped, "excess exactly where the column is above the cap", exc[k], k);
                    check(!is_capped || near(exc[k], cols[k] - cap, 1e-9, 1e-12), "excess is the column above the cap", exc[k], cols[k] - cap);
                    capped_channels += is_capped;
                    for (double h : { 0.0, 1.0, 4.0 })
                    {
                        for (double mu : { 1.0, 0.5, 0.2, 0.05, 0.01 })
                        {
                            const Vec3 tp = part.transmittance(h, mu, d), tw = real.transmittance(h, mu, d);
                            const double a = k == 0 ? tp.x : (k == 1 ? tp.y : tp.z), b = k == 0 ? tw.x : (k == 1 ? tw.y : tw.z);
                            if (!is_capped)
                            {
                                worst_free = std::fmax(worst_free, std::fabs(a - b));
                            }
                            else
                            {
                                check(a <= b * (1.0 + 1e-12), "a capped channel is never brighter than the wide cap's", a, b);
                                if (mu == 1.0 && h == 0.0)
                                {
                                    worst_noon = std::fmax(worst_noon, std::fabs(a - b));
                                }
                            }
                        }
                    }
                    if (is_capped)
                    {
                        const Vec3 th = part.transmittance(0.0, 0.0, d);
                        check((k == 0 ? th.x : (k == 1 ? th.y : th.z)) < 1e-6, "a capped channel is dark at the horizon", th.x);
                    }
                }
                std::printf("  cap %.4f: %d channel(s) capped; uncapped channels differ from the wide cap by %.3g; capped channels at noon by %.3g\n", cap, capped_channels, worst_free, worst_noon);
                check(worst_free < 1e-9, "an uncapped channel is as the wide cap has it", worst_free);
                check(worst_noon < 1e-6, "a capped channel keeps the noon sun the sky asks for", worst_noon);
                check(capped_channels > 0, "the partial caps cap something");
            }

            // (c) the path factor scales the excess only (the cloud light's thinner air in the physical model comes from taking T at the
            // cloud layer's altitude; the excess has none, so a rig's cloud light passes 0.5 to have today's half airmass): half the
            // path factor is the square root of the excess factor, e^(excess/2) brighter than the sun's at the zenith;
            // a sky without excess does not depend on it
            {
                const LLAtmosphere rig = capped(lo_col * 0.5);
                const Vec3 t1 = rig.transmittance(0.0, 1.0, d), t5 = rig.transmittance(0.0, 1.0, d, 0.5), t0 = rig.transmittance(0.0, 1.0, d, 0.0);
                check(near(t5.x / t1.x, std::exp(0.5 * rig.excess.x), 1e-12, 1e-12) && near(t5.z / t1.z, std::exp(0.5 * rig.excess.z), 1e-12, 1e-12), "path factor 0.5 halves the excess factor only", t5.x / t1.x);
                const Vec3 p0 = rig.physical_transmittance(0.0, 1.0, d);
                check(t0.x == p0.x && t0.y == p0.y && t0.z == p0.z, "path factor 0 leaves the physical sky", t0.x);
                check(real.transmittance(0.0, 0.3, d, 0.5).y == real.transmittance(0.0, 0.3, d).y, "a sky without excess ignores the path factor");
                check(in_unit(rig.transmittance(0.0, 0.0, d, std::nan(""))) && in_unit(rig.transmittance(1.0, 0.5, d, -3.0)) && in_unit(rig.transmittance(1.0, 0.5, d, HUGE_VAL)), "hostile path factors: T in [0,1]");
            }

            // (c2) the cap does not move the other channels: on Bangko 1 and Default, caps at 0.3, 0.5 and 0.9 of each channel's column;
            // every channel under the cap equals the uncapped sky, across mu down to the horizon, at the ground and at altitude
            {
                double worst = 0.0;
                int uncapped_checked = 0;
                for (int sky_index : { 0, 2 })
                {
                    const LegacySky sk = legacy_of(kSkies[sky_index]);
                    const LLAtmosphere free_sky = LLAtmosphere::fromLegacy(sk, wide);
                    const double fc[3] = { free_sky.column.x, free_sky.column.y, free_sky.column.z };
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        for (double frac : { 0.3, 0.5, 0.9 })
                        {
                            AtmosphereBounds b;
                            b.max_column = frac * fc[ch];
                            const LLAtmosphere cp = LLAtmosphere::fromLegacy(sk, b);
                            const double cc[3] = { cp.column.x, cp.column.y, cp.column.z };
                            const double ex[3] = { cp.excess.x, cp.excess.y, cp.excess.z };
                            for (int k = 0; k < 3; ++k)
                            {
                                // the model's column plus the excess is always the sky's column
                                check(near(cc[k] + ex[k], fc[k], 1e-12, 1e-12), "capped atmosphere: column + excess is the sky's column", cc[k] + ex[k], fc[k]);
                            }
                            for (double h : { 0.0, 1.5 })
                            {
                                for (double mu : { 1.0, 0.5, 0.2, 0.05, 0.01, 0.003, 0.0 })
                                {
                                    const Vec3 tp = cp.transmittance(h, mu, d), tf = free_sky.transmittance(h, mu, d);
                                    const double a[3] = { tp.x, tp.y, tp.z }, f[3] = { tf.x, tf.y, tf.z };
                                    for (int k = 0; k < 3; ++k)
                                    {
                                        if (ex[k] == 0.0)
                                        {
                                            worst = std::fmax(worst, std::fabs(a[k] - f[k]));
                                            ++uncapped_checked;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                std::printf("uncapped channels vs the uncapped sky (Default, Bangko 1; caps 0.3, 0.5, 0.9 of each column): %d values, worst %.3g\n", uncapped_checked, worst);
                check(uncapped_checked > 0 && worst < 1e-9, "an uncapped channel equals the uncapped sky", worst);
            }

            // (d) an atmosphere that is not finalized computes its cuts per call and gives the same T
            {
                LLAtmosphere hand = real;
                hand.cut_count = -1;
                const Vec3 a = hand.transmittance(0.0, 0.05, d), b = real.transmittance(0.0, 0.05, d);
                check(a.x == b.x && a.y == b.y && a.z == b.z, "an atmosphere without finalized cuts gives the same T", a.x, b.x);
            }

            // (e) the real rigs: the same sweep, T falls monotonically with the sun, dark at the horizon at any altitude
            for (int rig_index : { 7, 8 })
            {
                const LLAtmosphere rig = LLAtmosphere::fromLegacy(legacy_of(kSkies[rig_index]));
                double last = 0.0, step = 0.0;
                for (int i = 0; i <= 9000; ++i)
                {
                    const double e = 90.0 - 0.01 * i;
                    const Vec3 t = rig.transmittance(0.0, std::sin(e * pi / 180.0), d);
                    check(in_unit(t) && (i == 0 || t.y <= last * (1.0 + 1e-12)), "a light rig: T falls with the sun", t.y, e);
                    step = i > 0 ? std::fmax(step, std::fabs(t.y - last)) : step;
                    last = t.y;
                }
                check(step < 1e-5, "a light rig: no jump in the sweep", step);
                check(rig.transmittance(0.0, 0.0, d).y < 1e-12, "a light rig: dark at the horizon", rig.transmittance(0.0, 0.0, d).y);
                for (double h : { 0.0, 1.0, 4.0 })
                {
                    check(rig.transmittance(h, 0.0, d).y < 1e-12 && rig.transmittance(h, 0.5, d).y < 1e-3, "a light rig stays dark at any altitude", rig.transmittance(h, 0.5, d).y);
                }
            }
        }
    }

    // 7. the handover
    {
        // route_direction
        const Vec3 up = dir_of(60.0), low = dir_of(5.0, 40.0);
        check(angle_between_deg(route_direction(up, low, 0.0), up) < 1e-9 && angle_between_deg(route_direction(up, low, 1.0), low) < 1e-9, "route ends");
        const Vec3 sun = dir_of(0.0), moon = { -1.0, 0.0, 0.0 };
        for (double t = 0.0; t <= 1.0; t += 0.01)
        {
            const Vec3 v = route_direction(sun, moon, t);
            check(finite_unit(v) && v.z >= -1e-12, "an antipodal route is unit and passes over the zenith", v.z, t);
        }
        check(route_direction(sun, moon, 0.5).z > 0.99, "the antipodal route is over the zenith at t = 0.5", route_direction(sun, moon, 0.5).z);
        check(finite_unit(route_direction({ 0, 0, 0 }, { 0, 0, 0 }, 0.5)), "zero-length directions count as the zenith");
        check(route_direction({ 0, 0, 0 }, { 0, 0, 0 }, 0.5).z == 1.0, "zero-length directions are Z");
        // two lights above the horizon never route below it
        double lowest = 1.0;
        for (double e1 = 0.0; e1 <= 90.0; e1 += 5.0)
        {
            for (double e2 = 0.0; e2 <= 90.0; e2 += 5.0)
            {
                for (double az = 0.0; az <= 180.0; az += 15.0)
                {
                    for (double t = 0.0; t <= 1.0; t += 0.05)
                    {
                        lowest = std::fmin(lowest, route_direction(dir_of(e1), dir_of(e2, az), t).z);
                    }
                }
            }
        }
        check(lowest >= -1e-12, "two lights above the horizon never route below it", lowest);

        // moon_compensation and cross_fade
        const double depth = std::sin(6.0 * 3.14159265358979323846 / 180.0);
        check(moon_compensation(0.125, 0.5, depth) == MOON_PHYSICAL && moon_compensation(0.125, 0.0, depth) == MOON_PHYSICAL, "the moon is physical while the sun is up");
        check(near(moon_compensation(0.125, -depth, depth), 0.125, 1e-12) && near(moon_compensation(0.125, -0.5, depth), 0.125, 1e-12), "the moon is compensated by -6 degrees");
        check(near(moon_compensation(0.125, -0.5 * depth, depth), 0.5 * (MOON_PHYSICAL + 0.125), 1e-12), "the compensation is halfway at -3 degrees");
        const Vec3 cf = cross_fade({ 1.0, 0.8, 0.6 }, { 0.5, 0.5, 0.5 }, 0.25, 0.5);
        check(near(cf.x, 0.5 * 1.0 + 0.5 * 0.25 * 0.5) && near(cf.z, 0.5 * 0.6 + 0.5 * 0.25 * 0.5), "cross_fade");
        check(cross_fade({ 1, 1, 1 }, { 1, 1, 1 }, 0.3, 0.0).x == 1.0 && near(cross_fade({ 1, 1, 1 }, { 1, 1, 1 }, 0.3, 1.0).x, 0.3), "cross_fade ends");

        // the phases, on the Default sky, by prescribing the ratio through the moon's gain
        {
            PrimaryLightInputs in;
            in.sun_dir = dir_of(40.0);
            in.moon_dir = dir_of(20.0, 120.0);
            in.sun_T = { 1.0, 1.0, 1.0 };
            in.moon_T = { 1.0, 1.0, 1.0 };
            in.source = { 1.0, 1.0, 1.0 };
            const PrimaryLightParams p;
            struct Row { double r, shadow, t; } rows[] = {
                { 0.05, 1.0, 0.0 }, { 0.10, 1.0, 0.0 }, { 0.25, 0.5, 0.0 }, { 0.40, 0.0, 0.0 }, { 0.5, 0.0, 0.5 }, { 0.60, 0.0, 1.0 }, { 0.75, 0.5, 1.0 }, { 0.90, 1.0, 1.0 }, { 0.95, 1.0, 1.0 } };
            for (const Row& row : rows)
            {
                in.moon_gain = row.r / (1.0 - row.r); // E_m / E_s
                const PrimaryLight a = primary_light(in, p);
                check(near(a.ratio, row.r, 1e-12), "ratio", a.ratio, row.r);
                // smoothstep(0.1, 0.4, r) etc. at the middle of a window is 0.5
                check(near(a.moon_weight, row.t, 1e-9, 1e-12), "phase b weight", a.moon_weight, row.t);
                check(near(a.shadow_strength, row.shadow, 1e-9, 1e-12), "phase a/c shadow", a.shadow_strength, row.shadow);
            }
            // stateless: the same inputs, the same output
            in.moon_gain = 0.7;
            const PrimaryLight x = primary_light(in, p), y = primary_light(in, p);
            check(x.dir.x == y.dir.x && x.dir.z == y.dir.z && x.T.x == y.T.x && x.shadow_strength == y.shadow_strength && x.moon_weight == y.moon_weight, "primary_light is stateless");
            // the windows are clamped to 0.49
            PrimaryLightParams wide;
            wide.swing = 0.4;
            wide.fade = 0.4;
            in.moon_gain = 1.0;
            const PrimaryLight w = primary_light(in, wide);
            check(std::isfinite(w.shadow_strength) && w.shadow_strength >= 0.0 && w.shadow_strength <= 1.0 && std::isfinite(w.moon_weight), "wide windows are clamped");
        }

        // the dark fallback: both lights black, the sun's side while it is up
        {
            PrimaryLightInputs in;
            in.source = { 1.0, 1.0, 1.0 };
            in.moon_dir = dir_of(30.0, 100.0);
            in.moon_gain = 0.1;
            in.sun_dir = dir_of(20.0);
            PrimaryLight a = primary_light(in, PrimaryLightParams{});
            check(a.ratio == 0.0 && a.moon_weight == 0.0 && a.shadow_strength == 1.0 && angle_between_deg(a.dir, in.sun_dir) < 1e-9, "dark: the sun while it is up");
            in.sun_dir = dir_of(-20.0);
            a = primary_light(in, PrimaryLightParams{});
            check(a.ratio == 1.0 && a.moon_weight == 1.0 && angle_between_deg(a.dir, in.moon_dir) < 1e-9, "dark: the moon when the sun is down");
            in.sun_dir = dir_of(0.0);
            a = primary_light(in, PrimaryLightParams{});
            check(near(a.ratio, 0.5, 1e-12) && near(a.moon_weight, 0.5, 1e-9), "dark: even at the horizon", a.ratio);
            in.source = { 0.0, 0.0, 0.0 }; // a black source
            a = primary_light(in, PrimaryLightParams{});
            check(std::isfinite(a.ratio) && std::isfinite(a.shadow_strength) && finite_unit(a.dir), "a black source is safe");
            in.source = { 1.0, 1.0, 1.0 };
            in.sun_T = { std::nan(""), 2.0, -1.0 };
            in.moon_T = { HUGE_VAL, 0.0, 0.0 };
            in.moon_gain = std::nan("");
            a = primary_light(in, PrimaryLightParams{});
            check(in_unit(a.T) && std::isfinite(a.shadow_strength) && finite_unit(a.dir), "hostile transmittances are cleaned");
            in.sun_dir = { 0, 0, 0 };
            in.moon_dir = { 0, 0, 0 };
            a = primary_light(in, PrimaryLightParams{});
            check(finite_unit(a.dir), "zero-length directions are safe");
        }

        // a daytime moon is nothing; dawn mirrors dusk
        const LLAtmosphere atm = LLAtmosphere::fromLegacy(legacy_of(kSkies[0]));
        const Vec3 src = srgb_to_linear(legacy_of(kSkies[0]).source);
        for (double e : { 60.0, 30.0, 10.0 })
        {
            PrimaryLightInputs in;
            in.sun_dir = dir_of(e);
            in.moon_dir = dir_of(30.0, 120.0);
            in.sun_T = atm.transmittance(0.0, in.sun_dir.z, 0.0047);
            in.moon_T = atm.transmittance(0.0, in.moon_dir.z, 0.0045);
            in.source = src;
            in.moon_gain = 0.5 * moon_compensation(0.125, in.sun_dir.z, depth);
            const PrimaryLight a = primary_light(in, PrimaryLightParams{});
            check(a.ratio < 1e-4 && a.moon_weight == 0.0 && a.shadow_strength == 1.0, "a moon in the daytime sky is nothing", a.ratio, e);
        }
        for (double e : { 3.0, 0.5, -0.1, -1.0, -3.0, -6.0 })
        {
            for (int antipodal = 0; antipodal < 2; ++antipodal)
            {
                PrimaryLightInputs dusk, dawn;
                dusk.sun_dir = dir_of(e, 0.0);
                dusk.moon_dir = antipodal ? dir_of(-e, 180.0) : dir_of(30.0, 100.0);
                dusk.sun_T = atm.transmittance(0.0, dusk.sun_dir.z, 0.0047);
                dusk.moon_T = atm.transmittance(0.0, dusk.moon_dir.z, 0.0045);
                dusk.source = src;
                dusk.moon_gain = 0.5 * moon_compensation(0.125, dusk.sun_dir.z, depth);
                dawn = dusk; // the mirror image through the plane x = 0
                dawn.sun_dir.x = -dawn.sun_dir.x;
                dawn.moon_dir.x = -dawn.moon_dir.x;
                const PrimaryLight a = primary_light(dusk, PrimaryLightParams{}), b = primary_light(dawn, PrimaryLightParams{});
                check(near(a.dir.x, -b.dir.x, 1e-12, 1e-12) && near(a.dir.y, b.dir.y, 1e-12, 1e-12) && near(a.dir.z, b.dir.z, 1e-12, 1e-12)
                          && a.T.x == b.T.x && a.shadow_strength == b.shadow_strength && a.moon_weight == b.moon_weight,
                      "dawn mirrors dusk", e);
            }
        }

        // the sunset sweep, 0.01 degree steps, on a Default-like and a thin-air atmosphere
        {
            LegacySky thin = legacy_of(kSkies[0]);
            thin.density_multiplier *= 0.15;
            thin.haze_density *= 0.15;
            struct Case { const char* name; LegacySky sky; } cases[] = { { "Default", legacy_of(kSkies[0]) }, { "thin air", thin }, { "Sunset", legacy_of(kSkies[1]) }, { "Bangko 1", legacy_of(kSkies[2]) } };
            const char* moons[] = { "moon high", "moon opposite", "no moon" };
            std::printf("sunset sweep (0.01 degree steps, 10 to -10 degrees; moon_brightness 0.5, -3 EV):\n");
            for (const Case& c : cases)
            {
                const LLAtmosphere a = LLAtmosphere::fromLegacy(c.sky);
                for (int moon = 0; moon < 3; ++moon)
                {
                    const SweepResult r = sunset_sweep(a, c.sky.source, moon);
                    std::printf("  %-9s %-14s: shadow fades from %6.2f deg, max step: direction %.2f deg, shadow %.3f, light %.4f (sun up %.4f); light at the jumps %.4f\n", c.name,
                                moons[moon], r.fade_start_at, r.max_angle_step, r.max_shadow_step, r.max_light_step, r.max_light_step_sun_up, r.jump_light_level);
                    check(r.all_unit && r.all_finite, "sweep: unit directions, finite values");
                    check(r.jump_light_level < SWEEP_JUMP_LIGHT, "sweep: the ground light is dark at the jumps", r.jump_light_level);
                    check(r.max_light_step_sun_up < SWEEP_LIGHT_STEP, "sweep: light step while the sun disc is up", r.max_light_step_sun_up);
                    check(moon == 2 || r.min_dir_z >= -0.1, "sweep: the route stays above -0.1", r.min_dir_z);
                    check(moon == 2 || r.fade_start_at <= 1.0, "sweep: the sun's shadow lasts to the horizon", r.fade_start_at);
                }
            }
        }
    }

    // ================= stage AT1/L3: compute_primary_light, the chain the viewer runs once per frame =================
    {
        auto setup_of = [](const LegacySky& sky, const Vec3& sun, const Vec3& moon) {
            PrimaryLightSetup s;
            s.sky = sky;
            s.sun_dir = sun;
            s.moon_dir = moon;
            return s;
        };
        auto lin_of = [](const PrimaryLightRig& r) { return srgb_to_linear(r.color); }; // the linear ground light

        // 1. noon identity: the ground light at noon is today's lin(source e^-tau), physical and A/B, every embedded sky
        //    (the light rigs clamped at tau = 8 included); the sun's direction, full shadow, the sun's glow and size
        int noon_skies = 0;
        double worst_noon = 0.0, worst_noon_ab = 0.0;
        for (const Sky& sky : kSkies)
        {
            const LegacySky legacy = legacy_of(sky);
            const Vec3 want = legacy_noon_light(legacy);
            for (int physical = 1; physical >= 0; --physical)
            {
                PrimaryLightSetup s = setup_of(legacy, dir_of(90.0), dir_of(-90.0));
                s.physical = physical != 0;
                s.sun_scale = 1.5;
                s.moon_scale = 0.7;
                const PrimaryLightRig r = compute_primary_light(s);
                const Vec3 got = lin_of(r);
                const double err = std::fmax(std::fabs(got.x - want.x), std::fmax(std::fabs(got.y - want.y), std::fabs(got.z - want.z)));
                const double scale = std::fmax(1e-3, std::fmax(want.x, std::fmax(want.y, want.z)));
                double& worst = physical ? worst_noon : worst_noon_ab;
                worst = std::fmax(worst, err / scale);
                check(err <= 1e-6 * scale + 1e-9, "noon identity: the ground light is today's", err / scale, 0.0);
                check(angle_between_deg(r.light.dir, dir_of(90.0)) < 1e-9 && r.light.shadow_strength == 1.0 && r.light.moon_weight == 0.0,
                      "noon: the sun, full shadow", r.light.shadow_strength);
                check(r.haze_glow_factor == 1.0 && near(r.light_scale, 1.5, 1e-12, 1e-12), "noon: the sun's glow and size", r.haze_glow_factor, r.light_scale);
                check(r.shadow_visible || luminance(want) < 1e-4 * luminance(srgb_to_linear(legacy.source)), "noon: the shadow shows", luminance(want));
            }
            ++noon_skies;
        }
        std::printf("AT1 noon identity (%d skies, physical and A/B): worst relative error %.2e (physical), %.2e (A/B)\n", noon_skies, worst_noon, worst_noon_ab);

        // the clouds' sun at noon against the old cloud light (lin(source e^-tau/2 airmass) = legacy_transmittance(.., 1, 0.5)): the ground light
        // is identical at noon, the clouds' is not by design (physical: taken at the cloud layer; A/B: the same legacy formula, but the
        // dome's glow now fades with the cloud handover)
        {
            std::vector<double> ratios;
            for (const Sky& sky : kSkies)
            {
                const LegacySky legacy = legacy_of(sky);
                PrimaryLightSetup s = setup_of(legacy, dir_of(90.0), dir_of(-90.0));
                const PrimaryLightRig r = compute_primary_light(s);
                const double old_y = luminance(legacy_transmittance(legacy, 1.0, 0.5));
                if (old_y > 1e-3)
                {
                    ratios.push_back(luminance(r.cloud_T) / old_y);
                }
                check(r.cloud_moon_weight == 0.0, "noon: the clouds are the sun's", r.cloud_moon_weight);
            }
            std::sort(ratios.begin(), ratios.end());
            std::printf("AT1 clouds' sun at noon, physical / old: median %.3f, min %.3f, max %.3f (%d skies)\n", ratios[ratios.size() / 2], ratios.front(), ratios.back(),
                        static_cast<int>(ratios.size()));
        }

        // 2. the handover through the whole chain, 0.01 degree steps from +10 to -20 degrees: physical and A/B, four skies,
        //    a moon high, opposite, or none
        {
            LegacySky thin = legacy_of(kSkies[0]);
            thin.density_multiplier *= 0.15;
            thin.haze_density *= 0.15;
            struct Case { const char* name; LegacySky sky; } cases[] = { { "Default", legacy_of(kSkies[0]) }, { "thin air", thin }, { "Sunset", legacy_of(kSkies[1]) }, { "Bangko 1", legacy_of(kSkies[2]) } };
            const char* moons[] = { "moon high", "moon opposite", "no moon" };
            std::printf("AT1 handover sweep (0.01 degree steps, +10 to -20 degrees; -3 EV, 6 degrees):\n");
            for (const Case& c : cases)
            {
                for (int physical = 1; physical >= 0; --physical)
                {
                    for (int moon = 0; moon < 3; ++moon)
                    {
                        PrimaryLightRig last = {};
                        bool have_last = false;
                        double max_cloud_abs = 0.0, max_angle = 0.0, max_shadow = 0.0, max_glow = 0.0, max_cloud = 0.0, jump_level = 0.0, max_light_up = 0.0;
                        double min_z = 1.0;
                        bool all_ok = true;
                        const double ys = luminance(srgb_to_linear(c.sky.source));
                        for (int i = 0; i <= 3000; ++i)
                        {
                            const double e = 10.0 - 0.01 * i;
                            PrimaryLightSetup s = setup_of(c.sky, dir_of(e), Vec3{});
                            s.moon_dir = moon == 1 ? Vec3{ -s.sun_dir.x, -s.sun_dir.y, -s.sun_dir.z } : dir_of(30.0, 100.0);
                            s.moon_brightness = moon == 2 ? 0.0 : 0.5;
                            s.physical = physical != 0;
                            const PrimaryLightRig r = compute_primary_light(s);
                            const Vec3 ground = lin_of(r);
                            all_ok = all_ok && finite_unit(r.light.dir) && std::isfinite(ground.x + ground.y + ground.z + r.cloud_T.x + r.cloud_T.y + r.cloud_T.z)
                                     && std::isfinite(r.haze_glow_factor + r.light_scale) && r.light.shadow_strength >= 0.0 && r.light.shadow_strength <= 1.0
                                     && r.haze_glow_factor >= 0.0 && r.haze_glow_factor <= 1.0 + 1e-12 && r.cloud_T.x >= 0.0 && r.cloud_T.y >= 0.0 && r.cloud_T.z >= 0.0;
                            if (moon != 2)
                            {
                                min_z = std::min(min_z, r.light.dir.z);
                            }
                            if (have_last)
                            {
                                const double ang = angle_between_deg(last.light.dir, r.light.dir);
                                const double dsh = std::fabs(r.light.shadow_strength - last.light.shadow_strength);
                                const double dgl = std::fabs(r.haze_glow_factor - last.haze_glow_factor);
                                const double y0 = luminance(srgb_to_linear(last.color)), y1 = luminance(ground);
                                max_angle = std::max(max_angle, ang);
                                max_shadow = std::max(max_shadow, dsh);
                                max_glow = std::max(max_glow, dgl);
                                // what the cloud shader multiplies: the clouds' sun term times (1 - cloud_moon_blend)
                                const double yc0 = (1.0 - last.cloud_moon_weight) * luminance(last.cloud_T), yc1 = (1.0 - r.cloud_moon_weight) * luminance(r.cloud_T);
                                if (std::max(yc0, yc1) > 1e-3)
                                {
                                    max_cloud = std::max(max_cloud, std::fabs(yc1 - yc0) / std::max(yc0, yc1));
                                }
                                max_cloud_abs = std::max(max_cloud_abs, std::fabs(yc1 - yc0));
                                // a jump of the direction, the shadow or the glow only where the ground light is dark
                                if (ang > 5.0 || dsh > 0.25 || dgl > 0.25)
                                {
                                    jump_level = std::max(jump_level, std::max(y0, y1) / ys);
                                }
                                if (std::max(y0, y1) > 1e-3 * ys && e > 0.3)
                                {
                                    max_light_up = std::max(max_light_up, std::fabs(y1 - y0) / std::max(y0, y1));
                                }
                            }
                            last = r;
                            have_last = true;
                        }
                        std::printf("  %-9s %-8s %-13s: max step: direction %.2f deg, shadow %.3f, glow %.3f, cloud sun term step %.3f (abs %.4f), ground light (sun up) %.4f; light at the jumps %.4f\n",
                                    c.name, physical ? "physical" : "A/B", moons[moon], max_angle, max_shadow, max_glow, max_cloud, max_cloud_abs, max_light_up, jump_level);
                        // the clouds' sun term (1 - t_cloud) Y(cloud_T) per 0.01 degree: no pop (with t from the ground light it fell by 0.2-0.4 in one step)
                        check(max_cloud_abs < SWEEP_CLOUD_STEP, "AT1 sweep: the clouds' sun term is continuous", max_cloud_abs);
                        check(all_ok, "AT1 sweep: unit directions, finite values in range");
                        check(jump_level < SWEEP_JUMP_LIGHT, "AT1 sweep: direction, shadow and glow jump only where the ground light is dark", jump_level);
                        // the A/B airmass is today's exp(-x / mu): steep by itself in a thin-air sky near the horizon, not bounded here
                        check(!physical || max_light_up < SWEEP_LIGHT_STEP, "AT1 sweep: the ground light is continuous while the sun is up", max_light_up);
                        check(moon == 2 || min_z >= -0.1, "AT1 sweep: the route stays above -0.1", min_z);
                    }
                }
            }
        }

        // 3. continuity across the cuts r = 0.10 / 0.40 / 0.60 / 0.90 (default parameters): primary_light is continuous in
        //    the ratio, on both sides of each cut
        {
            const LegacySky sky = legacy_of(kSkies[0]);
            for (double cut : { 0.10, 0.40, 0.60, 0.90 })
            {
                PrimaryLightInputs in;
                in.sun_dir = dir_of(20.0);
                in.moon_dir = dir_of(25.0, 90.0);
                in.source = srgb_to_linear(sky.source);
                in.sun_T = { 1.0, 1.0, 1.0 };
                in.moon_gain = 1.0;
                auto at_ratio = [&](double r) {
                    const double q = r / (1.0 - r); // E_m / E_s for r = E_m / (E_s + E_m), the sun's T 1 and the same source
                    in.moon_T = { q, q, q };
                    return primary_light(in, PrimaryLightParams{});
                };
                const PrimaryLight below = at_ratio(cut - 1e-6), above = at_ratio(cut + 1e-6);
                check(std::fabs(below.shadow_strength - above.shadow_strength) < 1e-4 && std::fabs(below.moon_weight - above.moon_weight) < 1e-4
                          && angle_between_deg(below.dir, above.dir) < 0.05,
                      "the cuts are continuous (smoothstep, C1)", cut, std::fabs(below.shadow_strength - above.shadow_strength));
            }
        }

        // 4. the moon never exceeds its cap: the sun far below the horizon, the moon high, any setting
        {
            const LegacySky sky = legacy_of(kSkies[0]);
            const double y_src = luminance(srgb_to_linear(sky.source));
            double worst = 0.0, at_default = 0.0;
            for (double ev : { -8.0, -3.0, -2.0, -1.0, 0.0, 5.0, 100.0 })
            {
                for (double mb : { 0.0, 0.5, 1.0, 3.0 })
                {
                    PrimaryLightSetup s = setup_of(sky, dir_of(-30.0), dir_of(60.0, 100.0));
                    s.compensation_ev = ev;
                    s.moon_brightness = mb;
                    const PrimaryLightRig r = compute_primary_light(s);
                    const double ratio = luminance(lin_of(r)) / y_src;
                    worst = std::max(worst, ratio);
                    check(ratio <= 0.25 + 1e-9, "the moon never exceeds a quarter of the sun's colour", ratio, ev);
                    if (ev == -3.0 && mb == 0.5)
                    {
                        at_default = ratio;
                    }
                }
            }
            std::printf("AT1 moon cap: worst moonlight/source %.4f (cap 0.25); default (-3 EV, moon brightness 0.5, moon at 60 degrees) %.4f\n", worst, at_default);
            check(worst > 0.15, "the cap is nearly reached at -2 EV and moon brightness 1 (the moon's own transmittance is below 1)", worst);
            check(at_default > 0.0 && at_default < 0.0625 + 1e-9, "the default night is at most 1/16 of the source", at_default);
        }

        // 5. a sky with no moon (moon_brightness 0): no direct light at night, the sun's light and shadow by day
        for (const Sky& sky : kSkies)
        {
            PrimaryLightSetup s = setup_of(legacy_of(sky), dir_of(-30.0), dir_of(40.0, 100.0));
            s.moon_brightness = 0.0;
            const PrimaryLightRig night = compute_primary_light(s);
            check(night.color.x == 0.0 && night.color.y == 0.0 && night.color.z == 0.0 && !night.shadow_visible, "no moon: no direct light at night", night.color.x);
            check(finite_unit(night.light.dir) && std::isfinite(night.haze_glow_factor), "no moon: a finite unit direction at night");
            check(night.haze_glow_factor == 0.0, "no moon: no glow at night", night.haze_glow_factor);
            s.sun_dir = dir_of(25.0);
            const PrimaryLightRig day = compute_primary_light(s);
            check(day.light.shadow_strength == 1.0 && day.light.moon_weight == 0.0, "no moon: the day is the sun's");
        }

        // 6. nothing is NaN at the horizon, exactly on it or hostile: the sun and the moon at z = 0, +-1e-12, -0.0, NaN, infinity
        {
            const double zs[] = { 0.0, -0.0, 1e-12, -1e-12, 1e-9, -1e-9, 0.0047, -0.0047, 1.0, -1.0, std::nan(""), HUGE_VAL, -HUGE_VAL };
            const double scales[] = { 0.0, 1.0, 5.0, -1.0, 1e9, std::nan("") };
            int cases_run = 0;
            auto sane = [](const PrimaryLightRig& r) {
                return finite_unit(r.light.dir) && std::isfinite(r.color.x + r.color.y + r.color.z) && std::isfinite(r.cloud_T.x + r.cloud_T.y + r.cloud_T.z)
                    && std::isfinite(r.light.shadow_strength + r.haze_glow_factor + r.light_scale + r.light.moon_weight) && in_unit(r.light.T);
            };
            for (double sz : zs)
            {
                for (double mz : zs)
                {
                    for (double scale : scales)
                    {
                        for (int physical = 0; physical < 2; ++physical)
                        {
                            PrimaryLightSetup s = setup_of(legacy_of(kSkies[0]), { std::sqrt(std::fmax(0.0, 1.0 - sz * sz)), 0.0, sz },
                                                           { 0.0, std::sqrt(std::fmax(0.0, 1.0 - mz * mz)), mz });
                            s.physical = physical != 0;
                            s.sun_scale = s.moon_scale = scale;
                            ++cases_run;
                            check(sane(compute_primary_light(s)), "the horizon and hostile directions give finite values", sz, mz);
                        }
                    }
                }
            }
            const double bad[] = { std::nan(""), HUGE_VAL, -HUGE_VAL, -5.0, 1e30 };
            for (double v : bad)
            {
                PrimaryLightSetup s = setup_of(legacy_of(kSkies[1]), dir_of(0.0), dir_of(0.0, 180.0));
                s.params.swing = v;
                s.params.fade = v;
                s.compensation_ev = v;
                s.adaptation_depth_deg = v;
                s.moon_brightness = v;
                s.cloud_height_km = v;
                s.bounds.haze_slab_km = v;
                ++cases_run;
                check(sane(compute_primary_light(s)), "hostile settings give finite values", v);
            }
            std::printf("AT1 horizon and hostile inputs: %d cases, all finite and unit\n", cases_run);
        }

        // 7. night values: the moon's glow, size and cloud light; neither body up
        {
            const LegacySky sky = legacy_of(kSkies[0]);
            PrimaryLightSetup s = setup_of(sky, dir_of(-30.0), dir_of(45.0, 100.0));
            s.moon_scale = 2.0;
            s.moon_brightness = 0.8;
            const PrimaryLightRig n = compute_primary_light(s);
            check(n.light.moon_weight == 1.0 && near(n.haze_glow_factor, 0.8 * 0.25, 1e-12, 1e-12), "night: the moon's glow is moon_brightness / 4", n.haze_glow_factor);
            check(near(n.light_scale, 2.0, 1e-12, 1e-12) && angle_between_deg(n.light.dir, s.moon_dir) < 1e-9, "night: the moon's size and direction", n.light_scale);
            check(n.shadow_visible && n.light.shadow_strength == 1.0, "night: the moon casts a shadow");
            check(n.cloud_moon_weight == 1.0 && n.cloud_T.x == 0.0, "night: the clouds have no sun term", n.cloud_moon_weight);
            s.moon_dir = dir_of(-10.0, 100.0); // the moon is down too: both dark
            const PrimaryLightRig dark = compute_primary_light(s);
            check(dark.color.x == 0.0 && !dark.shadow_visible && finite_unit(dark.light.dir), "neither body up: no light, no shadow");
        }
    }

    if (failures)
    {
        std::printf("llatmosphericlight_test: %d FAILED\n", failures);
        return 1;
    }
    std::printf("llatmosphericlight_test: OK (%d light vectors, %d transmittance vectors)\n", count, tcount);
    return 0;
}
