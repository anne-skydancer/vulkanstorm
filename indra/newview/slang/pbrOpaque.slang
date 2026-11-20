// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pbrOpaque.slang (converted from class1/deferred/pbropaqueV.glsl and pbropaqueF.glsl)
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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
import pbrTexture;
import gbuffer;
import mirrorclip;
import postcolor;

#ifndef IS_HUD
#define IS_HUD 0
#endif
#ifndef HAS_EMISSIVE
#define HAS_EMISSIVE 0
#endif
#ifndef HAS_SKIN
#define HAS_SKIN 0
#endif

#if !IS_HUD
uniform mat4 modelview_matrix;
#endif
#if HAS_SKIN
uniform mat4 projection_matrix;
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];
#else
uniform mat4 modelview_projection_matrix;
#if !IS_HUD
uniform mat3 normal_matrix;
#endif
#endif

uniform mat4 texture_matrix0;
uniform vec4 texture_base_color_transform[2];
#if !IS_HUD
uniform vec4 texture_normal_transform[2];
uniform vec4 texture_metallic_roughness_transform[2];
#endif
#if IS_HUD || HAS_EMISSIVE
uniform vec4 texture_emissive_transform[2];
#endif

uniform sampler2D diffuseMap;
uniform float minimum_alpha;
#if !IS_HUD
uniform sampler2D bumpMap;
uniform sampler2D specularMap;
uniform float metallicFactor;
uniform float roughnessFactor;
#endif
#if IS_HUD || HAS_EMISSIVE
uniform sampler2D emissiveMap;
uniform vec3 emissiveColor;
#endif

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
#if !IS_HUD
    [[vk::location(1)]] vec3 normal : NORMAL;
#endif
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
    [[vk::location(6)]] vec4 diffuse_color : COLOR;
#if !IS_HUD
    [[vk::location(8)]] vec4 tangent : TANGENT;
#endif
#if HAS_SKIN
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
#endif
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec2 base_color_texcoord : BASE_COLOR_TEXCOORD;
    [[vk::location(1)]] vec4 vertex_color : VERTEX_COLOR;
#if !IS_HUD
    [[vk::location(2)]] vec2 normal_texcoord : NORMAL_TEXCOORD;
    [[vk::location(3)]] vec2 metallic_roughness_texcoord : METALLIC_ROUGHNESS_TEXCOORD;
    [[vk::location(4)]] vec3 vary_position : VARY_POSITION;
    [[vk::location(5)]] vec3 vary_normal : VARY_NORMAL;
    [[vk::location(6)]] vec3 vary_tangent : VARY_TANGENT;
    [[vk::location(7)]] nointerpolation float vary_sign : VARY_SIGN;
#endif
#if IS_HUD || HAS_EMISSIVE
    [[vk::location(8)]] vec2 emissive_texcoord : EMISSIVE_TEXCOORD;
#endif
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
    vec3 n = (mat * vec4(v.normal + v.position, 1.0)).xyz - pos;
    vec3 t = (mat * vec4(v.tangent.xyz + v.position, 1.0)).xyz - pos;
#else
    o.pos = vkPositionClip(modelview_projection_matrix, v.position);
#if !IS_HUD
    o.vary_position = (modelview_matrix * vec4(v.position, 1.0)).xyz;
    vec3 n = normal_matrix * v.normal;
    vec3 t = normal_matrix * v.tangent.xyz;
#endif
#endif

    o.base_color_texcoord = texture_transform(v.texcoord0, texture_base_color_transform, texture_matrix0);
    o.vertex_color = v.diffuse_color;
#if !IS_HUD
    o.normal_texcoord = texture_transform(v.texcoord0, texture_normal_transform, texture_matrix0);
    o.metallic_roughness_texcoord = texture_transform(v.texcoord0, texture_metallic_roughness_transform, texture_matrix0);

    n = normalize(n);
    vec4 transformed_tangent = tangent_space_transform(vec4(t, v.tangent.w), n, texture_normal_transform, texture_matrix0);
    o.vary_tangent = normalize(transformed_tangent.xyz);
    o.vary_sign = transformed_tangent.w;
    o.vary_normal = n;
#endif
#if IS_HUD || HAS_EMISSIVE
    o.emissive_texcoord = texture_transform(v.texcoord0, texture_emissive_transform, texture_matrix0);
#endif
    return o;
}

#if IS_HUD
[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    vec4 basecolor = texture(diffuseMap, i.base_color_texcoord);
    basecolor.a *= i.vertex_color.a;
    if (basecolor.a < minimum_alpha)
    {
        discard;
    }

    vec3 col = i.vertex_color.rgb * srgb_to_linear(basecolor.rgb);
    vec3 emissive = emissiveColor;
    emissive *= srgb_to_linear(texture(emissiveMap, i.emissive_texcoord).rgb);
    col += emissive;

    // HUDs are drawn after gamma correction.
    return vec4(linear_to_srgb(col), 0.0);
}
#else
struct GBufferOut
{
    vec4 data0 : SV_Target0;
    vec4 data1 : SV_Target1;
    vec4 data2 : SV_Target2;
#if HAS_EMISSIVE
    vec4 data3 : SV_Target3;
#endif
};

[shader("fragment")]
GBufferOut fsMain(Varyings i, bool is_front_face : SV_IsFrontFace)
{
    mirrorClip(i.vary_position);

    vec4 basecolor = texture(diffuseMap, i.base_color_texcoord);
    basecolor.rgb = srgb_to_linear(basecolor.rgb);
    basecolor *= i.vertex_color;
    if (basecolor.a < minimum_alpha)
    {
        discard;
    }

    vec3 vNt = texture(bumpMap, i.normal_texcoord).xyz * 2.0 - 1.0;
    vec3 vN = i.vary_normal;
    vec3 vT = i.vary_tangent;
    vec3 vB = i.vary_sign * cross(vN, vT);
    vec3 tnorm = normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);

    // The specular map packs linear occlusion, roughness, and metalness.
    vec3 spec = texture(specularMap, i.metallic_roughness_texcoord).rgb;
    spec.g *= roughnessFactor;
    spec.b *= metallicFactor;
    tnorm *= is_front_face ? 1.0 : -1.0;

    GBufferOut o;
    o.data0 = max(vec4(basecolor.rgb, 0.0), vec4(0));
    o.data1 = max(vec4(spec, 0.0), vec4(0));
    o.data2 = encodeNormal(tnorm, 0, GBUFFER_FLAG_HAS_PBR);
#if HAS_EMISSIVE
    vec3 emissive = emissiveColor;
    emissive *= srgb_to_linear(texture(emissiveMap, i.emissive_texcoord).rgb);
    o.data3 = max(vec4(emissive, 0), vec4(0));
#endif
    return o;
}
#endif
