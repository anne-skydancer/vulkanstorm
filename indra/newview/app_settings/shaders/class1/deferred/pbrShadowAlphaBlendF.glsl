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

#if defined(SHADOW_STOCH_ALPHA)
// Stochastic shadows, S1 coverage: a Blending caster keeps a fragment when alpha > threshold. The threshold is
// interleaved-gradient noise of the shadow-map texel (world-stable for a stable cascade), plus a per-layer hash
// of its 4 x 4 block (shadow_st_layer: constant per drawn batch, so stacked layers are independent and the
// kept fraction through n layers is 1 - prod(1 - alpha_i)), plus the golden-ratio phase of the frame
// (shadow_st_caster_phase, from the CPU: stratified over time). The body is the same in all three casters.
uniform float shadow_st_caster_phase;
uniform float shadow_st_layer;
float shadowCasterThreshold()
{
    vec2 p = gl_FragCoord.xy;
    float n = fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
    vec3 p3 = fract(vec3(floor(p * 0.25).xyx + shadow_st_layer) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract(n + fract((p3.x + p3.y) * p3.z) + shadow_st_caster_phase);
}
#endif

void main()
{
#if defined(SHADOW_STOCH_ALPHA)
    // Stochastic shadows: finer mip, no stipple
    float alpha = texture(diffuseMap,vary_texcoord0.xy,SHADOW_ALPHA_LOD_BIAS).a;
#else
    float alpha = texture(diffuseMap,vary_texcoord0.xy).a;
#endif

    alpha *= vertex_color.a;

    if (alpha < 0.05) // treat as totally transparent
    {
        discard;
    }

#if defined(SHADOW_STOCH_ALPHA)
    // Blending: kept with probability alpha (the blend cutoff does not apply)
    if (alpha <= shadowCasterThreshold())
    {
        discard;
    }
#else
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
