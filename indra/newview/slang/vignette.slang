/***********************************
 * exolineartoneF.glsl
 * Provides linear tone mapping functionality.
 * Copyright Geenz Spad, 2012
 **********************************
 *
 * Converted to Slang as vignette.slang (converted from post/exoPostBaseV.glsl, post/exoVignetteF.glsl).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
import glsl;
import position;

uniform sampler2D diffuseRect;
uniform vec3 vignette;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec2 vary_fragcoord : VARY_FRAGCOORD;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.pos = vkPositionNdc(v.position.xyz);
    o.vary_fragcoord = (v.position.xy * 0.5 + 0.5);
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    vec4 diff = texture(diffuseRect, i.vary_fragcoord.xy);

    vec2 tc = i.vary_fragcoord - 0.5f;
    float vignette_val = 1 - dot(tc, tc);
    diff.rgb *= clamp(pow(mix(1, vignette_val * vignette_val * vignette_val * vignette_val * vignette.z, vignette.x), vignette.y), 0, 1);

    return diff;
}
