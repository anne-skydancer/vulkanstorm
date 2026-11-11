/**
 * @file alphaStochasticResolveF.glsl
 * @brief Stochastic transparency: the temporal accumulation of the resolved colour
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

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

// The transparency's part of the stochastic engine's temporal resolve (stochasticResolveUtilF.glsl).
// The accumulated colour of this frame (alpha x colour, alpha, averaged over the samples once by
// alphaStochasticCurrentF.glsl) is blended with the last frame's blended colour where this
// pixel's nearest transparent surface was, moved by the camera's motion alone (the surfaces' own
// motion is not reprojected) and clamped to the 3 x 3 neighbourhood of this frame as a TAA's
// history is. The result is the next frame's history and the composite's accumulated colour,
// whose alpha correction (total alpha over this alpha) is made there with the current frame's
// total alpha, so that an alpha change is never ghosted. This file gives the value of this frame
// and the reprojection depth (the nearest stochastic depth); the rest is the engine's.

uniform sampler2D   alpha_st_current;  // this frame's averaged accumulation (RGBA32F)
uniform sampler2DMS alpha_st_depth;    // the stochastic depth: the nearest transparent surface, or the opaque one
uniform sampler2D   alpha_st_history;  // the last frame's blended colour
uniform int         st_samples_hw;     // samples of the targets

bool stoch_has_history();
bool stoch_reproject(ivec2 px, float z, ivec2 size, out vec4 pp, out vec2 puv);
vec4 stoch_history_nearest(sampler2D history, vec2 puv, ivec2 size);
void stoch_neighbourhood(ivec2 px, ivec2 size, vec4 cur, out vec4 mn, out vec4 mx);
vec4 stoch_accumulate(vec4 hist, vec4 mn, vec4 mx, vec4 cur);

vec4 stoch_current(ivec2 px)
{
    return texelFetch(alpha_st_current, px, 0);
}

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec4 cur = stoch_current(px);
    if (!stoch_has_history() || cur.a <= 0.0)
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
    vec4 pp;
    vec2 puv;
    if (z >= 1.0 || !stoch_reproject(px, z, size, pp, puv))
    {
        frag_color = cur;
        return;
    }
    vec4 old = stoch_history_nearest(alpha_st_history, puv, size);
    vec4 mn;
    vec4 mx;
    stoch_neighbourhood(px, size, cur, mn, mx);
    frag_color = stoch_accumulate(old, mn, mx, cur);
}
