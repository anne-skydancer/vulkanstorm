/**
 * @file lllightthrough.h
 * @brief Lamp shadows L3: the light let through see-through casters. The GL-free rules the lamp pools share (the projector pool,
 *        and the point-light faces when they come): the light through one face, the solid and invisible limits, one tap of the
 *        lookup, the depth the layer stores, its memory and its carry through a repack
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

#ifndef LL_LLLIGHTTHROUGH_H
#define LL_LLLIGHTTHROUGH_H

// Header-only, std only and free of GL and viewer types, like llprojectorshadowpool.h, so that
// indra/newview/tests/lllightthrough_test.cpp checks the rules standalone. The GL half is lllightthroughgl.h (the layer's
// textures, the caster pass's state, the carry); the shaders are deferred/shadowLightThroughF.glsl (the casters) and
// deferred/shadowStochasticUtil.glsl (stThroughTap, stThroughFilter: the lookup). Each function here is the C++ twin of a
// shader line, named in its comment; scripts/tests/test_light_through_twin.py pins the two texts together.
//
// The method (reports/light-through-l3-2026-11-18/plan.md): beside the depth of a lamp's tile, a second cached picture holds the
// share of the lamp's light that the see-through casters let through, per colour channel, and the depth of the nearest of them: a
// single-layer coloured translucency map (Gosselin et al. 2004; Filion & McNaughton 2008, StarCraft II; Crytek 2011). It is
// deterministic, so a cached tile holds no grain, and it depends on the lamp and the scene alone. Stacked casters multiply (the
// blend is a product, so the order they are drawn in does not matter). Its known limit: one depth is stored, so a receiver between
// two see-through casters gets both.

#include "llprojectorshadowpool.h" // Rect, Move

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lllightthrough
{

// ---------------------------------------------------------------------------------------------
// The limits of a blended caster's opacity a (its texture's alpha times its face's alpha)
// ---------------------------------------------------------------------------------------------

// At or above it the texel is solid: it goes into the depth layer, as it does without the light-through layer (the 2026-11-14
// decision). The light through a texel just below it is at most 1 - kSolidAlpha^2 < 0.04 (transmittance), so the switch is a step of
// at most 0.04 of the lamp's light. (shadowAlphaMaskF.glsl and pbrShadowAlphaBlendF.glsl draw a blend texel into depth from it;
// shadowLightThroughF.glsl draws one into the layer below it.)
constexpr float kSolidAlpha = 0.98f;

// At or below it the face is invisible: one 8-bit step of alpha, whose light through rounds to within one step of 1 in the layer's
// 8 bits. It is left out of the layer, so it records no depth either (a receiver behind an invisible face and in front of real
// glass must not read as behind a see-through caster). (shadowLightThroughF.glsl)
constexpr float kInvisibleAlpha = 1.0f / 255.0f;

inline bool isSolid(float a) { return a >= kSolidAlpha; }
inline bool isInvisible(float a) { return !(a > kInvisibleAlpha); } // NaN: invisible

// [0, 1], NaN to 0
inline float clamp01(float v)
{
    return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f;
}

// ---------------------------------------------------------------------------------------------
// The light through one face (the audit's N4, derived in the plan, section 3.2)
// ---------------------------------------------------------------------------------------------

// McGuire & Enderton 2011 model a surface by its coverage a and its transmission colour t: it blocks rho = a (1 - t) of the light
// per channel and lets tau = 1 - rho through. SL gives a face one alpha and one colour C (the texture's colour times the face's, in
// linear; glTF: the base colour texture times its factor) and no transmission parameter, so t follows from what SL defines: a faint
// face is a pure filter of its colour (t -> C as a -> 0), and a face at a = 1 is opaque, as SL draws it (t -> 0 as a -> 1). The
// lowest-order t that meets both is t = C (1 - a), so
//     tau = (1 - a) (1 + a C)
// It lies between 1 - a (a black or neutral face: a covering of opaque threads) and 1 - a^2 (white), never above 1; a red pane at
// a = 0.6 lets (0.64, 0.40, 0.40) through, clear glass at a = 0.3 lets 0.91 through. (shadowLightThroughF.glsl, the caster's
// output; the layer multiplies it into what the casters before it let through.)
inline float transmittance(float a, float c)
{
    a = clamp01(a);
    c = clamp01(c);
    return (1.0f - a) * (1.0f + a * c);
}

// ---------------------------------------------------------------------------------------------
// The layer's depth
// ---------------------------------------------------------------------------------------------

// A texel no see-through caster covers holds the far plane (the layer is cleared to it). A recorded caster is always nearer: its
// fragment passed the depth test against the tile's opaque depth, which is at most the far plane.
constexpr float kNoLayer = 1.0f;

// The layer keeps the nearest caster's depth in 16 bits (UNORM), written one step farther than the caster: the conversion to the
// stored value may round to either neighbour (OpenGL 4.6, 2.3.5.2), and one step up makes the stored depth never nearer the light
// than the caster (to the float rounding of the sum, a millionth of a step), so a receiver in front of a pane never reads as behind
// it. A receiver less than two steps behind the pane reads as in front of it: no tint there (the safe side, as the depth bias is).
// (shadowLightThroughF.glsl: LIGHT_THROUGH_DEPTH_STEP)
constexpr float kDepthStep = 1.0f / 65535.0f;

inline float storedDepth(float z)
{
    return std::min(clamp01(z) + kDepthStep, 1.0f);
}

// ---------------------------------------------------------------------------------------------
// One tap of the lookup
// ---------------------------------------------------------------------------------------------

// The share f of a tap's footprint whose see-through caster lies in front of the receiver: the four texels of the bilinear
// footprint (gather order, any order works) with their bilinear weights bw and their stored depths z; among the texels that hold a
// caster (z < kNoLayer), the weight of those in front of the receiver's reference depth ref (z < ref), divided by the weight of all
// of them. 0 when no texel holds a caster. (shadowStochasticUtil.glsl, stThroughTap)
inline float frontShare(const float bw[4], const float z[4], float ref)
{
    float w_all = 0.f;
    float w_front = 0.f;
    for (int i = 0; i < 4; ++i)
    {
        if (z[i] < kNoLayer)
        {
            w_all += bw[i];
            if (z[i] < ref)
            {
                w_front += bw[i];
            }
        }
    }
    return w_all > 0.f ? w_front / w_all : 0.f;
}

// One tap of one channel: the hardware's bilinear tau of the footprint's four texels (tau_bilinear; a texel without a caster
// holds 1) applied to the share in front of the receiver, mix(1, tau_bilinear, f). Exact where the receiver is behind every caster of
// the footprint (f = 1: the bilinear tau itself) and exactly 1 where it is in front of them all or none is there (f = 0): an
// avatar between the lamp and a window gets no tinted rim, and a tile without a see-through caster multiplies by 1.
// (shadowStochasticUtil.glsl, stThroughTap)
inline float throughTap(const float bw[4], const float z[4], float ref, float tau_bilinear)
{
    const float f = frontShare(bw, z, ref);
    return 1.0f + f * (tau_bilinear - 1.0f); // GLSL mix(1, tau, f) = 1 * (1 - f) + tau * f; the same value, written to stay 1 at f = 0
}

// ---------------------------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------------------------

constexpr uint32_t kTauBytes = 4;   // RGBA8: the share let through per colour channel (alpha unused, 1)
constexpr uint32_t kDepthBytes = 2; // R16: the nearest see-through caster's depth

// The layer of one pool, at its atlas' edge (the same texels as the depth atlas): 24 MiB at 2048, 96 MiB at 4096, 384 MiB at 8192.
// Made only once a tile of the pool meets a see-through caster.
constexpr uint64_t layerBytes(uint32_t edge)
{
    return uint64_t(edge) * edge * (kTauBytes + kDepthBytes);
}

// ---------------------------------------------------------------------------------------------
// The carry through a repack
// ---------------------------------------------------------------------------------------------

// Once the layer exists it holds every drawn tile's rect: the tile's casters, or "no caster" (1, kNoLayer). A repack moves drawn
// tiles to a fresh layout in one frame (llprojectorpool::planRepack); the depth goes S[old] -> L[new] -> S. The layer has no twin, so:
//   copies  the moves of tiles that hold a caster: each source is read into transient storage before any new rect is written
//           (moves can form cycles: one tile's new rect is another's old one), then written to its new rect;
//   clears  the new rects of moved tiles that hold none: set to "no caster", after every source was read.
// A tile whose rect does not change keeps its layer as it is. `holds(key)`: the tile's last static render drew a caster into it.
struct CarryPlan
{
    std::vector<llprojectorpool::Move> copies;
    std::vector<llprojectorpool::Rect> clears;
};

template <class Holds>
CarryPlan planCarry(const std::vector<llprojectorpool::Move>& moves, Holds holds)
{
    CarryPlan plan;
    for (const llprojectorpool::Move& move : moves)
    {
        if (move.src == move.dst)
        {
            continue;
        }
        if (holds(move.key))
        {
            plan.copies.push_back(move);
        }
        else
        {
            plan.clears.push_back(move.dst);
        }
    }
    return plan;
}

} // namespace lllightthrough

#endif // LL_LLLIGHTTHROUGH_H
