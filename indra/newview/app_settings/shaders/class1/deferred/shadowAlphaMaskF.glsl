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

#if defined(SHADOW_SOLID_ALPHA)
uniform float shadow_st_coverage;   // 1: a Blending face (continuous coverage), 0: a Masking face (the cutoff)
// Stochastic shadows, S1 coverage: a Blending caster keeps a fragment when alpha > threshold, the threshold
// being interleaved-gradient noise stepped by the golden ratio per frame (stratified over time). The noise is
// anchored to the shadow-map texel, so a stable cascade gives a world-stable pattern.
uniform float shadow_st_caster_frame;
float shadowCasterThreshold()
{
    float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    return fract(n + 0.618034 * shadow_st_caster_frame);
}
#endif

// SHADOW_SOLID_ALPHA (Stochastic shadows): a semi-transparent caster casts solid shade, no raster-column
// stipple (it showed as fixed stripes in thin foliage); the cutoff tests the final alpha and the lookup
// samples a finer mip (SHADOW_ALPHA_LOD_BIAS, applied in the generated diffuseLookup).
void main()
{
    float alpha = diffuseLookup(vary_texcoord0.xy).a;

#if !defined(SHADOW_SOLID_ALPHA)
    if (alpha < minimum_alpha)
    {
        discard;
    }
#endif

#if !defined(IS_FULLBRIGHT)
    alpha *= vertex_color.a;
#endif

#if defined(SHADOW_SOLID_ALPHA)
    // Blending: the fragment is kept with probability alpha (the blend cutoff does not apply); Masking: the
    // creator's cutoff on the final alpha (texture x vertex), as pbrShadowAlphaBlendF did.
    if (shadow_st_coverage < 0.5 && alpha < minimum_alpha)
    {
        discard;
    }
#endif

    if (alpha < 0.05) // treat as totally transparent
    {
        discard;
    }

#if defined(SHADOW_SOLID_ALPHA)
    if (shadow_st_coverage > 0.5 && alpha <= shadowCasterThreshold())
    {
        discard;
    }
#endif

#if !defined(SHADOW_SOLID_ALPHA)
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
