/**
 * @file stochasticAlphaHistory.slang
 * @brief The stochastic engine's temporal resolve, shared by its producers
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
module stochasticAlphaHistory;
import glsl;

// Whether a value is a finite number, tested on its bits (the exponent is all ones for an infinity and a NaN)
// (stoch_finite(float) is in stochasticSampleF.glsl: one definition only, as both objects may meet in a program)
public bool stoch_finite4(vec4 v)
{
    uvec4 e = asuint(v) & uvec4(0x7f800000u);
    return all(notEqual(e, uvec4(0x7f800000u)));
}

// The last frame's clip position (pp) and texture coordinate (puv) of pixel px at window depth z,
// moved by the camera's motion alone (the surfaces' own motion is not reprojected); false where
// the last frame did not see it (behind the camera or off screen).
public bool stoch_reproject(mat4 reprojection, ivec2 px, float z, ivec2 size, out vec4 pp, out vec2 puv)
{
    vec4 clip = vec4((vec2(px) + 0.5) / vec2(size) * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
    pp = reprojection * clip;
    puv = vec2(0.0);
    if (pp.w <= 0.0)
    {
        return false;
    }
    puv = pp.xy / pp.w * 0.5 + 0.5;
    return !(puv.x < 0.0 || puv.y < 0.0 || puv.x >= 1.0 || puv.y >= 1.0);
}

// The transparency's grain-measuring resolve (alphaStochasticCurrentF.glsl measures, alphaStochasticResolveF.glsl blends,
// alphaStochasticCompositeF.glsl smooths). The constants are Aurora Viewer PR 3663's (oit_stochastic.wgsl) except where the
// PR's text says otherwise (doc/oit/stochastic-transparency.md).
// The grain a colour drawn may keep, as a part of that colour (of a dim one, as a part of ST_QUIET_DIM): a pixel under
// it is this frame's alone, one over it takes as much of the previous frames as brings it there.
static const float ST_QUIET = 0.004;
static const float ST_QUIET_DIM = 0.05;
// The difference of total alpha from which the last frame's colour is another surface's, and the span over which it is left out.
static const float ST_OTHER_FROM = 0.004;
static const float ST_OTHER_SPAN = 0.012;
// How far, in pixels, the camera's motion has slid the farthest of a pixel's layers past its nearest, from which the
// last frame's colour (reprojected as the nearest) is no longer what lies behind it, and the span over which it is left out.
static const float ST_SLID_FROM = 0.25;
static const float ST_SLID_SPAN = 0.5;
// How far from a pixel's colour, in the grain the blend leaves in it, a neighbour's still is the same colour (the smooth). PR 3663 uses 2; at 2 the smooth
// blurred textured layers here (the pan scenes of test_alpha_stochastic.py, static rmse 3.4 against 1.5 without the smooth), at 0.5 it does not (doc).
static const float ST_GRAIN_REACH = 0.5;
// The clamp box of the 3 x 3 neighbourhood is widened by this many in-pixel grains.

// This frame's weight in the temporal blend for a colour c (alpha corrected) of grain `unsure` to come out quiet: all of
// it where the colour is sure (one layer covers the pixel: sharp, no history), the least (stoch_weight) where it is far
// from it (a blend of weight w leaves w / (2 - w) of the variance).
public float st_frame_weight(vec3 c, float unsure, float current_weight)
{
    float quiet = ST_QUIET * max(max(c.r, max(c.g, c.b)), ST_QUIET_DIM);
    float q2 = quiet * quiet;
    return clamp(2.0 * q2 / (unsure * unsure + q2), current_weight, 1.0);
}

// The part of the last frame's colour left out (1: all of it): where its total alpha is not this pixel's (something else
// was there), or where the camera has slid the pixel's farthest layer past its nearest by slid_px pixels since.
public float st_left_out(float last_total, float total, float slid_px)
{
    float other = clamp((abs(last_total - total) - ST_OTHER_FROM) / ST_OTHER_SPAN, 0.0, 1.0);
    float slid = clamp((slid_px - ST_SLID_FROM) / ST_SLID_SPAN, 0.0, 1.0);
    return max(other, slid);
}

// The clamp box of the neighbourhood is widened by this much for a pixel of grain `grain`.
// What the blend leaves of a pixel's grain (as a part of it), more of it where it went without the last frame
// (left_out: st_left_out), and how far from the pixel's colour a neighbour's is still the same colour under it.
public float st_grain_reach(float unsure, float weight, float left_out)
{
    return max(ST_GRAIN_REACH * unsure * mix(sqrt(weight / (2.0 - weight)), 1.0, left_out), 1e-6);
}

// The weight of a neighbour of total alpha b in the smooth of a pixel of total alpha a: 1 for a like one, 0 from ST_OTHER_FROM apart
public float st_like_total(float a, float b)
{
    return max(1.0 - abs(a - b) / ST_OTHER_FROM, 0.0);
}

// The transparency's history at the exact reprojected position, Catmull-Rom filtered over 4 x 4 texels
// (the temporal history filter of Karis, "High Quality Temporal Supersampling", SIGGRAPH 2014; the
// weights as in Jimenez, "Filmic SMAA", SIGGRAPH 2016). The nearest texel drops the sub-pixel part of
// the camera's motion every frame, and the 1/8 blend accumulates it as a smear 1 to 2 px behind the
// image. Where the reprojection is a whole number of pixels (an offset of the texel's centre within
// 1/64 px, a static view included: the reprojection's float error is far below that, also at 4K) the weights are
// exactly 0, 1, 0, 0 and the one texel is read, as the nearest read did: the result is then bit-identical. The negative lobes are bounded by the
// 3 x 3 clamp of stoch_accumulate (the neighbourhood is never negative: nor is the result).
public vec4 stoch_history_catmull_rom(sampler2D history, vec2 puv, ivec2 size)
{
    vec2 u = puv * vec2(size);                // puv is in [0, 1) (stoch_reproject), so u is not negative and ivec2(u) is its floor
    ivec2 n = ivec2(u);                       // the texel the nearest read takes
    vec2 d = u - (vec2(n) + 0.5);             // the offset from its centre, -0.5 to 0.5
    d = vec2(abs(d.x) < 1.0 / 64.0 ? 0.0 : d.x, abs(d.y) < 1.0 / 64.0 ? 0.0 : d.y);
    if (d.x == 0.0 && d.y == 0.0)
    {
        return texelFetch(history, n, 0);
    }
    ivec2 b = n - ivec2(d.x < 0.0 ? 1 : 0, d.y < 0.0 ? 1 : 0);   // the texel left of / below the position
    vec2 t = d + vec2(d.x < 0.0 ? 1.0 : 0.0, d.y < 0.0 ? 1.0 : 0.0);
    vec2 w0 = t * (-0.5 + t * (1.0 - 0.5 * t));
    vec2 w1 = 1.0 + t * t * (-2.5 + 1.5 * t);
    vec2 w2 = t * (0.5 + t * (2.0 - 1.5 * t));
    vec2 w3 = t * t * (-0.5 + 0.5 * t);
    float wx[4] = float[4](w0.x, w1.x, w2.x, w3.x);
    float wy[4] = float[4](w0.y, w1.y, w2.y, w3.y);
    vec4 h = vec4(0.0);
    for (int j = 0; j < 4; ++j)
    {
        for (int i = 0; i < 4; ++i)
        {
            float w = wx[i] * wy[j];
            if (w != 0.0)
            {
                vec4 tap = texelFetch(history, clamp(b + ivec2(i - 1, j - 1), ivec2(0), size - 1), 0);
                // a tap that is not a finite number adds nothing (an infinity under a negative lobe would be a NaN)
                h += (stoch_finite4(tap) ? tap : vec4(0.0)) * w;
            }
        }
    }
    return h;
}
