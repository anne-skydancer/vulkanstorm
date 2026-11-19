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
// transparency's table (S = LLStochasticEngine::TRANSPARENCY_SAMPLES = 8), dithered by a blue-noise tile.
//
// Monte Carlo against grain (stochastic transparency's coverage R = floor(a S + xi) of S samples is a
// stratified estimate; its error is the grain the resolve measures and averages):
//  - the pixel's dither xi is a 64 x 64 blue-noise tile (void and cluster, Ulichney 1993,
//    scripts/perf/gen_stochastic_blue_noise.py) plus a face offset plus the golden-ratio step of the frame
//    (a Kronecker sequence, one multiply): the dither of neighbouring pixels is stratified in screen
//    space, and a pixel's dither over the frames equidistributed, where a hash's is white in both;
//  - the subset of the covered samples is still drawn afresh from the table, per face, triangle, pixel and
//    frame (a subset fixed over the frames, or one rotation stride for all layers, measured 1.8 to 1.9
//    times worse).

uniform usampler2D stoch_masks;   // [r][column]: a random r-subset of the S sample bits (2048 x (S + 1)), then the blue-noise tile's ranks in two more rows

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
    return (floatBitsToUint(x) & 0x7f800000u) != 0x7f800000u;
}

// The pixel's dither xi in [0, 1): the blue-noise tile's value (rank + 0.5) / 4096 of the pixel (tiled), the face's
// offset (its seed), and the frame's golden-ratio step: frame * 2^32 / phi mod 2^32 (a Kronecker sequence, exact in
// integers; frame 0, the fixed field, takes no step). The sum is a rotation of the tile modulo 1: every pixel's
// values are uniform, and the tile's neighbours are spread.
// (The table must be the one LLStochasticEngine::transparencyTable(S) builds: S + 1 mask rows, then the tile's NOISE_ROWS rows.)
float stoch_dither(uvec2 px, uint face_seed, int frame, uint s)
{
    uint at = (px.y & (STOCH_NOISE_SIZE - 1u)) * STOCH_NOISE_SIZE + (px.x & (STOCH_NOISE_SIZE - 1u));
    float blue = (float(texelFetch(stoch_masks, ivec2(int(at & (STOCH_SEEDS - 1u)), int(s + 1u + (at >> 11u))), 0).r) + 0.5) / 4096.0;
    float face = float(stoch_hash(face_seed ^ 0x2545f491u) >> 8u) / 16777216.0;
    float step_of_frame = frame != 0 ? float((uint(frame) * 0x9e3779b9u) >> 8u) / 16777216.0 : 0.0;
    return fract(blue + face + step_of_frame);
}

// The S-bit mask (s = S) of a fragment of coverage a at pixel px: R = floor(a S + xi) bits, xi the dither above.
// The subset is hashed from the face's seed, the frame and the pixel, whatever the pixel values, so the noise is
// uncorrelated between fragments: two layers whose rotations met on one table index would otherwise share a
// subset over their whole overlap, and the layers behind them would see too few covered samples (the Aurora
// Viewer's fix of 2026-11-06, the author's own: object.wgsl st_mask).
uint stoch_mask(float a, uint face_seed, int frame, uvec2 px, uint s)
{
    uint seed = stoch_frame_seed(face_seed, frame);
    uint h = stoch_hash(px.x | (px.y << 16u));
    float xi = stoch_dither(px, face_seed, frame, s);
    float col = float(stoch_hash(seed ^ h ^ 0x5bd1e995u) >> 8u) / 16777216.0;
    uint r = min(uint(floor(clamp(a, 0.0, 1.0) * float(s) + xi)), s);
    uint column = min(uint(col * float(STOCH_SEEDS)), STOCH_SEEDS - 1u);
    return texelFetch(stoch_masks, ivec2(int(column), int(r)), 0).r;
}
