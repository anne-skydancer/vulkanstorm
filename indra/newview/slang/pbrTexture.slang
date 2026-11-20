// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pbrTexture.slang (converted from class1/deferred/textureUtilV.glsl)
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, Inc.,
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
 * $/LicenseInfo$
 */
module pbrTexture;
import glsl;

// Keep the transform order of the KHR_texture_transform helper.
vec2 khr_texture_transform(vec2 texcoord, vec2 scale, float rotation, vec2 offset)
{
    mat3 scale_mat = mat3(scale.x, 0, 0, 0, scale.y, 0, 0, 0, 1);
    mat3 offset_mat = mat3(1, 0, 0, 0, 1, 0, offset.x, offset.y, 1);
    mat3 rotation_mat = mat3(
        cos(rotation), -sin(rotation), 0,
        sin(rotation), cos(rotation), 0,
        0, 0, 1
    );

    mat3 transform = offset_mat * rotation_mat * scale_mat;
    return (transform * vec3(texcoord, 1)).xy;
}

public vec2 texture_transform(vec2 vertex_texcoord, vec4 khr_gltf_transform[2], mat4 sl_animation_transform)
{
    // SL animations use right-handed UVs. Convert to left-handed UVs for the glTF transform.
    vec2 texcoord = (sl_animation_transform * vec4(vertex_texcoord, 0, 1)).xy;
    texcoord.y = 1.0 - texcoord.y;
    texcoord = khr_texture_transform(texcoord, khr_gltf_transform[0].xy, khr_gltf_transform[0].z, khr_gltf_transform[1].xy);
    texcoord.y = 1.0 - texcoord.y;
    return texcoord;
}

public vec4 tangent_space_transform(vec4 vertex_tangent, vec3 vertex_normal, vec4 khr_gltf_transform[2], mat4 sl_animation_transform)
{
    vec2 weights = vec2(1, 0);

    // Invert the glTF rotation and retain the sign of its scale.
    float khr_rotation = -khr_gltf_transform[0].z;
    mat2 khr_rotation_mat = mat2(
        cos(khr_rotation), -sin(khr_rotation),
        sin(khr_rotation), cos(khr_rotation)
    );
    weights = khr_rotation_mat * weights;
    vec2 khr_scale_sign = sign(khr_gltf_transform[0].xy);
    weights *= khr_scale_sign.xy;

    // Leave the handedness conversion until after applying the SL animation matrix.
    mat2 inv_sl_rot_scale = mat2(
        sl_animation_transform[0][0], sl_animation_transform[0][1],
        sl_animation_transform[1][0], sl_animation_transform[1][1]
    );
    weights = inv_sl_rot_scale * weights;
    weights.x += 1.0 - abs(sign(sign(weights.x) + (0.5 * sign(weights.y))));
    weights = normalize(weights);
    weights.y = -weights.y;

    vec3 vertex_binormal = vertex_tangent.w * cross(vertex_normal, vertex_tangent.xyz);
    float sign_flip = khr_scale_sign.x * khr_scale_sign.y;
    return vec4((weights.x * vertex_tangent.xyz) + (weights.y * vertex_binormal.xyz), vertex_tangent.w * sign_flip);
}
