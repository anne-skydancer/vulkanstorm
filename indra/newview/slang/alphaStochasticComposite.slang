/**
 * @file alphaStochasticComposite.slang
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
import glsl;
import postfullscreen;
import stochasticAlphaSamples;
import stochasticAlphaHistory;

// Depth-based stochastic transparency (alphaStochasticF.glsl; Enderton et al., I3D 2010,
// section 3.4), as the author's Aurora Viewer resolves it without MSAA (oit_stochastic.wgsl
// cs_resolve, fs_composite, fs_composite_glow), per pixel in one pass:
//   the transparency, the mean over the pass's samples of prod(1 - alpha), dims the scene
//   (blend ONE, ONE_MINUS_SRC_ALPHA; the glow in the scene alpha dimmed the same way, ZERO,
//   ONE_MINUS_SRC_ALPHA), and the accumulated alpha x colour, averaged over all the samples
//   is added, times the alpha correction (total alpha over accumulated alpha).
// With the temporal accumulation (st_temporal) the colour is the resolve's: this frame's accumulation blended with the
// last frame's by its grain (alphaStochasticResolveF.glsl, into the history, which is not touched here) and smoothed
// here, in the pass that draws it: what grain the blend leaves is averaged away with the neighbours whose colour is
// within that grain of the pixel's and whose total alpha is like its own, so broad glass comes out even while an edge
// or a texture stronger than the grain stays as it is (twice: the nearest neighbours around the pixel's own colour first,
// which is off by its grain, then the wider ones around their mean). The colour of a texel is its blended four-vector
// corrected by this frame's total alpha over its blended alpha, as below. The method is Aurora Viewer PR 3663's cs_smooth
// (Gaalrauch, GPL-3.0-or-later). The smoothed colour was a target of its own, drawn and read back as RGBA16F; it is rounded
// to half floats here as that store did, so the image is the same. A pixel whose mask is 0 (nothing drawn, see
// alphaStochasticCurrentF.glsl) has a total alpha of 0 and a glow of 0 and draws nothing, so the pass leaves it at once.
// The total alpha, which dims the scene, is still the mean over the samples at this pass.
// st_glow 1: the blended faces' glow, accumulated like their colour and corrected by the same
// ratio, added to the scene alpha (blend ZERO, ONE, ONE, ONE) after the composite.

// OpenGL has separate namespaces for the globals block and the texture units.
[[vk::binding(0, 0)]] uniform sampler2DMS alpha_st_accum; // alpha x colour, alpha (additive)
[[vk::binding(1, 0)]] uniform sampler2DMS alpha_st_trans; // red prod(1 - alpha), green the glow (additive)
uniform int st_samples_hw;          // samples of the targets
uniform int st_samples;             // samples in all
uniform int st_glow;
[[vk::binding(2, 0)]] uniform sampler2D alpha_st_history; // the blended four-vector (RGBA16F)
[[vk::binding(3, 0)]] uniform sampler2D alpha_st_aux;     // this frame's (grain, total alpha, depths) (RGBA16F)
[[vk::binding(4, 0)]] uniform sampler2D alpha_st_extra;   // (total alpha, part left out) (RG16F)
[[vk::binding(5, 0)]] uniform sampler2D alpha_st_mask;    // 1 where something transparent was drawn (R8)
uniform int st_temporal;             // 1: the colour pass takes the smoothed history instead of the samples

uniform float stoch_weight;

// the colour drawn of a texel (alpha corrected), and this frame's total alpha at it
vec4 st_drawn(ivec2 px, out float corr)
{
    vec4 h = texelFetch(alpha_st_history, px, 0);
    float total = texelFetch(alpha_st_aux, px, 0).g;
    corr = h.a > 1e-5 ? total / h.a : 1.0;
    return vec4(max(min(h.rgb * corr, vec3(HDR_MAX)), vec3(0.0)), total);
}

// Rounds to half floats as the RGBA16F target of the old smooth pass stored (the smooth is folded into this pass)
vec3 st_half(vec3 c)
{
    return vec3(unpackHalf2x16(packHalf2x16(c.rg)).xy, unpackHalf2x16(packHalf2x16(vec2(c.b, 0.0))).x);
}

// The colour drawn of the temporal accumulation, smoothed
vec3 st_smoothed(ivec2 px)
{
    float corr;
    vec4 c = st_drawn(px, corr);
    float unsure = min(texelFetch(alpha_st_aux, px, 0).r * corr, HDR_MAX);
    // a sure colour (and nothing) stays as it is
    float weight = st_frame_weight(c.rgb, unsure, stoch_weight);
    if (weight >= 1.0 || c.a <= 0.0)
    {
        return c.rgb;
    }
    float left_out = texelFetch(alpha_st_extra, px, 0).g;
    float reach = st_grain_reach(unsure, weight, left_out);
    ivec2 last = textureSize(alpha_st_history, 0) - ivec2(1);
    vec3 mid = c.rgb;
    for (int radius = 1; radius <= 2; ++radius)
    {
        // (the mean so far weighs a little, should no neighbour be like it)
        vec4 sum = vec4(mid, 1.0) * 0.01;
        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                float qc;
                vec4 q = st_drawn(clamp(px + ivec2(dx, dy), ivec2(0), last), qc);
                vec3 d = abs(q.rgb - mid);
                float apart = max(d.r, max(d.g, d.b)) / reach;
                sum += vec4(q.rgb, 1.0) * st_like_total(q.a, c.a) * exp(-apart * apart);
            }
        }
        mid = sum.rgb / sum.a;
    }
    return mid;
}

[shader("vertex")]
PostVaryings vsMain(PostVertexIn v)
{
    return postFullscreenVertex(v);
}

[shader("fragment")]
vec4 fsMain(PostVaryings input) : SV_Target0
{
    ivec2 px = ivec2(input.pos.xy);
    if (st_temporal != 0 && texelFetch(alpha_st_mask, px, 0).r == 0.0)
    {
        discard;   // nothing drawn here (neither the colour pass nor the glow pass (st_glow) would draw: no total alpha, no glow)
    }
    float trans = 0.0;
    float glow = 0.0;
    vec4 accum = vec4(0.0);
    for (int s = 0; s < st_samples_hw; ++s)
    {
        vec2 tg = texelFetch(alpha_st_trans, px, s).rg;
        trans += st_through(tg.r);
        glow += st_amount(tg.g);
        if (st_temporal == 0 || st_glow != 0)
        {
            accum += st_sample(texelFetch(alpha_st_accum, px, s));
        }
    }
    float total = clamp(1.0 - trans / float(max(st_samples_hw, 1)), 0.0, 1.0);
    accum /= float(max(st_samples, 1));
    float corr = accum.a > 1e-5 ? total / accum.a : 1.0;
    if (st_glow != 0)
    {
        glow = min(glow / float(max(st_samples, 1)) * corr, HDR_MAX);
        if (glow <= 0.0)
        {
            discard;
        }
        return vec4(0.0, 0.0, 0.0, glow);
    }
    if (total <= 0.0)
    {
        discard;
    }
    vec3 colour;
    if (st_temporal != 0)
    {
        // the colour drawn: blended, averaged with its like neighbours where it was grainy, corrected
        vec3 r = st_half(st_smoothed(px));
        colour = (st_nan(r.r) || st_nan(r.g) || st_nan(r.b)) ? vec3(0.0) : min(max(r, vec3(0.0)), vec3(HDR_MAX));
    }
    else
    {
        colour = min(accum.rgb * corr, vec3(HDR_MAX));
    }
    return vec4(colour, total);
}
