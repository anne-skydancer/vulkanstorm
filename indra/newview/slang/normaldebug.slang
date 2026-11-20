/**
 * @file normaldebug.slang (converted from normaldebugV.glsl, normaldebugG.glsl, and normaldebugF.glsl)
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

uniform mat4 projection_matrix;
uniform mat4 modelview_matrix;
uniform float debug_normal_draw_length;
#if HAS_SKIN
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];
#endif

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(1)]] vec3 normal : NORMAL;
#if HAS_ATTRIBUTE_TANGENT
    [[vk::location(8)]] vec4 tangent : TANGENT;
#endif
#if HAS_SKIN
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
#endif
};

struct VertexOut
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec4 normal_clip : NORMAL_CLIP;
#if HAS_ATTRIBUTE_TANGENT
    [[vk::location(1)]] vec4 tangent_clip : TANGENT_CLIP;
#endif
};

struct LineVertex
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec4 color : LINE_COLOR;
};

vec4 directionEndpoint(vec3 point, vec4 eye, vec3 direction, mat4 view)
{
    vec4 endpoint = view * vec4(point + direction, 1.0);
    endpoint.xyz -= eye.xyz;
    endpoint.xyz = debug_normal_draw_length * normalize(endpoint.xyz);
    endpoint.xyz += eye.xyz;
    return vkPositionProject(projection_matrix, endpoint);
}

[shader("vertex")]
VertexOut vsMain(VertexIn v)
{
    mat4 view = modelview_matrix;
#if HAS_SKIN
    view = view * vkSkinObject(matrixPalette, v.weight4);
#endif
    vec4 eye = view * vec4(v.position, 1.0);

    VertexOut o;
    o.pos = vkPositionProject(projection_matrix, eye);
    o.normal_clip = directionEndpoint(v.position, eye, v.normal, view);
#if HAS_ATTRIBUTE_TANGENT
    o.tangent_clip = directionEndpoint(v.position, eye, v.tangent.xyz, view);
#endif
    return o;
}

void emitLine(inout LineStream<LineVertex> stream, vec4 start, vec4 end, vec4 color)
{
    LineVertex o;
    o.color = color;
    o.pos = vkPositionPassthrough(start);
    stream.Append(o);
    o.pos = vkPositionPassthrough(end);
    stream.Append(o);
    stream.RestartStrip();
}

[shader("geometry")]
#if HAS_ATTRIBUTE_TANGENT
[maxvertexcount(12)]
#else
[maxvertexcount(6)]
#endif
void gsMain(triangle VertexOut vertices[3], inout LineStream<LineVertex> stream)
{
    for (int i = 0; i < 3; ++i)
    {
        emitLine(stream, vertices[i].pos, vertices[i].normal_clip, vec4(1, 1, 0, 1));
#if HAS_ATTRIBUTE_TANGENT
        emitLine(stream, vertices[i].pos, vertices[i].tangent_clip, vec4(0, 1, 1, 1));
#endif
    }
}

[shader("fragment")]
vec4 fsMain(LineVertex i) : SV_Target0
{
    return max(i.color, vec4(0.0));
}
