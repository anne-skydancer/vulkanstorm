/**
 * @file alphaCoverageMask.slang
 * @brief Stochastic transparency: the coverage mask of a blended fragment
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
module alphaCoverageMask;
import glsl;

uniform int st_mode;
uniform float st_min_alpha;
uniform int st_samples_hw;
uniform int st_samples;
uniform int st_seed;
uniform int st_frame;

// From stochasticSampleF.glsl, including the 24-bit blue-noise rotation and frame step.
#define STOCH_SEEDS 2048u
#define STOCH_NOISE_SIZE 64u

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

// Whether x is a finite number, tested on its bits (the exponent is all ones for an infinity and a NaN): a driver
// may fold x != x to false, and what min, max and clamp make of a NaN is left to the card.
bool stoch_finite(float x)
{
    return (asuint(x) & 0x7f800000u) != 0x7f800000u;
}

// The dither of px in 24-bit fixed point, xi = xi24 / 2^24 in [0, 1): the blue-noise tile's stratum (2 rank + 1) / 8192 of
// the pixel (tiled), rotated by the face's own offset (the top 24 bits of its seed: Cranley-Patterson, so that layers stay
// decorrelated) and stepped by the frame's golden-ratio fraction (frame * 0x9e3779b9 >> 8, the conjugate's share of 2^24;
// frame 0, the fixed field, takes no step), all modulo 2^24. Integer arithmetic, so it is exact at any frame number: over the
// tile the mean of R is a S within one stratum for any rotation, and eight consecutive frames of a pixel sum to 8 a S within 1
// for a on a 1/64 grid, within 1.4 for any a (the local discrepancy of eight golden-ratio points; a fresh hash reaches 4).
// (The table must be the one LLStochasticEngine::transparencyTable(S) builds: S + 1 mask rows, then the tile's NOISE_ROWS rows.)
uint stoch_xi24(usampler2D mask_table, uvec2 px, uint face_seed, int frame, uint s)
{
    uint at = (px.y & (STOCH_NOISE_SIZE - 1u)) * STOCH_NOISE_SIZE + (px.x & (STOCH_NOISE_SIZE - 1u));
    uint rank = texelFetch(mask_table, ivec2(int(at & (STOCH_SEEDS - 1u)), int(s + 1u + (at >> 11u))), 0).r;
    uint step_of_frame = frame != 0 ? (uint(frame) * 0x9e3779b9u) >> 8u : 0u;
    return ((2u * rank + 1u) * 2048u + (face_seed >> 8u) + step_of_frame) & 0xffffffu;
}

// The S-bit mask (s = S) of a fragment of coverage a at pixel px: R = floor(a S + xi) bits, xi the dither above.
// The subset is hashed from the face's seed, the frame and the pixel, whatever the pixel values, so the noise is
// uncorrelated between fragments: two layers whose rotations met on one table index would otherwise share a
// subset over their whole overlap, and the layers behind them would see too few covered samples (the Aurora
// Viewer's fix of 2026-11-06, the author's own: object.wgsl st_mask).
uint stoch_mask(usampler2D mask_table, float a, uint face_seed, int frame, uvec2 px, uint s)
{
    uint seed = stoch_frame_seed(face_seed, frame);
    uint h = stoch_hash(px.x | (px.y << 16u));
    float xi = float(stoch_xi24(mask_table, px, face_seed, frame, s)) / 16777216.0;
    float col = float(stoch_hash(seed ^ h ^ 0x5bd1e995u) >> 8u) / 16777216.0;
    uint r = min(uint(floor(clamp(a, 0.0, 1.0) * float(s) + xi)), s);
    uint column = min(uint(col * float(STOCH_SEEDS)), STOCH_SEEDS - 1u);
    return texelFetch(mask_table, ivec2(int(column), int(r)), 0).r;
}

public uint coverageMask(usampler2D mask_table, float alpha, uint primitive_id, uvec2 pixel)
{
    if (!stoch_finite(alpha) || (st_mode == 3 && alpha < st_min_alpha))
    {
        discard;
    }
    if (st_mode != 2)
    {
        return 0xffffffffu;
    }

    uint seed = stoch_hash(uint(st_seed) * 0x9e3779b9u + 0x7f4a7c15u);
    seed = stoch_hash(seed ^ stoch_hash(primitive_id + 0x632be5abu));
    uint samples = uint(st_samples_hw);
    uint bits = samples >= 32u ? 0xffffffffu : ((1u << samples) - 1u);
    return stoch_mask(mask_table, alpha, seed, st_frame, pixel, uint(st_samples)) & bits;
}
