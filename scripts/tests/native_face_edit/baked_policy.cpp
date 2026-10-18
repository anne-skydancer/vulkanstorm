#include <cassert>
#include <map>
#include <string>
#include <vector>
using LLUUID=int;constexpr LLUUID IMG_DEFAULT=99;
std::vector<std::string>trace;
namespace LLAvatarAppearanceDefines{enum EBakedTextureIndex{BAKED_HEAD=0,BAKED_UPPER=1,BAKED_LEFT_ARM=6,BAKED_NUM_INDICES=11};struct LLAvatarAppearanceDictionary{static bool isBakedImageId(int id){trace.push_back("magic");return id==100||id==101;}static EBakedTextureIndex assetIdToBakedTextureIndex(int id){trace.push_back("index");return id==100?BAKED_HEAD:BAKED_UPPER;}};}
constexpr int FTT_DEFAULT=0;
namespace LLGLTexture{constexpr int BOOST_NONE=0;}
struct LLViewerTexture{int id=0;bool missing=false;enum{LOD_TEXTURE=0};bool isMissingAsset()const{trace.push_back("missing");return missing;}};
struct LLViewerTextureManager{static LLViewerTexture*getFetchedTexture(int id,int,bool,int,int){trace.push_back("fetch:"+std::to_string(id));static std::map<int,LLViewerTexture>images;auto&v=images[id];v.id=id;return &v;}};
struct LLVOAvatar{LLViewerTexture*image=nullptr;LLViewerTexture*getBakedTexture(int index){trace.push_back("bake:"+std::to_string(index));return image;}};
struct LLViewerObject{LLViewerObject*root=nullptr;LLVOAvatar*avatar=nullptr;bool animated=false,hud=false;LLViewerObject*getRootEdit(){trace.push_back("root");return root;}bool isAnimatedObject(){trace.push_back("animated");return animated;}LLVOAvatar*getAvatar(){trace.push_back("avatar");return avatar;}bool isHUDAttachment(){trace.push_back("hud");return hud;}LLViewerTexture*getBakedTextureForMagicId(const LLUUID&);};
ORIGINAL
KERNEL
struct Service{using Image=LLViewerTexture*;Image fetch(int id)const{return LLViewerTextureManager::getFetchedTexture(id,FTT_DEFAULT,true,LLGLTexture::BOOST_NONE,LLViewerTexture::LOD_TEXTURE);}Image bake(LLVOAvatar*av,int index)const{return av->getBakedTexture(index);}bool missing(Image image)const{return image->isMissingAsset();}};
int main(){int cases=0;for(int raw:{0,17,100,101})for(int rootPresent=0;rootPresent<2;rootPresent++)for(int animated=0;animated<2;animated++)for(int avPresent=0;avPresent<2;avPresent++)for(int hud=0;hud<2;hud++)for(int bake=0;bake<3;bake++){
LLViewerTexture image{714,bake==2};LLVOAvatar avatar{bake?&image:nullptr};LLViewerObject root;root.animated=animated;LLViewerObject object;object.root=rootPresent?&root:nullptr;object.avatar=avPresent?&avatar:nullptr;object.hud=hud;
trace.clear();const auto expected=object.getBakedTextureForMagicId(raw);const auto calls=trace;trace.clear();const auto actual=vs_baked_texture_for_magic_id(object,raw,Service{});assert(expected==actual);assert(trace==calls);++cases;
}assert(cases==192);
}
