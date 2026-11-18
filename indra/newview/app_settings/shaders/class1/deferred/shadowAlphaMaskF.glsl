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
#if defined(STOCHASTIC_SHADOW_ALPHA)
uniform float shadow_solid_alpha; // <Vulkanstorm/> 1.0 for blend faces (the caster rule below full opacity); 0.0 for mask faces
bool shadow_caster_keeps(float alpha);   // <Vulkanstorm/> the caster rule (shadowStochasticCasterF.glsl)
#endif

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

#if defined(STOCHASTIC_SHADOW_ALPHA)
    // <Vulkanstorm/> Stochastic shadows. A mask face (shadow_solid_alpha 0) follows the creator's cutoff (minimum_alpha,
    // above) and the stock floor, and is solid otherwise: never dithered. A blend face (shadow_solid_alpha 1, minimum_alpha 0)
    // casts in proportion to its opacity with no cut (the Aurora Viewer's rule, shadowStochasticCasterF.glsl): R of the 4
    // texels of each 2 x 2 block, a random R-subset per layer, so stacked sheers multiply their transmittance; a texel at
    // full opacity is solid. (Legacy keeps the stock floor and the 0.88 stipple, below.)
    if (shadow_solid_alpha <= 0.0)
    {
        if (alpha < 0.05)
        {
            discard;
        }
    }
    else if (alpha < shadow_solid_alpha && !shadow_caster_keeps(alpha))
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
