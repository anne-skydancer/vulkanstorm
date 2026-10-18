#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
using U32=uint32_t;using U64=uint64_t;using U8=uint8_t;using S32=int;using F64=double;
struct VSResourceBudget{struct Reservation{};};
struct LLFrameTimer{static double getTotalSeconds(){return 10;}};
struct Vector{void clearVec(){}};struct LLBBox{};
struct Settings{bool linked=false;bool getBOOL(const char*)const{return linked;}void setBOOL(const char*,bool b){linked=b;}}gSavedSettings;
struct VSWorldScene
{
 struct ObjectHandle{int host=1,id=0;U32 local=0;U64 region=1,generation=1;int kind=1,account=99;U64 accountGeneration=1;};
 enum class FamilyTraversal{NonJoint,AllChildren};
 std::map<int,unsigned>faces;std::map<int,bool>chosen;std::map<int,unsigned>selectMotion,deselectMotion;std::vector<ObjectHandle>family;int budget=1000;
 bool current(const ObjectHandle&o)const{return o.account==99&&o.accountGeneration==1&&o.generation==1&&o.region==1&&faces.count(o.id);}
 std::optional<U32>faceCount(const ObjectHandle&o)const{return current(o)?std::optional<U32>(faces.at(o.id)):std::nullopt;}
 auto reserveSpatialBytes(size_t){return budget-->0?std::make_shared<const VSResourceBudget::Reservation>():nullptr;}
 bool setSelectionState(const ObjectHandle&o,bool b){if(!current(o))return false;chosen[o.id]=b;return true;}
 bool initializeSelectionMotion(const ObjectHandle&o){if(!current(o))return false;++selectMotion[o.id];return true;}
 bool initializeDeselectionMotion(const ObjectHandle&o){if(!current(o))return false;++deselectMotion[o.id];return true;}
 std::optional<std::vector<ObjectHandle>>linkedFamily(const ObjectHandle&o,bool=true,bool=true,FamilyTraversal=FamilyTraversal::NonJoint)const{if(!current(o))return {};return family.empty()?std::vector<ObjectHandle>{o}:family;}
 std::optional<ObjectHandle>rootObject(const ObjectHandle&o,bool=true)const{if(!current(o))return {};return family.empty()?o:family.front();}
};
VSWorldScene*activeWorld=nullptr;VSWorldScene*scene(){return activeWorld;}
bool sameObject(const VSWorldScene::ObjectHandle&a,const VSWorldScene::ObjectHandle&b){return a.host==b.host&&a.id==b.id&&a.local==b.local&&a.region==b.region&&a.generation==b.generation&&a.kind==b.kind&&a.account==b.account&&a.accountGeneration==b.accountGeneration;}
class VSObjectSelection;
struct VSNativeSession:std::enable_shared_from_this<VSNativeSession>
{
 enum class Phase{Connected,Disconnected};Phase state=Phase::Connected;
 struct Account{int agent=99;U64 generation=1;}identity;
 std::shared_ptr<VSObjectSelection>selection;
 Phase phase()const{return state;}Account account()const{return identity;}
 bool current(const Account&a)const{return state==Phase::Connected&&a.agent==identity.agent&&a.generation==identity.generation;}
 std::function<bool()>accountGuard(){auto self=shared_from_this();auto a=account();return[self,a]{return self->current(a);};}
 std::shared_ptr<VSObjectSelection>retainObjectSelection()const{return selection;}
};
class VSObjectSelection
{
public:
 struct NodeMetadata{U32 faceMask=0;S32 lastFace=0,gltfNode=-1,gltfPrimitive=-1;bool transient=false,individual=false,primary=false;};
 struct Publication{};
 struct Impl
 {
  struct Node{VSWorldScene::ObjectHandle object;std::shared_ptr<const VSResourceBudget::Reservation>residency;std::optional<Publication>publication;bool pending=true;F64 retryAt=0;NodeMetadata metadata;};
  std::weak_ptr<VSNativeSession>session;std::vector<Node>nodes,hover;std::optional<VSWorldScene::ObjectHandle>primary,hoverRoot,hoverPrimary;U64 revision=1,hoverRevision=1,highlightRevision=1;bool retired=false,forceSelection=false,allowSelectAvatar=false,geometryKnown=false,showSelection=false;std::vector<int>pauses,families,highlights;std::function<bool()>highlightCurrent;Vector selectionCenter;LLBBox bounds;std::shared_ptr<const void>selectionLease=std::make_shared<const U8>(0);
  std::vector<std::pair<std::string,std::vector<VSWorldScene::ObjectHandle>>>sent;std::function<void()>duringSend,changed=[]{ };
  CURRENT
  void sendSelection(const char*n,const std::vector<VSWorldScene::ObjectHandle>&objects){sent.emplace_back(n,objects);if(duringSend){auto cb=std::exchange(duringSend,{});cb();}}
 };
 std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();bool permitted=true;
 std::optional<NodeMetadata>node(const VSWorldScene::ObjectHandle&)const;bool contains(const VSWorldScene::ObjectHandle&,S32=-1)const;
 bool selectOnly(const VSWorldScene::ObjectHandle&,S32=-1,S32=-1,S32=-1);bool selectFamily(const VSWorldScene::ObjectHandle&,bool=false,bool=false);bool deselectOnly(const VSWorldScene::ObjectHandle&,bool=true);bool deselectFamily(const VSWorldScene::ObjectHandle&,bool=true,bool=false);bool deselectAll();void promoteSelectionToRoot();void demoteSelectionToIndividuals();void reset();bool setForceSelection(bool);bool setAllowSelectAvatar(bool);bool allowSelectAvatar()const;std::shared_ptr<const void>retainSelection()const;
 bool canSelect(const VSWorldScene::ObjectHandle&o,bool force=false,bool=false)const{return mImpl->current(o)&&(force||permitted);}
 void refreshSelectionGeometry(){}void updateAttention(){}
 std::vector<VSWorldScene::ObjectHandle>selected()const{std::vector<VSWorldScene::ObjectHandle>out;for(auto&n:mImpl->nodes)if(mImpl->current(n.object))out.push_back(n.object);return out;}
};
METHODS
int main()
{
 VSWorldScene world;activeWorld=&world;for(int i=1;i<=4;++i)world.faces[i]=4;
 auto session=std::make_shared<VSNativeSession>();auto selection=std::make_shared<VSObjectSelection>();session->selection=selection;selection->mImpl->session=session;
 auto object=[](int id){VSWorldScene::ObjectHandle h;h.id=id;h.local=id;return h;};auto a=object(1),b=object(2),c=object(3),d=object(4);
 assert(selection->selectOnly(a,2,7,3));assert(selection->node(a)->faceMask==4&&selection->node(a)->individual&&selection->contains(a,2)&&!selection->contains(a));assert(world.selectMotion[1]==1);
 assert(selection->selectOnly(a,-1,-1,-1));assert(selection->node(a)->faceMask==4&&selection->node(a)->gltfNode==7&&world.selectMotion[1]==1);
 assert(selection->deselectAll());world.family={b,c};assert(selection->selectFamily(c));auto selected=selection->selected();assert(selected.size()==2&&selected[0].id==3&&selected[1].id==2);assert(world.selectMotion[2]==1&&world.selectMotion[3]==0&&selection->contains(b));
 world.family.clear();world.budget=0;auto before=selection->selected();assert(!selection->selectOnly(d));assert(selection->selected().size()==before.size()&&!world.chosen[4]);world.budget=100;
 assert(selection->selectOnly(d,1));selection->mImpl->duringSend=[&]{selection->mImpl->nodes.clear();++selection->mImpl->revision;};assert(!selection->deselectOnly(d));assert(selection->selected().empty());
 assert(selection->selectOnly(d));auto lease=selection->retainSelection();assert(lease);std::weak_ptr<VSObjectSelection>weak=selection;
 selection->mImpl->duringSend=[&]{selection->reset();session->selection.reset();};assert(!selection->deselectOnly(d));assert(!selection->retainSelection()&&selection->selected().empty()&&!selection->selectOnly(a));assert(lease);
 // A genuine in-flight controller reference keeps only memory alive through
 // account reset. Its authority and opaque selection-use lease are retired.
 auto retained=selection;selection.reset();assert(!weak.expired());retained.reset();assert(weak.expired());
}
