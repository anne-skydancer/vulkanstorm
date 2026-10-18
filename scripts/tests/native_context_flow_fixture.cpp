#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <map>
using S32=int;using U32=unsigned;using MASK=unsigned;
std::vector<std::string> trace;
void call(std::string value){trace.push_back(std::move(value));}
struct LLUUID{int value=0;bool notNull()const{return value!=0;}bool operator==(LLUUID rhs)const{return value==rhs.value;}bool operator!=(LLUUID rhs)const{return !(*this==rhs);}};
struct Point{int mX=7,mY=9;};
struct LLViewerObject
{
 bool attached=false,hud=false,owned=false,avatar=false;LLViewerObject* parent=nullptr;LLUUID id{3};
 bool isAttachment(){call("attachment");return attached;}bool isHUDAttachment(){call("hud");return hud;}
 bool permYouOwner(){call("owned");return owned;}bool isAvatar(){call("avatar");return avatar;}
 LLViewerObject* getParent(){call("parent");return parent;}LLUUID getID(){call("id");return id;}
 std::string getFullname(){call("fullname");return "Real Avatar";}
};
using LLVOAvatar=LLViewerObject;
struct LLPickInfo
{
 enum{PICK_LAND=1};int mPickType=0;Point mMousePt;MASK mKeyMask=0;LLViewerObject* object=nullptr;LLUUID mObjectID{3},mParticleOwnerID{};int mPosGlobal=11;
 LLViewerObject* getObject()const{call("object");return object;}
};
struct LLUICtrl{void setValue(std::string value){call("label:"+value);}};
struct Menu
{
 std::string name;LLUICtrl control;
 void show(int,int){call("show:"+name);}
 template<class T>T* getChild(const char* name){call(std::string("child:")+name);return &control;}
 void setItemVisible(const char* name,bool value){call(std::string("visible:")+name+(value?"1":"0"));}
};
Menu land{"land"},self{"self"},other{"other"},object{"object"},attached{"attached"},attachedOther{"attached-other"},particles{"particles"};
Menu pieLand{"pie-land"},pieSelf{"pie-self"},pieOther{"pie-other"},pieObject{"pie-object"},pieAttached{"pie-attached"},pieAttachedOther{"pie-attached-other"},pieParticles{"pie-particles"};
Menu *gMenuLand=&land,*gMenuAvatarSelf=&self,*gMenuAvatarOther=&other,*gMenuObject=&object,*gMenuAttachmentSelf=&attached,*gMenuAttachmentOther=&attachedOther,*gMenuMuteParticle=&particles;
Menu *gPieMenuLand=&pieLand,*gPieMenuAvatarSelf=&pieSelf,*gPieMenuAvatarOther=&pieOther,*gPieMenuObject=&pieObject,*gPieMenuAttachmentSelf=&pieAttached,*gPieMenuAttachmentOther=&pieAttachedOther,*gPieMenuMuteParticle=&pieParticles;
struct Node{std::string mName="Original Name";};
using LLSelectNode=Node;
struct Selection{bool empty=false;int roots=1;Node node;bool isEmpty(){call("empty");return empty;}int getRootObjectCount(){call("rootcount");return roots;}Node* getFirstRootNode(){call("rootname");return roots?&node:nullptr;}}selection;
struct LLSelectMgr{static LLSelectMgr& instance(){static LLSelectMgr s;return s;}static LLSelectMgr* getInstance(){return &instance();}Selection* getSelection(){return &selection;}};
struct Holder{void setParcelSelection(int){call("retain-parcel");}void setObjectSelection(Selection*){call("retain-object");}}holder,*gMenuHolder=&holder;
using LLParcelSelectionHandle=int;
struct LLViewerParcelMgr{static LLViewerParcelMgr* getInstance(){static LLViewerParcelMgr p;return &p;}void deselectLand(){call("deselect-land");}int selectParcelAt(int){call("select-parcel");return 1;}};
struct Settings{bool pie=false;bool getBOOL(const char*){call("setting-pie");return pie;}}gSavedSettings;
struct Agent{LLUUID getID(){return {1};}}gAgent;
constexpr int RLV_BHVR_FARTOUCH=1;
bool rlv=false,far=false,muted=false;unsigned renderModeBits=0;
struct Rlv{bool hasBehaviour(int){call("fartouch");return far;}}gRlvHandler;
struct rlv_handler_t{static bool isEnabled(){call("rlv");return rlv;}};
struct LLTrans{static std::string getString(const char* value){return value;}};
struct LLMuteList{static LLMuteList* getInstance(){static LLMuteList s;return &s;}bool isMuted(LLUUID,const std::string& name){call("muted:"+name);return muted;}};
struct Focus{bool keyboard=true;void* getKeyboardFocus(){call("focus");return keyboard?this:nullptr;}void setKeyboardFocus(void*){call("clear-focus");keyboard=false;}}gFocusMgr;
void make_ui_sound(const char* value){call(std::string("sound:")+value);}
bool check_avatar_render_mode(U32 mode){call("render:"+std::to_string(mode));return (renderModeBits&(1u<<mode))!=0;}
struct LLTool{bool handleRightMouseDown(int,int,MASK){call("base-right");return false;}};
struct LLToolSelect{static void handleObjectSelection(const LLPickInfo&,bool,bool){call("select");}};
#define llassert(x) assert(x)
struct LLToolPie:LLTool{LLPickInfo mPick;bool handleRightClickPick();void showVisualContextMenuEffect(){call("feedback");}};
ORIGINAL
PEER
KERNEL
int main()
{
 for(unsigned bits=0;bits<32768;++bits)
 {
  LLViewerObject child,parent;parent.avatar=true;parent.id={4};child.parent=&parent;
  child.attached=bits&1;child.hud=bits&2;child.owned=bits&4;child.avatar=!child.attached&&(bits&8);
  rlv=bits&16;far=bits&32;muted=bits&64;selection.empty=bits&128;selection.roots=(bits>>8)%3;gSavedSettings.pie=bits&1024;renderModeBits=bits%8;
  LLToolPie tool;tool.mPick.object=(bits&2048)?nullptr:&child;tool.mPick.mPickType=(bits&4096)?LLPickInfo::PICK_LAND:0;
  tool.mPick.mObjectID={(bits&8192)?1:3};tool.mPick.mParticleOwnerID={(bits&16384)?2:0};
  const bool initialFocus=(bits&256)!=0;gFocusMgr.keyboard=initialFocus;trace.clear();
  const bool expected=tool.handleRightClickPick();const auto gold=trace;const bool focus=gFocusMgr.keyboard;
  gFocusMgr.keyboard=initialFocus;trace.clear();ReferenceContextSelectionPeer peer{[&]{tool.showVisualContextMenuEffect();},tool};
  const bool actual=vs_show_object_context_menu(peer,tool.mPick);
  if(expected!=actual||gold!=trace||focus!=gFocusMgr.keyboard)
  {
   std::cerr<<"Context mismatch "<<bits<<"\n";for(auto& row:gold)std::cerr<<"G "<<row<<"\n";for(auto& row:trace)std::cerr<<"N "<<row<<"\n";return 1;
  }
 }
}
