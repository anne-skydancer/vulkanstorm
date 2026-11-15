/**
 * @file alphaStochasticSmoothF.glsl
 * @brief Stochastic transparency: the smooth of the grain the blend leaves
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

out vec4 frag_color;   // RGBA16F: the colour drawn (alpha corrected), and the total alpha

// What grain the temporal blend leaves (all of this frame's where the last frame was left out), averaged away with the
// neighbours whose colour is within that grain of the pixel's and whose total alpha is like its own: broad glass comes
// out even, while an edge or a texture stronger than the grain stays as it is. Twice: the nearest neighbours around the
// pixel's own colour first, which is off by its grain, then the wider ones around their mean. The result is drawn, not
// kept: the next frame blends with the history the resolve wrote (alphaStochasticResolveF.glsl). Pixels with no grain
// (one layer, or none) leave early. The method is Aurora Viewer PR 3663's cs_smooth (Gaalrauch, GPL-3.0-or-later), on
// this engine's history: the colour of a texel is its blended four-vector corrected by this frame's total alpha over its
// blended alpha, as the composite corrects it.

uniform sampler2D alpha_st_history;  // the blended four-vector (RGBA16F)
uniform sampler2D alpha_st_aux;      // this frame's (grain, total alpha, depths) (RGBA16F)
uniform sampler2D alpha_st_extra;    // (last total alpha, the part of the last frame left out) (RG16F)

float st_frame_weight(vec3 c, float unsure);
float st_grain_reach(float unsure, float weight, float left_out);
float st_like_total(float a, float b);

const float ST_HDR_MAX = 65504.0;    // the largest value of an RGBA16F target

// not used here: the engine's resolve object declares it
vec4 stoch_current(ivec2 px)
{
    return texelFetch(alpha_st_history, px, 0);
}

// the colour drawn of a texel (alpha corrected), and this frame's total alpha at it
vec4 st_drawn(ivec2 px, out float corr)
{
    vec4 h = texelFetch(alpha_st_history, px, 0);
    float total = texelFetch(alpha_st_aux, px, 0).g;
    corr = h.a > 1e-5 ? total / h.a : 1.0;
    return vec4(max(min(h.rgb * corr, vec3(ST_HDR_MAX)), vec3(0.0)), total);
}

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    float corr;
    vec4 c = st_drawn(px, corr);
    float unsure = min(texelFetch(alpha_st_aux, px, 0).r * corr, ST_HDR_MAX);
    // a sure colour (and nothing) stays as it is
    float weight = st_frame_weight(c.rgb, unsure);
    if (weight >= 1.0 || c.a <= 0.0)
    {
        frag_color = c;
        return;
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
    frag_color = vec4(mid, c.a);
}
