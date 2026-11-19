/**
 * @file glow.slang (converted from effects/glowV.glsl, effects/glowF.glsl)
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

uniform sampler2D diffuseMap;
uniform float glowStrength;
uniform vec2 glowDelta;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec4 vary_texcoord0 : VARY_TEXCOORD0;
    [[vk::location(1)]] vec4 vary_texcoord1 : VARY_TEXCOORD1;
    [[vk::location(2)]] vec4 vary_texcoord2 : VARY_TEXCOORD2;
    [[vk::location(3)]] vec4 vary_texcoord3 : VARY_TEXCOORD3;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.pos = vkPositionNdc(v.position);

    vec2 texcoord = v.position.xy * 0.5 + 0.5;
    o.vary_texcoord0 = vec4(texcoord + glowDelta * (-3.5), texcoord + glowDelta * (0.5));
    o.vary_texcoord1 = vec4(texcoord + glowDelta * (-2.5), texcoord + glowDelta * (1.5));
    o.vary_texcoord2 = vec4(texcoord + glowDelta * (-1.5), texcoord + glowDelta * (2.5));
    o.vary_texcoord3 = vec4(texcoord + glowDelta * (-0.5), texcoord + glowDelta * (3.5));
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    vec4 col = vec4(0.0, 0.0, 0.0, 0.0);

    float kern[8];
    kern[0] = 0.25; kern[1] = 0.5; kern[2] = 0.8; kern[3] = 1.0;
    kern[4] = 1.0;  kern[5] = 0.8; kern[6] = 0.5; kern[7] = 0.25;

    col += kern[0] * texture(diffuseMap, i.vary_texcoord0.xy);
    col += kern[1] * texture(diffuseMap, i.vary_texcoord1.xy);
    col += kern[2] * texture(diffuseMap, i.vary_texcoord2.xy);
    col += kern[3] * texture(diffuseMap, i.vary_texcoord3.xy);
    col += kern[4] * texture(diffuseMap, i.vary_texcoord0.zw);
    col += kern[5] * texture(diffuseMap, i.vary_texcoord1.zw);
    col += kern[6] * texture(diffuseMap, i.vary_texcoord2.zw);
    col += kern[7] * texture(diffuseMap, i.vary_texcoord3.zw);

    return max(vec4(col.rgb * glowStrength, col.a), vec4(0));
}
