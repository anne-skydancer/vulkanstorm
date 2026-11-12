/**
 * @file localLightWeightUtil.glsl
 * @brief The light pool's local light weight: a light's unshadowed contribution at a receiver (unified lighting U1)
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

// The light pool's side of the interface the shadow pool's ratio pass (localLightRatioF.glsl) calls: the photometry of one
// light of the light table (lllocallighttable.h), with no knowledge of shadows. Photometry P1 will grow it (P1/P2 make it
// localLightE). Listed among the ratio program's own files.
//
// A row of the table has 8 vec4 (all view space), in a std140 uniform block of 256 rows (32 KB) when the device's
// uniform blocks hold that (LOCAL_LIGHT_TABLE_UBO; a warp reads one row, which the constant cache broadcasts) and in an
// RGBA32F texture otherwise (the same rows):
//   0  centre.xyz, size (the light's radius, volume->getLightRadius() * 1.5)
//   1  colour.rgb (linear; a worn light's capped at 1.0), falloff
//   2  origin.xyz (the projector's eye, or the centre), kind (1 a projector)
//   3  the projector texture's average luminance (1 for none), the emitter's radius s, the exclusion radius r_excl, worn (1 an
//      attachment)
//   4-7 the projector matrix (view space to the light's texture space, column major); zero for a point light
// A worn light follows #437's caps for shadow purposes: it casts within WORN_REACH of its centre and is lit unshadowed beyond
// (it is out of the pixel's weights there), and its colour in the table is capped on the CPU.

#define WORN_REACH 6.0   // metres
#define WEIGHT_RADIUS 0.9293   // the share of its radius a light has weight within: beyond it the weight is under 0.5 % of its peak, so the
                               // tile lists (CPU rectangles, GPU cull) are exact at it and the light is lit unshadowed there

#ifdef LOCAL_LIGHT_TABLE_UBO
layout(std140) uniform LLLocalLightTable { vec4 ll_rows[2048]; };
#define LL_ROW(j, idx) ll_rows[(idx) * 8 + (j)]
#else
uniform sampler2D local_light_table;
#define LL_ROW(j, idx) texelFetch(local_light_table, ivec2((j), (idx)), 0)
#endif

// the light's unshadowed contribution to a pixel at pos with the unit normal n, per colour channel: colour x the legacy
// attenuation x n.l (x the projector texture's average luminance), inside its radius (a projector's frustum) and, worn,
// WORN_REACH. calcLegacyDistanceAttenuation's 2 (1 - d)^2 / (1 + falloff)^2 for d < 1, written without its divisions.
vec3 localLightWeight(int idx, vec3 pos, vec3 n)
{
    vec4 a = LL_ROW(0, idx);   // centre, size
    vec4 b = LL_ROW(1, idx);   // colour, falloff
    vec4 c = LL_ROW(2, idx);   // origin, kind
    vec4 d = LL_ROW(3, idx);   // texture, s, r_excl, worn
    float reach = d.w > 0.5 ? min(a.w * WEIGHT_RADIUS, WORN_REACH) : a.w * WEIGHT_RADIUS;
    vec3 lv = a.xyz - pos;
    float d2 = dot(lv, lv);
    if (d2 >= reach * reach)
    {
        return vec3(0.0);
    }
    float dist = d2 * inversesqrt(d2);
    float tex = 1.0;
    if (c.w > 0.5)
    {
        mat4 proj_mat = mat4(LL_ROW(4, idx), LL_ROW(5, idx), LL_ROW(6, idx), LL_ROW(7, idx));
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
        tex = d.x;
    }
    float one = max(1.0 - dist / a.w, 0.0);
    float g = 1.0 / (1.0 + b.w);
    float nl = max(dot(n, lv) * inversesqrt(dot(lv, lv)), 0.0);
    return b.rgb * (2.0 * g * g * one * one * nl * tex);
}

// the light's emitter: the eye of a projector or the centre of a point light, the radius s of its disc, and r_excl, the
// distance from the eye within which the screen march stops (the light's own prim)
void localLightEmitter(int idx, out vec3 eye, out float radius, out float r_excl)
{
    vec4 c = LL_ROW(2, idx);
    vec4 d = LL_ROW(3, idx);
    eye = c.xyz;
    radius = d.y;
    r_excl = d.z;
}

// the light's reach for the march: its radius, and WORN_REACH for a worn light
float localLightReach(int idx)
{
    return LL_ROW(3, idx).w > 0.5 ? min(LL_ROW(0, idx).w, WORN_REACH) : LL_ROW(0, idx).w;
}
