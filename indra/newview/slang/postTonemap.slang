/**
 * @file postTonemap.slang (converted from postDeferredTonemap.glsl and tonemapUtilF.glsl)
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
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
import postfullscreen;
import postcolor;

uniform sampler2D diffuseRect; // Unit 0, where LLPipeline::tonemap unbinds the source.

#if GAMMA_MODE == 2
uniform float gamma;
#endif

#if !NO_POST
uniform sampler2D exposureMap;
uniform float exposure;
uniform float tonemap_mix;
uniform int tonemap_type;

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const mat3 ACESInputMat = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const mat3 ACESOutputMat = mat3(
    1.60475, -0.10208, -0.00327,
    -0.53108, 1.10813, -0.07276,
    -0.07367, -0.00605, 1.07602);

// ACES filmic tone map approximation
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
vec3 RRTAndODTFit(vec3 color)
{
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return a / b;
}

vec3 toneMapACES_Hill(vec3 color)
{
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0);
}

// Khronos Neutral tonemapping
// https://github.com/KhronosGroup/ToneMapping/tree/main
// Input color is non-negative linear Rec. 709, output color is in [0, 1].
vec3 PBRNeutralToneMapping(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
    {
        return color;
    }

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

vec3 toneMap(vec3 color)
{
    float exp_scale = texture(exposureMap, vec2(0.5, 0.5)).r;
    vec3 exposed_color = color * (exposure * exp_scale);

    vec3 tonemapped_color = exposed_color;
    switch (tonemap_type)
    {
    case 0:
        tonemapped_color = PBRNeutralToneMapping(exposed_color);
        break;
    case 1:
        tonemapped_color = toneMapACES_Hill(exposed_color);
        break;
    }

    return clamp(mix(exposed_color, tonemapped_color, tonemap_mix), 0.0, 1.0);
}
#endif

[shader("vertex")]
PostVaryings vsMain(PostVertexIn v)
{
    return postFullscreenVertex(v);
}

[shader("fragment")]
vec4 fsMain(PostVaryings i) : SV_Target0
{
    vec4 diff = texture(diffuseRect, i.tc);

#if NO_POST
    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0));
#else
    diff.rgb = toneMap(diff.rgb);
#endif

#if GAMMA_MODE != 0
    diff.rgb = linear_to_srgb(diff.rgb);
#endif
#if GAMMA_MODE == 2
    diff.rgb = legacyGamma(diff.rgb, gamma);
#endif

    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0));
    return diff;
}
