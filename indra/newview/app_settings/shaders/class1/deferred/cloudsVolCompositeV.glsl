/**
 * @file class1/deferred/cloudsVolCompositeV.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The volumetric clouds' composite (stage L7): the screen triangle at the far plane, so that the depth test (LEQUAL
 * against the deferred depth, no writes) lets only the sky's pixels through.
 */

in vec3 position;

void main()
{
    gl_Position = vec4(position.xy, 1.0, 1.0);
}
