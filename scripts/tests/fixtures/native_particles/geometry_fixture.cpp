
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <random>
using F32=float;using U8=unsigned char;
struct LLVector2 {float mV[2]{};void normalize(){const float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]);if(n){mV[0]/=n;mV[1]/=n;}}};
struct LLQuaternion{float factor=1;};
struct LLVector3{float mV[3]{};LLVector3(float x=0,float y=0,float z=0):mV{x,y,z}{}bool isExactlyZero()const{return !mV[0]&&!mV[1]&&!mV[2];}LLVector3&operator*=(const LLQuaternion&q){for(auto&v:mV)v*=q.factor;return *this;}};
struct Float{float x;float getF32()const{return x;}};
struct LLVector4a{float x[4]{};LLVector4a(float a=0,float b=0,float c=0):x{a,b,c,0}{}void load3(const float*p){std::copy(p,p+3,x);}float*getF32ptr(){return x;}void setAdd(const LLVector4a&a,const LLVector4a&b){for(int i=0;i<4;++i)x[i]=a.x[i]+b.x[i];}void setSub(const LLVector4a&a,const LLVector4a&b){for(int i=0;i<4;++i)x[i]=a.x[i]-b.x[i];}void setCross3(const LLVector4a&a,const LLVector4a&b){x[0]=a.x[1]*b.x[2]-a.x[2]*b.x[1];x[1]=a.x[2]*b.x[0]-a.x[0]*b.x[2];x[2]=a.x[0]*b.x[1]-a.x[1]*b.x[0];x[3]=0;}void normalize3fast(){float n=std::sqrt(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]);if(n)for(auto&v:x)v/=n;}void mul(float f){for(auto&v:x)v*=f;}void add(const LLVector4a&a){for(int i=0;i<4;++i)x[i]+=a.x[i];}void sub(const LLVector4a&a){for(int i=0;i<4;++i)x[i]-=a.x[i];}Float dot3(const LLVector4a&a){return {x[0]*a.x[0]+x[1]*a.x[1]+x[2]*a.x[2]};}};
template<class T>using LLStrider=T*;
template<class T>struct Ptr{T*p=nullptr;bool notNull()const{return p;}T*operator->()const{return p;}};
struct SourceObject{LLQuaternion rotation;LLQuaternion getRenderRotation()const{return rotation;}};
struct Source{Ptr<SourceObject>mSourceObjectp;LLVector3 mPosAgent;};
struct LLPartData{enum{LL_PART_RIBBON_MASK=1,LL_PART_FOLLOW_VELOCITY_MASK=2};};
struct LLViewerPart{unsigned mFlags=0;LLVector3 mPosAgent,mAxis,mVelocity;LLVector2 mScale,mStartScale;LLViewerPart*mParent=nullptr;Source*mPartSourcep=nullptr;};
struct LLVOPartGroup{LLVector3 camera;LLVector3 getCameraPosition(){return camera;}void getGeometry(const LLViewerPart&,LLStrider<LLVector4a>&);};
GOLD
KERNEL
int main(){std::mt19937 gen(784);std::uniform_real_distribution<float>d(-10,10);SourceObject object;Source source;source.mSourceObjectp.p=&object;LLVOPartGroup reference;LLViewerPart parent;parent.mPartSourcep=&source;
for(unsigned n=0;n<50000;++n){LLViewerPart part;part.mPartSourcep=&source;part.mParent=n&1?&parent:nullptr;source.mSourceObjectp.p=n&2?&object:nullptr;object.rotation.factor=d(gen);part.mFlags=(n>>2)&3;part.mPosAgent={d(gen),d(gen),d(gen)};part.mAxis={d(gen),d(gen),d(gen)};part.mVelocity={d(gen),d(gen),d(gen)};if(n%11==0)part.mVelocity={};part.mScale={{std::abs(d(gen)),std::abs(d(gen))}};part.mStartScale={{std::abs(d(gen)),0}};parent.mAxis={d(gen),d(gen),d(gen)};parent.mPosAgent={d(gen),d(gen),d(gen)};parent.mScale={{std::abs(d(gen)),0}};source.mPosAgent={d(gen),d(gen),d(gen)};reference.camera={d(gen),d(gen),d(gen)};LLVector4a a[4],b[4];auto*pa=a;auto*pb=b;reference.getGeometry(part,pa);vs_particle_geometry(part,reference.camera,pb,[&]()->std::optional<LLQuaternion>{return source.mSourceObjectp.notNull()?std::optional<LLQuaternion>(object.rotation):std::nullopt;});assert(pa==a+4&&pb==b+4);assert(std::memcmp(a,b,sizeof(a))==0);}}
