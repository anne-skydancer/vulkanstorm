// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pbrTerrain.slang (converted from class1/deferred/pbrterrainV/F.glsl)
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */
/**
 * Triplanar mapping implementation adapted from Inigo Quilez' example shader,
 * MIT license.
 * https://www.shadertoy.com/view/MtsGWH
 * Copyright © 2015 Inigo Quilez
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions: The above copyright
 * notice and this permission notice shall be included in all copies or
 * substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
import glsl;
import position;
import gbuffer;
import mirrorclip;
import postcolor;
import pbrTexture;

#define TERRAIN_PBR_DETAIL_EMISSIVE 0
#define TERRAIN_PBR_DETAIL_OCCLUSION -1
#define TERRAIN_PBR_DETAIL_NORMAL -2
#define TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS -3

#define TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE 0
#define TERRAIN_PAINT_TYPE_PBR_PAINTMAP 1

// Emissive is only read when an attachment receives it.
#define TERRAIN_SAMPLE_EMISSIVE (HAS_EMISSIVE && (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE))

#include "pbrTerrainVertex.slang"
#include "pbrTerrainUtil.slang"

// OpenGL texture units are separate from uniform block bindings. Pin the units
// so Slang's allocator cannot skip the global block's binding.
#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
[[vk::binding(0, 0)]] uniform sampler2D alpha_ramp;
#elif TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_PBR_PAINTMAP
[[vk::binding(0, 0)]] uniform sampler2D paint_map;
#endif

// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#additional-textures
[[vk::binding(1, 0)]] uniform sampler2D detail_0_base_color;
[[vk::binding(2, 0)]] uniform sampler2D detail_1_base_color;
[[vk::binding(3, 0)]] uniform sampler2D detail_2_base_color;
[[vk::binding(4, 0)]] uniform sampler2D detail_3_base_color;
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
[[vk::binding(5, 0)]] uniform sampler2D detail_0_metallic_roughness;
[[vk::binding(6, 0)]] uniform sampler2D detail_1_metallic_roughness;
[[vk::binding(7, 0)]] uniform sampler2D detail_2_metallic_roughness;
[[vk::binding(8, 0)]] uniform sampler2D detail_3_metallic_roughness;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
[[vk::binding(9, 0)]] uniform sampler2D detail_0_normal;
[[vk::binding(10, 0)]] uniform sampler2D detail_1_normal;
[[vk::binding(11, 0)]] uniform sampler2D detail_2_normal;
[[vk::binding(12, 0)]] uniform sampler2D detail_3_normal;
#endif
#if TERRAIN_SAMPLE_EMISSIVE
[[vk::binding(13, 0)]] uniform sampler2D detail_0_emissive;
[[vk::binding(14, 0)]] uniform sampler2D detail_1_emissive;
[[vk::binding(15, 0)]] uniform sampler2D detail_2_emissive;
[[vk::binding(16, 0)]] uniform sampler2D detail_3_emissive;
#endif

uniform vec4 baseColorFactors[4]; // See also vertex_color in pbropaqueV.glsl
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
uniform vec4 metallicFactors;
uniform vec4 roughnessFactors;
#endif
#if TERRAIN_SAMPLE_EMISSIVE
uniform vec3 emissiveColors[4];
#endif
uniform vec4 minimum_alphas; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()

struct GBufferOut
{
    vec4 data0 : SV_Target0;
    vec4 data1 : SV_Target1;
    vec4 data2 : SV_Target2;
#if HAS_EMISSIVE
    vec4 data3 : SV_Target3;
#endif
};

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
// from mikktspace.com
vec3 mikktspace(vec3 vNt, vec3 vT, float sign, vec3 vary_normal, bool is_front_face)
{
    vec3 vN = vary_normal;

    vec3 vB = sign * cross(vN, vT);
    vec3 tnorm = normalize( vNt.x * vT + vNt.y * vB + vNt.z * vN );

    tnorm *= is_front_face ? 1.0 : -1.0;

    return tnorm;
}
#endif

[shader("fragment")]
GBufferOut fsMain(Varyings i, bool is_front_face : SV_IsFrontFace)
{
    // Make sure we clip the terrain if we're in a mirror.
    mirrorClip(i.vary_position);

    TerrainMix tm;
#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    float alpha1 = texture(alpha_ramp, i.vary_texcoord0.zw).a;
    float alpha2 = texture(alpha_ramp,i.vary_texcoord1.xy).a;
    float alphaFinal = texture(alpha_ramp, i.vary_texcoord1.zw).a;

    tm = get_terrain_mix_weights(alpha1, alpha2, alphaFinal);
#elif TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_PBR_PAINTMAP
    tm = get_terrain_usage_from_weight3(texture(paint_map, i.vary_texcoord).xyz);
#endif

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    // RGB = Occlusion, Roughness, Metal
    // default values, see LLViewerTexture::sDefaultPBRORMImagep
    //   occlusion 1.0
    //   roughness 0.0
    //   metal     0.0
    vec3 orm_factors[4];
    orm_factors[0] = vec3(1.0, roughnessFactors.x, metallicFactors.x);
    orm_factors[1] = vec3(1.0, roughnessFactors.y, metallicFactors.y);
    orm_factors[2] = vec3(1.0, roughnessFactors.z, metallicFactors.z);
    orm_factors[3] = vec3(1.0, roughnessFactors.w, metallicFactors.w);
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    vec2 rm_factors[4];
    rm_factors[0] = vec2(roughnessFactors.x, metallicFactors.x);
    rm_factors[1] = vec2(roughnessFactors.y, metallicFactors.y);
    rm_factors[2] = vec2(roughnessFactors.z, metallicFactors.z);
    rm_factors[3] = vec2(roughnessFactors.w, metallicFactors.w);
#endif

    PBRMix pbr_mix = init_pbr_mix();
    PBRMix mix2;
    TerrainCoord terrain_texcoord;
    switch (tm.type & MIX_X)
    {
    case MIX_X:
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
        terrain_texcoord[0].xy = i.vary_coords[0].xy;
        terrain_texcoord[0].zw = i.vary_coords[0].zw;
        terrain_texcoord[1].xy = i.vary_coords[1].xy;
        terrain_texcoord[1].zw = i.vary_coords[1].zw;
        terrain_texcoord[2].xy = i.vary_coords[2].xy;
        terrain_texcoord[2].zw = vec2(0);
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
        terrain_texcoord = i.vary_coords[0].xy;
#endif
        mix2 = terrain_sample_and_multiply_pbr(
            terrain_texcoord
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , i.vary_vertex_normal
#endif
            , detail_0_base_color
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_0_metallic_roughness
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_0_normal
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , i.vary_signs[0]
#endif
#endif
#if TERRAIN_SAMPLE_EMISSIVE
            , detail_0_emissive
#endif
            , baseColorFactors[0]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[0]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[0]
#endif
#if TERRAIN_SAMPLE_EMISSIVE
            , emissiveColors[0]
#endif
        );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        mix2.vNt = mikktspace(mix2.vNt, i.vary_tangents[0], i.vary_signs[0], i.vary_normal, is_front_face);
#endif
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.x);
        break;
    default:
        break;
    }
    switch (tm.type & MIX_Y)
    {
    case MIX_Y:
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
        terrain_texcoord[0].xy = i.vary_coords[2].zw;
        terrain_texcoord[0].zw = i.vary_coords[3].xy;
        terrain_texcoord[1].xy = i.vary_coords[3].zw;
        terrain_texcoord[1].zw = i.vary_coords[4].xy;
        terrain_texcoord[2].xy = i.vary_coords[4].zw;
        terrain_texcoord[2].zw = vec2(0);
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
        terrain_texcoord = i.vary_coords[0].zw;
#endif
        mix2 = terrain_sample_and_multiply_pbr(
            terrain_texcoord
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , i.vary_vertex_normal
#endif
            , detail_1_base_color
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_1_metallic_roughness
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_1_normal
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , i.vary_signs[1]
#endif
#endif
#if TERRAIN_SAMPLE_EMISSIVE
            , detail_1_emissive
#endif
            , baseColorFactors[1]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[1]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[1]
#endif
#if TERRAIN_SAMPLE_EMISSIVE
            , emissiveColors[1]
#endif
        );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        mix2.vNt = mikktspace(mix2.vNt, i.vary_tangents[1], i.vary_signs[1], i.vary_normal, is_front_face);
#endif
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.y);
        break;
    default:
        break;
    }
    switch (tm.type & MIX_Z)
    {
    case MIX_Z:
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
        terrain_texcoord[0].xy = i.vary_coords[5].xy;
        terrain_texcoord[0].zw = i.vary_coords[5].zw;
        terrain_texcoord[1].xy = i.vary_coords[6].xy;
        terrain_texcoord[1].zw = i.vary_coords[6].zw;
        terrain_texcoord[2].xy = i.vary_coords[7].xy;
        terrain_texcoord[2].zw = vec2(0);
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
        terrain_texcoord = i.vary_coords[1].xy;
#endif
        mix2 = terrain_sample_and_multiply_pbr(
            terrain_texcoord
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , i.vary_vertex_normal
#endif
            , detail_2_base_color
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_2_metallic_roughness
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_2_normal
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , i.vary_signs[2]
#endif
#endif
#if TERRAIN_SAMPLE_EMISSIVE
            , detail_2_emissive
#endif
            , baseColorFactors[2]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[2]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[2]
#endif
#if TERRAIN_SAMPLE_EMISSIVE
            , emissiveColors[2]
#endif
        );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        mix2.vNt = mikktspace(mix2.vNt, i.vary_tangents[2], i.vary_signs[2], i.vary_normal, is_front_face);
#endif
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.z);
        break;
    default:
        break;
    }
    switch (tm.type & MIX_W)
    {
    case MIX_W:
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
        terrain_texcoord[0].xy = i.vary_coords[7].zw;
        terrain_texcoord[0].zw = i.vary_coords[8].xy;
        terrain_texcoord[1].xy = i.vary_coords[8].zw;
        terrain_texcoord[1].zw = i.vary_coords[9].xy;
        terrain_texcoord[2].xy = i.vary_coords[9].zw;
        terrain_texcoord[2].zw = vec2(0);
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
        terrain_texcoord = i.vary_coords[1].zw;
#endif
        mix2 = terrain_sample_and_multiply_pbr(
            terrain_texcoord
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , i.vary_vertex_normal
#endif
            , detail_3_base_color
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_3_metallic_roughness
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_3_normal
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , i.vary_signs[3]
#endif
#endif
#if TERRAIN_SAMPLE_EMISSIVE
            , detail_3_emissive
#endif
            , baseColorFactors[3]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[3]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[3]
#endif
#if TERRAIN_SAMPLE_EMISSIVE
            , emissiveColors[3]
#endif
        );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        mix2.vNt = mikktspace(mix2.vNt, i.vary_tangents[3], i.vary_signs[3], i.vary_normal, is_front_face);
#endif
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.w);
        break;
    default:
        break;
    }

    float minimum_alpha = terrain_mix(tm, minimum_alphas);
    if (pbr_mix.col.a < minimum_alpha)
    {
        discard;
    }
    float base_color_factor_alpha = terrain_mix(tm, vec4(baseColorFactors[0].z, baseColorFactors[1].z, baseColorFactors[2].z, baseColorFactors[3].z));

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    vec3 tnorm = normalize(pbr_mix.vNt);
#else
    vec3 tnorm = i.vary_normal;
#endif
    tnorm *= is_front_face ? 1.0 : -1.0;


#if TERRAIN_SAMPLE_EMISSIVE
#define mix_emissive pbr_mix.emissive
#else
#define mix_emissive vec3(0)
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
#define mix_orm pbr_mix.orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
#define mix_orm vec3(1.0, pbr_mix.rm)
#else
// Matte plastic potato terrain
#define mix_orm vec3(1.0, 1.0, 0.0)
#endif
    GBufferOut o;
    o.data0 = max(vec4(pbr_mix.col.xyz, 0.0), vec4(0));                                                   // Diffuse
    o.data1 = max(vec4(mix_orm.rgb, base_color_factor_alpha), vec4(0));                                    // PBR linear packed Occlusion, Roughness, Metal.
    o.data2 = encodeNormal(tnorm, 0, GBUFFER_FLAG_HAS_PBR); // normal, flags

#if HAS_EMISSIVE
    o.data3 = max(vec4(mix_emissive,0), vec4(0));                                                // PBR sRGB Emissive
#endif
    return o;
}
