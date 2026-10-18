#include <map>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <type_traits>
#include <utility>
#include <cassert>
struct LLUUID{int value;LLUUID(int v=0):value(v){}operator int()const{return value;}inline static const int null=0;};using uuid_vec_t=std::vector<int>;struct LLSD{std::string value;std::string asString()const{return value;}};
unsigned epoch=1;std::function<bool()>vs_native_im_guard(){auto saved=epoch;return[saved]{return saved==epoch;};}
ACCOUNT
LIFETIME
namespace boost{using std::bind;}
using namespace std::placeholders;
std::map<std::string,std::function<void(const LLSD&)>>commits;std::map<std::string,std::function<bool(const LLSD&)>>enables;
namespace LLUICtrl{struct CommitCallbackRegistry{struct ScopedRegistrar{template<class F>void add(const char*n,F f){commits[n]=[f](const LLSD&p)mutable{f(nullptr,p);};}};};struct EnableCallbackRegistry{struct ScopedRegistrar{template<class F>void add(const char*n,F f){enables[n]=[f](const LLSD&p)mutable{return f(nullptr,p);};}};};}
struct LLContextMenu{bool dead=false;void die(){dead=true;}};
struct Handle{LLContextMenu*value=nullptr;auto*get(){return value;}void markDead(){value=nullptr;}};
std::vector<int>calls;
struct LLAvatarActions{static void showProfile(int id){calls.push_back(id);}static void startIM(int id){calls.push_back(id);}static void viewChatHistory(int id){calls.push_back(id);}static void requestFriendshipDialog(int id){calls.push_back(id);}static void zoomIn(int id){calls.push_back(id);}static void teleportRequest(int id){calls.push_back(id);}static void removeFriendDialog(int id){calls.push_back(id);}static void toggleBlock(int id){calls.push_back(id);}static bool canZoomIn(int id){return id!=0;}static bool isBlocked(int){return false;}static void startConference(uuid_vec_t ids,int){calls.insert(calls.end(),ids.begin(),ids.end());}static void removeFriendsDialog(uuid_vec_t ids){calls.insert(calls.end(),ids.begin(),ids.end());}};
struct FSNameListAvatarMenu{uuid_vec_t mUUIDs;std::shared_ptr<const void>mMenuOwner;Handle mMenuHandle;LLContextMenu menu;LLContextMenu*createMenu();void resetAccount();static bool isNativeCallback(std::string_view,const LLSD&);bool enableContextMenuItem(const LLSD&){return !mUUIDs.empty();}void addToContactSet(){}void teleportToAvatar(){}void offerTeleport(){}void onTrackAvatarMenuItemClick(){}void copyNameToClipboard(const LLUUID&id){calls.push_back(id);}void copySLURLToClipboard(const LLUUID&id){calls.push_back(id);}LLContextMenu*createFromFile(const char*){mMenuHandle.value=&menu;return&menu;}};
CREATE
RESET
GATE
int main(){FSNameListAvatarMenu menu;menu.mUUIDs={41};menu.createMenu();GATE_ASSERTIONS
commits["Namelist.SendIM"]({});assert(calls==std::vector<int>({41}));auto old=commits["Namelist.SendIM"];auto oldEnable=enables["Namelist.EnableItem"];menu.mUUIDs={72};menu.createMenu();old({});assert(!oldEnable({})&&calls.size()==1);commits["Namelist.CopyUrl"]({});assert(calls==std::vector<int>({41,72}));auto stale=commits["Namelist.SendIM"];++epoch;stale({});assert(calls.size()==2);menu.resetAccount();assert(menu.menu.dead&&menu.mUUIDs.empty()&&!menu.mMenuOwner);assert(!FSNameListAvatarMenu::isNativeCallback("Namelist.EnableItem",{"unreviewed_scene_action"}));}
