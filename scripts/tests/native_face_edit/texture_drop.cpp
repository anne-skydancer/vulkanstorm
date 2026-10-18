// Extracted production preparation/publication with controlled callback owners.
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>
using S32=int;using U8=unsigned char;using U32=unsigned;
struct LLUUID{int id=0;LLUUID()=default;LLUUID(int v):id(v){};bool isNull()const{return !id;}bool notNull()const{return id;}bool operator==(const LLUUID&)const=default;static const LLUUID null;};const LLUUID LLUUID::null;
template<class T>struct LLPointer{T*p=nullptr;LLPointer()=default;LLPointer(T*v):p(v){};T*operator->()const{return p;}T&operator*()const{return *p;}T*get()const{return p;}operator T*()const{return p;}bool isNull()const{return !p;}bool notNull()const{return p;}LLPointer&operator=(T*v){p=v;return *this;}};
struct LLGLTFMaterial{enum TextureInfo{GLTF_TEXTURE_INFO_BASE_COLOR,GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS,GLTF_TEXTURE_INFO_EMISSIVE,GLTF_TEXTURE_INFO_NORMAL,GLTF_TEXTURE_INFO_COUNT};int channel[4]{},transform=7;bool operator==(const LLGLTFMaterial&)const=default;void setBaseColorId(LLUUID id,bool){channel[0]=id.id;}void setOcclusionRoughnessMetallicId(LLUUID id,bool){channel[1]=id.id;}void setEmissiveId(LLUUID id,bool){channel[2]=id.id;}void setNormalId(LLUUID id,bool){channel[3]=id.id;}};
struct LLMaterial{int normal=0,specular=0;void setNormalID(LLUUID v){normal=v.id;}void setSpecularID(LLUUID v){specular=v.id;}int asLLSD()const{return normal*1000+specular;}};using LLMaterialPtr=LLPointer<LLMaterial>;
struct LLTextureEntry{LLUUID id{10};int revision=1;LLPointer<LLGLTFMaterial>gltf;LLMaterialPtr legacy;bool operator!=(const LLTextureEntry&v)const{return id!=v.id || revision!=v.revision;}LLGLTFMaterial*getGLTFMaterialOverride()const{return gltf;}int setGLTFMaterialOverride(LLGLTFMaterial*v){gltf=v;return 1;}LLMaterialPtr getMaterialParams()const{return legacy;}void setMaterialParams(LLMaterialPtr v){legacy=v;}void setBaseMaterial(){if(gltf){auto*v=new LLGLTFMaterial;v->transform=gltf->transform;gltf=v;}}void setGLTFRenderMaterial(std::nullptr_t){};};
bool llsd_equals(int a,int b){return a==b;}
int epoch=1,protects=0,sends=0,prepares=0,submittedPuts=0,queues=0,refreshes=0,stats=0,requests=0;
bool cap=true,budget=true,modify=true,fetch=false,defaultMaterial=true;std::function<void()>protectHook,refreshHook;
struct LLInventoryItem{LLUUID asset{77};LLUUID getAssetUUID()const{return asset;}};
struct Permissions{bool allowModifyBy(LLUUID)const{return modify;}};struct LLViewerInventoryItem{Permissions getPermissions()const{return {};}};
LLUUID gAgentID{9};
struct VSTaskInventoryObject{int generation=1;explicit operator bool()const{return epoch==generation;}const VSTaskInventoryObject*operator->()const{return this;}LLUUID getID()const{return {4};}int getHost()const{return 1;}int nativeObject()const{return 4;}std::function<bool()>objectGuard()const{return [generation=generation]{return epoch==generation;};}std::string getCapability(const std::string&)const{return cap?"actual-cap":"";}bool requiresInventoryFetch()const{return fetch;}void requestInventory()const{++requests;}};
struct VSMaterialAssets{struct PreparedPut{int generation=epoch;LLMaterial value;};std::shared_ptr<PreparedPut>preparePut(int,U8,const LLMaterial&m){++prepares;if(!budget)return {};auto v=std::make_shared<PreparedPut>();v->value=m;return v;}bool put(const std::shared_ptr<PreparedPut>&p){if(!p || p->generation!=epoch)return false;++submittedPuts;return true;}}assets;
struct State{std::vector<LLTextureEntry>entries{2};std::vector<LLUUID>base{2,LLUUID{20}};}state;
struct VSMaterialObject{int generation=epoch;static VSMaterialObject find(int){return {};}explicit operator bool()const{return epoch==generation;}LLTextureEntry*getTE(S32 side)const{return side>=0&&side<int(state.entries.size())?&state.entries[side]:nullptr;}LLUUID getRenderMaterialID(S32 side)const{return state.base.at(side);}LLViewerInventoryItem*getInventoryItemByAsset(LLUUID)const{static LLViewerInventoryItem item;return &item;}void queueModify(S32,const LLGLTFMaterial*)const{++queues;}void setTEMaterialParams(U8 side,LLMaterialPtr v)const{state.entries.at(side).legacy=v;}void setTEImage(U8 side,LLUUID v)const{state.entries.at(side).id=v;}void sendTEUpdate()const{++sends;}};
struct World{VSMaterialAssets*legacyMaterialAssets(int){return &assets;}template<class F>int editTextureEntry(int,U32 side,F f){return f(state.entries.at(side));}bool previewMaterialID(int,U32 side,LLUUID v){state.base.at(side)=v;return true;}}world;
struct Context{World&world(){return ::world;}}context;struct Window{Context*nativeContext(){return &context;}}window;Window*gViewerWindow=&window;
struct Tools{LLMaterialPtr createDefaultMaterial(LLMaterialPtr v){return defaultMaterial?LLMaterialPtr(v?new LLMaterial(*v):new LLMaterial):LLMaterialPtr{};}}tools;Tools*gFloaterTools=&tools;
struct LLToolDragAndDrop{enum ESource{SOURCE_AGENT,SOURCE_WORLD};static bool handleDropMaterialProtections(const VSTaskInventoryObject&,LLInventoryItem*,ESource,LLUUID){++protects;if(protectHook)protectHook();return true;}};
struct LLGLTFMaterialList{static void queueApply(LLUUID,S32,LLUUID,const LLGLTFMaterial*,std::function<bool()>current,const std::string&){if(current())++queues;}static void flushUpdates(std::nullptr_t){}};
namespace LLStatViewer{constexpr int EDIT_TEXTURE=1;}void add(int,int){++stats;}void dialog_refresh_all(){++refreshes;if(refreshHook)refreshHook();}constexpr int TEM_CHANGE_TEXTURE=1;
POLICY
PLAN
PREPARE
PUBLISH
VSFaceTextureDrop original(bool pbr,bool remove,S32 channel,bool visible,S32 selected,S32 count,bool copy,bool transfer)
{
    // Frozen branch order of LLToolDragAndDrop::dropTextureOneFace (2026-10-18).
    if(pbr && !remove && channel>=-1)return copy&&transfer?VSFaceTextureDrop::PBROverride:VSFaceTextureDrop::Refuse;
    if(channel<-1)channel=-1;
    if(visible && selected!=count){channel=channel>-1?channel:selected;switch(channel){case 1:return VSFaceTextureDrop::Normal;case 2:return VSFaceTextureDrop::Specular;default:return VSFaceTextureDrop::Diffuse;}}
    return VSFaceTextureDrop::Diffuse;
}
void reset(){epoch=1;protects=sends=prepares=submittedPuts=queues=refreshes=stats=requests=0;cap=budget=modify=defaultMaterial=true;fetch=false;protectHook=refreshHook={};state=State{};}
int main()
{
    unsigned cases=0;for(int bits=0;bits<32;++bits)for(int channel=-3;channel<=8;++channel)for(int selected=0;selected<=7;++selected)
    {assert(original(bits&1,bits&2,channel,bits&4,selected,7,bits&8,bits&16)==vs_face_texture_drop(bits&1,bits&2,channel,bits&4,selected,7,bits&8,bits&16));++cases;}assert(cases==3072);
    VSTaskInventoryObject task;LLInventoryItem item;auto object=VSMaterialObject::find(4);
    reset();FaceTextureDrop op;op.side=0;op.kind=VSFaceTextureDrop::Normal;budget=false;assert(!prepareFaceTextureDrop(task,object,op,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL));assert(prepares==1&&!protects&&!submittedPuts&&state.entries[0].legacy.isNull());
    reset();op={};op.kind=VSFaceTextureDrop::PBROverride;fetch=true;assert(!prepareFaceTextureDrop(task,object,op,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL));assert(requests==1&&!queues);
    reset();op={};op.kind=VSFaceTextureDrop::PBROverride;modify=false;assert(!prepareFaceTextureDrop(task,object,op,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL));assert(!queues);
    reset();state.entries[0].gltf=new LLGLTFMaterial;op={};op.kind=VSFaceTextureDrop::PBROverride;assert(prepareFaceTextureDrop(task,object,op,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL));assert(op.override->channel[3]==77&&op.override->transform==7&&state.entries[0].gltf->channel[3]==0);std::vector<FaceTextureDrop>ops{op};assert(publishFaceTextureDrops(task,&item,LLToolDragAndDrop::SOURCE_AGENT,{},false,ops,false,false));assert(queues==1&&!protects&&!sends&&!stats);
    reset();op={};op.kind=VSFaceTextureDrop::Normal;assert(prepareFaceTextureDrop(task,object,op,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL));assert(prepares==1&&!submittedPuts);ops={op};assert(publishFaceTextureDrops(task,&item,LLToolDragAndDrop::SOURCE_AGENT,{},false,ops,true,true));assert(protects==1&&submittedPuts==1&&sends==1&&state.entries[0].legacy->normal==77);
    reset();op={};assert(prepareFaceTextureDrop(task,object,op,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR));ops={op};protectHook=[]{epoch=2;};assert(!publishFaceTextureDrops(task,&item,LLToolDragAndDrop::SOURCE_AGENT,{},false,ops,true,true));assert(state.entries[0].id==LLUUID{10}&&!sends);
    reset();op={};assert(prepareFaceTextureDrop(task,object,op,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR));ops={op};protectHook=[]{++state.entries[0].revision;};assert(!publishFaceTextureDrops(task,&item,LLToolDragAndDrop::SOURCE_AGENT,{},false,ops,true,true));assert(state.entries[0].id==LLUUID{10}&&!sends);
    reset();op={};assert(prepareFaceTextureDrop(task,object,op,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR));ops={op};protectHook=[&]{item.asset={88};};assert(!publishFaceTextureDrops(task,&item,LLToolDragAndDrop::SOURCE_AGENT,{},false,ops,true,true));assert(state.entries[0].id==LLUUID{10}&&!sends);item.asset={77};
    reset();op={};op.kind=VSFaceTextureDrop::Diffuse;assert(prepareFaceTextureDrop(task,object,op,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR));ops={op};auto next=op;next.side=1;assert(prepareFaceTextureDrop(task,object,next,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR));ops.push_back(next);refreshHook=[]{++state.entries[1].revision;};assert(!publishFaceTextureDrops(task,&item,LLToolDragAndDrop::SOURCE_AGENT,{},false,ops,true,true));assert(state.entries[0].id==item.asset&&state.entries[1].id==LLUUID{10}&&!sends);
    reset();op={};assert(prepareFaceTextureDrop(task,object,op,item.asset,LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR));ops={op};cap=false;assert(!publishFaceTextureDrops(task,&item,LLToolDragAndDrop::SOURCE_AGENT,{},true,ops,true,true));assert(!protects&&!queues&&state.entries[0].id==LLUUID{10});
}
