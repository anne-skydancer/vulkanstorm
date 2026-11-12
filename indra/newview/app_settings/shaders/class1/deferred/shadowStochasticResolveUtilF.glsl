/**
 * @file shadowStochasticResolveUtilF.glsl
 * @brief The Stochastic shadow method's temporal resolve, for either stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 */

/*[EXTRA_CODE_HERE]*/

// Stage-neutral (only uniforms, texelFetch and functions, like the engine's stochasticResolveUtilF.glsl),
// so that the fragment resolve (the OpenGL viewer) and the compute resolve (kept for the Vulkan renderer) run this one
// source and give the same result. Each adds its own main: shadowStochasticResolveF.glsl and
// shadowStochasticResolveC.glsl.
//
// The Stochastic shadow method's part of the stochastic engine's temporal resolve
// (stochasticResolveUtilF.glsl, attached as the STOCH_RESOLVE object). This frame's sun pass
// (shadow_st_raw: r sun, g AO, b and a the two projector slots; 8 rays per pixel) is blended with
// the last frame's result (shadow_st_history: r sun, g the linear view depth, b and a the
// projectors), moved by the camera's motion alone. The history is read bilinearly from the 4 nearest
// texels, each kept only where its stored depth agrees with the expected one (a revealed surface
// has no history), and clamped per channel to the 3 x 3 neighbourhood of this frame's values.
// The result is the next frame's history; shadowStochasticComposeF.glsl makes the light map of it.

uniform sampler2D shadow_st_raw;
uniform sampler2D shadow_st_history;
uniform sampler2D depthMap;
uniform vec3 shadow_st_channel_valid;   // the sun, projector slot 0 and 1: the channel's history is the same light as last frame's
uniform float stoch_weight;             // the engine's: this frame's weight (1/8)
uniform mat4 inv_proj;                  // the projection's inverse: set by the pipeline for both resolve paths

bool stoch_has_history();
bool stoch_reproject(ivec2 px, float z, ivec2 size, out vec4 pp, out vec2 puv);
float stoch_history_bilinear(sampler2D history, vec2 puv, ivec2 size, float w_expected, int depth_channel, out vec4 hist);
void stoch_neighbourhood(ivec2 px, ivec2 size, vec4 cur, out vec4 mn, out vec4 mx);
vec4 stoch_accumulate_w(vec4 hist, vec4 mn, vec4 mx, vec4 cur, vec4 weight);

#define SHADOW_ST_MIN_KEPT 0.25   // less of the 4 history texels kept than this: no history

vec4 stoch_current(ivec2 px)
{
    return texelFetch(shadow_st_raw, px, 0);
}

// the linear view depth of the pixel at window depth z (what getPosition gives, without deferredUtil.glsl)
float shadow_linear_depth(ivec2 px, ivec2 size, float z)
{
    vec4 pos = inv_proj * vec4((vec2(px) + 0.5) / vec2(size) * 2.0 - 1.0, 2.0 * z - 1.0, 1.0);
    return -pos.z / pos.w;
}


// the resolved value of pixel px: r sun, g linear view depth, b and a the projector slots
vec4 shadow_resolve(ivec2 px, ivec2 size)
{
    vec4 cur = stoch_current(px);
    float z = texelFetch(depthMap, px, 0).r;
    float w_cur = shadow_linear_depth(px, size, z);
    vec4 res = vec4(cur.r, w_cur, cur.b, cur.a);

    vec4 pp;
    vec2 puv;
    vec4 hist;
    if (z < 1.0 && stoch_has_history() && stoch_reproject(px, z, size, pp, puv) &&
        stoch_history_bilinear(shadow_st_history, puv, size, pp.w * w_cur, 1, hist) >= SHADOW_ST_MIN_KEPT)   // pp.w: last frame's clip w over this frame's
    {
        vec4 mn;
        vec4 mx;
        stoch_neighbourhood(px, size, cur, mn, mx);
        // per channel: a channel whose light changed (or the AO and depth channels, which are not accumulated) takes this frame's value
        vec4 weight = vec4(shadow_st_channel_valid.x > 0.5 ? stoch_weight : 1.0, 1.0,
                           shadow_st_channel_valid.y > 0.5 ? stoch_weight : 1.0,
                           shadow_st_channel_valid.z > 0.5 ? stoch_weight : 1.0);
        vec4 acc = stoch_accumulate_w(hist, mn, mx, cur, weight);
        res = vec4(acc.r, w_cur, acc.b, acc.a);
    }
    return res;
}
