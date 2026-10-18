#include <functional>
#include <vector>
#include <cassert>
using LLUUID=int;struct LLVector3{static LLVector3 zero;};LLVector3 LLVector3::zero{};struct LLPickInfo{LLVector3 mObjectOffset;};
std::vector<int>events;bool exists=true,rlv=false,allowed=true;struct LLViewerObject{}object;
struct LLSelectMgr{static LLSelectMgr*getInstance(){static LLSelectMgr s;return &s;}LLSelectMgr*getSelection(){return this;}LLViewerObject*getPrimaryObject(){events.push_back(1);return exists?&object:nullptr;}};
struct LLToolPie{static LLToolPie*getInstance(){static LLToolPie s;return &s;}LLPickInfo getPick(){events.push_back(2);return {};}};
struct RlvActions{static bool isRlvEnabled(){events.push_back(3);return rlv;}static bool canTouch(LLViewerObject*,LLVector3){events.push_back(4);return allowed;}};
#define RLV_ASSERT(x) events.push_back(5)
void send_ObjectGrab_message(LLViewerObject*,const LLPickInfo&,LLVector3){events.push_back(6);}void send_ObjectDeGrab_message(LLViewerObject*,const LLPickInfo&){events.push_back(7);}
PEER
SHARED
ORIGINAL
struct NativePeer{struct Object{bool exists;explicit operator bool()const{return exists;}};bool live=true;mutable bool still=true;bool retireOnGrab=false;Object primary()const{events.push_back(1);return {exists};}bool canTouch(Object,LLVector3)const{events.push_back(4);return allowed;}bool current(Object)const{return live&&still;}void grab(Object,const LLPickInfo&)const{events.push_back(6);if(retireOnGrab)still=false;}void release(Object,const LLPickInfo&)const{events.push_back(7);}};
int main(){for(int flags=0;flags<8;++flags){exists=flags&1;rlv=flags&2;allowed=flags&4;events.clear();originalTouch();auto expected=events;events.clear();requestObjectTouch(ReferenceMenuTouch{});assert(events==expected);events.clear();requestObjectTouch(NativePeer{});assert(events==expected);}exists=true;rlv=false;allowed=true;events.clear();NativePeer retired;retired.live=false;requestObjectTouch(retired);for(int e:events)assert(e!=6&&e!=7);events.clear();NativePeer switching;switching.retireOnGrab=true;requestObjectTouch(switching);assert(events.back()==6);}
