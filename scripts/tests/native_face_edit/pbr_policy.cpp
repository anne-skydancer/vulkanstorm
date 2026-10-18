#include <cassert>
#include <array>
#include <vector>
#include <random>
#include <type_traits>
#include <utility>
using U32=unsigned;using S32=int;using F32=float;constexpr int PERM_ITEM_UNRESTRICTED=7,ALEXANDRIA_LINDEN_ID=7;struct LLUUID{int id=0;bool isNull()const{return !id;}bool notNull()const{return id;}bool operator==(const LLUUID&)const=default;static const LLUUID null;};const LLUUID LLUUID::null{};const LLUUID BLANK_MATERIAL_ASSET_ID{999};
struct LLVector2{float mV[2]{};LLVector2(float a=0,float b=0):mV{a,b}{}bool operator==(const LLVector2&)const=default;};
struct LLGLTFMaterial{static constexpr unsigned GLTF_TEXTURE_INFO_COUNT=4;struct TextureTransform{LLVector2 mScale{1,1},mOffset;float mRotation=0;bool operator==(const TextureTransform&)const=default;};std::array<TextureTransform,4>mTextureTransform;static void convertTextureTransformToPBR(float s,float t,float x,float y,float r,LLVector2&scale,LLVector2&offset,float&rotation){scale={s,t};offset={x+(s-1)*.5f,y+(t-1)*.5f};rotation=r;}bool operator==(const LLGLTFMaterial&)const=default;};
template<class T>struct LLPointer{T*p=nullptr;LLPointer()=default;LLPointer(T*v):p(v){}operator T*()const{return p;}T*operator->()const{return p;}T&operator*()const{return*p;}};
struct LLTextureEntry{float s=1,t=1,x=0,y=0,r=0;LLGLTFMaterial*overrides=nullptr;auto*getGLTFMaterialOverride(){return overrides;}void getScale(float*a,float*b){*a=s;*b=t;}void getOffset(float*a,float*b){*a=x;*b=y;}float getRotation()const{return r;}void setGLTFMaterialOverride(LLGLTFMaterial*m){overrides=m;}};
struct LLPermissions{int owner=1,base=7;int getOwner()const{return owner;}int getMaskBase()const{return base;}};struct LLViewerInventoryItem{LLPermissions perm;LLUUID asset{13};const auto&getPermissions(){return perm;}LLUUID getAssetUUID(){return asset;}};
struct LLViewerObject{bool modify=true,attachment=false,allowDrop=true;LLUUID id{10};LLTextureEntry te;int drops=0,clears=0,sets=0;struct Queue{LLUUID id;std::array<LLGLTFMaterial::TextureTransform,4>transforms;bool operator==(const Queue&)const=default;};std::vector<Queue>queue;bool permModify(){return modify;}bool isAttachment(){return attachment;}LLUUID getRenderMaterialID(int){return id;}auto*getTE(int){return &te;}void clearTEWaterExclusion(int){++clears;}void setRenderMaterialID(int,LLUUID value,bool server=true,bool=true){++sets;id=value;if(server)te.overrides=nullptr;}void setGLTFMaterialOverride(int,const LLGLTFMaterial*m){te.overrides=new LLGLTFMaterial(*m);}void queueApply(int,LLUUID value,const LLGLTFMaterial*m){queue.push_back({value,m->mTextureTransform});}};
struct LLToolDragAndDrop{enum{SOURCE_AGENT};static bool handleDropMaterialProtections(LLViewerObject*o,LLViewerInventoryItem*,int,LLUUID){++o->drops;return o->allowDrop;}};
struct LLGLTFMaterialList{static void queueApply(LLViewerObject*o,int side,LLUUID id,const LLGLTFMaterial*m){o->queueApply(side,id,m);}};
struct Original{LLViewerInventoryItem*mItem;LLUUID mMatId;ORIGINAL};
KERNEL
LLViewerObject clone(const LLViewerObject&seed){auto o=seed;if(seed.te.overrides)o.te.overrides=new LLGLTFMaterial(*seed.te.overrides);return o;}
struct CheckedObject:LLViewerObject
{
 bool refuse=false;
 bool setRenderMaterialID(int side,LLUUID value,bool server=true,bool local=true){if(refuse)return false;LLViewerObject::setRenderMaterialID(side,value,server,local);return true;}
 bool queueApply(int side,LLUUID value,const LLGLTFMaterial* material){if(refuse)return false;LLViewerObject::queueApply(side,value,material);return true;}
 bool setGLTFMaterialOverride(int side,const LLGLTFMaterial* material){if(refuse)return false;LLViewerObject::setGLTFMaterialOverride(side,material);return true;}
};
int main(){std::mt19937 rng(9004);for(int iteration=0;iteration<2048;++iteration){LLViewerObject seed;seed.modify=rng()%3;seed.attachment=rng()%2;seed.allowDrop=rng()%2;seed.id={int(rng()%3)};seed.te.s=float(rng()%3);seed.te.t=float(rng()%3);seed.te.x=float(rng()%5)*.1f;seed.te.y=float(rng()%5)*.1f;seed.te.r=float(rng()%5)*.1f;if(rng()%2){seed.te.overrides=new LLGLTFMaterial;if(rng()%2)seed.te.overrides->mTextureTransform[0].mScale={2,3};}LLViewerInventoryItem item;item.perm.owner=(rng()%2)?7:1;item.perm.base=rng()%8;item.asset={int(rng()%5)};auto original=clone(seed),native=clone(seed);auto*p=(rng()%2)?&item:nullptr;LLUUID mat{int(rng()%5)};Original ref{p,mat};bool a=ref.apply(&original,0);bool b=vs_apply_pbr_face(&native,0,p,mat,[](auto*o,auto*item){return LLToolDragAndDrop::handleDropMaterialProtections(o,item,0,{});},[]{return true;});assert(a==b&&original.drops==native.drops&&original.clears==native.clears&&original.sets==native.sets&&original.id==native.id&&original.queue==native.queue);if(original.te.overrides||native.te.overrides)assert(original.te.overrides&&native.te.overrides&&*original.te.overrides==*native.te.overrides);}
 // A synchronous inventory callback retiring the originating selection prevents
 // all later material changes rather than applying to a replacement account.
 CheckedObject checked;checked.refuse=true;assert(!vs_apply_pbr_face(&checked,0,nullptr,{24},[](auto*,auto*){return true;},[]{return true;}));assert(!checked.sets && checked.queue.empty());
 checked.te.s=2;assert(!vs_apply_pbr_face(&checked,0,nullptr,{24},[](auto*,auto*){return true;},[]{return true;}));assert(!checked.sets && checked.queue.empty());
 LLViewerObject object;LLViewerInventoryItem item;bool active=true;assert(!vs_apply_pbr_face(&object,0,&item,{24},[&](auto*,auto*){active=false;return true;},[&]{return active;}));assert(object.sets==0&&object.clears==0&&object.queue.empty());
}
