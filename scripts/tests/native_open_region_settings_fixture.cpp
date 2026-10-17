#include <cassert>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
using F32=float;
struct LLSD {
 std::map<std::string,LLSD> cells;double number=0;std::string text;bool defined=false,map=false;
 LLSD()=default;LLSD(double n):number(n),defined(true){}LLSD(const char*s):text(s),defined(true){}
 static LLSD emptyMap(){LLSD value;value.map=value.defined=true;return value;}
 LLSD&operator[](const std::string&key){map=defined=true;return cells[key];}
 const LLSD&operator[](const std::string&key)const{static LLSD empty;auto found=cells.find(key);return found==cells.end()?empty:found->second;}
 bool has(const char*key)const{return cells.count(key);}bool isMap()const{return map;}explicit operator bool()const{return defined;}
 double asReal()const{return number;}int asInteger()const{return int(number);}std::string asString()const{return text;}
};
struct LLHost{std::string value;LLHost(const std::string&s):value(s){}bool operator!=(const LLHost&b)const{return value!=b.value;}};
bool current=true,available=true;
struct VSRegionContext{bool current()const{return ::current;}LLHost getHost()const{return LLHost("real-sim");}};
std::optional<VSRegionContext>vs_current_region_context(){return available?std::optional<VSRegionContext>(VSRegionContext{}):std::nullopt;}
struct Window{bool native=true;bool isNativeVulkan()const{return native;}}window,*gViewerWindow=&window;
struct Model{
 std::map<std::string,double>values;int refresh=0,effects=0,landFarClip=0;
 void refreshLimits(){values.clear();++refresh;}void updateLimits(){++effects;}void setLandFarClip(float){++landFarClip;}
 SETTERS
 static Model*getInstance();
}model;
Model*Model::getInstance(){return &model;}using LLWorld=Model;using VSRegionLimits=Model;
VSRegionLimits&vs_region_limits(){return model;}void vs_apply_region_limits(){++model.effects;}
struct Settings{std::map<std::string,float>values;bool getBOOL(const char*){return true;}void setF32(const char*key,float value){values[key]=value;}}gSavedSettings;
struct Camera{float mDrawDistance=0;}gAgentCamera;
int gUTCOffset=0,gMaxAgentGroups=0;
struct LLDrawPoolTerrain{static inline float sDetailScale=0;};
struct LLHTTPNode{using ResponsePtr=int;};std::ostringstream logs;
#define LL_INFOS(...) logs
#define LL_WARNS(...) logs
#define LL_ENDL ""
ORIGINAL
PROPOSED
int main(){
 LLSD input=LLSD::emptyMap(),context=LLSD::emptyMap();input["sender"]="real-sim";input["body"]=LLSD::emptyMap();auto&body=input["body"];
 for(const char*name:{"AllowMinimap","AllowPhysicalPrims","DrawDistance","ForceDrawDistance","TerrainDetailScale","MaxDragDistance","MinHoleSize","MaxHollowSize","MaxInventoryItemsTransfer","MaxLinkCount","MaxLinkCountPhys","MaxPos","MaxPosX","MaxPosY","MaxPosZ","MinPos","MinPosX","MinPosY","MinPosZ","MaxPrimScale","MaxPhysPrimScale","MinPrimScale","OffsetOfUTCDST","OffsetOfUTC","RenderWater","ToggleTeenMode","ShowTags","EnforceMaxBuild","MaxGroups","AllowParcelWindLight"})body[name]=1.;
 body["DrawDistance"]=512.;body["TerrainDetailScale"]=4.;body["MaxGroups"]=60.;body["OffsetOfUTC"]=-8.;
 window.native=false;original(0,context,input);const auto values=model.values;const auto settings=gSavedSettings.values;const auto detail=LLDrawPoolTerrain::sDetailScale;
 assert(model.refresh==1&&model.effects==1&&model.landFarClip==1&&gAgentCamera.mDrawDistance==512&&gMaxAgentGroups==60&&gUTCOffset==-7);
 model={};gSavedSettings.values.clear();gAgentCamera.mDrawDistance=0;gMaxAgentGroups=gUTCOffset=0;
 window.native=true;proposed(0,context,input);assert(model.values==values&&model.refresh==1&&model.effects==1&&model.landFarClip==0);
 assert(gSavedSettings.values==settings&&LLDrawPoolTerrain::sDetailScale==detail&&gAgentCamera.mDrawDistance==512&&gMaxAgentGroups==60&&gUTCOffset==-7);
 auto unchanged=[&]{assert(model.refresh==1&&model.effects==1&&model.values==values&&gAgentCamera.mDrawDistance==512&&gMaxAgentGroups==60);};
 input["sender"]="forged";body["DrawDistance"]=128.;proposed(0,context,input);unchanged();
 input["sender"]="real-sim";current=false;proposed(0,context,input);unchanged();current=true;available=false;proposed(0,context,input);unchanged();available=true;
 body["DrawDistance"]=std::numeric_limits<double>::quiet_NaN();proposed(0,context,input);unchanged();
 body["DrawDistance"]=512.;body["MaxPrimScale"]=std::numeric_limits<double>::infinity();proposed(0,context,input);unchanged();
 input["body"]=LLSD(1.);proposed(0,context,input);unchanged();
}
