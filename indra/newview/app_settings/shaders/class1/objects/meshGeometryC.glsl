// SPDX-License-Identifier: LGPL-2.1-or-later
#version 430 core
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Source { uint source_words[]; };
layout(std430, binding = 1) buffer Destination { uint destination_words[]; };
uniform uint operation;
uniform mat4 texture_transform;
uniform mat4 bump_rotation;
uniform vec4 uv_scale, uv_transform, uv_offset, bump_s, bump_t, binormal_direction;
uniform uint uv_flags;
uniform mat4 position_transform;
uniform mat4 normal_transform;
// Source is three contiguous planes of equally sized vec4 elements.
uniform uvec4 source_range; // source count, visible count, padded count, texture index
uniform uvec4 destination_range; // position, normal, tangent offsets; write mask
vec4 sourceVertex(uint offset)
{
    offset *= 4u;
    return uintBitsToFloat(uvec4(source_words[offset],source_words[offset+1u],source_words[offset+2u],source_words[offset+3u]));
}
void destinationVertex(uint offset, vec4 value)
{
    offset *= 4u;
    uvec4 bits = floatBitsToUint(value);
    destination_words[offset]=bits.x; destination_words[offset+1u]=bits.y;
    destination_words[offset+2u]=bits.z; destination_words[offset+3u]=bits.w;
}
void main()
{
    uint vertex = gl_GlobalInvocationID.x;
    if (vertex >= source_range.z) return;
    if (operation == 5u) // stable, already ordered draw ranges -> MDI commands
    {
        uint offset = vertex * 5u;
        destination_words[offset] = source_words[vertex * 2u];
        destination_words[offset + 1u] = 1u;
        destination_words[offset + 2u] = source_words[vertex * 2u + 1u];
        destination_words[offset + 3u] = 0u;
        destination_words[offset + 4u] = 0u;
        return;
    }
    if (operation == 1u) // repeat packed color/glow without a vertex upload
    {
        destination_words[destination_range.x+vertex] = source_range.w;
        return;
    }
    if (operation == 2u) // immutable skin weights
    {
        uint source_offset = source_range.x*14u + vertex*4u;
        uint target_offset = destination_range.x + vertex*4u;
        for(uint i=0u;i<4u;++i) destination_words[target_offset+i]=source_words[source_offset+i];
        return;
    }
    if (operation == 4u) // rebase packed U16 indices, preserving adjacent faces
    {
        uint source_index = source_range.x + vertex;
        uint packed_indices = source_words[source_index/2u];
        uint index = ((packed_indices >> ((source_index&1u)*16u)) & 65535u) + source_range.w;
        uint target_index = destination_range.x + vertex;
        uint shift = (target_index&1u)*16u;
        atomicAnd(destination_words[target_index/2u], ~(65535u<<shift));
        atomicOr(destination_words[target_index/2u], (index&65535u)<<shift);
        return;
    }
    if (operation == 3u)
    {
        uint uv_source = source_range.x*12u + vertex*2u;
        vec2 tc = uintBitsToFloat(uvec2(source_words[uv_source],source_words[uv_source+1u]));
        vec3 normal = sourceVertex(source_range.x+vertex).xyz;
        if ((uv_flags & 1u) != 0u)
        {
            vec3 binormal = abs(normal.x)>=0.5 ? vec3(0,normal.x<0?-1:1,0) : vec3(normal.y>0?-1:1,0,0);
            vec3 tangent = cross(binormal,normal);
            vec3 position = sourceVertex(vertex).xyz * uv_scale.xyz;
            tc=vec2(0.5+2.0*dot(binormal,position),0.5-2.0*dot(tangent,position));
        }
        if ((uv_flags & 2u) != 0u) tc=(texture_transform*vec4(tc,0,1)).xy;
        else if ((uv_flags & 4u) != 0u)
        {
            tc-=vec2(0.5);
            tc=vec2(tc.x*uv_transform.x+tc.y*uv_transform.y,
                    -tc.x*uv_transform.y+tc.y*uv_transform.x)*uv_transform.zw+uv_offset.xy+vec2(0.5);
        }
        if ((uv_flags & 8u) != 0u)
        {
            vec4 tangent = sourceVertex(2u*source_range.x+vertex);
            vec3 bitangent = cross(normal,tangent.xyz)*tangent.w;
            vec3 binormal = (normal_transform*vec4(mat3(tangent.xyz,bitangent,normal)*binormal_direction.xyz,0)).xyz;
            if ((uv_flags & 16u) != 0u) binormal=(bump_rotation*vec4(binormal,0)).xyz;
            float length_squared=dot(binormal,binormal);
            binormal=length_squared>0.0 ? binormal*inversesqrt(length_squared) : vec3(0);
            tc+=vec2(dot(bump_s.xyz,tangent.xyz),dot(bump_t.xyz,binormal));
        }
        uint offset=destination_range.x+vertex*2u;
        destination_words[offset]=floatBitsToUint(tc.x);
        destination_words[offset+1u]=floatBitsToUint(tc.y);
        return;
    }
    uint source_vertex = min(vertex, source_range.y - 1u);
    vec3 position = (position_transform * vec4(sourceVertex(source_vertex).xyz, 1.0)).xyz;
    destinationVertex(destination_range.x + vertex, vec4(position, uintBitsToFloat(source_range.w)));
    if ((destination_range.w & 1u) != 0u)
    {
        vec3 normal = (normal_transform * vec4(sourceVertex(source_range.x + source_vertex).xyz, 0.0)).xyz;
        destinationVertex(destination_range.y + vertex, vec4(normal, 0.0));
    }
    if ((destination_range.w & 2u) != 0u)
    {
        vec4 tangent = sourceVertex(2u * source_range.x + source_vertex);
        destinationVertex(destination_range.z + vertex, vec4(
            (normal_transform * vec4(tangent.xyz, 0.0)).xyz, tangent.w));
    }
}
