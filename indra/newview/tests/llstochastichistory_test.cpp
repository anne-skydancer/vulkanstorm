// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llstochasticstate.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llstochastichistory_test.cpp
// The stochastic engine's GL-free rules (the C++ half is llstochastichistory.h):
//   - a history's frame state: temporal or not, the frame counter of the seeds (never 0, across the wrap),
//     whether the last frame's colour may be read (the stamp is the frame before; a skipped frame, an
//     invalidation or a release lose it), which texture is the latest, whether it fits a size;
//   - the mask table: row r holds only r-subsets of the S bits, varied, and a hash of the whole table is
//     fixed. scripts/perf/test_alpha_stochastic.py reads MASK_HASH_* from this file and checks that its
//     own Python generator makes the same tables (the shader's lookup is checked there against it);
//   - the dither (stochasticSampleF.glsl stoch_xi24, stoch_mask): the blue-noise tile is the Aurora Viewer's
//     (SHA-256 of its file), and the properties and golden values of the Aurora Viewer's st_mask hold for the
//     twins below (after crates/aurora-render/src/oit_stochastic.rs, its tests, Anne Skydancer, GPL-3.0-or-later).
//     The golden values are computed from Aurora's formulas on Aurora's own tile, not from this header or these twins:
//     scripts/tests/gen_stochastic_goldens.py prints them.

#include "llstochasticstate.h"

#include <bit>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

// FNV-1a over the table's words, little endian (the Python test has the same function, and reads
// these three literals from here: the one source).
constexpr unsigned long long MASK_HASH_4 = 0xc162a2078068620aULL;
constexpr unsigned long long MASK_HASH_8 = 0xd8062231acc3d415ULL;
constexpr unsigned long long MASK_HASH_16 = 0xa5bbb68bfab929d6ULL;
// the transparency's table (maskTable(8) and the blue-noise tile in two rows): the same FNV-1a
constexpr unsigned long long TRANSPARENCY_TABLE_HASH = 0x557629fd90ffa241ULL;
// SHA-256 of the Aurora Viewer's crates/aurora-render/src/oit_stochastic_tile.bin: the tile's ranks as 16-bit
// little-endian words (scripts/perf/gen_stochastic_blue_noise.py checks its output against the same digest)
constexpr const char* TILE_SHA256 = "8c547004839c1d151a35efb2528733d5e8eed0387ca39cafce969fa932e9c0ad";
struct LLStochasticFrameStateTest
{
    static void setFrame(LLStochasticFrameState& s, uint32_t frame) { s.mFrame = frame; }
};

namespace
{
    int failures = 0;

