/**
 * @file luminance.slang (converted from luminanceF.glsl)
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
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
import gbuffer;
import postfullscreen;

uniform sampler2D diffuseRect;
uniform sampler2D emissiveRect;
uniform sampler2D normalMap;
uniform float diffuse_luminance_scale;

[shader("vertex")]
PostVaryings vsMain(PostVertexIn v)
{
    return postFullscreenVertex(v);
}

[shader("fragment")]
vec4 fsMain(PostVaryings i) : SV_Target0
{
    vec2 tc = i.tc * 0.6 + 0.2;
    tc.y -= 0.1; // Favour the ground over the sky in the exposure sample.
    vec3 c = texture(diffuseRect, tc).rgb;
    float flag = texture(normalMap, tc).w;

    if (!hasGBufferFlag(flag, GBUFFER_FLAG_HAS_HDRI) &&
        !hasGBufferFlag(flag, GBUFFER_FLAG_SKIP_ATMOS))
    {
        // Scale the luminance of objects while allowing bright skies.
        c *= diffuse_luminance_scale;
    }

    c += texture(emissiveRect, tc).rgb;
    float luminance = dot(vec3(0.2126, 0.7152, 0.0722), c);
    return vec4(max(luminance, 0.0));
}
