#version 450 core
// SPDX-License-Identifier: LGPL-2.1-or-later
// Partition the existing sorted particle stream around host world-alpha draws.
// Each GPU result is (first sorted entry, draw count, domain, color/glow draw count).
// Draw counts are consumed directly with ARB_indirect_parameters.
layout(local_size_x=64) in;
struct Entry { uvec4 key; uvec4 value; };
layout(std430,binding=0) readonly buffer Order { Entry entries[]; };
layout(std430,binding=1) readonly buffer Boundaries { float boundaries[]; };
layout(std430,binding=2) writeonly buffer Intervals { uvec4 intervals[]; };
uniform uint particleCount, boundaryCount, domain;
uint orderedFloat(float v)
{
    if(v==0.0) v=0.0;
    uint bits=floatBitsToUint(v);
    return (bits & 0x80000000u)!=0u ? ~bits : (bits ^ 0x80000000u);
}
uint lowerBound(uvec2 key)
{
    uint first=0u,last=particleCount;
    while(first<last)
    {
        uint middle=first+(last-first)/2u;
        uvec2 candidate=entries[middle].key.xy;
        bool less=candidate.x<key.x || (candidate.x==key.x && candidate.y<key.y);
        if(less) first=middle+1u; else last=middle;
    }
    return first;
}
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>boundaryCount) return;
    uint first=i==0u ? lowerBound(uvec2(domain,0u))
        : lowerBound(uvec2(domain,~orderedFloat(boundaries[i-1u])));
    uint last=i==boundaryCount ? lowerBound(uvec2(domain+1u,0u))
        : lowerBound(uvec2(domain,~orderedFloat(boundaries[i])));
    intervals[i]=uvec4(first,last-first,domain,2u*(last-first));
}
