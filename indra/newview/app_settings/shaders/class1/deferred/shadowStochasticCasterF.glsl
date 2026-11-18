/**
 * @file shadowStochasticCasterF.glsl
 * @brief The Stochastic shadow method's rule for blended casters: coverage in proportion to the opacity
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

// After the Aurora Viewer's crates/aurora-render/src/shaders/shadow_alpha.wgsl (sst_keep, fs_shadow_aurora), Anne Skydancer,
// GPL-3.0-or-later. One fragment shader object, compiled once (LLViewerShaderMgr::loadBasicShaders) and attached with the
// engine's sampling (stochasticSampleF.glsl) to the caster programs of the Stochastic shadow method (STOCHASTIC_SHADOW_ALPHA:
// shadowAlphaMaskF, pbrShadowAlphaBlendF, avatarAlphaShadowF, avatarAlphaMaskShadowF; LLShaderMgr::attachShaderFeatures).
//
// A blended face casts R = floor(4 alpha + xi) of the 4 texels of each 2 x 2 block of the shadow map, a random R-subset, so
// the map holds the face's opacity as a coverage that the receivers' filter averages back (Enderton, Sintorn, Shirley,
// Luebke, "Stochastic Transparency", I3D 2010, section 3.1: a transparent shadow map rendered by stochastically discarding
// fragments). Independent layers multiply: two faces of 0.5 leave 0.25 of the light, as light through two sheers does.
// No cut: a 3 % sheer casts 3 %, a 98 % pane lets 2 % through (owner decision D4, 2026-11-18).
//  - The rule is the transparency's coverage rule at S = 4 with the 2 x 2 block standing for the samples: xi is the engine's
//    dither (stoch_xi24: the Aurora Viewer's blue-noise tile at the block, the layer's own offset, the frame's golden-ratio
//    step; owner decision D3), the subset is picked per block and layer from the 16 subsets of 4 bits.
//  - The blocks are anchored where the map's texels are: a sun cascade subtracts its wrapped world-grid index
//    (LLShadowCascadeFit::alphaDitherOrigin, so a block stays on its world texel while the camera walks), a projector tile its
//    corner in the atlas (shadow_tile_origin, LLLocalShadowGL::setTileOrigin).
//  - The layer's seed is the draw's identity (st_seed: LLDrawInfo::mStochasticSeed, the avatar's for the avatar passes), the
//    triangle (a hash of its provoking vertex's position, shadowStochasticCasterV.glsl) and the side: identical meshes of
//    different objects (rigged, attachments, repeats) and the triangles of one mesh are independent layers.
//  - shadow_caster_frame 0 keeps the pattern fixed; the sun cascades of the main view rotate it every frame while the
//    Stochastic shadows' history accumulates (frames 1 .. 8, the Aurora Viewer's period), which averages it out, as
//    Enderton et al. re-render the shadow map with new seeds. Projector tiles and probes keep it fixed.
// tests/llstochastichistory_test.cpp holds a C++ twin of shadow_caster_keeps with the Aurora Viewer's golden values and
// properties; scripts/tests/test_shadow_alpha_permutation.py pins this text against the twin.

uniform vec2 shadow_tile_origin;   // where the blocks start in this map, in texels (0 for every pass but the sun cascades and the projector tiles)
uniform int st_seed;               // the draw's identity (LLDrawInfo::mStochasticSeed, or the avatar's)
uniform int shadow_caster_frame;   // 0: a fixed pattern; 1 .. 8: the frame of a rotating one

uint stoch_hash(uint v);
uint stoch_frame_seed(uint seed, int frame);
uint stoch_xi24(uvec2 px, uint face_seed, int frame, uint s);

// the S of the engine's table, whose two rows after the S + 1 mask rows hold the tile (LLStochasticEngine::TRANSPARENCY_SAMPLES)
#define SHADOW_CASTER_TABLE_S 8u

// The 16 subsets of a 2 x 2 block's 4 texels by size R, packed in nibbles (no array): SHADOW_CASTER_LO holds entries 0 .. 7,
// SHADOW_CASTER_HI 8 .. 15 (0 | 1 2 4 8 | 3 5 6 9 10 12 | 7 11 13 14 | 15); SHADOW_CASTER_OFF is the first entry of each R and
// SHADOW_CASTER_CNT the number of entries (the Aurora Viewer's SST_LO, SST_HI, SST_OFF, SST_CNT).
#define SHADOW_CASTER_LO 0x65384210u
#define SHADOW_CASTER_HI 0xFEDB7CA9u
#define SHADOW_CASTER_OFF 0xFB510u
#define SHADOW_CASTER_CNT 0x14641u

// Whether the blended face of opacity alpha (in [0, 1)) on triangle tri casts this fragment's texel.
bool shadow_caster_keeps(float alpha, uint tri)
{
    ivec2 cell = ivec2(floor(gl_FragCoord.xy - shadow_tile_origin));
    // the block's coordinates, 16 bits each (a negative one must not OR away the other)
    uvec2 block = uvec2(cell >> 1) & 0xffffu;
    uint layer = stoch_hash(stoch_hash(uint(st_seed) * 0x9e3779b9u + 0x7f4a7c15u) ^ stoch_hash(tri * 2u + (gl_FrontFacing ? 0u : 1u) + 0x632be5abu));
    uint seed = stoch_frame_seed(layer, shadow_caster_frame);
    float xi = float(stoch_xi24(block, layer, shadow_caster_frame, SHADOW_CASTER_TABLE_S)) / 16777216.0;
    float pick = float(stoch_hash(seed ^ stoch_hash(block.x | (block.y << 16u)) ^ 0x5bd1e995u) >> 8u) / 16777216.0;
    uint r = min(uint(floor(clamp(alpha, 0.0, 1.0) * 4.0 + xi)), 4u);
    uint cnt = (SHADOW_CASTER_CNT >> (4u * r)) & 0xfu;
    uint i = ((SHADOW_CASTER_OFF >> (4u * r)) & 0xfu) + min(uint(pick * float(cnt)), cnt - 1u);
    uint subset = ((i >= 8u ? SHADOW_CASTER_HI : SHADOW_CASTER_LO) >> (4u * (i & 7u))) & 0xfu;
    uint bit = uint(cell.x & 1) | (uint(cell.y & 1) << 1u);
    return (subset & (1u << bit)) != 0u;
}
