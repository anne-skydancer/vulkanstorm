// Execute the real shared RegionInfo controller against the published CPU model.
#include <cassert>
#include <string>
#include <map>
#include <optional>
using U64=unsigned long long;using U32=unsigned;using U8=unsigned char;using S32=int;using F32=float;
constexpr int VX=0,VY=1;
struct LLSD{double number=0;std::string text;LLSD()=default;LLSD(double v):number(v){}LLSD(int v):number(v){}LLSD(bool v):number(v){}LLSD(std::string v):text(v){}LLSD(const char*v):text(v){}};
struct Widget{std::map<std::string,Widget>children;LLSD value;bool enabled=true,defaultText=false;float maximum=0;template<class T>T*getChild(const char*name){return&children[name];}Widget*getChildView(const char*name){return&children[name];}void setValue(LLSD v){value=v;}void setEnabled(bool b){enabled=b;}void setCtrlsEnabled(bool b){enabled=b;}void setAllChildrenEnabled(bool b,bool){enabled=b;}void setMaxValue(float v){maximum=v;}void setDefaultText(){defaultText=true;}void setObjBonusFactor(float v){value=v;}};
using LLUICtrl=Widget;using LLPanel=Widget;using LLSpinCtrl=Widget;using LLLineEditor=Widget;using LLPanelRegionGeneralInfo=Widget;using LLTabContainer=Widget;
constexpr U64 REGION_FLAGS_BLOCK_TERRAFORM=1,REGION_FLAGS_BLOCK_FLY=2,REGION_FLAGS_BLOCK_FLYOVER=4,REGION_FLAGS_ALLOW_DAMAGE=8,REGION_FLAGS_RESTRICT_PUSHOBJECT=16,REGION_FLAGS_BLOCK_LAND_RESELL=32,REGION_FLAGS_ALLOW_PARCEL_CHANGES=64,REGION_FLAGS_BLOCK_PARCEL_SEARCH=128,REGION_FLAGS_SKIP_SCRIPTS=256,REGION_FLAGS_SKIP_COLLISIONS=512,REGION_FLAGS_SKIP_PHYSICS=1024;
bool is_flag_set(U64 v,U64 flag){return(v&flag)!=0;}
struct Agent{bool god=false;bool isGodlike(){return god;}}gAgent;
struct Position{double mdV[3]={512,768,0};double operator[](int i)const{return mdV[i];}};struct Region{bool manager=true;bool canManageEstate()const{return manager;}Position getOriginGlobal()const{return{};}};
std::optional<Region>currentRegion{Region{}};std::optional<Region>vs_current_region_context(){return currentRegion;}
struct LLRegionInfoModel{U8 mSimAccess=21,mAgentLimit=77;S32 mHardAgentLimit=100;U64 mRegionFlags=REGION_FLAGS_ALLOW_DAMAGE|REGION_FLAGS_SKIP_PHYSICS;U32 mEstateID=42,mParentEstateID=0;float mObjectBonusFactor=1.25,mWaterHeight=23,mTerrainRaiseLimit=4,mTerrainLowerLimit=-4;std::string mSimName="actualsim",mSimType="actualproduct";static LLRegionInfoModel&instance(){static LLRegionInfoModel model;return model;}};
struct LLTrans{static std::string getString(const char*s){return s;}static void findString(std::string&to,const std::string&from){to="localized:"+from;}};
struct LLMessageSystem{};std::string gLastVersionChannel="actualversion";
struct LLFloaterRegionInfo:Widget{bool visible=true;int refreshes=0;bool getVisible(){return visible;}void refreshFromRegion(std::optional<Region>){++refreshes;}static void processRegionInfo(LLMessageSystem*);static LLPanelRegionGeneralInfo*getPanelGeneral();};
LLFloaterRegionInfo floater;bool exists=true;
struct LLFloaterReg{template<class T>static T*findTypedInstance(const char*){return exists?&floater:nullptr;}};
LLPanelRegionGeneralInfo*LLFloaterRegionInfo::getPanelGeneral(){return&floater.children["region_panels"].children["General"];}
CONTROLLER
int main(){auto&model=LLRegionInfoModel::instance();
LLFloaterRegionInfo::processRegionInfo(nullptr);auto&general=floater.children["region_panels"].children["General"];
assert(general.children["region_text"].value.text=="actualsim");assert(general.children["region_type"].value.text=="localized:actualproduct");assert(general.children["agent_limit_spin"].value.number==77&&general.children["agent_limit_spin"].maximum==100);
assert(general.children["allow_damage_check"].value.number==1&&general.children["block_fly_check"].value.number==0);assert(general.children["estate_id"].value.number==42);assert(general.children["grid_position_x"].value.number==2&&general.children["grid_position_y"].value.number==3);
assert(floater.children["region_panels"].children["Terrain"].children["water_height_spin"].value.number==23);assert(floater.children["region_panels"].children["Debug"].children["disable_physics_check"].value.number==1);assert(floater.refreshes==1);
currentRegion->manager=false;LLFloaterRegionInfo::processRegionInfo(nullptr);assert(!general.enabled);gAgent.god=true;LLFloaterRegionInfo::processRegionInfo(nullptr);assert(general.enabled);
currentRegion.reset();gAgent.god=false;LLFloaterRegionInfo::processRegionInfo(nullptr);assert(general.children["grid_position_x"].defaultText&&!general.enabled);
int old=floater.refreshes;exists=false;LLFloaterRegionInfo::processRegionInfo(nullptr);assert(floater.refreshes==old);
}
