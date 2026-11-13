/**
 * @file localLightRatioResolveF.glsl
 * @brief The temporal resolve of the local light ratio (unified lighting U1), in the stochastic core's history
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

// The ratio pass's value (localLightRatioF.glsl) blended with the last frame's ratio, moved by the camera, by the
// engine's rules (the shadow pool's, as the sun's resolve): the history is read bilinearly from the 4 nearest
// texels, kept where the stored depth agrees (the depth is the sun history's: the ratio history has no channel for
// it), and clamped per channel to this frame's 3 x 3 neighbourhood. Where a projector changed since last frame, the
// share of its light (the raw's alpha) takes this frame's value instead of the history.

uniform sampler2D shadow_st_raw;
uniform sampler2D shadow_st_history;    // the ratio's last frame
uniform sampler2D shadow_st_resolved;   // the sun history's last frame: g the linear view depth
uniform sampler2D depthMap;
uniform float shadow_st_scale;          // 1; 1/8 for the sum of 8 passes
uniform float stoch_weight;
uniform mat4 inv_proj;

bool stoch_has_history();
bool stoch_reproject(ivec2 px, float z, ivec2 size, out vec4 pp, out vec2 puv);
float stoch_history_bilinear_ext(sampler2D history, sampler2D depth_history, vec2 puv, ivec2 size, float w_expected, int depth_channel, out vec4 hist);
void stoch_neighbourhood(ivec2 px, ivec2 size, vec4 cur, out vec4 mn, out vec4 mx);

#define RATIO_MIN_KEPT 0.25   // less of the 4 history texels kept than this: no history

// the engine's resolve util calls this
vec4 stoch_current(ivec2 px)
{
    return texelFetch(shadow_st_raw, px, 0) * shadow_st_scale;
}

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(shadow_st_raw, 0);
    vec4 cur = stoch_current(px);
    // a non-finite value (a NaN weight, a march at the camera plane) is no estimate: R = 1, and never enters the history
    if (any(isnan(cur)) || any(isinf(cur)))
    {
        cur = vec4(1.0, 1.0, 1.0, 0.0);
    }
    vec3 res = cur.rgb;
    float z = texelFetch(depthMap, px, 0).r;
    vec4 pp;
    vec2 puv;
    vec4 hist;
    if (z < 1.0 && stoch_has_history() && stoch_reproject(px, z, size, pp, puv))
    {
        vec4 pos = inv_proj * vec4((vec2(px) + 0.5) / vec2(size) * 2.0 - 1.0, 2.0 * z - 1.0, 1.0);
        float w_cur = -pos.z / pos.w;
        if (stoch_history_bilinear_ext(shadow_st_history, shadow_st_resolved, puv, size, pp.w * w_cur, 1, hist) >= RATIO_MIN_KEPT
            && !any(isnan(hist)) && !any(isinf(hist)))
        {
            vec4 mn;
            vec4 mx;
            stoch_neighbourhood(px, size, cur, mn, mx);
            float weight = mix(stoch_weight, 1.0, clamp(cur.a, 0.0, 1.0));
            res = mix(clamp(hist.rgb, mn.rgb, mx.rgb), cur.rgb, weight);
        }
    }
    frag_color = vec4(res, 1.0);
}
