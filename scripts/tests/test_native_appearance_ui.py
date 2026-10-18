from pathlib import Path
import os,unittest
from test_native_material_selection import run,body
ROOT=Path(os.environ.get('VS_APPEARANCE_TEST_ROOT',Path(__file__).resolve().parents[2]))
OVERLAY=Path(os.environ['VS_APPEARANCE_PROPOSED']) if 'VS_APPEARANCE_PROPOSED' in os.environ else None
def source(p):
 q=OVERLAY/p if OVERLAY else ROOT/p
 return (q if q.exists() else ROOT/p).read_text()
class NativeAppearanceUITests(unittest.TestCase):
 def test_actual_favorites_category_transaction_does_not_cross_account(self):
  code=body(source('indra/newview/fsfloaterwearablefavorites.cpp'),'void FSFloaterWearableFavorites::initCategory(')
  run(self,r'''
#include <cassert>
#include <functional>
#include <vector>
#include <optional>
#include <string>
struct LLUUID{int n=0;bool isNull()const{return!n;}};
using inventory_func_type=std::function<void(const LLUUID&)>;
namespace LLFolderType{enum EType{FT_NONE};}
const char*ROOT_FIRESTORM_FOLDER="Firestorm";const char*FS_WEARABLE_FAVORITES_FOLDER="#Wearable Favorites";
struct Warning{template<class T>Warning&operator<<(const T&){return*this;}};
#define LL_WARNS() Warning()
#define LL_ENDL 0
bool active=true;std::function<bool()>vs_native_im_guard(){const int old=active;return[old]{return old&&active;};}
struct Inventory{bool usable=true;int created=0;std::vector<inventory_func_type>callbacks;bool isInventoryUsable(){return usable;}LLUUID getRootFolderID(){return{1};}LLUUID findCategoryByName(const char*){return{};}void createNewCategory(LLUUID,LLFolderType::EType,const char*,inventory_func_type cb){++created;callbacks.push_back(cb);}void complete(){auto cb=callbacks.front();callbacks.erase(callbacks.begin());cb({10+created});}}gInventory;
struct FSFloaterWearableFavorites{static LLUUID sFolderID;static std::optional<LLUUID>getWearableFavoritesFolderID(){return{};}static void initCategory(inventory_func_type callback)BODY};LLUUID FSFloaterWearableFavorites::sFolderID;
int main(){int completed=0;FSFloaterWearableFavorites::initCategory([&](auto){++completed;});assert(gInventory.created==1);active=false;gInventory.complete();assert(gInventory.created==1&&completed==0&&FSFloaterWearableFavorites::sFolderID.isNull());active=true;FSFloaterWearableFavorites::initCategory([&](auto){++completed;});gInventory.complete();assert(gInventory.created==3);active=false;gInventory.complete();assert(completed==0&&FSFloaterWearableFavorites::sFolderID.isNull());active=true;FSFloaterWearableFavorites::initCategory([&](auto){++completed;});gInventory.complete();gInventory.complete();assert(completed==1&&!FSFloaterWearableFavorites::sFolderID.isNull());}
'''.replace('BODY',code))
 def test_original_wear_remove_rlva_policy_remains_in_actual_controller(self):
  cpp=source('indra/newview/fsfloaterwearablefavorites.cpp')
  click=body(cpp,'void FSFloaterWearableFavorites::onDoubleClick()')
  self.assertLess(click.index('if (!mAccountCurrent()) return;'),click.index('getSelectedUUID'))
  self.assertLess(click.index('if (!item) return;'),click.index('item->getType()'))
  for policy in ('gRlvWearableLocks.canRemove(item)','gRlvAttachmentLocks.canDetach(item)','gRlvWearableLocks.canWear(item)','gRlvAttachmentLocks.canAttach(item)','LLAppearanceMgr::instance().canAddWearables(ids)','wear_multiple(ids, true)','wear_multiple(ids, false)','LLAppearanceMgr::instance().removeItemsFromAvatar(ids)'):
   self.assertIn(policy,click)
 def test_real_favorites_graph_and_account_retirement_are_paired(self):
  ui=source('indra/newview/vsstartupui.cpp');reset=source('indra/newview/vsnativeim.cpp')
  for t in ('FSFloaterWearableFavorites','FSWearableFavoritesItemsList','LLWearableItemsList','LLPanelWearableOutfitItem'):
   self.assertIn('typeid('+t+')',ui)
  self.assertIn('name == "fs_wearable_favorites"',ui)
  self.assertIn('name == "wearable_favorites"',ui)
  self.assertIn('FSFloaterWearableFavorites::retireNativeAccount();',reset)
 def test_actual_attachment_label_uses_current_native_metadata_and_original_lock(self):
  native=body(body(source('indra/newview/llviewermenu.cpp'),'static bool onEnableAttachmentLabel('),'if (const auto owner = VSNativeSession::active())')
  run(self,r'''
#define VS_NATIVE_VULKAN 1
#include <cassert>
#include <algorithm>
#include <string>
#include <vector>
using S32=int;struct LLSD{int index=0;std::string label;LLSD operator[](const char*key)const{return std::string(key)=="index"?LLSD{index,{}}:LLSD{0,label};}int asInteger()const{return index;}std::string asString()const{return label;}};
struct LLUICtrl{virtual~LLUICtrl()=default;};struct LLMenuItemGL:LLUICtrl{std::string label;void setLabel(std::string s){label=s;}};
struct Owner{bool ready=true;bool regionReady(){return ready;}};Owner state;struct VSNativeSession{static Owner*active(){return&state;}};
struct Attachment{unsigned point;int object,item;};std::vector<std::pair<unsigned,std::string>>vs_avatar_attachment_points(){return{{1,"Chest"},{31,"HUD Center 2"}};}std::vector<Attachment>vs_avatar_attachments(){return{{31,1,70},{31,2,71},{1,3,72}};}
struct Item{int id;std::string name;int getUUID(){return id;}std::string getName(){return name;}};Item a{70,"Bridge"},b{71,"HUD"},c{72,"Shirt"};struct Inventory{Item*getItem(int id){return id==70?&a:id==71?&b:id==72?&c:nullptr;}}gInventory;
constexpr int FS_BRIDGE_POINT=31,RLV_LOCK_ADD=1;struct FSLSLBridge{static FSLSLBridge&instance(){static FSLSLBridge x;return x;}Item*getBridge(){return&a;}};
bool rlv=false,locked=false;struct rlv_handler_t{static bool isEnabled(){return rlv;}};struct Locks{int point=-1;bool isLockedAttachmentPoint(int p,int mask){assert(mask==RLV_LOCK_ADD);point=p;return locked;}}gRlvAttachmentLocks;
bool enable(LLUICtrl*ctrl,const LLSD&data){auto owner=VSNativeSession::active();BODY}
int main(){LLMenuItemGL menu;assert(enable(&menu,{31,"HUD Center 2"}));assert(menu.label=="HUD Center 2 (HUD)");assert(enable(&menu,{1,"Chest"}));assert(menu.label=="Chest (Shirt)");rlv=true;locked=true;assert(!enable(&menu,{1,"Chest"})&&gRlvAttachmentLocks.point==1);assert(!enable(&menu,{99,"Unknown"}));state.ready=false;assert(!enable(&menu,{1,"Chest"}));}
'''.replace('BODY',native))

if __name__=='__main__':unittest.main()
