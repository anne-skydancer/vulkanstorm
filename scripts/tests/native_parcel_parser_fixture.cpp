#include "vsparcelbitmap.h"
#include "vsparcelrequests.h"
#include <cassert>
#include <map>
#include <string>
#include <memory>
#include <functional>
#include <optional>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#define VS_NATIVE_VULKAN 1
using U8=unsigned char;using U32=unsigned;using U64=unsigned long long;using S32=int;using F32=float;using F64=double;
constexpr int VX=0,VY=1,VZ=2;constexpr float PARCEL_GRID_STEP_METERS=4;using LLHost=int;
struct LLUUID{int value=0;bool operator==(const LLUUID& b)const{return value==b.value;}bool operator!=(const LLUUID& b)const{return value!=b.value;}};
std::ostream& operator<<(std::ostream& s,LLUUID v){return s<<v.value;}
struct LLVector3{float mV[3]{};LLVector3(float x=0,float y=0,float z=0):mV{x,y,z}{}};
struct LLVector3d{double mdV[3]{};LLVector3d(double x=0,double y=0,double z=0):mdV{x,y,z}{}LLVector3d(LLVector3 v):mdV{v.mV[0],v.mV[1],v.mV[2]}{}void clearVec(){mdV[0]=mdV[1]=mdV[2]=0;}bool isNull()const{return mdV[0]==0&&mdV[1]==0&&mdV[2]==0;}};
std::ostream& operator<<(std::ostream& s,LLVector3 v){return s<<v.mV[0]<<","<<v.mV[1]<<","<<v.mV[2];}
PREHASH
CONSTANTS
constexpr int BA_BANNED=1,BA_NOT_IN_GROUP=2,BA_NOT_ON_LIST=3;constexpr unsigned AL_ACCESS=1,AL_BAN=2,AL_ALLOW_EXPERIENCE=4,AL_BLOCK_EXPERIENCE=8;constexpr float DWELL_NAN=-1;
std::ostringstream logs;
#define LL_DEBUGS(...) logs
#define LL_INFOS(...) logs
#define LL_WARNS(...) logs
#define LL_ENDL ""
struct LLMessageSystem{
 LLHost sender=1;int sequence=1,result=PARCEL_RESULT_SUCCESS,local=42;LLUUID owner{51};std::string music="https://music.invalid/";std::vector<U8>bitmap=std::vector<U8>(512,0);bool snap=true;
 LLHost getSender()const{return sender;}
 void getS32Fast(const char*,const char* name,S32& out){std::string n=name;out=n=="RequestResult"?result:n=="SequenceID"?sequence:n=="LocalID"?local:n=="SelfCount"?1:n=="OtherCount"?2:n=="PublicCount"?3:107;}
 void getS32(const char* b,const char* n,S32& out){getS32Fast(b,n,out);}void getU32Fast(const char*,const char*,U32& out){out=108;}
 void getU8(const char*,const char*,U8& out){out=2;}void getUUIDFast(const char*,const char*,LLUUID& out){out=owner;}
 void getBOOL(const char*,const char* name,bool& out){out=std::string(name)=="SnapSelection"?snap:true;}void getBOOLFast(const char* b,const char* n,bool& out){getBOOL(b,n,out);}
 void getF32Fast(const char*,const char*,F32& out){out=1.5;}
 void getVector3Fast(const char*,const char* name,LLVector3& out){out=std::string(name)=="AABBMin"?LLVector3(0,0,0):LLVector3(256,256,25);}
 int getNumberOfBlocks(const char*)const{return 1;}int getNumberOfBlocksFast(const char*)const{return 1;}
 int getSizeFast(const char*,const char*)const{return int(bitmap.size());}
 void getBinaryDataFast(const char*,const char*,U8* out,int bytes){std::memcpy(out,bitmap.data(),std::min<std::size_t>(bytes,bitmap.size()));}
};
struct LLParcel{enum EOwnershipStatus{OS_NONE,OS_LEASED,OS_OWNED};int local=-1,environment=7,mLocalID=-1;LLUUID region;std::string music;std::map<std::string,std::string>values;int unpacks=0;
 template<class...A>void record(const char* name,A...args){std::ostringstream s;((s<<args<<";"),...);values[name]=s.str();}
 template<class...A>void init(A...args){record("init",args...);}
 void setRegionID(LLUUID id){region=id;}LLUUID getRegionID()const{return region;}
 void setLocalID(int id){local=mLocalID=id;record("setLocalID",id);}int getLocalID()const{return local;}
 void setParcelEnvironmentVersion(int v){environment=v;record("setParcelEnvironmentVersion",v);}int getParcelEnvironmentVersion()const{return environment;}
 void unpackMessage(LLMessageSystem* msg){++unpacks;music=msg->music;values["unpack"]="actual-shared-parser-entry";}
 std::string getMusicURL()const{return music;}
 SETTERS
};
struct VSRegionContext{LLHost host=1;LLUUID id{10};bool live=true;float width=256,height=256;
 bool current()const{return live;}LLHost getHost()const{return host;}LLUUID getRegionID()const{return id;}float getWidth()const{return width;}float getHeight()const{return height;}U64 getHandle()const{return 77;}
 LLVector3d getPosGlobalFromRegion(LLVector3 v)const{return LLVector3d(v);}LLVector3 getPosRegionFromGlobal(LLVector3d v)const{return {float(v.mdV[0]),float(v.mdV[1]),float(v.mdV[2])};}
 bool operator==(const VSRegionContext& c)const{return host==c.host && id==c.id;}bool operator!=(const VSRegionContext& c)const{return !(*this==c);}};
