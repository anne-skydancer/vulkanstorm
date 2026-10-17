// Transpose the shared textureUtilV/glTF reference tangent-space transforms. LGPL-2.1.
#pragma once
#include <array>
#include <cmath>
namespace VSNormalBasis
{
inline float sign(float value) { return float(value>0)-float(value<0); }
inline std::array<float,4> animate(std::array<float,4> value,const std::array<float,4>& animation)
{
    // textureUtilV: apply SL animation after inverse KHR rotation/sign,
    // remove animation scale, and convert weights back to SL handedness.
    float x=animation[0]*value[0]-animation[2]*value[1];
    float y=animation[1]*value[0]-animation[3]*value[1];
    x+=1-std::abs(sign(sign(x)+.5f*sign(y)));
    const float length=std::hypot(x,y);
    value[0]=x/length;value[1]=-y/length;
    return value;
}
inline std::array<float,4> primitive(float sx,float sy,float angle)
{
    const std::array<float,4> value{std::cos(angle)*sign(sx),-std::sin(angle)*sign(sy),sign(sx)*sign(sy),0};
    return animate(value,{1,0,0,1});
}
inline std::array<float,4> imported(float angle)
{
    // gltf/pbrmetallicroughnessV uses rotation only and retains tangent.w.
    return {std::cos(angle),-std::sin(angle),1,0};
}
}
