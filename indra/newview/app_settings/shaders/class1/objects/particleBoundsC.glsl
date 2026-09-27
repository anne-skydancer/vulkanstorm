#version 430 core
// SPDX-License-Identifier: LGPL-2.1-or-later
layout(local_size_x=64) in;
struct Entry { uvec4 key,value; };
struct Vertex { vec4 position,normalMaterial,color,uvGlow; };
struct Bounds { vec4 lower,upper; };
layout(std430,binding=0) readonly buffer Entries { Entry entries[]; };
layout(std430,binding=1) readonly buffer Ranges { uvec4 ranges[]; };
layout(std430,binding=2) readonly buffer Vertices { Vertex vertices[]; };
layout(std430,binding=3) writeonly buffer Output { Bounds bounds[]; };
uniform uint particleCount,cullEnabled,viewDomain;
uniform mat4 viewProjection;
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>=particleCount || ranges[i].y==0u) return;
    uint finish=i+ranges[i].y;
    vec3 lower=vec3(3.402823e38),upper=-lower;
    for(uint j=i;j<finish;++j)
        for(uint k=0u;k<4u;++k)
        {
            vec3 p=vertices[entries[j].value.x*4u+k].position.xyz;
            lower=min(lower,p); upper=max(upper,p);
        }
    bool visible=true;
    if(cullEnabled!=0u)
    {
        visible=entries[i].value.w==viewDomain;
        uint outside=63u;
        for(uint k=0u;k<8u;++k)
        {
            vec3 p=vec3((k&1u)!=0u?upper.x:lower.x,(k&2u)!=0u?upper.y:lower.y,(k&4u)!=0u?upper.z:lower.z);
            vec4 clip=viewProjection*vec4(p,1);
            uint mask=0u;
            if(clip.x < -clip.w) mask|=1u; if(clip.x > clip.w) mask|=2u;
            if(clip.y < -clip.w) mask|=4u; if(clip.y > clip.w) mask|=8u;
            if(clip.z < -clip.w) mask|=16u; if(clip.z > clip.w) mask|=32u;
            outside &= mask;
        }
        visible=visible && outside==0u;
    }
    // Replicated per-slot group bounds avoid a CPU group map or a second GPU
    // lookup table. Each slot belongs to exactly one disjoint spatial range.
    for(uint j=i;j<finish;++j)
        bounds[entries[j].value.x]=Bounds(vec4(lower,visible?1.:0.),vec4(upper,float(i)));
}
