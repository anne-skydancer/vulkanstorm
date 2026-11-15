/**
 * @file stochasticResolveUtilF.glsl
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

/*[EXTRA_CODE_HERE]*/

// Attached as a fragment object (STOCH_RESOLVE, LLShaderMgr::attachShaderFeatures) to each producer's resolve program
// (alphaStochasticResolveF.glsl and the shadows' shadowStochasticResolveF.glsl), or, for
// the shadows' compute resolve (A/B only), compiled as a compute object. The file is
// stage-neutral: it uses no stage built-ins (only uniforms, texelFetch and functions), so the same
// source serves a compute or a fragment producer.
// The producer defines stoch_current() (its value of this frame at a pixel) and its main(); the engine gives the camera
// reprojection, the history reads, the 3 x 3 neighbourhood and the blend. The design and the weights are the author's
// Aurora Viewer's (oit_stochastic.wgsl `reprojected`, cs_resolve); the transparency's grain-measuring resolve takes its
// constants, below, from Aurora Viewer PR 3663 (Gaalrauch, GPL-3.0-or-later). The uniforms are set by the
// producer's LLStochasticHistory (setResolveUniforms).

uniform mat4  stoch_reproj;          // this frame's clip space to the last frame's (camera only)
uniform float stoch_weight;          // the current frame's weight (1/8)
uniform int   stoch_history_valid;   // the history holds the last frame, at this size

vec4 stoch_current(ivec2 px);        // the producer's value of this frame

// Whether a value is a finite number, tested on its bits (the exponent is all ones for an infinity and a NaN)
// (stoch_finite(float) is in stochasticSampleF.glsl: one definition only, as both objects may meet in a program)
bool stoch_finite4(vec4 v)
{
    uvec4 e = floatBitsToUint(v) & uvec4(0x7f800000u);
    return all(notEqual(e, uvec4(0x7f800000u)));
}

bool stoch_has_history()
{
    return stoch_history_valid != 0;
}

