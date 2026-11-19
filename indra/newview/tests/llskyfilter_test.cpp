// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of the EEP filter's C++ twin, llskyfilter.h (stage AT2-1; no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llskyfilter_test.cpp
//
//   - the factored form is skyV.glsl's colour: A + B haze_glow equals the direct evaluation for every glow (1e-12 relative), and A and
//     B depend on the direction through its elevation only (the tables are one column, a row per elevation);
//   - the GPU vectors (llskyfilter_vectors.inc, scripts/light/gen_sky_filter_vectors.py): skyV.glsl itself (today's dome colour, its own
//     glow) and skyFilterF.glsl's A and B rows are the twin's to 1e-4 relative, its cast-light level to 1e-3, with a physical sky and
//     with a black one (a night with the moon down: k undefined); the .inc holds the SHA-256 of skyV.glsl, skyFilterF.glsl and srgbF.glsl
//     and the test checks them;
//   - the sky-view grid: texel to direction and back; the cast-light weights integrate cos(zenith) over the hemisphere to pi;
//   - a filter that is the physical sky has k = 1; a black filtered sky, or a black physical one, has no k;
//   - the change keys: A and B's (table_key) moves with any change of what they read and with nothing else; k's (inputs_key) moves with any
//     input and holds still under 1e-3 of it, and through a sunrise k moves by under 1 % between equal keys.

#include "llskyfilter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace LLSkyFilter;
using LLAtmosphericLight::Vec3;

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

bool near(double a, double b, double rel, double abs_tol)
{
    return std::fabs(a - b) <= rel * std::fmax(std::fabs(a), std::fabs(b)) + abs_tol;
}

const double kSky[][33] = {
#define SF_SKY(...) { __VA_ARGS__ },
#define SF_HAZE(...)
#define SF_AB(...)
#define SF_K(...)
#define SF_SHA(...)
#include "llskyfilter_vectors.inc"
#undef SF_SKY
#undef SF_HAZE
#undef SF_AB
#undef SF_K
#undef SF_SHA
};

const double kHaze[][7] = {
#define SF_SKY(...)
#define SF_HAZE(...) { __VA_ARGS__ },
#define SF_AB(...)
#define SF_K(...)
#define SF_SHA(...)
#include "llskyfilter_vectors.inc"
#undef SF_SKY
#undef SF_HAZE
#undef SF_AB
#undef SF_K
#undef SF_SHA
};

const double kAB[][8] = {
#define SF_SKY(...)
#define SF_HAZE(...)
#define SF_AB(...) { __VA_ARGS__ },
#define SF_K(...)
#define SF_SHA(...)
#include "llskyfilter_vectors.inc"
#undef SF_SKY
#undef SF_HAZE
#undef SF_AB
#undef SF_K
#undef SF_SHA
};

const double kK[][6] = {
#define SF_SKY(...)
#define SF_HAZE(...)
#define SF_AB(...)
#define SF_K(...) { __VA_ARGS__ },
#define SF_SHA(...)
#include "llskyfilter_vectors.inc"
#undef SF_SKY
#undef SF_HAZE
#undef SF_AB
#undef SF_K
#undef SF_SHA
};

struct ShaEntry
{
    const char* path;
    const char* hex;
};
const ShaEntry kSha[] = {
#define SF_SKY(...)
#define SF_HAZE(...)
#define SF_AB(...)
#define SF_K(...)
#define SF_SHA(path, hex) { path, hex },
#include "llskyfilter_vectors.inc"
#undef SF_SKY
#undef SF_HAZE
#undef SF_AB
#undef SF_K
#undef SF_SHA
};

Inputs inputs_of(const double* v)
{
    const double* p = v + 1;
    auto v3 = [&]() { const Vec3 r = { p[0], p[1], p[2] }; p += 3; return r; };
    auto v1 = [&]() { return *p++; };
    Inputs in;
    in.lightnorm = v3();
    in.sunlight_color = v3();
    in.moonlight_color = v3();
    in.ambient_color = v3();
    in.blue_horizon = v3();
    in.blue_density = v3();
    in.haze_horizon = v1();
    in.haze_density = v1();
    in.cloud_shadow = v1();
    in.density_multiplier = v1();
    in.max_y = v1();
    in.glow = v3();
    in.sun_moon_blend = v1();
    in.sun_glow_norm = v3();
    in.sky_hdr_scale = v1();
    in.classic_mode = static_cast<int>(v1());
    return in;
}

