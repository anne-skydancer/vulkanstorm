/**
 * @file stochasticSampleF.glsl
 * @brief The stochastic engine's sampling, shared by its producers
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

// One fragment shader object, compiled once (LLViewerShaderMgr::loadBasicShaders) and attached to
// every program of a stochastic producer: the alpha programs' ALPHA_STOCHASTIC variants today,
// the shadow programs of the Stochastic shadow method later (LLShaderMgr::attachShaderFeatures).
// Producers keep their own identities (the face and triangle of a blended face) and their own
// frame counters; the engine gives the hash, the per-frame seed and the stratified mask from the
// S = 8 table that LLStochasticEngine owns, rotated by the pixel's value.

uniform usampler2D stoch_masks;   // [r][column]: a random r-subset of the S sample bits (2048 x (S + 1))

#define STOCH_SEEDS 2048u

uint stoch_hash(uint v)
{
    uint x = v * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (x >> 22u) ^ x;
}

// A stable identity's seed of this frame; frame 0 keeps the field fixed (no accumulation). With
// the temporal accumulation the field changes every frame, from stable identities.
uint stoch_frame_seed(uint seed, int frame)
{
    if (frame != 0)
    {
        seed = stoch_hash(seed + uint(frame) * 0x9e3779b9u);
    }
    return seed;
}

// The S-bit mask (s = S) of a fragment of coverage a at pixel px: R = floor(a S + xi) bits, xi the
// pixel's value rotated by the seed, which dithers R. The subset is hashed from the seed and the
// pixel, whatever the pixel values, so the noise is a field fixed in screen space and
// uncorrelated between fragments: two layers whose rotations met on one table index would
// otherwise share a subset over their whole overlap, and the layers behind them would see too few
// covered samples (the Aurora Viewer's fix of 2026-11-06, the author's own: object.wgsl st_mask).
uint stoch_mask(float a, uint seed, uvec2 px, uint s)
{
    uint h = stoch_hash(px.x | (px.y << 16u));
    float xi = fract(float(h >> 8u) / 16777216.0 + float(seed >> 8u) / 16777216.0);
    float col = float(stoch_hash(seed ^ h ^ 0x5bd1e995u) >> 8u) / 16777216.0;
    uint r = min(uint(floor(clamp(a, 0.0, 1.0) * float(s) + xi)), s);
    uint column = min(uint(col * float(STOCH_SEEDS)), STOCH_SEEDS - 1u);
    return texelFetch(stoch_masks, ivec2(int(column), int(r)), 0).r;
}
