/**
 * @file alphaStochasticCurrentF.glsl
 * @brief Stochastic transparency: this frame's accumulation, averaged over the samples once
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

out vec4 frag_color;   // RGBA32F: the value the temporal resolve reads, kept exactly

// The accumulated colour of this frame (alpha x colour, alpha) averaged over the samples, once
// per pixel. The temporal resolve reads it for its pixel and for the 3 x 3 neighbourhood; this
// pass spares it nine reads and sums of the samples. The sum is the one the resolve made, in the
// same order, so that the result is bit for bit the same.

uniform sampler2DMS alpha_st_accum;  // alpha x colour, alpha, additive, over the passes' samples
uniform int st_samples_hw;           // samples of the targets
uniform int st_samples;              // samples in all, over the iterations

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec4 sum = vec4(0.0);
    for (int s = 0; s < st_samples_hw; ++s)
    {
        sum += texelFetch(alpha_st_accum, px, s);
    }
    frag_color = sum / float(max(st_samples, 1));
}
