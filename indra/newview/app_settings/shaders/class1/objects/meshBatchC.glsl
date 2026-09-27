// SPDX-License-Identifier: LGPL-2.1-or-later
#version 430 core
layout(local_size_x=64) in;
struct Command { uint count; uint instances; uint firstIndex; int baseVertex; uint baseInstance; };
struct Bounds { vec4 minimum; vec4 maximum; };
layout(std430, binding=0) readonly buffer Source { Command source[]; };
layout(std430, binding=1) readonly buffer Candidates { uint candidates[]; };
layout(std430, binding=2) writeonly buffer Output { Command commands[]; };
layout(std430, binding=3) readonly buffer ResidentBounds { Bounds bounds[]; };
uniform uint candidateCount;
uniform uint sourceCount;
uniform mat4 clipFromBuffer;
// Depth-clamped shadow passes clip only against the four lateral planes.
uniform uint clipPlaneMask;

bool visible(Bounds c)
{
    if (any(isnan(c.minimum.xyz)) || any(isinf(c.minimum.xyz)) ||
        any(isnan(c.maximum.xyz)) || any(isinf(c.maximum.xyz)) ||
        any(greaterThan(c.minimum.xyz, c.maximum.xyz))) return true;
    uint commonOutside = clipPlaneMask;
    for (uint corner=0u; corner<8u; ++corner)
    {
        vec3 p = vec3((corner&1u)==0u ? c.minimum.x : c.maximum.x,
                      (corner&2u)==0u ? c.minimum.y : c.maximum.y,
                      (corner&4u)==0u ? c.minimum.z : c.maximum.z);
        vec4 clip = clipFromBuffer * vec4(p, 1.0);
        if (any(isnan(clip)) || any(isinf(clip))) return true;
        // Conservative epsilon at clip boundaries avoids precision flicker.
        float w = clip.w + 1e-5 * max(1.0, abs(clip.w));
        uint outside = (clip.x < -w ? 1u : 0u) | (clip.x > w ? 2u : 0u) |
                       (clip.y < -w ? 4u : 0u) | (clip.y > w ? 8u : 0u) |
                       (clip.z < -w ? 16u : 0u) | (clip.z > w ? 32u : 0u);
        commonOutside &= outside;
    }
    return commonOutside == 0u;
}

void main()
{
    uint i=gl_GlobalInvocationID.x;
    if (i>=candidateCount) return;
    uint slot=candidates[i];
    if (slot>=sourceCount)
    {
        commands[i]=Command(0u,0u,0u,0,0u);
        return;
    }
    Command command=source[slot];
    if (!visible(bounds[slot])) command.instances=0u;
    commands[i]=command;
}
