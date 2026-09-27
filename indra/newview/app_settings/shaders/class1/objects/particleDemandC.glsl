#version 430 core
// SPDX-License-Identifier: LGPL-2.1-or-later
layout(local_size_x=64) in;
struct Particle {vec4 pos,velocity,acceleration,offset,startColor,endColor,color,scales,scaleGlow;uvec4 identity,source;vec4 axis;};
struct Bounds {vec4 lower,upper;};
layout(std430,binding=0) readonly buffer Particles {Particle particles[];};
layout(std430,binding=1) readonly buffer Groups {Bounds bounds[];};
layout(std430,binding=2) buffer Demand {uint demands[];};
uniform uint particleCount;
uniform float pixelMeterRatio;
uniform vec3 cameraPosition;
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>=particleCount || particles[i].identity.y==0u || bounds[i].lower.w==0.) return;
    Particle p=particles[i];
    vec3 delta=p.pos.xyz-cameraPosition;
    float pixels=10.*abs(p.scaleGlow.x*p.scaleGlow.y)*pixelMeterRatio*pixelMeterRatio/max(dot(delta,delta),1.);
    if(p.identity.w<uint(demands.length()) && !isnan(pixels) && !isinf(pixels))
        atomicMax(demands[p.identity.w],floatBitsToUint(max(pixels,0.)));
}
