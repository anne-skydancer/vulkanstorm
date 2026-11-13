/**
 * @file shadowAlphaMaskF.glsl
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
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

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

in vec4 post_pos;
in float target_pos_x;
in vec4 vertex_color;
in vec2 vary_texcoord0;
uniform float minimum_alpha;

void main()
{
    float alpha = diffuseLookup(vary_texcoord0.xy).a;

    if (alpha < minimum_alpha)
    {
        discard;
    }

#if !defined(IS_FULLBRIGHT)
    alpha *= vertex_color.a;
#endif

    if (alpha < 0.05) // treat as totally transparent
    {
        discard;
    }

    if (alpha < 0.88) // treat as semi-transparent
    {
#if defined(STOCHASTIC_SHADOW_ALPHA)
        // <Vulkanstorm/> Stochastic shadows read one texel per ray; a stipple that drops every other texel column
        // reads as columns of light and shade. A hashed per-texel dither (anchored to the map's texel, so it is
        // stable wherever the map is) keeps the fraction alpha of the texels, with no column period. It is still grain at
        // texel scale: static, with no per-layer or per-frame decorrelation, so only the ray jitter averages it. Every
        // texel below 0.88 is dithered here, mask or blend faces alike, as the stipple it replaces did.
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
