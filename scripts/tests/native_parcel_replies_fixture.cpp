#include <cassert>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
using S32=int;using U32=unsigned;using F32=float;using LLHost=int;
struct LLUUID {int id=0;bool operator==(LLUUID other)const{return id==other.id;}bool operator!=(LLUUID other)const{return !(*this==other);}};
#include "vsparcelrequests.h"
int epoch=1;bool circuit=true;
std::function<bool()>vs_native_im_guard(){int saved=epoch;return [saved]{return saved==epoch;};}
struct VSRegionContext {LLHost host=1;LLUUID region{10};bool current()const{return circuit;}LLHost getHost()const{return host;}LLUUID getRegionID()const{return region;}};
struct Agent{LLUUID getID()const{return {1};}}gAgent;
struct LLMessageSystem {LLUUID agent{1},parcel{20};LLHost sender=1;int local=5,sequence=0;U32 flags=1;float dwell=123;
 LLHost getSender()const{return sender;}
 void getUUIDFast(const char*,const char*name,LLUUID& out){out=std::string(name)=="AgentID"?agent:parcel;}
 void getS32Fast(const char*,const char*name,int& out){out=std::string(name)=="SequenceID"?sequence:local;}
 void getU32Fast(const char*,const char*,U32&out){out=flags;}
 void getUUID(const char*a,const char*b,LLUUID&out){getUUIDFast(a,b,out);}void getS32(const char*a,const char*b,int&out){getS32Fast(a,b,out);}void getF32(const char*,const char*,F32&out){out=dwell;}
};
constexpr int AL_ACCESS=1,AL_BAN=2,AL_ALLOW_EXPERIENCE=4,AL_BLOCK_EXPERIENCE=8,EXPERIENCE_KEY_TYPE_ALLOWED=1,EXPERIENCE_KEY_TYPE_BLOCKED=2,PARCEL_RESULT_NO_DATA=-1;
struct LLParcel {int local=5;LLUUID region{10};std::vector<int>mAccessList,mBanList;int allowed=0,blocked=0;
 int getLocalID()const{return local;}LLUUID getRegionID()const{return region;}
 void unpackAccessEntries(LLMessageSystem*,std::vector<int>* list){list->push_back(1);}void unpackExperienceEntries(LLMessageSystem*,int type){if(type==1)++allowed;else ++blocked;}
};
PREHASH
#define LL_WARNS_ONCE(...) std::ostringstream()
#define LL_ENDL std::endl
struct LLViewerParcelMgr {
 inline static LLViewerParcelMgr*owner=nullptr;static auto&instance(){return *owner;}static auto*getInstance(){return owner;}static bool instanceExists(){return owner;}
 bool mNativeCpu=true,mSelected=true;int mRequestResult=0,notifications=0;float mSelectedDwell=0;
 LLParcel parcel;LLParcel*mCurrentParcel=&parcel;using Handle=std::shared_ptr<int>;
 Handle mCurrentParcelSelection=std::make_shared<int>(0),mNativeAccessRequestHandle=mCurrentParcelSelection,mNativeDwellRequestHandle=mCurrentParcelSelection;
 std::optional<VSRegionContext>mNativeAccessRequestRegion=VSRegionContext{},mNativeDwellRequestRegion=VSRegionContext{};
 S32 mNativeDwellLocal=5;VSParcelRequests mNativeRequests;
 std::optional<VSRegionContext>getSelectionRegionContext()const{return mNativeAccessRequestRegion;}
 void notifyObservers(){++notifications;}
 static void processParcelAccessListReply(LLMessageSystem*,void**);static void processParcelDwellReply(LLMessageSystem*,void**);
 std::function<bool()>selectionGuard()const;
};
PRODUCTION
int main(){
 LLViewerParcelMgr owner;LLViewerParcelMgr::owner=&owner;LLMessageSystem message;
 auto issue=[&]{auto tag=owner.mNativeRequests.issue(VSParcelRequests::Channel::Access,0,[]{return circuit;},5);assert(tag);message.sequence=*tag;owner.mNativeAccessRequestHandle=owner.mCurrentParcelSelection;};
 auto access=[&]{LLViewerParcelMgr::processParcelAccessListReply(&message,nullptr);};
 issue();access();access();assert(owner.parcel.mAccessList.size()==2 && owner.notifications==2);
 message.flags=AL_BAN;access();assert(owner.parcel.mBanList.size()==1);
 message.flags=AL_ALLOW_EXPERIENCE;access();assert(owner.parcel.allowed==1);
 message.flags=AL_BLOCK_EXPERIENCE;access();assert(owner.parcel.blocked==1);
 // Independent list refreshes share the selection, not a single replacement slot.
 auto accessTag=owner.mNativeRequests.issue(VSParcelRequests::Channel::Access,0,[]{return circuit;},5,AL_ACCESS);
 auto banTag=owner.mNativeRequests.issue(VSParcelRequests::Channel::Access,0,[]{return circuit;},5,AL_BAN);
 assert(accessTag && banTag);message.sequence=*accessTag;message.flags=AL_ACCESS;access();assert(owner.parcel.mAccessList.size()==3);
 message.sequence=*banTag;message.flags=AL_BAN;access();assert(owner.parcel.mBanList.size()==2);
 // The real handler processes the first recognized flag only; a still-current
 // ban chunk cannot authorize an obsolete mixed access/ban reply.
 message.flags=AL_ACCESS|AL_BAN;access();assert(owner.parcel.mAccessList.size()==3);
 issue();message.flags=AL_ACCESS;
 auto before=owner.notifications;message.agent={99};access();message.agent={1};assert(owner.notifications==before);
 message.sender=2;access();message.sender=1;assert(owner.notifications==before);
 circuit=false;access();circuit=true;assert(owner.notifications==before);
 message.local=6;access();message.local=5;assert(owner.notifications==before);
 auto old=message.sequence;issue();auto current=message.sequence;message.sequence=old;access();message.sequence=current;assert(owner.notifications==before);
 owner.mCurrentParcelSelection=std::make_shared<int>(1);access();assert(owner.notifications==before);issue();
 owner.mSelected=false;access();owner.mSelected=true;assert(owner.notifications==before);
 auto guard=owner.selectionGuard();assert(guard());++epoch;assert(!guard());
 guard=owner.selectionGuard();owner.mCurrentParcelSelection=std::make_shared<int>(2);assert(!guard());
 guard=owner.selectionGuard();circuit=false;assert(!guard());circuit=true;
 auto dwell=[&]{LLViewerParcelMgr::processParcelDwellReply(&message,nullptr);};
 owner.mNativeDwellRequestHandle=owner.mCurrentParcelSelection;dwell();assert(owner.mSelectedDwell==123);
 before=owner.notifications;message.dwell=456;message.agent={8};dwell();message.agent={1};assert(owner.notifications==before);
 message.sender=2;dwell();message.sender=1;assert(owner.notifications==before);
 circuit=false;dwell();circuit=true;assert(owner.notifications==before);
 owner.mCurrentParcelSelection=std::make_shared<int>(3);dwell();assert(owner.notifications==before);owner.mNativeDwellRequestHandle=owner.mCurrentParcelSelection;
 message.dwell=std::numeric_limits<float>::quiet_NaN();dwell();assert(owner.notifications==before);
 message.dwell=456;owner.parcel.region={99};dwell();owner.parcel.region={10};assert(owner.notifications==before);
 dwell();assert(owner.mSelectedDwell==456 && owner.notifications==before+1);
 owner.mNativeRequests.retire();access();assert(owner.notifications==before+1);
}
