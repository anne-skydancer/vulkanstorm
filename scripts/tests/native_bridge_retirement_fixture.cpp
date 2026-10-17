#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#define VS_NATIVE_VULKAN 1
using U64=unsigned long long;
struct UUID {
 int id=1;bool null=false;
 void setNull(){null=true;id=0;}
 bool operator==(const UUID& rhs)const{return id==rhs.id;}
 bool operator!=(const UUID& rhs)const{return !(*this==rhs);}
};
using LLUUID=UUID;LLUUID gAgentID,gAgentSessionID;
std::function<bool()> vs_native_im_guard(){return []{return true;};}
struct Settings{bool enable=true;}gSavedSettings;
template<class T>struct LLCachedControl{Settings& settings;LLCachedControl(Settings& s,const char*):settings(s){}operator bool()const{return settings.enable;}};
struct VSNativeSession{
 static bool native;U64 region=1;bool ready=true;
 U64 regionEpoch()const{return region;}
 static VSNativeSession* active(){static VSNativeSession s;return native?&s:nullptr;}
};bool VSNativeSession::native=false;
bool fs_bridge_avatar_ready(){auto owner=VSNativeSession::active();return owner&&owner->ready;}
struct ViewerWindow{bool native=false;bool isNativeVulkan(){return native;}} window,*gViewerWindow=&window;
struct Item{UUID id;const UUID& getUUID()const{return id;}} item;
struct Adapter{
 struct Attachment{struct Object{UUID id;}object;};
 bool valid=true,present=true;UUID object;
 auto guard(const UUID&)const{return [this]{return valid;};}
 std::optional<Attachment> attachment(const UUID&)const{return present?std::optional<Attachment>(Attachment{{object}}):std::nullopt;}
};
struct FSLSLBridge{
 inline static FSLSLBridge* value=nullptr;
 static bool instanceExists(){return value;}static auto& instance(){return *value;}
 U64 mLifecycle=1;U64 lifecycle()const{return mLifecycle;}
 enum TimerResult{NO_TIMER,START_CREATION_FINISHED};
 std::string mCurrentURL="https://old-region.invalid/bridge";Item* mpBridge=&item;
 UUID mScriptItemID,mBridgeFolderID,mBridgeContainerFolderID,mBridgeUUID,mReattachBridgeUUID,mNativeConfigureItem;
 std::unique_ptr<Adapter> mNativeAdapter;
 std::vector<int> mAllowedDetachables{1,2};bool mBridgeCreating=true,mAllowDetach=true,mFinishCreation=true,mIsFirstCallDone=true;
 TimerResult mTimerResult=START_CREATION_FINISHED;
 bool isBridgeValid(){return mpBridge!=nullptr;}Item* getBridge(){return mpBridge;}
 bool canUseBridge();void resetNativeAccount();
};
DEFINITIONS
int main(){
 FSLSLBridge bridge;FSLSLBridge::value=&bridge;assert(bridge.canUseBridge());
 auto current=fs_bridge_account_guard();assert(current());++gAgentSessionID.id;assert(!current());--gAgentSessionID.id;
 VSNativeSession::native=true;assert(!bridge.canUseBridge());
 bridge.mNativeAdapter=std::make_unique<Adapter>();assert(bridge.canUseBridge());
 bridge.mNativeAdapter->valid=false;assert(!bridge.canUseBridge());bridge.mNativeAdapter->valid=true;
 bridge.mNativeAdapter->object.id=2;assert(!bridge.canUseBridge());bridge.mNativeAdapter->object.id=1;
 bridge.mNativeAdapter->present=false;assert(!bridge.canUseBridge());bridge.mNativeAdapter->present=true;
 bridge.resetNativeAccount();assert(bridge.mCurrentURL.empty()&&!bridge.mpBridge&&!bridge.mNativeAdapter);
 assert(bridge.mLifecycle==2&&!current()&&bridge.mNativeConfigureItem.null);
 assert(bridge.mScriptItemID.null&&bridge.mBridgeFolderID.null&&bridge.mBridgeContainerFolderID.null&&bridge.mBridgeUUID.null&&bridge.mReattachBridgeUUID.null);
 assert(bridge.mAllowedDetachables.empty()&&!bridge.mBridgeCreating&&!bridge.mAllowDetach&&!bridge.mFinishCreation&&!bridge.mIsFirstCallDone&&bridge.mTimerResult==FSLSLBridge::NO_TIMER);
 VSNativeSession::native=false;assert(!bridge.canUseBridge());bridge.mpBridge=&item;bridge.mCurrentURL="https://new-region.invalid/bridge";
 current=fs_bridge_account_guard();assert(bridge.canUseBridge()&&current());
 window.native=true;assert(!bridge.canUseBridge()&&!current());window.native=false;
 gViewerWindow=nullptr;assert(!bridge.canUseBridge()&&!current());gViewerWindow=&window;
 gSavedSettings.enable=false;assert(!bridge.canUseBridge());FSLSLBridge::value=nullptr;
}
