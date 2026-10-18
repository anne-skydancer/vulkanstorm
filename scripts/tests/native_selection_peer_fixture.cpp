#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
using U8=unsigned char;using U32=unsigned;
enum ESelectType{SELECT_TYPE_WORLD,SELECT_TYPE_HUD,SELECT_TYPE_ATTACHMENT};
constexpr U32 FLAGS_OBJECT_COPY=1,FLAGS_OBJECT_MOVE=2,FLAGS_AFFECTS_NAVMESH=4;
constexpr U8 LL_PCODE_LEGACY_AVATAR=5;
unsigned bits=0;bool live=true,hasRow=true;
struct Agent{bool isGodlike()const{return bits&(1<<9);}int getGodLevel()const{return 0;}}gAgent;
constexpr int GOD_MAINTENANCE=200;
struct LLGridManager{static LLGridManager*getInstance(){static LLGridManager x;return &x;}bool isInSLBeta(){return false;}};
template<class Override>bool vs_object_you_owner(bool value,Override){return value;}
struct Settings{bool getBOOL(const char*name)const{return bits&(std::string(name)=="SelectOwnedOnly"?1:std::string(name)=="SelectMovableOnly"?2:std::string(name)=="FSSelectCopyableOnly"?4:8);}}gSavedSettings;
struct VSWorldScene{
 struct ObjectHandle{int host=1;};
 struct ObjectSnapshot{ObjectHandle object;U32 rootUpdateFlags=0,updateFlags=0;bool hud=false,attachment=false;U8 pcode=0;};
 ObjectSnapshot row;
 std::optional<ObjectSnapshot>objectSnapshot(ObjectHandle)const{return hasRow?std::optional<ObjectSnapshot>(row):std::nullopt;}
}world;
VSWorldScene*scene(){return &world;}
struct VSObjectContext{static bool ownedByAgent(U32 flags){return flags&8;}};
struct VSNativeSession{int host()const{return 1;}};
struct LLViewerObject{
 static constexpr U8 LL_VO_SURFACE_PATCH=9;
 bool isDead()const{return !live;}bool permYouOwner()const{return VSObjectContext::ownedByAgent(world.row.rootUpdateFlags);}
 bool permMove()const{return world.row.rootUpdateFlags&FLAGS_OBJECT_MOVE;}bool permCopy()const{return world.row.rootUpdateFlags&FLAGS_OBJECT_COPY;}
 bool isPermanentEnforced()const{return(world.row.updateFlags&FLAGS_AFFECTS_NAVMESH)&&world.row.object.host!=1&&!gAgent.isGodlike();}
 bool isOrphaned()const{return false;}bool isAvatar()const{return world.row.pcode==LL_PCODE_LEGACY_AVATAR;}U8 getPCode()const{return world.row.pcode;}
};
struct LLObjectSelection{ESelectType mSelectType=SELECT_TYPE_WORLD;int getObjectCount()const{return bits&(1<<10)?1:0;}};
struct LLSelectMgr{
 bool mForceSelection=false;LLObjectSelection selected;LLObjectSelection*mSelectedObjects=&selected;
 ESelectType getSelectTypeForObject(LLViewerObject*)const{return world.row.hud?SELECT_TYPE_HUD:world.row.attachment?SELECT_TYPE_ATTACHMENT:SELECT_TYPE_WORLD;}
 bool canSelectObject(LLViewerObject*,bool);
};
REFERENCE_METHOD
SHARED
struct VSObjectSelection{
 struct Impl{std::weak_ptr<VSNativeSession>session;bool current(VSWorldScene::ObjectHandle)const{return live;}};
 std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();
 std::vector<VSWorldScene::ObjectHandle>selected()const{return bits&(1<<10)?std::vector<VSWorldScene::ObjectHandle>{{}}:std::vector<VSWorldScene::ObjectHandle>{};}
 bool canSelect(const VSWorldScene::ObjectHandle&,bool,bool)const;
};
CURRENT_METHOD
int main(){
 auto owner=std::make_shared<VSNativeSession>();VSObjectSelection native;native.mImpl->session=owner;
 for(bits=0;bits<65536;++bits){
  world.row.rootUpdateFlags=((bits&16)?FLAGS_OBJECT_COPY:0)|((bits&32)?FLAGS_OBJECT_MOVE:0)|((bits&64)?8:0);
  world.row.updateFlags=(bits&128)?FLAGS_AFFECTS_NAVMESH:0;world.row.object.host=(bits&256)?2:1;
  world.row.hud=bits&(1<<11);world.row.attachment=bits&(1<<12);world.row.pcode=(bits&(1<<13))?LL_PCODE_LEGACY_AVATAR:(bits&(1<<14))?LLViewerObject::LL_VO_SURFACE_PATCH:0;
  LLSelectMgr reference;reference.mForceSelection=bits&(1<<15);reference.selected.mSelectType=reference.getSelectTypeForObject(nullptr);
  LLViewerObject object;
  for(bool ignore:{false,true})assert(reference.canSelectObject(&object,ignore)==native.canSelect(world.row.object,reference.mForceSelection,ignore));
 }
 live=false;assert(!native.canSelect({},true,true));live=true;hasRow=false;assert(!native.canSelect({},true,true));hasRow=true;owner.reset();assert(!native.canSelect({},true,true));
}
