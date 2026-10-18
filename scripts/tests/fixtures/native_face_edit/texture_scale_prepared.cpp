#include <cassert>
#include <vector>
#include <memory>
#include <array>
#include <cmath>
#include <random>
#include <map>
#include <functional>
#include <string>
#include <tuple>
using S32=int;using LLUUID=int;constexpr int VZ=2,TEM_CHANGE_TEXTURE=1;
using U8=unsigned char;using U32=unsigned;using F32=float;using F64=double;const int VX=0,VY=1;
#define llassert assert
namespace LLRender {enum eTexIndex {DIFFUSE_MAP=0};}
struct LLVector2 {F32 mV[2]{};LLVector2()=default;LLVector2(F32 s,F32 t):mV{s,t}{}void set(F32 s,F32 t){mV[0]=s;mV[1]=t;}float operator[](int i)const{return mV[i];}bool operator==(const LLVector2&)const=default;};
struct LLVector3 {F32 mV[3]{};LLVector3()=default;LLVector3(F32 s,F32 t,F32 z):mV{s,t,z}{}bool operator==(const LLVector3&)const=default;bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}};
template<class T>struct LLPointer {T*p=nullptr;LLPointer()=default;LLPointer(T*v):p(v){}bool notNull()const{return p;}bool isNull()const{return !p;}explicit operator bool()const{return p;}operator T*()const{return p;}T*operator->()const{return p;}T&operator*()const{return*p;}};
struct LLMaterial {float ns=1,nt=1,ss=1,st=1;void setNormalRepeat(float s,float t){ns=s;nt=t;}void setSpecularRepeat(float s,float t){ss=s;st=t;}bool operator==(const LLMaterial&)const=default;auto asLLSD()const{return std::tuple{ns,nt,ss,st};}};using LLMaterialPtr=LLPointer<LLMaterial>;
struct LLGLTFMaterial {static constexpr unsigned GLTF_TEXTURE_INFO_COUNT=4;struct TextureTransform {LLVector2 mScale{1,1},mOffset{0,0};F32 mRotation=0;bool operator==(const TextureTransform&)const=default;};std::array<TextureTransform,4>mTextureTransform;bool operator==(const LLGLTFMaterial&)const=default;void applyOverride(const LLGLTFMaterial& v){mTextureTransform=v.mTextureTransform;}};struct LLFetchedGLTFMaterial:LLGLTFMaterial{};
struct LLTextureEntry {static constexpr U8 TEX_GEN_PLANAR=2;bool operator==(const LLTextureEntry&b)const{return s==b.s&&t==b.t&&gen==b.gen;}void setScale(float a,float b){s=a;t=b;}float s=1,t=1;U8 gen=0;LLMaterialPtr material;LLGLTFMaterial* base=nullptr;LLGLTFMaterial* overrides=nullptr;LLGLTFMaterial* render=nullptr;void getScale(float* a,float*b)const{*a=s;*b=t;}U8 getTexGen()const{return gen;}LLMaterialPtr getMaterialParams()const{return material;}auto* getGLTFMaterial()const{return base;}auto*getGLTFMaterialOverride()const{return overrides;}void setGLTFMaterialOverride(LLGLTFMaterial* p){overrides=p;}void setGLTFRenderMaterial(LLGLTFMaterial* p){render=p;}};
struct LLPrimitive {static bool getTESTAxes(U8 te,U32*s,U32*t){if(te==4)return false;*s=(te+1)%3;*t=(te+2)%3;return true;}};
struct LLViewerObject {bool modify=true;LLVector3 scale{1,1,1};std::vector<LLTextureEntry> entries;unsigned updates=0,sets=0;bool permModify(){return modify;}auto getNumTEs(){return entries.size();}LLVector3 getScale(){return scale;}LLTextureEntry* getTE(U8 side){return &entries.at(side);}int getID()const{return 71;}void setTEScale(U8 side,float s,float t){entries.at(side).s=s;entries.at(side).t=t;++sets;}void sendTEUpdate(){++updates;}};
struct Tools {LLMaterialPtr createDefaultMaterial(LLMaterialPtr m){return m?new LLMaterial(*m):new LLMaterial;}}tools;auto*gFloaterTools=&tools;
struct LLMaterialMgr {struct Put {int id;U8 side;LLMaterial material;bool operator==(const Put&)const=default;};std::vector<Put>puts;static auto*getInstance(){static LLMaterialMgr m;return&m;}static auto&instance(){return*getInstance();}void put(int id,U8 side,const LLMaterial& m){puts.push_back({id,side,m});}};
struct LLGLTFMaterialList {struct Update {int id;U8 side;std::array<LLGLTFMaterial::TextureTransform,4> transform;bool operator==(const Update&)const=default;};static inline std::vector<Update>updates;static void queueModify(int id,U8 side,const LLGLTFMaterial*m,std::function<bool()> current,const std::string&){assert(current());updates.push_back({id,side,m->mTextureTransform});}static void queueModify(LLViewerObject*o,U8 s,const LLGLTFMaterial*m){updates.push_back({o->getID(),s,m->mTextureTransform});}};

