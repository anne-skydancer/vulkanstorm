// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of the skin palette cache's age rule (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llskinpalettecache_test.cpp
// The cache must stay bounded while an avatar changes skins (b-9 F6), and must never drop an entry a draw still uses.

#include "llskinpalettecache.h"

#include <cstdio>
#include <unordered_map>
#include <vector>

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

    // As LLVOAvatar::MatrixPaletteCache: the frame of the entry's last use, and the palette it holds.
    struct Entry
    {
        uint32_t mFrame = 0;
        std::vector<float> mGLMp;
    };

    using Cache = std::unordered_map<uint64_t, Entry>;

    // As LLVOAvatar::updateSkinInfoMatrixPalette: a draw finds or makes the entry and marks this frame.
    void draw(Cache& cache, uint64_t skin, uint32_t frame)
    {
        Entry& e = cache[skin];
        e.mFrame = frame;
        e.mGLMp.assign(37 * 12, 1.0f);
    }

    // As LLVOAvatar::idleUpdate: one pass when due.
    void idle(Cache& cache, uint32_t& last_prune, uint32_t frame)
    {
        if (LLSkinPaletteCache::pruneDue(frame, last_prune))
        {
            last_prune = frame;
            LLSkinPaletteCache::prune(cache, frame);
        }
    }
}

int main()
{
    using namespace LLSkinPaletteCache;

    // the rule itself
    check(!pruneDue(100, 100) && !pruneDue(355, 100) && pruneDue(356, 100), "a pass every 256 frames");
    check(!stale(2000, 2000 - MAX_AGE) && stale(2000, 2000 - MAX_AGE - 1), "stale after more than 1024 frames unused");
    check(!stale(5, 0xfffffff0u) && stale(2000, 0xfffffff0u), "the age is right across the frame counter's wrap");
    check(pruneDue(10, 0xffffff00u) && !pruneDue(0xffffff10u, 0xffffff00u), "the period is right across the wrap");

    // prune drops exactly the stale entries
    {
        Cache cache;
        draw(cache, 1, 100);
        draw(cache, 2, 1500);
        draw(cache, 3, 2000);
        const size_t dropped = prune(cache, 2124);
        check(dropped == 1 && cache.count(1) == 0 && cache.count(2) == 1 && cache.count(3) == 1, "only the entry unused for over 1024 frames goes");
        check(prune(cache, 2124) == 0, "a second pass in the same frame drops nothing");
    }

    // an outfit-change loop: a new skin every 30 frames, each drawn for 300 frames; one skin worn all along
    {
        Cache cache;
        uint32_t last_prune = 0;
        size_t peak = 0;
        bool worn_kept = true;
        for (uint32_t frame = 1; frame <= 100000; ++frame)
        {
            draw(cache, 0, frame); // the body, every frame
            const uint32_t outfit = frame / 30;
            for (uint32_t k = outfit >= 10 ? outfit - 9 : 0; k <= outfit; ++k)
            {
                draw(cache, 1000 + k, frame); // the last ten outfits are still drawn
            }
            idle(cache, last_prune, frame);
            worn_kept = worn_kept && cache.count(0) == 1;
            peak = cache.size() > peak ? cache.size() : peak;
        }
        // without the rule the cache would hold 3,335 entries by now. With it: the body, and every skin whose last use (300
        // frames after it was put on) is at most MAX_AGE + PRUNE_PERIOD frames old
        const size_t bound = 1 + (MAX_AGE + PRUNE_PERIOD + 300) / 30 + 2;
        check(peak <= bound && cache.size() <= bound, "the cache stays bounded while skins change");
        check(worn_kept, "an entry drawn every frame is never dropped");
    }

    // without draws the whole cache empties after MAX_AGE + PRUNE_PERIOD frames, and not before MAX_AGE
    {
        Cache cache;
        for (uint64_t s = 0; s < 50; ++s)
        {
            draw(cache, s, 10);
        }
        uint32_t last_prune = 0;
        bool early = false;
        for (uint32_t frame = 11; frame <= 10 + MAX_AGE + PRUNE_PERIOD; ++frame)
        {
            idle(cache, last_prune, frame);
            early = early || (frame <= 10 + MAX_AGE && cache.size() != 50);
        }
        check(!early, "nothing is dropped within 1024 frames of its last use");
        check(cache.empty(), "an unused cache empties within 1024 + 256 frames");
    }

    if (failures)
    {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("llskinpalettecache_test: all checks passed\n");
    return 0;
}
