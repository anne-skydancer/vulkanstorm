#include <cassert>
#include <vector>
#include <string>
#define VS_NATIVE_VULKAN 1
using KEY=int;using MASK=int;constexpr KEY KEY_ALT=18;
std::vector<std::string> events;
struct LLWindow{};
struct LLTool{bool captured=false;virtual~LLTool()=default;virtual void handleSelect(){events.push_back("select");}virtual void handleDeselect(){events.push_back("deselect");}};
struct LLToolCompInspect:LLTool{static inline bool exists=true;static LLToolCompInspect&instance(){static LLToolCompInspect i;return i;}static bool instanceExists(){return exists;}static LLToolCompInspect*getInstance(){return &instance();}void keyUp(KEY,MASK){events.push_back("inspect-up");}};
struct LLApp{static inline bool exiting=false;static bool isExiting(){return exiting;}};
struct LLToolMgr{static inline bool exists=true;bool ready=true;LLTool*mSelectedTool=LLToolCompInspect::getInstance();static bool instanceExists(){return exists;}static LLToolMgr&instance(){static LLToolMgr m;return m;}static LLToolMgr*getInstance(){return &instance();}bool nativeToolsReady(){return ready;}LLTool*nativeCurrentTool(){return mSelectedTool;}LLTool*getCurrentTool(){return mSelectedTool;}void updateToolStatus(){events.push_back("tool-status");}void onAppFocusLost();void onAppFocusGained();};
struct Focus{void setAppHasFocus(bool yes){events.push_back(yes?"focus":"blur");}void setMouseCapture(void*){events.push_back("release-capture");}}gFocusMgr;
struct Menus{void resetMenuTrigger(){events.push_back("menu-reset");}};Menus*gMenuBarView=nullptr,*gLoginMenuBarView=nullptr;
struct Timer{void pause(){events.push_back("pause");}void unpause(){events.push_back("unpause");}}gForegroundTime;
struct Keyboard{void resetMaskKeys(){events.push_back("mask-reset");}void resetKeyDownAndHandle(){events.push_back("key-release");}void resetKeys(){events.push_back("key-reset");}}keyboard;Keyboard*gKeyboard=&keyboard;
struct Agent{void onAppFocusGained(){events.push_back("agent-focus");}}gAgent;
struct Input{void resetNativeGlobalKeys(){events.push_back("global-reset");}void handleGlobalBindsKeyUp(KEY,MASK){events.push_back("global-up");}bool handleKeyUp(KEY,MASK){events.push_back("shared-up");return true;}}gViewerInput;
struct LLModalDialog{static void onAppFocusGained(){events.push_back("modal-focus");}};
struct LLToolPipette{static bool instanceExists(){return false;}static LLToolPipette&instance(){static LLToolPipette p;return p;}void setNativeActive(bool){}};
struct VSNativeSession{static VSNativeSession*active(){return nullptr;}void releaseMovement(){events.push_back("movement-release");}};
struct Window{void setMouseClipping(bool){events.push_back("unclip");}};
struct LLViewerWindow{bool mNativeVulkan=true,mNativeCameraDrag=true;Window w;void showCursor(){events.push_back("cursor");}Window*getWindow(){return &w;}void handleFocus(LLWindow*);void handleFocusLost(LLWindow*);bool handleTranslatedKeyUp(KEY,MASK);};
METHODS
int main(){LLViewerWindow view;auto&tools=LLToolMgr::instance();
 events.clear();view.handleFocus(nullptr);assert((events==std::vector<std::string>{"focus","modal-focus","agent-focus","select","tool-status","mask-reset","unpause"}));
 events.clear();view.handleFocusLost(nullptr);assert((events==std::vector<std::string>{"blur","deselect","tool-status","pause","global-reset","release-capture","cursor","unclip","key-release","key-reset"}));assert(!view.mNativeCameraDrag);
 events.clear();assert(view.handleTranslatedKeyUp(KEY_ALT,0));assert((events==std::vector<std::string>{"global-up","inspect-up","shared-up"}));
 for(unsigned mode=0;mode<3;++mode){tools.ready=mode!=0;LLToolMgr::exists=mode!=1;LLToolCompInspect::exists=mode!=2;events.clear();assert(view.handleTranslatedKeyUp(KEY_ALT,0));assert((events==std::vector<std::string>{"global-up","shared-up"}));}
 tools.ready=false;LLToolMgr::exists=true;LLToolCompInspect::exists=true;events.clear();view.handleFocus(nullptr);assert((events==std::vector<std::string>{"focus","modal-focus","agent-focus","mask-reset","unpause"}));
 tools.ready=true;view.mNativeVulkan=false;events.clear();view.handleFocus(nullptr);assert((events==std::vector<std::string>{"focus","modal-focus","agent-focus","select","tool-status","mask-reset","unpause"}));
 events.clear();assert(view.handleTranslatedKeyUp(KEY_ALT,0));assert((events==std::vector<std::string>{"global-up","inspect-up","shared-up"}));
 LLApp::exiting=true;events.clear();tools.onAppFocusLost();assert(events.empty());
}
