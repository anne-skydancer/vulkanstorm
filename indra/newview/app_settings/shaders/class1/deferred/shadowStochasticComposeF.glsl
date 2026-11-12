/**
 * @file shadowStochasticComposeF.glsl
 * @brief The Stochastic shadow method's light map from its resolved history
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 */

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

// The light map the soften and light passes read (r sun shadow, g AO; the projectors' visibility is the local light ratio's):
// the resolved r, and this frame's AO (the AO is blurred afterwards, not accumulated).
uniform sampler2D shadow_st_raw;
uniform sampler2D shadow_st_resolved;
uniform float shadow_st_scale;   // 1; a snapshot's 8 summed passes 1/8 (raw and resolved are then the sum)

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec4 raw = texelFetch(shadow_st_raw, px, 0);
    vec4 res = texelFetch(shadow_st_resolved, px, 0);
    // r the sun, g this frame's AO; b and a are the projector channels of the Legacy light map, which the
    // local light ratio replaced (unified lighting U1): nothing reads them
    frag_color = vec4(vec2(res.r, raw.g) * shadow_st_scale, 1.0, 1.0);
}
