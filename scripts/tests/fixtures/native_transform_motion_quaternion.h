
#pragma once
#include "v3math.h"
struct LLQuaternion {
 float z=0,w=1;
 bool isFinite()const{return std::isfinite(z)&&std::isfinite(w);}
 void loadIdentity(){z=0;w=1;}
 void setQuat(float angle,const LLVector3& axis){z=axis.mV[2]*std::sin(angle*.5f);w=std::cos(angle*.5f);}
 LLQuaternion operator*(const LLQuaternion& b)const{return {z*b.w+w*b.z,w*b.w-z*b.z};}
 void normalize(){const float n=std::sqrt(z*z+w*w);z/=n;w/=n;}
 bool operator!=(const LLQuaternion& b)const{return z!=b.z||w!=b.w;}
};
