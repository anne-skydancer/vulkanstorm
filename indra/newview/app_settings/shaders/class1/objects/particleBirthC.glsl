#version 450 core
// SPDX-License-Identifier: LGPL-2.1-or-later
// Deterministic bounded allocation. Each mode is a separate dispatch/barrier.
// 0: block counts; 1: block prefixes/admission; 2: free-slot scatter;
// 3: install births; 4: connect ribbons; 5: publish per-source last handles.
layout(local_size_x=64) in;
struct Particle
{
    vec4 positionAge, velocityLife, accelerationParameter, offsetStartGlow;
    vec4 startColor, endColor, color, scales, scaleGlow;
    uvec4 identity, sourceParent;
    vec4 axis;
};
struct Birth { Particle particle; uvec4 chain; };
layout(std430,binding=0) buffer Particles { Particle particles[]; };
layout(std430,binding=1) readonly buffer Births { Birth births[]; };
layout(std430,binding=2) buffer Allocation { uint allocation[]; };
// slot, particle generation, source generation, reserved
layout(std430,binding=3) buffer SourceTails { uvec4 tails[]; };
uniform uint particleCount, birthCount, maxLiveCount, birthMode;
const uint INVALID=0xffffffffu;
shared uint freePrefix[64];
shared uint liveCount[64];
void main()
{
    uint i=gl_GlobalInvocationID.x, lane=gl_LocalInvocationID.x;
    uint blocks=(particleCount+63u)/64u;
    uint freeBase=4u+2u*blocks;
    if(birthMode==0u || birthMode==2u)
    {
        bool live=i<particleCount && particles[i].identity.y!=0u;
        bool available=i<particleCount && !live && particles[i].identity.x!=INVALID;
        freePrefix[lane]=available?1u:0u;
        liveCount[lane]=live?1u:0u;
        barrier();
        for(uint stride=1u;stride<64u;stride*=2u)
        {
            uint f=lane>=stride?freePrefix[lane-stride]:0u;
            uint l=lane>=stride?liveCount[lane-stride]:0u;
            barrier();
            freePrefix[lane]+=f; liveCount[lane]+=l;
            barrier();
        }
        if(birthMode==0u && lane==63u)
        {
            allocation[4u+2u*gl_WorkGroupID.x]=freePrefix[63];
            allocation[5u+2u*gl_WorkGroupID.x]=liveCount[63];
        }
        if(birthMode==2u && available)
            allocation[freeBase+allocation[4u+2u*gl_WorkGroupID.x]+freePrefix[lane]-1u]=i;
        return;
    }
    if(birthMode==1u)
    {
        if(i!=0u) return;
        uint freeTotal=0u, liveTotal=0u;
        for(uint b=0u;b<blocks;++b)
        {
            uint freeCount=allocation[4u+2u*b];
            allocation[4u+2u*b]=freeTotal;
            freeTotal+=freeCount;
            liveTotal+=allocation[5u+2u*b];
        }
        allocation[0]=freeTotal;
        allocation[1]=liveTotal;
        allocation[2]=min(min(freeTotal,birthCount),maxLiveCount>liveTotal?maxLiveCount-liveTotal:0u);
        return;
    }
    uint admitted=allocation[2];
    if(i>=admitted) return;
    uint slot=allocation[freeBase+i];
    if(birthMode==3u)
    {
        Particle p=births[i].particle;
        p.identity.x=particles[slot].identity.x+1u;
        p.identity.y=1u;
        p.sourceParent.zw=uvec2(INVALID,0u);
        particles[slot]=p;
        return;
    }
    uint source=births[i].particle.sourceParent.x;
    uint sourceGeneration=births[i].particle.sourceParent.y;
    uint next=births[i].chain.y;
    if(birthMode==4u)
    {
        // The older particle points TO the next/newer particle in this fork.
        // Linking is separate from installation so generation reads cannot race.
        if(next<admitted && (births[next].particle.identity.z & 0x400u)!=0u)
        {
            uint parent=allocation[freeBase+next];
            particles[slot].sourceParent.zw=uvec2(parent,particles[parent].identity.x);
        }
        if(births[i].chain.x==INVALID && (births[i].particle.identity.z & 0x400u)!=0u)
        {
            uvec4 tail=tails[source];
            if(tail.z==sourceGeneration && tail.x<particleCount &&
               particles[tail.x].identity.x==tail.y && particles[tail.x].identity.y!=0u &&
               all(equal(particles[tail.x].sourceParent.xy,uvec2(source,sourceGeneration))))
                particles[tail.x].sourceParent.zw=uvec2(slot,particles[slot].identity.x);
        }
        return;
    }
    // Do not publish tails until every invocation has finished reading old tails.
    if(next>=admitted)
        tails[source]=uvec4(slot,particles[slot].identity.x,sourceGeneration,0u);
}
