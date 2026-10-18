#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <set>
#include <utility>
#include <vector>
using U32=uint32_t;using U64=uint64_t;using U8=uint8_t;using S32=int;using F32=float;using F64=double;
struct LLVector3{float x=0;static const LLVector3 zero;float length()const{return x;}};const LLVector3 LLVector3::zero{};
float dist_vec_squared(LLVector3 a,LLVector3 b){return (a.x-b.x)*(a.x-b.x);}
struct LLCamera{int result=2;S32 sphereInFrustum(LLVector3,F32)const{return result;}};
struct Settings{std::map<std::string,bool>values;bool getBOOL(const char*k){return values[k];}}gSavedSettings;
struct Agent{LLVector3 getPosGlobalFromAgent(LLVector3){return {};}int getGodLevel(){return 0;}bool isGodlike(){return false;}}gAgent;
struct LLGridManager{static LLGridManager*getInstance(){static LLGridManager s;return &s;}bool isInSLBeta(){return false;}};
constexpr int GOD_MAINTENANCE=200,LL_PCODE_VOLUME=9;
constexpr U32 FLAGS_OBJECT_YOU_OWNER=1,FLAGS_OBJECT_MOVE=2,FLAGS_OBJECT_COPY=4,FLAGS_AFFECTS_NAVMESH=8,FLAGS_OBJECT_GROUP_OWNED=16;
template<class F>bool vs_object_you_owner(bool b,F){return b;}
struct VSResourceBudget{struct Reservation{};};
struct VSWorldScene{
 struct ObjectHandle{int id=0,host=1;};
 struct ObjectAdmission{U32 rootUpdateFlags=7,updateFlags=0;int pcode=9;bool attachment=false;};
 struct SelectionCandidates{std::vector<ObjectHandle>objects;};
 struct Frame{LLVector3 renderPosition,rawScale{1};};
 struct Row{ObjectAdmission admission;int parent=0;std::vector<int>children;bool alive=true,visible=true,root=true;float position=0;};
 std::map<int,Row>rows;std::vector<int>candidates;int budget=1000;bool refuseQuery=false;
 std::map<int,bool>selected;
 auto reserveSpatialBytes(size_t){return budget-->0?std::make_shared<const VSResourceBudget::Reservation>():nullptr;}
 std::optional<SelectionCandidates>selectionCandidates(const LLCamera&,LLVector3){if(refuseQuery)return {};SelectionCandidates c;for(int id:candidates)c.objects.push_back({id});return c;}
 bool current(ObjectHandle t){return rows.count(t.id)&&rows.at(t.id).alive;}
 std::optional<ObjectAdmission>objectAdmission(ObjectHandle t){return current(t)?std::optional<ObjectAdmission>(rows[t.id].admission):std::nullopt;}
 std::optional<Frame>focusObject(ObjectHandle t,LLVector3){return current(t)?std::optional<Frame>(Frame{{rows[t.id].position},{1}}):std::nullopt;}
 bool objectVertsVisible(ObjectHandle t,const LLCamera&,bool,LLVector3){return current(t)&&rows[t.id].visible;}
 std::optional<ObjectHandle>rootObject(ObjectHandle t,bool){if(!current(t))return {};while(rows[t.id].parent)t.id=rows[t.id].parent;return t;}
 std::optional<std::vector<ObjectHandle>>directChildren(ObjectHandle t){if(!current(t))return {};std::vector<ObjectHandle>v;for(int id:rows[t.id].children)v.push_back({id});return v;}
 std::optional<U32>faceCount(ObjectHandle t){return current(t)?std::optional<U32>(4):std::nullopt;}
 bool setSelectionState(ObjectHandle t,bool value){if(!current(t))return false;selected[t.id]=value;return true;}
 std::optional<bool>objectIsRootEdit(ObjectHandle t){if(!current(t))return {};return rows[t.id].parent==0;}
}world;
auto*scene(){return &world;}
bool sameObject(VSWorldScene::ObjectHandle a,VSWorldScene::ObjectHandle b){return a.id==b.id;}
struct VSObjectContext{static bool ownedByAgent(U32 f){return f&1;}static std::optional<VSObjectContext>resolve(VSWorldScene::ObjectHandle){return VSObjectContext{};}};
struct RlvActions{static bool isRlvEnabled(){return false;}static bool canEdit(VSObjectContext){return true;}};
struct LLSelectMgr{static inline bool sRectSelectInclusive=false;};
struct LLFrameTimer{static F64 getTotalSeconds(){return 3;}};
struct VSNativeSession:std::enable_shared_from_this<VSNativeSession>{bool live=true;int host(){return 1;}auto accountGuard(){return [self=shared_from_this()]{return self->live;};}};
struct VSObjectSelection{
 struct NodeMetadata{U32 faceMask=0;bool individual=false;};struct Publication{};
 struct Impl{
  struct Highlight{VSWorldScene::ObjectHandle object;std::shared_ptr<const VSResourceBudget::Reservation>residency;};
  struct Node{VSWorldScene::ObjectHandle object;std::shared_ptr<const VSResourceBudget::Reservation>residency;std::optional<Publication>publication;bool pending=true;F64 retryAt=0;NodeMetadata metadata;};
  std::weak_ptr<VSNativeSession>session;bool retired=false,forceSelection=false;U64 rectangleRevision=1,revision=1;
  std::vector<Highlight>rectangleHits;std::vector<Node>rectangleNodes,nodes;std::optional<VSWorldScene::ObjectHandle>primary;
  std::function<void()>changed=[]{};
  bool current(VSWorldScene::ObjectHandle t){auto s=session.lock();return !retired&&s&&s->live&&world.current(t);}
 };
 std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();int sends=0,saves=0;std::vector<int>familyRequests,deselected,wire;std::function<void()>duringSend;
 std::optional<NodeMetadata>node(VSWorldScene::ObjectHandle t){for(auto&n:mImpl->nodes)if(sameObject(n.object,t)&&mImpl->current(t))return n.metadata;return {};}
 bool canSelect(VSWorldScene::ObjectHandle t,bool){return mImpl->current(t)&&t.id!=7;}
 bool deselectOnly(VSWorldScene::ObjectHandle t){deselected.push_back(t.id);mImpl->nodes.erase(std::remove_if(mImpl->nodes.begin(),mImpl->nodes.end(),[&](auto&n){return sameObject(n.object,t);}),mImpl->nodes.end());return true;}
 bool deselectFamily(VSWorldScene::ObjectHandle t){deselected.push_back(-t.id);return true;}
 void sendSelect(){++sends;for(auto&n:mImpl->nodes)wire.push_back(n.object.id);if(duringSend)std::exchange(duringSend,{})();}
 template<class F>bool requestFamily(VSWorldScene::ObjectHandle t,int,F){familyRequests.push_back(t.id);return true;}
 void refreshSelectionGeometry(){}void updateAttention(){}void bindEditMenuHandler(){}bool saveSelectedObjectTransform(){++saves;return true;}
 bool updateRectangle(const LLCamera&,bool,bool,bool,bool,F32,const LLVector3&,const std::function<bool()>&);
 bool commitRectangle(bool,const std::function<bool()>&);void clearRectangle();std::vector<VSWorldScene::ObjectHandle>rectangleHighlights()const;
};
struct LLViewerObject{
 U32 flags=0;bool permanent=false;int getPCode(){return LL_PCODE_VOLUME;}bool permYouOwner(){return flags&FLAGS_OBJECT_YOU_OWNER;}bool permMove(){return flags&FLAGS_OBJECT_MOVE;}bool isPermanentEnforced(){return permanent;}bool permCopy(){return flags&FLAGS_OBJECT_COPY;}bool permGroupOwner(){return flags&FLAGS_OBJECT_GROUP_OWNED;}
};
struct Reference{std::set<LLViewerObject*>mRectSelectedObjects;void highlightObjectOnly(LLViewerObject*);};
REFERENCE
METHODS
int main(){
 auto account=std::make_shared<VSNativeSession>();VSObjectSelection s;s.mImpl->session=account;
 for(int id=1;id<=8;++id)world.rows[id]={};world.rows[1].children={2,3};world.rows[2].parent=world.rows[3].parent=1;world.rows[3].children={4};world.rows[4].parent=3;
 world.candidates={2};LLCamera camera;auto live=[] {return true;};
 assert(s.updateRectangle(camera,true,false,false,false,100,{},live));
 auto high=s.rectangleHighlights();assert(high.size()==3&&high[0].id==2&&high[1].id==3&&high[2].id==1); // Immediate children, root LAST, never grandchild4.
 auto residency=s.mImpl->rectangleNodes[0].residency;
 assert(s.updateRectangle(camera,true,false,false,false,100,{},live));assert(s.mImpl->rectangleNodes[0].residency==residency);
 assert(s.commitRectangle(false,live));assert(s.sends==1&&s.saves==1&&s.wire==std::vector<int>({1,3,2})&&s.familyRequests==std::vector<int>({1}));assert(!s.mImpl->primary&&s.rectangleHighlights().empty());
 // No initializeSelectionMotion is available to this compiled actual method;
 // a mistaken selectOnly/family substitution would not compile here.
 world.candidates={5};assert(s.updateRectangle(camera,true,false,false,false,100,{},live));auto prior=s.rectangleHighlights();world.budget=0;world.candidates={6};assert(!s.updateRectangle(camera,true,false,false,false,100,{},live));assert(s.rectangleHighlights()[0].id==prior[0].id);world.budget=1000;
 world.refuseQuery=true;assert(!s.updateRectangle(camera,true,false,false,false,100,{},live));assert(s.rectangleHighlights()[0].id==5);world.refuseQuery=false;
 camera.result=0;assert(s.updateRectangle(camera,false,true,false,false,100,{},live));assert(s.rectangleHighlights().empty());camera.result=1;world.rows[5].visible=false;world.candidates={5};assert(s.updateRectangle(camera,true,false,false,false,100,{},live));assert(s.rectangleHighlights().empty());world.rows[5].visible=true;
 gSavedSettings.values["EditLinkedParts"]=true;assert(s.updateRectangle(camera,true,false,false,false,100,{},live));assert(s.rectangleHighlights().size()==1&&s.mImpl->rectangleNodes[0].metadata.individual);
 s.clearRectangle();world.rows[5].position=20;assert(s.updateRectangle(camera,true,false,false,true,25,{},live));assert(s.rectangleHighlights().empty());world.rows[5].position=0;
 world.rows[5].admission.attachment=true;assert(s.updateRectangle(camera,true,false,false,false,100,{},live));assert(s.rectangleHighlights().empty());world.rows[5].admission.attachment=false;
 gSavedSettings.values["SelectOwnedOnly"]=true;world.rows[5].admission.rootUpdateFlags=0;assert(s.updateRectangle(camera,true,false,false,false,100,{},live));assert(s.rectangleHighlights().empty());gSavedSettings.values["SelectOwnedOnly"]=false;
 world.candidates={1};assert(s.updateRectangle(camera,true,false,true,false,100,{},live));assert(s.commitRectangle(true,live));assert(s.deselected==std::vector<int>({1}));
 world.candidates={6};assert(s.updateRectangle(camera,true,false,false,false,100,{},live));auto nodes=s.mImpl->nodes.size();bool operation=false;assert(!s.commitRectangle(false,[&]{return operation;}));assert(s.mImpl->nodes.size()==nodes);
 // Actual original raw rectangle filter across all permission/settings bits.
 world.candidates={5};world.rows[5].admission.attachment=false;world.rows[5].position=0;camera.result=2;
 for(unsigned flags=0;flags<32;++flags)for(unsigned settings=0;settings<16;++settings){
  s.clearRectangle();world.budget=1000;world.rows[5].admission.rootUpdateFlags=flags;
  world.rows[5].admission.updateFlags=0;
  gSavedSettings.values["SelectOwnedOnly"]=settings&1;gSavedSettings.values["SelectMovableOnly"]=settings&2;
  gSavedSettings.values["FSSelectCopyableOnly"]=settings&4;gSavedSettings.values["FSSelectIncludeGroupOwned"]=settings&8;
  LLViewerObject object;object.flags=flags;Reference reference;reference.highlightObjectOnly(&object);
  assert(s.updateRectangle(camera,true,false,false,false,100,{},live));
  assert(!s.rectangleHighlights().empty()==!reference.mRectSelectedObjects.empty());
 }
 gSavedSettings.values.clear();gSavedSettings.values["EditLinkedParts"]=true;world.candidates={6};s.clearRectangle();world.budget=1000;assert(s.updateRectangle(camera,true,false,false,false,100,{},live));
 s.duringSend=[&]{account->live=false;s.mImpl->retired=true;s.mImpl->nodes.clear();++s.mImpl->revision;};assert(!s.commitRectangle(false,live));assert(s.mImpl->nodes.empty());
}
