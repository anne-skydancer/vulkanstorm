#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>
using U64=unsigned long long;using U32=unsigned;using S32=int;
struct LLUUID{std::string value;void generate(const std::string& s){value=s;}bool operator<(const LLUUID& b)const{return value<b.value;}};
struct LLUIImage{int marker=1;};template<class T>using LLPointer=std::shared_ptr<T>;
struct VSMapImage{LLPointer<LLUIImage> image;bool missing=false;};
struct LLURI{std::string value;explicit LLURI(std::string s):value(s){}std::string hostName()const{auto i=value.find("://");if(i==std::string::npos||i+3==value.size())return {};return value.substr(i+3);}};
struct VSNativeSession{enum class Phase{Login,Connected,Logout};U64 epoch=1;Phase state=Phase::Connected;inline static std::shared_ptr<VSNativeSession> owner;static auto active(){return owner;}Phase phase()const{return state;}U64 generation()const{return epoch;}};
struct PriorityFetch{bool updateRequestPriority(const LLUUID&,float){return true;}} priorityFetch;
struct LLAppViewer{static auto* getTextureFetch(){return &priorityFetch;}};
struct VSUIImageProvider {
    struct Impl{struct Remote{std::string sourceURL;unsigned target=0;bool exactWorld=false,missingMapTile=false,mapVisible=false;S32 fullWidth=0,fullHeight=0,components=4;U64 generation=0,readyGeneration=0,failedGeneration=0;unsigned attempts=0;double retryAt=0;bool uiDemand=false;LLPointer<LLUIImage> facade;};std::map<LLUUID,Remote> remote;bool evict(const LLUUID&,bool){return false;}};
    std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();int requests=0;
    void getUIImageByID(const LLUUID& id,int){++requests;auto& e=mImpl->remote[id];e.generation=VSNativeSession::owner->generation();if(!e.facade)e.facade=std::make_shared<LLUIImage>();}
    VSMapImage requestMapImage(const std::string&,bool);void retryMapImage(const std::string&);
} provider;
PROVIDER_METHODS
VSMapImage vs_request_map_image(const std::string& url,bool load){return provider.requestMapImage(url,load);}
void vs_retry_map_image(const std::string& url){provider.retryMapImage(url);}
struct LFSimFeatureHandler{std::string base="https://map.example/";static LFSimFeatureHandler& instance(){static LFSimFeatureHandler v;return v;}std::string mapServerURL()const{return base;}};
std::string llformat(const char* f,int level,U32 x,U32 y){char text[100];std::snprintf(text,sizeof(text),f,level,x,y);return text;}
struct LLWorldMipmap{static constexpr int MAP_LEVELS=8;int mCurrentLevel=0;std::map<U64,std::string> mNativeTiles[8];U64 convertGridToHandle(U32 x,U32 y){return (U64(x)<<32)|y;}static std::string objectsTileURL(U32,U32,S32);VSMapImage getNativeObjectsTile(U32,U32,S32,bool=true);};
MIPMAP_METHODS
using LLHost=int;enum{FTT_DEFAULT=0,FTT_MAP_TILE=1};
struct Fetch {int type=-1;std::string source;LLUUID identity;int createRequest(int t,const std::string& u,const LLUUID& id,LLHost,float,int,int,int,int,bool,bool){type=t;source=u;identity=id;return 1;}} worker;
int queue_actual_request(VSUIImageProvider::Impl::Remote& entry,const std::string& url,LLUUID id){auto* fetch=&worker;unsigned target=256;int desired=0;QUEUE_EXPRESSION return queued;}
int main(){
    VSNativeSession::owner=std::make_shared<VSNativeSession>();const std::string url="https://map.example/map-1-1000-1000-objects.jpg";LLUUID id;id.generate(url);
    assert(!provider.requestMapImage(url,false).image&&provider.requests==0);
    assert(!provider.requestMapImage("file:///tile.jpg",true).image&&provider.requests==0);
    assert(!provider.requestMapImage("https://",true).image&&provider.requests==0);
    assert(!provider.requestMapImage(url,true).image&&provider.requests==1);
    auto& entry=provider.mImpl->remote[id];assert(entry.sourceURL==url&&entry.exactWorld&&entry.target==256);
    assert(queue_actual_request(entry,"https://asset.example/texture?id=asset",id)==1&&worker.type==FTT_MAP_TILE&&worker.source==url);
    auto asset=entry;asset.sourceURL.clear();assert(queue_actual_request(asset,"https://asset.example/texture?id=asset",id)==1&&worker.type==FTT_DEFAULT&&worker.source=="https://asset.example/texture?id=asset");
    entry.readyGeneration=1;assert(provider.requestMapImage(url,false).image&&provider.requests==1);
    entry.readyGeneration=0;entry.missingMapTile=true;entry.failedGeneration=1;entry.attempts=3;assert(provider.requestMapImage(url,true).missing&&provider.requests==1);
    provider.retryMapImage(url);assert(!entry.missingMapTile&&entry.attempts==0);
    entry.generation=1;entry.missingMapTile=true;entry.failedGeneration=1;VSNativeSession::owner->epoch=2;assert(!provider.requestMapImage(url,false).missing);provider.requestMapImage(url,true);assert(!entry.missingMapTile&&provider.requests==2);
    VSNativeSession::owner->state=VSNativeSession::Phase::Logout;assert(!provider.requestMapImage(url,true).image&&provider.requests==2);VSNativeSession::owner->state=VSNativeSession::Phase::Connected;
    provider.mImpl->remote.clear();provider.requests=0;LLWorldMipmap mip;
    assert(!mip.getNativeObjectsTile(1000,1000,1,false).image&&provider.requests==0);
    assert(!mip.getNativeObjectsTile(1000,1000,1,true).image&&provider.requests==1);
    auto& tile=provider.mImpl->remote[id];tile.missingMapTile=true;tile.failedGeneration=2;tile.attempts=3;
    assert(mip.getNativeObjectsTile(1000,1000,1,true).missing&&provider.requests==1);
    mip.getNativeObjectsTile(1000,1000,2,true);mip.getNativeObjectsTile(1000,1000,1,true);assert(!tile.missingMapTile&&provider.requests==3);
    LFSimFeatureHandler::instance().base="https://other-grid.example/";assert(!mip.getNativeObjectsTile(1000,1000,1,false).image);assert(!mip.getNativeObjectsTile(1000,1000,1,true).image&&provider.requests==4);
}