    void check(bool cond, const char* what)
    {
        if (!cond)
        {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    }

    uint64_t fnv(const std::vector<uint32_t>& words)
    {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (uint32_t w : words)
        {
            for (int i = 0; i < 4; ++i)
            {
                h ^= (w >> (8 * i)) & 0xff;
                h *= 0x100000001b3ULL;
            }
        }
        return h;
    }

    // SHA-256 (FIPS 180-4) of a byte string, as lower-case hex: the tile's pin (self-checked on the standard vectors)
    std::string sha256Hex(const std::vector<uint8_t>& data)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
        std::vector<uint8_t> m(data);
        const uint64_t bits = (uint64_t)data.size() * 8;
        m.push_back(0x80);
        while (m.size() % 64 != 56)
        {
            m.push_back(0);
        }
        for (int i = 7; i >= 0; --i)
        {
            m.push_back((uint8_t)(bits >> (8 * i)));
        }
        for (size_t off = 0; off < m.size(); off += 64)
        {
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
            {
                w[i] = (uint32_t)m[off + 4 * i] << 24 | (uint32_t)m[off + 4 * i + 1] << 16 | (uint32_t)m[off + 4 * i + 2] << 8 | m[off + 4 * i + 3];
            }
            for (int i = 16; i < 64; ++i)
            {
                const uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
            for (int i = 0; i < 64; ++i)
            {
                const uint32_t t1 = hh + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
                const uint32_t t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }
        std::string hex;
        char digits[9];
        for (uint32_t v : h)
        {
            std::snprintf(digits, sizeof(digits), "%08x", v);
            hex += digits;
        }
        return hex;
    }
}

// The engine's GLSL in C++, operation for operation (stochasticSampleF.glsl; shadowStochasticCasterF.glsl for the Stochastic
// shadows' blended casters), as the Aurora Viewer's tests mirror its shaders.
// The transparency's coverage is checked on the GPU against its own Python model by scripts/perf/test_alpha_stochastic.py;
// scripts/tests/test_shadow_alpha_permutation.py pins the caster's GLSL text to the lines these twins follow.
namespace mirror
{
    using namespace LLStochasticEngine;

    uint32_t hash(uint32_t v)   // stoch_hash (a PCG hash)
    {
        uint32_t x = v * 747796405u + 2891336453u;
        x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
        return (x >> 22u) ^ x;
    }

    // a face's seed: its identity and its triangle (alphaStochasticF.glsl st_mask; the Aurora Viewer's st_mask `layer`)
    uint32_t layerSeed(uint32_t tie, uint32_t prim)
    {
        return hash(hash(tie * 0x9e3779b9u + 0x7f4a7c15u) ^ hash(prim + 0x632be5abu));
    }

    uint32_t frameSeed(uint32_t seed, uint32_t frame)   // stoch_frame_seed (frame 0: the fixed field)
    {
        return frame ? hash(seed + frame * 0x9e3779b9u) : seed;
    }

    // the frame's golden-ratio fraction of 2^24: frame * 0x9e3779b9 wrapped to 32 bits, then >> 8
    uint32_t frameFraction(uint32_t frame)
    {
        return (frame * 0x9e3779b9u) >> 8;
    }

    // the dither in 24-bit fixed point: the rank's stratum, the layer's rotation (24 bits) and the frame's fraction
    uint32_t xi24(uint32_t rank, uint32_t rot, uint32_t frame_fraction)
    {
        return ((2u * rank + 1u) * 2048u + rot + frame_fraction) & 0xffffffu;
    }

    // stoch_xi24 at pixel px for a face seed and a frame (the tile's rank read where the table's spare rows hold it)
    uint32_t xi24At(uint32_t px, uint32_t py, uint32_t face_seed, uint32_t frame)
    {
        return xi24(BLUE_NOISE[(py & 63u) * 64u + (px & 63u)], face_seed >> 8, frame ? frameFraction(frame) : 0u);
    }

    // R = floor(a S + xi) in float, at most S
    uint32_t rOf(float a, uint32_t s, uint32_t xi)
    {
        const float v = a * (float)s + (float)xi / 16777216.0f;
        return std::min((uint32_t)std::floor(v), s);
    }

    float unit(uint32_t v)   // the top 24 bits in [0, 1)
    {
        return (float)(v >> 8) / 16777216.0f;
    }

    // the per-pixel pick of a subset: stoch_mask's col (the Aurora Viewer's st_mask pick, shadow_alpha.wgsl's sst pick)
    float pick(uint32_t seed, uint32_t px, uint32_t py)
    {
        return unit(hash(seed ^ hash(px | (py << 16)) ^ 0x5bd1e995u));
    }

    // stoch_mask: the S-bit mask of a fragment of coverage a, face seed face_seed, frame (0: fixed) at pixel px
    uint32_t mask(const std::vector<uint32_t>& table, float a, uint32_t face_seed, uint32_t frame, uint32_t px, uint32_t py, uint32_t s)
    {
        const uint32_t r = rOf(std::clamp(a, 0.f, 1.f), s, xi24At(px, py, face_seed, frame));
        const uint32_t column = std::min((uint32_t)(pick(frameSeed(face_seed, frame), px, py) * (float)SEEDS), SEEDS - 1u);
        return table[r * SEEDS + column];
    }

    // The 16 subsets of a 2 x 2 block's 4 texels by size R: the entries in popcount order, the first entry of each R and the
    // number of entries (shadowStochasticCasterF.glsl packs them in SHADOW_CASTER_LO, _HI, _OFF and _CNT)
    struct Subsets
    {
        uint32_t entries[16] = {};
        uint32_t off[5] = {};
        uint32_t cnt[5] = {};
    };
    const Subsets& subsets()
    {
        static const Subsets s = []()
        {
            Subsets out;
            uint32_t i = 0;
            for (uint32_t r = 0; r <= 4; ++r)
            {
                out.off[r] = i;
                for (uint32_t m = 0; m < 16; ++m)
                {
                    if ((uint32_t)std::popcount(m) == r)
                    {
                        out.entries[i++] = m;
                        ++out.cnt[r];
                    }
                }
            }
            return out;
        }();
        return s;
    }

    uint32_t pack(const uint32_t* v, uint32_t n)   // nibbles, the first value lowest
    {
        uint32_t out = 0;
        for (uint32_t i = 0; i < n; ++i)
        {
            out |= v[i] << (4 * i);
        }
        return out;
    }

    // a caster layer's seed (the Aurora Viewer's sst_seed): the draw's identity (st_seed), the triangle's index in the draw
    // (gl_PrimitiveID) and the side
    uint32_t casterLayer(uint32_t draw_seed, uint32_t prim, bool front)
    {
        return layerSeed(draw_seed, prim * 2u + (front ? 0u : 1u));
    }

    // shadow_caster_keeps: does the blended face of opacity a, layer seed `layer`, cast the texel `cell` of the map's anchored
    // grid? R = floor(4 a + xi) of the block's 4 texels, xi the engine's dither at the block (a fixed pattern: frame 0), a
    // random R-subset picked per block and layer.
    bool casterKeeps(int32_t cx, int32_t cy, uint32_t layer, float a)
    {
        const uint32_t bx = (uint32_t)(cx >> 1) & 0xffffu, by = (uint32_t)(cy >> 1) & 0xffffu;
        const uint32_t r = rOf(std::clamp(a, 0.f, 1.f), 4, xi24At(bx, by, layer, 0));
        const Subsets& s = subsets();
        const uint32_t n = s.cnt[r];
        const uint32_t i = s.off[r] + std::min((uint32_t)(pick(layer, bx, by) * (float)n), n - 1u);
        const uint32_t bit = (uint32_t)(cx & 1) | ((uint32_t)(cy & 1) << 1);
        return (s.entries[i] >> bit) & 1u;
    }

    // the share of the texels of a size x size region at the origin that no layer (seed, alpha) casts: what a receiver behind
    // them sees of the light once its filter has averaged the map
    struct Layer
    {
        uint32_t seed;
        float alpha;
    };
    double lit(const std::vector<Layer>& layers, int32_t size = 128)
    {
        uint32_t n = 0;
        for (int32_t y = 0; y < size; ++y)
        {
            for (int32_t x = 0; x < size; ++x)
            {
                bool hit = false;
                for (const Layer& l : layers)
                {
                    hit = hit || casterKeeps(x, y, l.seed, l.alpha);
                }
                n += !hit;
            }
        }
        return (double)n / (size * size);
    }
}

namespace
{

    void test_masks()
    {
        using namespace LLStochasticEngine;
        static_assert(SAMPLES == 8 && SEEDS == 2048);
        for (uint32_t s : { 4u, 8u, 16u })
        {
            const std::vector<uint32_t> table = maskTable(s);
            check(table.size() == (size_t)(s + 1) * SEEDS, "table size");
            bool exact = true;
            for (uint32_t r = 0; r <= s && exact; ++r)
            {
                std::set<uint32_t> distinct;
                for (uint32_t i = 0; i < SEEDS; ++i)
                {
                    const uint32_t m = table[r * SEEDS + i];
                    exact = exact && (uint32_t)std::popcount(m) == r && (s >= 32 || (m >> s) == 0);
                    distinct.insert(m);
                }
                if (r > 0 && r < s)
                {
                    check(distinct.size() > 1, "a middle row is varied");
                }
            }
            check(exact, "row r holds exactly r bits, none above S");
            check(table == maskTable(s), "the same table every time");
            std::printf("S = %u hash 0x%016llx\n", s, (unsigned long long)fnv(table));
        }
        check(fnv(maskTable(4)) == MASK_HASH_4, "MASK_HASH_4");
        check(fnv(maskTable(8)) == MASK_HASH_8, "MASK_HASH_8");
        check(fnv(maskTable(16)) == MASK_HASH_16, "MASK_HASH_16");
    }

    // The transparency's table: S = 8 (its own constant, equal to the shadows' SAMPLES), the mask rows and then
    // the blue-noise tile in the two spare rows, one texture
    void test_transparency_table()
    {
        using namespace LLStochasticEngine;
        static_assert(TRANSPARENCY_SAMPLES == 8 && SAMPLES == 8 && NOISE_ROWS == 2 && BLUE_NOISE_SIZE == 64);
        const std::vector<uint32_t> table = transparencyTable();
        check(table.size() == (size_t)(TRANSPARENCY_SAMPLES + 1 + NOISE_ROWS) * SEEDS, "the transparency table has the mask rows and the noise rows");
        const std::vector<uint32_t> masks = maskTable(TRANSPARENCY_SAMPLES);
        check(std::equal(masks.begin(), masks.end(), table.begin()), "its first rows are maskTable(S)");
        std::vector<bool> seen(BLUE_NOISE_SIZE * BLUE_NOISE_SIZE, false);
        bool permutation = true;
        for (uint32_t i = 0; i < BLUE_NOISE_SIZE * BLUE_NOISE_SIZE; ++i)
        {
            const uint32_t v = table[masks.size() + i];
            permutation = permutation && v == BLUE_NOISE[i] && v < seen.size() && !seen[v];
            if (v < seen.size())
            {
                seen[v] = true;
            }
        }
        check(permutation, "the tile holds the ranks 0 .. 4095, each once, in the spare rows");
        // blue: the means of the 8 x 8 blocks of the tile (a low-pass of the torus) vary far less than white noise's
        // (variance 1/12 / 64 = 0.0013 of the rank range squared)
        double mean_sq = 0.0;
        for (uint32_t by = 0; by < 8; ++by)
        {
            for (uint32_t bx = 0; bx < 8; ++bx)
            {
                double sum = 0.0;
                for (uint32_t y = 0; y < 8; ++y)
                {
                    for (uint32_t x = 0; x < 8; ++x)
                    {
                        sum += (BLUE_NOISE[(by * 8 + y) * 64 + bx * 8 + x] + 0.5) / 4096.0;
                    }
                }
                mean_sq += (sum / 64.0 - 0.5) * (sum / 64.0 - 0.5);
            }
        }
        const double variance = mean_sq / 64.0;
        std::printf("blue noise block-mean variance %.6f (white 0.001302), table hash 0x%016llx\n", variance, (unsigned long long)fnv(table));
        check(variance < 0.00033, "the tile is blue: its block means vary under a quarter of white noise's");
        check(fnv(table) == TRANSPARENCY_TABLE_HASH, "TRANSPARENCY_TABLE_HASH");
    }

    // The standard deviation, over the 64 aligned 8 x 8 blocks of a tile, of the block's mean xi (0 when every block is balanced)
    double blockMeanSd(const std::vector<uint32_t>& ranks)
    {
        double ss = 0.0;
        for (uint32_t b = 0; b < 64; ++b)
        {
            const uint32_t bx = b % 8 * 8, by = b / 8 * 8;
            double sum = 0.0;
            for (uint32_t i = 0; i < 64; ++i)
            {
                sum += (ranks[(by + i / 8) * 64 + bx + i % 8] + 0.5) / 4096.0;
            }
            ss += (sum / 64.0 - 0.5) * (sum / 64.0 - 0.5);
        }
        return std::sqrt(ss / 64.0);
    }

    // The dither (stochasticSampleF.glsl stoch_xi24): the Aurora Viewer's tile and xi24, with the properties its tests hold
    // (oit_stochastic.rs: the mean of R over the tile, eight frames, the frame fraction, two layers, the 8 x 8 balance)
    void test_dither()
    {
        using namespace LLStochasticEngine;
        using namespace mirror;
        check(sha256Hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" &&
              sha256Hex({ 'a', 'b', 'c' }) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 self-check");
        std::vector<uint8_t> bytes;
        for (uint16_t v : BLUE_NOISE)
        {
            bytes.push_back((uint8_t)(v & 0xff));
            bytes.push_back((uint8_t)(v >> 8));
        }
        const std::string digest = sha256Hex(bytes);
        std::printf("blue-noise tile SHA-256 %s\n", digest.c_str());
        check(digest == TILE_SHA256, "the tile is the Aurora Viewer's (SHA-256 of oit_stochastic_tile.bin)");

        // golden values: Aurora's formula on Aurora's tile file (computed apart from this header and these twins)
        const uint32_t s53 = layerSeed(5, 3);
        check(hash(0) == 0x07bb2fe2u && hash(1) == 0xa8beea3cu && hash(7) == 0x7e67121cu && hash(0xdeadbeefu) == 0x67299972u, "golden hash");
        check(s53 == 0xb56d4236u && layerSeed(123456, 0) == 0x416ef572u, "golden layer seeds");
        check(frameSeed(s53, 0) == s53 && frameSeed(s53, 1) == 0x0f656520u && frameSeed(s53, 8) == 0xbbfcd367u, "golden frame seeds");
        check(xi24At(0, 0, s53, 0) == 8926530u && xi24At(17, 42, s53, 0) == 447810u && xi24At(63, 63, s53, 0) == 10409282u, "golden xi24, fixed field");
        check(xi24At(64, 0, s53, 0) == xi24At(0, 0, s53, 0), "the tile repeats every 64 pixels");
        check(xi24At(5, 9, s53, 1) == 11705531u && xi24At(5, 9, s53, 2) == 5297205u, "golden xi24, frames 1 and 2");
        check(xi24At(1000, 2000, 0xdeadbeefu, 123456789u) == 1023711u && xi24At(65535, 1, 0x416ef572u, 0xffffffffu) == 2523003u, "golden xi24, far pixels and frames");
        check(rOf(0.37f, 8, xi24At(0, 0, s53, 0)) == 3 && rOf(0.37f, 4, xi24At(0, 0, s53, 0)) == 2 &&
              rOf(0.37f, 8, xi24At(17, 42, s53, 0)) == 2 && rOf(0.37f, 4, xi24At(17, 42, s53, 0)) == 1, "golden R");
        check(pick(s53, 0, 0) == (float)0.22607415914535522 && pick(s53, 17, 42) == (float)0.839809000492096 &&
              pick(s53, 65535, 1) == (float)0.7138386964797974, "golden picks (the Aurora Viewer's shadow_alpha tests)");
        {
            const std::vector<uint32_t> t8 = maskTable(8);
            const uint32_t s = layerSeed(123456, 9), s_far = layerSeed(0xcafef00du, 77);
            check(mask(t8, 0.37f, s53, 0, 0, 0, 8) == 0x1cu && mask(t8, 0.5f, s53, 0, 17, 42, 8) == 0xa6u && mask(t8, 0.9f, s, 0, 100, 7, 8) == 0xffu &&
                  mask(t8, 0.37f, s53, 1, 3, 4, 8) == 0x43u && mask(t8, 0.37f, s53, 2, 3, 4, 8) == 0x1au &&
                  mask(t8, 0.25f, s_far, 0xfffffffeu, 2560, 1369, 8) == 0x11u, "golden stoch_mask (the S = 8 table)");
        }

        std::vector<uint32_t> tile(BLUE_NOISE, BLUE_NOISE + BLUE_NOISE_SIZE * BLUE_NOISE_SIZE);

        // the mean of R over the tile is a S within one stratum (1/4096) for any rotation
        bool mean_exact = true;
        for (uint32_t s : { 2u, 4u, 8u })
        {
            for (uint32_t ai = 0; ai <= 64; ++ai)
            {
                const float a = (float)ai / 64.f;
                for (uint32_t rot : { 0u, 1u, 999999u, 8388608u, 16777215u })
                {
                    uint64_t sum = 0;
                    for (uint32_t r : tile)
                    {
                        sum += rOf(a, s, xi24(r, rot, 0));
                    }
                    const double mean = (double)sum / tile.size();
                    mean_exact = mean_exact && std::fabs(mean - (double)a * s) <= 1.0 / 4096.0 + 1e-6;
                }
            }
        }
        check(mean_exact, "the mean of R over the tile is a S for any rotation");

        // eight consecutive frames of the golden step sum to 8 a S within 1 (a fresh hash every frame does not)
        {
            double weyl_ss = 0, hash_ss = 0, weyl_max = 0, hash_max = 0, n = 0;
            for (uint32_t ai = 1; ai < 64; ++ai)
            {
                const float a = (float)ai / 64.f;
                for (uint32_t k = 0; k < tile.size(); k += 37)
                {
                    const uint32_t rot = hash(k * 77 + 1) >> 8;
                    for (uint32_t f0 : { 0u, 1000u, 123456u, 0xfffffff0u })
                    {
                        uint32_t weyl = 0, hashed = 0;
                        for (uint32_t f = 0; f < 8; ++f)
                        {
                            const uint32_t frame = f0 + f;
                            weyl += rOf(a, 8, xi24(tile[k], rot, frameFraction(frame)));
                            hashed += rOf(a, 8, xi24(tile[k], hash(rot ^ (hash(frame) * 3u)) >> 8, 0));
                        }
                        const double want = (double)a * 64.0;
                        const double ew = weyl - want, eh = hashed - want;
                        weyl_ss += ew * ew;
                        hash_ss += eh * eh;
                        weyl_max = std::max(weyl_max, std::fabs(ew));
                        hash_max = std::max(hash_max, std::fabs(eh));
                        n += 1;
                    }
                }
            }
            const double weyl_rms = std::sqrt(weyl_ss / n), hash_rms = std::sqrt(hash_ss / n);
            std::printf("eight frames: golden step max %.2f rms %.3f, hashed max %.2f rms %.3f\n", weyl_max, weyl_rms, hash_max, hash_rms);
            check(weyl_max <= 1.0 && weyl_rms < 0.5, "eight frames of the golden step sum to 8 a S within 1 (a on the 1/64 grid)");
            check(weyl_rms < hash_rms * 0.5 && weyl_max < hash_max, "the golden step beats a fresh hash every frame");
            // off the grid the bound is the local discrepancy of eight golden-ratio points: 1.375 at worst (a 1/1024 sweep)
            double any_max = 0;
            for (uint32_t ai = 1; ai < 1024; ++ai)
            {
                const float a = (float)ai / 1024.f;
                for (uint32_t k = 0; k < tile.size(); k += 97)
                {
                    const uint32_t rot = hash(k * 77 + 1) >> 8;
                    for (uint32_t f0 : { 1u, 123456u })
                    {
                        uint32_t sum = 0;
                        for (uint32_t f = 0; f < 8; ++f)
                        {
                            sum += rOf(a, 8, xi24(tile[k], rot, frameFraction(f0 + f)));
                        }
                        any_max = std::max(any_max, std::fabs(sum - (double)a * 64.0));
                    }
                }
            }
            std::printf("eight frames, any alpha: max %.3f\n", any_max);
            check(any_max < 1.4, "eight frames of the golden step sum to 8 a S within 1.4 for any a");
        }

        // the frame fraction is exact at any frame number, and one frame later is always the conjugate further
        bool exact = true;
        for (uint32_t f : { 0u, 1u, 2u, 7u, 1u << 20, (1u << 24) + 3u, 123456789u, 0xfffffffeu, 0xffffffffu })
        {
            exact = exact && frameFraction(f) == (uint32_t)((((uint64_t)f * 0x9e3779b9ULL) & 0xffffffffULL) >> 8) && frameFraction(f) < (1u << 24);
        }
        for (uint32_t f : { 0u, 5u, 1u << 24, 3000000000u, 0xffffffffu })
        {
            const uint32_t step = (frameFraction(f + 1) - frameFraction(f)) & 0xffffffu;
            exact = exact && step >= 10368889u && step <= 10368891u;
        }
        check(exact, "the frame fraction stays exact at any frame number");

        // two layers never share a subset over their overlap: the pick is hashed per pixel and layer
        {
            const uint32_t a = hash(7), b = hash(7) + 1;
            uint32_t same = 0;
            std::set<uint32_t> seen;
            bool inside = true;
            for (uint32_t y = 0; y < 64; ++y)
            {
                for (uint32_t x = 0; x < 64; ++x)
                {
                    const uint32_t ia = (uint32_t)(pick(a, x, y) * (float)SEEDS), ib = (uint32_t)(pick(b, x, y) * (float)SEEDS);
                    inside = inside && ia < SEEDS && ib < SEEDS;
                    same += ia == ib;
                    seen.insert(ia);
                }
            }
            std::printf("two layers: %u of 4096 pixels share a subset (chance 2), one layer covers %zu of 2048\n", same, seen.size());
            check(inside && same < 40 && seen.size() > 1200, "two layers never share a subset over their overlap");
        }

        // the tile balances every 8 x 8 block better than white noise, in xi and in R
        {
            std::vector<uint32_t> white(tile.size());
            for (uint32_t i = 0; i < white.size(); ++i)
            {
                white[i] = i;
            }
            uint32_t h = 12345;
            for (uint32_t i = (uint32_t)white.size() - 1; i > 0; --i)
            {
                h = hash(h);
                std::swap(white[i], white[h % (i + 1)]);
            }
            const double t = blockMeanSd(tile), w = blockMeanSd(white);
            double st = 0, sw = 0, n = 0, mt = 0, mw = 0;
            for (uint32_t ai = 1; ai < 64; ++ai)
            {
                const float a = (float)ai / 64.f;
                for (uint32_t rot : { 0u, 5000000u, 12345678u })
                {
                    for (uint32_t b = 0; b < 64; ++b)
                    {
                        const uint32_t bx = b % 8 * 8, by = b / 8 * 8;
                        uint32_t sum_t = 0, sum_w = 0;
                        for (uint32_t i = 0; i < 64; ++i)
                        {
                            sum_t += rOf(a, 8, xi24(tile[(by + i / 8) * 64 + bx + i % 8], rot, 0));
                            sum_w += rOf(a, 8, xi24(white[(by + i / 8) * 64 + bx + i % 8], rot, 0));
                        }
                        const double et = sum_t / 64.0 - a * 8.0, ew = sum_w / 64.0 - a * 8.0;
                        st += et * et;
                        sw += ew * ew;
                        mt = std::max(mt, std::fabs(et));
                        mw = std::max(mw, std::fabs(ew));
                        n += 1;
                    }
                }
            }
            const double rt = std::sqrt(st / n), rw = std::sqrt(sw / n);
            std::printf("8 x 8 blocks: tile sd %.4f (white %.4f), R rms %.3f max %.3f (white %.3f, %.3f)\n", t, w, rt, mt, rw, mw);
            check(t < 0.015 && w > 0.025, "the tile's 8 x 8 blocks are balanced, white noise's are not");
            check(rt < 0.025 && mt < 0.1 && rt < rw * 0.6 && mt < mw * 0.7, "R over the 8 x 8 blocks follows a S better than with white noise");
        }
    }

    // The Stochastic shadows' blended casters (shadowStochasticCasterF.glsl): the Aurora Viewer's rule (shadow_alpha.wgsl
    // sst_keep and sst_seed, its tests in shadow_alpha.rs) with the engine's dither at the block (owner decision D3), no cut
    // (D4), the seed of the draw, the triangle's index and the side, and a fixed pattern (the Aurora Viewer's without TAA)
    void test_caster()
    {
        using namespace mirror;
        // the packed subsets are the generated ones, and are the Aurora Viewer's SST_LO, SST_HI, SST_OFF, SST_CNT
        const Subsets& s = subsets();
        bool sizes = true;
        for (uint32_t r = 0; r <= 4; ++r)
        {
            for (uint32_t k = 0; k < s.cnt[r]; ++k)
            {
                sizes = sizes && (uint32_t)std::popcount(s.entries[s.off[r] + k]) == r;
            }
        }
        check(sizes && s.cnt[0] + s.cnt[1] + s.cnt[2] + s.cnt[3] + s.cnt[4] == 16, "the subsets of 4 texels by size");
        check(pack(s.entries, 8) == 0x65384210u && pack(s.entries + 8, 8) == 0xFEDB7CA9u && pack(s.off, 5) == 0xFB510u &&
              pack(s.cnt, 5) == 0x14641u, "the packed subset tables (SHADOW_CASTER_LO, _HI, _OFF, _CNT)");

        // golden values of the Aurora Viewer's shadow_alpha tests that the rule keeps (the seed of tie 5, primitive 3; the
        // hash and the picks are pinned in test_dither); its hashed dither is replaced by D3's
        const uint32_t s53 = layerSeed(5, 3);
        check(s53 == 0xb56d4236u && layerSeed(123456, 0) == 0x416ef572u && casterLayer(5, 1, false) == s53, "golden caster seeds (3: triangle 1, back)");
        // golden texels: Aurora's rule with the tile's dither, computed apart from this file on Aurora's own tile
        // (scripts/tests/gen_stochastic_goldens.py)
        {
            const uint16_t fixed[16] = { 0x1239, 0x5949, 0x9266, 0x2448, 0x2600, 0xaabd, 0x9328, 0x8841,
                                         0x2c0f, 0x6270, 0x98c2, 0x4625, 0x1a16, 0x8150, 0x52aa, 0xaa86 };
            bool same = true;
            for (int32_t y = 0; y < 16; ++y)
            {
                uint32_t row = 0;
                for (int32_t x = 0; x < 16; ++x)
                {
                    row |= (uint32_t)casterKeeps(x, y, s53, 0.37f) << x;
                }
                same = same && row == fixed[y];
            }
            check(same, "golden caster texels (alpha 0.37)");
            const uint32_t layer = casterLayer(5, 7, true);
            const uint8_t rows[8] = { 0xd0, 0x5f, 0x7e, 0xa3, 0xb9, 0xc7, 0xfe, 0x23 };
            for (int32_t y = 0; y < 8; ++y)
            {
                uint32_t row = 0;
                for (int32_t x = 0; x < 8; ++x)
                {
                    row |= (uint32_t)casterKeeps(x, y, layer, 0.6f) << x;
                }
                same = same && row == rows[y];
            }
            check(layer == 0x4e30ee75u && casterLayer(5, 7, false) == 0x8146fa41u && same,
                  "golden caster layer and texels (triangle 7 of a draw, alpha 0.6)");
        }

        // the coverage follows the opacity, with no cut (D4): a 3 % sheer casts 3 %, a 98 % pane lets 2 % through. Over
        // 128 x 128 texels (64 x 64 blocks, one period of the tile) the mean of R is 4 a within one stratum, so the light
        // that passes is 1 - a within 1/16384.
        bool proportional = true;
        for (float a : { 0.03f, 0.1f, 0.3f, 0.5f, 0.8f, 0.98f })
        {
            const double l = lit({ { s53, a } });
            proportional = proportional && std::fabs(l - (1.0 - a)) < 1.0 / 16384.0 + 1e-6;
            std::printf("caster alpha %.2f: %.4f of the light passes\n", a, l);
        }
        check(proportional, "one layer lets 1 - alpha of the light through within one stratum, with no cut");
        check(lit({ { s53, 0.f } }) == 1.0 && lit({ { s53, 1.f } }) == 0.0, "alpha 0 casts nothing, alpha 1 everything");

        // independent layers multiply their transmittance (Enderton et al. 3.1); VS's former shared dither passed max(alpha)
        {
            const uint32_t a = casterLayer(11, 7, true);
            const uint32_t other_draw = casterLayer(12, 7, true);   // the same triangle index in another draw (an identical mesh of another object)
            const uint32_t other_tri = casterLayer(11, 8, true);    // another triangle of the same draw (also a coincident copy in one batch)
            const uint32_t back = casterLayer(11, 7, false);        // the back of the same triangle
            const double two_draws = lit({ { a, 0.5f }, { other_draw, 0.5f } });
            const double two_tris = lit({ { a, 0.5f }, { other_tri, 0.5f } });
            const double sides = lit({ { a, 0.5f }, { back, 0.5f } });
            const double three = lit({ { a, 0.3f }, { other_draw, 0.3f }, { other_tri, 0.3f } });
            std::printf("caster layers: 0.5 + 0.5 pass %.4f (two draws), %.4f (two triangles), %.4f (two sides); 3 x 0.3 pass %.4f (0.343)\n",
                        two_draws, two_tris, sides, three);
            check(std::fabs(two_draws - 0.25) < 0.01 && std::fabs(two_tris - 0.25) < 0.01 && std::fabs(sides - 0.25) < 0.01,
                  "two layers of 0.5 pass 0.25 of the light: two draws, two triangles, two sides");
            check(std::fabs(three - 0.343) < 0.01, "three layers of 0.3 pass 0.343 of the light");
            // the limit: the same draw, triangle index and side is one layer (the same triangle drawn twice)
            check(lit({ { a, 0.5f }, { a, 0.5f } }) == 0.5, "the same layer twice is one layer");
        }

        // the cells: a negative one is not another pattern (the block's 16-bit fields keep x and y apart), the pattern repeats
        // every 2^17 texels, and a 2 x 2 block keeps its parity when the origin moves by an even number of texels
        {
            uint32_t hits = 0;
            for (int32_t y = -32; y < 32; ++y)
            {
                for (int32_t x = -32; x < 32; ++x)
                {
                    hits += casterKeeps(x, y, s53, 0.5f);
                }
            }
            check(hits == 64 * 64 / 2, "a region across the negative cells is covered in proportion");
            check(casterKeeps(5, 7, s53, 0.4f) == casterKeeps(5 + (1 << 17), 7 + (1 << 17), s53, 0.4f), "the pattern repeats every 2^17 texels");
            check(!casterKeeps(-1, -1, s53, 0.5f) && casterKeeps(-2, 5, s53, 0.5f) && casterKeeps(-65536, 3, s53, 0.5f) &&
                  !casterKeeps(70000, -9, s53, 0.5f), "golden texels at negative and far cells");
        }
    }

    void test_state()
    {
        LLStochasticFrameState s;
        // no history: not temporal, a fixed field
        check(!s.begin(false, 100) && !s.temporal() && !s.used() && s.seedFrame() == 0, "no history is not temporal");
        s.reset(1920, 1080);
        check(s.sized(1920, 1080) && !s.sized(1920, 1081) && !s.sized(1280, 1080), "sized");
        // the first frame: temporal, the counter starts at 1, nothing to read yet
        check(s.begin(true, 100) && s.temporal() && !s.used() && s.seedFrame() == 1, "first frame");
        check(s.index() == 0, "index 0 before the first resolve");
        s.end(100);
        check(s.index() == 1 && s.valid(), "the resolve makes the other texture the latest");
        // the next frame reads it
        check(s.begin(true, 101) && s.used() && s.seedFrame() == 2, "the next frame reads the history");
        s.end(101);
        check(s.index() == 0, "textures alternate");
        // a frame that did not run the method: stale
        check(s.begin(true, 103) && !s.used() && s.seedFrame() == 3, "a skipped frame loses the history");
        s.end(103);
        check(s.begin(true, 104) && s.used(), "and it is back the frame after");
        s.end(104);
        // the same frame count again (a second render in one frame) is not the next frame
        check(s.begin(true, 104) && !s.used(), "the same frame is not the next");
        s.end(104);
        // an origin shift
        check(s.begin(true, 105) && s.used(), "read before the shift");
        s.end(105);
        s.invalidate();
        check(s.begin(true, 106) && !s.used(), "invalidate loses the history");
        s.end(106);
        check(s.begin(true, 107) && s.used(), "and it builds again");
        s.end(107);
        // the resolve not ready (a shader reload): not temporal, the counter and the history stay
        const uint32_t kept = s.frame();
        check(!s.begin(false, 108) && s.seedFrame() == 0 && s.frame() == kept, "not usable: a fixed field, the counter kept");
        // the counter never reads 0, across the wrap
        LLStochasticFrameStateTest::setFrame(s, 0xfffffffeu);
        check(s.begin(true, 109) && s.seedFrame() == 0xffffffffu, "counter before the wrap");
        check(s.begin(true, 110) && s.seedFrame() == 1, "the counter skips 0 at the wrap");
        // a new history (resize, method re-selected) starts blank
        s.reset(1280, 720);
        check(s.begin(true, 111) && !s.used() && s.index() == 0 && s.sized(1280, 720) && !s.sized(1920, 1080), "reset");
        s.end(111);
        s.clear();
        check(!s.sized(1280, 720) && !s.valid() && !s.begin(false, 112), "release");
        s.reset(1280, 720);
        check(s.begin(true, 112) && !s.used(), "a history made after a release has no past");
    }

    // The face seed (faceSeed, mixIndex): deterministic, and distinct across the objects, faces and
    // GLTF primitives of a scene
    void test_seeds()
    {
        using namespace LLStochasticEngine;
        uint8_t id[16];
        for (int i = 0; i < 16; ++i) id[i] = (uint8_t)(i * 17 + 3);
        check(faceSeed(id, 5, 0) == faceSeed(id, 5, 0), "the seed is deterministic");

        std::set<uint32_t> seen;
        size_t count = 0;
        for (int obj = 0; obj < 64; ++obj)
        {
            uint8_t other[16];
            std::memcpy(other, id, 16);
            other[obj % 16] ^= (uint8_t)(1 + obj / 16);
            for (uint32_t face = 0; face < 16; ++face)
            {
                seen.insert(faceSeed(other, face, 0));
                ++count;
            }
        }
        check(seen.size() == count, "prim faces: distinct across objects and faces");

        // GLTF primitives of one object: sides, batches, nodes and primitives, past the old 16-bit packing
        seen.clear();
        count = 0;
        for (uint32_t side = 0; side < 2; ++side)
        {
            for (uint32_t batch : { 0u, 1u, 2u, 15u, 16u, 255u, 256u, 65535u, 65536u, 70000u })
            {
                for (uint32_t node : { 0u, 1u, 2u, 255u, 256u, 65535u, 65536u, 131072u })
                {
                    for (uint32_t prim : { 0u, 1u, 2u, 65535u, 65536u })
                    {
                        seen.insert(faceSeed(id, mixIndex(side, batch), mixIndex(node, prim)));
                        ++count;
                    }
                }
            }
        }
        check(seen.size() == count, "GLTF primitives: distinct, no aliasing of large indices");

        check(mixIndex(1, 2) != mixIndex(2, 1), "mixIndex is ordered");
        // for every a a bijection of b; across the 32-bit outputs of 40000 pairs a chance collision is
        // possible (about 0.2 expected), so the grid may lose at most one
        std::set<uint32_t> pairs;
        bool rows_distinct = true;
        for (uint32_t a = 0; a < 200; ++a)
        {
            std::set<uint32_t> row;
            for (uint32_t b = 0; b < 200; ++b)
            {
                row.insert(mixIndex(a, b));
                pairs.insert(mixIndex(a, b));
            }
            rows_distinct = rows_distinct && row.size() == 200u;
        }
        check(rows_distinct, "mixIndex: a bijection of b for each a");
        check(pairs.size() >= 200u * 200u - 1u, "mixIndex: (almost) no collision on a 200 x 200 grid");
        check(faceSeed(id, 1, 0) != faceSeed(id, 0, 1), "the two words are not interchangeable");
    }
}

namespace
{
    // the Stochastic shadow method's light-change rules (LLShadowLightChange)
    void test_light_change()
    {
        const float pi = 3.14159265358979f;
        auto dir = [pi](float degrees, float out[3]) { out[0] = std::cos(degrees * pi / 180.f); out[1] = std::sin(degrees * pi / 180.f); out[2] = 0.f; };
        float a[3], b[3];
        dir(0.f, a);
        dir(0.4f, b);
        check(LLShadowLightChange::sunUnchanged(true, b, a), "the sun turned 0.4 degrees: the same light");
        dir(0.6f, b);
        check(!LLShadowLightChange::sunUnchanged(true, b, a), "the sun turned 0.6 degrees: changed");
        check(!LLShadowLightChange::sunUnchanged(false, a, a), "the first frame has no history");

        const float pos[3] = { 10.f, 20.f, 30.f };
        const float rot[4] = { 0.f, 0.f, 0.f, 1.f };
        float moved[3] = { 10.005f, 20.f, 30.f };
        check(LLShadowLightChange::projectorUnchanged(true, moved, pos, rot, rot), "a projector moved 0.5 cm: the same light");
        moved[0] = 10.02f;
        check(!LLShadowLightChange::projectorUnchanged(true, moved, pos, rot, rot), "moved 2 cm: changed");
        check(!LLShadowLightChange::projectorUnchanged(false, pos, pos, rot, rot), "another light in the slot (a slot swap): changed");
        auto turn = [pi](float degrees, float q[4]) { q[0] = std::sin(0.5f * degrees * pi / 180.f); q[1] = q[2] = 0.f; q[3] = std::cos(0.5f * degrees * pi / 180.f); };
        float turned[4];
        turn(0.4f, turned);
        check(LLShadowLightChange::projectorUnchanged(true, pos, pos, turned, rot), "turned 0.4 degrees: the same light");
        turn(0.6f, turned);
        check(!LLShadowLightChange::projectorUnchanged(true, pos, pos, turned, rot), "turned 0.6 degrees: changed");
        for (float& v : turned) v = -v;   // q and -q are one rotation
        check(!LLShadowLightChange::projectorUnchanged(true, pos, pos, turned, rot), "the sign of the quaternion does not matter");
    }
}

int main()
{
    test_masks();
    test_transparency_table();
    test_dither();
    test_caster();
    test_state();
    test_seeds();
    test_light_change();
    if (failures == 0)
    {
        std::printf("llstochastichistory_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
