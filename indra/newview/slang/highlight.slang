/**
 * @file highlight.slang (converted from highlightV.glsl)
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
import position;

// UVSET picks the texture coordinate set: 1 normal map, 2 specular map
// (highlightNormV and highlightSpecV, over the one highlightF; highlightV has a skinned variant and stays GLSL)
uniform mat4 texture_matrix0;
uniform mat4 modelview_projection_matrix;
uniform vec4 color;
uniform sampler2D diffuseMap;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
#if UVSET == 1
    [[vk::location(3)]] vec2 texcoord1 : TEXCOORD1;
#endif
#if UVSET == 2
    [[vk::location(4)]] vec2 texcoord2 : TEXCOORD2;
#endif
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
#if UVSET == 1
    vec2 uv = v.texcoord1;
#else
    vec2 uv = v.texcoord2;
#endif
    o.vary_texcoord0 = (texture_matrix0 * vec4(uv, 0, 1)).xy;
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    return max(color * texture(diffuseMap, i.vary_texcoord0.xy), vec4(0));
}
