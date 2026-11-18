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

#if defined(STOCHASTIC_SHADOW_ALPHA)
flat in uint shadow_caster_tri;   // <Vulkanstorm/> the triangle's seed (shadowStochasticCasterV.glsl)
bool shadow_caster_keeps(float alpha, uint tri);   // <Vulkanstorm/> the caster rule (shadowStochasticCasterF.glsl)
#endif

void main()
{
    float alpha = texture(diffuseMap,vary_texcoord0.xy).a;

    alpha *= vertex_color.a;

#if defined(STOCHASTIC_SHADOW_ALPHA)
    // <Vulkanstorm/> Stochastic shadows: in proportion to the opacity with no cut, solid at full opacity (the caster rule,
    // see shadowAlphaMaskF.glsl). Legacy keeps the stock floor and the 0.88 stipple.
    if (alpha < 1.0 && !shadow_caster_keeps(alpha, shadow_caster_tri))
    {
        discard;
    }
#else
    if (alpha < 0.05) // treat as totally transparent
    {
        discard;
    }

    if (alpha < 0.88) // treat as semi-transparent
    {
        if (fract(0.5*floor(target_pos_x / post_pos.w )) < 0.25)
        {
            discard;
        }
    }
#endif

    frag_color = vec4(1,1,1,1);
}
