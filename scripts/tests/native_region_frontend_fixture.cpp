#include <cassert>
#include <map>
#include <string>
#include <typeinfo>
#include <functional>
#define llassert(x) assert(x)
bool native=true;
struct Window{bool isNativeVulkan()const{return native;}}window;Window*gViewerWindow=&window;
struct LLPanel{virtual~LLPanel()=default;};
struct LLPanelEstateInfo:LLPanel{};struct LLPanelEstateAccess:LLPanel{};struct LLPanelEstateCovenant:LLPanel{};
struct LLPanelRegionGeneralInfo:LLPanel{};struct LLPanelRegionEnvironment:LLPanel{};struct LLPanelRegionTerrainInfo:LLPanel{};
struct LLPanelRegionExperiences:LLPanel{};struct LLPanelRegionOpenSettingsInfo:LLPanel{};
struct LLPanelRegionInfo:LLPanel{};struct LLPanelRegionDebugInfo:LLPanel{};
struct LLTabContainer{std::map<std::string,LLPanel*>panels;int manufactured=0;
 template<class T>T*getChild(const std::string&key){auto*result=panels[key];if(!result)++manufactured;return dynamic_cast<T*>(result);}
 template<class T>T*findChild(const std::string&key,bool){auto found=panels.find(key);return found==panels.end()?nullptr:dynamic_cast<T*>(found->second);}
};
struct LLFloaterRegionInfo{LLTabContainer tab;
 template<class T>T*getChild(const char*){return &tab;}
 static bool isNativeInternalType(const std::type_info&);
 static LLPanelEstateInfo*getPanelEstate();static LLPanelEstateAccess*getPanelAccess();static LLPanelEstateCovenant*getPanelCovenant();
 static LLPanelRegionGeneralInfo*getPanelGeneral();static LLPanelRegionEnvironment*getPanelEnvironment();static LLPanelRegionTerrainInfo*getPanelRegionTerrain();
 static LLPanelRegionExperiences*getPanelExperiences();static LLPanelRegionOpenSettingsInfo*getPanelOpenSettings();
};
LLFloaterRegionInfo*live=nullptr;int creations=0;
struct LLFloaterReg{template<class T>static T*getTypedInstance(const char*){++creations;return live;}template<class T>static T*findTypedInstance(const char*){return live;}};
METHODS
int main(){
 assert(LLFloaterRegionInfo::isNativeInternalType(typeid(LLPanelRegionEnvironment)));assert(LLFloaterRegionInfo::isNativeInternalType(typeid(LLPanelRegionOpenSettingsInfo)));assert(!LLFloaterRegionInfo::isNativeInternalType(typeid(Window)));
 auto check=[](){assert(!LLFloaterRegionInfo::getPanelEstate());assert(!LLFloaterRegionInfo::getPanelAccess());assert(!LLFloaterRegionInfo::getPanelCovenant());assert(!LLFloaterRegionInfo::getPanelGeneral());assert(!LLFloaterRegionInfo::getPanelEnvironment());assert(!LLFloaterRegionInfo::getPanelRegionTerrain());assert(!LLFloaterRegionInfo::getPanelExperiences());assert(!LLFloaterRegionInfo::getPanelOpenSettings());};
 check();assert(creations==0);
 LLFloaterRegionInfo owner;live=&owner;LLPanelEstateInfo estate;LLPanelEstateAccess access;LLPanelEstateCovenant covenant;LLPanelRegionGeneralInfo general;LLPanelRegionEnvironment env;LLPanelRegionTerrainInfo terrain;
 owner.tab.panels={{"Estate",&estate},{"Access",&access},{"Covenant",&covenant},{"General",&general},{"panel_env_info",&env},{"Terrain",&terrain}};
 assert(LLFloaterRegionInfo::getPanelEstate()==&estate);assert(LLFloaterRegionInfo::getPanelAccess()==&access);assert(LLFloaterRegionInfo::getPanelCovenant()==&covenant);assert(LLFloaterRegionInfo::getPanelGeneral()==&general);assert(LLFloaterRegionInfo::getPanelEnvironment()==&env);assert(LLFloaterRegionInfo::getPanelRegionTerrain()==&terrain);assert(!LLFloaterRegionInfo::getPanelExperiences());assert(!LLFloaterRegionInfo::getPanelOpenSettings());assert(owner.tab.manufactured==0&&creations==0);
 LLPanelRegionExperiences experience;LLPanelRegionOpenSettingsInfo aurora;owner.tab.panels["Experiences"]=&experience;owner.tab.panels["RegionSettings"]=&aurora;assert(LLFloaterRegionInfo::getPanelExperiences()==&experience);assert(LLFloaterRegionInfo::getPanelOpenSettings()==&aurora);
 live=nullptr;check();assert(creations==0);live=&owner;native=false;assert(LLFloaterRegionInfo::getPanelEstate()==&estate&&creations==1);
}
