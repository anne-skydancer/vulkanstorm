#include <cassert>
#include <vector>
struct LLHUDNameTag {
 bool mNativeSource=true,mNativePresented=false,mNativePending=false;
 struct Pointer {LLHUDNameTag* p;operator LLHUDNameTag*()const{return p;}};
 static inline std::vector<Pointer> sTextObjects;
 static void finishNative(bool);
};
CURRENT
int main(){
 LLHUDNameTag first,missing,peer;first.mNativePending=true;peer.mNativeSource=false;peer.mNativePresented=true;
 LLHUDNameTag::sTextObjects={{&first},{&missing},{&peer}};
 LLHUDNameTag::finishNative(false);assert(!first.mNativePresented&&!missing.mNativePresented&&peer.mNativePresented&&!first.mNativePending);
 first.mNativePending=true;LLHUDNameTag::finishNative(true);assert(first.mNativePresented&&!missing.mNativePresented&&peer.mNativePresented);
 LLHUDNameTag::finishNative(true);assert(!first.mNativePresented); // no new actual submission
}
