#version 430 core
// SPDX-License-Identifier: LGPL-2.1-or-later
// One disjoint-pair bitonic stage. Host dispatches width=2..N, stride=width/2..1,
// with GL_SHADER_STORAGE_BARRIER_BIT between stages. N must be a power of two.
// Entries move, resident particle slots do not.
layout(local_size_x=64) in;
struct Entry { uvec4 key; uvec4 value; };
layout(std430, binding=0) buffer Entries { Entry entries[]; };
uniform uint paddedCount;
uniform uint sortWidth;
uniform uint sortStride;
bool lessEntry(Entry a, Entry b)
{
    for (int k=0; k<4; ++k)
    {
        if (a.key[k] < b.key[k]) return true;
        if (a.key[k] > b.key[k]) return false;
    }
    return a.value.x < b.value.x;
}
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if (i>=paddedCount) return;
    uint j=i ^ sortStride;
    if (j<=i || j>=paddedCount) return;
    Entry a=entries[i], b=entries[j];
    bool ascending=(i & sortWidth)==0u;
    if ((ascending && lessEntry(b,a)) || (!ascending && lessEntry(a,b)))
    {
        entries[i]=b;
        entries[j]=a;
    }
}
