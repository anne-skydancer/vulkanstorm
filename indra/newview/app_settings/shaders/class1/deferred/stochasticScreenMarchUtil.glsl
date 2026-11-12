/**
 * @file stochasticScreenMarchUtil.glsl
 * @brief The stochastic core's screen-space march toward an emitter (unified lighting U2)
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

// The core's second march (next to stMarchRay's, which walks a light's depth map): one ray from a receiver toward a point of
// an emitter, walked through the camera's own depth buffer. It has no knowledge of its consumer; the shadow pool's local
// light ratio pass (localLightRatioF.glsl) calls it. A ray is blocked where the depth buffer's surface lies in front of
// the ray at a step and within a thickness behind it (a surface the ray passes behind, not a wall far in front of the
// receiver's own side). It sees what the depth buffer holds: an occluder off screen, a back side or a thing thinner than the
// steps' spacing can be missed (the hint tier of a later stage covers some of that).
//
// Uses the program's depthMap and screen_res (deferredUtil.glsl) and `st_march_proj`, the camera's projection.

uniform sampler2D depthMap;
uniform vec2 screen_res;
uniform mat4 st_march_proj;

#define ST_SCREEN_THICK_MIN 0.5    // metres: the thickness a surface in the depth buffer is taken to have, at least

// view-space distance in front of the camera of a depth buffer value (the clip w of the standard projection)
float stLinearDepth(float z)
{
    return st_march_proj[3][2] / (z * 2.0 - 1.0 + st_march_proj[2][2]);
}

// One ray from the receiver p (view space, already off its surface) to the emitter point e: 1 when blocked, 0 when free.
// c0: the receiver in clip space. The ray ends xr short of e (the light's exclusion zone: its own prim) and no farther than
// `reach` from the receiver, and in front of the near plane. The steps (at least 2, at most `steps_max`) grow with the ray's
// length on screen, clamp(ceil(px / 32)), are spaced densely near the receiver, and start at a jitter in [0, 1) that the
// ray index k rotates.
float stMarchRayScreen(vec3 p, vec4 c0, vec3 e, float xr, float reach, float jitter, int k, int steps_max)
{
    vec3 dir = e - p;
    float len = length(dir);
    dir /= len;
    float tmax = min(len - xr, reach);
    if (dir.z > 0.0)
    {
        tmax = min(tmax, (-0.26 - p.z) / dir.z);
    }
    if (tmax <= 0.0)
    {
        return 0.0;
    }
    vec4 c1 = st_march_proj * vec4(p + dir * tmax, 1.0);
    float lpx = length((c1.xy / c1.w - c0.xy / c0.w) * 0.5 * screen_res);
    int steps = clamp(int(ceil(lpx / 32.0)), 2, steps_max);
    float j = fract(jitter + 0.618034 * float(k));
    float thick = max(ST_SCREEN_THICK_MIN, 2.0 * abs(c1.w - c0.w) / float(steps));
    for (int i = 0; i < steps; ++i)
    {
        float u = (float(i) + j) / float(steps);
        u *= u;
        vec4 c = mix(c0, c1, u);
        vec2 uv = c.xy / c.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0))))
        {
            break;
        }
        float ls = stLinearDepth(texelFetch(depthMap, ivec2(uv * screen_res), 0).r);
        if (c.w > ls + 0.002 * c.w && c.w - ls < thick)
        {
            return 1.0;
        }
    }
    return 0.0;
}
