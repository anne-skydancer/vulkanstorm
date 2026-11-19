/**
 * @file occlusionSkinned.slang (converted from interface/occlusionSkinnedV.glsl, interface/occlusionF.glsl)
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

// The occlusion program for rigged meshes: occlusion.slang's fragment stage under the skinned position
uniform mat4 modelview_matrix;
uniform mat4 projection_matrix;
uniform mat3x4 matrixPalette[VK_MAX_JOINTS];

// the G-buffer flag of a PBR material (the value of GBUFFER_FLAG_HAS_PBR in the hand-written helper code)
static const float GBUFFER_FLAG_HAS_PBR = 0.67;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(10)]] vec4 weight4 : WEIGHT4;
};

struct Varyings
{
    vec4 pos : SV_Position;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    mat4 mat = modelview_matrix * vkSkinObject(matrixPalette, v.weight4);
    vec3 eye;
    o.pos = vkPositionRigged(projection_matrix, mat, v.position.xyz, eye);
    return o;
}

struct FragOut
{
    vec4 data0 : SV_Target0;
    vec4 data1 : SV_Target1;
    vec4 data2 : SV_Target2;
};

[shader("fragment")]
FragOut fsMain(Varyings i)
{
    FragOut o;
    // emissive red PBR material for debugging
    o.data0 = vec4(0, 0, 0, 0);
    o.data1 = vec4(0, 0, 0, 0);
    o.data2 = vec4(1, 0, 0, GBUFFER_FLAG_HAS_PBR);
    return o;
}
