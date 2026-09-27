#version 450 core
// SPDX-License-Identifier: LGPL-2.1-or-later
// Geometry reads only resident particles and source snapshots. Four vertices per
// stable slot; sorting and alpha intervals reference these vertices by index.
layout(local_size_x=64) in;
struct Particle
{
    vec4 positionAge, velocityLife, accelerationParameter, offsetStartGlow;
    vec4 startColor, endColor, color, scales, scaleGlow;
    uvec4 identity, sourceParent;
    vec4 axis;
};
struct Source
{
    vec4 position, target, callbackPosition, callbackTarget, regionOriginWidth;
    uvec4 control, wind;
    vec4 ribbonAxis;
};
struct Vertex { vec4 position, normalMaterial, color, uvGlow; };
layout(std430,binding=0) readonly buffer Particles { Particle particles[]; };
layout(std430,binding=1) readonly buffer Sources { Source sources[]; };
layout(std430,binding=2) writeonly buffer Vertices { Vertex vertices[]; };
uniform uint particleCount, sourceCount;
uniform vec3 cameraPosition, cameraForward;
vec3 unitOr(vec3 v,vec3 fallback)
{
    float scale=max(max(abs(v.x),abs(v.y)),abs(v.z));
    if(!(scale>0.0) || isinf(scale)) return fallback;
    v/=scale;
    return v*inversesqrt(dot(v,v));
}
vec4 byteColor(vec4 c) { return floor(clamp(c,0.0,1.0)*255.0+0.5)/255.0; }
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>=particleCount) return;
    Particle p=particles[i];
    if(p.identity.y==0u) return; // inactive slots cannot occur in the depth stream
    bool ribbon=(p.identity.z & 0x400u)!=0u;
    vec3 right=vec3(0),up=vec3(0);
    vec3 parentPosition=p.positionAge.xyz,parentAxis=p.axis.xyz;
    float parentScale=p.scaleGlow.x,parentGlow=p.scaleGlow.z/255.0;
    vec4 parentColor=p.color;
    if(ribbon)
    {
        uint parent=p.sourceParent.z;
        if(parent<particleCount && particles[parent].identity.x==p.sourceParent.w && particles[parent].identity.y!=0u)
        {
            Particle q=particles[parent];
            parentPosition=q.positionAge.xyz; parentAxis=q.axis.xyz;
            parentScale=q.scaleGlow.x; parentColor=q.color; parentGlow=q.scaleGlow.z/255.0;
        }
        else
        {
            parentColor=p.startColor;
            parentGlow=floor(clamp(p.offsetStartGlow.w,0.0,1.0)*255.0+0.5)/255.0;
            uint source=p.sourceParent.x;
            if(source<sourceCount && sources[source].control.x==p.sourceParent.y && sources[source].ribbonAxis.w!=0.0)
            {
                parentPosition=sources[source].position.xyz;
                parentAxis=sources[source].ribbonAxis.xyz;
                parentScale=p.scales.x;
            }
        }
    }
    else
    {
        vec3 camera=(p.identity.z & 0x40000000u)!=0u ? vec3(-1,0,0) : cameraPosition;
        vec3 at=unitOr(p.positionAge.xyz-camera,vec3(0,1,0));
        right=unitOr(cross(at,vec3(0,0,1)),vec3(1,0,0));
        up=unitOr(cross(right,at),vec3(0,0,1));
        if((p.identity.z & 0x20u)!=0u)
        {
            vec3 velocity=unitOr(p.velocityLife.xyz,vec3(0));
            vec2 projected=vec2(dot(velocity,right),dot(velocity,up));
            if(dot(projected,projected)>1e-12)
            {
                projected=normalize(projected);
                vec3 newUp=unitOr(projected.x*right+projected.y*up,up);
                right=unitOr(projected.y*right-projected.x*up,right);
                up=newUp;
            }
        }
        right*=0.5*p.scaleGlow.x; up*=0.5*p.scaleGlow.y;
    }
    for(uint corner=0u;corner<4u;++corner)
    {
        bool parent=ribbon && corner<2u;
        vec3 pos=p.positionAge.xyz+((corner & 1u)==0u?up:-up)+(corner<2u?-right:right);
        if(ribbon)
            pos=(parent?parentPosition:p.positionAge.xyz)+((corner & 1u)==0u?0.5:-0.5)*
                (parent?parentScale:p.scaleGlow.x)*(parent?parentAxis:p.axis.xyz);
        Vertex v;
        v.position=vec4(pos,1.0);
        v.normalMaterial=vec4(-cameraForward,uintBitsToFloat(p.identity.w));
        v.color=byteColor(parent?parentColor:p.color);
        v.uvGlow=vec4(corner<2u?0.0:1.0,(corner & 1u)==0u?1.0:0.0,parent?parentGlow:p.scaleGlow.z/255.0,0.0);
        vertices[i*4u+corner]=v;
    }
}
