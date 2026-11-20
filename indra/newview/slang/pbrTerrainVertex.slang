/**
 * @file pbrTerrainVertex.slang (converted from deferred/pbrterrainV.glsl
 *       and the terrain transforms in deferred/textureUtilV.glsl)
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

uniform mat3 normal_matrix;
uniform mat4 modelview_matrix;
uniform mat4 modelview_projection_matrix;
uniform vec4 terrain_texture_transforms[5];
#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_PBR_PAINTMAP
uniform float region_scale;
#endif

struct VertexIn
{
    [[vk::location(0)]] vec3 position : POSITION;
    [[vk::location(1)]] vec3 normal : NORMAL;
#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    [[vk::location(3)]] vec2 texcoord1 : TEXCOORD1;
#endif
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL
    [[vk::location(8)]] vec4 tangent : TANGENT;
#endif
};

struct Varyings
{
    vec4 clip : SV_Position;
    [[vk::location(0)]] vec3 vary_position : VARY_POSITION;
    [[vk::location(1)]] vec3 vary_normal : VARY_NORMAL;
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    [[vk::location(2)]] vec3 vary_vertex_normal : VARY_VERTEX_NORMAL;
#endif
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL
    [[vk::location(3)]] vec3 vary_tangents[4] : VARY_TANGENTS;
    [[vk::location(7)]] nointerpolation vec4 vary_signs : VARY_SIGNS;
#endif
#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    [[vk::location(8)]] vec4 vary_texcoord0 : VARY_TEXCOORD0;
    [[vk::location(9)]] vec4 vary_texcoord1 : VARY_TEXCOORD1;
#else
    [[vk::location(8)]] vec2 vary_texcoord : VARY_TEXCOORD;
#endif
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    [[vk::location(10)]] vec4 vary_coords[10] : VARY_COORDS;
#else
    [[vk::location(10)]] vec4 vary_coords[2] : VARY_COORDS;
#endif
};

struct TerrainTextureTransform
{
    vec2 scale;
    float rotation;
    vec2 offset;
};

vec2 terrainTextureTransform(vec2 uv, TerrainTextureTransform transform)
{
    // Terrain coordinates reverse Y without a unit offset or an animation matrix.
    vec2 transformed = khr_texture_transform(vec2(uv.x, -uv.y), transform.scale, transform.rotation, transform.offset);
    return vec2(transformed.x, -transformed.y);
}

#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL
vec4 terrainTangentSpaceTransform(vec4 tangent, vec3 normal, TerrainTextureTransform transform)
{
    float angle = -transform.rotation;
    mat2 inverse_rotation = mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
    vec2 scale_sign = sign(transform.scale);
    vec2 weights = (inverse_rotation * vec2(1, 0)) * scale_sign;
    weights.x += 1.0 - abs(sign(sign(weights.x) + (0.5 * sign(weights.y))));
    weights.y = -weights.y;

    vec3 binormal = tangent.w * cross(normal, tangent.xyz);
    float sign_flip = scale_sign.x * scale_sign.y;
    return vec4((weights.x * tangent.xyz) + (weights.y * binormal), tangent.w * sign_flip);
}
#endif

[shader("vertex")]
Varyings vsMain(VertexIn v)
{
    Varyings o;
    o.clip = vkPositionClip(modelview_projection_matrix, v.position);
    o.vary_position = (modelview_matrix * vec4(v.position, 1.0)).xyz;
    vec3 normal = normal_matrix * v.normal;
    o.vary_normal = normalize(normal);
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    o.vary_vertex_normal = v.normal;
#endif

    // The viewer packs scale.xy, rotation and offset.xy for each of its four materials.
    TerrainTextureTransform transforms[4] = {
        {terrain_texture_transforms[0].xy, terrain_texture_transforms[0].z,
            vec2(terrain_texture_transforms[0].w, terrain_texture_transforms[1].x)},
        {terrain_texture_transforms[1].yz, terrain_texture_transforms[1].w, terrain_texture_transforms[2].xy},
        {terrain_texture_transforms[2].zw, terrain_texture_transforms[3].x, terrain_texture_transforms[3].yz},
        {vec2(terrain_texture_transforms[3].w, terrain_texture_transforms[4].x),
            terrain_texture_transforms[4].y, terrain_texture_transforms[4].zw},
    };

#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL
    vec4 tangent = vec4(normal_matrix * v.tangent.xyz, v.tangent.w);
    for (int material = 0; material < 4; ++material)
    {
        vec4 transformed = terrainTangentSpaceTransform(tangent, normal, transforms[material]);
        o.vary_tangents[material] = normalize(transformed.xyz);
        o.vary_signs[material] = transformed.w;
    }
#endif

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    // Interleave each material's five UV pairs in the legacy triplanar order.
    vec2 coordinates[20];
    for (int material = 0; material < 4; ++material)
    {
        TerrainTextureTransform transform = transforms[material];
        int first = material * 5;
        coordinates[first] = terrainTextureTransform(v.position.xy, transform);
        coordinates[first + 1] = terrainTextureTransform(v.position.yz, transform);
        coordinates[first + 2] = terrainTextureTransform(v.position.xz * vec2(-1, 1), transform);
        coordinates[first + 3] = terrainTextureTransform(v.position.yz * vec2(-1, 1), transform);
        coordinates[first + 4] = terrainTextureTransform(v.position.xz, transform);
    }
    for (int pair = 0; pair < 10; ++pair)
    {
        o.vary_coords[pair] = vec4(coordinates[pair * 2], coordinates[pair * 2 + 1]);
    }
#else
    o.vary_coords[0] = vec4(terrainTextureTransform(v.position.xy, transforms[0]),
                           terrainTextureTransform(v.position.xy, transforms[1]));
    o.vary_coords[1] = vec4(terrainTextureTransform(v.position.xy, transforms[2]),
                           terrainTextureTransform(v.position.xy, transforms[3]));
#endif

#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    vec2 tc = v.texcoord1;
    o.vary_texcoord0 = vec4(0.0, 0.0, tc);
    o.vary_texcoord1 = vec4(tc - vec2(2.0, 0.0), tc - vec2(1.0, 0.0));
#else
    o.vary_texcoord = v.position.xy / region_scale;
#endif
    return o;
}
