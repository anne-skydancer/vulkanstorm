/**
 * @file localLightRatioF.glsl
 * @brief The shadow pool's local light ratio (unified lighting U1): the visibility every local light is lit by
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

// The ratio of the local lights' light at a pixel that the shadows leave: R = sum_i w_i V_i / sum_i w_i per colour
// channel, over the lights of the pixel's 32 x 32 screen tile (the light pool's table and tile list), with
// w_i the light's unshadowed contribution (localLightW: its photometry without the surface's colour) and V_i its
// visibility. The light passes multiply their direct light by R (after the temporal resolve of this pass,
// localLightRatioResolveF), which is exact when the weights are proportional to the passes' light (diffuse; a
// projector's texture is not in them).
// Stage U1: V_i comes from the projector maps (stTransmittance, two lights); every other light has V = 1.
// A pixel no map shades (V = 1 for both) is R = 1 without a loop.
// Output: rgb the ratio, a the share of the pixel's light that comes from a projector that changed since last frame
// (its visibility has no history).

in vec2 vary_fragcoord;

uniform sampler2D local_light_table;     // RGBA32F, 8 texels per light: see lllocallighttable.h
uniform usampler2D local_light_tiles;    // R8UI, 33 texels per tile, tile (tx, ty) at x = 33 tx, y = ty: the count, then the light indices
uniform vec2 spot_slot_on;               // 1 where the projector slot holds a light this frame
uniform vec2 spot_shadow_fade;           // 1 - the slot's fade-in (the projectors cross-fade when they swap)

vec4 getNorm(vec2 screenpos);
vec4 getPosition(vec2 pos_screen);
float calcLegacyDistanceAttenuation(float distance, float falloff);
float sampleSpotShadow(vec3 pos, vec3 norm, int index, vec2 pos_screen);

// The light's unshadowed contribution to a pixel at pos with the normal n (view space), per colour channel; the
// slot of its projector map in `slot` (-1: none) and whether that light changed since last frame in `changed`.
vec3 localLightW(int idx, vec3 pos, vec3 n, out int slot, out float changed)
{
    vec4 a = texelFetch(local_light_table, ivec2(0, idx), 0);   // centre (view space), size
    vec4 b = texelFetch(local_light_table, ivec2(1, idx), 0);   // colour, falloff
    vec4 c = texelFetch(local_light_table, ivec2(2, idx), 0);   // origin (view space), kind: 1 projector
    vec4 d = texelFetch(local_light_table, ivec2(3, idx), 0);   // slot, changed
    slot = int(d.x);
    changed = d.y;
    vec3 lv = a.xyz - pos;
    float dist = length(lv);
    if (dist >= a.w)
    {
        return vec3(0.0);
    }
    if (c.w > 0.5)
    {
        mat4 proj_mat = mat4(texelFetch(local_light_table, ivec2(4, idx), 0), texelFetch(local_light_table, ivec2(5, idx), 0),
                             texelFetch(local_light_table, ivec2(6, idx), 0), texelFetch(local_light_table, ivec2(7, idx), 0));
        vec4 pt = proj_mat * vec4(pos, 1.0);
        if (pt.z < 0.0)
        {
            return vec3(0.0);
        }
        pt.xy /= pt.w;
        if (pt.x <= 0.0 || pt.x >= 1.0 || pt.y <= 0.0 || pt.y >= 1.0)
        {
            return vec3(0.0);
        }
        lv = c.xyz - pos;
    }
    float atten = calcLegacyDistanceAttenuation(dist / a.w, b.w);
    float nl = max(dot(n, normalize(lv)), 0.0);
    return b.rgb * (atten * nl);
}

void main()
{
    vec2 pos_screen = vary_fragcoord.xy;
    vec4 pos = getPosition(pos_screen);
    vec4 norm = getNorm(pos_screen);

    vec2 v = vec2(1.0);
    if (spot_slot_on.x > 0.5)
    {
        v.x = clamp(sampleSpotShadow(pos.xyz, norm.xyz, 0, pos_screen) + spot_shadow_fade.x, 0.0, 1.0);
    }
    if (spot_slot_on.y > 0.5)
    {
        v.y = clamp(sampleSpotShadow(pos.xyz, norm.xyz, 1, pos_screen) + spot_shadow_fade.y, 0.0, 1.0);
    }
    if (v.x >= 1.0 && v.y >= 1.0)
    {
        frag_color = vec4(1.0, 1.0, 1.0, 0.0);
        return;
    }

    ivec2 px = ivec2(gl_FragCoord.xy);
    ivec2 tile = ivec2((px.x >> 5) * 33, px.y >> 5);
    int count = int(texelFetch(local_light_tiles, tile, 0).r);
    vec3 n = normalize(norm.xyz);
    vec3 num = vec3(0.0);
    vec3 den = vec3(0.0);
    float den_all = 0.0;
    float den_changed = 0.0;
    for (int i = 0; i < count; ++i)
    {
        int idx = int(texelFetch(local_light_tiles, tile + ivec2(1 + i, 0), 0).r);
        int slot;
        float changed;
        vec3 w = localLightW(idx, pos.xyz, n, slot, changed);
        float vis = slot == 0 ? v.x : (slot == 1 ? v.y : 1.0);
        num += w * vis;
        den += w;
        float ws = w.r + w.g + w.b;
        den_all += ws;
        den_changed += changed * ws;
    }
    vec3 ratio = vec3(1.0);
    if (den.r > 0.0) ratio.r = num.r / den.r;
    if (den.g > 0.0) ratio.g = num.g / den.g;
    if (den.b > 0.0) ratio.b = num.b / den.b;
    frag_color = vec4(clamp(ratio, vec3(0.0), vec3(1.0)), den_all > 0.0 ? den_changed / den_all : 0.0);
}
