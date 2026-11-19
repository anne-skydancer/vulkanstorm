/**
 * @file class1/deferred/cloudsVolCompositeF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The volumetric clouds' composite (stage L7, doc/light/volumetric-clouds.md): after the lighting pass, on the sky's
 * pixels of the linear HDR target, blended dst * T + L (GL_ONE, GL_SRC_ALPHA; the alpha, the glow, is not written).
 * The dome, its haze, the sun, the moon and the stars already drawn are seen through the clouds' transmittance.
 * cloud_vol_history is the reconstruction's half-resolution image (live) or the still trace (a photo, full size).
 */

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

uniform sampler2D cloud_vol_history;
uniform vec4 cv_sizes; // the full target's width and height

void main()
{
    frag_color = textureLod(cloud_vol_history, gl_FragCoord.xy / cv_sizes.xy, 0.0);
}