// The last frame's clip position (pp) and texture coordinate (puv) of pixel px at window depth z,
// moved by the camera's motion alone (the surfaces' own motion is not reprojected); false where
// the last frame did not see it (behind the camera or off screen).
bool stoch_reproject(ivec2 px, float z, ivec2 size, out vec4 pp, out vec2 puv)
{
    vec4 clip = vec4((vec2(px) + 0.5) / vec2(size) * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
    pp = stoch_reproj * clip;
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
const float ST_HDR_MAX = 65504.0;    // the largest value of an RGBA16F target
// The grain a colour drawn may keep, as a part of that colour (of a dim one, as a part of ST_QUIET_DIM): a pixel under
// it is this frame's alone, one over it takes as much of the previous frames as brings it there.
const float ST_QUIET = 0.004;
const float ST_QUIET_DIM = 0.05;
// The difference of total alpha from which the last frame's colour is another surface's, and the span over which it is left out.
const float ST_OTHER_FROM = 0.004;
const float ST_OTHER_SPAN = 0.012;
// How far, in pixels, the camera's motion has slid the farthest of a pixel's layers past its nearest, from which the
// last frame's colour (reprojected as the nearest) is no longer what lies behind it, and the span over which it is left out.
const float ST_SLID_FROM = 0.25;
const float ST_SLID_SPAN = 0.5;
// How far from a pixel's colour, in the grain the blend leaves in it, a neighbour's still is the same colour (the smooth). PR 3663 uses 2; at 2 the smooth
// blurred textured layers here (the pan scenes of test_alpha_stochastic.py, static rmse 3.4 against 1.5 without the smooth), at 0.5 it does not (doc).
const float ST_GRAIN_REACH = 0.5;
// The clamp box of the 3 x 3 neighbourhood is widened by this many in-pixel grains.
const float ST_BOX_GRAIN = 1.0;

// This frame's weight in the temporal blend for a colour c (alpha corrected) of grain `unsure` to come out quiet: all of
// it where the colour is sure (one layer covers the pixel: sharp, no history), the least (stoch_weight) where it is far
// from it (a blend of weight w leaves w / (2 - w) of the variance).
float st_frame_weight(vec3 c, float unsure)
{
    float quiet = ST_QUIET * max(max(c.r, max(c.g, c.b)), ST_QUIET_DIM);
    float q2 = quiet * quiet;
    return clamp(2.0 * q2 / (unsure * unsure + q2), stoch_weight, 1.0);
}

// The part of the last frame's colour left out (1: all of it): where its total alpha is not this pixel's (something else
// was there), or where the camera has slid the pixel's farthest layer past its nearest by slid_px pixels since.
float st_left_out(float last_total, float total, float slid_px)
{
    float other = clamp((abs(last_total - total) - ST_OTHER_FROM) / ST_OTHER_SPAN, 0.0, 1.0);
    float slid = clamp((slid_px - ST_SLID_FROM) / ST_SLID_SPAN, 0.0, 1.0);
    return max(other, slid);
}

// The clamp box of the neighbourhood is widened by this much for a pixel of grain `grain`.
float st_box_widen(float grain)
{
    return ST_BOX_GRAIN * grain;
}

// What the blend leaves of a pixel's grain (as a part of it), more of it where it went without the last frame
// (left_out: st_left_out), and how far from the pixel's colour a neighbour's is still the same colour under it.
float st_grain_reach(float unsure, float weight, float left_out)
{
    return max(ST_GRAIN_REACH * unsure * mix(sqrt(weight / (2.0 - weight)), 1.0, left_out), 1e-6);
}

// The weight of a neighbour of total alpha b in the smooth of a pixel of total alpha a: 1 for a like one, 0 from ST_OTHER_FROM apart
float st_like_total(float a, float b)
{
    return max(1.0 - abs(a - b) / ST_OTHER_FROM, 0.0);
}

// Not called by the viewer: the pre-2026-11-14 transparency read, kept as the baseline of scripts/perf/test_alpha_stochastic.py.
vec4 stoch_history_nearest(sampler2D history, vec2 puv, ivec2 size)
{
    return texelFetch(history, ivec2(puv * vec2(size)), 0);
}

// The transparency's history at the exact reprojected position, Catmull-Rom filtered over 4 x 4 texels
// (the temporal history filter of Karis, "High Quality Temporal Supersampling", SIGGRAPH 2014; the
// weights as in Jimenez, "Filmic SMAA", SIGGRAPH 2016). The nearest texel drops the sub-pixel part of
// the camera's motion every frame, and the 1/8 blend accumulates it as a smear 1 to 2 px behind the
// image. Where the reprojection is a whole number of pixels (an offset of the texel's centre within
// 1/64 px, a static view included: the reprojection's float error is far below that, also at 4K) the weights are
// exactly 0, 1, 0, 0 and the one texel is read, as the nearest read did: the result is then bit-identical. The negative lobes are bounded by the
// 3 x 3 clamp of stoch_accumulate (the neighbourhood is never negative: nor is the result).
vec4 stoch_history_catmull_rom(sampler2D history, vec2 puv, ivec2 size)
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

// For the shadow producer (not used by the transparency): bilinear history from the 4 nearest texels, each kept only where its stored linear depth
// (channel depth_channel) agrees with the expected one (disocclusion); returns the kept weight.
float stoch_history_bilinear(sampler2D history, vec2 puv, ivec2 size, float w_expected, int depth_channel, out vec4 hist)
{
    vec2 f = puv * vec2(size) - 0.5;
    ivec2 b = ivec2(floor(f));
    vec2 fr = f - vec2(b);
    hist = vec4(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        ivec2 o = ivec2(i & 1, i >> 1);
        vec4 h = texelFetch(history, clamp(b + o, ivec2(0), size - 1), 0);
        float wb = (o.x == 1 ? fr.x : 1.0 - fr.x) * (o.y == 1 ? fr.y : 1.0 - fr.y);
        float ok = abs(h[depth_channel] - w_expected) <= 0.02 * w_expected + 0.02 ? 1.0 : 0.0;
        hist += h * (wb * ok);
        wsum += wb * ok;
    }
    if (wsum > 0.0)
    {
        hist /= wsum;
    }
    return wsum;
}

// The 3 x 3 min / max of this frame's values around px (the TAA clamp box): the history is clamped
// to it, so that what moved shows no ghost beyond what the neighbourhood holds.
void stoch_neighbourhood(ivec2 px, ivec2 size, vec4 cur, out vec4 mn, out vec4 mx)
{
    mn = cur;
    mx = cur;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            vec4 c = stoch_current(clamp(px + ivec2(dx, dy), ivec2(0), size - ivec2(1)));
            mn = min(mn, c);
            mx = max(mx, c);
        }
    }
}

vec4 stoch_accumulate(vec4 hist, vec4 mn, vec4 mx, vec4 cur)
{
    return mix(clamp(hist, mn, mx), cur, stoch_weight);
}

// For the shadow producer (not used by the transparency): per channel weights: 1 passes this frame's value through (a channel the producer does not accumulate)
vec4 stoch_accumulate_w(vec4 hist, vec4 mn, vec4 mx, vec4 cur, vec4 weight)
{
    return mix(clamp(hist, mn, mx), cur, weight);
}
