/**
 * @file alphaStochasticResolveF.glsl
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

/*[EXTRA_CODE_HERE]*/

out vec4 frag_data[2];   // 0: the history's colour (RGBA16F), 1: (total alpha, the part of the last frame left out) (RG16F)

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
// The last frame's total alpha is the second target of the history (RG16F, with the part left out).

uniform sampler2D   alpha_st_current;  // this frame's averaged accumulation (RGBA16F)
uniform sampler2D   alpha_st_aux;      // (grain, total alpha, 1 - nearest depth, 1 - farthest depth) (RGBA16F)
uniform sampler2D   alpha_st_history;  // the last frame's blended four-vector (RGBA16F)
uniform sampler2D   alpha_st_extra;    // the last frame's (total alpha, part left out) (RG16F)

bool stoch_has_history();
bool stoch_reproject(ivec2 px, float z, ivec2 size, out vec4 pp, out vec2 puv);
vec4 stoch_history_catmull_rom(sampler2D history, vec2 puv, ivec2 size);
bool stoch_finite4(vec4 v);
float st_frame_weight(vec3 c, float unsure);
float st_left_out(float last_total, float total, float slid_px);
float st_box_widen(float grain);

const float ST_HDR_MAX = 65504.0;   // the largest value of an RGBA16F target

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

void write_out(vec4 colour, float total, float left_out)
{
    frag_data[0] = colour;
    frag_data[1] = vec4(total, left_out, 0.0, 0.0);
}

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(alpha_st_history, 0);
    vec4 cur = stoch_current(px);
    vec4 aux = texelFetch(alpha_st_aux, px, 0);
    float total = aux.g;
    if (!stoch_has_history() || cur.a <= 0.0 || aux.b <= 0.0)
    {
        write_out(cur, total, 1.0);
        return;
    }
    // the weight of this frame, from the grain of the colour as it will be drawn (alpha corrected)
    float corr = cur.a > 1e-5 ? total / cur.a : 1.0;
    float weight = st_frame_weight(min(cur.rgb * corr, vec3(ST_HDR_MAX)), min(aux.r * corr, ST_HDR_MAX));
    if (weight >= 1.0)
    {
        write_out(cur, total, 1.0);
        return;
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
    float grain = st_box_widen(aux.r);
    mn.rgb -= vec3(grain);
    mx.rgb += vec3(grain);

    vec4 pp;
    vec2 puv;
    vec4 pp_far;
    vec2 puv_far;
    if (!stoch_reproject(px, 1.0 - aux.b, size, pp, puv))
    {
        write_out(cur, total, 1.0);
        return;
    }
    // the farthest layer where the camera has slid it to; off screen or behind the camera is a slide of all of the span
    float slid_px = 1e6;
    if (stoch_reproject(px, 1.0 - far_min, size, pp_far, puv_far))
    {
        slid_px = length((puv_far - puv) * vec2(size));
    }
    float left_out = st_left_out(st_last_total(puv, size), total, slid_px);
    if (left_out >= 1.0)
    {
        write_out(cur, total, 1.0);
        return;
    }
    vec4 old = stoch_history_catmull_rom(alpha_st_history, puv, size);
    vec4 blended = mix(clamp(old, mn, mx), cur, mix(weight, 1.0, left_out));
    // the history never takes a value that is not a finite number
    if (!stoch_finite4(blended))
    {
        write_out(cur, total, 1.0);
        return;
    }
    // (no part over what the RGBA16F history holds: a half float stores a larger value as an infinity)
    write_out(clamp(blended, vec4(0.0), vec4(ST_HDR_MAX)), total, left_out);
}
