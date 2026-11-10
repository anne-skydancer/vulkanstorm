/**
 * @file stochasticResolveUtilF.glsl
 * @brief The stochastic engine's temporal resolve, shared by its producers
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

// Attached to each producer's resolve program (STOCH_RESOLVE, LLShaderMgr::attachShaderFeatures).
// The producer defines stoch_current() (its value of this frame at a pixel: the averaged samples
// of the transparency) and its main(); the engine gives the camera reprojection, the history
// reads, the 3 x 3 neighbourhood and the blend. The design and the weights are the author's
// Aurora Viewer's (oit_stochastic.wgsl `reprojected`, cs_resolve). The uniforms are set by the
// producer's LLStochasticHistory (setResolveUniforms).

uniform mat4  stoch_reproj;          // this frame's clip space to the last frame's (camera only)
uniform float stoch_weight;          // the current frame's weight (1/8)
uniform int   stoch_history_valid;   // the history holds the last frame, at this size

vec4 stoch_current(ivec2 px);        // the producer's value of this frame

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

vec4 stoch_history_nearest(sampler2D history, vec2 puv, ivec2 size)
{
    return texelFetch(history, ivec2(puv * vec2(size)), 0);
}

// Bilinear history from the 4 nearest texels, each kept only where its stored linear depth
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

// per channel weights: 1 passes this frame's value through (a channel the producer does not accumulate)
vec4 stoch_accumulate_w(vec4 hist, vec4 mn, vec4 mx, vec4 cur, vec4 weight)
{
    return mix(clamp(hist, mn, mx), cur, weight);
}
