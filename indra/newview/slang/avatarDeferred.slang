// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file avatarDeferred.slang (converted from class1/deferred/avatarV/F.glsl and class1/avatar/avatarSkinV.glsl)
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

uniform mat4 projection_matrix;
uniform vec4 matrixPalette[45];

// Legacy AVATAR_CLOTH is always defined to 0 or 1 and tested with #ifdef.
// Keep the cloth path in both settings. The draw pool uploads these only with cloth enabled.
uniform vec4 gWindDir;
uniform vec4 gSinWaveParams;
uniform vec4 gGravity;

static const vec4 gMinMaxConstants = vec4(1.0, 0.166666, 0.0083143, 0.00018542);
static const vec4 gPiConstants = vec4(0.159154943, 6.28318530, 3.141592653, 1.5707963);

uniform sampler2D diffuseMap;
uniform float minimum_alpha;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(1)]] vec3 normal : NORMAL;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
    [[vk::location(9)]] float weight : WEIGHT;
    [[vk::location(11)]] vec4 clothing : CLOTHING;
};

struct Varyings
{
    vec4 clip : SV_Position;
    [[vk::location(0)]] vec3 vary_normal : VARY_NORMAL;
    [[vk::location(1)]] vec2 vary_texcoord0 : VARY_TEXCOORD0;
    [[vk::location(2)]] vec3 vary_position : VARY_POSITION;
};

mat4 getSkinnedTransform(float weight)
{
    int i = int(floor(weight));
    i = min(i, 14);
    int next = min(i + 1, 14);
    float x = fract(weight);

    // The native avatar palette is axis-major in eye space, with 15 joint rows per axis.
    mat4 ret;
    ret[0] = mix(matrixPalette[i + 0], matrixPalette[next + 0], x);
    ret[1] = mix(matrixPalette[i + 15], matrixPalette[next + 15], x);
    ret[2] = mix(matrixPalette[i + 30], matrixPalette[next + 30], x);
    ret[3] = vec4(0, 0, 0, 1);
    return ret;
}

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    vec4 pos;
    vec3 norm;
    vec4 pos_in = vec4(v.position.xyz, 1.0);
    mat4 trans = getSkinnedTransform(v.weight);

    norm.x = dot(trans[0].xyz, v.normal);
    norm.y = dot(trans[1].xyz, v.normal);
    norm.z = dot(trans[2].xyz, v.normal);
    norm = normalize(norm);

    vec4 windEffect;
    windEffect = vec4(dot(norm, gWindDir.xyz));
    pos.x = dot(trans[2], pos_in);
    windEffect.xyz = pos.x * vec3(0.015, 0.015, 0.015) + windEffect.xyz;
    windEffect.w = windEffect.w * 2.0 + 1.0;
    windEffect.w = windEffect.w * gWindDir.w;
    windEffect.xyz = windEffect.xyz * gSinWaveParams.xyz + vec3(gSinWaveParams.w);

    // Reduce the two primary oscillations to the original period.
    vec4 temp1, temp0, temp2, offsetPos;
    temp1.xyz = windEffect.xyz * gPiConstants.x;
    temp0.y = mod(temp1.x, 1.0);
    windEffect.x = temp0.y * gPiConstants.y;
    temp1.z = temp1.z - gPiConstants.w;
    temp0.y = mod(temp1.z, 1.0);
    windEffect.z = temp0.y * gPiConstants.y;
    windEffect.xyz = windEffect.xyz + vec3(-3.141592);

    // Minimax sine approximation, with the same evaluation order as the GLSL.
    vec4 sinWave;
    temp1 = windEffect * windEffect;
    sinWave = -temp1 * gMinMaxConstants.w + vec4(gMinMaxConstants.z);
    sinWave = sinWave * -temp1 + vec4(gMinMaxConstants.y);
    sinWave = sinWave * -temp1 + vec4(gMinMaxConstants.x);
    sinWave = sinWave * windEffect;
    sinWave.xyz = sinWave.xyz * gWindDir.w + vec3(windEffect.w);

    temp1 = vec4(dot(norm, gGravity.xyz));
    temp1 = min(temp1, vec4(0.2, 0.0, 0.0, 0.0));
    temp1 = temp1 * vec4(1.5, 0.0, 0.0, 0.0);
    sinWave.x = sinWave.x + temp1.x;
    sinWave.xyz = sinWave.xyz * v.clothing.w;
    sinWave.xyz = max(sinWave.xyz, vec3(-1.0, -1.0, -1.0));
    offsetPos = v.clothing * sinWave.x;
    temp2 = gWindDir * sinWave.z + vec4(norm, 0);
    offsetPos = vec4(1.0, 1.0, 1.0, 0.0) * offsetPos + pos_in;
    norm += temp2.xyz * 2.0;
    norm = normalize(norm);

    pos.x = dot(trans[0], offsetPos);
    pos.y = dot(trans[1], offsetPos);
    pos.z = dot(trans[2], offsetPos);
    pos.w = 1.0;

    Varyings o;
    o.clip = vkPositionProject(projection_matrix, pos);
    o.vary_normal = norm;
    o.vary_texcoord0 = v.texcoord0;
    o.vary_position = pos.xyz;
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

[shader("fragment")]
GBufferOut fsMain(Varyings i)
{
    mirrorClip(i.vary_position);
    vec4 diff = texture(diffuseMap, i.vary_texcoord0);
    if (diff.a < minimum_alpha)
    {
        discard;
    }

    GBufferOut o;
    o.data0 = vec4(diff.rgb, 0.0);
    o.data1 = vec4(0, 0, 0, 0);
    vec3 nvn = normalize(i.vary_normal);
    o.data2 = encodeNormal(nvn.xyz, 0, GBUFFER_FLAG_HAS_ATMOS);
#if HAS_EMISSIVE
    o.data3 = vec4(0);
#endif
    return o;
}
