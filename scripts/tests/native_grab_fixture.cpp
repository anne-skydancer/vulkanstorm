#include <cassert>
#include <array>
#include <functional>
#include <string>
#include <vector>
#include <sstream>
using U32=unsigned;using S32=int;
using LLHost=int;using LLUUID=int;
struct LLVector2{float x=0,y=0;};
struct LLVector3{float x=0,y=0,z=0;LLVector3()=default;LLVector3(LLVector2 v):x(v.x),y(v.y){};LLVector3(float a,float b,float c):x(a),y(b),z(c){};};
struct LLPickInfo{LLVector2 mUVCoords{.2f,.8f},mSTCoords{.3f,.9f};int mObjectFace=4;LLVector3 mIntersection{1,2,3},mNormal{4,5,6},mBinormal{7,8,9};};
constexpr const char*_PREHASH_ObjectGrab="ObjectGrab",*_PREHASH_ObjectDeGrab="ObjectDeGrab",*_PREHASH_AgentData="AgentData",*_PREHASH_AgentID="AgentID",*_PREHASH_SessionID="SessionID",*_PREHASH_ObjectData="ObjectData",*_PREHASH_LocalID="LocalID",*_PREHASH_GrabOffset="GrabOffset",*_PREHASH_FaceIndex="FaceIndex";
namespace REFERENCE{
COMMON
ORIGINAL
}
namespace CURRENT{
COMMON
HELPERS
WRAPPERS
}
int main(){
 for(bool present:{false,true})for(bool live:{false,true}){
  LLPickInfo pick;LLVector3 offset{.125f,-2.5f,4};
  REFERENCE::message.events.clear();CURRENT::message.events.clear();
  REFERENCE::LLViewerObject a;CURRENT::LLViewerObject b;a.valid=b.valid=live;
  REFERENCE::send_ObjectGrab_message(present?&a:nullptr,pick,offset);REFERENCE::send_ObjectDeGrab_message(present?&a:nullptr,pick);
  CURRENT::send_ObjectGrab_message(present?&b:nullptr,pick,offset);CURRENT::send_ObjectDeGrab_message(present?&b:nullptr,pick);
  assert(REFERENCE::message.events==CURRENT::message.events);assert(a.regionCalls==b.regionCalls);
  CURRENT::message.events.clear();CURRENT::VSObjectContext context;context.current=[=]{return present&&live;};
  CURRENT::send_ObjectGrab_message(context,pick,offset);CURRENT::send_ObjectDeGrab_message(context,pick);
  assert(REFERENCE::message.events==CURRENT::message.events);
 }
}
