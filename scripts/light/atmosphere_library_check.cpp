// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The offline library check of stage AT2-0 (driven by scripts/light/check_atmosphere_library.py, which writes its input): every shipped
// sky is translated by LLScatter::fromLegacy (llatmosphericlight.h) and its sky radiance is sampled; the measurable "no whiteout" is
// that outside 10 degrees of the light, no sky radiance exceeds the white-card limit lum(E_light) / pi (a white Lambertian card facing
// the light, with no atmosphere between: nothing the sky shows can be brighter than that without making light).
//
// The owner's decision (2026-11-11): a hazy sky's aureole (10 to 20 degrees from the light) may exceed the card, as physical (a thick
// forward-scattering haze near the sun is brighter than a white card), at most kAureoleMax times (a bound that catches a real whiteout);
// beyond 20 degrees the rule is strict. Aureole exceedances are listed as AUREOLE lines (the Python driver compares them to its expected
// set); any other exceedance is a WHITEOUT line and fails. Also reported: how
// many skies hit each bound (clamp bits), the largest multiple-scattering amplification, and the aerial-perspective march's error
// (12 steps against 1024) over the clouds' domain, relative to the larger of the reference and 1 % of the white-card radiance (a dark tail, as a sun on the horizon in dense fog, does not count).
//
//   g++ -std=c++20 -O2 -I indra/newview scripts/light/atmosphere_library_check.cpp -o atmosphere_library_check
//   atmosphere_library_check skies.txt [--acc]

#include "llatmosphericlight.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace LLAtmosphericLight;

namespace
{
constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;
constexpr double kAureoleDeg = 20.0;   // the aureole: 10 to 20 degrees from the light
constexpr double kAureoleMax = 2.0;    // an exceedance of the card by more than this is a whiteout even there

struct SkyRow
{
    std::string name;
    double v[20]; // sun rgb, blue_density rgb, haze, dm, my, dist, blue_horizon rgb, haze_horizon, ambient rgb, glow x, glow z, sun_angle
};

Vec3 dir_of(double el_deg, double az_deg = 0.0)
{
    const double e = el_deg * DEG, a = az_deg * DEG;
    return { std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e) };
}

