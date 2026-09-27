#version 430 core
// SPDX-License-Identifier: LGPL-2.1-or-later
// Sparse ranges: only a run's first sorted position receives an entry.
// Binary search finds the end of spatial bins, whose equal keys are contiguous.
// Material runs use a precomputed next-boundary suffix minimum (mode 2).
layout(local_size_x=64) in;
struct Entry { uvec4 key; uvec4 value; };
layout(std430, binding=0) readonly buffer Entries { Entry entries[]; };
layout(std430, binding=1) writeonly buffer Ranges { uvec4 ranges[]; };
layout(std430, binding=2) readonly buffer Ends { uint ends[]; };
uniform uint paddedCount;
uniform uint rangeMode; // 0 spatial, 1 boundary initialization, 2 material ranges
bool same(Entry a, Entry b)
{
    if (rangeMode==0u) return all(equal(a.key,b.key));
    return a.value.y==b.value.y && a.value.w==b.value.w;
}
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if (i>=paddedCount) return;
    ranges[i]=uvec4(0u);
    Entry e=entries[i];
    if (rangeMode==1u)
    {
        bool end=i+1u==paddedCount;
        if (!end) end=!same(e,entries[i+1u]);
        ranges[i]=uvec4(end ? i+1u : paddedCount,0u,0u,0u);
        return;
    }
    if (e.value.x==0xffffffffu) return;
    if (i>0u && same(e,entries[i-1u])) return;
    uint finish;
    if (rangeMode==0u)
    {
        uint low=i+1u, high=paddedCount;
        while (low<high)
        {
            uint mid=low+(high-low)/2u;
            if (same(e,entries[mid])) low=mid+1u;
            else high=mid;
        }
        finish=low;
    }
    else finish=ends[i*4u];
    ranges[i]=uvec4(i,finish-i,e.value.y,e.value.w);
}
