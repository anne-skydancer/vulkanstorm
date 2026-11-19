/**
 * @file glowextract.slang (converted from effects/glowExtractV.glsl, effects/glowExtractF.glsl)
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

// HAS_NOISE 1 dithers the extracted colour with the glow noise map (RenderGlowNoise)
uniform sampler2D diffuseMap;
#if HAS_NOISE
uniform sampler2D glowNoiseMap;
uniform vec2 screen_res;
#endif
uniform float minLuminance;
uniform float maxExtractAlpha;
uniform vec3 lumWeights;
uniform vec3 warmthWeights;
uniform float warmthAmount;

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
};

struct Varyings
{
    vec4 pos : SV_Position;
    [[vk::location(0)]] vec2 vary_texcoord0 : VARY_TEXCOORD0;
};

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.pos = vkPositionNdc(v.position);
    o.vary_texcoord0 = v.position.xy * 0.5 + 0.5;
    return o;
}

[shader("fragment")]
vec4 fsMain(Varyings i) : SV_Target0
{
    vec4 col = texture(diffuseMap, i.vary_texcoord0.xy);

    /// CALCULATING LUMINANCE (Using NTSC lum weights)
    /// http://en.wikipedia.org/wiki/Luma_%28video%29
    float lum = smoothstep(minLuminance, minLuminance + 1.0, dot(col.rgb, lumWeights));
    float warmth = smoothstep(minLuminance, minLuminance + 1.0, max(col.r * warmthWeights.r, max(col.g * warmthWeights.g, col.b * warmthWeights.b)));

#if HAS_NOISE
    float TRUE_NOISE_RES = 128; // See mTrueNoiseMap

    // *NOTE: Usually this is vary_fragcoord not vary_texcoord0, but glow extraction is in screen space
    vec3 glow_noise = texture(glowNoiseMap, i.vary_texcoord0.xy * (screen_res / TRUE_NOISE_RES)).xyz;

    // Dithering. Reduces banding effects in the reduced precision glow buffer.
    float NOISE_DEPTH = 64.0;
    col.rgb += glow_noise / NOISE_DEPTH;
    col.rgb = max(col.rgb, vec3(0));
#endif

    vec4 frag_color;
    frag_color.rgb = col.rgb;
    frag_color.a = max(col.a, mix(lum, warmth, warmthAmount) * maxExtractAlpha);
    return frag_color;
}
