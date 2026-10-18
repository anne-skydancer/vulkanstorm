#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <array>
#include <set>
#include <limits>
#include <utility>
#include <vector>
using U32=unsigned;using U64=unsigned long long;using LLHost=int;
int mode=0;std::array<unsigned,2>equalities{},links{};
struct LLUUID{unsigned value=0;friend bool operator==(LLUUID a,LLUUID b){++equalities[mode];return a.value==b.value;}friend bool operator!=(LLUUID a,LLUUID b){return !(a==b);}friend bool operator<(LLUUID a,LLUUID b){return a.value<b.value;}};
INDEX
namespace REFERENCE{
COMMON
REFERENCE_METHOD
}
namespace CURRENT{
COMMON
CURRENT_METHOD
}
int main(){
 REFERENCE::VSWorldScene a;CURRENT::VSWorldScene b;
 for(unsigned local=1;local<=10000;++local){
  REFERENCE::VSWorldScene::Impl::Object old;old.id={local%250};old.parent=local/4;old.live=local%3!=0;
  CURRENT::VSWorldScene::Impl::Object now;now.id=old.id;now.parent=old.parent;now.live=old.live;
  a.mImpl->objects[local]=old;b.mImpl->objectIndex.replace(local,(const CURRENT::VSWorldScene::Impl::Object*)nullptr,now);b.mImpl->objects[local]=now;
 }
 assert(b.mImpl->objectIndex.size()==b.mImpl->objects.size());
 for(unsigned target=0;target<280;++target){
  mode=0;auto old=a.object(LLUUID{target});mode=1;auto now=b.object(LLUUID{target});assert(bool(old)==bool(now));if(old)assert(old->local==now->local&&old->host==now->host);
 }
 assert(links[0]==links[1]);assert(equalities[0]>100000&&equalities[1]==0);
 // Reused local IDs remove old identities and parent edges before new ownership.
 auto previous=b.mImpl->objects.at(4);auto next=previous;next.id={999};next.parent=77;
 b.mImpl->objectIndex.replace(4,&previous,next);b.mImpl->objects[4]=next;
 auto [start,end]=b.mImpl->objectIndex.children(77);bool found=false;for(auto it=start;it!=end;++it)found|=it->second==4;assert(found);
 auto [before,after]=b.mImpl->objectIndex.children(previous.parent);for(auto it=before;it!=after;++it)assert(it->second!=4);
 assert(b.object(LLUUID{999})->local==4);
 b.mImpl->objectIndex.erase(4,b.mImpl->objects.at(4));b.mImpl->objects.erase(4);assert(!b.object(LLUUID{999}));
 b.mImpl->objectIndex.clear();b.mImpl->objects.clear();assert(b.mImpl->objectIndex.size()==0&&!b.object(LLUUID{2}));
}
