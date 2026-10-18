#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
using U32=uint32_t;using U64=uint64_t;using U8=uint8_t;
void require(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
struct LLUUID{std::string value;LLUUID(std::string s=""):value(s){}std::string asString()const{return value;}void setNull(){value.clear();}static const LLUUID null;};const LLUUID LLUUID::null{};LLUUID gAgentID{"agent"},gAgentSessionID{"session"};
struct Region{bool current()const{return true;}std::string getName()const{return "Actual current region";}U64 getRegionFlags()const{return(U64(1)<<42)|512;}U8 getSimAccess()const{return 21;}float getWaterHeight()const{return 20;}LLUUID getOwner()const{return LLUUID("owner");}};
bool current=true;std::optional<Region>vs_current_region_context(){return current?std::optional<Region>(Region{}):std::nullopt;}
struct VSNativeSession{int host()const{return 77;}};
struct LLMessageSystem{std::string name,block;std::vector<std::string>fields;std::map<std::string,std::string>values;int destination=0;
 void newMessage(const char*n){name=n;fields.clear();values.clear();destination=0;}void nextBlock(const char*n){block=n;fields.push_back(std::string("block:")+n);}template<class T>void add(const char*t,const char*n,const T&v){fields.push_back(std::string(t)+":"+block+"/"+n);values[block+"/"+n]=std::to_string(v);}
 void addString(const char*n,const std::string&v){fields.push_back("string:"+block+"/"+n);values[block+"/"+n]=v;}void addUUID(const char*n,const LLUUID&v){fields.push_back("uuid:"+block+"/"+n);values[block+"/"+n]=v.value;}
 void addU32(const char*n,U32 v){add("u32",n,v);}void addU64(const char*n,U64 v){add("u64",n,v);}void addU8(const char*n,U8 v){add("u8",n,v);}void addF32(const char*n,float v){add("f32",n,v);}void addS32(const char*n,int v){add("s32",n,v);}void addBOOL(const char*n,bool v){add("bool",n,v);}void sendReliable(int host){destination=host;}
}message,*gMessageSystem=&message;
struct Replay{unsigned regionReplies=0;LLUUID estateInvoice{"invoice"};void publishRegionUI(VSNativeSession&);void publishEstateUI(VSNativeSession&);};
METHODS
int main(){Replay state;VSNativeSession owner;state.publishRegionUI(owner);assert(state.regionReplies==1&&message.name=="RegionInfo"&&message.destination==77);assert(message.values["AgentData/AgentID"]=="agent"&&message.values["AgentData/SessionID"]=="session");assert(message.values["RegionInfo/SimName"]=="Actual current region");assert(message.values["RegionInfo3/RegionFlagsExtended"]==std::to_string((U64(1)<<42)|512));assert(message.values["RegionInfo2/MaxAgents32"]=="40"&&message.values["RegionInfo2/HardMaxAgents"]=="100");assert(message.values["RegionInfo/TerrainLowerLimit"]=="-4.000000");
 assert(message.fields==std::vector<std::string>({"block:AgentData","uuid:AgentData/AgentID","uuid:AgentData/SessionID","block:RegionInfo","string:RegionInfo/SimName","u32:RegionInfo/EstateID","u32:RegionInfo/ParentEstateID","u32:RegionInfo/RegionFlags","u8:RegionInfo/SimAccess","u8:RegionInfo/MaxAgents","f32:RegionInfo/BillableFactor","f32:RegionInfo/ObjectBonusFactor","f32:RegionInfo/WaterHeight","f32:RegionInfo/TerrainRaiseLimit","f32:RegionInfo/TerrainLowerLimit","s32:RegionInfo/PricePerMeter","s32:RegionInfo/RedirectGridX","s32:RegionInfo/RedirectGridY","bool:RegionInfo/UseEstateSun","f32:RegionInfo/SunHour","block:RegionInfo2","string:RegionInfo2/ProductSKU","string:RegionInfo2/ProductName","u32:RegionInfo2/MaxAgents32","u32:RegionInfo2/HardMaxAgents","u32:RegionInfo2/HardMaxObjects","block:RegionInfo3","u64:RegionInfo3/RegionFlagsExtended"}));
 state.publishEstateUI(owner);assert(message.name=="EstateOwnerMessage"&&message.destination==77&&state.estateInvoice.asString().empty());assert(message.values["MethodData/Invoice"]=="invoice"&&message.values["MethodData/Method"]=="estateupdateinfo");assert(std::count(message.fields.begin(),message.fields.end(),"block:ParamList")==10);
 current=false;try{state.publishRegionUI(owner);assert(false);}catch(const std::runtime_error&){}assert(state.regionReplies==1);
}
