/**
 * @file alphaStochasticF.glsl
 * @brief Stochastic transparency: the coverage mask of a blended fragment
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of either the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 2.1 of the License or
 * (at your option) any later version, or the GNU General Public License as
 * published by the Free Software Foundation, version 3 of the License or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE,
 * LICENSE-LGPL-2.1, LICENSE-GPL-3.0 and NOTICE.md in the source tree.
 */

// Depth-based stochastic transparency (Enderton, Sintorn, Shirley and Luebke, "Stochastic
// Transparency", I3D 2010, section 3.4), from the author's Aurora Viewer implementation
// (object.wgsl st_mask, st_sub_mask). Attached to the alpha programs' stochastic variants
// (ALPHA_STOCHASTIC, llviewershadermgr.cpp), which shade a blended fragment for its alpha
// alone and return early:
//   st_mode 1, total alpha: every sample, the target keeps prod(1 - alpha) (blend ZERO,
//              ONE_MINUS_SRC_ALPHA into the red of an RG16F target, cleared to 1);
//   st_mode 2, stochastic depth: R = floor(alpha S + xi) of the S samples, a stratified
//              S-choose-R subset from a fixed table, depth only.
// The accumulation pass is the ordinary program (no sample mask, early depth test kept).

uniform usampler2D alpha_st_masks; // [r][seed]: a random r-subset of the S sample bits
uniform int st_mode;               // 1 total alpha, 2 stochastic depth
uniform int st_samples_hw;         // samples of the pass's targets (at most 8)
uniform int st_samples;            // samples in all (4, 8 or 16: the table's S)
uniform int st_iteration;          // the run of the depth and accumulation passes
uniform int st_seed;               // the face's identity (LLDrawInfo::mStochasticSeed)

#define ST_SEEDS 2048u

uint st_hash(uint v)
{
    uint x = v * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (x >> 22u) ^ x;
}

// The whole S-bit mask of a fragment of alpha a: the seed is the face's identity and the
// triangle, which rotate the pixel's value for the dither of R; the choice of the subset is hashed
// from the seed and the pixel, so the noise is a field fixed in screen space and uncorrelated
// between fragments.
uint st_mask(float a)
{
    uint seed = st_hash(uint(st_seed) * 0x9e3779b9u + 0x7f4a7c15u);
    seed = st_hash(seed ^ st_hash(uint(gl_PrimitiveID) + 0x632be5abu));
    uvec2 px = uvec2(gl_FragCoord.xy);
    uint h = st_hash(px.x | (px.y << 16u));
    vec2 pv = vec2(float(h >> 8u), float(st_hash(h) >> 8u)) / 16777216.0;
    vec2 rot = vec2(float(seed >> 8u), float(st_hash(seed) >> 8u)) / 16777216.0;
    vec2 v = fract(pv + rot);
    // the subset is hashed from the layer's seed and the pixel, whatever the pixel values: two
    // layers whose rotations met on one table index would otherwise share a subset over their
    // whole overlap, and the layers behind them would see too few covered samples (the Aurora
    // Viewer's fix of 2026-11-06, the author's own: object.wgsl st_mask)
    v.y = float(st_hash(seed ^ h ^ 0x5bd1e995u) >> 8u) / 16777216.0;
    uint s = uint(st_samples);
    uint r = min(uint(floor(clamp(a, 0.0, 1.0) * float(s) + v.x)), s);
    uint column = min(uint(v.y * float(ST_SEEDS)), ST_SEEDS - 1u);
    return texelFetch(alpha_st_masks, ivec2(int(column), int(r)), 0).r;
}

// The value for gl_SampleMask[0]: every sample for the total alpha, this iteration's bits of
// the mask, as the pass's samples, for the stochastic depth.
int alpha_stochastic_mask(float a)
{
    if (st_mode != 2)
    {
        return -1;
    }
    uint n = uint(st_samples_hw);
    uint shift = (uint(st_iteration) * n) % 32u;
    uint bits = n >= 32u ? 0xffffffffu : ((1u << n) - 1u);
    return int((st_mask(a) >> shift) & bits);
}
