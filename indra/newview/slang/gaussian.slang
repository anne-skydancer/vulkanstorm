/**
 * @file gaussian.slang (converted from gaussianF.glsl)
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
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
import position;

uniform mat4 modelview_projection_matrix;
uniform sampler2D diffuseRect;
uniform float resScale;
// texture direction, will be <1, 0> or <0, 1>
uniform vec2 direction;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec2 vary_texcoord0 : VARY_TEXCOORD0;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.pos = vkPositionClip(modelview_projection_matrix, v.position.xyz);
    o.vary_texcoord0 = v.position.xy * 0.5 + 0.5;
    return o;
}

static const float w[9] = { 0.0002, 0.0060, 0.0606, 0.2417, 0.3829, 0.2417, 0.0606, 0.0060, 0.0002 };

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    vec3 col = vec3(0, 0, 0);
    for (int k = 0; k < 9; ++k)
    {
        vec2 tc = i.vary_texcoord0 + float(k - 4) * direction * resScale;
        col += texture(diffuseRect, tc).rgb * w[k];
    }
    return max(vec4(col, 0.0), vec4(0));
}
