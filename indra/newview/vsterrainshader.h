// Native terrain material adapter around the shared reference sampler. LGPL-2.1.
#pragma once
#include <string>
namespace VSTerrainShader {
inline std::string source(int detail) { return "#define TERRAIN_PBR_DETAIL "+std::to_string(detail)+"\n" + std::string(R"VS_TERRAIN(
layout(location=0) in vec2 texUV;
layout(location=1) in vec3 light;
layout(location=2) in vec3 worldNormal;
layout(location=3) in vec3 worldPosition;
layout(location=4) in vec2 texUV1;
layout(location=5) in vec4 worldTangent;
layout(location=6) in vec4 faceColor;
layout(location=0) out vec4 result;
layout(std140) uniform WorldView { mat4 projection; vec4 sun; vec4 ambient; vec4 tint; vec4 material; vec4 regionOffset; vec4 surfaceFlags; vec4 surfaceParams; vec4 emissive; vec4 specular; vec4 channelSets; vec4 normalOcclusion; vec4 normalTangent; vec4 uvTransform[10]; };
layout(std140) uniform TerrainView {
 vec4 terrainBase[4]; vec4 terrainFactors[4]; vec4 terrainEmission[4];
 vec4 terrainTransform[8]; vec4 terrainTangent[4]; vec4 terrainSettings; vec4 terrainPaint;
};
uniform texture2D terrainColor[4];
uniform texture2D terrainNormal[4];
uniform texture2D terrainORM[4];
uniform texture2D terrainEmissive[4];
uniform texture2D terrainBlend;
uniform sampler terrainSampler;
uniform sampler terrainClamp;
#define TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT 3
#define TERRAIN_TRIPLANAR_BLEND_FACTOR terrainSettings.z
vec3 srgb_to_linear(vec3 c) {return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));}
vec3 linear_to_srgb(vec3 c) {return mix(c*12.92,1.055*pow(max(c,vec3(0)),vec3(1.0/2.4))-.055,step(vec3(.0031308),c));}
/**
 * @file class1\deferred\pbrterrainUtilF.glsl
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

/*[EXTRA_CODE_HERE]*/

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

#define TERRAIN_PBR_DETAIL_EMISSIVE 0
#define TERRAIN_PBR_DETAIL_OCCLUSION -1
#define TERRAIN_PBR_DETAIL_NORMAL -2
#define TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS -3

#define TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE 0
#define TERRAIN_PAINT_TYPE_PBR_PAINTMAP 1

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
#define vary_vertex_normal worldNormal
#endif

vec3 srgb_to_linear(vec3 c);

// A relatively agressive threshold for terrain material mixing sampling
// cutoff. This ensures that only one or two materials are used in most places,
// making PBR terrain blending more performant. Should be greater than 0 to work.
#define TERRAIN_RAMP_MIX_THRESHOLD 0.1
// A small threshold for triplanar mapping sampling cutoff. This and
// TERRAIN_TRIPLANAR_BLEND_FACTOR together ensures that only one or two samples
// per texture are used in most places, making triplanar mapping more
// performant. Should be greater than 0 to work.
// There's also an artistic design choice in the use of these factors, and the
// use of triplanar generally. Don't take these triplanar constants for granted.
#define TERRAIN_TRIPLANAR_MIX_THRESHOLD 0.01

#define SAMPLE_X 1 << 0
#define SAMPLE_Y 1 << 1
#define SAMPLE_Z 1 << 2
#define MIX_X    1 << 3
#define MIX_Y    1 << 4
#define MIX_Z    1 << 5
#define MIX_W    1 << 6

struct PBRMix
{
    vec4 col;       // RGB color with alpha, linear space
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    vec3 orm;       // Occlusion, roughness, metallic
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    vec2 rm;        // Roughness, metallic
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    vec3 vNt;       // Unpacked normal texture sample, vector
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    vec3 emissive;  // RGB emissive color, linear space
#endif
};

PBRMix init_pbr_mix()
{
    PBRMix mix;
    mix.col = vec4(0);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm = vec3(0);
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm = vec2(0);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    mix.vNt = vec3(0);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive = vec3(0);
#endif
    return mix;
}

// Usage example, for two weights:
// vec2 weights = ... // Weights must add up to 1
// PBRMix mix = init_pbr_mix();
// PBRMix mix1 = ...
// mix = mix_pbr(mix, mix1, weights.x);
// PBRMix mix2 = ...
// mix = mix_pbr(mix, mix2, weights.y);
PBRMix mix_pbr(PBRMix mix1, PBRMix mix2, float mix2_weight)
{
    PBRMix mix;
    mix.col      = mix1.col      + (mix2.col      * mix2_weight);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm      = mix1.orm      + (mix2.orm      * mix2_weight);
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm       = mix1.rm       + (mix2.rm       * mix2_weight);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    mix.vNt      = mix1.vNt      + (mix2.vNt      * mix2_weight);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive = mix1.emissive + (mix2.emissive * mix2_weight);
#endif
    return mix;
}

PBRMix sample_pbr(
    vec2 uv
    , texture2D tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , texture2D tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    , texture2D tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , texture2D tex_emissive
#endif
    )
{
    PBRMix mix;
    mix.col = texture(sampler2D(tex_col,terrainSampler), uv);
    mix.col.rgb = srgb_to_linear(mix.col.rgb);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm = texture(sampler2D(tex_orm,terrainSampler), uv).xyz;
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm = texture(sampler2D(tex_orm,terrainSampler), uv).yz;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    mix.vNt = texture(sampler2D(tex_vNt,terrainSampler), uv).xyz*2.0-1.0;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive = srgb_to_linear(texture(sampler2D(tex_emissive,terrainSampler), uv).xyz);
#endif
    return mix;
}

struct TerrainTriplanar
{
    vec3 weight;
    int type;
};

struct TerrainMix
{
    vec4 weight;
    int type;
};

#define TerrainMixSample vec4[4]
#define TerrainMixSample)VS_TERRAIN") + std::string(R"VS_TERRAIN(3 vec3[4]

TerrainMix get_terrain_mix_weights(float alpha1, float alpha2, float alphaFinal)
{
    TerrainMix tm;
    vec4 sample_x = vec4(1,0,0,0);
    vec4 sample_y = vec4(0,1,0,0);
    vec4 sample_z = vec4(0,0,1,0);
    vec4 sample_w = vec4(0,0,0,1);

    tm.weight = mix( mix(sample_w, sample_z, alpha2), mix(sample_y, sample_x, alpha1), alphaFinal );
    tm.weight -= TERRAIN_RAMP_MIX_THRESHOLD;
    ivec4 usage = max(ivec4(0), ivec4(ceil(tm.weight)));
    // Prevent negative weights and keep weights balanced
    tm.weight = tm.weight*vec4(usage);
    tm.weight /= (tm.weight.x + tm.weight.y + tm.weight.z + tm.weight.w);

    tm.type = (usage.x * MIX_X) |
              (usage.y * MIX_Y) |
              (usage.z * MIX_Z) |
              (usage.w * MIX_W);
    return tm;
}

// A paintmap weight applier for 4 swatches. The input saves a channel by not
// storing swatch 1, and assuming the weights of the 4 swatches add to 1.
// The components of weight3 should be between 0 and 1
// The sum of the components of weight3 should be between 0 and 1
TerrainMix get_terrain_usage_from_weight3(vec3 weight3)
{
    // These steps ensure the output weights add to between 0 and 1
    weight3.xyz = max(vec3(0.0), weight3.xyz);
    weight3.xyz /= max(1.0, weight3.x + weight3.y + weight3.z);

    TerrainMix tm;

    // Extract the first weight from the other weights
    tm.weight.x = 1.0 - (weight3.x + weight3.y + weight3.z);
    tm.weight.yzw = weight3.xyz;
    ivec4 usage = max(ivec4(0), ivec4(ceil(tm.weight)));

    tm.type = (usage.x * MIX_X) |
              (usage.y * MIX_Y) |
              (usage.z * MIX_Z) |
              (usage.w * MIX_W);
    return tm;
}

// Inverse of get_terrain_usage_from_weight3, excluding usage flags
// The components of weight should be between 0 and 1
// The sum of the components of weight should be 1
vec3 get_weight3_from_terrain_weight(vec4 weight)
{
    // These steps ensure the input weights add to 1
    weight = max(vec4(0.0), weight);
    weight.x += 1.0 - sign(weight.x + weight.y + weight.z + weight.w);
    weight /= weight.x + weight.y + weight.z + weight.w;

    // Then return the input weights with the first weight truncated
    return weight.yzw;
}

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
TerrainTriplanar _t_triplanar()
{
    float sharpness = TERRAIN_TRIPLANAR_BLEND_FACTOR;
    float threshold = TERRAIN_TRIPLANAR_MIX_THRESHOLD;
    vec3 weight_signed = pow(abs(vary_vertex_normal), vec3(sharpness));
    weight_signed /= (weight_signed.x + weight_signed.y + weight_signed.z);
    weight_signed -= vec3(threshold);
    TerrainTriplanar tw;
    // *NOTE: Make sure the threshold doesn't affect the materials
    tw.weight = max(vec3(0), weight_signed);
    tw.weight /= (tw.weight.x + tw.weight.y + tw.weight.z);
    ivec3 usage = ivec3(round(max(vec3(0), sign(weight_signed))));
    tw.type = ((usage.x) * SAMPLE_X) |
              ((usage.y) * SAMPLE_Y) |
              ((usage.z) * SAMPLE_Z);
    return tw;
}
#endif


// Assume weights add to 1
float terrain_mix(TerrainMix tm, vec4 tms4)
{
    return (tm.weight.x * tms4[0]) +
           (tm.weight.y * tms4[1]) +
           (tm.weight.z * tms4[2]) +
           (tm.weight.w * tms4[3]);
}

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
// Triplanar mapping

// Pre-transformed texture coordinates for each axial uv slice (Packing: xy, yz, (-x)z, unused)
#define TerrainCoord vec4[3]

// If sign_or_zero is positive, use uv_unflippped, otherwise use uv_flipped
vec2 _t_uv(vec2 uv_unflipped, vec2 uv_flipped, float sign_or_zero)
{
    return mix(uv_flipped, uv_unflipped, max(0.0, sign_or_zero));
}

vec3 _t_normal_post_1(vec3 vNt0, float sign_or_zero)
{
    // Assume normal is unpacked
    vec3 vNt1 = vNt0;
    // Get sign
    float sign = sign_or_zero;
    // Handle case where sign is 0
    sign = (2.0*sign) + 1.0;
    sign /= abs(sign);
    // If the sign is negative, rotate normal by 180 degrees
    vNt1.xy = (min(0, sign) * vNt1.xy) + (min(0, -sign) * -vNt1.xy);
    return vNt1;
}

// Triplanar-specific normal texture fixes
vec3 _t_normal_post_x(vec3 vNt0, float tangent_sign)
{
    vec3 vNt_x = _t_normal_post_1(vNt0, sign(vary_vertex_normal.x));
    // *HACK: Transform normals according to orientation of the UVs
    vNt_x.xy = vec2(-vNt_x.y, vNt_x.x);
    vNt_x.xy *= tangent_sign;
    return vNt_x;
}
vec3 _t_normal_post_y(vec3 vNt0)
{
    vec3 vNt_y = _t_normal_post_1(vNt0, sign(vary_vertex_normal.y));
    // *HACK: Transform normals according to orientation of the UVs
    vNt_y.xy = -vNt_y.xy;
    return vNt_y;
}
vec3 _t_normal_post_z(vec3 vNt0)
{
    vec3 vNt_z = _t_normal_post_1(vNt0, sign(vary_vertex_normal.z));
    return vNt_z;
}

PBRMix terrain_sample_pbr(
    TerrainCoord terrain_coord
    , TerrainTriplanar tw
    , texture2D tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , texture2D tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    , texture2D tex_vNt
    , float tangent_sign
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , texture2D tex_emissive
#endif
    )
{
    PBRMix mix = init_pbr_mix();

#define get_uv_x() _t_uv(terrain_coord[0].zw, terrain_coord[1].zw, sign(vary_vertex_normal.x))
#define get_uv_y() _t_uv(terrain_coord[1].xy, terrain_coord[2].xy, sign(vary_vertex_normal.y))
#define get_uv_z() _t_uv(terrain_coord[0].xy, vec2(0),             sign(vary_vertex_normal.z))
    switch (tw.type & SAMPLE_X)
    {
    case SAMPLE_X:
        PBRMix mix_x = sample_pbr(
            get_uv_x()
            , tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , tex_emissive
#endif
            );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        // Triplanar-specific normal texture fix
        mix_x.vNt = _t_normal_post_x(mix_x.vNt, tangent_sign);
#endif
        mix = mix_pbr(mix, mix_x, tw.weight.x);
        break;
    default:
        break;
    }

    switch (tw.type & SAMPLE_Y)
    {
    case SAMPLE_Y:
        PBRMix mix_y = sample_pbr(
            get_uv_y()
            , tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , tex_emissive
#endif
            );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        // Triplanar-specific normal texture fix
        mix_y.vNt = _t_normal_post_y(mix_y.vNt);
#endif
        mix = mix_pbr(mix, mix_y, tw.weight.y);
        break;
    default:
        break;
    }

    switch (tw.type & SAMPLE_Z)
    {
    case SAMPLE_Z:
        PBRMix mix_z = sample_pbr(
            get_uv_z()
            , tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , tex_emissive
#endif
            );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        // Triplanar-specific normal texture fix
        // *NOTE: Bottom face has not been tested
        mix_z.vNt = _t_normal_post_z(mix_z.vNt);
#endif
        mix = mix_pbr(mix, mix_z, tw.weight.z);
        break;
    default:
        break;
    }

    return mix;
}

#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1

#define TerrainCoord vec2

#define terrain_sample_pbr sample_pbr

#endif

PBRMix multiply_factors_pbr(
    PBRMix mix_in
    , vec4 factor_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    , vec3 factor_orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , vec2 factor_rm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , vec3 factor_emissive
#endif
    )
{
    )VS_TERRAIN") + std::string(R"VS_TERRAIN(PBRMix mix = mix_in;
    mix.col *= factor_col;
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm *= factor_orm;
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm *= factor_rm;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive *= factor_emissive;
#endif
    return mix;
}

PBRMix terrain_sample_and_multiply_pbr(
    TerrainCoord terrain_coord
    , texture2D tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , texture2D tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    , texture2D tex_vNt
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    , float tangent_sign
#endif
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , texture2D tex_emissive
#endif
    , vec4 factor_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    , vec3 factor_orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , vec2 factor_rm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , vec3 factor_emissive
#endif
    )
{
    PBRMix mix = terrain_sample_pbr(
        terrain_coord
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
        , _t_triplanar()
#endif
        , tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        , tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        , tex_vNt
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
        , tangent_sign
#endif
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
        , tex_emissive
#endif
        );

    mix = multiply_factors_pbr(mix
        , factor_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
        , factor_orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        , factor_rm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
        , factor_emissive
#endif
    );

    return mix;
}

vec2 terrainUV(int layer,vec2 p) {
 vec4 m=terrainTransform[layer*2]; vec2 o=terrainTransform[layer*2+1].xy;
 vec2 uv=vec2(m.x*p.x+m.z*p.y,m.y*p.x+m.w*p.y)+o;
 return vec2(uv.x,1.0-uv.y);
}
PBRMix terrainLayer(int layer,texture2D colorTex,texture2D normalTex,texture2D ormTex,texture2D emissiveTex) {
 TerrainCoord coord;
 vec3 position=worldPosition-regionOffset.xyz;
 coord[0]=vec4(terrainUV(layer,position.xy),terrainUV(layer,position.yz));
 coord[1]=vec4(terrainUV(layer,position.xz*vec2(-1,1)),terrainUV(layer,position.yz*vec2(-1,1)));
 coord[2]=vec4(terrainUV(layer,position.xz),0,0);
 TerrainTriplanar tw=_t_triplanar();
 if(terrainSettings.y<2) {tw.weight=vec3(0,0,1);tw.type=SAMPLE_Z;}
 PBRMix sampled=terrain_sample_pbr(coord,tw,colorTex,ormTex
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL
 ,normalTex,terrainTangent[layer].z
#endif
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE
 ,emissiveTex
#endif
 );
 sampled=multiply_factors_pbr(sampled,terrainBase[layer]
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION
 ,terrainFactors[layer].xyz
#else
 ,terrainFactors[layer].yz
#endif
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE
 ,terrainEmission[layer].rgb
#endif
 );
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL
 vec3 n=normalize(worldNormal);
 vec3 t=abs(n.z)>.00001?normalize(vec3(1,0,-n.x/n.z)):normalize(cross(vec3(0,1,0),n));
 vec3 b=cross(n,t);
 vec4 rotation=terrainTangent[layer];
 vec3 tangent=normalize(rotation.x*t+rotation.y*b);
 sampled.vNt=normalize(mat3(tangent,rotation.z*cross(n,tangent),n)*sampled.vNt);
#endif
 return sampled;
}
void main() {
 vec4 weights;
 if(terrainPaint.x>.5) {
  vec2 uv=vec2(texUV.x,1.0-texUV.y)/terrainPaint.y;
  uv.y=1.0-uv.y;
  weights=get_terrain_usage_from_weight3(texture(sampler2D(terrainBlend,terrainClamp),uv).rgb).weight;
 } else {
  float a1=texture(sampler2D(terrainBlend,terrainClamp),vec2(texUV1.x,1.0-texUV1.y)).a;
  float a2=texture(sampler2D(terrainBlend,terrainClamp),vec2(texUV1.x-2,1.0-texUV1.y)).a;
  float af=texture(sampler2D(terrainBlend,terrainClamp),vec2(texUV1.x-1,1.0-texUV1.y)).a;
  weights=get_terrain_mix_weights(a1,a2,af).weight;
 }
 PBRMix mixed=init_pbr_mix();
#define LAYER(I) if(weights[I]>0) mixed=mix_pbr(mixed,terrainLayer(I,terrainColor[I],terrainNormal[I],terrainORM[I],terrainEmissive[I]),weights[I]);
 LAYER(0) LAYER(1) LAYER(2) LAYER(3)
#undef LAYER
 float cutoff=dot(weights,vec4(terrainFactors[0].w,terrainFactors[1].w,terrainFactors[2].w,terrainFactors[3].w));
 if(mixed.col.a<cutoff) discard;
 vec3 n=normalize(
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL
 mixed.vNt
#else
 worldNormal
#endif
 )*(gl_FrontFacing?1.0:-1.0);
 vec3 illumination=material.x>.5?vec3(1):ambient.rgb+(1-ambient.rgb)*max(dot(n,normalize(sun.xyz)),0);
 // As in the object foundation, retain roughness/metallic inputs for R4/R5's
 // G-buffer/BRDF. Do not invent a replacement lighting model at R3.
 vec3 color=mixed.col.rgb*illumination;
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION
 color*=mixed.orm.x;
#endif
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE
 color+=mixed.emissive;
#endif
 result=vec4(linear_to_srgb(color),1);
}
)VS_TERRAIN"); }
}
