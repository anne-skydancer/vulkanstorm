/**
 * @file alphaStochasticResolveC.glsl
 * @brief Stochastic transparency: the temporal accumulation of the resolved colour (compute)
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
 */

/*[EXTRA_CODE_HERE]*/

// GL 4.3 compute, A/B only in the OpenGL viewer (VKSTORM_STOCH_COMPUTE_RESOLVE=1; the fragment resolve, alphaStochasticResolveF.glsl, is the default on every device). The transparency's part of the
// stochastic engine's temporal resolve (stochasticResolveUtilF.glsl, compiled as a second compute
// object of this program). The accumulated colour of this frame (alpha x colour, alpha, averaged
// over the samples) is blended with the last frame's blended colour where this pixel's nearest
// transparent surface was, moved by the camera's motion alone (the surfaces' own motion is not
// reprojected) and clamped to the 3 x 3 neighbourhood of this frame as a TAA's history is. The
// result is the next frame's history and the composite's accumulated colour, whose alpha
// correction (total alpha over this alpha) is made there with the current frame's total alpha,
// so that an alpha change is never ghosted. This file gives the value of this frame and the
// reprojection depth (the nearest stochastic depth); the rest is the engine's.
//
// A 16 x 16 workgroup averages the samples of its pixels and of a one-pixel border (coordinates
// clamped to the image, as the neighbourhood clamps its reads) once into an 18 x 18 tile of shared
// memory, which the engine's neighbourhood reads through stoch_current(): the samples are summed
// once per pixel (plus the border's 4 x 16 + 4 texels per group) and no full-size target holds the
// average. The sum is the same, in the same order, as the fragment pre-pass makes, so that the
// result is the same sum in the same order (identical on the measured NVIDIA).

layout(local_size_x = 16, local_size_y = 16) in;

uniform sampler2DMS alpha_st_accum;    // alpha x colour, alpha, additive, over the passes' samples
uniform sampler2DMS alpha_st_depth;    // the stochastic depth: the nearest transparent surface, or the opaque one
uniform sampler2D   alpha_st_history;  // the last frame's blended colour
uniform int         st_samples_hw;     // samples of the targets
uniform int         st_samples;        // samples in all, over the iterations

// the history written this frame (the texture alpha_st_history does not read); bound outside
// LLTexUnit with glBindImageTexture (LLPipeline::resolveAlphaStochasticTemporal)
layout(binding = 0, rgba16f) uniform writeonly image2D alpha_st_out;

bool stoch_has_history();
bool stoch_reproject(ivec2 px, float z, ivec2 size, out vec4 pp, out vec2 puv);
vec4 stoch_history_catmull_rom(sampler2D history, vec2 puv, ivec2 size);
void stoch_neighbourhood(ivec2 px, ivec2 size, vec4 cur, out vec4 mn, out vec4 mx);
vec4 stoch_accumulate(vec4 hist, vec4 mn, vec4 mx, vec4 cur);

#define ST_GROUP 16
#define ST_TILE  18   // ST_GROUP + the one-pixel border of the 3 x 3 neighbourhood

shared vec4 st_tile[ST_TILE][ST_TILE];
ivec2 st_origin;      // this workgroup's first pixel

vec4 st_average(ivec2 px)
{
    vec4 sum = vec4(0.0);
    for (int s = 0; s < st_samples_hw; ++s)
    {
        sum += texelFetch(alpha_st_accum, px, s);
    }
    return sum / float(max(st_samples, 1));
}

// p is a clamped image coordinate within one pixel of this workgroup's block
vec4 stoch_current(ivec2 p)
{
    ivec2 l = p - st_origin + 1;
    return st_tile[l.y][l.x];
}

void main()
{
    ivec2 size = textureSize(alpha_st_history, 0);
    st_origin = ivec2(gl_WorkGroupID.xy) * ST_GROUP;
    // every invocation takes part in filling the tile and in the barrier, also those past the
    // image's edge (a partial group); only then may they leave
    for (int i = int(gl_LocalInvocationIndex); i < ST_TILE * ST_TILE; i += ST_GROUP * ST_GROUP)
    {
        ivec2 l = ivec2(i % ST_TILE, i / ST_TILE);
        st_tile[l.y][l.x] = st_average(clamp(st_origin + l - 1, ivec2(0), size - ivec2(1)));
    }
    memoryBarrierShared();   // GLSL 4.30: barrier() alone does not make the shared writes visible
    barrier();

    ivec2 px = st_origin + ivec2(gl_LocalInvocationID.xy);
    if (px.x >= size.x || px.y >= size.y)
    {
        return;
    }
    vec4 cur = stoch_current(px);
    vec4 res = cur;
    if (stoch_has_history() && cur.a > 0.0)
    {
        float z = 1.0;
        for (int s = 0; s < st_samples_hw; ++s)
        {
            z = min(z, texelFetch(alpha_st_depth, px, s).r);
        }
        vec4 pp;
        vec2 puv;
        if (z < 1.0 && stoch_reproject(px, z, size, pp, puv))
        {
            vec4 old = stoch_history_catmull_rom(alpha_st_history, puv, size);
            vec4 mn;
            vec4 mx;
            stoch_neighbourhood(px, size, cur, mn, mx);
            res = stoch_accumulate(old, mn, mx, cur);
        }
    }
    imageStore(alpha_st_out, px, res);
}