// the synthetic physical sky of the cast-light vectors (gen_sky_filter_vectors.py sf_p)
Vec3 sf_p(int i, int j)
{
    const double u = i / (TABLE_W - 1.0), v = j / (TABLE_H - 1.0);
    return { 0.02 + 0.01 * u, 0.03 + 0.02 * v, 0.05 + 0.01 * u * v };
}

// SHA-256 (FIPS 180-4), for the drift check of the GLSL files against the vectors
std::string sha256_hex(const std::string& data)
{
    static const unsigned K[64] = { 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354,
        0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2 };
    unsigned h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    std::string m = data;
    const unsigned long long bits = static_cast<unsigned long long>(data.size()) * 8ull;
    m.push_back(static_cast<char>(0x80));
    while (m.size() % 64 != 56)
    {
        m.push_back(0);
    }
    for (int i = 7; i >= 0; --i)
    {
        m.push_back(static_cast<char>((bits >> (8 * i)) & 0xff));
    }
    auto rotr = [](unsigned x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64)
    {
        unsigned w[64];
        for (int i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<unsigned>(static_cast<unsigned char>(m[off + 4 * i])) << 24) | (static_cast<unsigned>(static_cast<unsigned char>(m[off + 4 * i + 1])) << 16)
                 | (static_cast<unsigned>(static_cast<unsigned char>(m[off + 4 * i + 2])) << 8) | static_cast<unsigned>(static_cast<unsigned char>(m[off + 4 * i + 3]));
        }
        for (int i = 16; i < 64; ++i)
        {
            const unsigned s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        unsigned a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            const unsigned S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[i] + w[i];
            const unsigned S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i)
    {
        std::snprintf(out + 8 * i, 9, "%08x", h[i]);
    }
    return std::string(out, 64);
}

// a shader file (relative to ../app_settings/shaders/), line ends normalised as the generator does; "" if unreadable
std::string shader_hash(const char* rel)
{
    std::string path = __FILE__;
    const size_t slash = path.find_last_of("/\\");
    path = path.substr(0, slash == std::string::npos ? 0 : slash) + "/../app_settings/shaders/" + rel;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
    {
        return "";
    }
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
    {
        data.append(buf, n);
    }
    std::fclose(f);
    std::string norm;
    for (size_t i = 0; i < data.size(); ++i)
    {
        if (data[i] == '\r')
        {
            norm.push_back('\n');
            if (i + 1 < data.size() && data[i + 1] == '\n')
            {
                ++i;
            }
        }
        else
        {
            norm.push_back(data[i]);
        }
    }
    return sha256_hex(norm);
}
} // namespace

