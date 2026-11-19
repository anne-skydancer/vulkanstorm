// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Offline accuracy check for the atmosphere refresh study (doc/light/atmosphere-study.md, refresh 2026-11-16): the sky view's march.
// Before stage AT2-1, LLScatter::sky_radiance (and its GLSL twin pa_sky_radiance) marched UNIFORM midpoint steps from the observer to
// the top of the atmosphere or the ground, with no split at the 0.25 km haze slab, unlike atmosphere_ap; AT2-1 replaced it with the
// slab-split march (finding R2, decision D1). This keeps the record of what the uniform march cost near the horizon and in fog: the
// uniform march (a local copy of the old sky_radiance, uniform_sky below) at 24, 32 and 64 steps, and the slab-split march (today's
// sky_radiance) at the same counts, against the slab-split march at 4096 steps (the reference; a uniform march is not one, see the
// AT2-0 doc's R-ACC). The uniform march at 4096 steps is reported against it too. Today's gate on the shipped march is
// llatmosphericscatter_test.cpp's test 10b. Not part of the build or CI.
//
//   g++ -std=c++20 -O2 -DNDEBUG -I indra/newview scripts/light/check_skyview_steps.cpp -o check_skyview_steps.exe

#include "llatmosphericlight.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace LLAtmosphericLight;

namespace
{
struct SkyEx
{
    const char* name;
    double sun[3], blue[3], haze, dm, my, dist, bh[3], hh, amb[3], gx, gz, sun_angle;
};

// rows of indra/newview/tests/llatmosphericscatter_test.cpp kSkies
const SkyEx kSkies[] = {
    { "Default", { 0.7342105507850647, 0.7815789580345154, 0.8999999761581421 }, { 0.24475815892219543, 0.44872328639030457, 0.7599999904632568 }, 0.699999988079071, 0.00017999998817685992, 1605.0, 0.800000011920929, { 0.4954838156700134, 0.4954838156700134, 0.6399999856948853 }, 0.1899999976158142, { 1.0499999523162842, 1.0499999523162842, 1.0499999523162842 }, 5.0, -0.47999998927116394, 1.991769790649414 },
    { "Foggy", { 0.5399999618530273, 0.5399999618530273, 0.5399999618530273 }, { 0.14522500336170197, 0.3999969959259033, 0.8000019788742065 }, 4.0, 0.0003499999875202775, 600.0, 4.0, { 0.4307021498680115, 0.8539466857910156, 1.0 }, 0.6499999761581421, { 0.32999998331069946, 0.32999998331069946, 0.32999998331069946 }, 3.799999952316284, -0.5, 1.5707963705062866 },
    { "Sunset", { 2.8385701179504395, 2.8385701179504395, 2.8385701179504395 }, { 0.14522500336170197, 0.3999969959259033, 0.8000019788742065 }, 0.699999988079071, 0.0004600000102072954, 562.5, 1.0, { 0.10767599940299988, 0.21348699927330017, 0.25 }, 0.1599999964237213, { 1.0199999809265137, 0.809999942779541, 0.809999942779541 }, 5.0, -0.47999998927116394, 3.066194772720337 },
};

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

constexpr double DEG = 3.14159265358979323846 / 180.0;

Vec3 dir_of(double el, double az)
{
    return { std::cos(el * DEG) * std::cos(az * DEG), std::cos(el * DEG) * std::sin(az * DEG), std::sin(el * DEG) };
}

// error per channel relative to the larger of the reference and 1e-3 of its largest channel (the R-ACC rule of the core doc)
double rel_err(const Vec3& a, const Vec3& ref)
{
    const double m = std::max({ ref.x, ref.y, ref.z });
    auto e = [&](double x, double r) { return std::fabs(x - r) / std::max(std::fabs(r), 1e-3 * m); };
    return std::max({ e(a.x, ref.x), e(a.y, ref.y), e(a.z, ref.z) });
}

// the old uniform sky march (LLScatter::sky_radiance before AT2-1): `steps` uniform midpoint steps to the ground or the top, plus the
// ground at ground_albedo lit by the lights' T and the sky irradiance
Vec3 uniform_sky(const LLScatter& sc, const Vec3& d, double h0, int steps)
{
    steps = std::min(std::max(steps, 1), AP_MAX_STEPS);
    const double tg = sc.ground_distance(h0, d);
    const double tmax = sc.ray_length(h0, d);
    Vec3 Lin, T = { 1.0, 1.0, 1.0 };
    sc.march_seg(h0, d, 0.0, tmax, steps, 1.0, false, sc.lights, Lin, T);
    if (tg >= 0.0)
    {
        const LLScatter::PathPoint p = sc.path_point(h0, d, tg);
        const Vec3 n = p.x * (1.0 / detail::len(p.x));
        Vec3 E;
        for (int l = 0; l < std::min(std::max(sc.lights.count, 0), 2); ++l)
        {
            const double mu = detail::dot(n, sc.lights.dir[l]);
            const Vec3 sky_e = sc.e_lut ? sc.e_lut->at(0.0, mu) : sc.sky_irradiance_texel(0.0, mu);
            E += sc.lights.E[l] * (sc.light_T(p.x, 0.0, sc.lights, l) * std::max(mu, 0.0) + sky_e);
        }
        Lin += T * E * (sc.bounds.ground_albedo / detail::SC_PI);
    }
    return Lin;
}

// the slab-split march: since AT2-1 this is sky_radiance itself (atmosphere_ap to the end of the ray, plus the ground term)
Vec3 sky_split(const LLScatter& sc, const Vec3& d, double h0, int steps)
{
    return sc.sky_radiance(d, h0, steps);
}
} // namespace

