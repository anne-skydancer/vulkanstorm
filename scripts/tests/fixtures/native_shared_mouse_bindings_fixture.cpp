#include <cassert>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
using S32=int;using MASK=unsigned;
struct LLWindow{};struct LLCoordGL{};
enum EMouseClickType{CLICK_LEFT,CLICK_DOUBLELEFT,CLICK_MIDDLE,CLICK_RIGHT,CLICK_BUTTON4,CLICK_BUTTON5,CLICK_COUNT,CLICK_NONE};
enum EMouseState{MOUSE_STATE_SILENT,MOUSE_STATE_DOWN,MOUSE_STATE_CLICK,MOUSE_STATE_LEVEL,MOUSE_STATE_UP};
enum EKeystate{KEYSTATE_DOWN,KEYSTATE_UP,KEYSTATE_LEVEL};
struct Window{bool native=false,handled=false,tool=false;bool isNativeVulkan(){return native;}bool handleAnyMouseClick(LLWindow*,LLCoordGL,MASK,EMouseClickType,bool,bool& isTool){isTool=tool;return handled;}}window,*gViewerWindow=&window;
struct InputState{EMouseState mMouseLevel[CLICK_COUNT]{};};
struct ReferenceInput:InputState{bool handleMouse(LLWindow*,LLCoordGL,MASK,EMouseClickType,bool);};
REFERENCE
struct LLMouseBinding{EMouseClickType mMouse;MASK mMask;std::function<bool(EKeystate)>mFunction;};
struct Keyboard{MASK mask=0;MASK currentMask(bool){return mask;}}keyboard,*gKeyboard=&keyboard;
struct Agent{bool grabbed=false;bool leftButtonGrabbed(){return grabbed;}}gAgent;constexpr int MODE_FIRST_PERSON=1;
std::vector<EKeystate>defaults;void agent_control_lbutton_handle(EKeystate state){defaults.push_back(state);}
struct LLViewerInput:InputState{int mode=0;std::vector<LLMouseBinding>mMouseBindings[2];bool mLMouseDefaultHandling[2]{true,true};int getMode()const{return mode;}bool handleMouse(LLWindow*,LLCoordGL,MASK,EMouseClickType,bool);bool scanMouse(const std::vector<LLMouseBinding>&,S32,EMouseClickType,MASK,EMouseState,bool)const;bool scanMouse(EMouseClickType,EMouseState)const;void scanMouse();};
METHODS
int main(){ReferenceInput reference;LLViewerInput native;for(unsigned seed=0;seed<4096;++seed){window.handled=(seed*13)%11<4;window.tool=(seed*7)%5==0;const auto click=EMouseClickType((seed*37)%CLICK_COUNT);const bool down=(seed*13)%3;window.native=false;const bool old=reference.handleMouse(nullptr,{},seed%8,click,down);window.native=true;const bool actual=native.handleMouse(nullptr,{},seed%8,click,down);assert(old==actual);assert(std::equal(reference.mMouseLevel,reference.mMouseLevel+CLICK_COUNT,native.mMouseLevel));}
 LLViewerInput input;std::vector<EKeystate>calls;input.mMouseBindings[0]={{CLICK_BUTTON4,2,[&](auto state){calls.push_back(state);return true;}}};keyboard.mask=2;input.mMouseLevel[CLICK_BUTTON4]=MOUSE_STATE_DOWN;input.scanMouse();assert(input.mMouseLevel[CLICK_BUTTON4]==MOUSE_STATE_LEVEL);input.scanMouse();input.mMouseLevel[CLICK_BUTTON4]=MOUSE_STATE_UP;input.scanMouse();assert((calls==std::vector<EKeystate>{KEYSTATE_DOWN,KEYSTATE_LEVEL,KEYSTATE_UP}));assert(input.mMouseLevel[CLICK_BUTTON4]==MOUSE_STATE_SILENT);
 calls.clear();input.mMouseLevel[CLICK_BUTTON4]=MOUSE_STATE_CLICK;input.scanMouse();assert((calls==std::vector<EKeystate>{KEYSTATE_DOWN,KEYSTATE_UP}));assert(input.mMouseLevel[CLICK_BUTTON4]==MOUSE_STATE_SILENT);
 defaults.clear();input.mMouseLevel[CLICK_LEFT]=MOUSE_STATE_CLICK;input.scanMouse();assert((defaults==std::vector<EKeystate>{KEYSTATE_DOWN,KEYSTATE_UP}));
 input.mode=MODE_FIRST_PERSON;gAgent.grabbed=true;defaults.clear();input.mMouseLevel[CLICK_LEFT]=MOUSE_STATE_CLICK;input.scanMouse();assert(defaults.empty());
}
