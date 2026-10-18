#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <random>
#include <string>
#include <vector>
using F32=float;using U8=unsigned char;
std::vector<std::string> trace;
struct LLVector3;
struct LLVector2{float mV[2]{};LLVector2()=default;LLVector2(float x,float y):mV{x,y}{};LLVector2(const LLVector3&);};
struct LLVector3{float mV[3]{};void scaleVec(LLVector3 b){for(int n=0;n<3;n++)mV[n]*=b.mV[n];}LLVector3 scaledVec(LLVector3 b)const{b.scaleVec(*this);return b;}LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{};LLVector3(const float*v):mV{v[0],v[1],v[2]}{};LLVector3(LLVector2 a):mV{a.mV[0],a.mV[1],0}{}};
LLVector3 operator+(LLVector3 a,LLVector3 b){return {a.mV[0]+b.mV[0],a.mV[1]+b.mV[1],a.mV[2]+b.mV[2]};}
LLVector3 operator-(LLVector3 a,LLVector3 b){return {a.mV[0]-b.mV[0],a.mV[1]-b.mV[1],a.mV[2]-b.mV[2]};}
LLVector3 operator*(LLVector3 a,float s){return {a.mV[0]*s,a.mV[1]*s,a.mV[2]*s};}
LLVector2::LLVector2(const LLVector3&a):mV{a.mV[0],a.mV[1]}{}
struct LLVector4a{float value[4]{};float operator[](unsigned n)const{return value[n];}const float*getF32ptr()const{return value;}void set(float x,float y,float z){value[0]=x;value[1]=y;value[2]=z;}void load3(const float*p){set(p[0],p[1],p[2]);}void mul(const LLVector4a&b){for(int n=0;n<3;n++)value[n]*=b[n];}void normalize3fast(){float l=std::sqrt(value[0]*value[0]+value[1]*value[1]+value[2]*value[2]);if(l>0)for(float&v:value)v/=l;}void setCross3(const LLVector4a&a,const LLVector4a&b){set(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]);}struct Scalar{float value;float getF32()const{return value;}};Scalar dot3(const LLVector4a&b)const{return {value[0]*b[0]+value[1]*b[1]+value[2]*b[2]};}};
struct LLMatrix4{float m[3][3]{};};LLVector3 operator*(LLVector3 v,const LLMatrix4&m){return {v.mV[0]*m.m[0][0]+v.mV[1]*m.m[1][0]+m.m[2][0],v.mV[0]*m.m[0][1]+v.mV[1]*m.m[1][1]+m.m[2][1],0};}
struct LLTextureEntry{enum{TEX_GEN_DEFAULT=0,TEX_GEN_PLANAR=2};U8 gen=0;float rotation=0,mOffsetS=0,mOffsetT=0,mScaleS=1,mScaleT=1;U8 getTexGen()const{trace.push_back("texgen");return gen;}float getRotation()const{trace.push_back("TErotation");return rotation;}};
struct VolumeFace{LLVector4a*mCenter;};struct Volume{VolumeFace face;VolumeFace getVolumeFace(int){trace.push_back("center");return face;}};
struct VolumeObject{Volume volume;LLVector3 origin,scale;float angle=0;bool global=false;Volume*getVolume(){trace.push_back("volume");return &volume;}LLVector3 agentPositionToVolume(LLVector3 v){trace.push_back("position");for(int k=0;k<3;k++)v.mV[k]-=origin.mV[k];float a=v.mV[0],b=v.mV[1];v.mV[0]=a*std::cos(angle)+b*std::sin(angle);v.mV[1]=-a*std::sin(angle)+b*std::cos(angle);if(!global)for(int k=0;k<3;k++)v.mV[k]/=scale.mV[k];return v;}LLVector3 agentDirectionToVolume(LLVector3 v){trace.push_back("normal");float a=v.mV[0],b=v.mV[1];v.mV[0]=a*std::cos(angle)+b*std::sin(angle);v.mV[1]=-a*std::sin(angle)+b*std::cos(angle);if(!global)for(int k=0;k<3;k++)v.mV[k]*=scale.mV[k];return v;}bool isVolumeGlobal(){trace.push_back("global");return global;}LLVector3 getScale(){trace.push_back("scale");return scale;}};
struct Drawable{VolumeObject*volume;VolumeObject*getVOVolume(){trace.push_back("vovolume");return volume;}};
struct LLFace{LLTextureEntry*te;Drawable*mDrawablep;VolumeObject*mVObjp;int mTEOffset=0;LLMatrix4*mTextureMatrix=nullptr;LLTextureEntry*getTextureEntry(){trace.push_back("te");return te;}LLVector2 surfaceToTexture(LLVector2,const LLVector4a&,const LLVector4a&);};
REFERENCE_MATH
REFERENCE
CURRENT
struct Adapter{LLFace*face;const LLTextureEntry*textureEntry()const{return face->getTextureEntry();}const LLVector4a&center()const{return *face->mDrawablep->getVOVolume()->getVolume()->getVolumeFace(face->mTEOffset).mCenter;}LLVector3 positionToVolume(LLVector3 v)const{return face->mDrawablep->getVOVolume()->agentPositionToVolume(v);}bool volumeGlobal()const{return face->mDrawablep->getVOVolume()->isVolumeGlobal();}LLVector3 scale()const{return face->mVObjp->getScale();}LLVector3 directionToVolume(LLVector3 v)const{return face->mDrawablep->getVOVolume()->agentDirectionToVolume(v);}const LLMatrix4*textureMatrix()const{return face->mTextureMatrix;}};
int main(){std::mt19937 rng(721);std::uniform_real_distribution<float>n(-50,50),u(-1,1);for(int i=0;i<30000;i++){
LLTextureEntry te;te.gen=i%3==0?0:i%3==1?2:4;te.rotation=n(rng);te.mScaleS=n(rng);te.mScaleT=n(rng);te.mOffsetS=n(rng);te.mOffsetT=n(rng);LLMatrix4 m;for(auto&row:m.m)for(float&v:row)v=n(rng);LLVector4a center,position,normal;center.set(n(rng),n(rng),n(rng));position.set(n(rng),n(rng),n(rng));normal.set(u(rng),u(rng),u(rng));VolumeObject vo;vo.origin={n(rng),n(rng),n(rng)};vo.scale={std::abs(n(rng))+.01f,std::abs(n(rng))+.01f,std::abs(n(rng))+.01f};vo.angle=u(rng)*3;vo.global=i%2;vo.volume.face.mCenter=&center;Drawable d{&vo};LLFace face{i%7?&te:nullptr,&d,&vo,0,i%5?nullptr:&m};LLVector2 st{n(rng),n(rng)};
trace.clear();auto a=face.surfaceToTexture(st,position,normal);auto expected=trace;trace.clear();auto b=VSSurfaceCoordinates::surfaceToTexture(Adapter{&face},st,position,normal);assert(trace==expected);for(int k=0;k<2;k++)assert(a.mV[k]==b.mV[k]||(std::isnan(a.mV[k])&&std::isnan(b.mV[k])));
}}