double lum(const Vec3& c) { return luminance(c); }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: atmosphere_library_check skies.txt [--acc]\n");
        return 2;
    }
    bool acc = false;
    ScatterBounds sb; // experiments: --ms-max X, --albedo-m X, --fms X, --max-phase X; --exclude-deg X (default 10)
    double exclude_deg = 10.0;
    for (int i = 2; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--acc"))
        {
            acc = true;
        }
        else if (i + 1 < argc && !std::strcmp(argv[i], "--ms-max"))
        {
            sb.max_ms_scale = std::atof(argv[++i]);
        }
        else if (i + 1 < argc && !std::strcmp(argv[i], "--albedo-m"))
        {
            sb.max_aerosol_albedo = std::atof(argv[++i]);
        }
        else if (i + 1 < argc && !std::strcmp(argv[i], "--max-phase"))
        {
            sb.max_phase = std::atof(argv[++i]);
        }
        else if (i + 1 < argc && !std::strcmp(argv[i], "--exclude-deg"))
        {
            exclude_deg = std::atof(argv[++i]);
        }
        else if (i + 1 < argc && !std::strcmp(argv[i], "--fms"))
        {
            sb.max_f_ms = std::atof(argv[++i]);
        }
    }
    std::vector<SkyRow> skies;
    {
        std::FILE* f = std::fopen(argv[1], "r");
        if (!f)
        {
            std::printf("cannot open %s\n", argv[1]);
            return 2;
        }
        char line[2048];
        while (std::fgets(line, sizeof line, f))
        {
            SkyRow r;
            char* tab = std::strchr(line, '\t');
            if (!tab)
            {
                continue;
            }
            *tab = 0;
            r.name = line;
            char* p = tab + 1;
            int n = 0;
            for (; n < 20; ++n)
            {
                char* end = nullptr;
                r.v[n] = std::strtod(p, &end);
                if (end == p)
                {
                    break;
                }
                p = end;
            }
            if (n == 20)
            {
                skies.push_back(r);
            }
        }
        std::fclose(f);
    }

    int violations = 0, nonfinite = 0, aureole = 0, bits[12] = {};
    double max_ambient = 0.0;
    std::string max_ambient_name;
    double worst_ratio = 0.0, worst_amp = 1.0, worst_ratio_t = 0.0, worst_acc[3] = {};
    std::string worst_name, worst_amp_name, worst_acc_name[3];
    int acc_skies = 0, acc_over = 0, acc_over_rig = 0, rigs = 0, acc_over24 = 0, acc_over48 = 0;
    for (const SkyRow& r : skies)
    {
        const double* v = r.v;
        LegacySky sky;
        const double m = std::fmax(v[0], std::fmax(v[1], v[2]));
        const double k = m > 1.0 ? 1.0 / m : 1.0;
        sky.source = { std::min(std::max(v[0] * k, 0.0), 1.0), std::min(std::max(v[1] * k, 0.0), 1.0), std::min(std::max(v[2] * k, 0.0), 1.0) };
        sky.blue_density = { v[3], v[4], v[5] };
        sky.haze_density = v[6];
        sky.density_multiplier = v[7];
        sky.max_y = v[8];
        sky.distance_multiplier = v[9];
        sky.absorption.count = 2; // the validated stored ozone
        sky.absorption.layer[0].width_m = 25000.0;
        sky.absorption.layer[1].constant = 1.0;
        ScatterFields f;
        f.blue_horizon = { v[10], v[11], v[12] };
        f.haze_horizon = v[13];
        f.ambient = { v[14], v[15], v[16] };
        f.glow_x = v[17];
        f.glow_z = v[18];
        const double file_el = std::asin(std::sin(v[19])) / DEG;
        for (int c = 14; c < 17; ++c)
        {
            if (v[c] / sb.ref_ambient > max_ambient)
            {
                max_ambient = v[c] / sb.ref_ambient;
                max_ambient_name = r.name;
            }
        }

        LLScatter base = LLScatter::fromLegacy(sky, f, 0.0, {}, sb);
        for (int b = 0; b < 10; ++b)
        {
            bits[b] += (base.scatter_clamped >> b) & 1u;
        }
        TransmittanceLUT tl;
        MsLUT ms;
        base.build_t_lut(tl);
        base.t_lut = &tl;
        base.build_ms_lut(ms, 16, 16, 6, 12);
        base.ms_lut = &ms;
        // the largest multiple-scattering amplification of the sky
        for (double h : { 0.0, 0.5, 2.0 })
        {
            for (double mu : { -0.1, 0.3, 0.9 })
            {
                double fms = 0.0;
                base.ms_texel(h, mu, 4, 12, &fms);
                const double amp = 1.0 / (1.0 - std::min(fms, base.bounds.max_f_ms));
                if (amp > worst_amp)
                {
                    worst_amp = amp;
                    worst_amp_name = r.name;
                }
            }
        }

        // sun elevations: the file's, a few fixed, and a night with the moon up (AT1's gain)
        const double elevations[] = { file_el, 5.0, 30.0, 60.0, 90.0, -30.0 };
        for (double sun_el : elevations)
        {
            LLScatter sc = base;
            const double gain = 0.5 * moon_compensation(0.125, std::sin(sun_el * DEG), std::sin(6.0 * DEG));
            sc.lights.E[1] = sc.lights.E[0] * std::min(gain, 0.25);
            const Vec3 sun = dir_of(sun_el);
            sc.set_light_dirs(sun, { -sun.x, -sun.y, -sun.z });
            sc.lights.count = sun_el < -6.0 ? 2 : 1; // by day the moon's irradiance is 1e-6 of the sun's
            const int lit = sun_el < -6.0 ? 1 : 0;
            const Vec3 light = sc.lights.dir[lit];
            const double limit = lum(sc.lights.E[lit]) / PI;
            const double limit_t = lum(sc.lights.E[lit] * sc.light_T({ 0.0, 0.0, sc.atm.planet_radius_km }, 0.0, sc.lights, lit)) / PI;
            for (double el : { -3.0, 0.0, 3.0, 10.0, 30.0, 60.0, 90.0 })
            {
                for (double az : { 0.0, 60.0, 120.0, 180.0, 240.0, 300.0 })
                {
                    const Vec3 d = dir_of(el, az);
                    const double sep = std::acos(std::min(1.0, d.x * light.x + d.y * light.y + d.z * light.z)) / DEG;
                    if (sep < exclude_deg)
                    {
                        continue;
                    }
                    const Vec3 L = sc.sky_radiance(d, 0.0, 24);
                    if (!std::isfinite(L.x + L.y + L.z) || L.x < 0.0 || L.y < 0.0 || L.z < 0.0)
                    {
                        ++nonfinite;
                        std::printf("NON-FINITE %s sun %.1f dir %.0f/%.0f\n", r.name.c_str(), sun_el, el, az);
                        continue;
                    }
                    if (limit > 0.0)
                    {
                        const double ratio = lum(L) / limit;
                        if (ratio > worst_ratio)
                        {
                            worst_ratio = ratio;
                            worst_name = r.name;
                        }
                        if (ratio > 1.0)
                        {
                            if (sep < kAureoleDeg && ratio <= kAureoleMax)
                            {
                                ++aureole;
                                std::printf("AUREOLE\t%s\tsun %.1f dir %.0f/%.0f\t%.2f degrees from the light\t%.3fx\n", r.name.c_str(), sun_el, el, az, sep, ratio);
                            }
                            else
                            {
                                ++violations;
                                std::printf("WHITEOUT\t%s\tsun %.1f dir %.0f/%.0f: %.2f degrees from the light, radiance %.4f, limit %.4f (%.2fx)\n", r.name.c_str(), sun_el, el, az, sep, lum(L), limit, ratio);
                            }
                        }
                        if (limit_t > 0.01 * limit)
                        {
                            worst_ratio_t = std::max(worst_ratio_t, lum(L) / limit_t);
                        }
                    }
                }
            }
        }

        if (acc)
        {
            LLScatter sc = base;
            const double sun_el = file_el;
            sc.lights.E[1] = sc.lights.E[0] * std::min(0.5 * moon_compensation(0.125, std::sin(sun_el * DEG), std::sin(6.0 * DEG)), 0.25);
            const Vec3 sun = dir_of(sun_el);
            sc.set_light_dirs(sun, { -sun.x, -sun.y, -sun.z });
            double w[3] = {};
            const double dists[3] = { 2.0, 10.0, 40.0 };
            for (double el : { 0.0, 2.0, 5.0, 10.0, 30.0, 90.0 })
            {
                for (double az : { 0.0, 90.0, 180.0 })
                {
                    for (int q = 0; q < 3; ++q)
                    {
                        const double end_alt = std::sqrt(6360.0 * 6360.0 + dists[q] * dists[q] + 2.0 * 6360.0 * dists[q] * std::sin(el * DEG)) - 6360.0;
                        if (end_alt > 12.0)
                        {
                            continue;
                        }
                        Vec3 L12, T12, Lr, Tr;
                        sc.atmosphere_ap(dir_of(el, az), dists[q], 0.0, 12, L12, T12);
                        sc.atmosphere_ap(dir_of(el, az), dists[q], 0.0, 1024, Lr, Tr);
                        const double mx = std::max(detail::vmax(Lr), 1e-12), floor_l = std::max(1e-3 * mx, 1e-2 * lum(sc.lights.E[0]) / PI), a[3] = { L12.x, L12.y, L12.z }, b[3] = { Lr.x, Lr.y, Lr.z };
                        for (int c = 0; c < 3; ++c)
                        {
                            w[q] = std::max(w[q], std::fabs(a[c] - b[c]) / std::max(std::fabs(b[c]), floor_l));
                        }
                    }
                }
            }
            ++acc_skies;
            bool over = false;
            for (int q = 0; q < 3; ++q)
            {
                if (w[q] > worst_acc[q])
                {
                    worst_acc[q] = w[q];
                    worst_acc_name[q] = r.name;
                }
                over = over || w[q] > 0.01;
            }
            const bool is_rig = sc.atm.excess.x > 0.0 || sc.atm.excess.y > 0.0 || sc.atm.excess.z > 0.0; // a light rig: a sun asked for almost not at all
            rigs += is_rig;
            acc_over += over;
            // the same at 24 and 48 steps: how many steps the whole library would need
            for (int more : { 24, 48 })
            {
                double wm = 0.0;
                for (double el : { 0.0, 2.0, 5.0, 10.0, 30.0, 90.0 })
                {
                    for (double az : { 0.0, 90.0, 180.0 })
                    {
                        for (int q = 0; q < 3; ++q)
                        {
                            const double end_alt = std::sqrt(6360.0 * 6360.0 + dists[q] * dists[q] + 2.0 * 6360.0 * dists[q] * std::sin(el * DEG)) - 6360.0;
                            if (end_alt > 12.0)
                            {
                                continue;
                            }
                            Vec3 Lm, Tm, Lr, Tr;
                            sc.atmosphere_ap(dir_of(el, az), dists[q], 0.0, more, Lm, Tm);
                            sc.atmosphere_ap(dir_of(el, az), dists[q], 0.0, 1024, Lr, Tr);
                            const double mx = std::max(detail::vmax(Lr), 1e-12), floor_l = std::max(1e-3 * mx, 1e-2 * lum(sc.E_sun()) / PI), a[3] = { Lm.x, Lm.y, Lm.z }, b[3] = { Lr.x, Lr.y, Lr.z };
                            for (int c = 0; c < 3; ++c)
                            {
                                wm = std::max(wm, std::fabs(a[c] - b[c]) / std::max(std::fabs(b[c]), floor_l));
                            }
                        }
                    }
                }
                (more == 24 ? acc_over24 : acc_over48) += wm > 0.01;
            }
            acc_over_rig += over && is_rig;
        }
    }

    std::printf("library: %d skies\n", static_cast<int>(skies.size()));
    const char* names[10] = { "Rayleigh albedo", "aerosol albedo", "ms scale", "glow.x or glow.z clamped    ", "phase peak", "sunlight scale", "moon gain", "source", "non-finite", "anisotropy" };
    for (int b = 0; b < 10; ++b)
    {
        std::printf("  bound %-30s applied on %d skies\n", names[b], bits[b]);
    }
    std::printf("  largest ambient / Default's: %.2f (%s); the multiple-scattering scale bound is %.1f\n", max_ambient, max_ambient_name.c_str(), sb.max_ms_scale);
    std::printf("  largest multiple-scattering amplification %.2f (%s), bound 10\n", worst_amp, worst_amp_name.c_str());
    std::printf("  no whiteout (outside %.0f degrees of the light): largest sky luminance / (E_light lum / pi) = %.3f (%s); %d violations, %d aureole exceedances accepted (10 to 20 degrees, at most 2x), %d non-finite; against E T / pi where T > 1%% of 1: %.3f\n", exclude_deg, worst_ratio,
                worst_name.c_str(), violations, aureole, nonfinite, worst_ratio_t);
    if (acc)
    {
        std::printf("  aerial perspective, 12 steps vs 1024 over the clouds' domain (sun at the file's elevation): worst %.2f / %.2f / %.2f %% at 2 / 10 / 40 km (%s / %s / %s); %d of %d skies over 1 %% (%d of the %d light rigs among them); at 24 steps %d, at 48 steps %d\n",
                    100.0 * worst_acc[0], 100.0 * worst_acc[1], 100.0 * worst_acc[2], worst_acc_name[0].c_str(), worst_acc_name[1].c_str(), worst_acc_name[2].c_str(), acc_over, acc_skies, acc_over_rig, rigs, acc_over24, acc_over48);
    }
    return (violations || nonfinite) ? 1 : 0;
}
