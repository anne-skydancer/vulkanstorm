#include <set>
#include <vector>
#include <algorithm>
#include <functional>
#include <cassert>
struct LLHUDNameTag;
template<class T>struct LLPointer{
 T* value=nullptr;
 LLPointer()=default;LLPointer(T* p):value(p){if(value)++value->references;}
 LLPointer(const LLPointer& p):LLPointer(p.value){}
 LLPointer(LLPointer&& p):value(p.value){p.value=nullptr;}
 ~LLPointer(){if(value)--value->references;}
 LLPointer& operator=(LLPointer&& p){if(value)--value->references;value=p.value;p.value=nullptr;return *this;}
 T* get()const{return value;}
 friend bool operator<(const LLPointer& a,const LLPointer& b){return a.value<b.value;}
};
struct LLHUDObject{bool dead=false;unsigned marks=0;void markDead(){dead=true;++marks;}};
struct LLHUDNameTag:LLHUDObject{
 int references=0;std::function<void()> mNativeSource;bool mNativePresented=true,mNativePending=true;
 static std::set<LLPointer<LLHUDNameTag>> sTextObjects;
 static std::vector<LLPointer<LLHUDNameTag>> sVisibleTextObjects;
 void markDead();
};
std::set<LLPointer<LLHUDNameTag>> LLHUDNameTag::sTextObjects;
std::vector<LLPointer<LLHUDNameTag>> LLHUDNameTag::sVisibleTextObjects;
CURRENT
int main(){
 LLHUDNameTag old,peer;
 LLPointer<LLHUDNameTag> sharedHud(&old),nativeOwner(&old),peerOwner(&peer);
 unsigned callback=0;old.mNativeSource=[&]{++callback;};peer.mNativeSource=[]{};
 LLHUDNameTag::sTextObjects.emplace(&old);LLHUDNameTag::sTextObjects.emplace(&peer);
 LLHUDNameTag::sVisibleTextObjects.emplace_back(&old);LLHUDNameTag::sVisibleTextObjects.emplace_back(&peer);
 assert(old.references==4);old.markDead();
 assert(old.dead&&old.marks==1&&!old.mNativeSource&&!old.mNativePresented);
 assert(old.references==2);assert(LLHUDNameTag::sVisibleTextObjects.size()==1&&LLHUDNameTag::sVisibleTextObjects[0].get()==&peer);
 assert(LLHUDNameTag::sTextObjects.size()==1&&LLHUDNameTag::sTextObjects.begin()->get()==&peer);
 assert(!peer.dead&&peer.mNativePresented&&callback==0);
 old.markDead();assert(old.references==2&&old.marks==2);
 LLHUDNameTag::sVisibleTextObjects.clear();LLHUDNameTag::sTextObjects.clear();
 assert(peer.references==1);
}
