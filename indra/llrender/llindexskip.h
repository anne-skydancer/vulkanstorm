/**
 * @file llindexskip.h
 * @brief The parts of an indexed draw that remain once some index ranges are left out (GL-free)
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

#ifndef LL_LLINDEXSKIP_H
#define LL_LLINDEXSKIP_H

// A point light's shadow faces leave out the prim that emits the light, by identity (owner decision D4): its faces' index ranges in the
// vertex buffers they share with other prims of their spatial group (a draw batches the consecutive faces of a group that look alike, so
// the emitter's faces sit inside a draw with its neighbours'). LLVertexBuffer::drawRange draws what is left (llvertexbuffer.cpp,
// sSkipRanges). Header-only and std only, so indra/newview/tests/llpointshadowpool_test.cpp checks the split standalone.

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace llindexskip
{

struct Range
{
    uint32_t begin = 0; // the first index left out
    uint32_t end = 0;   // one past the last
};

// The parts of the indices [offset, offset + count) outside every range of `skip` (any order, overlaps allowed), in ascending order, as
// (offset, count) pairs appended to `out`. Returns false when no range meets the draw (the caller draws it whole, `out` untouched).
inline bool split(uint32_t offset, uint32_t count, const std::vector<Range>& skip, std::vector<std::pair<uint32_t, uint32_t>>& out)
{
    const uint64_t lo = offset, hi = uint64_t(offset) + count;
    std::vector<Range> hits;
    for (const Range& r : skip)
    {
        if (r.end > r.begin && uint64_t(r.begin) < hi && uint64_t(r.end) > lo)
        {
            hits.push_back(r);
        }
    }
    if (hits.empty())
    {
        return false;
    }
    std::sort(hits.begin(), hits.end(), [](const Range& a, const Range& b) { return a.begin < b.begin; });
    uint64_t at = lo;
    for (const Range& r : hits)
    {
        if (uint64_t(r.begin) > at)
        {
            out.emplace_back(uint32_t(at), uint32_t(uint64_t(r.begin) - at));
        }
        at = std::max(at, uint64_t(r.end));
        if (at >= hi)
        {
            return true;
        }
    }
    if (at < hi)
    {
        out.emplace_back(uint32_t(at), uint32_t(hi - at));
    }
    return true;
}

} // namespace llindexskip

#endif // LL_LLINDEXSKIP_H
