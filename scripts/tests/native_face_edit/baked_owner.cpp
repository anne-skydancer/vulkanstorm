// Actual native avatar source, hierarchy resolver, and public current-owner queries.
#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <set>
#include <cstdint>
#include <cstddef>
using F32=float;using S32=int;using U32=unsigned;using U64=unsigned long long;using U8=unsigned char;struct LLUUID{int value=0;LLUUID()=default;LLUUID(int v):value(v){};operator int()const{return value;}bool isNull()const{return value==0;}bool notNull()const{return value!=0;}std::string asString()const{return std::to_string(value);}};constexpr int IMG_DEFAULT=99;
template<class T>using LLPointer=std::shared_ptr<T>;template<class T>T llmax(T a,T b){return std::max(a,b);}
namespace LLAvatarAppearanceDefines{enum EBakedTextureIndex{BAKED_HEAD=0,BAKED_UPPER=1,BAKED_LEFT_ARM=6,BAKED_NUM_INDICES=11};struct LLAvatarAppearanceDictionary{static bool isBakedImageId(int id){return id==100||id==101;}static EBakedTextureIndex assetIdToBakedTextureIndex(int id){return id==100?BAKED_HEAD:BAKED_UPPER;}};}
namespace VSWorldContracts{enum class AlphaMode{Opaque,Mask,Blend};struct Image{unsigned width=0,height=0,components=4;AlphaMode alpha=AlphaMode::Opaque;bool alphaMask=false;};}
struct GPUImage{int generation=0;unsigned width=0,height=0;VSWorldContracts::AlphaMode alpha=VSWorldContracts::AlphaMode::Opaque;};namespace VSUIRenderer{using Image=std::shared_ptr<GPUImage>;VSWorldContracts::Image imageDescription(const Image&i){return {i->width,i->height,4,i->alpha,false};}std::array<unsigned,2>imageExtent(const Image&i){return {i->width,i->height};}}
struct LLImageRaw{int components=4,width=1,height=1;std::vector<unsigned char>pixels{255,255,255,255};int getComponents()const{return components;}int getWidth()const{return width;}int getHeight()const{return height;}const unsigned char*getData()const{return pixels.data();}};struct Image{bool failed=false;VSUIRenderer::Image image;LLPointer<LLImageRaw>raw;};std::map<int,Image>images;int rawQueries=0;int requests=0;std::function<void()>requestHook;
void vs_request_world_image(int,int){++requests;if(requestHook)requestHook();}bool vs_ui_image_failed(int id){return images[id].failed;}VSUIRenderer::Image vs_ui_image_snapshot(int id){return images[id].image;}LLPointer<LLImageRaw>vs_ui_image_raw(int id){++rawQueries;return images[id].raw;}LLUUID vs_native_image_id(int id){return id;}
struct Appearance{bool editing=false;bool isEditing(){return editing;}};struct Session{Appearance appearanceValue;Appearance*appearance(){return &appearanceValue;}};
class VSWorldAvatar{public:
 struct BakedTexture{bool present=false,missing=false;LLUUID asset=0;VSUIRenderer::Image image;LLPointer<LLImageRaw>raw;U64 generation=0;explicit operator bool()const{return present;}};
 struct LocalBake{VSUIRenderer::Image image;LLPointer<LLImageRaw>color;U64 generation=0;};
 struct Base{virtual bool isEditingAppearance()const=0;virtual~Base()=default;};
 struct Impl:Base{bool self=true,preview=false;std::weak_ptr<Session>appearanceSession;std::function<bool()>appearanceCurrent;struct Data{int mTextureIndex=0;};std::array<Data,11>mBakedTextureDatas;struct TE{int id=0;int getID()const{return id;}};struct Textures{std::array<TE,11>entries;const TE*getTE(U8 i)const{return &entries.at(i);}}textures;bool isTextureDefined(int i,int)const{return textures.getTE(U8(i))->id!=0;}EDITING};
 std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::array<LocalBake,11>composites;bool hud=false;int bakeQueries=0;
 LocalBake localBake(unsigned i)const{return composites.at(i);}bool hudAttachment(unsigned)const{return hud;}
 BakedTexture bakedTexture(unsigned,S32,bool=true)const;
};
AVATAR
constexpr int LL_PCODE_VOLUME=9,LL_PCODE_LEGACY_AVATAR=47;namespace LLExtendedMeshParams{constexpr unsigned ANIMATED_MESH_ENABLED_FLAG=1;}
struct Region{int maxBakes=11;bool live=true;bool current()const{return live;}int getRegionMaxBakes()const{return maxBakes;}};std::map<int,Region>regions;std::optional<Region>vs_region_context(int host){if(!regions.count(host))return {};return regions[host];}
KERNEL
class VSWorldScene{public:
 struct ObjectHandle{int host=1;unsigned local=1;bool live=true;};
 struct ResolvedTexture{bool present=false,missing=false;LLUUID asset=0;VSUIRenderer::Image image;LLPointer<LLImageRaw>raw;U64 generation=0;explicit operator bool()const{return present;}};
 struct Media{int id=500;LLUUID texture(unsigned)const{return id;}};
 struct Object{int id=0;unsigned parent=0;int pcode=LL_PCODE_VOLUME;unsigned extendedMeshFlags=0;unsigned char attachment=0;unsigned long long revision=1;bool linked=true;std::shared_ptr<Media>media;struct Material{int id=0;bool media=false;bool hasMedia()const{return media;}int getID()const{return id;}};struct Face{Material material;};std::vector<Face>faces;};
 struct Resources{std::map<std::string,VSUIRenderer::Image>gpu;std::map<std::string,LLPointer<LLImageRaw>>cpu;U64 generation(const std::string&key)const{const auto i=gpu.find(key);return i==gpu.end()?0:i->second->generation;}VSUIRenderer::Image image(const std::string&key)const{const auto i=gpu.find(key);return i==gpu.end()?nullptr:i->second;}LLPointer<LLImageRaw>rawImage(const std::string&key)const{const auto i=cpu.find(key);return i==cpu.end()?nullptr:i->second;}};
 struct Impl{int host=1;unsigned long long nextRevision=1;std::map<unsigned,Object>objects;std::map<int,std::unique_ptr<VSWorldAvatar>>avatars;Resources resources;bool linksCurrent(const Object&o)const{return o.linked;}TEXTURESOURCE
COSTTEXTURE
COSTFUNCTION
};
 std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<int,std::shared_ptr<VSWorldScene>>mNeighbours;bool live=true;
 bool current(const ObjectHandle&h)const{return live&&h.live&&(h.host==mImpl->host?mImpl->objects.count(h.local):mNeighbours.count(h.host)&&mNeighbours.at(h.host)->current(h));}
 std::function<bool()>guard(const ObjectHandle& h)const{return [this,h]{return current(h);};}
 ResolvedTexture resolveTexture(const ObjectHandle&,const LLUUID&,bool=true)const;ResolvedTexture diffuseTexture(const ObjectHandle&,U32,bool=true)const;
};
PUBLIC
int main(){regions[1]={};VSWorldScene scene;VSWorldScene::ObjectHandle token;auto&objects=scene.mImpl->objects;objects[1].id=1;objects[1].parent=2;objects[1].faces={{{100,false}}};objects[2].id=2;objects[2].parent=3;objects[2].attachment=1;objects[3].id=444;objects[3].pcode=LL_PCODE_LEGACY_AVATAR;
auto avatar=std::make_unique<VSWorldAvatar>();auto*av=avatar.get();scene.mImpl->avatars[444]=std::move(avatar);av->mImpl->textures.entries[0].id=714;images[714]={false,std::make_shared<GPUImage>(GPUImage{7}),std::make_shared<LLImageRaw>()};images[99]={false,std::make_shared<GPUImage>(GPUImage{2}),{}};
VSWorldScene::Impl::TextureSource metadata{scene.mImpl,objects[1],scene.guard(token)};
const auto oldRequests=requests,oldRaw=rawQueries;
auto cpu=metadata.resolve(100,true,false,false);assert(cpu.asset==714 && cpu.image && !cpu.raw);assert(requests==oldRequests && rawQueries==oldRaw);
VSWorldScene::Impl::CostTextures costs;
costs.insert(VSWorldScene::Impl::CostTexture{{true,false,714,images[714].image,{},7},{}});costs.insert(VSWorldScene::Impl::CostTexture{{true,false,714,{},{},0},{}});assert(costs.size()==1);
auto localRaw=std::make_shared<LLImageRaw>();localRaw->width=256;localRaw->height=128;localRaw->pixels.resize(256*128*4,255);localRaw->pixels[3]=127;
VSWorldScene::ResolvedTexture local{true,false,0,{},localRaw,17};costs.insert(VSWorldScene::Impl::CostTexture{local,{}});costs.insert(VSWorldScene::Impl::CostTexture{local,{}});assert(costs.size()==2);
const auto found=costs.find({local,{}});assert(found!=costs.end()&&found->description()->alpha==VSWorldContracts::AlphaMode::Blend);assert(scene.mImpl->textureCost(*found)==304.f);
auto raw2=std::make_shared<LLImageRaw>();raw2->pixels[3]=0;costs.insert(VSWorldScene::Impl::CostTexture{{true,false,0,{},raw2,1},{}});assert(costs.size()==3);assert(costs.find({{true,false,0,{},raw2,1},{}})->description()->alpha==VSWorldContracts::AlphaMode::Mask);
assert(scene.mImpl->textureCost({{true,false,912,{},{},0},{}})==256.f);
auto result=scene.diffuseTexture(token,0);assert(result&&result.asset==714&&result.image==images[714].image&&result.raw==images[714].raw);assert(objects[1].faces[0].material.id==100);objects[2].extendedMeshFlags=1;result=scene.resolveTexture(token,100);assert(result.asset==100);objects[2].extendedMeshFlags=0;av->hud=true;assert(scene.resolveTexture(token,100).asset==100);av->hud=false;images[714].failed=true;assert(scene.resolveTexture(token,100).asset==99);images[714].failed=false;regions[1].maxBakes=0;assert(scene.resolveTexture(token,100).asset==99);regions[1].maxBakes=11;
auto session=std::make_shared<Session>();int epoch=1;av->mImpl->appearanceSession=session;av->mImpl->appearanceCurrent=[&]{return epoch==1;};session->appearanceValue.editing=true;av->composites[0]={std::make_shared<GPUImage>(GPUImage{31}),std::make_shared<LLImageRaw>(),31};result=scene.resolveTexture(token,100);assert(result.image==av->composites[0].image&&result.asset==0&&result.generation==31);epoch=2;assert(scene.resolveTexture(token,100).asset==714);epoch=1;av->mImpl->preview=true;assert(scene.resolveTexture(token,100).asset==714);av->mImpl->preview=false;session->appearanceValue.editing=false;
// Retire/revise during the real image request; no fallback request or stale resource is used.
requestHook=[&]{++objects[1].revision;};const auto requestsBefore=requests;assert(!scene.resolveTexture(token,100));assert(requests==requestsBefore+1);requestHook={};
VSWorldScene::Impl::TextureSource cached{scene.mImpl,objects[1],scene.guard(token)};assert(cached.resolve(100).asset==714);
objects[4].id=4;objects[4].parent=3;objects[4].extendedMeshFlags=1;objects[2].parent=4;++scene.mImpl->nextRevision;assert(cached.resolve(100).asset==100);objects[2].parent=3;++scene.mImpl->nextRevision;
objects[1].linked=false;assert(!scene.resolveTexture(token,100));objects[1].linked=true;token.live=false;const auto before=requests;assert(!scene.resolveTexture(token,100)&&requests==before);token.live=true;
const auto rawBefore=rawQueries;result=scene.resolveTexture(token,100,false);assert(result.present && result.asset==714 && !result.raw && rawQueries==rawBefore);
objects[1].media=std::make_shared<VSWorldScene::Media>();objects[1].faces[0].material.media=true;scene.mImpl->resources.gpu["media:500"]=std::make_shared<GPUImage>(GPUImage{81});result=scene.diffuseTexture(token,0);assert(result.asset==500&&result.image->generation==81);objects[1].media.reset();assert(scene.diffuseTexture(token,0).asset==714);
scene.mImpl->avatars.clear();assert(scene.resolveTexture(token,100).asset==100);
auto neighbour=std::make_shared<VSWorldScene>();neighbour->mImpl->host=2;neighbour->mImpl->objects[1].id=42;scene.mNeighbours[2]=neighbour;VSWorldScene::ObjectHandle other{2,1,true};assert(scene.resolveTexture(other,17).asset==17);neighbour->live=false;assert(!scene.resolveTexture(other,17));scene.live=false;assert(!scene.diffuseTexture(token,0));
}
