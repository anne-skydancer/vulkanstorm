
#pragma once
#include <cmath>
struct LLVector3 {
 float mV[3]{};
 LLVector3(float x=0,float y=0,float z=0):mV{x,y,z}{}
 bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}
 float length()const{return std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);}
 LLVector3 operator+(const LLVector3& b)const{return {mV[0]+b.mV[0],mV[1]+b.mV[1],mV[2]+b.mV[2]};}
 LLVector3 operator*(float s)const{return {mV[0]*s,mV[1]*s,mV[2]*s};}
 LLVector3 operator/(float s)const{return *this*(1/s);}
 LLVector3& operator+=(const LLVector3& b){*this=*this+b;return *this;}
 bool operator!=(const LLVector3& b)const{return mV[0]!=b.mV[0]||mV[1]!=b.mV[1]||mV[2]!=b.mV[2];}
};
