#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <cmath>
using U64=unsigned long long;using U32=unsigned;using U8=unsigned char;using S32=int;using F32=float;using LLHost=int;using LLUUID=int;
constexpr U64 REGION_FLAGS_SUN_FIXED=16;
PREHASH
std::ostringstream logs;
#define LL_DEBUGS(...) logs
#define LL_INFOS(...) logs
#define LL_ENDL ""
struct LLMessageSystem {
 std::string name="RegionInfo";LLHost sender=1;LLUUID agent=1,session=2;bool extended=true,product=true;U64 flags=0x800000001ULL;F32 billable=2.5,water=32;
 const char* getMessageName()const{return name.c_str();}LLHost getSender()const{return sender;}
 void getUUID(const char*,const char* key,LLUUID& out){out=std::string(key)=="AgentID"?agent:session;}
 void getStringFast(const char*,const char*,std::string& out){out="actual-sim";}void getString(const char*,const char*,std::string& out){out="actual-product";}
 void getU32Fast(const char*,const char* key,U32& out){out=std::string(key)=="RegionFlags"?U32(flags):99;}
 void getU64Fast(const char*,const char*,U64& out){out=flags;}
 void getU8Fast(const char*,const char* key,U8& out){out=std::string(key)=="SimAccess"?42:75;}
 void getF32Fast(const char*,const char* key,F32& out){getF32(nullptr,key,out);}void getF32(const char*,const char* key,F32& out){std::string n=key;out=n=="BillableFactor"?billable:n=="WaterHeight"?water:3.5;}
 void getS32Fast(const char*,const char*,S32& out){out=101;}
 void getBOOL(const char*,const char*,bool& out){out=true;}
 bool has(const char* key)const{return std::string(key)=="RegionInfo3"?extended:false;}int getSize(const char*,const char*)const{return product?10:0;}
};
struct LLRegionInfoModel {
 FIELDS
 std::function<void()> mUpdateSignal=[]{};static LLRegionInfoModel& instance(){static LLRegionInfoModel m;return m;}
 void reset();void update(LLMessageSystem*,bool=true);
};
MODEL_METHODS
struct Agent {bool flying=false;bool getFlying()const{return flying;}bool canFly()const{return false;}void setFlying(bool value){flying=value;}}gAgent;
struct VSNativeSession {
 U64 generation=7,mRegionEpoch=3;LLUUID mAgent=1,mSession=2;LLHost mHost=1;bool ready=true;
 std::string mName;U8 mAccess=0;U64 mFlags=0;F32 mWaterHeight=0;bool mWaterKnown=false;
 struct Metadata{F32 billableFactor=1;bool billableKnown=false;std::string productName="unknown";bool productKnown=false;};
 struct Circuit {bool acknowledged=true,handshake=true;U64 incarnation=9;std::string name;U8 access=0;U64 flags=0;F32 waterHeight=0;bool waterKnown=false;Metadata metadata;};std::map<LLHost,Circuit>mCircuits;
 bool regionReady()const{return ready;}std::function<bool()>accountGuard(){const auto before=generation;return [this,before]{return generation==before&&ready;};}
 U64 circuitGeneration(LLHost h)const{auto i=mCircuits.find(h);return i==mCircuits.end()?0:i->second.incarnation;}
 bool applyRegionInfo(LLMessageSystem&);
};
APPLY
int main(){
 VSNativeSession owner;owner.mCircuits[1]={};LLMessageSystem message;auto& model=LLRegionInfoModel::instance();model.reset();int notifications=0;
 model.mUpdateSignal=[&]{++notifications;assert(owner.mFlags==message.flags && owner.mCircuits.at(1).metadata.billableKnown && model.mSimName==owner.mName);};
 message.agent=3;assert(!owner.applyRegionInfo(message) && notifications==0);message.agent=1;message.session=4;assert(!owner.applyRegionInfo(message));message.session=2;message.sender=3;assert(!owner.applyRegionInfo(message));message.sender=1;
 owner.ready=false;assert(!owner.applyRegionInfo(message));owner.ready=true;owner.mCircuits.at(1).handshake=false;assert(!owner.applyRegionInfo(message));owner.mCircuits.at(1).handshake=true;
 gAgent.flying=true;assert(owner.applyRegionInfo(message) && notifications==1 && !gAgent.flying);assert(model.mEstateID==99 && model.mParentEstateID==99 && model.mSimAccess==42 && model.mAgentLimit==75 && model.mHardAgentLimit==101);assert(model.mBillableFactor==2.5 && model.mWaterHeight==32 && model.mUseEstateSun && model.mSunHour==3.5);assert(owner.mCircuits.at(1).metadata.productName=="actual-product");
 model.mUpdateSignal=[&]{++notifications;++owner.generation;owner.mCircuits.clear();};assert(!owner.applyRegionInfo(message) && notifications==2);assert(owner.mCircuits.empty());
 model.reset();assert(model.mRegionFlags==0 && model.mEstateID==0 && model.mSimName.empty() && model.mSimType.empty());
 // Legacy default notification still fires; native suppressed parse publishes no premature signal.
 int legacy=0;model.mUpdateSignal=[&]{++legacy;};message.extended=false;model.update(&message);assert(legacy==1 && model.mRegionFlags==U32(message.flags));model.update(&message,false);assert(legacy==1);
}
