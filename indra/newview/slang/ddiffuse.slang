/**
 * @file ddiffuse.slang (converted from diffuseV.glsl, diffuseNoColorV.glsl and deferred diffuse fragment shaders)
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
 * @file diffuseAlphaMaskIndexedF.glsl
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
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
 * @file diffuseAlphaMaskNoColorF.glsl
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2005, Linden Research, Inc.
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

#ifndef INDEXED
#define INDEXED 1
#endif
#ifndef HAS_SKIN
#define HAS_SKIN 0
#endif
#ifndef NO_COLOR
#define NO_COLOR 0
#endif
#ifndef MASK
#define MASK 0
#endif

uniform mat4 texture_matrix0;
#if HAS_SKIN
uniform mat4 projection_matrix;
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];
#else
uniform mat3 normal_matrix;
uniform mat4 modelview_projection_matrix;
#endif
#if !NO_COLOR
uniform mat4 modelview_matrix;
#endif

#if INDEXED
// LLRenderPass::pushBatch binds the four textures directly to units 0–3.
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
#else
uniform sampler2D diffuseMap;
#endif
#if !INDEXED || MASK
uniform float minimum_alpha;
#endif

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(1)]] vec3 normal : NORMAL;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
#if !NO_COLOR
    [[vk::location(6)]] vec4 diffuse_color : COLOR;
#endif
#if HAS_SKIN
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
#endif
#if INDEXED
    [[vk::location(13)]] int texture_index : TEXINDEX;
#endif
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec3 vary_normal : VARY_NORMAL;
    [[vk::location(1)]] vec2 tc : TC;
#if !NO_COLOR
    [[vk::location(2)]] vec4 vertex_color : VERTEX_COLOR;
    [[vk::location(3)]] vec3 vary_position : VARY_POSITION;
#endif
#if INDEXED
    [[vk::location(4)]] nointerpolation int tex_index : VARY_TEXTURE_INDEX;
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
    o.vary_position = eye;
    o.vary_normal = normalize((mat * vec4(v.normal + v.position, 1.0)).xyz - eye);
#else
    o.pos = vkPositionClip(modelview_projection_matrix, v.position);
    o.vary_normal = normalize(normal_matrix * v.normal);
#if !NO_COLOR
    o.vary_position = (modelview_matrix * vec4(v.position, 1.0)).xyz;
#endif
#endif
    o.tc = (texture_matrix0 * vec4(v.texcoord0, 0.0, 1.0)).xy;
#if !NO_COLOR
    o.vertex_color = v.diffuse_color;
#endif
#if INDEXED
    o.tex_index = v.texture_index;
#endif
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
#if !NO_COLOR
    mirrorClip(i.vary_position);
#endif
#if INDEXED
    vec4 sample_color = diffuseLookup(i.tex_index, i.tc);
#else
    vec4 sample_color = texture(diffuseMap, i.tc);
#endif
#if !NO_COLOR
    sample_color *= i.vertex_color;
#endif

    FragOut o;
#if !INDEXED || MASK
    if (sample_color.a < minimum_alpha)
    {
        discard;
    }
    o.data1 = vec4(0.0);
    float env = 0.0;
#else
    o.data1 = i.vertex_color.aaaa;
    float env = i.vertex_color.a;
#endif
    o.data0 = vec4(sample_color.rgb, 0.0);
    o.data2 = encodeNormal(normalize(i.vary_normal), env, GBUFFER_FLAG_HAS_ATMOS);
#if HAS_EMISSIVE
    o.data3 = vec4(0.0);
#endif
    return o;
}
