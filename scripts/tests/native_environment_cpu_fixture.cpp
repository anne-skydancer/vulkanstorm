#include <cassert>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
using F32=float;
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_ENVIRONMENT
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL
struct Window {bool native=true;bool isNativeVulkan()const{return native;}}window;
Window*gViewerWindow=&window;
struct Agent{int agent=1,session=2;int getID()const{return agent;}int getSessionID()const{return session;}}gAgent;
struct LLApp{static inline bool exiting=false;static bool isExiting(){return exiting;}};
struct LLEnvironment {static inline LLEnvironment*live=nullptr;static bool instanceExists(){return live;}static auto&instance(){return *live;}};
int epoch=1,circuitGeneration=1;bool connected=true;
std::function<bool()>vs_native_im_guard(){auto saved=epoch;return [saved]{return saved==epoch;};}
struct Region{int id=1,generation=circuitGeneration;bool current()const{return generation==circuitGeneration;}bool operator==(const Region&other)const{return id==other.id && generation==other.generation;}};
std::optional<Region>region=Region{};
std::optional<Region>vs_current_region_context(){return connected?region:std::nullopt;}
GUARDS
struct Settings{}gSavedSettings;
template<class T>struct LLCachedControl {T value;LLCachedControl(Settings&,const char*,T v):value(v){}T operator()()const{return value;}};
template<class T>T llmax(T a,T b){return std::max(a,b);}
struct LLVector3{F32 value[3];F32 operator[](int n)const{return value[n];}};
int skyBase=0,waterBase=0,skyPublishes=0,waterPublishes=0,poolLookups=0;
struct LLSettingsSky {virtual~LLSettingsSky()=default;virtual void updateSettings(){++skyBase;}};
struct LLSettingsBase {virtual~LLSettingsBase()=default;virtual void updateSettings(){++waterBase;}};
struct SkyOwner{
 void setSunAndMoonDirectionsCFR(LLVector3,LLVector3){++skyPublishes;}
 void setSunTextures(int,int){++skyPublishes;}void setMoonTextures(int,int){++skyPublishes;}
 void setCloudNoiseTextures(int,int){++skyPublishes;}void setBloomTextures(int,int){++skyPublishes;}
 void setSunScale(float){++skyPublishes;}void setMoonScale(float){++skyPublishes;}
}gSky;
struct LLSettingsVOSky:LLSettingsSky {
 float mSceneLightStrength=0;
 LLVector3 getSunDirection(){return {{0,0,.5f}};}LLVector3 getMoonDirection(){return {{0,0,-.5f}};}
 int getSunTextureId(){return 1;}int getNextSunTextureId(){return 2;}int getMoonTextureId(){return 3;}int getNextMoonTextureId(){return 4;}
 int getCloudNoiseTextureId(){return 5;}int getNextCloudNoiseTextureId(){return 6;}int getBloomTextureId(){return 7;}int getNextBloomTextureId(){return 8;}
 float getSunScale(){return 1;}float getMoonScale(){return 1;}
 void updateSettings()override;
};
struct LLDrawPoolWater {void setTransparentTextures(int,int){++waterPublishes;}void setOpaqueTexture(int){++waterPublishes;}void setNormalMaps(int,int){++waterPublishes;}}waterPool;
struct LLDrawPool{enum{POOL_WATER};};
struct Pipeline{LLDrawPoolWater*getPool(int){++poolLookups;return &waterPool;}}gPipeline;
struct LLSettingsVOWater:LLSettingsBase {
 int getTransparentTextureID(){return 1;}int getNextTransparentTextureID(){return 2;}static int GetDefaultOpaqueTextureAssetId(){return 3;}int getNormalMapID(){return 4;}int getNextNormalMapID(){return 5;}
 void updateSettings()override;
};
SKY_UPDATE
WATER_UPDATE
int main(){
 LLEnvironment model,next;LLEnvironment::live=&model;
 auto accountGuard=vs_environment_model_guard(&model),regionGuard=vs_environment_region_guard(&model);
 assert(accountGuard() && regionGuard());++epoch;assert(!accountGuard() && !regionGuard());
 accountGuard=vs_environment_model_guard(&model);regionGuard=vs_environment_region_guard(&model);
 ++gAgent.session;assert(!accountGuard() && !regionGuard());
 accountGuard=vs_environment_model_guard(&model);regionGuard=vs_environment_region_guard(&model);
 LLEnvironment::live=&next;assert(!accountGuard() && !regionGuard());LLEnvironment::live=&model;
 regionGuard=vs_environment_region_guard(&model);++circuitGeneration;assert(!regionGuard());region=Region{};
 regionGuard=vs_environment_region_guard(&model);region->id=2;assert(!regionGuard());
 // Local preset/model requests remain valid during a region crossing, but not account retirement.
 accountGuard=vs_environment_model_guard(&model);region=std::nullopt;assert(accountGuard());++epoch;assert(!accountGuard());
 // Prelogin local presets have an explicit no-account owner; login cannot consume the old callback.
 connected=false;accountGuard=vs_environment_model_guard(&model);assert(accountGuard());++gAgent.session;assert(!accountGuard());connected=true;region=Region{};
 LLSettingsVOSky sky;LLSettingsVOWater water;sky.updateSettings();water.updateSettings();
 assert(skyBase==1 && waterBase==1 && skyPublishes==0 && waterPublishes==0 && poolLookups==0);
 assert(std::abs(sky.mSceneLightStrength-2.5f)<.00001f);
 window.native=false;sky.updateSettings();water.updateSettings();
 assert(skyBase==2 && waterBase==2 && skyPublishes==7 && waterPublishes==3 && poolLookups==1);
 assert(std::abs(sky.mSceneLightStrength-2.5f)<.00001f);
}
