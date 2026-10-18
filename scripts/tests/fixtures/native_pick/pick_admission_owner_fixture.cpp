#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <map>
#include <set>
#include <functional>
#include <cstring>
#include <array>
using F32=float;using U32=unsigned;using S32=int;
constexpr int VX=0,VY=1,VZ=2;constexpr float F32_MAX=std::numeric_limits<float>::max();
template<class T>T llmax(T a,T b){return std::max(a,b);}template<class T>T llmin(T a,T b){return std::min(a,b);}float llabs(float v){return std::abs(v);}
struct LLQuaternion{float a=0;};LLQuaternion operator~(LLQuaternion q){return {-q.a};}
struct LLVector3{static const LLVector3 zero;float mV[3]{};LLVector3(const float*v):mV{v[0],v[1],v[2]}{};void set(const float*v){for(int n=0;n<3;n++)mV[n]=v[n];}void scaleVec(LLVector3 b){for(int n=0;n<3;n++)mV[n]*=b.mV[n];}LLVector3 scaledVec(LLVector3 b)const{b.scaleVec(*this);return b;}LLVector3()=default;LLVector3(float a,float b,float c):mV{a,b,c}{};void setVec(LLVector3 v){*this=v;}void clamp(float a,float b){for(float&v:mV)v=std::clamp(v,a,b);}void abs(){for(float&v:mV)v=std::abs(v);}float lengthSquared()const{return mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2];}float magVec()const{return std::sqrt(lengthSquared());}float normalize(){float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);if(n>0)for(float&v:mV)v/=n;return n;}void rotVec(LLQuaternion q){float a=mV[0],b=mV[1];mV[0]=a*std::cos(q.a)-b*std::sin(q.a);mV[1]=a*std::sin(q.a)+b*std::cos(q.a);}LLVector3&operator-=(LLVector3 b){for(int n=0;n<3;n++)mV[n]-=b.mV[n];return *this;}};
LLVector3 operator+(LLVector3 a,LLVector3 b){return {a.mV[0]+b.mV[0],a.mV[1]+b.mV[1],a.mV[2]+b.mV[2]};}LLVector3 operator-(LLVector3 a,LLVector3 b){a-=b;return a;}LLVector3 operator*(float s,LLVector3 a){return {s*a.mV[0],s*a.mV[1],s*a.mV[2]};}float operator*(LLVector3 a,LLVector3 b){return a.mV[0]*b.mV[0]+a.mV[1]*b.mV[1]+a.mV[2]*b.mV[2];}LLVector3 operator/(LLVector3 a,float s){return (1/s)*a;}LLVector3 operator*(LLVector3 a,float s){return s*a;}using LLVector3d=LLVector3;
float dist_vec(LLVector3 a,LLVector3 b){auto v=a-b;return std::sqrt(v*v);}float clamp_rescale(float v,float a,float b,float c,float d){return c+std::clamp((v-a)/(b-a),0.f,1.f)*(d-c);}LLVector3 lerp(LLVector3 a,LLVector3 b,float t){return (1-t)*a+t*b;}
struct LLMatrix4{LLVector3 axes[3];LLVector3 getFwdRow4()const{return axes[0];}LLVector3 getLeftRow4()const{return axes[1];}LLVector3 getUpRow4()const{return axes[2];}};
struct LLBBox{LLVector3 center,size;bool containsPointAgent(LLVector3 p)const{p-=center;for(int n=0;n<3;n++)if(std::abs(p.mV[n])>size.mV[n]/2)return false;return true;}};


