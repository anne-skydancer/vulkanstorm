/**
 * @file shadowStochasticCasterV.glsl
 * @brief The Stochastic shadow method's rule for blended casters, the vertex half: a triangle's own seed
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

// One vertex shader object (LLViewerShaderMgr::loadBasicShaders), attached to the caster programs of the Stochastic shadow
// method (STOCHASTIC_SHADOW_ALPHA: shadowAlphaMaskV, pbrShadowAlphaMaskV, avatarAlphaShadowV; LLShaderMgr::attachShaderFeatures).
// Their vertex shaders pass shadow_caster_vertex_seed(position) to the fragment half (shadowStochasticCasterF.glsl) as a
// flat value, so a triangle takes its provoking (last) vertex's: the triangle's seed in the caster rule, which the Aurora
// Viewer takes from the primitive index. The position bits are those of the vertex buffer (the batch's frame: the region's
// for static faces, the object's or the bind pose's otherwise), so the seed does not change when the batch is sorted again
// or the camera moves, and the draw's identity (st_seed) separates identical meshes of different objects. Two triangles of
// one draw that share their last vertex share their seed: harmless unless they also overlap in the light.

// The PCG hash of the engine (stochasticSampleF.glsl stoch_hash; that object is a fragment shader's)
uint shadow_caster_vertex_hash(uint v)
{
    uint x = v * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (x >> 22u) ^ x;
}

// The seed of a vertex: the hash chained over its three coordinates' bits
uint shadow_caster_vertex_seed(vec3 p)
{
    uvec3 bits = floatBitsToUint(p);
    return shadow_caster_vertex_hash(shadow_caster_vertex_hash(shadow_caster_vertex_hash(bits.x) ^ bits.y) ^ bits.z);
}
