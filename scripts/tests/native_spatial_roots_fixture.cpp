#include "vscoarselocations.h"
#include "vsparceloverlay.h"
#include <cassert>
#include <map>
#include <string>
using U64=unsigned long long;using U32=unsigned;using U8=unsigned char;using S32=int;using F32=float;using LLHost=int;
struct LLUUID {int value=0;bool operator==(const LLUUID& b)const{return value==b.value;}bool operator<(const LLUUID& b)const{return value<b.value;}static const LLUUID null;};const LLUUID LLUUID::null{};
struct LLVector3d {double mdV[3];LLVector3d(double x=0,double y=0,double z=0):mdV{x,y,z}{}};
LLVector3d from_region_handle(U64 handle){return LLVector3d(double(handle),1000,0);}
struct LLSD {
    enum Kind{Undefined,Map,Array,Integer,UUID} kind=Undefined;int number=0;LLUUID id;std::map<std::string,LLSD> fields;std::vector<LLSD> rows;
    LLSD()=default;LLSD(int v):kind(Integer),number(v){}LLSD(LLUUID v):kind(UUID),id(v){}
    static LLSD emptyArray(){LLSD value;value.kind=Array;return value;}
    bool isMap()const{return kind==Map;}bool isArray()const{return kind==Array;}bool isInteger()const{return kind==Integer;}bool isUUID()const{return kind==UUID;}
    bool has(const std::string& key)const{return fields.count(key);}int asInteger()const{return number;}LLUUID asUUID()const{return id;}
    S32 size()const{return S32(rows.size());}void append(LLSD v){kind=Array;rows.push_back(v);}
    LLSD& operator[](const std::string& key){kind=Map;return fields[key];}
    const LLSD& operator[](const std::string& key)const{static const LLSD missing;auto it=fields.find(key);return it==fields.end()?missing:it->second;}
    LLSD& operator[](int i){kind=Array;if(S32(rows.size())<=i)rows.resize(i+1);return rows[i];}
    const LLSD& operator[](int i)const{static const LLSD missing;return i<0 || i>=S32(rows.size())?missing:rows[i];}
};
struct LLAvatarTracker {int updates=0;LLVector3d last;static LLAvatarTracker& instance(){static LLAvatarTracker owner;return owner;}void setTrackedCoarseLocation(LLVector3d p){++updates;last=p;}};
struct VSNativeSession {
    enum class Phase{Login,Connecting,Connected,Logout,Disconnected};bool mShutdown=false;Phase mPhase=Phase::Connected;U64 mGeneration=7;LLHost mHost=1;
    struct AccountHandle {int agent=1,session=1;U64 generation=7;};AccountHandle account()const{return {1,1,mGeneration};}
    struct Region {U64 handle=2000,incarnation=9,flags=0;U32 width=512,height=256;LLUUID id{10},owner{11};std::string name="region";U8 access=13;F32 waterHeight=20;bool waterKnown=true,acknowledged=true,handshake=true;VSCoarseSnapshot<LLUUID> coarse;VSParcelOverlay::Snapshot parcelOverlay;bool estateManager=false,estateManagerKnown=false;U32 objectCapacity=0;bool objectCapacityKnown=false;};
    std::map<LLHost,Region> mCircuits;
    struct RegionSnapshot {AccountHandle account;LLHost host;U64 handle=0,circuit=0,flags=0;U32 width=256,height=256;LLUUID id,owner;std::string name;U8 access=0;F32 waterHeight=0;bool waterKnown=false,current=false;std::vector<VSCoarseSample<LLUUID>> coarse;VSParcelOverlay::Snapshot parcelOverlay;bool estateManager=false,estateManagerKnown=false;U32 objectCapacity=0;bool objectCapacityKnown=false;};
    std::vector<RegionSnapshot> regionSnapshots()const;
    std::vector<VSCoarseSample<LLUUID>> coarseAvatarLocations()const;
    bool receiveCoarse(const LLSD&,const LLHost&,U64,U64,bool legacyUDP=false);
};
METHODS
LLSD message(){LLSD value,index;index["You"]=-1;index["Prey"]=0;value["Index"].append(index);LLSD position;position["X"]=20;position["Y"]=30;position["Z"]=255;value["Location"].append(position);LLSD identity;identity["AgentID"]=LLUUID{5};value["AgentData"].append(identity);return value;}
int main(){
    VSNativeSession session;session.mCircuits[1]={};auto body=message();
    assert(!session.receiveCoarse(body,1,6,9,true));assert(!session.receiveCoarse(body,1,7,8,true));assert(!session.receiveCoarse(body,2,7,9,true));
    session.mCircuits[1].handshake=false;assert(!session.receiveCoarse(body,1,7,9,true));session.mCircuits[1].handshake=true;
    assert(session.receiveCoarse(body,1,7,9,true));assert(session.coarseAvatarLocations().size()==1);auto snapshots=session.regionSnapshots();assert(snapshots.size()==1&&snapshots[0].current&&snapshots[0].width==512&&snapshots[0].coarse[0].position[0]==2040);
    assert(LLAvatarTracker::instance().updates==1&&LLAvatarTracker::instance().last.mdV[2]==1020);
    auto invalid=body;invalid["AgentData"]=LLSD::emptyArray();assert(!session.receiveCoarse(invalid,1,7,9,true));assert(session.coarseAvatarLocations().size()==1);
    invalid=body;invalid["Location"][0]["X"]=300;assert(!session.receiveCoarse(invalid,1,7,9,true));assert(session.coarseAvatarLocations().size()==1);
    assert(session.receiveCoarse(body,1,7,9,false));assert(session.coarseAvatarLocations().empty());assert(snapshots[0].coarse.size()==1);
    session.mCircuits[1].incarnation=10;assert(!session.receiveCoarse(body,1,7,9,true));
    session.mCircuits.clear();assert(session.regionSnapshots().empty()&&session.coarseAvatarLocations().empty());assert(!session.receiveCoarse(body,1,7,9,true));
    session.mCircuits[1]={};session.mPhase=VSNativeSession::Phase::Logout;assert(!session.receiveCoarse(body,1,7,9,true));session.mPhase=VSNativeSession::Phase::Connected;session.mShutdown=true;assert(!session.receiveCoarse(body,1,7,9,true));
}
