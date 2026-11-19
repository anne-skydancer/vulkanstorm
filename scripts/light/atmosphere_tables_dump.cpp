// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Stage AT2-1: the C++ side of scripts/light/check_atmosphere_tables.py. Builds the clear-day base (llatmospherebase.h) with the given
// lights, as LLAtmosphereLUTs does, and writes the `Atmosphere` block the viewer uploads (fill_atmosphere_block) and the tables the GPU
// passes must reproduce, in double: T (256 x 64), Psi (32 x 32, from T), E (64 x 16, from T and Psi) and the sky view (192 x 108, the
// base's sky from the ground with the slab-split march and E for the ground, from all three; texel directions in the dome's frame,
// llskyfilter.h).
//
//   g++ -std=c++20 -O2 -I indra/newview scripts/light/atmosphere_tables_dump.cpp -o atmosphere_tables_dump
//   atmosphere_tables_dump out.bin sun_el sun_az moon_el moon_az src_r src_g src_b sunlight_scale moon_gain steps
//
// out.bin: the 512-byte block, then T, Psi, E and the sky view as little-endian doubles (rgb per texel, rows bottom to top).

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
} // namespace

int main(int argc, char** argv)
{
    if (argc != 12)
    {
        std::fprintf(stderr, "usage: %s out.bin sun_el sun_az moon_el moon_az src_r src_g src_b sunlight_scale moon_gain steps\n", argv[0]);
        return 2;
    }
    auto num = [&](int i) { return std::atof(argv[i]); };
    LLScatter s = clear_day_scatter();
    set_clear_day_lights(s, dir_of(num(2), num(3)), dir_of(num(4), num(5)), { num(6), num(7), num(8) }, num(9), num(10), body_disc_sin(1.0), body_disc_sin(1.0));
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
    const AtmosphereBlock b = fill_atmosphere_block(s, 0.0);
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
    for (int j = 0; j < LLSkyFilter::TABLE_H; ++j)
    {
        for (int i = 0; i < LLSkyFilter::TABLE_W; ++i)
        {
            put(f, s.sky_radiance(LLSkyFilter::dome_to_sky(LLSkyFilter::sky_view_dir(i, j)), 0.0, steps));
        }
    }
    std::fclose(f);
    return 0;
}