const LLVector3 LLVector3::zero{};bool finiteVector(LLVector3 p){return std::isfinite(p.mV[0])&&std::isfinite(p.mV[1])&&std::isfinite(p.mV[2]);}
LLVector3 operator*(LLVector3 a,LLQuaternion q){a.rotVec(q);return a;}
using U64=unsigned long long;
constexpr int LL_PCODE_VOLUME=2,LL_PCODE_LEGACY_AVATAR=3,LL_PCODE_LEGACY_TREE=4,LL_PCODE_TREE_NEW=5,LL_PCODE_LEGACY_GRASS=6,CLICK_ACTION_IGNORE=9,FLAGS_USE_PHYSICS=1,FLAGS_HANDLE_TOUCH=2;
bool LLLineSegmentBoxIntersect(LLVector3,LLVector3,LLVector3,LLVector3){return true;}
bool linesegment_tetrahedron(LLVector3,LLVector3,LLVector3,LLVector3,LLQuaternion,LLVector3&,LLVector3&){return false;}
struct LLVector2{float mV[2]{};LLVector2()=default;LLVector2(float a,float b):mV{a,b}{};LLVector2(const float*p):mV{p[0],p[1]}{}};
struct LLMaterial{enum{DIFFUSE_ALPHA_MODE_NONE=0,DIFFUSE_ALPHA_MODE_BLEND=1,DIFFUSE_ALPHA_MODE_MASK=2,DIFFUSE_ALPHA_MODE_EMISSIVE=3};int mode=1,cutoff=128;int getDiffuseAlphaMode()const{return mode;}int getAlphaMaskCutoff()const{return cutoff;}};
struct TE{struct Ptr{const LLMaterial*p;bool notNull()const{return p;}const LLMaterial*operator->()const{return p;}};LLMaterial material;bool hasMaterial=false;struct Color{float mV[4]{1,1,1,1};}color;Ptr getMaterialParams()const{return {hasMaterial?&material:nullptr};}const Color&getColor()const{return color;}};
struct LLUUID{unsigned char mData[16]{};LLUUID()=default;LLUUID(unsigned n){mData[0]=n;}bool operator==(const LLUUID&o)const{return !std::memcmp(mData,o.mData,16);}bool operator<(const LLUUID&o)const{return std::memcmp(mData,o.mData,16)<0;}};
struct VSUIRenderer{struct Texture{std::vector<std::uint8_t>pixels;std::array<unsigned,2>extent{2,2};mutable unsigned reads=0;};using Image=std::shared_ptr<const Texture>;static auto imageExtent(const Image&i){return i->extent;}static const auto&imagePixels(const Image&i){++i->reads;return i->pixels;}};
struct VSWorldRenderer{
struct Vertex{float x,y,z,nx,ny,nz,u,v;float tx=1,ty=0,tz=0,tw=-1;};enum class Topology{Triangles,Lines,Points};struct Geometry{std::vector<Vertex>cpuVertices;std::vector<unsigned>cpuIndices;std::array<std::array<float,3>,2>bounds;Topology topology=Topology::Triangles;};using Mesh=std::shared_ptr<const Geometry>;
struct SurfaceHit{float fraction=1;std::array<float,3>point{},normal{},rawNormal{},barycentric{};std::array<float,2>uv{};std::array<float,4>tangent{};std::array<unsigned,3>triangle{};bool triangleKnown=false;};
struct Packet{Mesh mesh;std::array<unsigned char,16>objectIdentity{};unsigned objectGeneration=1,faceGeneration=1,faceIndex=0,terrainKey=0;bool hud=false,background=false,terrain=false;int gltfNode=-1,gltfPrimitive=-1;std::array<float,4>regionOffset{};};
static bool exact(const Mesh&,const std::array<float,3>&,const std::array<float,3>&,SurfaceHit&,float=0,const std::function<bool(const SurfaceHit&)>& = {});
static inline unsigned calls=0;static bool intersect(const Mesh&m,const std::array<float,3>&a,const std::array<float,3>&b,SurfaceHit&hit,float r=0,const std::function<bool(const SurfaceHit&)>&accept={}){++calls;return exact(m,a,b,hit,r,accept);}static auto bounds(const Mesh&m){return m->bounds;}
};
RENDERER
POLICY
struct Avatar{float z=.5f;unsigned calls=0;std::optional<VSWorldRenderer::SurfaceHit>collisionPick(LLVector3 a,LLVector3 b,bool renderSelf){++calls;if(!renderSelf||z>b.mV[2]&&z>a.mV[2]||z<b.mV[2]&&z<a.mV[2])return {};VSWorldRenderer::SurfaceHit hit;hit.point={0,0,z};hit.normal={0,0,1};hit.fraction=(z-a.mV[2])/(b.mV[2]-a.mV[2]);return hit;}};
struct VSWorldScene{
struct ObjectHandle{int host=1;LLUUID id;unsigned local=0,generation=1;};struct FaceHandle{ObjectHandle object;unsigned face=0,generation=1;};
struct Pick{FaceHandle face;LLVector3 position,normal;std::array<float,2>uv{-1,-1},st{-1,-1};std::array<float,4>tangent{};std::array<int,2>xy{-1,-1};int gltfNode=-1,gltfPrimitive=-1;float fraction=1;bool hud=false,land=false,avatar=false,surfaceKnown=false;};
struct PickOptions{bool transparent=false,rigged=false,unselectable=false,reflectionProbe=false;std::function<bool()>buildVisible;std::function<bool(const ObjectHandle&)>canSelect;std::function<bool()>renderSelfAvatar;};
struct Impl{
struct Raw{bool rigged=false;};struct Face{VSWorldRenderer::Mesh mesh;std::shared_ptr<Raw>pickSurface=std::make_shared<Raw>();TE publishedMaterial;};
struct Object{LLUUID id;unsigned generation=1,revision=1,parent=0,pcode=LL_PCODE_VOLUME,updateFlags=0;bool reflectionProbe=false,selectable=true,linked=true,beacon=false;int clickAction=0;LLQuaternion rotation;std::vector<Face>admittedFaces;};
struct Index{std::multimap<LLUUID,unsigned>v;auto identities(const LLUUID&id)const{return v.equal_range(id);}}objectIndex;
struct Visible{VSWorldRenderer::Packet packet;std::weak_ptr<const VSWorldRenderer::Geometry>mesh;std::weak_ptr<const VSUIRenderer::Texture>image;};
struct Ground{VSWorldRenderer::Mesh mesh;};
std::vector<Visible>visiblePackets;std::map<unsigned,Object>objects;std::map<unsigned,Ground>ground;std::map<LLUUID,std::shared_ptr<Avatar>>avatars;bool retired=false;unsigned incarnation=1;int host=1;LLUUID agent{99};
bool linksCurrent(const Object&o)const{return o.linked;}bool highlightedOrBeacon(const Object&o)const{return o.beacon;}static std::optional<Pick>surfacePick(const Face&,const VSWorldRenderer::SurfaceHit&hit,const VSUIRenderer::Image&){Pick p;p.uv={hit.uv[0],1-hit.uv[1]};p.surfaceKnown=true;p.normal=LLVector3(hit.normal.data());return p;}
};
std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;
std::optional<ObjectHandle>object(int host,unsigned local)const{auto f=mImpl->objects.find(local);if(host!=mImpl->host||mImpl->retired||f==mImpl->objects.end())return {};return ObjectHandle{host,f->second.id,local,f->second.generation};}
bool current(const FaceHandle&token)const{auto f=mImpl->objects.find(token.object.local);return !mImpl->retired&&f!=mImpl->objects.end()&&f->second.generation==token.object.generation&&f->second.revision==token.generation&&token.face<f->second.admittedFaces.size();}
struct Admission{bool attachment=false;};std::optional<Admission>objectAdmission(ObjectHandle token)const{auto f=mImpl->objects.find(token.local);return f==mImpl->objects.end()?std::nullopt:std::optional<Admission>{{f->second.parent!=0}};}
std::optional<Pick>pick(const LLVector3&,const LLVector3&,const PickOptions&,float=0,bool=false)const;
};
PICK
struct Fixture{VSWorldScene world;std::vector<VSWorldRenderer::Mesh>meshes;std::vector<std::shared_ptr<VSUIRenderer::Texture>>images;
void add(unsigned id,float z){auto mesh=std::make_shared<VSWorldRenderer::Geometry>();mesh->cpuVertices={{-1,-1,z,0,0,1,0,0},{1,-1,z,0,0,1,1,0},{0,1,z,0,0,1,.5f,1}};mesh->cpuIndices={0,1,2};mesh->bounds={{{-1,-1,z},{1,1,z}}};meshes.push_back(mesh);auto image=std::make_shared<VSUIRenderer::Texture>();image->pixels=std::vector<std::uint8_t>(16,255);images.push_back(image);auto&object=world.mImpl->objects[id];object.id=LLUUID{id};object.admittedFaces.push_back({mesh});world.mImpl->objectIndex.v.emplace(object.id,id);VSWorldRenderer::Packet packet;std::copy_n(object.id.mData,16,packet.objectIdentity.begin());world.mImpl->visiblePackets.push_back({packet,mesh,image});}
auto pick(const VSWorldScene::PickOptions&o){return world.pick({0,0,2},{0,0,-2},o);}
};
int main(){Fixture f;f.add(1,1);f.add(2,0);auto&front=f.world.mImpl->objects[1];VSWorldScene::PickOptions o;unsigned permissions=0,builds=0;o.canSelect=[&](const auto&){++permissions;return true;};o.buildVisible=[&]{++builds;return false;};auto p=f.pick(o);assert(p&&p->face.object.local==1);auto baseline=VSWorldRenderer::calls;
f.world.mImpl->visiblePackets.insert(f.world.mImpl->visiblePackets.begin()+1,f.world.mImpl->visiblePackets[0]);permissions=0;f.pick(o);assert(permissions==2);f.world.mImpl->visiblePackets.erase(f.world.mImpl->visiblePackets.begin()+1);baseline=VSWorldRenderer::calls;
front.admittedFaces[0].pickSurface->rigged=true;permissions=0;p=f.pick(o);assert(p&&p->face.object.local==2);assert(permissions==2&&builds==0);assert(VSWorldRenderer::calls-baseline==1);o.rigged=true;p=f.pick(o);assert(p->face.object.local==1);front.admittedFaces[0].pickSurface->rigged=false;o.rigged=false;
o.canSelect=[&](const auto&t){++permissions;return t.local!=1;};baseline=VSWorldRenderer::calls;p=f.pick(o);assert(p->face.object.local==2&&VSWorldRenderer::calls-baseline==1);o.unselectable=true;permissions=0;p=f.pick(o);assert(p->face.object.local==1&&permissions==0);o.unselectable=false;o.canSelect=[&](const auto&){++permissions;return true;};
front.linked=false;permissions=0;baseline=VSWorldRenderer::calls;p=f.pick(o);assert(p->face.object.local==2&&permissions==1&&VSWorldRenderer::calls-baseline==1);front.linked=true;
front.selectable=false;o.unselectable=true;baseline=VSWorldRenderer::calls;p=f.pick(o);assert(p->face.object.local==2&&VSWorldRenderer::calls-baseline==1);front.selectable=true;o.unselectable=false;
front.reflectionProbe=true;permissions=0;p=f.pick(o);assert(p->face.object.local==2&&permissions==1);o.reflectionProbe=true;front.admittedFaces[0].publishedMaterial.color.mV[3]=0;f.images[0]->pixels=std::vector<std::uint8_t>(16,0);p=f.pick(o);assert(p->face.object.local==1);front.reflectionProbe=false;o.reflectionProbe=false;front.beacon=true;p=f.pick(o);assert(p->face.object.local==1);front.beacon=false;p=f.pick(o);assert(p->face.object.local==2);front.admittedFaces[0].publishedMaterial.color.mV[3]=1;
front.clickAction=CLICK_ACTION_IGNORE;baseline=VSWorldRenderer::calls;p=f.pick(o);assert(p->face.object.local==2&&VSWorldRenderer::calls-baseline==1);o.buildVisible=[]{return true;};o.transparent=true;p=f.pick(o);assert(p->face.object.local==1);front.clickAction=0;o.transparent=false;
p=f.pick(o);assert(p->face.object.local==2);front.admittedFaces[0].publishedMaterial.hasMaterial=true;front.admittedFaces[0].publishedMaterial.material.mode=LLMaterial::DIFFUSE_ALPHA_MODE_NONE;auto reads=f.images[0]->reads;p=f.pick(o);assert(p->face.object.local==1&&f.images[0]->reads>reads);front.admittedFaces[0].publishedMaterial.hasMaterial=false;
// Original imported-scene traversal ignores volume selection/alpha policies.
f.world.mImpl->visiblePackets[0].packet.gltfNode=1;o.canSelect=[&](const auto&){assert(false);return false;};o.unselectable=true;p=f.pick(o);assert(p->face.object.local==1&&p->gltfNode==1);f.world.mImpl->visiblePackets[0].packet.gltfNode=-1;o.canSelect=[](const auto&){return true;};
// Rejected or retired generations never reach permissions, pixels, or triangles.
f.world.mImpl->visiblePackets[0].packet.faceGeneration=0;baseline=VSWorldRenderer::calls;reads=f.images[0]->reads;p=f.pick(o);assert(p->face.object.local==2&&VSWorldRenderer::calls-baseline==1&&reads==f.images[0]->reads);f.world.mImpl->visiblePackets[0].packet.faceGeneration=1;
f.world.mImpl->retired=true;baseline=VSWorldRenderer::calls;assert(!f.pick(o));assert(VSWorldRenderer::calls==baseline);f.world.mImpl->retired=false;
// Avatar body/correct actor ownership runs after attachments, with 0.1m priority.
Fixture a;a.add(1,.1f);a.add(2,0);a.world.mImpl->objects[1].parent=2;a.world.mImpl->objects[2].pcode=LL_PCODE_LEGACY_AVATAR;auto av=std::make_shared<Avatar>();a.world.mImpl->avatars.emplace(LLUUID{2},av);o.canSelect=[](const auto&){return true;};av->z=.15f;p=a.pick(o);assert(p&&!p->avatar&&p->face.object.local==1);av->z=.5f;p=a.pick(o);assert(p&&p->avatar&&p->face.object.local==2);a.world.mImpl->objects[2].revision++;auto count=av->calls;p=a.pick(o);assert(p&&!p->avatar&&av->calls==count);
}
