#include <algorithm>
#include <cassert>
#include <cmath>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>
using LLUUID=int;using F64=double;using S32=int;
struct LLVector3d {
 double x=0,y=0,z=0;
 LLVector3d operator-(const LLVector3d& b)const{return {x-b.x,y-b.y,z-b.z};}
 double magVecSquared()const{return x*x+y*y+z*z;}
 double magVec()const{return std::sqrt(magVecSquared());}
};
int earLocation=0;
template<class T>struct LLUICachedControl {LLUICachedControl(const char*,T){}operator T()const{return earLocation;}};
struct Agent{LLVector3d position;LLVector3d getPositionGlobal()const{return position;}}gAgent;
struct Camera{LLVector3d position;LLVector3d getCameraPositionGlobal()const{return position;}}gAgentCamera;
struct LLFrameTimer{static inline double now=10;static double getTotalSeconds(){return now;}};
struct LLMuteList{
 std::set<int> muted;static LLMuteList* getInstance(){static LLMuteList m;return &m;}
 static LLMuteList& instance(){return *getInstance();}
 bool isMuted(int id){return muted.contains(id);}
};
struct LLPermissions{int owner=0;int getOwner()const{return owner;}};
struct LLVOVolume{
 int id=0;bool hud=false;LLVector3d position;std::optional<LLPermissions> permission;
 int getID()const{return id;}bool isHUDAttachment()const{return hud;}
 LLVector3d getPositionGlobal()const{return position;}
};
struct LLSelectMgr{
 static LLSelectMgr* getInstance(){static LLSelectMgr manager;return &manager;}
 LLPermissions* findObjectPermissions(LLVOVolume* object){return object->permission?&*object->permission:nullptr;}
};
namespace VSWorldScene{struct ObjectSnapshot{int object=0;bool hud=false;LLVector3d position;};}
std::map<int,LLVOVolume> objects;
std::optional<VSWorldScene::ObjectSnapshot> nativeMediaObject(int id){
 const auto i=objects.find(id);if(i==objects.end())return {};
 return VSWorldScene::ObjectSnapshot{id,i->second.hud,i->second.position};
}
struct NativeSelection {
 struct Properties{int owner_id=0;};struct Publication{std::shared_ptr<Properties> properties;};
 std::optional<Publication> findProperties(int id)const{
  const auto i=objects.find(id);if(i==objects.end()||!i->second.permission)return {};
  return Publication{std::make_shared<Properties>(Properties{i->second.permission->owner})};
 }
};
struct VSNativeSession{
 NativeSelection selection;bool current=true;
 static VSNativeSession* active(){static VSNativeSession owner;return owner.current?&owner:nullptr;}
 NativeSelection* existingObjectSelection(){return &selection;}
};
struct GLMedia{
 std::list<LLVOVolume*> mObjectList;
 double mProximityDistance=0,mProximityCamera=0;bool mIsMuted=false,mNeedsMuteCheck=true;
 void calculateInterest();
};
struct LLViewerMediaImpl{
 struct NativeSurface{F64 pixels=0,distance=0,updated=0;};
 std::map<int,NativeSurface> mNativeSurfaces;std::list<int> mNativeSurfaceOrder;
 F64 mInterest=0,mProximityDistance=0,mProximityCamera=0;
 bool mIsMuted=false,mNeedsMuteCheck=true;unsigned legacyCalls=0;
 void setNativeSurface(const LLUUID&,F64,F64);void removeNativeSurface(const LLUUID&);
 std::optional<LLVector3d> nativeMediaSpatialPosition()const;
 void calculateInterest(){BRIDGE ++legacyCalls;}
};
METHODS
REFERENCE
int main(){
 for(int mask=0;mask<1024;++mask){
  objects.clear();LLMuteList::instance().muted.clear();LLFrameTimer::now=10;
  gAgent.position={3,4,5};gAgentCamera.position={7,8,9};earLocation=(mask&512)!=0;
  GLMedia reference;LLViewerMediaImpl actual;
  for(int n=0;n<3;++n){
   const int id=3-n;auto& o=objects[id];o.id=id;o.position={double(n*7+1),double(n*3+2),double(n*5+3)};o.hud=(mask&(1<<n));
   if(mask&(1<<(n+3)))o.permission=LLPermissions{n+10};
   if(mask&(1<<(n+6)))LLMuteList::instance().muted.insert(n%2?id:n+10);
   reference.mObjectList.push_back(&o);actual.setNativeSurface(id,(n+1)*400,1000-n);
  }
  reference.calculateInterest();actual.calculateInterest();
  assert(actual.mInterest==1200 && actual.mIsMuted==reference.mIsMuted);
  assert(actual.mProximityDistance==reference.mProximityDistance && actual.mProximityCamera==reference.mProximityCamera);
  // Updates keep original source order; visibility expiry does not erase the
  // object's source/owner mute policy or its reference proximity position.
  actual.setNativeSurface(1,50,1);LLFrameTimer::now=12;actual.calculateInterest();
  assert(actual.mInterest==0 && actual.mIsMuted==reference.mIsMuted && actual.mNativeSurfaceOrder.front()==3);
  actual.removeNativeSurface(3);reference.mObjectList.pop_front();reference.mNeedsMuteCheck=true;
  reference.calculateInterest();actual.calculateInterest();
  assert(actual.mIsMuted==reference.mIsMuted && actual.mProximityDistance==reference.mProximityDistance && actual.mProximityCamera==reference.mProximityCamera);
  actual.setNativeSurface(4,NAN,1);assert(!actual.mNativeSurfaces.contains(4));
  actual.removeNativeSurface(2);actual.removeNativeSurface(1);actual.calculateInterest();assert(actual.legacyCalls==1);
 }
}
