#version 430 core
// SPDX-License-Identifier: LGPL-2.1-or-later
// Ping-pong suffix minimum, strides 1,2,4,...,N/2; barrier between dispatches.
// Does not assume contiguous equal materials after depth sorting.
layout(local_size_x=64) in;
layout(std430,binding=0) readonly buffer Input { uvec4 inputValues[]; };
layout(std430,binding=1) writeonly buffer Output { uvec4 outputValues[]; };
uniform uint paddedCount;
uniform uint scanStride;
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>=paddedCount) return;
    uint v=inputValues[i].x;
    if(i+scanStride<paddedCount) v=min(v,inputValues[i+scanStride].x);
    outputValues[i]=uvec4(v,0u,0u,0u);
}
