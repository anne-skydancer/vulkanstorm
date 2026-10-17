#include "vsparcelbitmap.h"
#include "vsparcelrequests.h"
#include <cassert>
#include <array>
#include <map>
#include <string>
#include <limits>
using U8=unsigned char;using S32=int;
constexpr U8 SOUTH_MASK=1,WEST_MASK=2;
struct LLViewerParcelMgr{bool mNativeCpu=false;int mParcelsPerEdge=64,mNativeParcelRows=64;void writeSegmentsFromBitmap(const U8*,U8*);};
REFERENCE
int main(){
 VSResourceBudget budget(1000000);auto reserve=[&](std::size_t n){return budget.reserve(n);};
 std::vector<U8> bitmap(512);for(std::size_t i=0;i<bitmap.size();++i)bitmap[i]=U8((i*73+i/7)^0x92);
 auto snapshot=VSParcelBitmap::fromWire(64,64,bitmap.data(),bitmap.size(),reserve);assert(snapshot && snapshot->columns()==64 && snapshot->rows()==64);auto prior=snapshot;
 auto emptyReserve=[](std::size_t){return std::shared_ptr<const VSResourceBudget::Reservation>{};};assert(!VSParcelBitmap::fromWire(64,64,bitmap.data(),bitmap.size(),emptyReserve));assert(prior->bytes()==bitmap);
 std::vector<U8> expected(65*65),actual(expected.size());LLViewerParcelMgr manager;manager.writeSegmentsFromBitmap(bitmap.data(),expected.data());assert(vs_write_parcel_segments(bitmap.data(),bitmap.size(),64,64,actual.data(),actual.size(),SOUTH_MASK,WEST_MASK));assert(actual==expected);
 auto before=actual;assert(!vs_write_parcel_segments(bitmap.data(),bitmap.size()-1,64,64,actual.data(),actual.size(),1,2));assert(before==actual);
 std::array<U8,4> tiny{0xff,0xff,0xff,0xff};auto rectangle=VSParcelBitmap::fromWire(4,8,tiny.data(),4,reserve);assert(rectangle && rectangle->cell(3,7)==true && !rectangle->cell(4,7));std::vector<U8> border(5*9);assert(vs_write_parcel_segments(tiny.data(),4,4,8,border.data(),border.size(),1,2));for(int y=0;y<=8;++y)for(int x=0;x<=4;++x)assert(border[y*5+x]==U8(((y==0||y==8)&&x<4?1:0)|((x==0||x==4)&&y<8?2:0)));
 assert(!vs_write_parcel_segments(tiny.data(),4,std::numeric_limits<unsigned>::max(),std::numeric_limits<unsigned>::max(),border.data(),0,1,2));
 VSParcelRequests requests;int account=1,circuit=1,selection=1;auto guard=[&]{return account==1 && circuit==1 && selection==1;};using Channel=VSParcelRequests::Channel;
 auto selected=requests.issue(Channel::Selection,-10000,guard);auto hover=requests.issue(Channel::Hover,-50000,guard);assert(selected && hover && *selected!=*hover);assert(requests.properties(*selected)->logical==-10000 && requests.properties(*hover)->logical==-50000);
 auto newer=requests.issue(Channel::Selection,-10000,guard);assert(newer && !requests.properties(*selected) && requests.properties(*newer));++selection;assert(!requests.properties(*newer) && !requests.properties(*hover));assert(!requests.issue(Channel::Selection,-10000,guard));selection=1;
 auto access=requests.issue(Channel::Access,0,guard,42);assert(access && requests.match(Channel::Access,*access,42) && !requests.match(Channel::Access,*access,43));++account;assert(!requests.match(Channel::Access,*access,42));account=1;++circuit;assert(!requests.match(Channel::Access,*access,42));circuit=1;
 requests.retire();assert(!requests.properties(*newer) && !requests.match(Channel::Access,*access,42));auto next=requests.issue(Channel::Selection,-10000,guard);assert(next && *next!=*selected && *next!=*newer);
 auto reentrant=requests.issue(Channel::Hover,-50000,[]{return true;});assert(reentrant);bool retireDuringMatch=false;auto reentrantGuard=[&]{if(retireDuringMatch)requests.retire(Channel::Hover);return true;};reentrant=requests.issue(Channel::Hover,-50000,reentrantGuard);retireDuringMatch=true;assert(!requests.properties(*reentrant));
}
