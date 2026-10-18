// Actual avatar-context and coarse lookup methods against controlled peer ports.
#include <array>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
using U64=unsigned long long;using U8=unsigned char;constexpr U8 PARCEL_COLOR_MASK=7,PARCEL_SELF=3,PARCEL_GROUP=2;
struct LLUUID{int value=0;bool isNull()const{return !value;}bool operator==(const LLUUID&o)const{return value==o.value;}bool operator!=(const LLUUID&o)const{return value!=o.value;}bool operator<(const LLUUID&o)const{return value<o.value;}static const LLUUID null;};const LLUUID LLUUID::null{};
struct LLHost{int value=0;bool operator==(const LLHost&o)const{return value==o.value;}bool operator<(const LLHost&o)const{return value<o.value;}};
struct LLVector3d{double mV[3]={};LLVector3d()=default;LLVector3d(double x,double y,double z):mV{x,y,z}{}explicit LLVector3d(const double*p):mV{p[0],p[1],p[2]}{}};struct LLVector3{};
int regionEpoch=1;LLUUID regionOwner{7};bool manager=true;std::optional<U8>parcelFlags=PARCEL_SELF;
struct VSRegionContext{int epoch=regionEpoch;LLUUID owner=regionOwner;bool estate=manager;bool current()const{return epoch==regionEpoch;}LLUUID getOwner()const{return owner;}bool isEstateManager()const{return estate;}std::optional<U8>parcelFlagsAt(const LLVector3d&)const{return parcelFlags;}};
std::optional<VSRegionContext>vs_region_context(LLHost host){return host.value==10?std::optional<VSRegionContext>(VSRegionContext{}):std::nullopt;}
struct LLViewerRegion{LLHost getHost()const{return{10};}LLUUID getRegionID()const{return{30};}LLUUID getOwner()const{return regionOwner;}bool isEstateManager()const{return manager;}LLVector3 getPosRegionFromGlobal(const LLVector3d&)const{return{};}bool isOwnedSelf(LLVector3)const{return parcelFlags&&(*parcelFlags&7)==PARCEL_SELF;}bool isOwnedGroup(LLVector3)const{return parcelFlags&&(*parcelFlags&7)==PARCEL_GROUP;}}legacy;
int legacyLookups=0,worldLookups=0;bool legacyFound=true,worldExists=true,worldSame=true;
bool getRegionAndPosGlobalFromAgentID(const LLUUID&,const LLViewerRegion**r,LLVector3d*p){++legacyLookups;if(!legacyFound)return false;*r=&legacy;*p={11,12,13};return true;}
struct LLWorld{static bool instanceExists(){return worldExists;}static LLWorld&instance(){++worldLookups;static LLWorld w;return w;}LLViewerRegion*getRegion(LLHost){return worldSame?&legacy:nullptr;}};
namespace VSWorldContracts{enum class OwnerKind{Object,Avatar};}
int objectEpoch=1;bool objectExists=true,poseExists=true;VSWorldContracts::OwnerKind objectKind=VSWorldContracts::OwnerKind::Avatar;
struct VSWorldScene{struct ObjectHandle{LLHost host{10};VSWorldContracts::OwnerKind kind=objectKind;};struct Pose{LLVector3d position{25,26,27};};std::optional<ObjectHandle>object(const LLUUID&){return objectExists?std::optional<ObjectHandle>(ObjectHandle{}):std::nullopt;}std::optional<Pose>objectTransform(const ObjectHandle&){return poseExists?std::optional<Pose>(Pose{}):std::nullopt;}std::function<bool()>guard(const ObjectHandle&){int e=objectEpoch;return[e]{return objectExists&&e==objectEpoch;};}}world;
template<class T>struct VSCoarseSample{T id;std::array<double,3>position{};bool altitudeKnown=false;};
struct VSNativeSession:std::enable_shared_from_this<VSNativeSession>{enum class Phase{Connecting,Connected,Disconnected};Phase mPhase=Phase::Connected;bool mShutdown=false,ready=true;U64 generation=1;struct AccountHandle{U64 generation;};struct Peer{bool acknowledged=false,handshake=false;struct{std::vector<VSCoarseSample<LLUUID>>avatars;}coarse;};std::map<LLHost,Peer>mCircuits;
static std::shared_ptr<VSNativeSession>active();bool regionReady(){return ready;}AccountHandle account(){return{generation};}bool current(AccountHandle a){return !mShutdown&&generation==a.generation&&mPhase==Phase::Connected;}std::function<bool()>accountGuard(){auto weak=weak_from_this();auto a=account();return[weak,a]{auto s=weak.lock();return s&&s->current(a);};}std::optional<std::pair<LLHost,VSCoarseSample<LLUUID>>>coarseAvatarLocation(const LLUUID&)const;};
std::shared_ptr<VSNativeSession>session=std::make_shared<VSNativeSession>();std::shared_ptr<VSNativeSession>VSNativeSession::active(){return session;}
struct NativeContext{VSWorldScene&world(){return ::world;}}context;bool contextPresent=true;struct Window{bool native=true;bool isNativeVulkan(){return native;}NativeContext*nativeContext(){return contextPresent?&context:nullptr;}}window,*gViewerWindow=&window;
HEADER
METHODS
int main(){
auto precise=VSAvatarContext::resolve({42});assert(precise&&precise->current()&&precise->position().mV[2]==27&&precise->altitudeKnown()&&precise->getOwner().value==7&&precise->isEstateManager()&&precise->isOwnedSelf()&&!precise->isOwnedGroup());assert(legacyLookups==0&&worldLookups==0);
parcelFlags=U8(PARCEL_GROUP|128);assert(precise->isOwnedGroup()&&!precise->isOwnedSelf());parcelFlags.reset();assert(!precise->isOwnedGroup()&&!precise->isOwnedSelf());regionOwner={9};manager=false;assert(precise->getOwner().value==9&&!precise->isEstateManager());
++session->generation;assert(!precise->current()&&precise->getOwner().isNull());precise=VSAvatarContext::resolve({42});++objectEpoch;assert(!precise->current());
session->mCircuits[{10}]={true,true,{{{{42},{100,200,-1},false}}}};objectKind=VSWorldContracts::OwnerKind::Object;assert(!VSAvatarContext::resolve({42}));objectKind=VSWorldContracts::OwnerKind::Avatar;poseExists=false;assert(!VSAvatarContext::resolve({42}));poseExists=true;objectExists=false;
auto coarse=VSAvatarContext::resolve({42});assert(coarse&&coarse->current()&&!coarse->altitudeKnown()&&coarse->position().mV[2]==-1);assert(!session->coarseAvatarLocation({}));session->mCircuits[{10}].coarse.avatars.clear();assert(!coarse->current()&&!VSAvatarContext::resolve({42}));session->mCircuits[{10}]={true,false,{{{{42},{100,200,300},true}}}};assert(!VSAvatarContext::resolve({42}));session->mCircuits[{10}].handshake=true;coarse=VSAvatarContext::resolve({42});assert(coarse&&coarse->altitudeKnown());++regionEpoch;assert(!coarse->current());
contextPresent=false;assert(!VSAvatarContext::resolve({42}));contextPresent=true;session->ready=false;assert(!VSAvatarContext::resolve({42}));session->ready=true;
window.native=false;int before=legacyLookups;auto gl=VSAvatarContext::resolve({42});assert(gl&&legacyLookups==before+1&&gl->position().mV[2]==13&&gl->current());worldSame=false;assert(!gl->current());worldSame=true;worldExists=false;assert(!gl->current());worldExists=true;legacyFound=false;before=worldLookups;assert(!VSAvatarContext::resolve({42})&&worldLookups==before);
}
