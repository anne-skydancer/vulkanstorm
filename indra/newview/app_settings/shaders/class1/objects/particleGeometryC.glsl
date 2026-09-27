// SPDX-License-Identifier: LGPL-2.1-or-later
#version 430 core
layout(local_size_x = 64) in;

// Matches ParticleInput in llparticlecompute.cpp. Offsets are uint words in the
// existing planar LLVertexBuffer, not interleaved vertices.
struct Particle
{
    vec4 centerScaleX;
    vec4 axisVelocityScaleY;
    vec4 parentCenterScale;
    vec4 parentAxisFlags;
    uvec4 appearance; // color, glow, parent color, parent glow
};
layout(std430, binding = 0) readonly buffer Inputs { Particle particles[]; };
layout(std430, binding = 1) writeonly buffer Vertices { uint vertices[]; };
struct Group
{
    uvec4 offsets; // position, normal, color, emissive in scratch uint words
    uvec4 firstUV; // first particle index, texture-coordinate offset, reserved
    vec4 cameraOrigin;
    vec4 particleNormal;
};
layout(std430, binding = 2) readonly buffer Groups { Group groups[]; };
uniform uint particleCount;

void storeVector(uint offset, vec3 value)
{
    vertices[offset] = floatBitsToUint(value.x);
    vertices[offset + 1u] = floatBitsToUint(value.y);
    vertices[offset + 2u] = floatBitsToUint(value.z);
    vertices[offset + 3u] = 0u; // position.w is texture index, not homogeneous w
}

// Degenerate camera/velocity directions must not produce NaN vertices. Keep
// the ordinary billboard orientation when velocity has no screen projection.
vec3 unitOr(vec3 v, vec3 fallback)
{
    float scale = max(max(abs(v.x), abs(v.y)), abs(v.z));
    if (!(scale > 0.0) || isinf(scale)) return fallback;
    v /= scale;
    return v * inversesqrt(dot(v, v));
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= particleCount) return;
    Particle p = particles[i];
    uint flags = floatBitsToUint(p.parentAxisFlags.w);
    Group group = groups[flags >> 3u];
    uvec4 offsets = group.offsets;
    bool ribbon = (flags & 1u) != 0u;
    vec3 right = vec3(0.0), up = vec3(0.0);
    if (!ribbon)
    {
        vec3 camera = (flags & 4u) != 0u ? vec3(-1.0, 0.0, 0.0) : group.cameraOrigin.xyz;
        vec3 at = unitOr(p.centerScaleX.xyz - camera, vec3(0.0, 1.0, 0.0));
        right = unitOr(cross(at, vec3(0.0, 0.0, 1.0)), vec3(1.0, 0.0, 0.0));
        up = unitOr(cross(right, at), vec3(0.0, 0.0, 1.0));
        if ((flags & 2u) != 0u)
        {
            vec3 velocity = unitOr(p.axisVelocityScaleY.xyz, vec3(0.0));
            vec2 projection = vec2(dot(velocity, right), dot(velocity, up));
            if (dot(projection, projection) > 1e-12)
            {
                projection = normalize(projection);
                vec3 newUp = unitOr(projection.x * right + projection.y * up, up);
                right = unitOr(projection.y * right - projection.x * up, right);
                up = newUp;
            }
        }
        right *= 0.5 * p.centerScaleX.w;
        up *= 0.5 * p.axisVelocityScaleY.w;
    }
    for (uint corner = 0u; corner < 4u; ++corner)
    {
        uint vertex = (i - group.firstUV.x) * 4u + corner;
        // Preserve the CPU quad's corner/index/UV convention.
        vec3 center = p.centerScaleX.xyz + ((corner & 1u) == 0u ? up : -up);
        vec3 position = center + (corner < 2u ? -right : right);
        bool parent = ribbon && corner < 2u;
        if (ribbon)
        {
            vec3 axis = parent ? p.parentAxisFlags.xyz : p.axisVelocityScaleY.xyz;
            vec4 endpoint = parent ? p.parentCenterScale : p.centerScaleX;
            position = endpoint.xyz + ((corner & 1u) == 0u ? 0.5 : -0.5) * endpoint.w * axis;
        }
        storeVector(offsets.x + vertex * 4u, position);
        storeVector(offsets.y + vertex * 4u, group.particleNormal.xyz);
        // A complete GPU copy includes UVs, so scratch padding is never used
        // as a live attribute. Indices remain in the destination's index buffer.
        vertices[group.firstUV.y + vertex * 2u] = floatBitsToUint(corner < 2u ? 0.0 : 1.0);
        vertices[group.firstUV.y + vertex * 2u + 1u] = floatBitsToUint((corner & 1u) == 0u ? 1.0 : 0.0);
        vertices[offsets.z + vertex] = parent ? p.appearance.z : p.appearance.x;
        vertices[offsets.w + vertex] = parent ? p.appearance.w : p.appearance.y;
    }
}
