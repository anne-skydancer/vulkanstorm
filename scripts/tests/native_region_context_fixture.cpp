#include "vscoarselocations.h"
#include "vsparceloverlay.h"
#include <map>
#include <string>
#include <memory>
#include <functional>
#include <optional>
#include <vector>
#include <cmath>
#include <cassert>
#define VS_NATIVE_VULKAN 1
using F32=float;using F64=double;using U64=unsigned long long;using U32=unsigned;using U8=unsigned char;using S32=int;using LLHost=int;
constexpr float PARCEL_GRID_STEP_METERS=4;constexpr U8 PARCEL_WEST_LINE=64,PARCEL_SOUTH_LINE=128;
constexpr int VX=0,VY=1,VZ=2;constexpr U64 REGION_FLAGS_ALLOW_DAMAGE=1,REGION_FLAGS_BLOCK_FLY=2,REGION_FLAGS_ALLOW_VOICE=4,REGION_FLAGS_RESTRICT_PUSHOBJECT=8;
struct LLUUID{int value=0;bool operator==(const LLUUID& o)const{return value==o.value;}bool operator!=(const LLUUID& o)const{return !(*this==o);}bool operator<(const LLUUID& o)const{return value<o.value;}};
struct LLVector3d;
struct LLVector3{float mV[3]{};LLVector3()=default;explicit LLVector3(const LLVector3d&);};
struct LLVector3d{double mdV[3]{};LLVector3d()=default;LLVector3d(double x,double y,double z):mdV{x,y,z}{}explicit LLVector3d(const LLVector3& v):mdV{v.mV[0],v.mV[1],v.mV[2]}{}LLVector3d operator+(const LLVector3d& v)const{return {mdV[0]+v.mdV[0],mdV[1]+v.mdV[1],mdV[2]+v.mdV[2]};}LLVector3d operator-(const LLVector3d& v)const{return {mdV[0]-v.mdV[0],mdV[1]-v.mdV[1],mdV[2]-v.mdV[2]};}};
LLVector3::LLVector3(const LLVector3d& v):mV{float(v.mdV[0]),float(v.mdV[1]),float(v.mdV[2])}{}
LLVector3d from_region_handle(U64 h){return {double(h>>32),double(h&0xffffffff),0};}
struct LegacyOverlay{std::vector<U8> bytes=std::vector<U8>(64*64);const U8* getOwnership()const{return bytes.data();}};
struct LLViewerRegion{bool dynamicPathfindingEnabled()const{return false;}void setOwner(LLUUID value){owner=value;}LegacyOverlay overlay;LegacyOverlay* getParcelOverlay(){return &overlay;}const LegacyOverlay* getParcelOverlay()const{return &overlay;}LLHost host=1;LLUUID id{10},owner{11};U64 handle=U64(1000)<<32|2000,flags=0,protocols=0;std::string name="real-region",product="Mainland";F32 width=256,water=20,billable=1;U32 capacity=15000;bool estate=false;LLVector3d origin=from_region_handle(handle);
 LLHost getHost()const{return host;}LLUUID getRegionID()const{return id;}U64 getHandle()const{return handle;}LLUUID getOwner()const{return owner;}std::string getName()const{return name;}LLVector3d getOriginGlobal()const{return origin;}F32 getWidth()const{return width;}U64 getRegionFlags()const{return flags;}U64 getRegionProtocols()const{return protocols;}U8 getSimAccess()const{return 13;}F32 getWaterHeight()const{return water;}F32 getBillableFactor()const{return billable;}U32 getMaxTasks()const{return capacity;}bool isEstateManager()const{return estate;}std::string getLocalizedSimProductName()const{return product;}std::string getCapability(const std::string& n)const{return "https://legacy.invalid/"+n;}static std::string accessToString(U8 access){return std::to_string(access);}};
