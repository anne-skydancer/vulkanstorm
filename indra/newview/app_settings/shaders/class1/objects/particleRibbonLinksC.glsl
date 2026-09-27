#version 430 core
// SPDX-License-Identifier: LGPL-2.1-or-later
// Repair ribbon holes without racing on live state. Run before recycling slots.
// Seed (mode 0), ping-pong pointer jumping (mode 1, ceil(log2(capacity)) passes),
// then finalize (mode 2). All passes require a shader-storage barrier.
// Output is (parent slot, generation, 0, 0). Missing/stale/cyclic links become
// INVALID; geometry must then use its existing source/detached endpoint policy.
layout(local_size_x=64) in;
struct Particle
{
    vec4 positionAge, velocityLife, accelerationParameter, offsetStartGlow;
    vec4 startColor, endColor, color, scales, scaleGlow;
    uvec4 identity, sourceParent;
    vec4 axis;
};
layout(std430,binding=0) buffer Particles { Particle particles[]; };
layout(std430,binding=1) readonly buffer Previous { uvec4 previous[]; };
layout(std430,binding=2) writeonly buffer Next { uvec4 nextLinks[]; };
uniform uint particleCount;
uniform uint linkMode;
const uint INVALID=0xffffffffu;
bool valid(uvec2 handle)
{
    return handle.x<particleCount && particles[handle.x].identity.x==handle.y;
}
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>=particleCount) return;
    if(linkMode==3u)
    {
        particles[i].sourceParent.zw=previous[i].xy;
        return;
    }
    uvec2 handle;
    if(linkMode==0u) handle=particles[i].sourceParent.zw;
    else handle=previous[i].xy;
    if(!valid(handle) || handle.x==i) handle=uvec2(INVALID,0u);
    else if(particles[handle.x].identity.y==0u)
    {
        if(linkMode==1u) handle=previous[handle.x].xy;
        else if(linkMode==2u) handle=uvec2(INVALID,0u);
    }
    nextLinks[i]=uvec4(handle,0u,0u);
}
