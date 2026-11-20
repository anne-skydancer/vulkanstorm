// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file fullbrightSurface.slang (converted from class1/deferred/fullbrightV/F.glsl and emissiveV/F.glsl)
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2005, Linden Research, Inc.
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
import mirrorclip;
import postcolor;

#ifndef HAS_SKIN
#define HAS_SKIN 0
#endif
#ifndef IS_HUD
#define IS_HUD 0
#endif
#ifndef MASK
#define MASK 0
#endif
#ifndef IS_EMISSIVE
#define IS_EMISSIVE 0
#endif
#ifndef IS_SHINY
#define IS_SHINY 0
#endif
#ifndef IS_ALPHA
#define IS_ALPHA 0
#endif

#if (IS_SHINY || IS_ALPHA) && !IS_HUD
#error These shiny and blended fullbright variants are for HUD rendering.
#endif

// The class3 fullbrightShinyF.glsl HUD path uses ordinary smooth interpolation.
#if IS_SHINY
#define FULLBRIGHT_INTERPOLATION
#else
#define FULLBRIGHT_INTERPOLATION centroid
#endif

uniform mat4 texture_matrix0;
#if HAS_SKIN || !IS_EMISSIVE
uniform mat4 modelview_matrix;
#endif
#if HAS_SKIN
uniform mat4 projection_matrix;
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];
#else
uniform mat4 modelview_projection_matrix;
#endif
#if MASK
uniform float minimum_alpha;
#endif
#if IS_ALPHA
uniform vec4 waterPlane;
uniform float waterSign;

// HUD alpha uses the near-plane clip from class1/deferred/deferredUtil.glsl.
void clipHudNearPlane(vec3 pos)
{
    if (waterSign > 0)
    {
        if ((dot(pos.xyz, waterPlane.xyz) + waterPlane.w) < 0.0)
        {
            discard;
        }
    }
    else
    {
        if ((dot(pos.xyz, waterPlane.xyz) + waterPlane.w) > 0.0)
        {
            discard;
        }
    }
}
#endif

// LLRenderPass::pushBatch binds the indexed diffuse textures directly to units 0–3.
uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;
uniform sampler2D tex3;

vec4 diffuseLookup(int index, vec2 tc)
{
    if (index == 0) { return texture(tex0, tc); }
    if (index == 1) { return texture(tex1, tc); }
    if (index == 2) { return texture(tex2, tc); }
    if (index == 3) { return texture(tex3, tc); }
    return vec4(1.0, 0.0, 1.0, 1.0);
}

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
#if IS_EMISSIVE
    [[vk::location(7)]] vec4 emissive : EMISSIVE;
#else
    [[vk::location(6)]] vec4 diffuse_color : COLOR;
#endif
#if HAS_SKIN
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
#endif
    [[vk::location(13)]] int texture_index : TEXINDEX;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] FULLBRIGHT_INTERPOLATION vec2 vary_texcoord0 : VARY_TEXCOORD0;
    [[vk::location(1)]] FULLBRIGHT_INTERPOLATION vec4 vertex_color : VERTEX_COLOR;
    [[vk::location(2)]] nointerpolation int tex_index : VARY_TEXTURE_INDEX;
#if !IS_EMISSIVE
    [[vk::location(3)]] FULLBRIGHT_INTERPOLATION vec3 vary_position : VARY_POSITION;
#endif
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
#if HAS_SKIN
    mat4 mat = modelview_matrix * vkSkinObject(matrixPalette, v.weight4);
    vec3 eye;
    o.pos = vkPositionRigged(projection_matrix, mat, v.position, eye);
#if !IS_EMISSIVE
    o.vary_position = eye;
#endif
#else
    o.pos = vkPositionClip(modelview_projection_matrix, v.position);
#if !IS_EMISSIVE
    o.vary_position = (modelview_matrix * vec4(v.position, 1.0)).xyz;
#endif
#endif

    o.vary_texcoord0 = (texture_matrix0 * vec4(v.texcoord0, 0.0, 1.0)).xy;
    o.tex_index = v.texture_index;
#if IS_EMISSIVE
    o.vertex_color = v.emissive;
#else
    o.vertex_color = v.diffuse_color;
#endif
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
#if !IS_EMISSIVE
    mirrorClip(i.vary_position);
#endif
#if IS_ALPHA
    clipHudNearPlane(i.vary_position);
#endif

    vec4 color = diffuseLookup(i.tex_index, i.vary_texcoord0);
#if !IS_SHINY
    float final_alpha = color.a * i.vertex_color.a;
#endif
#if IS_EMISSIVE
    // The stochastic glow pass also accumulates the same value in green.
    return max(vec4(0, final_alpha, 0, final_alpha), vec4(0));
#else
#if MASK
    if (color.a < minimum_alpha)
    {
        discard;
    }
#endif

    color.rgb *= i.vertex_color.rgb;
#if IS_SHINY
    color.a = 1.0;
#else
    color.a = final_alpha;
#endif
#if !IS_HUD
    color.rgb = srgb_to_linear(color.rgb);
#endif
    return max(color, vec4(0));
#endif
}
