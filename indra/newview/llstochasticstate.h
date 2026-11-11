/**
 * @file llstochasticstate.h
 * @brief The stochastic engine's GL-free rules: the mask table and a history's frame state machine
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */

#ifndef LL_LLSTOCHASTICSTATE_H
#define LL_LLSTOCHASTICSTATE_H

// Header-only and free of GL and viewer types, so that indra/newview/tests/llstochastichistory_test.cpp
// can check the rules standalone. LLStochasticEngine and LLStochasticHistory (llstochastichistory.h)
// use these.

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace LLStochasticEngine
{
    constexpr uint32_t SAMPLES = 8;      // samples in all; the table's S. The shaders take S as a parameter, but
                                         // the one table the engine makes, and every producer, is for 8
    constexpr uint32_t SEEDS = 2048;     // columns of the table (STOCH_SEEDS in stochasticSampleF.glsl)

    // The table of masks for `samples` bits: entry [r * SEEDS + seed] is a random r-subset of the
    // bits, from a fixed generator (splitmix64, a partial Fisher-Yates shuffle), the same on every
    // launch: the author's Aurora Viewer, oit_stochastic.rs mask_table.
    inline std::vector<uint32_t> maskTable(uint32_t samples)
    {
        const uint32_t s = std::clamp(samples, 1u, 32u);
        uint64_t state = 0x9e3779b97f4a7c15ULL ^ (uint64_t)s;
        auto next = [&state]()
        {
            state += 0x9e3779b97f4a7c15ULL;
            uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            return z ^ (z >> 31);
        };
        std::vector<uint32_t> table;
        table.reserve((size_t)(s + 1) * SEEDS);
        std::vector<uint32_t> bits(s);
        for (uint32_t r = 0; r <= s; ++r)
        {
            for (uint32_t seed = 0; seed < SEEDS; ++seed)
            {
                for (uint32_t i = 0; i < s; ++i)
                {
                    bits[i] = i;
                }
                uint32_t mask = 0;
                for (uint32_t i = 0; i < r; ++i)
                {
                    const uint32_t j = i + (uint32_t)(next() % (uint64_t)(s - i));
                    std::swap(bits[i], bits[j]);
                    mask |= 1u << bits[i];
                }
                table.push_back(mask);
            }
        }
        return table;
    }
}

// The frame rules of one producer's history: which of its two textures is the latest, whether the
// last frame's colour may be read, the frame counter of the seeds.
class LLStochasticFrameState
{
public:
    // The history was made (its textures are blank): index 0, nothing valid.
    void reset(uint32_t width, uint32_t height)
    {
        mIndex = 0; mValid = false; mTemporal = false; mUsed = false;
        mWidth = width; mHeight = height;
    }
    // The history is gone.
    void clear() { reset(0, 0); }
    // Made, at this size (a different size is a new history: the last frame's colour is lost)
    bool sized(uint32_t width, uint32_t height) const { return mWidth && mWidth == width && mHeight == height; }

    void invalidate() { mValid = false; }       // the origin shifted: the last view is in the old frame

    // A new frame. temporal: this frame accumulates (`usable`: a history exists and the producer's
    // resolve is ready); then the seeds change every frame (the counter is never 0, also across
    // the wrap) and the last frame's colour is read when that frame ran the method: the stamp is
    // the frame before this one.
    bool begin(bool usable, uint32_t frame_count)
    {
        mTemporal = usable;
        mUsed = false;
        if (mTemporal)
        {
            if (++mFrame == 0)
            {
                mFrame = 1;
            }
            mUsed = mValid && mStamp + 1 == frame_count;
        }
        return mTemporal;
    }

    // The resolve has written the other texture: it is the latest.
    void end(uint32_t frame_count)
    {
        mIndex = 1 - mIndex;
        mValid = true;
        mStamp = frame_count;
    }

    bool temporal() const { return mTemporal; }
    bool used() const { return mUsed; }
    bool valid() const { return mValid; }
    uint32_t index() const { return mIndex; }
    uint32_t frame() const { return mFrame; }
    uint32_t seedFrame() const { return mTemporal ? mFrame : 0; }   // 0: a fixed field

private:
    friend struct LLStochasticFrameStateTest;   // tests/llstochastichistory_test.cpp
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    uint32_t mIndex = 0;        // the latest
    uint32_t mStamp = 0;        // the frame count of the last resolve
    uint32_t mFrame = 0;        // the frame counter of the seeds, never 0 while temporal
    bool mValid = false;
    bool mTemporal = false;
    bool mUsed = false;
};

#endif // LL_LLSTOCHASTICSTATE_H
