/**
 * @file clip.slang (converted from clipV.glsl)
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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
import glsl;
import position;

uniform mat4 modelview_projection_matrix;
uniform mat4 modelview_matrix;
uniform vec4 color;
uniform vec4 clip_plane;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec3 vary_position : VARY_POSITION;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.vary_position = (modelview_matrix * vec4(v.position.xyz, 1.0)).xyz;
    o.pos = vkPositionClip(modelview_projection_matrix, v.position.xyz);
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    if (dot(i.vary_position, clip_plane.xyz) + clip_plane.w < 0.0)
    {
        discard;
    }
    return max(color, vec4(0));
}
