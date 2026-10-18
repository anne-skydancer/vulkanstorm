#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <type_traits>
using U64=unsigned long long;using S32=int;using LLUUID=int;using uuid_vec_t=std::vector<int>;
struct LLSD{int option=0;LLSD&operator[](const char*){return *this;}LLSD&operator=(const std::string&){return *this;}};
struct LLTrans{static std::string getString(const char*s){return s;}};
int nativeAccount=1;
std::function<bool()>vs_native_im_guard(){const int current=nativeAccount;return [current]{return current==nativeAccount;};}
struct Agent{int id=1,session=2;int getID(){return id;}int getSessionID(){return session;}}gAgent;
struct LLFloater;
struct Slot{LLFloater*view=nullptr;};
struct Handle{std::shared_ptr<Slot>slot;LLFloater*get()const{return slot->view;}};
struct LLFloater{std::shared_ptr<Slot>slot=std::make_shared<Slot>();int closes=0;LLFloater(){slot->view=this;}virtual~LLFloater(){slot->view=nullptr;}auto getHandle(){return Handle{slot};}void onClose(bool){++closes;}};
namespace LLInventoryType{constexpr int IT_SETTINGS=5;}
namespace LLFolderType{constexpr int FT_TRASH=9;}
struct LLInventoryItem{int id=0,parent=2,type=LLInventoryType::IT_SETTINGS;bool removable=true;int getInventoryType(){return type;}int getParentUUID(){return parent;}};
int wireUpdates=0;
struct LLViewerInventoryItem:LLInventoryItem{LLViewerInventoryItem()=default;explicit LLViewerInventoryItem(LLInventoryItem*p){static_cast<LLInventoryItem&>(*this)=*p;}void setParent(int p){parent=p;}void updateParentOnServer(bool){++wireUpdates;}};
template<class T>struct LLPointer:std::shared_ptr<T>{LLPointer(T*p):std::shared_ptr<T>(p){}};
struct LLViewerInventoryCategory{};
struct LLInventoryModel{using LLCategoryUpdate=std::pair<int,int>;using update_list_t=std::vector<LLCategoryUpdate>;};
struct Inventory{std::map<int,std::shared_ptr<LLViewerInventoryItem>>items;int notifications=0,accounting=0;
int findCategoryUUIDForType(int){return 9;}auto*getItem(int id){auto i=items.find(id);return i==items.end()?nullptr:i->second.get();}
bool isObjectDescendentOf(int id,int root){auto*p=getItem(id);return p && p->parent==root;}LLViewerInventoryCategory*getCategory(int){return nullptr;}
void accountForUpdate(const LLInventoryModel::update_list_t&updates){assert(updates.size()==2 && updates[0].second==-1 && updates[1]==std::make_pair(9,1));++accounting;}
void updateItem(std::shared_ptr<LLViewerInventoryItem>p){items[p->id]=std::move(p);}void notifyObservers(){++notifications;}
}gInventory;
bool get_is_item_removable(Inventory*,int id,bool){auto*p=gInventory.getItem(id);return p && p->removable;}
bool get_is_category_removable(Inventory*,int){return false;}
namespace LLNotificationsUtil{std::function<void(const LLSD&,const LLSD&)>pending;int getSelectedOption(const LLSD&,const LLSD&response){return response.option;}template<class C>void add(const char*,const LLSD&,const LLSD&,C callback){pending=callback;}}
struct LLFloaterMyEnvironment:LLFloater{U64 mRequestEpoch=0;uuid_vec_t selection{1};std::function<bool()>requestGuard();void onClose(bool);void onDeleteSelected();void onItemsRemovalConfirmation(const LLSD&,const LLSD&,uuid_vec_t);void getSelectedIds(uuid_vec_t&out){out=selection;}};
METHODS
int main(){auto reset=[] {gInventory.items.clear();auto item=std::make_shared<LLViewerInventoryItem>();item->id=1;gInventory.items[1]=item;};
reset();LLFloaterMyEnvironment view;view.onDeleteSelected();auto current=LLNotificationsUtil::pending;current({},{});assert(wireUpdates==1 && gInventory.getItem(1)->parent==9 && gInventory.accounting==1);
reset();view.onDeleteSelected();auto closed=LLNotificationsUtil::pending;view.onClose(false);closed({},{});assert(wireUpdates==1 && gInventory.getItem(1)->parent==2 && view.closes==1);
view.onDeleteSelected();auto account=LLNotificationsUtil::pending;++nativeAccount;account({},{});assert(wireUpdates==1);
view.onDeleteSelected();auto session=LLNotificationsUtil::pending;++gAgent.session;session({},{});assert(wireUpdates==1);
view.onDeleteSelected();auto reopened=LLNotificationsUtil::pending;++view.mRequestEpoch;reopened({},{});assert(wireUpdates==1);
view.onDeleteSelected();auto denied=LLNotificationsUtil::pending;gInventory.getItem(1)->removable=false;denied({},{});assert(wireUpdates==1 && gInventory.getItem(1)->parent==2);
reset();{auto owned=std::make_unique<LLFloaterMyEnvironment>();owned->onDeleteSelected();}auto destroyed=LLNotificationsUtil::pending;destroyed({},{});assert(wireUpdates==1);
view.onDeleteSelected();auto canceled=LLNotificationsUtil::pending;LLSD response;response.option=1;canceled({},response);assert(wireUpdates==1);
}
