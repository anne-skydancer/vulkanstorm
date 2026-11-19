/**
 * @file avatareyes.slang (converted from deferred/avatarEyesV.glsl, deferred/diffuseF.glsl)
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
import gbuffer;
import mirrorclip;

// HAS_EMISSIVE 1 writes the emissive target (RenderEnableEmissiveBuffer); the loader picks the program.
uniform mat3 normal_matrix;
uniform mat4 texture_matrix0;
uniform mat4 modelview_projection_matrix;
uniform mat4 modelview_matrix;
uniform sampler2D diffuseMap;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(1)]] vec3 normal : NORMAL;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
    [[vk::location(6)]] vec4 diffuse_color : COLOR;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec3 vary_normal : VARY_NORMAL;
    [[vk::location(1)]] vec4 vertex_color : VERTEX_COLOR;
    [[vk::location(2)]] vec2 vary_texcoord0 : VARY_TEXCOORD0;
    [[vk::location(3)]] vec3 vary_position : VARY_POSITION;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.vary_position = (modelview_matrix * vec4(v.position.xyz, 1.0)).xyz;
    o.pos = vkPositionClip(modelview_projection_matrix, v.position.xyz);
    o.vary_texcoord0 = (texture_matrix0 * vec4(v.texcoord0, 0, 1)).xy;
    o.vary_normal = normalize(normal_matrix * v.normal);
    o.vertex_color = v.diffuse_color;
    return o;
}

struct FragOut
{
    vec4 data0 : SV_Target0;
    vec4 data1 : SV_Target1;
    vec4 data2 : SV_Target2;
#if HAS_EMISSIVE
    vec4 data3 : SV_Target3;
#endif
};

[shader("fragment")]
FragOut fsMain(Varyings i)
{
    mirrorClip(i.vary_position);
    vec3 col = i.vertex_color.rgb * texture(diffuseMap, i.vary_texcoord0.xy).rgb;

    FragOut o;
    o.data0 = vec4(col, 0.0);
    o.data1 = i.vertex_color.aaaa; // spec
    vec3 nvn = normalize(i.vary_normal);
    o.data2 = encodeNormal(nvn.xyz, i.vertex_color.a, GBUFFER_FLAG_HAS_ATMOS);
#if HAS_EMISSIVE
    o.data3 = vec4(0, 0, 0, 0);
#endif
    return o;
}
