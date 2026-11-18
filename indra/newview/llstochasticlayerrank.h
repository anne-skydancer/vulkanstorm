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
// Blended faces of one object that lie on the same triangles (the iris and the pupil of a mesh eye,
// stacked decals) have equal depth everywhere. Sorted alpha draws them in an order and the later one
// covers the earlier; the stochastic passes have no order, so equal depths made them share the
// samples and the pixel was their average. The creator's order of the faces is the texture entry
// order, and the reference here is that order, not the sort of the alpha faces (an unstable sort by
// distance with no tie-break).
//
// A face has the rank r = the number of faces of its object with a lower texture entry index whose
// extents equal its own (0 for every other face). In the three stochastic passes its polygon offset
// is pulled forward by 2 r depth units. The units are those of the passes' depth buffer, which is
// DEPTH_COMPONENT24 (LLPipeline::allocateAlphaStochastic, and the scene's targets, llrendertarget.cpp):
// one unit is 1 / 2^24 of the depth range, about 0.06 mm at 10 m and 6 mm at 100 m with a 0.1 m
// near plane: invisible as a position and enough as an order. Two units per
// rank, so that in the accumulation (which takes one unit more than the stochastic depth) a face
// one rank below the winner fails its depth test at the samples the winner covers: it needs
// -2 - 2 r_low <= -1 - 2 r_win, which is false for r_win >= r_low + 1.

#include <cstddef>
#include <cstdint>

namespace LLStochasticLayerRank
{
    // the base units of the passes (the offset unit term of glPolygonOffset; the factor is the slope's)
    constexpr float UNITS_TOTAL_ALPHA = -1.f;   // total alpha and stochastic depth
    constexpr float UNITS_DEPTH = -1.f;
    constexpr float UNITS_ACCUM = -2.f;         // one unit more than the depth
    constexpr float UNITS_GLOW = -3.f;          // one more again (other vertex shaders)
    constexpr float UNITS_PER_RANK = 2.f;
    constexpr uint8_t MAX_RANK = 31;            // a clamp, a volume has far fewer faces

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

    // The ranks of one object's faces, given in the creator's order (ascending texture entry index).
    inline void assign(const Box* boxes, std::size_t n, uint8_t* ranks)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            unsigned rank = 0;
            for (std::size_t j = 0; j < i; ++j)
            {
                if (coincident(boxes[i], boxes[j]))
                {
                    ++rank;
                }
            }
            ranks[i] = (uint8_t)(rank > MAX_RANK ? MAX_RANK : rank);
        }
    }

    // The offset units of a pass for a face of rank r: a higher rank is nearer.
    inline float units(float base, unsigned rank)
    {
        return base - UNITS_PER_RANK * (float)rank;
    }
}

#endif // LL_LLSTOCHASTICLAYERRANK_H
