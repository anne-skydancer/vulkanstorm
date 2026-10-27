#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
using U8=uint8_t;using U32=uint32_t;using F32=float;using LLUUID=int;using LLHost=int;
struct LLVector3d {double x=0;};
constexpr U32 FLAGS_OBJECT_YOU_OWNER=16;constexpr U8 GOD_MAINTENANCE=250;
constexpr U8 CLICK_ACTION_TOUCH=0,CLICK_ACTION_DISABLED=8,CLICK_ACTION_NONE=9;
INTERACTION
struct LLXform {virtual ~LLXform()=default;};
struct LLViewerRegion {int host=4;int getHost()const{return host;}};
struct LLViewerObject:LLXform {
 int id=1,local=12;LLViewerObject* root=this;LLViewerObject* edit=this;
 LLViewerRegion region;bool dead=false,attached=false,hud=false,owned=true;U32 flags=FLAGS_OBJECT_YOU_OWNER;
 bool isDead()const{return dead;}int getID()const{return id;}int getLocalID()const{return local;}
 LLViewerObject* getRootEdit()const{return edit;}LLXform* getRoot()const{return root;}
 U32 getFlags(){return flags;}U8 getPCode()const{return 9;}U8 getMaterial()const{return 3;}
 U8 getClickAction()const{return 7;}LLVector3d getPositionGlobal()const{return {10};}
 bool isAttachment()const{return attached;}bool isHUDAttachment()const{return hud;}
 bool permYouOwner()const{return owned;}const LLViewerRegion* getRegion()const{return &region;}
};
template<class T>struct LLPointer {
 T* value=nullptr;LLPointer(T* v):value(v){};T* operator->()const{return value;}
 T* get()const{return value;}bool notNull()const{return value!=nullptr;}
};
struct ObjectList {std::map<int,LLViewerObject*> objects;LLViewerObject* findObject(int id)const{auto i=objects.find(id);return i==objects.end()?nullptr:i->second;}}gObjectList;
struct Agent {
 int id=10,session=20;U8 god=0;
 int getID()const{return id;}int getSessionID()const{return session;}U8 getGodLevel()const{return god;}
}gAgent;
struct LLGridManager {
 bool beta=false;static LLGridManager* getInstance(){static LLGridManager grid;return &grid;}
 bool isInSLBeta()const{return beta;}
};
#define VS_NATIVE_VULKAN 1
struct VSWorldScene {
 struct ObjectHandle {int id=0;unsigned generation=1;int host=4;U32 local=12;};
 struct ObjectSnapshot {
  ObjectHandle object;int rootID=0,rootEditID=0;U32 rootUpdateFlags=FLAGS_OBJECT_YOU_OWNER;
  U8 pcode=9,material=3,clickAction=7,rootClickAction=0;bool attachment=false,hud=false;
  LLVector3d position{10};
 };
 std::map<int,ObjectSnapshot> objects;
 std::optional<ObjectHandle> object(int id)const{auto i=objects.find(id);return i==objects.end()?std::nullopt:std::optional(i->second.object);}
 std::optional<ObjectSnapshot> objectSnapshot(ObjectHandle h)const{auto i=objects.find(h.id);return i==objects.end()||i->second.object.generation!=h.generation?std::nullopt:std::optional(i->second);}
 bool current(ObjectHandle h)const{return bool(objectSnapshot(h));}
 // Mirrors VSWorldScene::rootObject: stopAtAvatar yields the edit root, otherwise the full linkset root.
 std::optional<ObjectHandle> rootObject(ObjectHandle h,bool stopAtAvatar=true)const{auto s=objectSnapshot(h);return s?object(stopAtAvatar?s->rootEditID:s->rootID):std::nullopt;}
 std::function<bool()> guard(ObjectHandle h){return [this,h](){return bool(objectSnapshot(h));};}
};
struct VSVulkanContext {VSWorldScene scene;VSWorldScene& world(){return scene;}};
struct Window {
 bool native=false;VSVulkanContext* context=nullptr;
 bool isNativeVulkan()const{return native;}VSVulkanContext* nativeContext()const{return context;}
}window,*gViewerWindow=&window;
struct VSNativeSession {
 enum class Phase {Connected,Disconnected};Phase state=Phase::Connected;int generation=1;
 static std::shared_ptr<VSNativeSession> instance;static auto active(){return instance;}
 Phase phase()const{return state;}int account()const{return generation;}
 bool current(int value)const{return value==generation;}
};
std::shared_ptr<VSNativeSession> VSNativeSession::instance;
CONTEXT_HEADER
CONTEXT_SOURCE
int main(){
 LLViewerObject object,root;root.id=2;object.root=&root;object.edit=&root;gObjectList.objects[1]=&object;
 auto value=VSObjectContext::resolve(1);assert(value&&value->current());assert(value->rootID==2&&value->youOwner);
 gAgent.session++;assert(!value->current());gAgent.session--;
 object.dead=true;assert(!value->current()&&!VSObjectContext::resolve(1));object.dead=false;
 object.edit=&object;assert(!value->current());object.edit=&root;
 object.region.host++;assert(!value->current());object.region.host--;
 gObjectList.objects.clear();assert(!value->current());gObjectList.objects[1]=&object;
 VSVulkanContext context;window.context=&context;window.native=true;
 VSNativeSession::instance=std::make_shared<VSNativeSession>();auto owner=VSNativeSession::instance;
 context.scene.objects[1]={{1,1},2,2};context.scene.objects[2]={{2,1},2,2};
 auto native=VSObjectContext::resolve(1);assert(native&&native->current());assert(native->rootEditID==2&&native->localID==12);
 context.scene.objects[1].rootEditID=1;assert(!native->current());context.scene.objects[1].rootEditID=2;
 context.scene.objects[2].object.generation++;assert(!native->current());context.scene.objects[2].object.generation--;
 owner->generation++;assert(!native->current());owner->generation--;
 owner->state=VSNativeSession::Phase::Disconnected;assert(!native->current()&&!VSObjectContext::resolve(1));owner->state=VSNativeSession::Phase::Connected;
 context.scene.objects.erase(1);assert(!native->current()&&!VSObjectContext::resolve(1));
 // Never fall through to a stale GL object while the native renderer is active.
 window.context=nullptr;assert(!VSObjectContext::resolve(1));
 bool expected=false;
#ifdef HACKED_GODLIKE_VIEWER
 expected=true;
#endif
 assert(VSObjectContext::ownedByAgent(0)==expected);
 assert(VSObjectContext::ownedByAgent(FLAGS_OBJECT_YOU_OWNER));
 LLGridManager::getInstance()->beta=true;gAgent.god=GOD_MAINTENANCE;
#ifdef TOGGLE_HACKED_GODLIKE_VIEWER
 expected=true;
#endif
 assert(VSObjectContext::ownedByAgent(0)==expected);
}
