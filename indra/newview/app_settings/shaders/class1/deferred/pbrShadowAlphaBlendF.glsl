/**
 * @file pbrShadowAlphaBlendF.glsl
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

out vec4 frag_color;

uniform sampler2D diffuseMap;

in vec4 post_pos;
in float target_pos_x;
in vec4 vertex_color;
in vec2 vary_texcoord0;
uniform float minimum_alpha;

void main()
{
    float alpha = texture(diffuseMap,vary_texcoord0.xy).a;

    alpha *= vertex_color.a;

    if (alpha < 0.05) // treat as totally transparent
    {
        discard;
    }

#if defined(STOCHASTIC_SHADOW_ALPHA)
    // <Vulkanstorm/> Stochastic shadows: a texel is solid from 0.98 up; below that it lets light through in
    // proportion to its transparency (Legacy keeps the stock 0.88 below).
    if (alpha < 0.98)
#else
    if (alpha < 0.88) // treat as semi-transparent
#endif
    {
#if defined(STOCHASTIC_SHADOW_ALPHA)
        // <Vulkanstorm/> Stochastic shadows: hashed per-texel dither instead of the column stipple (see shadowAlphaMaskF.glsl)
        float ign = fract(52.9829189 * fract(0.06711056 * gl_FragCoord.x + 0.00583715 * gl_FragCoord.y));
        if (ign >= alpha)
        {
            discard;
        }
#else
        if (fract(0.5*floor(target_pos_x / post_pos.w )) < 0.25)
        {
            discard;
        }
#endif
    }

    frag_color = vec4(1,1,1,1);
}
