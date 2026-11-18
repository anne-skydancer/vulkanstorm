/**
 * @file llstochasticlayerrank.h
 * @brief Stochastic transparency: the layer rank of coincident blended faces of one object
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

#ifndef LL_LLSTOCHASTICLAYERRANK_H
#define LL_LLSTOCHASTICLAYERRANK_H

// Header-only and free of GL and viewer types, so that indra/newview/tests/llstochasticlayerrank_test.cpp
// can check the rules standalone.
//
// Blended faces of one object that lie on the same triangles (the iris and the pupil of a mesh eye made
// as duplicated layers, stacked decals of one size) have equal depth everywhere; rigged ones too, since
// the faces of one object share the skin and the matrix palette. Sorted alpha draws them in an order and
// the later one covers the earlier; the stochastic passes have no order, so equal depths made them share
// the samples and the pixel was their average. The reference is the creator's order of the faces, the
// texture entry order: sorted alpha draws the rigged ones in it, and sorts the static ones by distance
// with no tie-break (an unstable sort), which gives coincident faces no defined order.
//
// A face has the rank r = the number of faces of its object with a lower texture entry index whose
// extents equal its own (0 for every other face). In the stochastic passes (total alpha, depth,
// accumulation, glow) its polygon offset is pulled forward by UNITS_PER_RANK r depth units. The units
// are those of the passes' depth buffer, DEPTH_COMPONENT24 (LLPipeline::allocateAlphaStochastic, and the
// scene's targets, llrendertarget.cpp): one unit is 1 / 2^24 of the depth range, about 0.06 mm at 10 m
// and 6 mm at 100 m with the 0.1 m near plane.
//
// The step k = UNITS_PER_RANK. The winner (the face of rank r that covers a sample) stores z - 1 - k r in
// the stochastic depth. Let n be the difference between a face's depth in another pass and the depth
// pass's at the same sample: the programs differ (the alpha-only variant, the full program, the glow
// program), and two coincident faces may be drawn by different vertex shader files. A face passes where
// its depth is at or in front of the stored one (LEQUAL):
//   the winner's accumulation          z - 2 - k r + n       <= z - 1 - k r   while n <= +1
//   the winner's glow                  z - 3 - k r + n       <= z - 1 - k r   while n <= +2
//   the accumulation of the face below z - 2 - k (r - 1) + n <= z - 1 - k r   while n <= 1 - k
//   the glow of the face below         z - 3 - k (r - 1) + n <= z - 1 - k r   while n <= 2 - k
// The passes give the winner 1 unit of noise between the depth and the accumulation (one vertex shader
// file) and 2 between the depth and the glow (other files). The face below meets the winner's depth from
// another program in both of its tests, so it gets 2 units in both: 1 - k < -2 and 2 - k < -2, k >= 5.
// (With k = 2 the glow of the face one below equalled the stored depth and passed with no noise at all.)
// The cost: a face of rank r lies 1 + k r units in front of the surface it is on, which is how far it can
// show through opaque geometry just in front of it: rank 1 (6 units) 0.4 mm at 10 m, 9 mm at 50 m and
// 3.6 cm at 100 m, where an eye is a pixel or less.

#include <cstddef>
#include <cstdint>

namespace LLStochasticLayerRank
{
    // the base units of the passes (the offset unit term of glPolygonOffset; the factor is the slope's)
    constexpr float UNITS_DEPTH = -1.f;         // the total alpha and the stochastic depth: one offset, so that
                                                // a face counts in the total alpha exactly where it can win samples
    constexpr float UNITS_ACCUM = -2.f;         // one unit more than the depth
    constexpr float UNITS_GLOW = -3.f;          // one more again (other vertex shaders)
    constexpr float UNITS_PER_RANK = 5.f;       // see above
    // A mesh has at most 8 faces (LL_SCULPT_MESH_MAX_FACES), so ranks 0 to 7; the clamp bounds the pull
    // at 1 + 5 x 7 = 36 units (2 mm at 10 m) for anything else.
    constexpr uint8_t MAX_RANK = 7;

    struct Box
    {
        float lo[3];
        float hi[3];
    };

    inline bool coincident(const Box& a, const Box& b)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (!(a.lo[i] == b.lo[i]) || !(a.hi[i] == b.hi[i]))
            {
                return false;
            }
        }
        return true;
    }

    // The ranks of one object's n faces, given in the creator's order (ascending texture entry index):
    // box(i) returns the extents of face i, set_rank(i, rank) takes its rank. The viewer reads the
    // extents of its volume's faces in place; the test passes arrays.
    template <typename BoxOf, typename SetRank>
    inline void assign(std::size_t n, BoxOf&& box, SetRank&& set_rank)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const Box face = box(i);
            unsigned rank = 0;
            for (std::size_t j = 0; j < i; ++j)
            {
                if (coincident(face, box(j)))
                {
                    ++rank;
                }
            }
            set_rank(i, (uint8_t)(rank > MAX_RANK ? MAX_RANK : rank));
        }
    }

    // The offset units of a pass for a face of rank r: a higher rank is nearer.
    inline float units(float base, unsigned rank)
    {
        return base - UNITS_PER_RANK * (float)rank;
    }
}

#endif // LL_LLSTOCHASTICLAYERRANK_H
