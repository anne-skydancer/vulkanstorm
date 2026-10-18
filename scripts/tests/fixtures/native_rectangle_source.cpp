#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>
using S32=int;using U32=unsigned;
struct LLVector3{float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{}explicit LLVector3(const float*p){std::copy_n(p,3,mV);}bool isFinite()const{return std::all_of(mV,mV+3,[](float f){return std::isfinite(f);});}bool operator==(const LLVector3&b)const{return std::equal(mV,mV+3,b.mV);}};using LLVector3d=LLVector3;
template<class T>T llmin(T a,T b){return std::min(a,b);}template<class T>T llmax(T a,T b){return std::max(a,b);}
struct LLMatrix4{float a[3][4]{{1,0,0,0},{0,1,0,0},{0,0,1,0}};};
LLVector3 operator*(const LLVector3&p,const LLMatrix4&m){LLVector3 r;for(int i=0;i<3;++i)r.mV[i]=m.a[i][0]*p.mV[0]+m.a[i][1]*p.mV[1]+m.a[i][2]*p.mV[2]+m.a[i][3];return r;}
struct LLVector4a{float v[4]{};LLVector4a()=default;LLVector4a(float x,float y,float z):v{x,y,z,0}{}const float*getF32ptr()const{return v;}};
struct LLMatrix4a{LLMatrix4 m;void loadu(const LLMatrix4&matrix){m=matrix;}void affineTransform(const LLVector4a&s,LLVector4a&r)const{auto p=LLVector3(s.getF32ptr())*m;r=LLVector4a(p.mV[0],p.mV[1],p.mV[2]);}};
struct LLVolumeFace{std::vector<LLVector4a>storage;LLVector4a*mPositions=nullptr;LLVector4a extents[2]{{-.5f,-.5f,-.5f},{.5f,.5f,.5f}};LLVector4a*mExtents=extents;S32 mNumVertices=0;};
struct LLVolume{std::vector<LLVolumeFace>faces;int getNumVolumeFaces()const{return int(faces.size());}const LLVolumeFace&getVolumeFace(int n)const{return faces[n];}};
KERNEL
REFERENCE
struct LLCamera{int pointInFrustum(const LLVector3&p)const{return std::abs(p.mV[0])<=2 &&std::abs(p.mV[1])<=2 &&std::abs(p.mV[2])<=2;}};
constexpr int LL_PCODE_VOLUME=9;
struct VSWorldScene{
 struct ObjectHandle{int host=1;U32 local=1,generation=1;};
 struct Focus{LLMatrix4 renderMatrix,assetToAgentMatrix;};
 struct Impl{struct Object{int pcode=9;U32 generation=1;bool flexible=false;std::shared_ptr<LLVolume>volume;};int host=1;bool links=true;std::map<U32,Object>objects;bool linksCurrent(const Object&)const{return links;}};
 std::unique_ptr<Impl>mImpl=std::make_unique<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;bool live=true;Focus frame;
 bool current(ObjectHandle t)const{if(!live)return false;if(t.host!=mImpl->host){const auto i=mNeighbours.find(t.host);return i!=mNeighbours.end()&&i->second->current(t);}const auto i=mImpl->objects.find(t.local);return i!=mImpl->objects.end()&&i->second.generation==t.generation;}
 std::optional<Focus>focusObject(ObjectHandle,const LLVector3d&)const{return frame;}
 bool objectVertsVisible(const ObjectHandle&,const LLCamera&,bool,const LLVector3d&)const;
 std::optional<std::array<LLVector3,2>>sourceObjectBounds(const ObjectHandle&,const LLVector3d&)const;
};
VERTS
BOUNDS
int main(){auto volume=std::make_shared<LLVolume>();volume->faces.resize(3);for(auto&face:volume->faces){for(int i=0;i<32;++i)face.storage.emplace_back((i%7-3)*.2f,(i%11-5)*.2f,(i%13-6)*.2f);face.mPositions=face.storage.data();face.mNumVertices=int(face.storage.size());}
 LLCamera camera;for(int c=0;c<4096;++c){LLMatrix4 relative,render;for(int i=0;i<3;++i){relative.a[i][i]=float(c%11+1)*.3f;relative.a[i][3]=float(c%19-9)*.1f;render.a[i][3]=float(c%23-11)*.2f;}for(bool active:{false,true})for(bool all:{false,true}){auto predicate=[&](const LLVector3&p){return camera.pointInFrustum(p)>0;};assert(vs_volume_vertices_visible(*volume,relative,render,active,all,predicate)==originalVisible(*volume,relative,render,active,all,predicate));}}
 VSWorldScene scene;scene.mImpl->objects[1].volume=volume;scene.frame.assetToAgentMatrix.a[0][0]=8;assert(!scene.objectVertsVisible({},camera,true,{}));assert(scene.objectVertsVisible({},camera,false,{}));scene.mImpl->objects[1].flexible=true;assert(scene.objectVertsVisible({},camera,true,{}));auto b=scene.sourceObjectBounds({},{});assert(b&&(*b)[0]==LLVector3(-.5f,-.5f,-.5f)&&(*b)[1]==LLVector3(.5f,.5f,.5f));scene.mImpl->objects[1].flexible=false;b=scene.sourceObjectBounds({},{});assert(b&&(*b)[0].mV[0]==-4&&(*b)[1].mV[0]==4);
 assert(!scene.objectVertsVisible({1,1,2},camera,false,{}));scene.mImpl->links=false;assert(!scene.sourceObjectBounds({},{}));scene.mImpl->links=true;scene.live=false;assert(!scene.sourceObjectBounds({},{}));scene.live=true;scene.mImpl->objects[1].volume.reset();assert(!scene.objectVertsVisible({},camera,true,{}));assert(!scene.sourceObjectBounds({},{}));
 auto neighbour=std::make_unique<VSWorldScene>();neighbour->mImpl->host=3;neighbour->mImpl->objects[1].volume=volume;scene.mNeighbours[3]=std::move(neighbour);assert(scene.sourceObjectBounds({3,1,1},{}));assert(scene.objectVertsVisible({3,1,1},camera,true,{}));
}
