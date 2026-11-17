// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Offline CPU bench for the atmosphere refresh study (doc/light/atmosphere-study.md, refresh of 2026-11-16): the main-thread cost of the
// renderer-free atmosphere per frame. Not part of the build or CI.
//
//   g++ -std=c++20 -O2 -DNDEBUG -I indra/newview scripts/light/bench_atmosphere_cpu.cpp -o bench_atmosphere_cpu.exe
//
// Times, on Default.xml, Foggy.xml and Sunset.xml:
//   1. compute_primary_light (AT1, today: once per main-thread frame in LLEnvironment::updatePrimaryLight);
//   2. LLScatter::fromLegacy (AT2-1 needs it when the sky's fields change: every frame of a day-cycle blend);
//   3. the hash of the translated atmosphere that decides whether the tables are rebuilt (stand-in: FNV-1a over the fields).
//
// With --dump it prints instead the translated atmosphere of each sky as one JSON line (the uniforms pa_atm, pa_*), which
// scripts/light/bench_atmosphere_passes.py reads so that the GPU passes march the viewer's own translation.

#include "llatmosphericlight.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

using namespace LLAtmosphericLight;

namespace
{
struct SkyEx
{
    const char* name;
    double sun[3], blue[3], haze, dm, my, dist, bh[3], hh, amb[3], gx, gz, sun_angle;
};

// rows copied from indra/newview/tests/llatmosphericscatter_test.cpp kSkies (Default.xml, Foggy.xml, Sunset.xml)
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

Vec3 dir_of(double el_deg, double az_deg)
{
    const double d = 3.14159265358979323846 / 180.0;
    return { std::cos(el_deg * d) * std::cos(az_deg * d), std::cos(el_deg * d) * std::sin(az_deg * d), std::sin(el_deg * d) };
}

template <class F> double median_us(F&& f, int reps)
{
    std::vector<double> t;
    t.reserve(reps);
    for (int i = 0; i < reps; ++i)
    {
        auto a = std::chrono::steady_clock::now();
        f(i);
        auto b = std::chrono::steady_clock::now();
        t.push_back(std::chrono::duration<double, std::micro>(b - a).count());
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}

volatile double g_sink = 0.0;
void dump_profile(const char* name, const DensityProfile& p)
{
    std::printf("\"%s\": [", name);
    for (int i = 0; i < p.count; ++i)
    {
        const DensityLayer& l = p.layer[i];
        std::printf("%s[%.9g, %.9g, %.9g, %.9g, %.9g]", i ? ", " : "", l.width_m, l.exp_term, l.exp_scale, l.linear, l.constant);
    }
    std::printf("]");
}

void dump(const SkyEx& s)
{
    const LLScatter sc = LLScatter::fromLegacy(legacy_of(s), fields_of(s), 0.0);
    const LLAtmosphere& a = sc.atm;
    std::printf("{\"name\": \"%s\", \"rayleigh_ext\": [%.9g, %.9g, %.9g], \"aerosol_ext\": %.9g, \"slab_ext\": %.9g, \"slab_top_km\": %.9g, "
                "\"ozone_ext\": [%.9g, %.9g, %.9g], \"excess\": [%.9g, %.9g, %.9g], \"planet_radius_km\": %.9g, \"top_radius_km\": %.9g, ",
                s.name, a.rayleigh_ext.x, a.rayleigh_ext.y, a.rayleigh_ext.z, a.aerosol_ext, a.slab_ext, a.slab_top_km, a.ozone_ext.x,
                a.ozone_ext.y, a.ozone_ext.z, a.excess.x, a.excess.y, a.excess.z, a.planet_radius_km, a.top_radius_km);
    dump_profile("rayleigh", a.rayleigh); std::printf(", ");
    dump_profile("mie", a.mie); std::printf(", ");
    dump_profile("absorption", a.absorption);
    std::printf(", \"rayleigh_scat\": [%.9g, %.9g, %.9g], \"aerosol_scat\": %.9g, \"slab_scat\": %.9g, \"ms_scale\": [%.9g, %.9g, %.9g], "
                "\"glow\": [%.9g, %.9g, %.9g], \"aniso\": %.9g, \"max_phase\": %.9g, \"E_sun\": [%.9g, %.9g, %.9g], \"sun_el_deg\": %.6g}\n",
                sc.rayleigh_scat.x, sc.rayleigh_scat.y, sc.rayleigh_scat.z, sc.aerosol_scat, sc.slab_scat, sc.ms_scale.x, sc.ms_scale.y,
                sc.ms_scale.z, sc.glow_x, sc.glow_z, sc.glow_norm, sc.anisotropy, sc.max_phase, sc.E_sun().x, sc.E_sun().y, sc.E_sun().z,
                s.sun_angle * 180.0 / 3.14159265358979323846);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--dump") == 0)
    {
        for (const SkyEx& s : kSkies)
        {
            dump(s);
        }
        return 0;
    }

    const int reps = 2001;
    for (const SkyEx& s : kSkies)
    {
        // a day cycle in motion: the sun moves a little every call, so nothing is cached between calls
        auto plight = [&](int i) {
            PrimaryLightSetup setup;
            setup.sky = legacy_of(s);
            const double el = -10.0 + 80.0 * (i % 997) / 997.0;
            setup.sun_dir = dir_of(el, 30.0);
            setup.moon_dir = dir_of(-el, 210.0);
            setup.cloud_source = setup.sky.source;
            setup.has_cloud_source = true;
            PrimaryLightRig rig = compute_primary_light(setup);
            g_sink += rig.color.x;
        };
        auto scatter = [&](int i) {
            LegacySky sky = legacy_of(s);
            sky.haze_density *= 1.0 + 1e-6 * (i % 7); // a blend: the fields change every frame
            LLScatter sc = LLScatter::fromLegacy(sky, fields_of(s), 0.01);
            g_sink += sc.atm.rayleigh_ext.x;
        };
        auto hash = [&](int i) {
            LegacySky sky = legacy_of(s);
            sky.haze_density *= 1.0 + 1e-6 * (i % 7);
            unsigned long long h = 1469598103934665603ull;
            const unsigned char* p = reinterpret_cast<const unsigned char*>(&sky);
            for (size_t k = 0; k < sizeof(sky); ++k)
            {
                h = (h ^ p[k]) * 1099511628211ull;
            }
            g_sink += double(h & 1);
        };
        for (int w = 0; w < 200; ++w)
        {
            plight(w); scatter(w); hash(w);
        }
        std::printf("%s: compute_primary_light %.2f us, LLScatter::fromLegacy %.2f us, fields hash %.3f us (median of %d)\n", s.name,
                    median_us(plight, reps), median_us(scatter, reps), median_us(hash, reps), reps);
    }
    return g_sink == 12345.0 ? 1 : 0;
}
