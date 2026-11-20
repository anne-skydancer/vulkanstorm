/**
 * @file dofCombine.slang (converted from dofCombineF.glsl)
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

import glsl;
import postfullscreen;

uniform sampler2D diffuseRect;
uniform sampler2D lightMap;
uniform vec2 screen_res;
uniform float max_cof;
uniform float res_scale;
uniform float dof_width;
uniform float dof_height;

[shader("vertex")]
PostVaryings vsMain(PostVertexIn v)
{
    return postFullscreenVertex(v);
}

[shader("fragment")]
vec4 fsMain(PostVaryings i) : SV_Target0
{
    vec2 tc = i.tc;
    vec2 dof_tc = min(tc * res_scale, vec2(dof_width, dof_height));
    vec4 dof = texture(diffuseRect, dof_tc);
    vec4 diff = texture(lightMap, tc);
    float a = min(abs(diff.a * 2.0 - 1.0) * max_cof * res_scale * res_scale, 1.0);

    // Sample four neighboring pixels through the transition.
    if (a > 0.25 && a < 0.75)
    {
        float sc = a / res_scale;
        vec4 col = texture(lightMap, tc + vec2(sc, sc) / screen_res);
        col += texture(lightMap, tc + vec2(-sc, sc) / screen_res);
        col += texture(lightMap, tc + vec2(sc, -sc) / screen_res);
        col += texture(lightMap, tc + vec2(-sc, -sc) / screen_res);
        diff = mix(diff, col * 0.25, a);
    }

    return mix(diff, dof, a);
}
