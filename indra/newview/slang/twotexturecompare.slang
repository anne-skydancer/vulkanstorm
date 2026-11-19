/**
 * @file twotexturecompare.slang (converted from twotexturecompareV.glsl)
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

uniform mat4 modelview_projection_matrix;
uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D dither_tex;
uniform float dither_scale;
uniform float dither_scale_s;
uniform float dither_scale_t;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
    [[vk::location(3)]] vec2 texcoord1 : TEXCOORD1;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec2 vary_texcoord0 : VARY_TEXCOORD0;
    [[vk::location(1)]] vec2 vary_texcoord1 : VARY_TEXCOORD1;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.pos = vkPositionClip(modelview_projection_matrix, v.position.xyz);
    o.vary_texcoord0 = v.texcoord0;
    o.vary_texcoord1 = v.texcoord1;
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    vec4 frag_color = abs(texture(tex0, i.vary_texcoord0.xy) - texture(tex1, i.vary_texcoord0.xy));

    vec2 dither_coord;
    dither_coord[0] = i.vary_texcoord0[0] * dither_scale_s;
    dither_coord[1] = i.vary_texcoord0[1] * dither_scale_t;

    vec4 dither_vec = texture(dither_tex, dither_coord.xy);

    for (int k = 0; k < 3; k++)
    {
        if (frag_color[k] < dither_vec[k] * dither_scale)
        {
            frag_color[k] = 0.f;
        }
    }
    return frag_color;
}
