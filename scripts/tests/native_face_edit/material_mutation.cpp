// Actual material-peer methods under explicit native publication refusal.
#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
using S32=int;using U8=unsigned char;using U32=unsigned;
template<class T>T llmax(T a,T b){return std::max(a,b);}template<class T>T llmin(T a,T b){return std::min(a,b);}
struct LLUUID{int id=0;LLUUID()=default;LLUUID(int v):id(v){}bool notNull()const{return id;}bool operator==(const LLUUID&)const=default;static const LLUUID null;};const LLUUID LLUUID::null;
const LLUUID IMG_ALPHA_GRAD{99},DEFAULT_OBJECT_TEXTURE{44};constexpr int TEM_CHANGE_NONE=0,TEM_CHANGE_TEXTURE=1;
template<class T>struct LLPointer{T*p=nullptr;LLPointer()=default;LLPointer(T*v):p(v){}T*operator->()const{return p;}operator T*()const{return p;}};
struct LLGLTFMaterial{int value=7;};
struct LLTextureEntry{LLUUID id{3};LLGLTFMaterial*override=nullptr;int reset=0;LLUUID getID()const{return id;}void setBaseMaterial(){++reset;}void setGLTFRenderMaterial(std::nullptr_t){}void setGLTFMaterialOverride(LLGLTFMaterial*v){override=v;}LLGLTFMaterial*getGLTFMaterialOverride()const{return override;}};
bool alive=true,cap=true,native=true,refuse=false,hiding=false;int attempts=0,previews=0,queues=0,sends=0,images=0;std::vector<LLTextureEntry>entries(2);std::vector<LLUUID>ids(2,LLUUID{20});
struct LLViewerObject{void setRenderMaterialID(S32,LLUUID,bool,bool){}void setTEGLTFMaterialOverride(U8,LLGLTFMaterial*){}void clearTEWaterExclusion(U8){}}legacy;
struct Task{explicit operator bool()const{return alive;}bool native()const{return ::native;}int nativeObject()const{return 1;}LLViewerObject*legacy()const{return &::legacy;}std::function<bool()>objectGuard()const{return []{return alive;};}std::string getCapability(const std::string&)const{return cap?"actual":"";}};
struct World{bool legacyMaterialPreview(int)const{return hiding;}template<class F>S32 editTextureEntry(int,U32 side,F f){++attempts;if(refuse)return 0;return f(entries.at(side));}bool previewMaterialID(int,U32 side,LLUUID id){++previews;ids.at(side)=id;return true;}}world;
struct Context{World&world(){return ::world;}}context;struct Window{Context*nativeContext(){return &context;}}window;Window*gViewerWindow=&window;
struct LLGLTFMaterialList{static void queueApply(int,S32,LLUUID,const LLGLTFMaterial*,std::function<bool()>current,const std::string&){if(current())++queues;}static void queueApply(LLViewerObject*,S32,LLUUID,const LLGLTFMaterial*){++queues;}};
struct LLPrimitive{static bool getTESTAxes(U8,U32*s,U32*t){*s=0;*t=1;return true;}};
struct VSMaterialObject
{
 Task mObject;explicit operator bool()const{return bool(mObject);}int getID()const{return 5;}S32 getNumTEs()const{return S32(entries.size());}bool permModify()const{return true;}LLUUID getRenderMaterialID(S32 side)const{return ids.at(side);}LLTextureEntry*getTE(S32 side)const{return side>=0&&side<getNumTEs()?&entries.at(side):nullptr;}
 bool queueApply(S32,const LLUUID&,const LLGLTFMaterial* =nullptr)const;bool setGLTFMaterialOverride(S32,const LLGLTFMaterial*)const;bool setRenderMaterialID(S32,const LLUUID&,bool=true,bool=true)const;bool clearTEWaterExclusion(U8)const;
 S32 setTEImage(U8 side,const LLUUID&id)const{++images;if(refuse)return 0;entries.at(side).id=id;return 1;}void setTEScale(U8,float,float)const{}void sendTEUpdate()const{++sends;}struct Scale{float mV[3]{1,1,1};};Scale getScale()const{return {};}
};
METHODS
void reset(){alive=cap=native=true;refuse=hiding=false;attempts=previews=queues=sends=images=0;entries=std::vector<LLTextureEntry>(2);ids=std::vector<LLUUID>(2,LLUUID{20});}
int main()
{
 VSMaterialObject object;LLGLTFMaterial material;
 reset();refuse=true;assert(!object.setGLTFMaterialOverride(0,&material));assert(attempts==1&&!previews&&!queues&&!entries[0].override);
 reset();refuse=true;assert(!object.setRenderMaterialID(0,{30}));assert(attempts==1&&!previews&&!queues&&ids[0]==LLUUID{20}&&entries[0].reset==0);
 reset();cap=false;assert(!object.setRenderMaterialID(0,{30}));assert(!attempts&&!previews&&!queues);
 reset();entries[0].id=IMG_ALPHA_GRAD;refuse=true;assert(!object.clearTEWaterExclusion(0));assert(images==1&&!sends&&entries[0].id==IMG_ALPHA_GRAD);
 reset();assert(object.setGLTFMaterialOverride(0,&material));assert(entries[0].override&&entries[0].override!=&material&&entries[0].override->value==7);
 reset();assert(object.setRenderMaterialID(0,{30}));assert(previews==1&&queues==1&&ids[0]==LLUUID{30}&&entries[0].reset==1);
 reset();hiding=true;assert(object.setRenderMaterialID(0,{30}));assert(previews==1&&queues==1&&ids[0]==LLUUID::null&&entries[0].reset==1);
 reset();assert(object.setRenderMaterialID(0,{30},false,true));assert(previews==1&&!queues&&ids[0]==LLUUID{30}&&entries[0].reset==0);
 reset();alive=false;assert(!object.setGLTFMaterialOverride(0,&material));assert(!object.setRenderMaterialID(0,{30}));assert(!attempts&&!previews&&!queues);
}