struct LLViewerRegion:VSRegionContext{};LLViewerRegion legacyRegion;VSRegionContext nativeRegion;
int worldTouches=0;struct LLWorld{static LLWorld* getInstance(){++worldTouches;static LLWorld w;return &w;}static LLWorld& instance(){return *getInstance();}LLViewerRegion* getRegion(LLHost h){return h==1?&legacyRegion:nullptr;}};
std::optional<VSRegionContext> vs_region_context(LLHost h){return h==1?std::optional<VSRegionContext>(nativeRegion):std::nullopt;}
std::optional<VSRegionContext> vs_current_region_context(){return nativeRegion;}
int epoch=1;std::function<bool()>vs_native_im_guard(){int before=epoch;return [before]{return before==epoch;};}
bool gDisconnected=false,gNonInteractive=false;struct LLApp{static bool isExiting(){return false;}};
struct Agent{int changes=0;std::function<void()>onChange;LLViewerRegion* getRegion(){return &legacyRegion;}LLVector3d getPositionGlobal(){return {};}void changeParcels(){++changes;if(onChange)onChange();}}gAgent;
struct Environment{int requests=0;static Environment& instance(){static Environment e;return e;}bool isExtendedEnvironmentEnabled()const{return true;}void requestParcel(int){++requests;}};using LLEnvironment=Environment;
struct Settings{bool getBOOL(const char*)const{return false;}}gSavedSettings;template<class T>struct LLCachedControl{T value;LLCachedControl(Settings&,const char*,T v):value(v){}operator T()const{return value;}};
struct LLViewerParcelAskPlay{static LLViewerParcelAskPlay* getInstance(){static LLViewerParcelAskPlay p;return &p;}void cancelNotification(){}};
struct LLStringUtil{static inline std::string null;static void trim(std::string&){};};
struct Audio{std::string getInternetStreamURL(){return {};}};Audio* gAudiop=nullptr;
struct LLViewerAudio{static LLViewerAudio* getInstance(){static LLViewerAudio a;return &a;}void startInternetStreamWithAutoFade(std::string){}void stopInternetStreamWithAutoFade(){}};
struct LLViewerParcelMedia{int updates=0;static LLViewerParcelMedia* getInstance(){static LLViewerParcelMedia p;return &p;}void update(LLParcel*){++updates;}};
VSResourceBudget budget(1000000);bool refuse=false;struct World{auto reserveSpatialBytes(size_t n){return refuse?std::shared_ptr<const VSResourceBudget::Reservation>{}:budget.reserve(n);}};
struct Context{World worldOwner;World& world(){return worldOwner;}};
struct Window{Context context;Context* nativeContext(){return &context;}}window;Window*gViewerWindow=&window;
struct VSNativeSession{int publications=0;static std::shared_ptr<VSNativeSession>active(){static auto s=std::make_shared<VSNativeSession>();return s;}void publishAgentParcel(const LLParcel& p){assert(p.unpacks>0);++publications;}};
struct Selection{int mSelectedSelfCount=0,mSelectedOtherCount=0,mSelectedPublicCount=0;bool mSelectedMultipleOwners=false,mWholeParcelSelected=false;};
struct LLViewerParcelMgr{
 static LLViewerParcelMgr*selected;static auto&instance(){return *selected;}static auto*getInstance(){return selected;}
 bool mNativeCpu=false;S32 mParcelsPerEdge=64,mNativeParcelRows=64,mAgentParcelSequenceID=-1,mRequestResult=0,mHoverRequestResult=0;VSParcelRequests mNativeRequests;std::optional<VSRegionContext>mNativeSelectionRequestRegion,mNativeHoverRequestRegion,mNativeAgentRegion,mNativeCollisionRegion;
 LLParcel a,b,c,d;LLParcel*mAgentParcel=&a,*mCurrentParcel=&b,*mHoverParcel=&c,*mCollisionParcel=&d;Selection selection;Selection*mCurrentParcelSelection=&selection,*mNativeSelectionRequestHandle=&selection;
 VSParcelBitmap::Snapshot mNativeAgentBitmap,mNativeSelectionBitmap,mNativeCollisionBitmap;bool mTeleportInProgress=false,mTeleportWithinRegion=false,mSelected=false;LLVector3d mTeleportInProgressPosition,mWestSouth,mEastNorth,mHoverWestSouth,mHoverEastNorth;
 std::vector<U8>highlights=std::vector<U8>(65*65),collisions=std::vector<U8>(65*65),collisionBits=std::vector<U8>(512);U8*mHighlightSegments=highlights.data(),*mCollisionSegments=collisions.data(),*mCollisionBitmap=collisionBits.data();int mCollisionBanned=0;U64 mCollisionRegionHandle=0;float mSelectedDwell=0;
 static constexpr int PARCEL_BAN_LINES_ON_COLLISION=0,PARCEL_BAN_LINES_ON_PROXIMITY=1;int notifications=0,access=0,dwell=0,teleports=0,collisionsNotified=0;std::function<void(LLViewerRegion*)>*mCollisionUpdateSignal=nullptr;std::function<void(const VSRegionContext&)>mSpatialCollisionSignal=[this](auto&){++collisionsNotified;};std::function<void(LLVector3d,bool)>mTeleportFinishedSignal=[](auto,bool){};
 bool configureNativeSegments(int columns,int rows,bool collision){auto& v=collision?collisions:highlights;v.resize(size_t(columns+1)*(rows+1));if(collision)mCollisionSegments=v.data();else mHighlightSegments=v.data();return true;}
 void writeAgentParcelFromBitmap(U8*){}void resetSegments(U8* p){std::fill(p,p+(mParcelsPerEdge+1)*(mNativeCpu?mNativeParcelRows+1:mParcelsPerEdge+1),0);}void writeHighlightSegments(float,float,float,float){}void writeSegmentsFromBitmap(const U8*p,U8*s){assert(vs_write_parcel_segments(p,size_t(mParcelsPerEdge)*(mNativeCpu?mNativeParcelRows:mParcelsPerEdge)/8,mParcelsPerEdge,mNativeCpu?mNativeParcelRows:mParcelsPerEdge,s,size_t(mParcelsPerEdge+1)*(mNativeCpu?mNativeParcelRows+1:mParcelsPerEdge+1),1,2));}
 const U8*getCollisionBitmap()const{return mNativeCpu?(mNativeCollisionBitmap?mNativeCollisionBitmap->bytes().data():nullptr):mCollisionBitmap;}size_t getCollisionBitmapSize()const{return mNativeCpu?(mNativeCollisionBitmap?mNativeCollisionBitmap->bytes().size():0):collisionBits.size();}
 void sendParcelAccessListRequest(U32){++access;}void sendParcelDwellRequest(){++dwell;}void notifyObservers(){++notifications;}void resetCollisionTimer(){}void postTeleportFinished(bool){++teleports;}static void optionallyStartMusic(std::string,int,LLUUID,bool){}
 static void processParcelProperties(LLMessageSystem*,void**);static void processParcelPropertiesReference(LLMessageSystem*,void**);
};LLViewerParcelMgr*LLViewerParcelMgr::selected=nullptr;
REFERENCE
PRODUCTION
int main(){
 for(int channel:{0,1,2,3}){
  LLMessageSystem input;input.bitmap[0]=3;int logical=channel==0?1:channel==1?SELECTED_PARCEL_SEQ_ID:channel==2?HOVERED_PARCEL_SEQ_ID:COLLISION_BANNED_PARCEL_SEQ_ID;input.sequence=logical;
  LLViewerParcelMgr reference;LLViewerParcelMgr::selected=&reference;LLViewerParcelMgr::processParcelPropertiesReference(&input,nullptr);
  LLParcel* expected=channel==0?reference.mAgentParcel:channel==1?reference.mCurrentParcel:channel==2?reference.mHoverParcel:reference.mCollisionParcel;
  LLViewerParcelMgr actual;actual.mNativeCpu=true;LLViewerParcelMgr::selected=&actual;
  if(channel==1){input.sequence=*actual.mNativeRequests.issue(VSParcelRequests::Channel::Selection,logical,[]{return true;});actual.mNativeSelectionRequestRegion=nativeRegion;actual.mSelected=true;}
  if(channel==2){input.sequence=*actual.mNativeRequests.issue(VSParcelRequests::Channel::Hover,logical,[]{return true;});actual.mNativeHoverRequestRegion=nativeRegion;}
  worldTouches=0;LLViewerParcelMgr::processParcelProperties(&input,nullptr);assert(worldTouches==0);
  LLParcel* got=channel==0?actual.mAgentParcel:channel==1?actual.mCurrentParcel:channel==2?actual.mHoverParcel:actual.mCollisionParcel;
  assert(got->values==expected->values && got->unpacks==expected->unpacks && got->local==expected->local && got->region==expected->region);assert(got->unpacks==1 && got->values.count("setSimWideMaxPrimCapacity") && got->values.count("setParcelPrimBonus") && got->values.count("setRegionAllowEnvironmentOverride"));
  if(channel==0){assert(actual.mNativeAgentBitmap && actual.mNativeAgentBitmap->cell(1,0)==true);auto old=actual.mNativeAgentBitmap;auto values=got->values;input.sequence++;input.bitmap[0]=4;refuse=true;LLViewerParcelMgr::processParcelProperties(&input,nullptr);refuse=false;assert(actual.mNativeAgentBitmap==old && got->values==values && actual.mAgentParcelSequenceID==1);}
  if(channel==1){assert(actual.mNativeSelectionBitmap && actual.selection.mSelectedSelfCount==1 && actual.selection.mSelectedOtherCount==2 && actual.selection.mSelectedPublicCount==3 && actual.access==1 && actual.dwell==1 && actual.notifications==1);actual.mNativeRequests.retire();auto n=got->unpacks;LLViewerParcelMgr::processParcelProperties(&input,nullptr);assert(got->unpacks==n);}
  if(channel==3)assert(actual.mNativeCollisionBitmap && actual.mNativeCollisionRegion && actual.collisionsNotified==1 && actual.mCollisionBanned==BA_BANNED);
 }
 // Real rectangular region coverage remains independent for every parcel channel.
 nativeRegion.width=512;nativeRegion.height=256;
 for(int channel:{0,1,2,3}){
  LLMessageSystem input;input.bitmap.resize(1024);input.bitmap[0]=1;
  LLViewerParcelMgr actual;actual.mNativeCpu=true;LLViewerParcelMgr::selected=&actual;
  if(channel==1){input.sequence=*actual.mNativeRequests.issue(VSParcelRequests::Channel::Selection,SELECTED_PARCEL_SEQ_ID,[]{return true;});actual.mNativeSelectionRequestRegion=nativeRegion;actual.mSelected=true;}
  if(channel==2){input.sequence=*actual.mNativeRequests.issue(VSParcelRequests::Channel::Hover,HOVERED_PARCEL_SEQ_ID,[]{return true;});actual.mNativeHoverRequestRegion=nativeRegion;}
  if(channel==3)input.sequence=COLLISION_BANNED_PARCEL_SEQ_ID;
  LLViewerParcelMgr::processParcelProperties(&input,nullptr);assert(actual.mParcelsPerEdge==128 && actual.mNativeParcelRows==64);
  if(channel==0)assert(actual.mNativeAgentBitmap && actual.mNativeAgentBitmap->columns()==128 && actual.mNativeAgentBitmap->rows()==64);
  if(channel==1)assert(actual.mNativeSelectionBitmap && actual.highlights.size()==129*65);
  if(channel==3)assert(actual.mNativeCollisionBitmap && actual.getCollisionBitmapSize()==1024 && actual.collisions.size()==129*65);
 }
 nativeRegion.width=nativeRegion.height=256;
 // Observer-triggered retirement must stop remaining teleport/account side effects.
 {LLViewerParcelMgr actual;actual.mNativeCpu=true;actual.mTeleportInProgress=true;LLViewerParcelMgr::selected=&actual;LLMessageSystem input;
  actual.mTeleportFinishedSignal=[](auto,bool){++epoch;};LLViewerParcelMgr::processParcelProperties(&input,nullptr);assert(actual.teleports==0);}
 {LLViewerParcelMgr actual;actual.mNativeCpu=true;LLViewerParcelMgr::selected=&actual;LLMessageSystem input;
  gAgent.onChange=[] {++epoch;};LLViewerParcelMgr::processParcelProperties(&input,nullptr);gAgent.onChange={};assert(actual.mAgentParcel->unpacks==1);}

}
