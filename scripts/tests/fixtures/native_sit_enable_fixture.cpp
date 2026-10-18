#include <cassert>
#include <memory>
#include <optional>
#include <string>
using U8=unsigned char;constexpr int LL_PCODE_VOLUME=9;constexpr int RLV_BHVR_SIT=1;
struct UUID{int value=1;bool notNull()const{return value!=0;}};struct Vec{};
struct Avatar{bool sitting=false,editing=false;bool isSitting()const{return sitting;}bool isEditingAppearance()const{return editing;}}avatar;Avatar*gAgentAvatarp=&avatar;
bool avatarValid=true;bool isAgentAvatarValid(){return avatarValid;}Avatar*vs_avatar_character(){return avatarValid?&avatar:nullptr;}
struct Agent{bool flying=false;bool isSitting(){return avatar.sitting;}bool getFlying(){return flying;}}gAgent;
struct Rlv{bool blocked=false;bool hasBehaviour(int){return blocked;}}gRlvHandler;
namespace rlv_handler_t{bool enabled=false;bool isEnabled(){return enabled;}}
bool sitAllowed=true,standAllowed=true;
struct LLViewerObject{int code=9;int getPCode()const{return code;}}object;
struct VSObjectContext{U8 pcode=9;bool(*current)()=nullptr;static std::optional<VSObjectContext>resolve(UUID);};bool sourceCurrent=true;std::optional<VSObjectContext>VSObjectContext::resolve(UUID){return VSObjectContext{U8(object.code),[]{return sourceCurrent;}};}
namespace RlvActions{bool canStand(){return standAllowed;}bool canSit(const LLViewerObject*,Vec){return sitAllowed;}bool canSit(const VSObjectContext&,Vec){return sitAllowed;}}
struct LLPickInfo{UUID mObjectID;Vec mObjectOffset;LLViewerObject*getObject()const{return &object;}}pick;
struct LLToolPie{static LLToolPie*getInstance(){static LLToolPie t;return &t;}const LLPickInfo&getPick(){return pick;}};
struct Properties{std::string sit_name;};Properties properties;
struct Pub{Properties*properties;};
struct Handle{UUID id;};
struct Selection{bool exists=true,rootExists=true,valid=true;std::optional<Handle>primaryObject(){return exists?std::optional{Handle{}}:std::nullopt;}std::optional<Handle>firstRootObject(){return rootExists?std::optional{Handle{}}:std::nullopt;}std::optional<Pub>findProperties(Handle){return valid?std::optional{Pub{&properties}}:std::nullopt;}}selection;
struct Appearance{bool isEditing(){return avatar.editing;}}appearance;
struct VSNativeSession{static std::shared_ptr<VSNativeSession>active(){static auto s=std::make_shared<VSNativeSession>();return s;}Appearance*appearance(){return &::appearance;}Selection*retainObjectSelection(){return &selection;}};
struct Window{bool native=false;bool isNativeVulkan(){return native;}}window;Window*gViewerWindow=&window;
struct LLSelectNode{bool mValid=true;std::string mSitName;};LLSelectNode row;
struct OldSelection{LLViewerObject*getPrimaryObject(){return selection.exists?&object:nullptr;}LLSelectNode*getFirstRootNode(){row.mValid=selection.valid;row.mSitName=properties.sit_name;return selection.rootExists?&row:nullptr;}}oldSelection;
struct LLSelectMgr{static LLSelectMgr*getInstance(){static LLSelectMgr s;return &s;}OldSelection*getSelection(){return &oldSelection;}};
struct LLUICtrl{std::string value;void setValue(std::string s){value=s;}std::string getName(){return "sit";}};
bool sittingOnSelection=false;bool sitting_on_selection(){return sittingOnSelection;}void init_default_item_label(LLUICtrl*){}std::string get_default_item_label(std::string){return "Sit Here";}
METHODS
int main(){for(unsigned bits=0;bits<8192;++bits){avatarValid=bits&1;avatar.sitting=bits&2;avatar.editing=bits&4;gAgent.flying=bits&8;gRlvHandler.blocked=bits&16;standAllowed=bits&32;rlv_handler_t::enabled=bits&64;sitAllowed=bits&128;selection.exists=bits&256;selection.rootExists=bits&512;selection.valid=bits&1024;properties.sit_name=bits&2048?"Custom Sit":"";sittingOnSelection=bits&4096;object.code=bits%3?9:47;pick.mObjectID.value=bits%5?1:0;sourceCurrent=true;LLUICtrl old,newer;window.native=false;const bool a=enable_standup_self(),b=enable_sitdown_self(),c=is_object_sittable(),d=enable_object_sit(&old);window.native=true;assert(a==enable_standup_self()&&b==enable_sitdown_self()&&c==is_object_sittable()&&d==enable_object_sit(&newer)&&old.value==newer.value);}
window.native=true;sourceCurrent=false;rlv_handler_t::enabled=true;pick.mObjectID.value=1;selection.exists=true;assert(!is_object_sittable());}
