/**
 * @file alphaStochasticF.glsl
 * @brief Stochastic transparency: the coverage mask of a blended fragment
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
// This file is the transparency's part of the stochastic engine: the identity of a fragment (the
// face and the triangle) and the pass; the hash, the frame seed and the table lookup are the
// engine's (stochasticSampleF.glsl).

uniform int st_mode;               // 1 total alpha, 2 stochastic depth
uniform int st_samples_hw;         // samples of the pass's targets (at most 8)
uniform int st_samples;            // samples in all (the table's S, 8)
uniform int st_iteration;          // the run of the depth and accumulation passes
uniform int st_seed;               // the face's identity (LLDrawInfo::mStochasticSeed)
uniform int st_frame;              // the frame, in the seed with the temporal accumulation (0: a fixed field)

uint stoch_hash(uint v);
uint stoch_frame_seed(uint seed, int frame);
uint stoch_mask(float a, uint seed, uvec2 px, uint s);

// The whole S-bit mask of a fragment of alpha a: the seed is the face's identity and the
// triangle.
uint st_mask(float a)
{
    uint seed = stoch_hash(uint(st_seed) * 0x9e3779b9u + 0x7f4a7c15u);
    seed = stoch_hash(seed ^ stoch_hash(uint(gl_PrimitiveID) + 0x632be5abu));
    seed = stoch_frame_seed(seed, st_frame);
    return stoch_mask(a, seed, uvec2(gl_FragCoord.xy), uint(st_samples));
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
