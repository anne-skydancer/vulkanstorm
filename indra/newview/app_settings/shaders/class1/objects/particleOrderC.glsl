#version 430 core
// SPDX-License-Identifier: LGPL-2.1-or-later
// Generate independent spatial and depth index streams. No material reordering.
// Same Particle layout as particleSimulationC.glsl; verified by the GL test.
layout(local_size_x = 64) in;
struct Particle
{
    vec4 positionAge, velocityLife, accelerationParameter, offsetStartGlow;
    vec4 startColor, endColor, color, scales, scaleGlow;
    uvec4 identity, sourceParent;
    vec4 axis;
};
struct Entry { uvec4 key; uvec4 value; };
layout(std430, binding=0) readonly buffer Particles { Particle particles[]; };
layout(std430, binding=1) writeonly buffer Spatial { Entry spatial[]; };
layout(std430, binding=2) writeonly buffer Depth { Entry depth[]; };
uniform uint particleCount;
uniform uint paddedCount;
uniform vec3 cameraPosition;
uniform vec3 cameraForward;
uniform float cellSize; // strictly positive; bins are not legacy LLSpatialGroups
uint orderedFloat(float v)
{
    if (v == 0.0) v = 0.0; // normalize signed zero for deterministic ties
    uint bits = floatBitsToUint(v);
    return (bits & 0x80000000u) != 0u ? ~bits : (bits ^ 0x80000000u);
}
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= paddedCount) return;
    Entry e;
    e.key = uvec4(0xffffffffu);
    e.value = uvec4(0xffffffffu);
    spatial[i] = e;
    depth[i] = e;
    if (i >= particleCount) return;
    Particle p = particles[i];
    if (p.identity.y == 0u || any(isnan(p.positionAge.xyz)) ||
        any(isinf(p.positionAge.xyz)) || !(cellSize > 0.0)) return;
    uint domain = (p.identity.z & 0x40000000u) != 0u ? 1u : 0u;
    vec3 bins = floor(p.positionAge.xyz/cellSize);
    // Reject unrepresentable cells instead of overflowing signed conversion.
    if (any(greaterThan(abs(bins), vec3(2147483520.0)))) return;
    ivec3 cell = ivec3(bins);
    e.key = uvec4(domain, uvec3(cell) ^ uvec3(0x80000000u));
    e.value = uvec4(i, p.identity.w, p.identity.x, domain);
    spatial[i] = e;
    float distance = dot(p.positionAge.xyz-cameraPosition, cameraForward);
    if (isnan(distance) || isinf(distance)) return;
    e.key = uvec4(domain, ~orderedFloat(distance), i, 0u);
    depth[i] = e;
}
