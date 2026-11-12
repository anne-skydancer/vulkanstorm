/**
 * @file localLightBlockF.glsl
 * @brief The shadow pool's depth bounds of 4 x 4 pixels (unified lighting U2)
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

// The shadow pool's depth bounds (unified lighting U2): the nearest and the farthest raw depth of each 4 x 4 pixels of the
// depth buffer (sky, depth 1, left out), by textureGather. The tile cull (localLightCullF.glsl) reads them.
// Output: r the nearest, g the farthest (r > g where the block holds only sky).

uniform sampler2D depthMap;
uniform vec2 screen_res;

out vec4 frag_color;

void main()
{
    ivec2 b = ivec2(gl_FragCoord.xy) * 4;
    vec2 inv = 1.0 / screen_res;
    float mn = 1.0;
    float mx = 0.0;
    for (int y = 0; y < 4; y += 2)
    {
        for (int x = 0; x < 4; x += 2)
        {
            // texels past the screen repeat an edge texel, which lies in the block's tile anyway
            vec4 g = textureGather(depthMap, (vec2(b + ivec2(x, y)) + 1.0) * inv);
            for (int k = 0; k < 4; ++k)
            {
                if (g[k] < 1.0)
                {
                    mn = min(mn, g[k]);
                    mx = max(mx, g[k]);
                }
            }
        }
    }
    frag_color = vec4(mn, mx, 0.0, 0.0);
}
