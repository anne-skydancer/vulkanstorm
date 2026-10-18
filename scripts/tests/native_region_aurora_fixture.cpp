#include <cassert>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
namespace boost{using std::bind;}
std::ostringstream logger;
#define LL_INFOS() logger
#define LL_ENDL ""
struct LLSD{using Boolean=bool;using Integer=int;using Real=double;double n=0;std::map<std::string,LLSD>members;LLSD()=default;LLSD(double v):n(v){}LLSD(int v):n(v){}LLSD(bool v):n(v){}explicit operator bool()const{return n!=0;}explicit operator int()const{return int(n);}explicit operator double()const{return n;}LLSD&operator[](const std::string&key){return members[key];}};
struct LLUICtrl{LLSD value;bool enabled=true;std::function<void()>commit;void setValue(LLSD v){value=v;}const LLSD&getValue()const{return value;}void setEnabled(bool v){enabled=v;}void setCommitCallback(std::function<void()>f){commit=f;}};
using LLView=LLUICtrl;
struct VSRegionContext{bool manager=false;bool canManageEstate()const{return manager;}};std::optional<VSRegionContext>vs_current_region_context(){return VSRegionContext{true};}
struct Agent{bool god=false;bool isGodlike()const{return god;}std::string getRegionCapability(const char*)const{return"http://loopback/ors";}}gAgent;int gMaxAgentGroups=42;
struct VSRegionLimits{bool teen=true;bool getEnableTeenMode()const{return teen;}
GETTERS
}limits;VSRegionLimits&vs_region_limits(){return limits;}
LLSD posted;
namespace LLCoreHttpUtil{struct HttpCoroutineAdapter{static void messageHttpPost(std::string,const LLSD&body,const char*,const char*){posted=body;}};}
struct LLPanelRegionInfo{std::map<std::string,LLUICtrl>controls;bool enabled=true;int refreshCount=0;
 template<class T>T*getChild(const std::string&name){return &controls.at(name);}template<class T>T*findChild(const std::string&name,bool){auto i=controls.find(name);return i==controls.end()?nullptr:&i->second;}LLUICtrl*findChildView(const std::string&name){return findChild<LLUICtrl>(name,true);}
 void childSetValue(const char*name,LLSD v){controls.at(name).value=v;}LLSD childGetValue(const char*name){return controls.at(name).value;}void setCtrlsEnabled(bool b){enabled=b;}template<class F>void childSetAction(const char*name,F,void*){assert(controls.count(name));}
 void initCtrl(const std::string&);virtual void onChangeAnything();void enableButton(const std::string&,bool enable=true);void refresh(){++refreshCount;}bool refreshFromRegion(const std::optional<VSRegionContext>&){return true;}bool postBuild(){return true;}
};
struct LLPanelRegionOpenSettingsInfo:LLPanelRegionInfo{void onChangeAnything() override;bool refreshFromRegion(const std::optional<VSRegionContext>&);bool postBuild();static void onClickOrs(void*);};
METHODS
int main(){
 LLPanelRegionOpenSettingsInfo view;
NAMES
 assert(!view.controls.count("enable_teen_mode"));assert(view.postBuild());assert(view.controls.at("enforce_max_build").commit);view.controls.at("apply_ors_btn").enabled=false;view.controls.at("enforce_max_build").value=false;view.controls.at("enforce_max_build").commit();assert(view.controls.at("apply_ors_btn").enabled&&view.refreshCount==1);
 view.refreshFromRegion(VSRegionContext{false});assert(!view.enabled);gAgent.god=true;view.refreshFromRegion(std::nullopt);assert(view.enabled);gAgent.god=false;
 LLPanelRegionOpenSettingsInfo::onClickOrs(&view);assert(bool(posted["enable_teen_mode"]));assert(!bool(posted["enforce_max_build"]));
 view.controls["enable_teen_mode"];assert(view.postBuild());assert(view.controls.at("enable_teen_mode").commit);view.refreshFromRegion(VSRegionContext{true});assert(bool(view.controls.at("enable_teen_mode").value));view.controls.at("enable_teen_mode").value=false;LLPanelRegionOpenSettingsInfo::onClickOrs(&view);assert(!bool(posted["enable_teen_mode"]));assert(limits.teen);
}
