// Exact region-height adapter using controlled transport/model ports.
#include <cassert>
#include <optional>
#include <functional>
using F32=float;constexpr int VX=0,VY=1;
#define VS_NATIVE_VULKAN 1
struct LLVector3d{double mV[3]={};};struct LLVector3{float mV[3]={};};
int regionEpoch=1,fieldEpoch=1;bool hasRegion=true,hasField=true,hasContext=true;
struct Region{int epoch=regionEpoch;bool current()const{return epoch==regionEpoch;}int getHost()const{return 10;}int getRegionID()const{return 20;}LLVector3 getPosRegionFromGlobal(const LLVector3d&p)const{return{{float(p.mV[0]-1000),float(p.mV[1]-2000),float(p.mV[2])}};}};
std::optional<Region>vs_region_context(const LLVector3d&){return hasRegion?std::optional<Region>(Region{}):std::nullopt;}
struct Field{std::function<bool()>current;std::function<std::optional<float>(float,float)>referenceHeight;};
std::function<std::optional<float>(float,float)>sample;
struct NativeWorld{std::optional<Field>terrainField(int host){assert(host==10);if(!hasField)return{};int e=fieldEpoch;return Field{[e]{return e==fieldEpoch;},sample};}};
struct Context{NativeWorld&world(){static NativeWorld w;return w;}}context;
struct Window{bool native=true;bool isNativeVulkan(){return native;}Context*nativeContext(){return hasContext?&context:nullptr;}}window,*gViewerWindow=&window;
struct Land{float resolveHeightGlobal(const LLVector3d&g){assert(g.mV[0]==1004);return 17.5f;}};
struct LegacyRegion{int id=20;int getRegionID(){return id;}Land&getLand(){static Land l;return l;}}legacy;
struct LLWorld{static LLWorld&instance(){static LLWorld w;return w;}LegacyRegion*getRegion(int host){assert(host==10);return &legacy;}};
METHOD
int main(){LLVector3d p{{1004,2008,25}};
 sample=[](float x,float y)->std::optional<float>{assert(x==4&&y==8);return -3.25f;};assert(vs_region_height(p)==-3.25f);
 hasRegion=false;assert(!vs_region_height(p));hasRegion=true;hasField=false;assert(!vs_region_height(p));hasField=true;hasContext=false;assert(!vs_region_height(p));hasContext=true;
 sample=[](float,float)->std::optional<float>{return{};};assert(!vs_region_height(p));
 sample=[](float,float)->std::optional<float>{++fieldEpoch;return 9;};assert(!vs_region_height(p));
 sample=[](float,float)->std::optional<float>{++regionEpoch;return 9;};assert(!vs_region_height(p));
 window.native=false;assert(vs_region_height(p)==17.5f);legacy.id=99;assert(!vs_region_height(p));
}
