#include "vsparceloverlay.h"
#include <cassert>
#include <vector>
int main(){
 VSResourceBudget budget(1024*1024);
 auto reserve=[&](std::size_t bytes){return budget.reserve(bytes);};
 std::array<std::uint8_t,1024> first{},second{};first.fill(0x41);second.fill(0x82);
 auto a=VSParcelOverlay::updated({},256,256,0,first.data(),first.size(),reserve);assert(a);
 assert(a->cell(0,0)==0x41 && a->cell(63,15)==0x41 && !a->cell(0,16));
 auto old=a;
 auto b=VSParcelOverlay::updated(a,256,256,1,second.data(),second.size(),reserve);assert(b);
 assert(b->cell(0,16)==0x82 && b->cell(63,31)==0x82 && b->cell(0,0)==0x41);
 assert(!a->cell(0,16)); // Old frame/account snapshots are immutable.
 auto changed=VSParcelOverlay::updated(b,256,256,0,second.data(),second.size(),reserve);assert(changed);
 assert(changed->cell(0,0)==0x82 && b->cell(0,0)==0x41);
 auto rectangular=VSParcelOverlay::updated({},512,1024,31,first.data(),first.size(),reserve);assert(rectangular);
 assert(rectangular->cell(127,255)==0x41 && !rectangular->cell(0,0));
 assert(!rectangular->cell(128,255) && !rectangular->cell(127,256));
 for(int sequence:{-1,32,1000000})assert(!VSParcelOverlay::updated({},512,1024,sequence,first.data(),1024,reserve));
 assert(!VSParcelOverlay::updated(a,256,256,0,first.data(),1023,reserve));
 assert(!VSParcelOverlay::updated(a,256,256,0,nullptr,1024,reserve));
 assert(!VSParcelOverlay::updated(a,258,256,0,first.data(),1024,reserve));
 VSResourceBudget refused(8);auto no=[&](std::size_t n){return refused.reserve(n);};
 assert(!VSParcelOverlay::updated(a,256,256,0,second.data(),1024,no));assert(a->cell(0,0)==0x41 && refused.used()==0);
 auto resized=VSParcelOverlay::updated(b,512,256,2,first.data(),1024,reserve);assert(resized && !resized->cell(0,0));
 a.reset();b.reset();old.reset();changed.reset();rectangular.reset();resized.reset();assert(budget.used()==0);
}
