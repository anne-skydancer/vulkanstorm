/**
 * @file dofBypass.slang (converted from postDeferredNoDoFF.glsl)
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
import postfullscreen;
import hdrRange;

uniform sampler2D diffuseRect;
uniform sampler2D depthMap;
#if HAS_NOISE
uniform vec2 screen_res;

// Noise from https://www.shadertoy.com/view/4dS3Wd
// By Morgan McGuire @morgan3d, http://graphicscodex.com
float noiseHash(vec2 p)
{
    return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x))));
}

float noise(vec2 x)
{
    vec2 i = floor(x);
    vec2 f = fract(x);
    float a = noiseHash(i);
    float b = noiseHash(i + vec2(1.0, 0.0));
    float c = noiseHash(i + vec2(0.0, 1.0));
    float d = noiseHash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}
#endif

[shader("vertex")]
PostVaryings vsMain(PostVertexIn v)
{
    return postFullscreenVertex(v);
}

struct FragOut
{
    vec4 color : SV_Target0;
    float depth : SV_Depth;
};

[shader("fragment")]
FragOut fsMain(PostVaryings i)
{
    FragOut o;
    vec4 diff = texture(diffuseRect, i.tc);

#if HAS_NOISE
    vec2 tc = i.tc * screen_res * 4.0;
    vec3 seed = (diff.rgb + vec3(1.0)) * vec3(tc, tc.x + tc.y);
    vec3 nz = vec3(noise(seed.rg), noise(seed.gb), noise(seed.rb));
    diff.rgb += nz * 0.003;
#endif

    diff.rgb = clampHDRRange(diff.rgb);
    o.color = diff;
    o.depth = texture(depthMap, i.tc).r;
    return o;
}
