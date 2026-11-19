// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The age rule of an avatar's skin palette cache (LLVOAvatar::mMatrixPaletteCache). An entry is made for a skin the first
// time a draw asks for its joint matrices and was never dropped, so the cache gained an entry (4.2 KB for 37 joints to
// 12.4 KB for 110) for every skin the avatar ever drew: the self avatar over a session of outfit changes, the upload
// preview at every change of its skin options. LLVOAvatar::idleUpdate drops, at most once per period, the entries no draw
// has asked for in a while. A draw rebuilds an entry on its first use in every frame anyway, so dropping one changes
// nothing a draw sees. Pure functions: tests/llskinpalettecache_test.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace LLSkinPaletteCache
{
    constexpr uint32_t PRUNE_PERIOD = 256;  // frames between two passes over an avatar's cache
    constexpr uint32_t MAX_AGE = 1024;      // frames since its last use after which an entry is dropped

    // A pass over the cache is due. Frame counters are unsigned and wrap; their differences stay right across the wrap.
    inline bool pruneDue(uint32_t now, uint32_t last_prune)
    {
        return now - last_prune >= PRUNE_PERIOD;
    }

    // No draw has asked for the entry for more than MAX_AGE frames.
    inline bool stale(uint32_t now, uint32_t last_use)
    {
        return now - last_use > MAX_AGE;
    }

    // Drops the stale entries of a map whose values hold mFrame, the frame of their last use; returns how many it dropped.
    template <class Map>
    size_t prune(Map& cache, uint32_t now)
    {
        size_t dropped = 0;
        for (auto it = cache.begin(); it != cache.end();)
        {
            if (stale(now, it->second.mFrame))
            {
                it = cache.erase(it);
                ++dropped;
            }
            else
            {
                ++it;
            }
        }
        return dropped;
    }
}
