/**
 * @file bump.slang (converted from objects/bumpV.glsl, objects/bumpF.glsl)
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

// HAS_SKIN 1 is the rigged variant (the object skin palette and weight4)
uniform mat4 modelview_matrix;
uniform mat4 texture_matrix0;
#if HAS_SKIN
uniform mat4 projection_matrix;
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];
#else
uniform mat4 modelview_projection_matrix;
#endif
uniform sampler2D texture0;
uniform sampler2D texture1;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
    [[vk::location(3)]] vec2 texcoord1 : TEXCOORD1;
#if HAS_SKIN
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
#endif
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec2 vary_texcoord0 : VARY_TEXCOORD0;
    [[vk::location(1)]] vec2 vary_texcoord1 : VARY_TEXCOORD1;
    [[vk::location(2)]] vec3 vary_position : VARY_POSITION;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
#if HAS_SKIN
    mat4 mat = modelview_matrix * vkSkinObject(matrixPalette, v.weight4);
    vec3 eye;
    o.pos = vkPositionRigged(projection_matrix, mat, v.position.xyz, eye);
    o.vary_position = eye;
#else
    o.vary_position = (modelview_matrix * vec4(v.position.xyz, 1.0)).xyz;
    o.pos = vkPositionClip(modelview_projection_matrix, v.position.xyz);
#endif
    o.vary_texcoord0 = (texture_matrix0 * vec4(v.texcoord0, 0, 1)).xy;
    o.vary_texcoord1 = (texture_matrix0 * vec4(v.texcoord1, 0, 1)).xy;
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    mirrorClip(i.vary_position);

    float tex0 = texture(texture0, i.vary_texcoord0.xy).a;
    float tex1 = texture(texture1, i.vary_texcoord1.xy).a;

    return max(vec4(tex0 + (1.0 - tex1) - 0.5), vec4(0));
}
