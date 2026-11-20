// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file alphaCoverage.slang (converted from deferred alpha, fullbright, material and PBR shaders)
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
/**
 * @file class1\deferred\pbralphaV.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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
/**
 * @file materialV.glsl
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
import mirrorclip;

#ifndef PBR
#define PBR 0
#endif
#ifndef MATERIAL
#define MATERIAL 0
#endif
#ifndef MASK_TEXTURE
#define MASK_TEXTURE 0
#endif
#ifndef IS_IMPOSTOR
#define IS_IMPOSTOR 0
#endif

#if PBR
import pbrTexture;
#endif
#if !IS_IMPOSTOR
import alphaCoverageMask;
#endif

uniform mat4 modelview_matrix;
uniform mat4 texture_matrix0;
uniform vec4 waterPlane;
uniform float waterSign;
#if HAS_SKIN
uniform mat4 projection_matrix;
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];
#else
uniform mat4 modelview_projection_matrix;
#endif

#if PBR || MATERIAL
[[vk::binding(0, 0)]] uniform sampler2D diffuseMap;
#if PBR
uniform vec4 texture_base_color_transform[2];
#endif
#else
[[vk::binding(0, 0)]] uniform sampler2D tex0;
[[vk::binding(1, 0)]] uniform sampler2D tex1;
[[vk::binding(2, 0)]] uniform sampler2D tex2;
[[vk::binding(3, 0)]] uniform sampler2D tex3;
uniform float minimum_alpha;

vec4 diffuseLookup(int index, vec2 tc)
{
    if (index == 0) { return texture(tex0, tc); }
    if (index == 1) { return texture(tex1, tc); }
    if (index == 2) { return texture(tex2, tc); }
    if (index == 3) { return texture(tex3, tc); }
    return vec4(1.0, 0.0, 1.0, 1.0);
}
#endif
#if IS_IMPOSTOR
uniform int classic_mode;
#elif PBR || MATERIAL
[[vk::binding(1, 0)]] uniform usampler2D stoch_masks;
#else
[[vk::binding(4, 0)]] uniform usampler2D stoch_masks;
#endif

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
    [[vk::location(6)]] vec4 diffuse_color : COLOR;
#if HAS_SKIN
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
#endif
#if !PBR && !MATERIAL
    [[vk::location(13)]] int texture_index : TEXTURE_INDEX;
#endif
};

struct Varyings
{
    vec4 clip : SV_Position;
    [[vk::location(0)]] centroid vec3 position : VARY_POSITION;
    [[vk::location(1)]] centroid vec2 uv : TEXCOORD0;
    [[vk::location(2)]] centroid vec4 vertex_color : VERTEX_COLOR;
#if !PBR && !MATERIAL
    [[vk::location(3)]] nointerpolation int texture_index : VARY_TEXTURE_INDEX;
#endif
};

[shader("vertex")]
Varyings vsMain(VertexIn vertex)
{
    Varyings result;
#if HAS_SKIN
    mat4 modelview_skin = modelview_matrix * vkSkinObject(matrixPalette, vertex.weight4);
    vec3 eye;
    result.clip = vkPositionRigged(projection_matrix, modelview_skin, vertex.position, eye);
    result.position = eye;
#else
    result.clip = vkPositionClip(modelview_projection_matrix, vertex.position);
    result.position = (modelview_matrix * vec4(vertex.position, 1.0)).xyz;
#endif
#if PBR
    result.uv = texture_transform(vertex.texcoord0, texture_base_color_transform, texture_matrix0);
#else
    result.uv = (texture_matrix0 * vec4(vertex.texcoord0, 0.0, 1.0)).xy;
#if !MATERIAL
    result.texture_index = vertex.texture_index;
#endif
#endif
    result.vertex_color = vertex.diffuse_color;
    return result;
}

vec4 sampleAlpha(Varyings input)
{
    mirrorClip(input.position);
    float distance_to_water = dot(input.position, waterPlane.xyz) + waterPlane.w;
    if (waterSign > 0.0 ? distance_to_water < 0.0 : distance_to_water > 0.0)
    {
        discard;
    }
#if PBR || MATERIAL
    vec4 diffuse = texture(diffuseMap, input.uv);
#else
    vec4 diffuse = diffuseLookup(input.texture_index, input.uv);
#endif
    float alpha = diffuse.a * input.vertex_color.a;
#if !PBR && !MATERIAL
#if MASK_TEXTURE
    if (diffuse.a < minimum_alpha)
#else
    if (alpha < minimum_alpha)
#endif
    {
        discard;
    }
#endif
    return vec4(diffuse.rgb, alpha);
}

#if IS_IMPOSTOR
[shader("fragment")]
vec4 fsMain(Varyings input) : SV_Target0
{
    vec4 value = sampleAlpha(input);
    float scale = classic_mode > 0 ? 1.1 : 1.0;
    return max(vec4((value.rgb * input.vertex_color.rgb) * scale, value.a), vec4(0.0));
}
#else
struct CoverageOut
{
    vec4 color : SV_Target0;
    uint coverage : SV_Coverage;
};

[shader("fragment")]
CoverageOut fsMain(Varyings input, uint primitive_id : SV_PrimitiveID)
{
    float alpha = sampleAlpha(input).a;
    CoverageOut result;
    result.color = vec4(0.0, 0.0, 0.0, max(alpha, 0.0));
    result.coverage = coverageMask(stoch_masks, alpha, primitive_id, uvec2(input.clip.xy));
    return result;
}
#endif
