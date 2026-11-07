/**
 * @file alphaStochasticResolveF.glsl
 * @brief Stochastic transparency: the temporal accumulation of the resolved colour
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

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

// The accumulated colour of this frame (alpha x colour, alpha) averaged over the samples, blended
// with the last frame's blended colour where this pixel's nearest transparent surface was, moved
// by the camera's motion alone (the surfaces' own motion is not reprojected) and clamped to the
// 3 x 3 neighbourhood of this frame as a TAA's history is, so that what moved shows no ghost
// beyond what the neighbourhood holds. The design and the weights are the author's Aurora Viewer's
// (oit_stochastic.wgsl `reprojected`, cs_resolve): the result is the next frame's history and the
// composite's accumulated colour, whose alpha correction (total alpha over this alpha) is made there
// with the current frame's total alpha, so that an alpha change is never ghosted.

uniform sampler2DMS alpha_st_accum;  // alpha x colour, alpha, additive, over the passes' samples
uniform sampler2DMS alpha_st_depth;  // the stochastic depth: the nearest transparent surface, or the opaque one
uniform sampler2D   alpha_st_history; // the last frame's blended colour
uniform int   st_samples_hw;         // samples of the targets
uniform int   st_samples;            // samples in all, over the iterations
uniform mat4  st_reproj;             // this frame's clip space to the last frame's
uniform float st_weight;             // the current frame's weight
uniform int   st_history_valid;      // the history holds the last frame, of the same size

vec4 st_resolve_pixel(ivec2 px)
{
    vec4 sum = vec4(0.0);
    for (int s = 0; s < st_samples_hw; ++s)
    {
        sum += texelFetch(alpha_st_accum, px, s);
    }
    return sum / float(max(st_samples, 1));
}

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec4 cur = st_resolve_pixel(px);
    if (st_history_valid == 0 || cur.a <= 0.0)
    {
        frag_color = cur;
        return;
    }
    float z = 1.0;
    for (int s = 0; s < st_samples_hw; ++s)
    {
        z = min(z, texelFetch(alpha_st_depth, px, s).r);
    }
    ivec2 size = textureSize(alpha_st_history, 0);
    if (z >= 1.0)
    {
        frag_color = cur;
        return;
    }
    vec4 clip = vec4((vec2(px) + 0.5) / vec2(size) * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
    vec4 pp = st_reproj * clip;
    if (pp.w <= 0.0)
    {
        frag_color = cur;
        return;
    }
    vec2 puv = pp.xy / pp.w * 0.5 + 0.5;
    if (puv.x < 0.0 || puv.y < 0.0 || puv.x >= 1.0 || puv.y >= 1.0)
    {
        frag_color = cur;
        return;
    }
    vec4 old = texelFetch(alpha_st_history, ivec2(puv * vec2(size)), 0);
    vec4 mn = cur;
    vec4 mx = cur;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            vec4 c = st_resolve_pixel(clamp(px + ivec2(dx, dy), ivec2(0), size - ivec2(1)));
            mn = min(mn, c);
            mx = max(mx, c);
        }
    }
    frag_color = mix(clamp(old, mn, mx), cur, st_weight);
}
