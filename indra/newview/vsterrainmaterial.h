// Reference terrain composition coordinates, independent of GL ownership. LGPL-2.1.
#pragma once
#include "llmath.h"
#include "noise.h"
#include <array>
#include <cmath>
namespace VSTerrainMaterial
{
inline std::array<float,4> weights(float a1,float a2,float final)
{
    // get_terrain_mix_weights: drop the reference's .1 sampling threshold,
    // then normalize the surviving contributions.
    std::array<float,4> value{final*a1,final*(1-a1),(1-final)*a2,(1-final)*(1-a2)};
    float total=0;
    for(auto& weight:value) {weight=std::max(weight-.1f,0.f);total+=weight;}
    for(auto& weight:value) weight/=total;
    return value;
}
inline float bilinear(float v00,float v01,float v10,float v11,float x,float y)
{
    const float ix=1.f-x,iy=1.f-y;
    return ix*iy*v00+x*iy*v10+ix*y*v01+x*y*v11;
}
// LLVLComposition::generateHeights and LLSurfacePatch::eval, in meters.
inline std::array<float,2> coordinates(double ox,double oy,float x,float y,float z,float width,
                                      const std::array<float,4>& starts,const std::array<float,4>& ranges)
{
    const float iw=1.f/width;
    const float start=bilinear(starts[0],starts[1],starts[2],starts[3],x*iw,y*iw);
    const float range=bilinear(ranges[0],ranges[1],ranges[2],ranges[3],x*iw,y*iw);
    float vec[3]={float(ox+x)*(1.f/4.9215f),float(oy+y)*(1.f/4.9215f),z*.25f};
    float low[3]={vec[0]*.2222222222f,vec[1]*.2222222222f,vec[2]*.2222222222f};
    const float twiddle=(noise2(low)*6.5f+turbulence2(vec,2)*(1.5f*1.5f))*2.f;
    const float value=range!=0?(z+twiddle-start)*4.f/range:(z+twiddle>start?3.f:0.f);
    const float scale=(1.f/(4.9215f*7.f))*.2222222222f;
    float random[3]={float(std::fmod(float(ox+x)*scale,256.f)),float(std::fmod(float(oy+y)*scale,256.f)),0};
    return {llclamp(value,0.f,3.f),llclamp(noise2(random)*.75f+.5f,0.f,1.f)};
}
}
