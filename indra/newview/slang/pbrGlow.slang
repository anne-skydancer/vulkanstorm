// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pbrGlow.slang (converted from class1/deferred/pbrglowV.glsl and pbrglowF.glsl)
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
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
import postcolor;

#if HAS_SKIN
uniform mat4 modelview_matrix;
uniform mat4 projection_matrix;
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];
#else
uniform mat4 modelview_projection_matrix;
#endif
uniform mat4 texture_matrix0;
uniform vec4 texture_base_color_transform[2];
uniform vec4 texture_emissive_transform[2];

uniform sampler2D diffuseMap;
uniform sampler2D emissiveMap;
uniform vec3 emissiveColor;
uniform float minimum_alpha;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
    [[vk::location(7)]] vec4 emissive : EMISSIVE;
#if HAS_SKIN
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
#endif
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] centroid vec2 base_color_texcoord : BASE_COLOR_TEXCOORD;
    [[vk::location(1)]] centroid vec2 emissive_texcoord : EMISSIVE_TEXCOORD;
    [[vk::location(2)]] centroid vec4 vertex_emissive : VERTEX_EMISSIVE;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
#if HAS_SKIN
    mat4 mat = modelview_matrix * vkSkinObject(matrixPalette, v.weight4);
    vec3 pos;
    o.pos = vkPositionRigged(projection_matrix, mat, v.position, pos);
#else
    o.pos = vkPositionClip(modelview_projection_matrix, v.position);
#endif
    o.base_color_texcoord = texture_transform(v.texcoord0, texture_base_color_transform, texture_matrix0);
    o.emissive_texcoord = texture_transform(v.texcoord0, texture_emissive_transform, texture_matrix0);
    o.vertex_emissive = v.emissive;
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    vec4 basecolor = texture(diffuseMap, i.base_color_texcoord);
    if (basecolor.a < minimum_alpha)
    {
        discard;
    }

    vec3 emissive = emissiveColor;
    emissive *= srgb_to_linear(texture(emissiveMap, i.emissive_texcoord).rgb);
    float lum = max(max(emissive.r, emissive.g), emissive.b);
    lum *= i.vertex_emissive.a;

    // The stochastic glow pass also stores the glow in green.
    return vec4(0, max(lum, 0.0), 0, lum);
}
