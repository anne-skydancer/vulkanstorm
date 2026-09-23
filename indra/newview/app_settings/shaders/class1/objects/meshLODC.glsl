// SPDX-License-Identifier: LGPL-2.1-or-later
#version 430 core
layout(local_size_x = 64) in;
// Mesh repository order: lowest, low, medium, high. Each range already resolves
// absent asset levels using LLMeshRepository::getActualMeshLOD.
struct Mesh { vec4 centerRadius; uvec4 ranges[4]; uvec4 owner; };
struct Avatar { vec4 center; vec4 minimum; vec4 maximum; };
struct Command { uint count; uint instanceCount; uint firstIndex; int baseVertex; uint baseInstance; };
layout(std430, binding=0) readonly buffer Meshes { Mesh meshes[]; };
layout(std430, binding=1) writeonly buffer Commands { Command commands[]; };
layout(std430, binding=2) readonly buffer Avatars { Avatar avatars[]; };
layout(std430, binding=3) buffer AvatarDecisions { uint avatarLOD[]; };
layout(std430, binding=4) writeonly buffer StreamingDemand { uint desiredLOD[]; };
uniform uint phase; // 0: one decision per avatar; 1: mesh draw commands
uniform uint avatarCount;
uniform uint candidateCount;
uniform vec3 cameraOrigin;
// distance scale, close-distance ramp, LOD factor (including FOV), dynamic flag
uniform vec4 policy;
uint selectLOD(vec4 centerRadius)
{
    uint lod = 3u; // fail open to high for invalid policy/coordinates
    precise float radius = floor(centerRadius.w * 100.0 + 0.5) * 0.01;
    precise float distance = length(centerRadius.xyz - cameraOrigin);
    distance = floor(distance * 100.0 + 0.5) * 0.01;
    distance *= policy.x;
    if (distance < policy.y && policy.y > 0.0)
    {
        distance *= 1.0 / policy.y;
        distance *= distance;
        distance *= policy.y;
    }
    distance *= 1.0471975511965976;
    distance = floor(distance * 100.0 + 0.5) * 0.01;
    if (!any(isnan(centerRadius)) && !any(isinf(centerRadius)) &&
        !isnan(radius) && !isinf(radius) && radius >= 0.0 &&
        !any(isnan(policy)) && !any(isinf(policy)) && policy.z >= 0.0)
    {
        if (policy.w != 0.0)
        {
            precise float product = policy.z * radius;
            precise float angle = product / distance;
            precise float rounded = floor(angle * 100.0 + 0.5) * 0.01;
            if (!isnan(rounded))
                lod = rounded <= 0.03 ? 0u : rounded <= 0.06 ? 1u : rounded <= 0.24 ? 2u : 3u;
        }
        else
        {
            precise float scaled = sqrt(radius) * policy.z;
            precise float level = scaled * 4.0;
            lod = uint(clamp(level, 0.0, 3.0));
        }
    }
    return lod;
}
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (phase == 0u)
    {
        if (i >= avatarCount) return;
        Avatar avatar = avatars[i];
        // Match calcLOD: full animated diagonal for ordinary avatars and half
        // diagonal for control avatars. All attachments share this GPU result.
        precise float radius = length(avatar.maximum.xyz - avatar.minimum.xyz) * avatar.center.w;
        // Animated bounds can be temporarily empty while attachments settle.
        // CPU calcLOD rejects zero radius; do not collapse rigged meshes to
        // their often empty Lowest range during that transient state.
        avatarLOD[i] = avatar.minimum.w != 0.0 && radius > 0.0 ? selectLOD(vec4(avatar.center.xyz, radius)) : 3u;
        return;
    }
    if (i >= candidateCount) return;
    Mesh mesh = meshes[i];
    uint lod = mesh.owner.x == 0u ? selectLOD(mesh.centerRadius) :
        (mesh.owner.x <= avatarCount ? min(avatarLOD[mesh.owner.x-1u], 3u) : 3u);
    desiredLOD[i] = lod;
    uvec4 range = mesh.ranges[lod];
    commands[i].count = range.x;
    commands[i].instanceCount = range.z;
    commands[i].firstIndex = range.y;
    commands[i].baseVertex = 0;
    commands[i].baseInstance = 0u;
}
