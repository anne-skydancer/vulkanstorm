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
//     own Python generator makes the same tables (the shader's lookup is checked there against it).

#include "llstochasticstate.h"

#include <bit>
#include <cstdio>
#include <set>

// FNV-1a over the table's words, little endian. The Python test has the same function.
// MASK_HASH_4 = 0xc162a2078068620a
// MASK_HASH_8 = 0xd8062231acc3d415
// MASK_HASH_16 = 0xa5bbb68bfab929d6
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
        check(fnv(maskTable(4)) == 0xc162a2078068620aULL, "MASK_HASH_4");
        check(fnv(maskTable(8)) == 0xd8062231acc3d415ULL, "MASK_HASH_8");
        check(fnv(maskTable(16)) == 0xa5bbb68bfab929d6ULL, "MASK_HASH_16");
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
        s.setFrameForTest(0xfffffffeu);
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
}

int main()
{
    test_masks();
    test_state();
    if (failures == 0)
    {
        std::printf("llstochastichistory_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
