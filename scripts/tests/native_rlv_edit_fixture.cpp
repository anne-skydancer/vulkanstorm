#include <cassert>
#include <string>
#include <vector>
#include <functional>
using LLUUID=int;
struct LLVector3{float x=0,y=0,z=0;};
struct LLVector3d{float x=0,y=0,z=0;LLVector3d()=default;LLVector3d(float a,float b,float c):x(a),y(b),z(c){}LLVector3d(LLVector3 v):x(v.x),y(v.y),z(v.z){}friend LLVector3d operator+(LLVector3d a,LLVector3d b){return {a.x+b.x,a.y+b.y,a.z+b.z};}};
float dist_vec_squared(LLVector3d a,LLVector3d b){return (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z);}
constexpr int RLV_BHVR_EDIT=1,RLV_BHVR_EDITOBJ=2,RLV_BHVR_EDITATTACH=4,RLV_BHVR_EDITWORLD=8,RLV_BHVR_INTERACT=16,RLV_BHVR_FARTOUCH=32,RLV_MODIFIER_FARTOUCHDIST=0;
namespace REFERENCE{
COMMON
REFERENCE_METHODS
}
namespace CURRENT{
COMMON
CURRENT_METHODS
}
int main(){for(unsigned bits=0;bits<2048;++bits)for(bool present:{false,true}){
 REFERENCE::bits=CURRENT::bits=bits;REFERENCE::LLViewerObject a;CURRENT::LLViewerObject b;LLVector3 offset{.25f,0,0};
 REFERENCE::calls.clear();CURRENT::calls.clear();auto before=REFERENCE::RlvActions::canEdit(present?&a:nullptr),after=CURRENT::RlvActions::canEdit(present?&b:nullptr);assert(before==after);assert(REFERENCE::calls==CURRENT::calls);
 REFERENCE::calls.clear();CURRENT::calls.clear();before=REFERENCE::RlvActions::canInteract(present?&a:nullptr,offset);after=CURRENT::RlvActions::canInteract(present?&b:nullptr,offset);assert(before==after);assert(REFERENCE::calls==CURRENT::calls);
 if(present){CURRENT::VSObjectContext native;native.attachment=bits&64;native.hud=bits&128;native.position={bits&256?10.f:1.f,0,0};native.current=[]{return true;};assert(REFERENCE::RlvActions::canEdit(&a)==CURRENT::RlvActions::canEdit(native));assert(REFERENCE::RlvActions::canInteract(&a,offset)==CURRENT::RlvActions::canInteract(native,offset));native.current=[]{return false;};CURRENT::calls.clear();assert(!CURRENT::RlvActions::canEdit(native)&&!CURRENT::RlvActions::canInteract(native,offset));assert(CURRENT::calls.empty());}
}}
