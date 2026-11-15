/**
 * @file alphaStochasticCurrentF.glsl
 * @brief Stochastic transparency: this frame's accumulation, its grain and its depths, measured per iteration
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

// Two RGBA16F targets (16 B/px, as the RGBA32F target they replace):
//   0: the accumulation (alpha x colour, alpha) averaged over the samples, as the resolve blends it;
//   1: the pixel's grain and total alpha, and its depths: (grain, total alpha, 1 - nearest stochastic depth,
//      1 - farthest depth of a transparent layer that covers a sample), the depths as one minus the window depth because a 16-bit float is fine near 0 (a window depth
//      near 1, which a distant surface has, is not).
// The grain is the standard error of the pixel's colour: the samples of this pixel (the S = 8 samples of its
// accumulation) each hold the colour of the layers that cover it, and their spread, over the root of their effective
// count, is how far this frame's colour is from the colour a pixel of infinitely many samples would show. Where one
// layer covers the pixel every sample holds its colour, the spread is 0 and the colour is exact: the resolve then
// takes no history (sharp). The measure is Aurora Viewer PR 3663's (oit_stochastic.wgsl cs_resolve, Gaalrauch,
// GPL-3.0-or-later), in the accumulation's own domain (alpha x colour: the alpha correction is made at the
// composite, and the history keeps the uncorrected four-vector).
// A sample that is not a number counts for nothing, one that is infinite is clamped to what the targets hold, and a
// transparency that is not a number hides nothing: nothing non-finite leaves this pass.
//
// Where the hardware holds fewer samples in a pass than S (2 or 4 of the 8), the passes run as iterations, and each
// iteration's lanes are measured on their own: the moments (the sums of alpha x colour, of its square over alpha and of
// alpha squared) and the depth range of the iterations are combined, never the lanes of different iterations summed first,
// which would give complementary masks the same lane sums and a grain of 0. ST_STAGE:
//   0  one pass holds all S samples: measured and finished here (the pass count 1, no extra target);
//   1  one iteration's moments, drawn twice into the moment targets (ST_PART 0: the sums, blended by adding; 1: the depth
//      range, blended by the maximum);
//   2  the moment targets of every iteration finished as stage 0 finishes its lanes.
#ifndef ST_STAGE
#define ST_STAGE 0
#endif

#if ST_STAGE == 1
out vec4 frag_data[3];
#else
out vec4 frag_data[2];
#endif

#if ST_STAGE != 2
uniform sampler2DMS alpha_st_accum;  // alpha x colour, alpha, additive, over the passes' samples
uniform sampler2DMS alpha_st_depth;  // the stochastic depth: the nearest transparent surface, or the opaque one
uniform sampler2D depthMap;          // the scene depth (DEFERRED_DEPTH)
#else
uniform sampler2D alpha_st_mom_sum;      // the sums over every iteration's lanes: alpha x colour, alpha (RGBA32F)
uniform sampler2D alpha_st_mom_squares;  // the sums of (alpha x colour)^2 / alpha (rgb) and of alpha^2 (a) (RGBA32F)
uniform sampler2D alpha_st_mom_depth;    // (1 - nearest depth, farthest depth - 1) over every iteration (RG16F)
#endif
#if ST_STAGE != 1
uniform sampler2DMS alpha_st_trans;  // red prod(1 - alpha), green the glow (additive)
#endif
uniform int st_samples_hw;           // samples of the targets
uniform int st_samples;              // samples in all, over the iterations
uniform int st_part;                 // stage 1: 0 the sums, 1 the depth range

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

#if ST_STAGE != 2
// The moments of this pass's lanes
void st_lanes(ivec2 px, out vec4 sum, out vec3 squares, out float alphas, out float z_near, out float z_far)
{
    sum = vec4(0.0);
    squares = vec3(0.0);   // the sum of (alpha x colour)^2 / alpha: the weighted second moment of the samples' colours
    alphas = 0.0;          // the sum of alpha^2: the effective count's divisor
    z_near = 1.0;
    z_far = 0.0;
    float opaque = texelFetch(depthMap, px, 0).r;   // the scene depth the samples start from
    for (int s = 0; s < st_samples_hw; ++s)
    {
        vec4 a = st_sample(texelFetch(alpha_st_accum, px, s));
        sum += a;
        if (a.a > 1e-5)
        {
            squares += a.rgb * a.rgb / a.a;
            alphas += a.a * a.a;
        }
        float d = texelFetch(alpha_st_depth, px, s).r;
        z_near = min(z_near, d);
        // the farthest transparent surface: a sample no layer covers still holds the opaque depth (the spread), which is no layer
        // of the pixel (a pane in front of a wall would else slide against the wall under any camera motion and lose its history)
        if (d < opaque - 1e-5)
        {
            z_far = max(z_far, d);
        }
    }
}
#endif

#if ST_STAGE != 1
// The pixel's colour, grain and depths from the moments of all S samples
void st_finish(vec4 sum, vec3 squares, float alphas, float z_near, float z_far, float total)
{
    z_far = max(z_far, z_near);
    vec4 cur = sum / float(max(st_samples, 1));
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
#endif

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
#if ST_STAGE == 0
    vec4 sum;
    vec3 squares;
    float alphas, z_near, z_far;
    st_lanes(px, sum, squares, alphas, z_near, z_far);
    float trans = 0.0;
    for (int s = 0; s < st_samples_hw; ++s)
    {
        trans += st_through(texelFetch(alpha_st_trans, px, s).r);
    }
    st_finish(sum, squares, alphas, z_near, z_far, clamp(1.0 - trans / float(max(st_samples_hw, 1)), 0.0, 1.0));
#elif ST_STAGE == 1
    vec4 sum;
    vec3 squares;
    float alphas, z_near, z_far;
    st_lanes(px, sum, squares, alphas, z_near, z_far);
    // the sums are added over the iterations by blending, the depth range is the maximum (1 - nearest, farthest - 1)
    frag_data[0] = st_part == 0 ? sum : vec4(0.0);
    frag_data[1] = st_part == 0 ? vec4(squares, alphas) : vec4(0.0);
    frag_data[2] = st_part == 0 ? vec4(0.0) : vec4(1.0 - z_near, z_far - 1.0, 0.0, 0.0);
#else
    vec4 sum = texelFetch(alpha_st_mom_sum, px, 0);
    vec4 sq = texelFetch(alpha_st_mom_squares, px, 0);
    vec2 zr = texelFetch(alpha_st_mom_depth, px, 0).rg;
    float trans = 0.0;
    for (int s = 0; s < st_samples_hw; ++s)
    {
        trans += st_through(texelFetch(alpha_st_trans, px, s).r);
    }
    st_finish(sum, sq.rgb, sq.a, 1.0 - zr.x, zr.y + 1.0, clamp(1.0 - trans / float(max(st_samples_hw, 1)), 0.0, 1.0));
#endif
}
