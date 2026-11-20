/**
 * @file dofBlur.slang (converted from postDeferredF.glsl)
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
uniform vec2 screen_res;
uniform float max_cof;

[shader("vertex")]
PostVaryings vsMain(PostVertexIn v)
{
    return postFullscreenVertex(v);
}

void addSample(inout vec4 diff, inout float weight, vec4 sample_color)
{
    // De-weight dull areas to make highlights stand out.
    float sample_weight = 0.25;
    sample_weight += sample_color.r + sample_color.g + sample_color.b;
    diff += sample_weight * sample_color;
    weight += sample_weight;
}

[shader("fragment")]
vec4 fsMain(PostVaryings i) : SV_Target0
{
    vec4 diff = texture(diffuseRect, i.tc);
    float weight = 1.0;
    float sc = (diff.a * 2.0 - 1.0) * max_cof;
    const float PI = 3.14159265358979323846264;

    // Sample rings for circular bokeh. The far-field pass excludes samples
    // that are more in focus than the current radius.
    bool near_field = sc > 0.5;
    sc = abs(sc);
    while (sc > 0.5)
    {
        int samples = int(max(1.0, sc * 3.7));
        for (int sample_index = 0; sample_index < samples; ++sample_index)
        {
            float angle = sc + sample_index * 2 * PI / samples;
            vec2 offset = vec2(sc * sin(angle), sc * cos(angle)) / screen_res;
            vec4 sample_color = texture(diffuseRect, i.tc + offset);
            if (near_field || abs(sample_color.a * 2.0 - 1.0) * max_cof > sc)
            {
                addSample(diff, weight, sample_color);
            }
        }
        sc -= 1.0;
    }

    diff /= weight;
    diff.rgb = clampHDRRange(diff.rgb);
    return diff;
}
