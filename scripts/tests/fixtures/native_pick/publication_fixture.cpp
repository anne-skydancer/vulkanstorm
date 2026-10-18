#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <random>
#include <memory>
#include <array>
#include <set>
#include "RESOURCE_BUDGET"
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
struct LLVector4a{float value[4]{};float operator[](unsigned n)const{return value[n];}const float*getF32ptr()const{return value;}void set(float x,float y,float z,float w=0){value[0]=x;value[1]=y;value[2]=z;value[3]=w;}void clear(){set(0,0,0,0);}void load3(const float*p){set(p[0],p[1],p[2]);}void mul(const LLVector4a&b){for(int n=0;n<3;n++)value[n]*=b[n];}void normalize3fast(){float l=std::sqrt(value[0]*value[0]+value[1]*value[1]+value[2]*value[2]);if(l>0)for(float&v:value)v/=l;}void setCross3(const LLVector4a&a,const LLVector4a&b){set(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]);}struct Scalar{float value;float getF32()const{return value;}};Scalar dot3(const LLVector4a&b)const{return {value[0]*b[0]+value[1]*b[1]+value[2]*b[2]};}};
struct LLMatrix4{float m[3][3]{};};LLVector3 operator*(LLVector3 v,const LLMatrix4&m){return {v.mV[0]*m.m[0][0]+v.mV[1]*m.m[1][0]+m.m[2][0],v.mV[0]*m.m[0][1]+v.mV[1]*m.m[1][1]+m.m[2][1],0};}
struct LLTextureEntry{enum{TEX_GEN_DEFAULT=0,TEX_GEN_PLANAR=2};U8 gen=0;float rotation=0,mOffsetS=0,mOffsetT=0,mScaleS=1,mScaleT=1;U8 getTexGen()const{trace.push_back("texgen");return gen;}float getRotation()const{trace.push_back("TErotation");return rotation;}};
struct VolumeFace{LLVector4a*mCenter;};struct Volume{VolumeFace face;VolumeFace getVolumeFace(int){trace.push_back("center");return face;}};
struct VolumeObject{Volume volume;LLVector3 origin,scale;float angle=0;bool global=false;Volume*getVolume(){trace.push_back("volume");return &volume;}LLVector3 agentPositionToVolume(LLVector3 v){trace.push_back("position");for(int k=0;k<3;k++)v.mV[k]-=origin.mV[k];float a=v.mV[0],b=v.mV[1];v.mV[0]=a*std::cos(angle)+b*std::sin(angle);v.mV[1]=-a*std::sin(angle)+b*std::cos(angle);if(!global)for(int k=0;k<3;k++)v.mV[k]/=scale.mV[k];return v;}LLVector3 agentDirectionToVolume(LLVector3 v){trace.push_back("normal");float a=v.mV[0],b=v.mV[1];v.mV[0]=a*std::cos(angle)+b*std::sin(angle);v.mV[1]=-a*std::sin(angle)+b*std::cos(angle);if(!global)for(int k=0;k<3;k++)v.mV[k]*=scale.mV[k];return v;}bool isVolumeGlobal(){trace.push_back("global");return global;}LLVector3 getScale(){trace.push_back("scale");return scale;}};
struct Drawable{VolumeObject*volume;VolumeObject*getVOVolume(){trace.push_back("vovolume");return volume;}};
struct LLFace{LLTextureEntry*te;Drawable*mDrawablep;VolumeObject*mVObjp;int mTEOffset=0;LLMatrix4*mTextureMatrix=nullptr;LLTextureEntry*getTextureEntry(){trace.push_back("te");return te;}LLVector2 surfaceToTexture(LLVector2,const LLVector4a&,const LLVector4a&);};
SURFACE_MATH
using S32=int;using U64=unsigned long long;using LLQuaternion=float;
struct VSWorldContracts{enum{Position=1,Normal=2,UV0=4,Tangent=8};};
struct VSWorldRenderer{enum class Topology{Triangles};struct Vertex{float x=0,y=0,z=0,nx=0,ny=0,nz=1,u=0,v=0,tx=1,ty=0,tz=0,tw=-1;};};
template<class T>using LLPointer=std::shared_ptr<T>;
struct Geometry{int mNumVertices=3;LLVector4a positions[3],normals[3],tangents[3],center;LLVector2 coords[3];LLVector4a*mPositions=positions,*mNormals=normals,*mTangents=tangents,*mCenter=&center;LLVector2*mTexCoords=coords;void*mWeights=nullptr;};
struct LLVolume{Geometry geometry;};
struct VSWorldAssets{struct Geometry{LLPointer<LLVolume>volume;};};
struct Impl{
METADATA
struct Face{int mesh=0;std::shared_ptr<const PickSurface>pickSurface;U64 poseGeneration=0;LLTextureEntry material;};
struct Renderer{bool accept=true;int next=0;bool publishMesh(int&mesh,const std::vector<VSWorldRenderer::Vertex>&,const std::vector<unsigned>&,VSWorldRenderer::Topology,unsigned){if(!accept)return false;mesh=++next;return true;}}renderer;
struct Budgets{VSResourceBudget primitiveBytes;Budgets(size_t n):primitiveBytes(n){}};std::shared_ptr<Budgets>budgets;
Impl(size_t n):budgets(std::make_shared<Budgets>(n)){}
};
struct Object{LLVector3 scale{2,3,4};bool flexible=false,dirty=true;LLPointer<LLVolume>volume;std::shared_ptr<const VSWorldAssets::Geometry>asset;std::shared_ptr<const VSResourceBudget::Reservation>primitiveResidency;std::vector<Impl::Face>faces,admittedFaces;};
void update(Impl*mImpl,Object&o,Geometry&geometry){
bool uploadDeferred=false;std::vector<Impl::Face>candidates=o.faces;const size_t count=1;LLVector3 position{7,8,9};LLQuaternion rotation=0;bool riggedOwner=bool(geometry.mWeights);const U64 poseGeneration=17;
for(auto&face:candidates){const int f=0;std::vector<VSWorldRenderer::Vertex>vertices(3);std::vector<unsigned>indices{0,1,2};unsigned attributes=7;
ADMISSION
PUBLISH
}
ATOMIC
}
int main(){Impl impl(100000);Object object;object.faces.resize(1);object.volume=std::make_shared<LLVolume>();object.primitiveResidency=impl.budgets->primitiveBytes.reserve(512);auto&g=object.volume->geometry;g.center.set(0,0,0);for(int n=0;n<3;n++){g.coords[n]={float(n),float(n+2)};g.positions[n].set(float(n),float(n+1),0);g.normals[n].set(0,0,1);g.tangents[n].set(1,0,0,-1);}
update(&impl,object,g);assert(object.admittedFaces.size()==1);auto old=object.admittedFaces[0].pickSurface;assert(old&&old->volume->geometry.coords[1].mV[0]==1&&old->volume->geometry.tangents[1][3]==-1);auto bytes=impl.budgets->primitiveBytes.used();assert(bytes==512+sizeof(Impl::PickSurface));int oldmesh=object.admittedFaces[0].mesh;
{Object rigged;rigged.faces.resize(1);rigged.volume=std::make_shared<LLVolume>();auto&geometry=rigged.volume->geometry;geometry.mWeights=&rigged;geometry.center.set(0,0,0);update(&impl,rigged,geometry);assert(rigged.admittedFaces[0].pickSurface->volumeGlobal&&rigged.admittedFaces[0].pickSurface->normalGlobal);}assert(impl.budgets->primitiveBytes.used()==bytes);
// Replacement always owns a NEW immutable volume, including sculpt/flexible generations.
object.volume=std::make_shared<LLVolume>();object.primitiveResidency=impl.budgets->primitiveBytes.reserve(512);auto&next=object.volume->geometry;next.coords[1]={100,200};next.center.set(0,0,0);impl.renderer.accept=false;update(&impl,object,next);assert(object.admittedFaces[0].mesh==oldmesh&&object.admittedFaces[0].pickSurface==old);assert(old->volume->geometry.coords[1].mV[0]==1);assert(impl.budgets->primitiveBytes.used()==bytes+512);
auto full=impl.budgets->primitiveBytes.reserve(100000-bytes-512);impl.renderer.accept=true;update(&impl,object,next);assert(object.admittedFaces[0].mesh==oldmesh&&object.admittedFaces[0].pickSurface==old);full.reset();
update(&impl,object,next);assert(object.admittedFaces[0].mesh!=oldmesh&&object.admittedFaces[0].pickSurface!=old);assert(object.admittedFaces[0].pickSurface->volume->geometry.coords[1].mV[0]==100);assert(old->volume->geometry.coords[1].mV[0]==1);old.reset();assert(impl.budgets->primitiveBytes.used()==bytes);object.faces.clear();object.admittedFaces.clear();object.volume.reset();object.primitiveResidency.reset();assert(impl.budgets->primitiveBytes.used()==0);
}