int main()
{
    const int nsky = static_cast<int>(sizeof(kSky) / sizeof(kSky[0]));

    // 1. the GLSL files the vectors were made from
    for (const ShaEntry& e : kSha)
    {
        const std::string h = shader_hash(e.path);
        check(!h.empty() && h == e.hex, "the GLSL file is the one the vectors were made from (run scripts/light/gen_sky_filter_vectors.py)");
        if (h != e.hex)
        {
            std::printf("  %s\n", e.path);
        }
    }
    check(sizeof(kSha) / sizeof(kSha[0]) == 3, "the vectors name skyV.glsl, skyFilterF.glsl and srgbF.glsl");

    // 2. the factored form is skyV's colour for any glow (the twin's own arithmetic, double)
    {
        double worst = 0.0;
        int n = 0;
        for (int s = 0; s < nsky; ++s)
        {
            const Inputs in = inputs_of(kSky[s]);
            for (int j = 0; j < TABLE_H; j += 7)
            {
                for (int i = 0; i < TABLE_W; i += 23)
                {
                    const Vec3 d = sky_view_dir(i, j);
                    Vec3 A, B;
                    haze_ab(in, d, A, B);
                    for (double hg : { 0.0, 0.25, 1.0, 13.0, 1e3 })
                    {
                        const Vec3 c = haze_color(in, d, hg);
                        if (!(std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z)))
                        {
                            check(A.x == 0.0 && B.x == 0.0, "a colour that is not finite has A = B = 0");
                            continue;
                        }
                        const double v[3] = { A.x + B.x * hg, A.y + B.y * hg, A.z + B.z * hg }, r[3] = { c.x, c.y, c.z };
                        for (int k = 0; k < 3; ++k)
                        {
                            worst = std::max(worst, std::fabs(v[k] - r[k]) / std::max(std::fabs(r[k]), 1e-12));
                        }
                        ++n;
                    }
                }
            }
        }
        std::printf("A + B haze_glow against skyV's colour: %d samples, worst %.2g relative\n", n, worst);
        check(worst < 1e-12, "A + B haze_glow is skyV's colour", worst);
    }

    // 3. skyV.glsl itself on the GPU: its vary_HazeColor is the twin's colour with the twin's glow
    {
        double worst = 0.0;
        int n = 0, nan_pairs = 0;
        for (const auto& v : kHaze)
        {
            const Inputs in = inputs_of(kSky[static_cast<int>(v[0])]);
            // skyV normalises rel_pos before the glow; the table's directions are unit already
            const double len = std::sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
            const Vec3 d = { v[1] / len, v[2] / len, v[3] / len };
            const Vec3 c = haze_color(in, d, haze_glow(in, d));
            const double g[3] = { v[4], v[5], v[6] }, r[3] = { c.x, c.y, c.z };
            for (int k = 0; k < 3; ++k)
            {
                if (std::isnan(r[k]) || std::isnan(g[k]))
                {
                    check(std::isnan(r[k]) == std::isnan(g[k]), "skyV and the twin agree where the colour is not a number (max_y 0)", r[k], g[k]);
                    ++nan_pairs;
                    continue;
                }
                const double e = std::fabs(r[k] - g[k]) / (std::fabs(g[k]) + 1e-2);
                worst = std::max(worst, e);
                if (!near(r[k], g[k], 1e-4, 1e-6))
                {
                    std::printf("  skyV vector, sky %d channel %d: C++ %.9g GLSL %.9g\n", static_cast<int>(v[0]), k, r[k], g[k]);
                    check(false, "skyV.glsl's colour is the twin's");
                }
            }
            ++n;
        }
        std::printf("skyV.glsl on the GPU: %d directions over %d skies (%d channels not a number in both), worst %.2g relative\n", n, nsky, nan_pairs, worst);
        check(n >= 8 * nsky, "every sky has its skyV vectors", n);
    }

    // 4. skyFilterF.glsl's A and B rows are the twin's
    {
        double worst = 0.0;
        int n = 0;
        for (const auto& v : kAB)
        {
            const Inputs in = inputs_of(kSky[static_cast<int>(v[0])]);
            Vec3 A, B;
            haze_ab(in, row_dir(static_cast<int>(v[1])), A, B);
            const double c[6] = { A.x, A.y, A.z, B.x, B.y, B.z };
            for (int k = 0; k < 6; ++k)
            {
                worst = std::max(worst, std::fabs(c[k] - v[2 + k]) / (std::fabs(v[2 + k]) + 1e-2));
                if (!near(c[k], v[2 + k], 1e-4, 1e-6))
                {
                    std::printf("  A/B row %d of sky %d, value %d: C++ %.9g GLSL %.9g\n", static_cast<int>(v[1]), static_cast<int>(v[0]), k, c[k], v[2 + k]);
                    check(false, "skyFilterF.glsl's A and B are the twin's");
                }
            }
            ++n;
        }
        std::printf("skyFilterF.glsl A and B: %d rows, worst %.2g relative\n", n, worst);
        check(n >= 8 * nsky, "every sky has its A/B vectors", n);
    }

    // 5. the cast-light level on the GPU (passes 2 and 3; the physical sky the synthetic one, or black: a night with the moon down) is
    //    the twin's
    {
        double worst = 0.0;
        int black = 0;
        for (const auto& v : kK)
        {
            const Inputs in = inputs_of(kSky[static_cast<int>(v[0])]);
            const bool dark = v[1] > 0.5;
            const CastLight c = dark ? cast_light_level(in, [](int, int) { return Vec3{}; }) : cast_light_level(in, sf_p);
            check(c.defined == (v[5] > 0.5), "the cast-light level is defined where the GPU's is", c.defined, v[5]);
            check(near(c.irr_p, v[2], 1e-4, 1e-7), "the physical sky's irradiance is the GPU's", c.irr_p, v[2]);
            check(near(c.irr_w, v[3], 1e-3, 1e-7), "the filtered sky's irradiance is the GPU's", c.irr_w, v[3]);
            if (dark)
            {
                check(!c.defined, "a black physical sky (the moon down) has no cast-light level", c.irr_w);
                ++black;
            }
            if (c.defined)
            {
                worst = std::max(worst, std::fabs(c.k - v[4]) / v[4]);
                check(near(c.k, v[4], 1e-3, 1e-9), "the cast-light level k is the GPU's", c.k, v[4]);
            }
        }
        std::printf("cast-light level: %d cases (%d with a black physical sky), worst k %.2g relative\n", static_cast<int>(sizeof(kK) / sizeof(kK[0])), black, worst);
        check(black >= nsky, "every sky has a black-physical-sky case", black);
    }

    // 6. the grid: texel to direction and back; the weights integrate cos(zenith) over the hemisphere to pi
    {
        double worst = 0.0;
        for (int j = 0; j < TABLE_H; ++j)
        {
            for (int i = 0; i < TABLE_W; ++i)
            {
                const Vec3 d = sky_view_dir(i, j);
                double u, v;
                sky_view_uv(d, u, v);
                const double eu = (j == 0 || j == TABLE_H - 1) ? 0.0 : std::min(std::fabs(u - i / (TABLE_W - 1.0)), 1.0 - std::fabs(u - i / (TABLE_W - 1.0)));
                worst = std::max(worst, std::max(eu, std::fabs(v - j / (TABLE_H - 1.0))));
            }
        }
        check(worst < 1e-9, "sky_view_uv inverts sky_view_dir", worst);
        double sum = 0.0;
        for (int j = TABLE_H / 2; j < TABLE_H; ++j)
        {
            sum += row_weight(j) * (TABLE_W - 1);
        }
        std::printf("the grid's cosine weights over the upper hemisphere: %.5f (pi %.5f)\n", sum, PI);
        check(near(sum, PI, 2e-3, 0.0), "the cast-light weights integrate cos(zenith) to pi", sum);
        // the horizon rows are within 0.01 degrees of it, the zenith row at it
        check(std::fabs(latitude(TABLE_H / 2)) < 0.01 * PI / 180.0 && std::fabs(latitude(TABLE_H - 1) - 0.5 * PI) < 1e-12, "the rows reach the horizon and the zenith");
    }

    // 7. a filter that is the physical sky has k = 1; a black filtered sky has no k, nor a physical sky under a millionth of it; A and B
    //    depend on the direction through its elevation only (the tables' rows)
    {
        const Inputs in = inputs_of(kSky[0]);
        const CastLight self = cast_light_level(in, [&](int i, int j) {
            const Vec3 d = sky_view_dir(i, j);
            Vec3 A, B;
            haze_ab(in, d, A, B);
            return look(in, A, B, haze_glow(in, d));
        });
        check(self.defined && near(self.k, 1.0, 1e-12, 0.0), "k = 1 when the physical sky is the filtered one", self.k);
        Inputs black = in;
        black.max_y = 0.0;
        const CastLight none = cast_light_level(black, sf_p);
        check(!none.defined && none.k == 0.0, "a black filtered sky has no cast-light level", none.irr_w);
        const CastLight lit = cast_light_level(in, sf_p);
        const double scale = 0.5 * K_MIN * lit.irr_w / lit.irr_p;
        const CastLight faint = cast_light_level(in, [&](int i, int j) { return sf_p(i, j) * scale; });
        check(!faint.defined && faint.irr_p > 0.0, "a physical sky under K_MIN of the filtered one has no cast-light level", faint.irr_p);
        check(!cast_light_defined(NAN, 1.0) && !cast_light_defined(1.0, NAN) && cast_light_defined(1.0, 1.0), "not a number is not defined");
        double worst = 0.0;
        for (int s = 0; s < nsky; ++s)
        {
            const Inputs si = inputs_of(kSky[s]);
            for (int j = 0; j < TABLE_H; ++j)
            {
                Vec3 Ar, Br;
                haze_ab(si, row_dir(j), Ar, Br);
                for (int i = 0; i < TABLE_W; i += 5)
                {
                    Vec3 A, B;
                    haze_ab(si, sky_view_dir(i, j), A, B);
                    const double a[6] = { A.x, A.y, A.z, B.x, B.y, B.z }, r[6] = { Ar.x, Ar.y, Ar.z, Br.x, Br.y, Br.z };
                    for (int k = 0; k < 6; ++k)
                    {
                        worst = std::max(worst, std::fabs(a[k] - r[k]) / std::max(std::fabs(r[k]), 1e-12));
                    }
                }
            }
        }
        std::printf("A and B along any azimuth against the row's: worst %.2g relative\n", worst);
        check(worst < 1e-12, "A and B depend on the elevation only", worst);
    }

    // 8. the change key: a change of 1 % moves it, a change of 1e-5 does not
    {
        const Inputs in = inputs_of(kSky[0]);
        const uint64_t k0 = inputs_key(in);
        Inputs a = in;
        a.sunlight_color.y *= 1.01;
        Inputs b = in;
        b.sunlight_color.y *= 1.0 + 1e-5;
        Inputs c = in;
        c.sun_glow_norm.x += 0.01;
        Inputs d = in;
        d.classic_mode = 1 - in.classic_mode;
        check(inputs_key(a) != k0 && inputs_key(c) != k0 && inputs_key(d) != k0, "the key moves with the inputs");
        check(inputs_key(b) == k0 || quantise(std::log(in.sunlight_color.y), 1e-3) != quantise(std::log(b.sunlight_color.y), 1e-3),
              "the key holds still under 1e-5 of an input (unless it sits on a step)");
        int moved = 0;
        for (int i = 0; i < 200; ++i)
        {
            Inputs e = in;
            e.haze_density *= 1.0 + 1e-4 * i;
            moved += inputs_key(e) != k0;
        }
        check(moved > 0 && moved < 200, "the key holds still under 1e-3 of an input and moves beyond it", moved);
        check(quantise(NAN, 1e-3) != quantise(0.0, 1e-3) && quantise(INFINITY, 1e-3) != quantise(-INFINITY, 1e-3) && quantise(INFINITY, 1e-3) != quantise(1.0, 1e-3),
              "non-finite inputs have keys of their own");
        // only the light's y is read (by A and B); its x and z move neither key
        Inputs f = in;
        f.lightnorm.y += 2e-3;
        Inputs g = in;
        g.lightnorm.x += 0.05;
        g.lightnorm.z -= 0.05;
        check(inputs_key(f) != k0 && inputs_key(g) == k0 && table_key(f) != table_key(in) && table_key(g) == table_key(in),
              "the keys follow the light's y only");
    }

    // 9. S1: the sky's own tables follow their inputs exactly. A nudge of a part in 1e7 of any input A and B read moves table_key, and
    //    nothing they do not read moves it (the glow, the sun's glow direction, the lighting pass's decode, the light's x and z): equal
    //    table keys are the same A and B, so the tables never lag the sky (a quantised light of 1e-4 lagged a thin, bright sky by 11
    //    display steps at sunset), at the price of one 1 x 108 pass whenever the sky moves.
    {
        const Inputs in = inputs_of(kSky[3]);
        const uint64_t t0 = table_key(in);
        auto bump = [](double& x) { x += 1e-7 * std::max(std::fabs(x), 1.0); };
        void (*const edits[])(Inputs&) = {
            [](Inputs& e) { e.lightnorm.y += 1e-7; },
            [](Inputs& e) { e.sunlight_color.x += 1e-7; },
            [](Inputs& e) { e.moonlight_color.y += 1e-7; },
            [](Inputs& e) { e.ambient_color.z += 1e-7; },
            [](Inputs& e) { e.blue_horizon.x += 1e-7; },
            [](Inputs& e) { e.blue_density.y += 1e-7; },
            [](Inputs& e) { e.haze_horizon += 1e-7; },
            [](Inputs& e) { e.haze_density += 1e-7; },
            [](Inputs& e) { e.cloud_shadow += 1e-7; },
            [](Inputs& e) { e.density_multiplier *= 1.0 + 1e-7; },
            [](Inputs& e) { e.max_y += 1e-4; },
            [](Inputs& e) { e.sun_moon_blend += 1e-7; },
        };
        int moved = 0;
        for (auto edit : edits)
        {
            Inputs e = in;
            edit(e);
            moved += table_key(e) != t0;
        }
        check(moved == static_cast<int>(sizeof(edits) / sizeof(edits[0])), "any change of what A and B read moves their key", moved);
        Inputs o = in;
        bump(o.glow.x);
        bump(o.glow.z);
        o.sun_glow_norm.x += 0.1;
        o.sky_hdr_scale *= 2.0;
        o.classic_mode = 1 - o.classic_mode;
        o.lightnorm.x += 0.1;
        check(table_key(o) == t0, "nothing A and B do not read moves their key");
    }

    // 10. k's quantised key through a sunrise. The light rises along its own azimuth (lightnorm and sun_glow_norm together, as a day sky
    //     has them); k is evaluated every 1e-3 of the light's y, the key's step, from -0.1 (the clamp) to 0.1 (above it the slope falls
    //     as 1 / y^2), with the physical sky held (sf_p; the base's sky view follows its own key). The change of k across one step bounds
    //     its change between equal keys. Default and the thin, bright [TOR] DUSK - Fairytale glow (extinction 0.18 and 0.008).
    {
        double worst = 0.0, worst_y = 0.0;
        int worst_sky = -1;
        for (int si : { 0, 3 })
        {
            const Inputs in = inputs_of(kSky[si]);
            const double az = std::atan2(in.lightnorm.z, in.lightnorm.x);
            auto k_at = [&](double y) {
                Inputs r = in;
                const double c = std::sqrt(std::max(0.0, 1.0 - y * y));
                r.lightnorm = { c * std::cos(az), y, c * std::sin(az) };
                r.sun_glow_norm = r.lightnorm;
                return cast_light_level(r, sf_p);
            };
            CastLight prev = k_at(-0.1);
            for (int q = 1; q <= 200; ++q)
            {
                const double y = -0.1 + q * 1e-3;
                const CastLight cur = k_at(y);
                if (prev.defined && cur.defined && std::fabs(cur.k - prev.k) / prev.k > worst)
                {
                    worst = std::fabs(cur.k - prev.k) / prev.k;
                    worst_y = y;
                    worst_sky = si;
                }
                prev = cur;
            }
        }
        // a 4-hour day cycle turns the sun 2 pi in 14400 s: its height changes by at most 4.4e-4 a second, near the horizon, so k's key
        // changes about 0.44 times a second there (and A and B every frame)
        std::printf("k through a sunrise (Default, Fairytale glow): worst change across one key step %.2g relative (sky %d, light y %.3f)\n", worst,
                    worst_sky, worst_y);
        check(worst < 0.01, "k moves by under 1 % between equal keys through a sunrise", worst);
    }

    if (failures)
    {
        std::printf("llskyfilter_test: %d FAILED\n", failures);
        return 1;
    }
    std::printf("llskyfilter_test: OK (%d skies)\n", nsky);
    return 0;
}
