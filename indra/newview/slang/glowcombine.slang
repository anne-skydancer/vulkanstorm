/**
 * @file glowcombine.slang (converted from glowcombineV.glsl)
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

uniform sampler2D diffuseRect;
uniform sampler2D emissiveRect;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec2 tc : TC;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.pos = vkPositionNdc(v.position.xyz);
    o.tc = v.position.xy * 0.5 + 0.5;
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    return texture(diffuseRect, i.tc) + texture(emissiveRect, i.tc);
}
