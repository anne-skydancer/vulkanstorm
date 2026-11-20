/**
 * @file postGammaCorrect.slang (converted from postDeferredGammaCorrect.glsl)
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
import postcolor;

uniform sampler2D diffuseRect;
#if LEGACY_GAMMA
uniform float gamma;
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
    diff.rgb = linear_to_srgb(diff.rgb);

#if LEGACY_GAMMA
    diff.rgb = legacyGamma(diff.rgb, gamma);
#endif

    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0));
    return diff;
}
