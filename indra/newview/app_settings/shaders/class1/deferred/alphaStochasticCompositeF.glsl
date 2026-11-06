/**
 * @file alphaStochasticCompositeF.glsl
 * @brief Stochastic transparency: the composite over the scene
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

// Depth-based stochastic transparency (alphaStochasticF.glsl; Enderton et al., I3D 2010,
// section 3.4), as the author's Aurora Viewer resolves it without MSAA (oit_stochastic.wgsl
// cs_resolve, fs_composite, fs_composite_glow), per pixel in one pass:
//   the transparency, the mean over the pass's samples of prod(1 - alpha), dims the scene
//   (blend ONE, ONE_MINUS_SRC_ALPHA; the glow in the scene alpha dimmed the same way, ZERO,
//   ONE_MINUS_SRC_ALPHA), and the accumulated alpha x colour, averaged over all the samples of
//   every iteration, is added, times the alpha correction (total alpha over accumulated alpha).
// st_glow 1: the blended faces' glow, accumulated like their colour and corrected by the same
// ratio, added to the scene alpha (blend ZERO, ONE, ONE, ONE) after the composite.

out vec4 frag_color;

uniform sampler2DMS alpha_st_accum; // alpha x colour, alpha (additive)
uniform sampler2DMS alpha_st_trans; // red prod(1 - alpha), green the glow (additive)
uniform int st_samples_hw;          // samples of the targets
uniform int st_samples;             // samples in all, over the iterations
uniform int st_glow;
uniform sampler2D alpha_st_resolved; // the temporal accumulation: the blended colour, averaged
uniform int st_temporal;             // 1: the colour pass takes alpha_st_resolved instead of the samples

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    float trans = 0.0;
    float glow = 0.0;
    vec4 accum = vec4(0.0);
    for (int s = 0; s < st_samples_hw; ++s)
    {
        vec2 tg = texelFetch(alpha_st_trans, px, s).rg;
        trans += tg.r;
        glow += tg.g;
        if (st_temporal == 0 || st_glow != 0)
        {
            accum += texelFetch(alpha_st_accum, px, s);
        }
    }
    float total = 1.0 - trans / float(max(st_samples_hw, 1));
    accum /= float(max(st_samples, 1));
    if (st_temporal != 0 && st_glow == 0)
    {
        // the blended colour (alpha included): the correction below is total alpha over its alpha
        accum = texelFetch(alpha_st_resolved, px, 0);
    }
    float corr = accum.a > 1e-5 ? total / accum.a : 1.0;
    if (st_glow != 0)
    {
        glow = glow / float(max(st_samples, 1)) * corr;
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
    frag_color = vec4(max(accum.rgb * corr, vec3(0.0)), clamp(total, 0.0, 1.0));
}
