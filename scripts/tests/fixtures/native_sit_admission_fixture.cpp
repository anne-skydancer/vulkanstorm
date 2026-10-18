#include <cassert>
#include <set>
#include <map>
#include <string>
#include <sstream>
struct LLHost{int value=1;bool isOk()const{return value!=0;}bool operator==(const LLHost&b)const{return value==b.value;}bool operator<(const LLHost&b)const{return value<b.value;}};
std::ostringstream sink;
#define LL_WARNS(x) sink
#define LL_ENDL ""
const std::set<std::string>transport={"PacketAck"},session={"ChatFromSimulator","RegionInfo","AvatarSitResponse"},worldMessages={"ObjectUpdate","LayerData"};
struct VSNativeSession{enum class Phase{Login,Connecting,Connected,Logout,Disconnected};LLHost mHost;Phase mPhase=Phase::Login;bool mChangingRegion=false,mSeedReady=false,mHandshake=false,mCircuitAck=false;unsigned mRejected=0;struct Region{bool acknowledged=false,handshake=false;};std::map<LLHost,Region>mCircuits;bool admit(const std::string&,const LLHost&);
READY
};
ADMIT
int main(){for(int phase=0;phase<5;++phase)for(int flags=0;flags<64;++flags){VSNativeSession s;s.mPhase=static_cast<VSNativeSession::Phase>(phase);s.mChangingRegion=flags&1;s.mSeedReady=flags&2;s.mHandshake=flags&4;s.mCircuitAck=flags&8;s.mCircuits[{1}]={bool(flags&16),bool(flags&32)};s.mCircuits[{2}]={bool(flags&16),bool(flags&32)};bool expected=phase==2&&!(flags&1)&&(flags&2)&&(flags&4)&&(flags&8)&&(flags&16)&&(flags&32);assert(s.admit("AvatarSitResponse",{1})==expected);assert(s.admit("AvatarSitResponse",{2})==expected);assert(!s.admit("AvatarSitResponse",{3}));s.mCircuits.erase({2});assert(!s.admit("AvatarSitResponse",{2}));}}
