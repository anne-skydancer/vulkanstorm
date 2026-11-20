/**
 * @file alphaStochasticResolve.slang
 * @brief Stochastic transparency: the grain-weighted temporal accumulation of the resolved colour
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
import glsl;
import postfullscreen;
import stochasticAlphaSamples;
import stochasticAlphaHistory;

// The transparency's part of the stochastic engine's temporal resolve (stochasticResolveUtilF.glsl).
// alphaStochasticCurrentF.glsl gives each pixel this frame's accumulation (alpha x colour, alpha), its grain (the
// standard error of the colour) and the nearest and farthest stochastic depth. The accumulation is blended with
// the last frame's, read where the nearest transparent surface was, moved by the camera's motion alone (the surfaces'
// own motion is not reprojected), with a Catmull-Rom filter, and clamped to the 3 x 3 neighbourhood of this frame as a
// TAA's history is, the box widened by the pixel's own grain (the colour this frame's samples left is that far from
// the colour they would show without noise: the history may be, too). As Aurora Viewer PR 3663 (Gaalrauch,
// GPL-3.0-or-later; oit_stochastic.wgsl cs_temporal) does:
//   - this frame's weight comes from the grain (st_frame_weight): 1 where one layer covers the pixel (sharp, no
//     history), the engine's 1/8 where the grain is large;
//   - the last frame is left out where its total alpha is not this pixel's (something else was there), and where the
//     camera has slid the pixel's farthest layer more than 1/4 px past its nearest since (what shows through the
//     nearest is no longer what was behind it); the part left out goes to the smooth, which has less to average
//     where the history helped.
// The history keeps this engine's format: the four-vector (alpha x colour, alpha), the alpha correction (total alpha
// over the blended alpha) made at the composite, with this frame's total alpha so that an alpha change is never
// ghosted. #3663 stores the corrected colour; offline that made the low-frequency error twice as large (measured offline at S = 4).
// Where the measuring pass's mask is 0 (nothing transparent drawn: every lane empty) the pixel is written empty at once.
// The last frame's total alpha is the second target of the history (RG16F, with the part left out).

uniform sampler2D   alpha_st_current;  // this frame's averaged accumulation (RGBA16F)
uniform sampler2D   alpha_st_aux;      // (grain, total alpha, 1 - nearest depth, 1 - farthest depth) (RGBA16F)
uniform sampler2D   alpha_st_history;  // the last frame's blended four-vector (RGBA16F)
uniform sampler2D   alpha_st_extra;    // the last frame's (total alpha, part left out) (RG16F)
uniform sampler2D   alpha_st_mask;     // 1 where anything transparent was drawn (alphaStochasticCurrentF.glsl, R8)

uniform mat4 stoch_reproj;
uniform float stoch_weight;
uniform int stoch_history_valid;

vec4 stoch_current(ivec2 px)
{
    return texelFetch(alpha_st_current, px, 0);
}

// the history's total alpha at the position, between its four texels
float st_last_total(vec2 puv, ivec2 size)
{
    vec2 f = puv * vec2(size) - 0.5;
    ivec2 b = ivec2(floor(f));
    vec2 t = f - vec2(b);
    float a00 = texelFetch(alpha_st_extra, clamp(b, ivec2(0), size - 1), 0).r;
    float a10 = texelFetch(alpha_st_extra, clamp(b + ivec2(1, 0), ivec2(0), size - 1), 0).r;
    float a01 = texelFetch(alpha_st_extra, clamp(b + ivec2(0, 1), ivec2(0), size - 1), 0).r;
    float a11 = texelFetch(alpha_st_extra, clamp(b + ivec2(1, 1), ivec2(0), size - 1), 0).r;
    return mix(mix(a00, a10, t.x), mix(a01, a11, t.x), t.y);
}

struct ResolveOut
{
    vec4 colour : SV_Target0;
    vec4 extra : SV_Target1;
};

ResolveOut resolveResult(vec4 colour, float total, float left_out)
{
    ResolveOut result;
    result.colour = colour;
    result.extra = vec4(total, left_out, 0.0, 0.0);
    return result;
}

[shader("vertex")]
PostVaryings vsMain(PostVertexIn v)
{
    return postFullscreenVertex(v);
}

[shader("fragment")]
ResolveOut fsMain(PostVaryings input)
{
    ivec2 px = ivec2(input.pos.xy);
    ivec2 size = textureSize(alpha_st_history, 0);
    if (texelFetch(alpha_st_mask, px, 0).r == 0.0)
    {
        // nothing drawn here: the accumulation and the total alpha are 0, and the result below is (0, 0, 0, 0) with the
        // part left out 1 (cur.a <= 0): what it writes, without the reads. The history is written for every pixel
        // (the textures alternate: a pixel left unwritten would keep the frame before last).
        return resolveResult(vec4(0.0), 0.0, 1.0);
    }
    vec4 cur = stoch_current(px);
    vec4 aux = texelFetch(alpha_st_aux, px, 0);
    float total = aux.g;
    if (stoch_history_valid == 0 || cur.a <= 0.0 || aux.b <= 0.0)
    {
        return resolveResult(cur, total, 1.0);
    }
    // the weight of this frame, from the grain of the colour as it will be drawn (alpha corrected)
    float corr = cur.a > 1e-5 ? total / cur.a : 1.0;
    float weight = st_frame_weight(min(cur.rgb * corr, vec3(HDR_MAX)), min(aux.r * corr, HDR_MAX), stoch_weight);
    if (weight >= 1.0)
    {
        return resolveResult(cur, total, 1.0);
    }
    // the 3 x 3 neighbourhood: the clamp box of this frame's accumulation, and the farthest depth, as which of a
    // pixel's layers win its few samples is chance, and so would be what is left out. A value that is not a finite
    // number is no part of the box (it would take every pixel that reads it).
    ivec2 last = size - ivec2(1);
    vec4 mn = cur;
    vec4 mx = cur;
    float far_min = aux.a;   // 1 - the farthest depth
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            ivec2 q = clamp(px + ivec2(dx, dy), ivec2(0), last);
            vec4 c = stoch_current(q);
            if (stoch_finite4(c))
            {
                mn = min(mn, c);
                mx = max(mx, c);
            }
            far_min = min(far_min, texelFetch(alpha_st_aux, q, 0).a);
        }
    }
    float grain = aux.r;
    mn.rgb -= vec3(grain);
    mx.rgb += vec3(grain);

    vec4 pp;
    vec2 puv;
    vec4 pp_far;
    vec2 puv_far;
    if (!stoch_reproject(stoch_reproj, px, 1.0 - aux.b, size, pp, puv))
    {
        return resolveResult(cur, total, 1.0);
    }
    // the farthest layer where the camera has slid it to; off screen or behind the camera is a slide of all of the span
    float slid_px = 1e6;
    if (stoch_reproject(stoch_reproj, px, 1.0 - far_min, size, pp_far, puv_far))
    {
        slid_px = length((puv_far - puv) * vec2(size));
    }
    float left_out = st_left_out(st_last_total(puv, size), total, slid_px);
    if (left_out >= 1.0)
    {
        return resolveResult(cur, total, 1.0);
    }
    vec4 old = stoch_history_catmull_rom(alpha_st_history, puv, size);
    vec4 blended = mix(clamp(old, mn, mx), cur, mix(weight, 1.0, left_out));
    // the history never takes a value that is not a finite number
    if (!stoch_finite4(blended))
    {
        return resolveResult(cur, total, 1.0);
    }
    // (no part over what the RGBA16F history holds: a half float stores a larger value as an infinity)
    return resolveResult(clamp(blended, vec4(0.0), vec4(HDR_MAX)), total, left_out);
}
