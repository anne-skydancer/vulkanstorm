#include <cassert>
#include <list>
#include <map>
#include <string>
#include <string_view>
using LLSD=int;
struct LLMortician{
 virtual~LLMortician();static inline std::list<LLMortician*>sGraveyard;
 static inline bool sDestroyImmediate=false;bool mIsDead=false;void die();static void updateClass();
};
struct Slot{bool live=true;};
struct LLFloater:LLMortician{
 std::string mInstanceName="buy_land";int mKey=0;Slot&slot;bool visible=true;
 explicit LLFloater(Slot&owner):slot(owner){}~LLFloater(){slot.live=false;}
 bool matchesKey(int key){return mKey==key;}void setVisible(bool value){visible=value;}void destroy();
};
struct LLFloaterReg{
 using instance_list_t=std::list<LLFloater*>;
 static inline std::map<std::string,std::string,std::less<>>sGroupMap{{"buy_land","buy_land"}};
 static inline std::map<std::string,instance_list_t>sInstanceMap;
 static LLFloater*removeInstance(std::string_view,const LLSD&);static bool destroyInstance(std::string_view,const LLSD&);
};
struct Connection{bool live=true;void disconnect(){live=false;}};
struct LLViewerParcelMgr{
 int observers=0;
 static bool instanceExists(){return true;}static auto&instance(){static LLViewerParcelMgr manager;return manager;}
 void removeObserver(int*){--observers;assert(observers>=0);}static void deleteParcelBuy(int**value){delete *value;*value=nullptr;}
};
struct LLFloaterBuyLandUI:LLFloater{
 Connection mVisibilityConnection;int mParcelSelectionObserver=0;int*mParcelBuyInfo=new int(1),*mTransaction=new int(1);
 explicit LLFloaterBuyLandUI(Slot&slot):LLFloater(slot){++LLViewerParcelMgr::instance().observers;}
 ~LLFloaterBuyLandUI();void onClose(bool);
};
METHODS
int main(){
 Slot first;auto*ui=new LLFloaterBuyLandUI(first);LLFloaterReg::sInstanceMap["buy_land"].push_back(ui);
 assert(LLViewerParcelMgr::instance().observers==1);
 ui->onClose(false);assert(first.live && !ui->visible && ui->mIsDead);
 assert(LLFloaterReg::sInstanceMap["buy_land"].empty());
 assert(!LLFloaterReg::destroyInstance("buy_land",0));
 assert(LLViewerParcelMgr::instance().observers==1);ui->die();assert(LLMortician::sGraveyard.size()==1);
 LLMortician::updateClass();assert(!first.live && LLViewerParcelMgr::instance().observers==0 && LLMortician::sGraveyard.empty());
 Slot second;auto*registered=new LLFloaterBuyLandUI(second);LLFloaterReg::sInstanceMap["buy_land"].push_back(registered);
 assert(LLFloaterReg::destroyInstance("buy_land",0));assert(!second.live && LLViewerParcelMgr::instance().observers==0);
 LLMortician::updateClass();assert(LLMortician::sGraveyard.empty());
}
