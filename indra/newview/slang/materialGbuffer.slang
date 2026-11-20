// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file materialGbuffer.slang (converted from class1/deferred/materialV.glsl and class3/deferred/materialF.glsl)
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, Inc.,
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
 * $/LicenseInfo$
 */
import glsl;
import position;
import gbuffer;
import mirrorclip;

#define MATERIAL_ALPHA_NONE 0
#define MATERIAL_ALPHA_MASK 2
#define MATERIAL_ALPHA_EMISSIVE 3

#if MODE != MATERIAL_ALPHA_NONE && MODE != MATERIAL_ALPHA_MASK && MODE != MATERIAL_ALPHA_EMISSIVE
#error The deferred material G-buffer only handles modes 0, 2 and 3.
#endif

uniform mat4 modelview_matrix;
uniform mat4 texture_matrix0;
#if HAS_SKIN
uniform mat4 projection_matrix;
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];
#else
uniform mat4 modelview_projection_matrix;
uniform mat3 normal_matrix;
#endif

uniform sampler2D diffuseMap;
#if HAS_NORMAL_MAP
uniform sampler2D bumpMap;
#endif
#if HAS_SPECULAR_MAP
uniform sampler2D specularMap;
#endif
uniform float emissive_brightness;
uniform float env_intensity;
uniform vec4 specular_color;
#if MODE == MATERIAL_ALPHA_MASK
uniform float minimum_alpha;
#endif

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(1)]] vec3 normal : NORMAL;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
#if HAS_NORMAL_MAP
    [[vk::location(3)]] vec2 texcoord1 : TEXCOORD1;
#endif
#if HAS_SPECULAR_MAP
    [[vk::location(4)]] vec2 texcoord2 : TEXCOORD2;
#endif
    [[vk::location(6)]] vec4 diffuse_color : COLOR;
#if HAS_NORMAL_MAP
    [[vk::location(8)]] vec4 tangent : TANGENT;
#endif
#if HAS_SKIN
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
#endif
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] centroid vec3 vary_position : VARY_POSITION;
    [[vk::location(1)]] centroid vec3 vary_normal : VARY_NORMAL;
#if HAS_NORMAL_MAP
    [[vk::location(2)]] centroid vec3 vary_tangent : VARY_TANGENT;
    [[vk::location(3)]] nointerpolation float vary_sign : VARY_SIGN;
    [[vk::location(4)]] centroid vec2 vary_texcoord1 : VARY_TEXCOORD1;
#endif
#if HAS_SPECULAR_MAP
    [[vk::location(5)]] centroid vec2 vary_texcoord2 : VARY_TEXCOORD2;
#endif
    [[vk::location(6)]] centroid vec4 vertex_color : VERTEX_COLOR;
    [[vk::location(7)]] centroid vec2 vary_texcoord0 : VARY_TEXCOORD0;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
#if HAS_SKIN
    mat4 mat = modelview_matrix * vkSkinObject(matrixPalette, v.weight4);
    vec3 pos;
    o.pos = vkPositionRigged(projection_matrix, mat, v.position, pos);
    o.vary_position = pos;
    o.vary_normal = normalize((mat * vec4(v.normal + v.position, 1.0)).xyz - pos);
#if HAS_NORMAL_MAP
    o.vary_tangent = normalize((mat * vec4(v.tangent.xyz + v.position, 1.0)).xyz - pos);
#endif
#else
    o.pos = vkPositionClip(modelview_projection_matrix, v.position);
    o.vary_position = (modelview_matrix * vec4(v.position, 1.0)).xyz;
    o.vary_normal = normalize(normal_matrix * v.normal);
#if HAS_NORMAL_MAP
    o.vary_tangent = normalize(normal_matrix * v.tangent.xyz);
#endif
#endif

    o.vertex_color = v.diffuse_color;
    o.vary_texcoord0 = (texture_matrix0 * vec4(v.texcoord0, 0, 1)).xy;
#if HAS_NORMAL_MAP
    o.vary_sign = v.tangent.w;
    o.vary_texcoord1 = (texture_matrix0 * vec4(v.texcoord1, 0, 1)).xy;
#endif
#if HAS_SPECULAR_MAP
    o.vary_texcoord2 = (texture_matrix0 * vec4(v.texcoord2, 0, 1)).xy;
#endif
    return o;
}

struct GBufferOut
{
    vec4 data0 : SV_Target0;
    vec4 data1 : SV_Target1;
    vec4 data2 : SV_Target2;
#if HAS_EMISSIVE
    vec4 data3 : SV_Target3;
#endif
};

vec3 getNormal(Varyings i, inout float glossiness)
{
#if HAS_NORMAL_MAP
    vec4 vNt = texture(bumpMap, i.vary_texcoord1);
    glossiness *= vNt.a;
    vNt.xyz = vNt.xyz * 2 - 1;
    vec3 vN = i.vary_normal;
    vec3 vT = i.vary_tangent;
    vec3 vB = i.vary_sign * cross(vN, vT);
    return normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);
#else
    return normalize(i.vary_normal);
#endif
}

vec4 getSpecular(Varyings i)
{
#if HAS_SPECULAR_MAP
    vec4 spec = texture(specularMap, i.vary_texcoord2);
    spec.rgb *= specular_color.rgb;
    return spec;
#else
    return vec4(specular_color.rgb, 1.0);
#endif
}

[shader("fragment")]
GBufferOut fsMain(Varyings i)
{
    mirrorClip(i.vary_position);

    vec4 diffcol = texture(diffuseMap, i.vary_texcoord0);
    diffcol.rgb *= i.vertex_color.rgb;
#if MODE == MATERIAL_ALPHA_MASK
    // Bias the cutoff by half an 8-bit quantization step to avoid acne at the transition.
    float bias = 0.001953125;
    if (diffcol.a < minimum_alpha - bias)
    {
        discard;
    }
#endif

    vec4 spec = getSpecular(i);
    float env = env_intensity * spec.a;
    float glossiness = specular_color.a;
    vec3 norm = getNormal(i, glossiness);

    float emissive = emissive_brightness;
#if MODE == MATERIAL_ALPHA_EMISSIVE
    emissive = max(diffcol.a, emissive_brightness);
#endif

    GBufferOut o;
    o.data0 = max(vec4(diffcol.rgb, emissive), vec4(0));
    o.data1 = max(vec4(spec.rgb, glossiness), vec4(0));
    o.data2 = encodeNormal(norm, env, GBUFFER_FLAG_HAS_ATMOS);
#if HAS_EMISSIVE
    o.data3 = vec4(0, 0, 0, 0);
#endif
    return o;
}
