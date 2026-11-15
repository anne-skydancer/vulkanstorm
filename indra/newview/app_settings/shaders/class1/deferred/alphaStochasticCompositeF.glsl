/**
 * @file alphaStochasticCompositeF.glsl
 * @brief Stochastic transparency: the composite over the scene
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 * Portions adapted from Aurora Viewer PR 3663 (oit_stochastic.wgsl), Gaalrauch / Rohacan Hirons,
 * GPL-3.0-or-later (the grain measure, the frame weight, the smooth, the non-finite guards)
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

/*[EXTRA_CODE_HERE]*/

// Depth-based stochastic transparency (alphaStochasticF.glsl; Enderton et al., I3D 2010,
// section 3.4), as the author's Aurora Viewer resolves it without MSAA (oit_stochastic.wgsl
// cs_resolve, fs_composite, fs_composite_glow), per pixel in one pass:
//   the transparency, the mean over the pass's samples of prod(1 - alpha), dims the scene
//   (blend ONE, ONE_MINUS_SRC_ALPHA; the glow in the scene alpha dimmed the same way, ZERO,
//   ONE_MINUS_SRC_ALPHA), and the accumulated alpha x colour, averaged over all the samples of
//   every iteration, is added, times the alpha correction (total alpha over accumulated alpha).
// With the temporal accumulation (st_temporal) the colour is the resolve's: this frame's accumulation blended with the
// last frame's by its grain (alphaStochasticResolveF.glsl) and smoothed (alphaStochasticSmoothF.glsl), already
// corrected by the alpha; the total alpha, which dims the scene, is still the mean over the samples at this pass.
// st_glow 1: the blended faces' glow, accumulated like their colour and corrected by the same
// ratio, added to the scene alpha (blend ZERO, ONE, ONE, ONE) after the composite.

out vec4 frag_color;

uniform sampler2DMS alpha_st_accum; // alpha x colour, alpha (additive)
uniform sampler2DMS alpha_st_trans; // red prod(1 - alpha), green the glow (additive)
uniform int st_samples_hw;          // samples of the targets
uniform int st_samples;             // samples in all, over the iterations
uniform int st_glow;
uniform sampler2D alpha_st_resolved; // the temporal accumulation, grain-measured and smoothed: the colour drawn (alpha corrected) and the total alpha
uniform int st_temporal;             // 1: the colour pass takes alpha_st_resolved instead of the samples
uniform int st_moments;              // 1 (glow pass, passes run as iterations): alpha_st_resolved is the sums over every iteration (alpha x colour, alpha)

// Nothing that is not a finite number goes through (the passes sum what the faces give them, and a face's NaN would
// take the pixel): a sample that is not a number counts for nothing, an infinite one is clamped to what the targets
// hold, a transparency that is not a number hides nothing.
const float HDR_MAX = 65504.0;

bool st_nan(float x)
{
    return (floatBitsToUint(x) & 0x7fffffffu) > 0x7f800000u;
}

vec4 st_sample(vec4 a)
{
    bool bad = st_nan(a.r) || st_nan(a.g) || st_nan(a.b) || st_nan(a.a);
    return bad ? vec4(0.0) : clamp(a, vec4(0.0), vec4(HDR_MAX));
}

float st_amount(float x)
{
    return st_nan(x) ? 0.0 : clamp(x, 0.0, HDR_MAX);
}

float st_through(float x)
{
    return st_nan(x) ? 1.0 : clamp(x, 0.0, 1.0);
}

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    float trans = 0.0;
    float glow = 0.0;
    vec4 accum = vec4(0.0);
    for (int s = 0; s < st_samples_hw; ++s)
    {
        vec2 tg = texelFetch(alpha_st_trans, px, s).rg;
        trans += st_through(tg.r);
        glow += st_amount(tg.g);
        if (st_temporal == 0 || (st_glow != 0 && st_moments == 0))
        {
            accum += st_sample(texelFetch(alpha_st_accum, px, s));
        }
    }
    float total = clamp(1.0 - trans / float(max(st_samples_hw, 1)), 0.0, 1.0);
    if (st_glow != 0 && st_moments != 0)
    {
        // the iterations were measured one by one and the accumulation cleared after each: the sums are the moments'
        accum = st_sample(texelFetch(alpha_st_resolved, px, 0));
    }
    accum /= float(max(st_samples, 1));
    float corr = accum.a > 1e-5 ? total / accum.a : 1.0;
    if (st_glow != 0)
    {
        glow = min(glow / float(max(st_samples, 1)) * corr, HDR_MAX);
        if (glow <= 0.0)
        {
            discard;
        }
        frag_color = vec4(0.0, 0.0, 0.0, glow);
        return;
    }
    if (total <= 0.0)
    {
        discard;
    }
    vec3 colour;
    if (st_temporal != 0)
    {
        // the colour drawn: blended, averaged with its like neighbours where it was grainy, corrected (alphaStochasticSmoothF.glsl)
        vec4 r = texelFetch(alpha_st_resolved, px, 0);
        colour = (st_nan(r.r) || st_nan(r.g) || st_nan(r.b)) ? vec3(0.0) : min(max(r.rgb, vec3(0.0)), vec3(HDR_MAX));
    }
    else
    {
        colour = min(accum.rgb * corr, vec3(HDR_MAX));
    }
    frag_color = vec4(colour, total);
}
