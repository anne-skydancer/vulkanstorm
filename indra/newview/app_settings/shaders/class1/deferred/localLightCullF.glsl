/**
 * @file localLightCullF.glsl
 * @brief The shadow pool's GPU tile cull (unified lighting U2): each 32 x 32 tile keeps the lights that can light its pixels
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

// For each tile, the lights of the CPU's coarse list (the screen rectangles) that can still have a weight on a pixel of it: the
// tile's depth bounds (localLightBlockF.glsl, 8 x 8 blocks of 4 x 4 pixels) and its four side planes make a convex frustum
// piece, whose 8 corners are unprojected. A light is dropped when its sphere (the weight's radius, localLightWeightUtil.glsl)
// misses the corners' box or lies outside a side plane, and a projector when all 8 corners are outside one of its frustum's
// planes (the weight's own tests). Only lights with weight 0 on every pixel of the tile are dropped, so the ratio is the same.
// The list keeps its order. Output: 3 RGBA32UI layers (the tile's 48 bytes: the count, then the light indices; MRT into an array).

#ifdef LOCAL_LIGHT_TABLE_UBO
layout(std140) uniform LLLocalLightTable { vec4 ll_rows[2048]; };
#define LL_ROW(j, idx) ll_rows[(idx) * 8 + (j)]
#else
uniform sampler2D local_light_table;
#define LL_ROW(j, idx) texelFetch(local_light_table, ivec2((j), (idx)), 0)
#endif

uniform sampler2D st_block_depth;        // the bounds: r the nearest, g the farthest
uniform usampler2DArray local_light_tiles;   // the coarse list, packed
uniform mat4 inv_proj;
uniform vec2 screen_res;

#define WEIGHT_RADIUS 0.9293
#define WORN_REACH 6.0

layout(location = 0) out uvec4 o0;
layout(location = 1) out uvec4 o1;
layout(location = 2) out uvec4 o2;

void main()
{
    ivec2 t = ivec2(gl_FragCoord.xy);
    ivec2 bs = textureSize(st_block_depth, 0);
    float mn = 1.0;
    float mx = 0.0;
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            vec2 v = texelFetch(st_block_depth, min(t * 8 + ivec2(x, y), bs - 1), 0).xy;
            mn = min(mn, v.x);
            mx = max(mx, v.y);
        }
    }
    uint ow[12];
    for (int i = 0; i < 12; ++i)
    {
        ow[i] = 0u;
    }
    int n = 0;
    if (mn <= mx)
    {
        vec2 p0 = vec2(t * 32);
        vec2 p1 = min(vec2(t * 32 + 32), screen_res);
        vec3 c[8];
        vec3 bmin = vec3(1.0e30);
        vec3 bmax = vec3(-1.0e30);
        for (int k = 0; k < 8; ++k)
        {
            vec2 s = vec2((k & 1) != 0 ? p1.x : p0.x, (k & 2) != 0 ? p1.y : p0.y) / screen_res * 2.0 - 1.0;
            vec4 v = inv_proj * vec4(s, 2.0 * ((k & 4) != 0 ? mx : mn) - 1.0, 1.0);
            c[k] = v.xyz / v.w;
            bmin = min(bmin, c[k]);
            bmax = max(bmax, c[k]);
        }
        vec3 ctr = 0.125 * (c[0] + c[1] + c[2] + c[3] + c[4] + c[5] + c[6] + c[7]);
        vec3 pl[4];
        pl[0] = normalize(cross(c[0], c[1]));
        pl[1] = normalize(cross(c[1], c[3]));
        pl[2] = normalize(cross(c[3], c[2]));
        pl[3] = normalize(cross(c[2], c[0]));
        for (int j = 0; j < 4; ++j)
        {
            if (dot(pl[j], ctr) < 0.0)
            {
                pl[j] = -pl[j];
            }
        }
        uvec4 q0 = texelFetch(local_light_tiles, ivec3(t, 0), 0);
        uvec4 q1 = texelFetch(local_light_tiles, ivec3(t, 1), 0);
        uvec4 q2 = texelFetch(local_light_tiles, ivec3(t, 2), 0);
        int count = min(int(q0.x & 0xFFu), 32);
        uint cur = q0.x >> 8;
        int left = 3;
        for (int i = 0; i < count; ++i)
        {
            if (left == 0)
            {
                q0 = uvec4(q0.yzw, q1.x);
                q1 = uvec4(q1.yzw, q2.x);
                q2 = uvec4(q2.yzw, 0u);
                cur = q0.x;
                left = 4;
            }
            int idx = int(cur & 0xFFu);
            cur >>= 8;
            --left;
            vec4 a = LL_ROW(0, idx);
            vec4 kind = LL_ROW(2, idx);
            vec4 d3 = LL_ROW(3, idx);
            float r = (d3.w > 0.5 ? min(a.w * WEIGHT_RADIUS, WORN_REACH) : a.w * WEIGHT_RADIUS) * 1.001 + 1.0e-4;
            vec3 d = max(max(bmin - a.xyz, a.xyz - bmax), vec3(0.0));
            if (dot(d, d) > r * r)
            {
                continue;
            }
            bool culled = false;
            for (int j = 0; j < 4; ++j)
            {
                culled = culled || dot(pl[j], a.xyz) < -r;
            }
            if (culled)
            {
                continue;
            }
            if (kind.w > 0.5)
            {
                mat4 M = mat4(LL_ROW(4, idx), LL_ROW(5, idx), LL_ROW(6, idx), LL_ROW(7, idx));
                bool zb = true, xl = true, xh = true, yl = true, yh = true;
                for (int k = 0; k < 8; ++k)
                {
                    vec4 pt = M * vec4(c[k], 1.0);
                    const float e = 1.0e-5;
                    zb = zb && pt.z < -e;
                    xl = xl && pt.w > 0.0 && pt.x < -e * pt.w;
                    xh = xh && pt.w > 0.0 && pt.x > pt.w * (1.0 + e);
                    yl = yl && pt.w > 0.0 && pt.y < -e * pt.w;
                    yh = yh && pt.w > 0.0 && pt.y > pt.w * (1.0 + e);
                }
                if (zb || xl || xh || yl || yh)
                {
                    continue;
                }
            }
            int bp = n + 1;
            ow[bp >> 2] |= uint(idx) << (8 * (bp & 3));
            ++n;
        }
    }
    ow[0] |= uint(n);
    o0 = uvec4(ow[0], ow[1], ow[2], ow[3]);
    o1 = uvec4(ow[4], ow[5], ow[6], ow[7]);
    o2 = uvec4(ow[8], ow[9], ow[10], ow[11]);
}
