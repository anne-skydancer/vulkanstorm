// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Fixture program (GLSL-flavoured Slang): one uniform block shared by both stages, a sampler, two varyings,
// a settings hook and one permutation axis.
import glsl;
import hooks_mod;
import position_mod;

uniform mat4 modelview_projection_matrix;
uniform vec4 tint;
uniform sampler2D diffuseMap;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(2)]] vec2 texcoord0 : TEXCOORD0;
    [[vk::location(6)]] vec4 diffuse_color : COLOR0;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec2 vary_texcoord0 : VARY_TEXCOORD0;
    [[vk::location(1)]] vec4 vertex_color : VERTEX_COLOR;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.pos = vkPositionClip(modelview_projection_matrix, v.position);
    o.vary_texcoord0 = v.texcoord0;
    o.vertex_color = v.diffuse_color;
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    vec4 c = texture(diffuseMap, i.vary_texcoord0) * i.vertex_color * tint;
#if DOUBLE
    c *= 2.0;
#endif
    if (hookSSR() != 0)
        c.rgb = c.bgr;
    return c;
}
