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
//              S-choose-R subset from a fixed table, depth only;
//   st_mode 3, the opaque scene's depth (depth of field, the rigged depth restore): the fragment
//              covers every sample of the default framebuffer, or is discarded under st_min_alpha.
// The accumulation pass is the ordinary program (no sample mask, early depth test kept).
// This file is the transparency's part of the stochastic engine: the identity of a fragment (the
// face and the triangle) and the pass; the hash, the frame seed and the table lookup are the
// engine's (stochasticSampleF.glsl).

uniform int st_mode;               // 1 total alpha, 2 stochastic depth, 3 depth only
uniform float st_min_alpha;        // mode 3: the cut under which a fragment is discarded
uniform int st_samples_hw;         // samples of the pass's targets (at most 8)
uniform int st_samples;            // samples in all: the table's S, which is 8 (LLStochasticEngine::TRANSPARENCY_SAMPLES)
uniform int st_seed;               // the face's identity (LLDrawInfo::mStochasticSeed)
uniform int st_frame;              // the frame, in the seed with the temporal accumulation (0: a fixed field)

uint stoch_hash(uint v);
bool stoch_finite(float x);
uint stoch_mask(float a, uint face_seed, int frame, uvec2 px, uint s);

// The whole S-bit mask of a fragment of alpha a: the seed is the face's identity and the
// triangle; the engine mixes the frame in (the subset) and steps the dither with it.
uint st_mask(float a)
{
    uint seed = stoch_hash(uint(st_seed) * 0x9e3779b9u + 0x7f4a7c15u);
    seed = stoch_hash(seed ^ stoch_hash(uint(gl_PrimitiveID) + 0x632be5abu));
    return stoch_mask(a, seed, st_frame, uvec2(gl_FragCoord.xy), uint(st_samples));
}

// The depth-only mode: the shader needs only the alpha (no glare, no shading)
bool alpha_stochastic_depth_only()
{
    return st_mode == 3;
}

// The value for gl_SampleMask[0]: every sample for the total alpha, the mask's bits (the
// pass's samples, all 8) for the stochastic depth.
int alpha_stochastic_mask(float a)
{
    // a non-finite alpha (a NaN or an infinity from a bad vertex or texture) is no fragment: it hides
    // nothing, covers no sample and adds nothing (the mask of a NaN would be a bit pattern the table
    // never holds, and the total alpha would take it)
    if (!stoch_finite(a))
    {
        discard;
    }
    if (st_mode == 3 && a < st_min_alpha)
    {
        discard;
    }
    if (st_mode != 2)
    {
        return -1;
    }
    uint n = uint(st_samples_hw);
    uint bits = n >= 32u ? 0xffffffffu : ((1u << n) - 1u);
    return int(st_mask(a) & bits);
}