struct LLWorld{int touches=0;std::map<LLHost,LLViewerRegion*> regions;static LLWorld& instance(){static LLWorld w;++w.touches;return w;}LLViewerRegion* getRegion(LLHost host){auto i=regions.find(host);return i==regions.end()?nullptr:i->second;}LLViewerRegion* getRegionFromID(LLUUID id){for(auto& [host,r]:regions)if(r->id==id)return r;return nullptr;}LLViewerRegion* getRegionFromPosGlobal(const LLVector3d& p){for(auto& [host,r]:regions){auto l=p-r->origin;if(l.mdV[0]>=0&&l.mdV[1]>=0&&l.mdV[0]<r->width&&l.mdV[1]<r->width)return r;}return nullptr;}};
struct Window{bool native=true;bool isNativeVulkan()const{return native;}}window;Window* gViewerWindow=&window;
struct LLAppViewer{static inline bool native=false;static auto*instance(){static LLAppViewer app;return &app;}bool isNativeVulkanLogin()const{return native;}};
struct Agent{LLUUID id{1},session{2};bool god=false;LLViewerRegion* region=nullptr;LLUUID getID()const{return id;}LLUUID getSessionID()const{return session;}bool isGodlike()const{return god;}LLViewerRegion* getRegion()const{return region;}}gAgent;
bool gDisconnected=false;
struct LLTrans{static bool findString(std::string& value,const std::string& key){if(key=="Mainland"){value="localized-Mainland";return true;}return false;}};
struct VSNativeSession {
 enum class Phase{Connecting,Connected,Logout};Phase mPhase=Phase::Connected;bool mShutdown=false,mChangingRegion=false;U64 mGeneration=7;LLHost mHost=1;
 struct AccountHandle{LLUUID agent{1},session{2};U64 generation=7;};AccountHandle account()const{return {{1},{2},mGeneration};}
 METADATA
 SNAPSHOT
 struct Features{bool enabled=false;const Features&operator[](const char*)const{return *this;}bool asBoolean()const{return enabled;}}features;const Features&simulatorFeatures()const{return features;}
 bool publishRegionOwner(const LLHost&,U64,const LLUUID&);U64 circuitGeneration(const LLHost&host)const{auto found=mCircuits.find(host);return found==mCircuits.end()?0:found->second.incarnation;}
 struct Circuit{bool acknowledged=true,handshake=true;U64 handle=(U64(1000)<<32)|2000,incarnation=9,flags=0;U32 width=512,height=256;LLUUID id{10},owner{11};std::string name="native-real";U8 access=13;F32 waterHeight=20;bool waterKnown=true,estateManager=false,estateManagerKnown=false;U32 objectCapacity=0;bool objectCapacityKnown=false;RegionMetadata metadata;VSCoarseSnapshot<LLUUID> coarse;VSParcelOverlay::Snapshot parcelOverlay;};std::map<LLHost,Circuit> mCircuits;
 static inline std::shared_ptr<VSNativeSession> selected;
 static auto active(){return selected;}bool regionReady()const{return !mShutdown && !mChangingRegion && mPhase==Phase::Connected;}LLHost host()const{return mHost;}
 bool current(const AccountHandle& a)const{return a.agent==gAgent.id && a.session==gAgent.session && a.generation==mGeneration;}
 VSParcelOverlay::Snapshot parcelOverlay(const LLHost&)const;
 bool current(const RegionSnapshot&)const;std::optional<RegionSnapshot> regionSnapshot(const LLHost&,bool=true)const;std::vector<RegionSnapshot> regionSnapshots(bool=true)const;
 std::string capability(LLHost host,const std::string& name)const{return "https://native.invalid/"+std::to_string(host)+"/"+name;}
};
CONTEXT_HEADER
SNAPSHOT_METHODS
CONTEXT_METHODS
int main(){
 auto owner=std::make_shared<VSNativeSession>();VSNativeSession::selected=owner;owner->mCircuits[1]={};
 auto& circuit=owner->mCircuits.at(1);circuit.flags=3;circuit.metadata.billableFactor=0;circuit.metadata.billableKnown=true;circuit.metadata.productName="Mainland";circuit.metadata.productKnown=true;circuit.objectCapacityKnown=true;
 auto context=vs_current_region_context();assert(context && context->current() && context->getWidth()==512 && context->getHeight()==256);assert(context->billableKnown() && context->getBillableFactor()==0 && context->objectCapacityKnown() && context->getMaxTasks()==0);assert(context->getLocalizedSimProductName()=="localized-Mainland");assert(context->getAllowDamage() && context->getBlockFly());
 assert(context->pointInRegionGlobal({1000,2000,-2000}) && context->pointInRegionGlobal({1511.99,2255.99,10000}));assert(!context->pointInRegionGlobal({1512,2000,0}) && !context->pointInRegionGlobal({1000,2256,0}));assert(!vs_region_context(LLVector3d(1600,2000,0)) && vs_region_context(LLVector3d(1500,2000,0)));assert(vs_region_context(LLUUID{10}) && !vs_region_context(LLUUID{99}));
 assert(context->getCapability("ParcelPropertiesUpdate")=="https://native.invalid/1/ParcelPropertiesUpdate");assert(!context->canManageEstate());circuit.estateManager=true;assert(!context->canManageEstate());circuit.estateManagerKnown=true;assert(context->canManageEstate());circuit.estateManager=false;assert(!context->canManageEstate());gAgent.god=true;assert(context->canManageEstate());gAgent.god=false;
 assert(!context->dynamicPathfindingEnabled());owner->features.enabled=true;assert(vs_current_region_context()->dynamicPathfindingEnabled());assert(vs_publish_region_owner(*context,LLUUID{71}));assert(circuit.owner.value==71);
 assert(LLWorld::instance().touches==1); // All native resolution above avoided constructing/touching a legacy world.
 gViewerWindow=nullptr;LLAppViewer::native=true;assert(vs_current_region_context());gViewerWindow=&window;LLAppViewer::native=false;
 circuit.objectCapacityKnown=false;assert(vs_current_region_context()->getMaxTasks()==15000 && !vs_current_region_context()->objectCapacityKnown());circuit.objectCapacityKnown=true;
 assert(!context->parcelFlagsAt({1000,2000,0}) && !context->parcelLineFlags(0,0));
 VSResourceBudget budget(100000);std::array<U8,1024> bytes{};bytes[0]=PARCEL_WEST_LINE|3;auto reserve=[&](std::size_t n){return budget.reserve(n);};circuit.parcelOverlay=VSParcelOverlay::updated({},512,256,0,bytes.data(),bytes.size(),reserve);
 assert(context->parcelFlagsAt({1000,2000,0})==U8(PARCEL_WEST_LINE|3) && context->parcelLineFlags(0,0)==PARCEL_WEST_LINE);assert(!context->parcelFlagsAt({1000,2100,0}) && !context->parcelLineFlags(-1,0));
 auto previous=*context;circuit.metadata.productName="changed";assert(previous.getLocalizedSimProductName()=="localized-Mainland");
 ++circuit.incarnation;assert(!vs_publish_region_owner(previous,LLUUID{99}));assert(circuit.owner.value==71);assert(!previous.current() && previous.getCapability("ParcelPropertiesUpdate").empty());context=vs_current_region_context();++owner->mGeneration;assert(!context->current());context=vs_current_region_context();gAgent.session={3};assert(!context->current());gAgent.session={2};context=vs_current_region_context();owner->mChangingRegion=true;assert(!context->current() && !vs_current_region_context());owner->mChangingRegion=false;
 context=vs_current_region_context();owner->mCircuits.clear();assert(!context->current());
 window.native=false;LLViewerRegion legacy;LLWorld::instance().regions[1]=&legacy;gAgent.region=&legacy;auto shared=vs_current_region_context();assert(shared && shared->current() && shared->getCapability("LandResources")=="https://legacy.invalid/LandResources");gAgent.session={77};assert(!shared->current() && shared->getCapability("LandResources").empty());
}
