#include <cassert>
#include <cmath>
#include <memory>
#include <optional>
#include <functional>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <algorithm>
using F32=float;using U64=unsigned long long;
const F32 MIN_HOVER_Z=-3,MAX_HOVER_Z=3;
float llclamp(float v,float lo,float hi){return std::clamp(v,lo,hi);}
#define LL_WARNS(x) std::cerr
#define LL_ENDL "\n"
struct LLSD{bool b=false;float n=0;std::map<std::string,LLSD>data;LLSD&operator[](const char*k){return data[k];}const LLSD&operator[](const char*k)const{static LLSD missing;auto i=data.find(k);return i==data.end()?missing:i->second;}bool asBoolean()const{return b;}LLSD&operator=(float v){n=v;return *this;}};
struct LLVector3{float z=0;LLVector3()=default;LLVector3(float,float,float v):z(v){}static LLVector3 zero;};LLVector3 LLVector3::zero;
struct Avatar{U64 gen=1;bool server=true;float local=0;Avatar&appearanceOwner(){return *this;}bool isUsingServerBakes()const{return server;}U64 ownerGeneration()const{return gen;}void hoverOffset(LLVector3 v){local=v.z;}}avatar;
Avatar*selfAvatar(){return &avatar;}
struct Settings{float value=1;F32 getF32(const char*){return value;}}gSavedPerAccountSettings;
namespace LLCore{struct HttpRequest{using ptr_t=std::shared_ptr<HttpRequest>;static constexpr int DEFAULT_POLICY_ID=0;};}
int httpCalls=0,httpStatus=200,cancels=0;float sentValue=0;std::function<void()>postHook;
namespace LLCoreHttpUtil{struct HttpCoroutineAdapter{using ptr_t=std::shared_ptr<HttpCoroutineAdapter>;HttpCoroutineAdapter(const char*,int){}void cancelSuspendedOperation(){++cancels;}
 LLSD postAndSuspend(std::shared_ptr<LLCore::HttpRequest>,const std::string&,LLSD body){++httpCalls;sentValue=body["hover_height"].n;if(postHook)postHook();LLSD r;r["http_result"].n=float(httpStatus);return r;}static bool getStatusFromLLSD(const LLSD&r){return r.n==200;}};}
struct VSNativeSession{struct AccountHandle{int id=1;};U64 epoch=1;bool valid=true,ready=true,featuresReceived=true;std::string cap="preferences";LLSD features;
 bool current(AccountHandle)const{return valid;}bool regionReady()const{return ready;}U64 regionEpoch()const{return epoch;}bool simulatorFeaturesReceived()const{return featuresReceived;}const LLSD&simulatorFeatures()const{return features;}std::string capability(const char*)const{return cap;}};
std::vector<std::function<void()>>tasks;struct LLCoros{static LLCoros&instance(){static LLCoros c;return c;}template<class C>void launch(const char*,C cb){tasks.emplace_back(std::move(cb));}};
struct VSNativeAppearance{struct Impl{std::weak_ptr<VSNativeSession>session;VSNativeSession::AccountHandle account;bool valid=true;
 U64 hoverRegion=0,hoverAvatar=0,hoverSerial=0,hoverSendRegion=0,hoverSendAvatar=0;bool hoverFeature=false,hoverServerBakes=false;
 std::optional<F32>hoverSaved,hoverLastSent,hoverPending;std::string hoverCapability;LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t hoverRequest;
 bool current()const{return valid&&!session.expired();}};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();bool hoverEnabled()const;void syncHoverHeight();bool setHoverHeight(F32,bool);};
void run(){auto current=std::move(tasks);tasks.clear();for(auto&task:current)task();}