int main()
{
    const double heights[] = { 0.0, 0.03 };
    const double els[] = { -2.0, -0.5, 0.1, 0.3, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 90.0 };
    const double azs[] = { 0.0, 90.0, 180.0 };
    std::printf("sky | steps | uniform: worst (where), median | slab-split: worst, median\n");
    for (const SkyEx& s : kSkies)
    {
        const double sun_el = std::asin(std::sin(s.sun_angle)) / DEG;
        LLScatter sc = LLScatter::fromLegacy(legacy_of(s), fields_of(s), 0.0);
        sc.set_light_dirs(dir_of(sun_el, 0.0), dir_of(-sun_el, 180.0));
        sc.lights.count = 1;
        TransmittanceLUT tl;
        sc.build_t_lut(tl);
        sc.t_lut = &tl;
        MsLUT ms;
        sc.build_ms_lut(ms);
        sc.ms_lut = &ms;
        SkyIrradianceLUT el;
        sc.build_e_lut(el);
        sc.e_lut = &el;
        for (int steps : { 24, 32, 48, 64, 4096 })
        {
            double worst_u = 0.0, wh = 0.0, we = 0.0, wa = 0.0;
            std::vector<double> all_u, all_s;
            for (double h : heights)
                for (double e : els)
                    for (double a : azs)
                    {
                        const Vec3 d = dir_of(e, a);
                        const Vec3 ref = sky_split(sc, d, h, 4096);
                        const double eu = rel_err(uniform_sky(sc, d, h, steps), ref);
                        all_u.push_back(eu);
                        all_s.push_back(rel_err(sky_split(sc, d, h, steps), ref));
                        if (eu > worst_u)
                        {
                            worst_u = eu; wh = h; we = e; wa = a;
                        }
                    }
            std::sort(all_u.begin(), all_u.end());
            std::sort(all_s.begin(), all_s.end());
            std::printf("%s (sun %.1f) | %d | %.2f %% (h %.2f km, el %.1f, az %.0f), %.2f %% | %.2f %%, %.2f %%\n", s.name, sun_el, steps,
                        worst_u * 100.0, wh, we, wa, all_u[all_u.size() / 2] * 100.0, all_s.back() * 100.0, all_s[all_s.size() / 2] * 100.0);
        }
    }
    return 0;
}
