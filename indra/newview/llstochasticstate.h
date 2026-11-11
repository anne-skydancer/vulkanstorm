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
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace LLStochasticEngine
{
    constexpr uint32_t SAMPLES = 8;      // samples in all; the table's S. The shaders take S as a parameter, but
                                         // the one table the engine makes, and every producer, is for 8
    constexpr uint32_t SEEDS = 2048;     // columns of the table (STOCH_SEEDS in stochasticSampleF.glsl)

    // The identity of a blended fragment's face for the stochastic depth pass: a hash of its object's
    // full id (16 bytes, never the region's local id) and of two words (a prim's face is (face, 0); a
    // GLTF primitive is (mixIndex(side, batch), mixIndex(node, primitive))), the same on every launch
    // whatever the order the objects arrived in (from the author's Aurora Viewer, types.rs oit_tie:
    // FNV-1a, then a final mix). Shared by LLVolumeGeometryManager and GLTFSceneManager.
    inline uint32_t mixBits(uint32_t h)
    {
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        return h ^ (h >> 16);
    }
    // Two indices into one word without packing them in bit fields (a packed pair aliases once an
    // index outgrows its field); for every a, a bijection of b
    inline uint32_t mixIndex(uint32_t a, uint32_t b)
    {
        return mixBits(a * 0x9e3779b1u + mixBits(b ^ 0x85ebca6bu));
    }
    inline uint32_t faceSeed(const uint8_t* id16, uint32_t a, uint32_t b)
    {
        uint32_t h = 0x811c9dc5u;
        for (int i = 0; i < 16; ++i)
        {
            h = (h ^ (uint32_t)id16[i]) * 0x01000193u;
        }
        for (uint32_t v : { a, b })
        {
            for (uint32_t i = 0; i < 4; ++i)
            {
                h = (h ^ ((v >> (8 * i)) & 0xffu)) * 0x01000193u;
            }
        }
        return mixBits(h);
    }

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

// The Stochastic shadow method's rules for a light that changed since the last frame (its history of that
// light is then not the same light's): the pipeline passes the primary light's direction and each
// projector slot's light, and a channel that changed takes this frame's rays alone. GL-free.
namespace LLShadowLightChange
{
    constexpr float SUN_DEGREES = 0.5f;          // a sun or moon direction that turned by more than this
    constexpr float PROJECTOR_METRES = 0.01f;    // a projector that moved by 1 cm or more
    constexpr float PROJECTOR_HALF_DEGREES = 0.25f;   // or turned by 0.5 degrees (a quaternion's dot is cos of half the angle)

    inline float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

    // unit directions (the world frame); false: the first frame, or the light turned
    inline bool sunUnchanged(bool known, const float now[3], const float before[3])
    {
        return known && dot3(now, before) >= std::cos(SUN_DEGREES * 3.14159265358979f / 180.f);
    }

    // a projector slot's light: the same light (identity), moved less than PROJECTOR_METRES, turned less
    // than twice PROJECTOR_HALF_DEGREES (unit quaternions x y z w)
    inline bool projectorUnchanged(bool same_light, const float pos_now[3], const float pos_before[3],
                                   const float rot_now[4], const float rot_before[4])
    {
        if (!same_light)
        {
            return false;
        }
        const float d[3] = { pos_now[0] - pos_before[0], pos_now[1] - pos_before[1], pos_now[2] - pos_before[2] };
        const float q = std::fabs(rot_now[0] * rot_before[0] + rot_now[1] * rot_before[1] + rot_now[2] * rot_before[2] + rot_now[3] * rot_before[3]);
        return dot3(d, d) < PROJECTOR_METRES * PROJECTOR_METRES && q >= std::cos(PROJECTOR_HALF_DEGREES * 3.14159265358979f / 180.f);
    }
}

#endif // LL_LLSTOCHASTICSTATE_H
