/**
 * @file shadowLightThroughF.glsl
 * @brief Lamp shadows L3: a see-through caster into a lamp tile's light-through layer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 *
 * The share of the lamp's light a see-through face lets through goes into the layer's first target, multiplied into what the casters
 * drawn before it let through (glBlendFunc(GL_ZERO, GL_SRC_COLOR): stacked faces multiply, in any order), and its window depth into the
 * second (R32F, as it is), which keeps the nearest (GL_MIN). Only the fragments in front of the tile's opaque depth reach it (the
 * pass is depth-tested against the pool's static depth atlas, LLLightThroughLayer::DrawScope). The rule and its derivation are
 * lllightthrough.h's (transmittance): McGuire & Enderton's rho = a (1 - t) with t = C (1 - a), so tau = (1 - a)(1 + a C). A texel at
 * or below the invisible limit is left out (it records no depth either), and one at or above the solid limit is the depth pass's
 * (shadowAlphaMaskF.glsl and pbrShadowAlphaBlendF.glsl draw it there with shadow_through set). Vertex shaders: shadowAlphaMaskV.glsl
 * for the classic faces (indexed textures), pbrShadowAlphaMaskV.glsl for glTF (LIGHT_THROUGH_GLTF).
 */

/*[EXTRA_CODE_HERE]*/

#define LIGHT_THROUGH_SOLID 0.98              // lllightthrough::kSolidAlpha
#define LIGHT_THROUGH_INVISIBLE (1.0 / 255.0) // lllightthrough::kInvisibleAlpha

out vec4 frag_data[2];

in vec4 vertex_color;
in vec2 vary_texcoord0;

#if defined(LIGHT_THROUGH_GLTF)
uniform sampler2D diffuseMap;
#endif

vec3 srgb_to_linear(vec3 cs);

void main()
{
#if defined(LIGHT_THROUGH_GLTF)
    // as the glTF alpha pass reads it (pbralphaF.glsl): the base colour texture (sRGB) times the factor baked into the vertex colour (linear)
    vec4 tap = texture(diffuseMap, vary_texcoord0.xy);
    float alpha = tap.a * vertex_color.a;
    vec3 colour = vertex_color.rgb * srgb_to_linear(tap.rgb);
#else
    // as the classic alpha pass reads it (alphaF.glsl): the texture times the face's colour, both sRGB, then linear
    vec4 tap = diffuseLookup(vary_texcoord0.xy);
    float alpha = tap.a * vertex_color.a;
    vec3 colour = srgb_to_linear(tap.rgb * vertex_color.rgb);
#endif

    if (!(alpha > LIGHT_THROUGH_INVISIBLE) || alpha >= LIGHT_THROUGH_SOLID)
    {
        discard;
    }

    vec3 c = clamp(colour, vec3(0.0), vec3(1.0));
    frag_data[0] = vec4((1.0 - alpha) * (1.0 + alpha * c), 1.0);
    frag_data[1] = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
