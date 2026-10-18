#include <cassert>
#include <memory>
#include <functional>
#include <type_traits>
#include <utility>
#include <optional>
#include <map>
using U64=unsigned long long;using S32=int;
struct LLUUID{int id=0;bool isNull()const{return !id;}bool operator==(LLUUID b)const{return id==b.id;}bool operator!=(LLUUID b)const{return !(*this==b);}};
struct Agent{LLUUID id{1},session{2};auto getID()const{return id;}auto getSessionID()const{return session;}}gAgent;
int account=1,regionEpoch=1,selectionEpoch=1;
std::function<bool()>vs_native_im_guard(){int saved=account;return [saved]{return saved==account;};}
struct LLView{virtual~LLView()=default;};
struct Slot{LLView*view=nullptr;};
template<class T>struct LLHandle{Slot*slot;T*get()const{return slot?dynamic_cast<T*>(slot->view):nullptr;}bool isDead()const{return !get();}};
struct LLPanel:LLView{Slot slot{this};auto getHandle(){return LLHandle<LLPanel>{&slot};}~LLPanel(){slot.view=nullptr;}};
struct LLFloater:LLView{Slot slot{this};auto getHandle(){return LLHandle<LLFloater>{&slot};}~LLFloater(){slot.view=nullptr;}};
struct Region{int epoch=regionEpoch;bool current()const{return epoch==regionEpoch;}};
std::optional<Region>vs_current_region_context(){return Region{};}
struct LLViewerParcelMgr{static auto&instance(){static LLViewerParcelMgr m;return m;}auto getSelectionRegionContext(){return vs_current_region_context();}std::function<bool()>selectionGuard(){int saved=selectionEpoch;return [saved]{return saved==selectionEpoch;};}};
struct LLPanelEnvironmentInfo:LLPanel{int parcel=31;bool region=false;bool isRegion(){return region;}int getParcelId(){return parcel;}std::function<bool()>requestGuard();};
struct LLInventoryItem{LLUUID asset{5};LLUUID getAssetUUID()const{return asset;}};
struct Inventory{std::map<int,LLInventoryItem*>items;auto*getItem(LLUUID id){auto i=items.find(id.id);return i==items.end()?nullptr:i->second;}}gInventory;
struct LLSettingsBase{using ptr_t=std::shared_ptr<LLSettingsBase>;};
struct LLFloaterEditEnvironmentBase:LLFloater{
 LLUUID mInventoryId;std::shared_ptr<LLInventoryItem>mInventoryItem;U64 mCallbackEpoch=1;
 LLSettingsBase::ptr_t settings=std::make_shared<LLSettingsBase>();
 auto getEditSettings(){return settings;}std::function<bool()>requestGuard();
};
struct LLFloaterSettingsPicker:LLFloater{LLUUID mSettingItemID{7};int publications=0;
 static void onAssetLoadedCb(LLHandle<LLFloater>,LLUUID,LLUUID,LLSettingsBase::ptr_t,S32);
 void onAssetLoaded(LLUUID,LLSettingsBase::ptr_t){++publications;}
};
METHODS
int main(){
 LLPanelEnvironmentInfo panel;auto current=panel.requestGuard();assert(current());
 ++selectionEpoch;assert(!current());current=panel.requestGuard();assert(current());
 ++regionEpoch;assert(!current());current=panel.requestGuard();assert(current());
 ++account;assert(!current());current=panel.requestGuard();assert(current());
 panel.parcel=32;assert(!current());panel.parcel=31;
 current=panel.requestGuard();panel.slot.view=nullptr;assert(!current());panel.slot.view=&panel;
 panel.region=true;current=panel.requestGuard();++selectionEpoch;assert(current());++regionEpoch;assert(!current());
 LLFloaterEditEnvironmentBase editor;auto edit=editor.requestGuard();assert(edit());
 ++editor.mCallbackEpoch;assert(!edit());edit=editor.requestGuard();
 editor.settings=std::make_shared<LLSettingsBase>();assert(!edit());
 editor.mInventoryId={7};editor.mInventoryItem=std::make_shared<LLInventoryItem>();gInventory.items[7]=editor.mInventoryItem.get();
 edit=editor.requestGuard();assert(edit());LLInventoryItem replacement;gInventory.items[7]=&replacement;assert(!edit());
 gInventory.items[7]=editor.mInventoryItem.get();edit=editor.requestGuard();gAgent.session={9};assert(!edit());gAgent.session={2};
 auto callback=vs_native_im_lifetime_callback(editor.requestGuard(),[](int a,int b){return a+b;});assert(callback(2,3)==5);
 ++editor.mCallbackEpoch;assert(callback(2,3)==0);int calls=0;
 auto three=vs_native_im_lifetime_callback(editor.requestGuard(),[&](int,int,int){++calls;});three(1,2,3);assert(calls==1);
 ++account;three(1,2,3);assert(calls==1);
 LLFloaterSettingsPicker picker;auto settings=std::make_shared<LLSettingsBase>();
 LLFloaterSettingsPicker::onAssetLoadedCb(picker.getHandle(),{7},{5},settings,0);assert(picker.publications==1);
 replacement.asset={6};gInventory.items[7]=&replacement;
 LLFloaterSettingsPicker::onAssetLoadedCb(picker.getHandle(),{7},{5},settings,0);assert(picker.publications==1);
 LLFloaterSettingsPicker::onAssetLoadedCb(picker.getHandle(),{7},{6},nullptr,0);assert(picker.publications==1);
 LLFloaterSettingsPicker::onAssetLoadedCb(picker.getHandle(),{7},{6},settings,1);assert(picker.publications==1);
 picker.slot.view=nullptr;LLFloaterSettingsPicker::onAssetLoadedCb(picker.getHandle(),{7},{6},settings,0);assert(picker.publications==1);
}
