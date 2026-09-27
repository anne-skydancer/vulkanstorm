#version 430 core
// SPDX-License-Identifier: LGPL-2.1-or-later
layout(local_size_x=64) in;
struct Entry { uvec4 key,value; };
struct Bounds { vec4 lower,upper; };
layout(std430,binding=0) buffer Entries { Entry entries[]; };
layout(std430,binding=1) readonly buffer Input { Bounds bounds[]; };
uniform uint particleCount;
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>=particleCount) return;
    uint slot=entries[i].value.x;
    if(slot!=0xffffffffu && bounds[slot].lower.w==0.)
        entries[i]=Entry(uvec4(0xffffffffu),uvec4(0xffffffffu));
}
