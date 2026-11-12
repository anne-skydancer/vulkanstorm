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
// localLightE). Listed among the ratio program's own files; it needs calcLegacyDistanceAttenuation (deferredUtil.glsl).
//
// A row of the table has 8 RGBA32F texels (all view space): centre and size; colour and falloff; origin and kind (1 a
// projector); the projector's texture as its average luminance (a stopgap for the texture itself, P3); and the projector
// matrix in texels 4 to 7.

uniform sampler2D local_light_table;

float calcLegacyDistanceAttenuation(float distance, float falloff);

// the light's unshadowed contribution to a pixel at pos with the unit normal n, per colour channel; rel is its share of the
// light's own peak (the attenuation and n.l, no colour or texture): the ratio pass leaves out a light under 2 % of its peak
// at a pixel (it keeps the light's share of the pixel's weights, not its brightness, in the estimator)
vec3 localLightWR(int idx, vec3 pos, vec3 n, out float rel)
{
    rel = 0.0;
    vec4 a = texelFetch(local_light_table, ivec2(0, idx), 0);   // centre, size
    vec4 b = texelFetch(local_light_table, ivec2(1, idx), 0);   // colour, falloff
    vec4 c = texelFetch(local_light_table, ivec2(2, idx), 0);   // origin, kind (fetched together: independent loads overlap)
    vec3 lv = a.xyz - pos;
    float dist = length(lv);
    if (dist >= a.w)
    {
        return vec3(0.0);
    }
    float tex = 1.0;
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
        tex = texelFetch(local_light_table, ivec2(3, idx), 0).x;
    }
    float atten = calcLegacyDistanceAttenuation(dist / a.w, b.w);
    float nl = max(dot(n, normalize(lv)), 0.0);
    rel = atten / calcLegacyDistanceAttenuation(0.0, b.w) * nl;
    return b.rgb * (atten * nl * tex);
}

vec3 localLightW(int idx, vec3 pos, vec3 n)
{
    float rel;
    return localLightWR(idx, pos, n, rel);
}

// the light's emitter: the eye of a projector or the centre of a point light, the radius s of its disc, and r_excl, the
// distance from the eye within which the screen march stops (the light's own prim)
void localLightEmitter(int idx, out vec3 eye, out float radius, out float r_excl)
{
    vec4 c = texelFetch(local_light_table, ivec2(2, idx), 0);
    vec4 d = texelFetch(local_light_table, ivec2(3, idx), 0);
    eye = c.xyz;
    radius = d.y;
    r_excl = d.z;
}

// the light's reach (the radius of its volume)
float localLightReach(int idx)
{
    return texelFetch(local_light_table, ivec2(0, idx), 0).w;
}
