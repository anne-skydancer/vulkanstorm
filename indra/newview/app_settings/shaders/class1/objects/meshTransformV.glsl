// SPDX-License-Identifier: LGPL-2.1-or-later
// Seven RGBA32F texels per resident slot: object-to-buffer matrix followed by
// three padded columns of its inverse-transpose normal matrix.
uniform samplerBuffer mesh_transforms;
uniform bool mesh_transform_enabled;
in uint texture_index;

vec3 residentMeshPosition(vec3 position)
{
    if (!mesh_transform_enabled) return position;
    int offset = int(texture_index) * 7;
    mat4 transform = mat4(texelFetch(mesh_transforms, offset),
                          texelFetch(mesh_transforms, offset+1),
                          texelFetch(mesh_transforms, offset+2),
                          texelFetch(mesh_transforms, offset+3));
    return (transform * vec4(position, 1.0)).xyz;
}

vec3 residentMeshNormal(vec3 normal)
{
    if (!mesh_transform_enabled) return normal;
    int offset = int(texture_index) * 7 + 4;
    mat3 transform = mat3(texelFetch(mesh_transforms, offset).xyz,
                          texelFetch(mesh_transforms, offset+1).xyz,
                          texelFetch(mesh_transforms, offset+2).xyz);
    return transform * normal;
}
