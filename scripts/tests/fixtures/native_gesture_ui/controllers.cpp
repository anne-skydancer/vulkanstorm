#include <cassert>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <string_view>
using S32=int;
struct LLUUID{int value=0;bool operator!=(const LLUUID&o)const{return value!=o.value;}static const LLUUID null;};const LLUUID LLUUID::null{};
struct LLSD{LLSD&operator=(const std::string&s){text=s;return*this;}std::map<std::string,LLSD>map;std::string text;LLUUID id;const LLSD&operator[](const std::string&key)const{static LLSD empty;auto i=map.find(key);return i==map.end()?empty:i->second;}LLSD&operator[](const std::string&key){return map[key];}std::string asString()const{return text;}LLUUID asUUID()const{return id;}};
int generation=1,opens=0,updates=0;bool reenter=false;std::function<bool()>vs_native_im_guard(){auto expected=generation;return[expected]{return generation==expected;};}bool vs_native_im_notification_current(const LLSD&p){return p["epoch"].id.value==generation;}
struct LLInventoryCallback{virtual~LLInventoryCallback()=default;virtual void fire(const LLUUID&)=0;};
struct LLPermissions{void setMaskNext(int){}void setMaskEveryone(int){}void setMaskGroup(int){}};struct LLInventoryItem{std::string name="gesture";LLPermissions getPermissions(){return {};}void setPermissions(LLPermissions){}void updateServer(bool){++updates;}std::string getName(){return name;}}item;struct Inventory{LLInventoryItem*getItem(LLUUID){return &item;}}gInventory;
using LLViewerInventoryItem=LLInventoryItem;
struct LLFloaterPerms{static int getNextOwnerPerms(const char*){return 1;}static int getEveryonePerms(const char*){return 2;}static int getGroupPerms(const char*){return 3;}};
struct LLPreviewGesture{static void show(LLUUID,LLUUID){++opens;if(reenter)++generation;}};
struct LLFloater{virtual~LLFloater()=default;std::shared_ptr<LLFloater*>life=std::make_shared<LLFloater*>(this);std::weak_ptr<LLFloater*>getHandle(){return life;}};template<class T>struct LLHandle{std::weak_ptr<T*>weak;LLHandle(std::weak_ptr<T*>w):weak(w){}T*get()const{auto p=weak.lock();return p?*p:nullptr;}};struct LLScrollListCtrl{};struct LLMultiGesture{};
struct LLFloaterGesture:LLFloater{int added=0,refresh=0,played=0;template<class T>T*getChild(const char*){return nullptr;}void addGesture(LLUUID,void*,LLScrollListCtrl*){++added;}void refreshAll(){++refresh;}void playGesture(LLUUID id){played=id.value;}std::function<void(LLMultiGesture*)>gestureLoadedCallback(const LLUUID&);static bool isNativeCallback(std::string_view,const LLSD&);static void onGestureRename(const LLSD&,const LLSD&);};
struct LLNotificationsUtil{static int getSelectedOption(const LLSD&,const LLSD&){return 0;}};struct LLInventoryObject{static void correctInventoryName(std::string&) {}};void update_inventory_item(LLUUID,const LLSD&,void*){++updates;}
SHOW
COPY
LOADED
RENAME
POLICY
int main(){POLICY_ASSERTIONS
GestureShowCallback show;show.fire({5});assert(opens==1&&updates==1);reenter=true;show.fire({5});assert(opens==2&&updates==1);show.fire({5});assert(opens==2);reenter=false;
std::unique_ptr<GestureCopiedCallback>copy;std::function<void(LLMultiGesture*)>loaded;{LLFloaterGesture floater;copy=std::make_unique<GestureCopiedCallback>(&floater);copy->fire({5});assert(floater.added==1&&floater.refresh==1);loaded=floater.gestureLoadedCallback({17});loaded(nullptr);assert(floater.played==17);++generation;copy->fire({5});floater.played=0;loaded(nullptr);assert(floater.added==1&&floater.played==0);}copy->fire({5});loaded(nullptr);
LLSD notification,response;notification["payload"]["epoch"].id.value=generation;notification["payload"]["gesture_id"].id={5};response["new_name"].text="new";LLFloaterGesture::onGestureRename(notification,response);assert(updates==2);++generation;LLFloaterGesture::onGestureRename(notification,response);assert(updates==2);}
