#include "vsparceloverlay.h"
#include <cassert>
#include <map>
#include <vector>
using U64=unsigned long long;using S32=int;using U8=unsigned char;using LLHost=int;
struct World {VSResourceBudget budget{100000};auto reserveSpatialBytes(std::size_t n){return budget.reserve(n);}};
struct Context {World value;World& world(){return value;}};
struct Window {Context* context;auto nativeContext(){return context;}};
Window* gViewerWindow=nullptr;
struct VSNativeSession {
 enum class Phase{Disconnected,Login,Connected};
 bool mShutdown=false;Phase mPhase=Phase::Connected;U64 mGeneration=4;
 struct Circuit{U64 incarnation=8;bool acknowledged=true,handshake=true;unsigned width=512,height=1024;VSParcelOverlay::Snapshot parcelOverlay;};
 std::map<LLHost,Circuit> mCircuits;
 bool receiveParcelOverlay(const LLHost&,U64,U64,S32,const std::vector<U8>&);
};
CONSUMER
int main(){
 Context context;Window window{&context};gViewerWindow=&window;VSNativeSession owner;owner.mCircuits[10]={};std::vector<U8> bytes(1024,0x41);
 auto receive=[&](int host=10,U64 account=4,U64 circuit=8,int sequence=0){return owner.receiveParcelOverlay(host,account,circuit,sequence,bytes);};
 assert(receive());auto old=owner.mCircuits.at(10).parcelOverlay;assert(old->cell(0,0)==0x41);
 bytes.assign(1024,0x82);assert(!receive(11) && !receive(10,3) && !receive(10,4,7) && !receive(10,4,8,-1));
 assert(owner.mCircuits.at(10).parcelOverlay==old);
 owner.mCircuits.at(10).handshake=false;assert(!receive());owner.mCircuits.at(10).handshake=true;
 owner.mCircuits.at(10).acknowledged=false;assert(!receive());owner.mCircuits.at(10).acknowledged=true;
 owner.mPhase=VSNativeSession::Phase::Login;assert(!receive());owner.mPhase=VSNativeSession::Phase::Connected;
 gViewerWindow=nullptr;assert(!receive());gViewerWindow=&window;
 bytes.resize(1023);assert(!receive());bytes.resize(1024);
 assert(receive(10,4,8,1));assert(!old->cell(0,8) && owner.mCircuits.at(10).parcelOverlay->cell(0,8)==0x82);
 owner.mCircuits.clear();assert(!receive());assert(old->cell(0,0)==0x41);old.reset();assert(context.value.budget.used()==0);
}
