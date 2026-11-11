/**
 * @file shadowStochasticResolveC.glsl
 * @brief The Stochastic shadow method's temporal resolve, compute stage (GL 4.3 and later)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 */

/*[EXTRA_CODE_HERE]*/

// GL 4.3 compute, as the transparency's resolve (alphaStochasticResolveC.glsl): the same resolve source
// (shadowStochasticResolveUtilF.glsl and the engine's stochasticResolveUtilF.glsl are compiled as further
// compute objects of the program), so the result is the fragment resolve's. The history written this
// frame is an image, bound with glBindImageTexture (LLStochasticHistory::bindWriteImage).

layout(local_size_x = 16, local_size_y = 16) in;

uniform sampler2D shadow_st_raw;
layout(binding = 0, rgba16f) uniform writeonly image2D shadow_st_out;

vec4 shadow_resolve(ivec2 px, ivec2 size);

void main()
{
    ivec2 size = textureSize(shadow_st_raw, 0);
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= size.x || px.y >= size.y)
    {
        return;
    }
    imageStore(shadow_st_out, px, shadow_resolve(px, size));
}
