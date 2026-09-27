#version 430 core
// SPDX-License-Identifier: LGPL-2.1-or-later
layout(local_size_x=64) in;
struct Particle { vec4 a,b,c,d,e,f,g,h,j; uvec4 identity,source; vec4 axis; };
struct Vertex { vec4 position,normalMaterial,color,uvGlow; };
layout(std430,binding=0) readonly buffer Particles { Particle particles[]; };
layout(std430,binding=1) readonly buffer Vertices { Vertex vertices[]; };
layout(std430,binding=2) buffer Result { uint distanceBits; uint slot; };
uniform uint particleCount,pickMode;
uniform vec3 rayStart,rayDirection;
float triangle(vec3 a,vec3 b,vec3 c)
{
    vec3 e=b-a,f=c-a,h=cross(rayDirection,f);
    float det=dot(e,h);
    if(abs(det)<1e-8) return 2.;
    vec3 s=rayStart-a;
    float u=dot(s,h)/det;
    vec3 q=cross(s,e);
    float v=dot(rayDirection,q)/det,t=dot(f,q)/det;
    return u>=0. && v>=0. && u+v<=1. && t>=0. && t<=1. ? t : 2.;
}
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>=particleCount || particles[i].identity.y==0u ||
       (particles[i].identity.z&0x40000000u)!=0u) return;
    uint k=i*4u;
    float t=min(triangle(vertices[k].position.xyz,vertices[k+1u].position.xyz,vertices[k+2u].position.xyz),
                triangle(vertices[k+2u].position.xyz,vertices[k+1u].position.xyz,vertices[k+3u].position.xyz));
    if(t>1.) return;
    if(pickMode==0u) atomicMin(distanceBits,floatBitsToUint(t));
    else if(floatBitsToUint(t)==distanceBits) atomicMin(slot,i);
}
