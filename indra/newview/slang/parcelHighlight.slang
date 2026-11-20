/**
 * @file parcelHighlight.slang (converted from highlightF.glsl)
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
import gbuffer;

uniform mat4 modelview_projection_matrix;
uniform mat4 texture_matrix0;
uniform vec4 color;
uniform sampler2D diffuseMap; // renderOwnership binds the overlay texture at unit 0.

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
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
    o.pos = vkPositionClip(modelview_projection_matrix, v.position);
    o.tc = (texture_matrix0 * vec4(v.texcoord0, 0.0, 1.0)).xy;
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
    FragOut o;
    o.data0 = color * texture(diffuseMap, i.tc);
    o.data1 = vec4(0.0);
    o.data2 = vec4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);
#if HAS_EMISSIVE
    o.data3 = vec4(0.0);
#endif
    return o;
}
