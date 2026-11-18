/**
 * @file avatarAlphaMaskShadowF.glsl
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2005, Linden Research, Inc.
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

uniform float minimum_alpha;
uniform sampler2D diffuseMap;

in float target_pos_x;
in float pos_w;
in vec2 vary_texcoord0;

#if defined(STOCHASTIC_SHADOW_ALPHA)
uniform vec4 color;               // <Vulkanstorm/> the mesh colour, as avatarAlphaShadowF.glsl takes it
flat in uint shadow_caster_tri;   // <Vulkanstorm/> the triangle's seed (shadowStochasticCasterV.glsl)
bool shadow_caster_keeps(float alpha, uint tri);   // <Vulkanstorm/> the caster rule (shadowStochasticCasterF.glsl)
#endif

void main()
{
    float alpha = texture(diffuseMap, vary_texcoord0.xy).a;

#if defined(STOCHASTIC_SHADOW_ALPHA)
    // <Vulkanstorm/> Stochastic shadows: this pass draws the same meshes as the alpha blend pass (LLDrawPoolAvatar, both with
    // the transparent parts), so it takes the same alpha and the same rule, with no cut: the two passes then keep the same
    // texels and the avatar casts one layer, not two (see avatarAlphaShadowF.glsl and shadowAlphaMaskF.glsl).
    alpha *= color.a;
    if (alpha < 1.0 && !shadow_caster_keeps(alpha, shadow_caster_tri))
    {
        discard;
    }
#else
    if (alpha < 0.05) // treat as totally transparent
    {
        discard;
    }

    if (alpha < minimum_alpha)
    {
      if (fract(0.5*floor(target_pos_x / pos_w )) < 0.25)
      {
        discard;
      }
    }
#endif

    frag_color = vec4(1,1,1,1);
}
