/**
 * @file lllightthrough.h
 * @brief Lamp shadows L3: the light let through see-through casters. The GL-free rules the lamp pools share (the projector pool,
 *        and the point-light faces when they come): the light through one face, the solid and invisible limits, one tap of the
 *        lookup and its combine, the layer's memory and its carry through a repack
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
// deferred/shadowStochasticUtil.glsl (stThroughRef, stThroughTap, stThroughFilter: the lookup). Each function here is the C++ twin of a
// shader line, named in its comment; scripts/tests/test_light_through_twin.py pins the two texts together.
//
// The method (doc/shadows/projector-shadow-pool.md, "The light let through see-through casters"): beside the depth of a lamp's tile,
// a second cached picture holds the share of the lamp's light that the see-through casters in front of the tile's opaque depth let
// through, per colour channel, and the depth of the nearest of them: a single-layer coloured translucency map (Gosselin et al. 2004;
// Filion & McNaughton 2008, StarCraft II; Crytek 2011). It is deterministic, so a cached tile holds no grain, and it depends on the
// lamp and the scene alone. Stacked casters multiply (the blend is a product, so the order they are drawn in does not matter). Its
// known limit: one depth is stored, so a receiver between two see-through casters gets both.

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

// At or above it the texel is solid: it goes into the depth layer, as it does without the light-through layer. It is full opacity:
// the owner removed the alpha cuts on 2026-11-18 (decision D4, #559: the Stochastic caster rule casts in proportion below 1.0 and is
// solid at 1.0), and the light through a texel goes to 0 as a reaches 1 (transmittance: (1 - a)(1 + a C)), so the switch to the depth
// layer is no step at all. One number in every text that draws a blend texel: shadowAlphaMaskF.glsl's shadow_solid_alpha
// (pipeline.cpp's SHADOW_BLEND_SOLID_ALPHA) and pbrShadowAlphaBlendF.glsl draw it into depth from here, shadowLightThroughF.glsl
// (LIGHT_THROUGH_SOLID) into the layer below it; test_light_through_twin.py fails when one of them moves alone.
constexpr float kSolidAlpha = 1.0f;

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
// The light through one face (the audit's N4)
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

// The layer keeps the nearest caster's window depth as it is (R32F: the fragment's float, stored and compared without a conversion,
// as the lookup compares the depth atlas's D32F). A texel no see-through caster covers holds 1, the far plane (the layer is cleared
// to it). A recorded caster passed the caster pass's depth test against the tile's opaque depth there (LEQUAL, at most the far
// plane) and clipping keeps it inside the frustum, so its depth is at most 1; one exactly on the far plane would read as none, and
// nothing lies behind it to tint.
constexpr float kNoLayer = 1.0f;

// ---------------------------------------------------------------------------------------------
// One tap of the lookup, and the filter's combine
// ---------------------------------------------------------------------------------------------

// The receiver's reference at one texel of a footprint: its depth cz less the constant bias, lowered (never raised) by the receiver
// plane over the texel's offset (plane: the plane's depth change from the receiver to the texel's centre), at most rp_max: the depth
// lookup's reference (stSpotQuadTile), so a see-through caster is "in front" and an opaque texel "blocks" by the depth filter's own
// test. (shadowStochasticUtil.glsl, stThroughRef)
inline float texelRef(float cz, float z_bias, float plane, float rp_max)
{
    return cz - z_bias + std::min(std::max(plane, -rp_max), 0.f);
}

// One tap: the four texels of its bilinear footprint (gather order, any order works) with their bilinear weights bw, the layer's
// stored depths zt, the lamp's depth atlas there s (the atlas the depth filter reads), the receiver's reference at each texel ref
// (texelRef) and the hardware's bilinear tau of the four texels tb (a texel without a caster holds exactly 1).
//   open  the weight of the texels the depth atlas does not block (s >= ref: the depth filter's lit test): the tap's weight in the
//         filter. A blocked texel's light is the depth filter's zero; it leaves the tap, so a window's frame does not brighten the
//         inside of its penumbra, whether the pane runs on under the frame or stops at it (painted leading, a cut pane).
//   lit   the light through the open texels: open - r (1 - tb), r the share of the caster texels' weight (zt < kNoLayer) that is open
//         with its caster in front of the receiver (zt < ref). tb - (1 - w_all) is the casters' own weighted tau, so a footprint whose
//         casters are all open and in front of the receiver is exact: open times their mean tau. Exactly open where no caster is in
//         front (a tile without a see-through caster, an avatar between the lamp and the glass).
// (shadowStochasticUtil.glsl, stThroughTap)
struct Tap
{
    float lit = 0.f;
    float open = 0.f;
};

inline Tap throughTap(const float bw[4], const float zt[4], const float s[4], const float ref[4], float tb)
{
    float w_all = 0.f;
    float w_open = 0.f;
    float w_front = 0.f;
    for (int i = 0; i < 4; ++i)
    {
        const bool has = zt[i] < kNoLayer;
        const bool open = s[i] >= ref[i];
        w_all += has ? bw[i] : 0.f;
        w_open += open ? bw[i] : 0.f;
        w_front += open && has && zt[i] < ref[i] ? bw[i] : 0.f;
    }
    const float r = w_all > 0.f ? w_front / w_all : 0.f;
    Tap tap;
    tap.lit = w_open + r * (tb - 1.0f);
    tap.open = w_open;
    return tap;
}

// The filter's value from its taps' weighted sums (sum of w_k lit_k, sum of w_k open_k): the light let through the open part of the
// disc, in [0, 1]. 1 when no tap is open: every tap lies behind an opaque caster, where the depth filter's own value is about 0 and the
// layer has nothing to add. (shadowStochasticUtil.glsl, stThroughFilter)
inline float combine(float lit, float open)
{
    return open > 0.f ? clamp01(lit / open) : 1.0f;
}

// ---------------------------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------------------------

constexpr uint32_t kTauBytes = 4;   // RGBA8: the share let through per colour channel (alpha unused, 1)
constexpr uint32_t kDepthBytes = 4; // R32F: the nearest see-through caster's depth

// The layer of one pool, at its atlas' edge (the same texels as the depth atlas): 32 MiB at 2048, 128 MiB at 4096, 512 MiB at 8192, as
// much as the pool's two depth atlases. Made only once a tile of the pool meets a see-through caster, freed when no tile has held one
// for a while (LLProjectorShadows::update). A repack's carry holds a transient copy of every moved tile that holds a caster for the
// length of the repack: at most one more layer, briefly (LLLightThroughLayer::carry).
constexpr uint64_t layerBytes(uint32_t edge)
{
    return uint64_t(edge) * edge * (kTauBytes + kDepthBytes);
}

// ---------------------------------------------------------------------------------------------
// The carry through a repack
// ---------------------------------------------------------------------------------------------

// Once the layer exists it holds every drawn tile's rect: the tile's casters, or "no caster" (1, kNoLayer). A repack moves drawn
// tiles to a fresh layout in one frame (llprojectorpool::planRepack); the depth goes S[old] -> L[new] -> S. The layer has no twin, so:
//   copies  the moves of tiles that hold a caster and keep their size (Move::copy): each source is read into transient storage before
//           any new rect is written (moves can form cycles: one tile's new rect is another's old one), then written to its new rect;
//   clears  the new rects of the other moved tiles: those that hold none, and those whose size changes (a resized tile has no content
//           at its new size: the depth path draws it again, and so is its layer), set to "no caster", after every source was read.
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
        if (move.copy && move.src.size == move.dst.size && holds(move.key))
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
