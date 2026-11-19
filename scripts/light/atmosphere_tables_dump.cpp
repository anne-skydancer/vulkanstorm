// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Stage AT2-1: the C++ side of scripts/light/check_atmosphere_tables.py. Builds the clear-day base (llatmospherebase.h) with the given
// lights, as LLAtmosphereLUTs does, and writes the `Atmosphere` block the viewer uploads (fill_atmosphere_block) and the tables the GPU
// passes must reproduce, in double: T (256 x 64), Psi (32 x 32, from T), E (64 x 16, from T and Psi) and the sky view (192 x 108, the
// base's sky from the ground with the slab-split march and E for the ground, from all three; texel directions in the dome's frame,
// llskyfilter.h); then the EEP filter's tables for the sky in `inputs` (LLSkyFilter::Inputs, whitespace-separated in their order): A and
// B (1 x 108, a row per elevation) and the cast-light level with this sky view as the physical sky.
//
//   g++ -std=c++20 -O2 -I indra/newview scripts/light/atmosphere_tables_dump.cpp -o atmosphere_tables_dump
//   atmosphere_tables_dump out.bin sun_el sun_az moon_el moon_az src_r src_g src_b sunlight_scale moon_gain steps inputs
//
// out.bin: the 512-byte block, then T, Psi, E and the sky view (rgb per texel, rows bottom to top), A and B (rgb per row, A's rows then
// B's), and irr_p, irr_w, k, defined: little-endian doubles.

#include "llatmospherebase.h"
#include "llskyfilter.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace LLAtmosphericLight;

namespace
{
Vec3 dir_of(double el_deg, double az_deg)
{
    const double d = 3.14159265358979323846 / 180.0, e = el_deg * d, a = az_deg * d;
    return { std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e) };
}

void put(std::FILE* f, const Vec3& v)
{
    const double x[3] = { v.x, v.y, v.z };
    std::fwrite(x, sizeof(double), 3, f);
}

bool read_inputs(const char* path, LLSkyFilter::Inputs& in)
{
    std::FILE* f = std::fopen(path, "r");
    if (!f)
    {
        return false;
    }
    double v[32]; // the 32 numbers of LLSkyFilter::Inputs
    for (double& x : v)
    {
        if (std::fscanf(f, "%lf", &x) != 1)
        {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    const double* p = v;
    auto v3 = [&]() { const Vec3 r = { p[0], p[1], p[2] }; p += 3; return r; };
    in.lightnorm = v3();
    in.sunlight_color = v3();
    in.moonlight_color = v3();
    in.ambient_color = v3();
    in.blue_horizon = v3();
    in.blue_density = v3();
    in.haze_horizon = *p++;
    in.haze_density = *p++;
    in.cloud_shadow = *p++;
    in.density_multiplier = *p++;
    in.max_y = *p++;
    in.glow = v3();
    in.sun_moon_blend = *p++;
    in.sun_glow_norm = v3();
    in.sky_hdr_scale = *p++;
    in.classic_mode = static_cast<int>(*p++);
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 13)
    {
        std::fprintf(stderr, "usage: %s out.bin sun_el sun_az moon_el moon_az src_r src_g src_b sunlight_scale moon_gain steps inputs\n", argv[0]);
        return 2;
    }
    auto num = [&](int i) { return std::atof(argv[i]); };
    LLSkyFilter::Inputs in;
    if (!read_inputs(argv[12], in))
    {
        std::fprintf(stderr, "cannot read the sky's inputs from %s\n", argv[12]);
        return 4;
    }
    LLScatter s = clear_day_scatter();
    set_clear_day_lights(s, dir_of(num(2), num(3)), dir_of(num(4), num(5)), { num(6), num(7), num(8) }, num(9), num(10));
    const int steps = std::atoi(argv[11]);

    TransmittanceLUT t;
    MsLUT ms;
    SkyIrradianceLUT e;
    s.build_t_lut(t);
    s.t_lut = &t;
    s.build_ms_lut(ms);
    s.ms_lut = &ms;
    s.build_e_lut(e);
    s.e_lut = &e;

    std::FILE* f = std::fopen(argv[1], "wb");
    if (!f)
    {
        return 3;
    }
    const AtmosphereBlock b = fill_atmosphere_block(s);
    std::fwrite(&b, sizeof(b), 1, f);
    for (const Vec3& v : t.texel)
    {
        put(f, v);
    }
    for (const Vec3& v : ms.texel)
    {
        put(f, v);
    }
    for (const Vec3& v : e.texel)
    {
        put(f, v);
    }
    std::vector<Vec3> sky(static_cast<size_t>(LLSkyFilter::TABLE_W) * LLSkyFilter::TABLE_H);
    for (int j = 0; j < LLSkyFilter::TABLE_H; ++j)
    {
        for (int i = 0; i < LLSkyFilter::TABLE_W; ++i)
        {
            sky[static_cast<size_t>(j) * LLSkyFilter::TABLE_W + i] = s.sky_radiance(LLSkyFilter::dome_to_sky(LLSkyFilter::sky_view_dir(i, j)), 0.0, steps);
            put(f, sky[static_cast<size_t>(j) * LLSkyFilter::TABLE_W + i]);
        }
    }
    std::vector<Vec3> A(LLSkyFilter::TABLE_H), B(LLSkyFilter::TABLE_H);
    for (int j = 0; j < LLSkyFilter::TABLE_H; ++j)
    {
        LLSkyFilter::haze_ab(in, LLSkyFilter::row_dir(j), A[j], B[j]);
    }
    for (const Vec3& v : A)
    {
        put(f, v);
    }
    for (const Vec3& v : B)
    {
        put(f, v);
    }
    const LLSkyFilter::CastLight k = LLSkyFilter::cast_light_level(in, [&](int i, int j) { return sky[static_cast<size_t>(j) * LLSkyFilter::TABLE_W + i]; });
    const double kv[4] = { k.irr_p, k.irr_w, k.k, k.defined ? 1.0 : 0.0 };
    std::fwrite(kv, sizeof(double), 4, f);
    std::fclose(f);
    return 0;
}
