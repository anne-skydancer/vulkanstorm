/**
 * @file alphaStochasticCurrentF.glsl
 * @brief Stochastic transparency: this frame's accumulation, its grain and its depths, measured once
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

// Two RGBA16F targets (16 B/px, as the RGBA32F target they replace):
//   0: the accumulation (alpha x colour, alpha) averaged over the samples, as the resolve blends it;
//   1: the pixel's grain and total alpha, and its depths: (grain, total alpha, 1 - nearest stochastic depth,
//      1 - farthest), the depths as one minus the window depth because a 16-bit float is fine near 0 (a window depth
//      near 1, which a distant surface has, is not).
// The grain is the standard error of the pixel's colour: the samples of this pixel (the S = 4 samples of its
// accumulation) each hold the colour of the layers that cover it, and their spread, over the root of their effective
// count, is how far this frame's colour is from the colour a pixel of infinitely many samples would show. Where one
// layer covers the pixel every sample holds its colour, the spread is 0 and the colour is exact: the resolve then
// takes no history (sharp). The measure is Aurora Viewer PR 3663's (oit_stochastic.wgsl cs_resolve, Gaalrauch,
// GPL-3.0-or-later), in the accumulation's own domain (alpha x colour: the alpha correction is made at the
// composite, and the history keeps the uncorrected four-vector).
// A sample that is not a number counts for nothing, one that is infinite is clamped to what the targets hold, and a
// transparency that is not a number hides nothing: nothing non-finite leaves this pass.

out vec4 frag_data[2];

uniform sampler2DMS alpha_st_accum;  // alpha x colour, alpha, additive, over the passes' samples
uniform sampler2DMS alpha_st_trans;  // red prod(1 - alpha), green the glow (additive)
uniform sampler2DMS alpha_st_depth;  // the stochastic depth: the nearest transparent surface, or the opaque one
uniform int st_samples_hw;           // samples of the targets
uniform int st_samples;              // samples in all, over the iterations

const float HDR_MAX = 65504.0;       // the largest value an RGBA16F target holds

bool st_nan(float x)
{
    return (floatBitsToUint(x) & 0x7fffffffu) > 0x7f800000u;
}

// A sample as the resolve takes it: nothing where a part is not a number, no part under 0 nor over what the target holds
vec4 st_sample(vec4 a)
{
    bool bad = st_nan(a.r) || st_nan(a.g) || st_nan(a.b) || st_nan(a.a);
    return bad ? vec4(0.0) : clamp(a, vec4(0.0), vec4(HDR_MAX));
}

// A sample of the transparency: between nothing and all of the light, and all of it where it is not a number
float st_through(float x)
{
    return st_nan(x) ? 1.0 : clamp(x, 0.0, 1.0);
}

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec4 sum = vec4(0.0);
    vec3 squares = vec3(0.0);   // the sum of (alpha x colour)^2 / alpha: the weighted second moment of the samples' colours
    float alphas = 0.0;         // the sum of alpha^2: the effective count's divisor
    float trans = 0.0;
    float z_near = 1.0;
    float z_far = 0.0;
    for (int s = 0; s < st_samples_hw; ++s)
    {
        vec4 a = st_sample(texelFetch(alpha_st_accum, px, s));
        sum += a;
        if (a.a > 1e-5)
        {
            squares += a.rgb * a.rgb / a.a;
            alphas += a.a * a.a;
        }
        trans += st_through(texelFetch(alpha_st_trans, px, s).r);
        float d = texelFetch(alpha_st_depth, px, s).r;
        z_near = min(z_near, d);
        z_far = max(z_far, d);
    }
    vec4 cur = sum / float(max(st_samples, 1));
    float total = clamp(1.0 - trans / float(max(st_samples_hw, 1)), 0.0, 1.0);
    float grain = 0.0;
    if (sum.a > 1e-5)
    {
        vec3 mean = sum.rgb / sum.a;
        vec3 spread = max(squares / sum.a - mean * mean, vec3(0.0));
        float count = sum.a * sum.a / max(alphas, 1e-10);
        grain = cur.a * sqrt(max(spread.r, max(spread.g, spread.b)) / max(count - 1.0, 1.0));
    }
    frag_data[0] = vec4(min(cur.rgb, vec3(HDR_MAX)), min(cur.a, HDR_MAX));
    frag_data[1] = vec4(min(grain, HDR_MAX), total, 1.0 - z_near, 1.0 - z_far);
}
