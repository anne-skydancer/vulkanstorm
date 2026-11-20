// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
import glsl;
import position_mod;

uniform mat4 modelview_projection_matrix;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(1)]] vec3 normal : NORMAL;
};

struct VertexOut
{
    vec4 pos : SV_Position;
    [[vk::location(2)]] vec4 endpoint : LINE_END;
    [[vk::location(5)]] vec2 detail[2] : DETAIL;
};

struct GeometryOut
{
    vec4 pos : SV_Position;
    [[vk::location(7)]] vec4 color : LINE_COLOR;
};

[shader("vertex")]
VertexOut vsMain(VertexIn v)
{
    VertexOut o;
    o.pos = vkPositionClip(modelview_projection_matrix, v.position);
    o.endpoint = vkPositionClip(modelview_projection_matrix, v.position + v.normal);
    o.detail[0] = v.position.xy;
    o.detail[1] = v.normal.xy;
    return o;
}

[shader("geometry")]
[maxvertexcount(2)]
void gsMain(triangle VertexOut v[3], inout LineStream<GeometryOut> stream)
{
    GeometryOut o;
    o.pos = vkPositionPassthrough(v[0].pos);
    o.color = vec4(v[0].detail[0], v[0].detail[1]);
    stream.Append(o);
    o.pos = vkPositionPassthrough(v[0].endpoint);
    stream.Append(o);
    stream.RestartStrip();
}

[shader("fragment")]
vec4 fsMain(GeometryOut i) : SV_Target0
{
    return i.color;
}