KERNEL
struct Saved {std::shared_ptr<VSTextureScaleRatios>textureRatios;};
std::map<int,LLViewerObject> objects;bool active=true,selected=true,allowBudget=true;int publications=0,leases=0;
struct VSMaterialAssets;
struct VSWorldScene {VSMaterialAssets*legacyMaterialAssets(int);struct ObjectHandle{int id=1,host=1;};std::shared_ptr<const void>reserveSpatialBytes(std::size_t){if(!allowBudget)return {}; ++leases;return {new int(1),[](const void*p){--leases;delete static_cast<const int*>(p);}};}int editTextureEntry(ObjectHandle t,U32 side,const std::function<int(LLTextureEntry&)>&edit){++publications;return edit(objects.at(t.id).entries.at(side));}};
struct Context{VSWorldScene scene;auto&world(){return scene;}};struct Window{Context ctx;auto*nativeContext(){return&ctx;}}window;auto*gViewerWindow=&window;
struct VSObjectSelection {std::shared_ptr<Saved>saved=std::make_shared<Saved>();std::vector<VSWorldScene::ObjectHandle>selected(){return{{1}};}bool node(VSWorldScene::ObjectHandle)const{return ::selected;}std::shared_ptr<Saved>savedTransform(VSWorldScene::ObjectHandle)const{return saved;}std::function<bool()>selectionGuard()const{return[]{return active&&::selected;};}};
struct Task {std::function<bool()>objectGuard()const{return[]{return active;};}std::string getCapability(const std::string&)const{return "cap";}};
struct VSMaterialObject {int id=0;static VSMaterialObject find(int id){return {objects.contains(id)?id:0};}explicit operator bool()const{return id&&active;}bool permModify()const{return objects.at(id).modify;}auto getNumTEs()const{return objects.at(id).entries.size();}LLVector3 getScale()const{return objects.at(id).scale;}LLTextureEntry*getTE(U8 side)const{return &objects.at(id).entries.at(side);}int getRenderMaterialID(U8 side)const{return getTE(side)->base?101:0;}Task task()const{return{};}void sendTEUpdate()const{++objects.at(id).updates;}};
template<class T>bool llsd_equals(const T&a,const T&b){return a==b;}
bool legacyBudget=true;
struct VSMaterialAssets {struct PreparedPut{int id;U8 side;LLMaterial material;};std::shared_ptr<PreparedPut>preparePut(VSWorldScene::ObjectHandle o,U8 side,const LLMaterial&m){return legacyBudget?std::make_shared<PreparedPut>(PreparedPut{o.id,side,m}):nullptr;}bool put(const std::shared_ptr<PreparedPut>&p){if(!p)return false;LLMaterialMgr::instance().put(p->id,p->side,p->material);return true;}};VSMaterialAssets assets;VSMaterialAssets*VSWorldScene::legacyMaterialAssets(int){return&assets;}
PREPARED
int main(){VSObjectSelection selection;LLViewerObject source;source.scale={1,2,3};source.entries.resize(1);source.entries[0].s=4;source.entries[0].t=6;source.entries[0].material=new LLMaterial;source.entries[0].base=new LLGLTFMaterial;source.entries[0].overrides=new LLGLTFMaterial;objects[1]=source;selection.saved->textureRatios=std::make_shared<VSTextureScaleRatios>();auto o=VSMaterialObject::find(1);vs_capture_texture_scale_ratios(&o,*selection.saved->textureRatios);
 std::map<int,LLVector3>scale{{1,{2,4,6}}};
 allowBudget=false;assert(!vs_prepare_selected_texture_adjustment(selection,scale,true,false));assert(publications==0&&objects[1].scale==source.scale&&LLGLTFMaterialList::updates.empty());
 allowBudget=true;legacyBudget=false;assert(!vs_prepare_selected_texture_adjustment(selection,scale,true,false)&&publications==0&&LLMaterialMgr::instance().puts.empty());legacyBudget=true;auto plan=vs_prepare_selected_texture_adjustment(selection,scale,true,false);assert(plan&&leases==1&&publications==0&&objects[1].entries[0].s==4&&source.entries[0].overrides->mTextureTransform[0].mScale==LLVector2(1,1));
 // Publish after exactly the proposed real transform; original wire precedes TE.
 objects[1].scale=scale[1];assert(vs_publish_prepared_texture_adjustment(plan));assert(publications==1&&objects[1].entries[0].s==8&&objects[1].entries[0].t==12&&objects[1].updates==1&&LLGLTFMaterialList::updates.size()==1&&LLMaterialMgr::instance().puts.size()==1);assert(!vs_publish_prepared_texture_adjustment(plan));plan.reset();assert(leases==0);
 // Reentrant selection/account changes and edited TEs cannot be overwritten.
 plan=vs_prepare_selected_texture_adjustment(selection,{{1,{4,8,12}}},true,false);assert(plan);selected=false;auto before=publications;assert(!vs_publish_prepared_texture_adjustment(plan)&&publications==before);selected=true;plan.reset();
 plan=vs_prepare_selected_texture_adjustment(selection,scale,true,false);assert(plan);objects[1].entries[0].s=99;assert(!vs_publish_prepared_texture_adjustment(plan)&&publications==before);plan.reset();
 active=false;assert(!vs_prepare_selected_texture_adjustment(selection,scale,true,false));assert(leases==0);
}
