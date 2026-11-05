/**
 * @file alphaStochasticExpandF.glsl
 * @brief Stochastic transparency: the opaque depth spread over the pass's samples
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

// The single-sampled opaque depth given to every sample of the stochastic passes' depth
// (after the author's Aurora Viewer, oit_stochastic_expand.wgsl fs_expand_ss): a full-screen
// triangle, depth test ALWAYS with writes, no colour. Run before the total alpha and before
// each iteration of the stochastic depth pass.
//
// Each sample takes the opaque surface's depth at its own position: the pixel centre's depth
// extended along the surface's screen-space depth gradient, on each axis from the side whose
// next two texels continue it in a straight line (the side that is not across a depth edge),
// none where neither does (a feature one or two pixels wide: the centre's depth, as before). A
// blended face is rasterised at its samples' own depths, so it now meets the opaque surface
// sample for sample, as sorted alpha meets it at the pixel centre, and a 256th of its depth
// slope (the rounding) keeps it on a surface it lies on. With the centre's depth on every
// sample instead, the passes needed a whole slope unit of polygon offset, and it let a blended
// face up to one pixel's depth step behind an opaque surface show through it, where sorted alpha
// hides it. Reading gl_SamplePosition runs this shader once per sample.

uniform sampler2D depthMap; // the scene depth (DEFERRED_DEPTH)

#define ALPHA_ST_DEPTH_LSB (1.0 / 16777216.0) // one step of the 24-bit depth

float alphaStochasticDepth(ivec2 px, ivec2 last)
{
    return texelFetch(depthMap, clamp(px, ivec2(0), last), 0).r;
}

// the depth step per pixel along axis (one of (1, 0), (0, 1)) at px, centre depth d: from the
// side whose two texels continue in a straight line within a quarter of the step (and the
// rounding), the straighter of the two; 0 where neither side does or lies inside the map
float alphaStochasticStep(ivec2 px, ivec2 axis, ivec2 last, float d)
{
    float step = 0.0;
    float best = 1.0e30;
    for (int side = -1; side <= 1; side += 2)
    {
        ivec2 p1 = px + side * axis;
        ivec2 p2 = px + 2 * side * axis;
        if (any(lessThan(p2, ivec2(0))) || any(greaterThan(p2, last)))
        {
            continue;
        }
        float s1 = (alphaStochasticDepth(p1, last) - d) * float(side);
        float s2 = (alphaStochasticDepth(p2, last) - alphaStochasticDepth(p1, last)) * float(side);
        float bend = abs(s2 - s1);
        if (bend <= 0.25 * abs(s1) + 4.0 * ALPHA_ST_DEPTH_LSB && bend < best)
        {
            best = bend;
            step = s1;
        }
    }
    return step;
}

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    ivec2 last = textureSize(depthMap, 0) - ivec2(1);
    float d = texelFetch(depthMap, px, 0).r;
    vec2 grad = vec2(alphaStochasticStep(px, ivec2(1, 0), last, d),
                     alphaStochasticStep(px, ivec2(0, 1), last, d));
    // the far plane (no opaque surface) stays where it is
    gl_FragDepth = d >= 1.0 ? 1.0 : clamp(d + dot(gl_SamplePosition - vec2(0.5), grad), 0.0, 1.0);
}
