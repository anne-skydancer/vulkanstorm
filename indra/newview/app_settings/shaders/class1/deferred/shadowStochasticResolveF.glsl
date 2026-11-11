/**
 * @file shadowStochasticResolveF.glsl
 * @brief The Stochastic shadow method's temporal resolve, fragment stage (GL 4.0 to 4.2)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 */

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

// The resolve itself is shadowStochasticResolveUtilF.glsl (listed with this file) and the engine's
// stochasticResolveUtilF.glsl (the STOCH_RESOLVE object); the result is the next frame's history.
vec4 shadow_resolve(ivec2 px, ivec2 size);
uniform sampler2D shadow_st_raw;

void main()
{
    frag_color = shadow_resolve(ivec2(gl_FragCoord.xy), textureSize(shadow_st_raw, 0));
}
