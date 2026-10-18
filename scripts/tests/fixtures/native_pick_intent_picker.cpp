#include <cassert>
#include <functional>
#include <cstdint>
#define VS_NATIVE_VULKAN 1
using U64=uint64_t;
struct UUID {int value=0;void setNull(){value=0;}bool notNull()const{return value!=0;}};
struct LLPickInfo {UUID mObjectID;std::function<bool()> mNativeRequestCurrent;};
struct VSNativeSession{static bool active(){return true;}};
struct LLToolObjPicker {
 static LLToolObjPicker* live;static bool instanceExists(){return live;}static LLToolObjPicker* getInstance(){return live;}
 U64 mNativePickGeneration=0;bool capture=true,mPicked=false;UUID mHitObjectID;
 void(*mExitCallback)(void*)=nullptr;void*mExitCallbackData=nullptr;
 bool hasMouseCapture()const{return capture;}
 std::function<bool()> nativePickGuard();void onMouseCaptureLost();void setExitCallback(void(*)(void*),void*);
 static void pickCallback(const LLPickInfo&);
};
LLToolObjPicker* LLToolObjPicker::live=nullptr;
GUARD
LOST
SET
CALLBACK
struct Reenter{LLToolObjPicker* tool;int calls=0;};
void next(void*){}
void reopen(void* raw){auto&r=*static_cast<Reenter*>(raw);++r.calls;assert(r.tool->mPicked&&r.tool->mHitObjectID.value==17);r.tool->setExitCallback(next,nullptr);r.tool->mPicked=true;r.tool->mHitObjectID.value=53;}
int main(){
 LLToolObjPicker a;LLToolObjPicker::live=&a;auto first=a.nativePickGuard();assert(first());
 LLPickInfo pick{{17},first};LLToolObjPicker::pickCallback(pick);assert(a.mPicked&&a.mHitObjectID.value==17);
 a.setExitCallback(next,nullptr);assert(!first()&&!a.mPicked);LLToolObjPicker::pickCallback(pick);assert(!a.mPicked);
 auto second=a.nativePickGuard();a.capture=false;assert(!second());a.capture=true;
 auto third=a.nativePickGuard();auto fourth=a.nativePickGuard();assert(!third()&&fourth());
 LLToolObjPicker::live=nullptr;assert(!fourth());LLToolObjPicker b;LLToolObjPicker::live=&b;assert(!fourth());LLToolObjPicker::live=&a;
 Reenter state{&a};a.setExitCallback(reopen,&state);auto old=a.nativePickGuard();a.mPicked=true;a.mHitObjectID.value=17;a.onMouseCaptureLost();assert(!old()&&state.calls==1&&a.mPicked&&a.mHitObjectID.value==53);assert(a.mExitCallback==next);
 a.onMouseCaptureLost();assert(!a.mPicked&&a.mHitObjectID.value==0&&!a.mExitCallback);
}
