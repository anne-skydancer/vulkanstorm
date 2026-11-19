/**
 * @file dbump.slang (converted from deferred/bumpV.glsl, deferred/bumpF.glsl)
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

// HAS_SKIN 1 is the rigged variant (the object skin palette and weight4). HAS_EMISSIVE 1 writes the emissive target
// (RenderEnableEmissiveBuffer); the loader picks the program.
uniform mat4 modelview_matrix;
uniform mat4 texture_matrix0;
#if HAS_SKIN
uniform mat4 projection_matrix;
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];
#else
uniform mat3 normal_matrix;
uniform mat4 modelview_projection_matrix;
#endif
uniform float minimum_alpha;
uniform sampler2D diffuseMap;
uniform sampler2D bumpMap;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(1)]] vec3 normal : NORMAL;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
    [[vk::location(6)]] vec4 diffuse_color : COLOR;
    [[vk::location(8)]] vec4 tangent : TANGENT;
#if HAS_SKIN
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
#endif
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec3 vary_mat0 : VARY_MAT0;
    [[vk::location(1)]] vec3 vary_mat1 : VARY_MAT1;
    [[vk::location(2)]] vec3 vary_mat2 : VARY_MAT2;
    [[vk::location(3)]] vec4 vertex_color : VERTEX_COLOR;
    [[vk::location(4)]] vec2 vary_texcoord0 : VARY_TEXCOORD0;
    [[vk::location(5)]] vec3 vary_position : VARY_POSITION;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
#if HAS_SKIN
    mat4 mat = modelview_matrix * vkSkinObject(matrixPalette, v.weight4);
    vec3 pos;
    o.pos = vkPositionRigged(projection_matrix, mat, v.position.xyz, pos);
    o.vary_position = pos;
    vec3 n = normalize((mat * vec4(v.normal.xyz + v.position.xyz, 1.0)).xyz - pos.xyz);
    vec3 t = normalize((mat * vec4(v.tangent.xyz + v.position.xyz, 1.0)).xyz - pos.xyz);
#else
    o.vary_position = (modelview_matrix * vec4(v.position.xyz, 1.0)).xyz;
    o.pos = vkPositionClip(modelview_projection_matrix, v.position.xyz);
    vec3 n = normalize(normal_matrix * v.normal);
    vec3 t = normalize(normal_matrix * v.tangent.xyz);
#endif
    vec3 b = cross(n, t) * v.tangent.w;

    o.vary_texcoord0 = (texture_matrix0 * vec4(v.texcoord0, 0, 1)).xy;

    o.vary_mat0 = vec3(t.x, b.x, n.x);
    o.vary_mat1 = vec3(t.y, b.y, n.y);
    o.vary_mat2 = vec3(t.z, b.z, n.z);

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

    vec4 col = texture(diffuseMap, i.vary_texcoord0.xy);
    if (col.a < minimum_alpha)
    {
        discard;
    }

    col *= i.vertex_color;

    vec3 norm = texture(bumpMap, i.vary_texcoord0.xy).rgb * 2.0 - 1.0;

    vec3 tnorm = vec3(dot(norm, i.vary_mat0),
                      dot(norm, i.vary_mat1),
                      dot(norm, i.vary_mat2));

    FragOut o;
    o.data0 = vec4(col.rgb, 0.0);
    o.data1 = i.vertex_color.aaaa; // spec

    vec3 nvn = normalize(tnorm);
    o.data2 = encodeNormal(nvn, i.vertex_color.a, GBUFFER_FLAG_HAS_ATMOS);
#if HAS_EMISSIVE
    o.data3 = vec4(0, 0, 0, 0);
#endif
    return o;
}
