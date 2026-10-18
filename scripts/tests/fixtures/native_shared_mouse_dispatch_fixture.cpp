
#include <cassert>
#include <memory>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include <utility>
#define VS_NATIVE_VULKAN 1
using F32=float;using S32=int;using MASK=unsigned;
constexpr int VX=0,VY=1,CAMERA_MODE_CUSTOMIZE_AVATAR=3;
enum EMouseClickType{CLICK_LEFT,CLICK_DOUBLELEFT,CLICK_RIGHT,CLICK_MIDDLE,CLICK_BUTTON4,CLICK_BUTTON5};
struct LLCoordGL{int mX=0,mY=0;LLCoordGL()=default;LLCoordGL(int x,int y):mX(x),mY(y){}};
int ll_round(float f){return int(std::round(f));}
struct NullStream{template<class T>NullStream&operator<<(const T&){return*this;}};
#define LL_INFOS(...) NullStream{}
#define LL_DEBUGS(...) NullStream{}
#define LL_ENDL 0
bool gDebugClicks=false,gDisconnected=false;
struct LLView{static inline bool sDebugMouseHandling=false;static inline std::string sMouseHandlerMessage;};
template<class T>struct LLHandle;
struct Handle{std::shared_ptr<bool>alive;bool isDead()const{return!alive||!*alive;}};
template<class T>struct LLHandle:Handle{};
struct Handler{bool consumes=false;std::function<void()>onClick;std::shared_ptr<bool>alive=std::make_shared<bool>(true);LLHandle<LLView> getHandle(){LLHandle<LLView> h;h.alive=alive;return h;}std::string getName(){return"handler";}void screenPointToLocal(int x,int y,int*a,int*b){*a=x;*b=y;}bool pointInView(int,int){return true;}virtual bool handleAnyMouseClick(int,int,MASK,EMouseClickType,bool){if(onClick)onClick();return consumes;}};
using LLMouseHandler=Handler;
struct Focus{Handler*capture=nullptr;Handler*getMouseCapture(){return capture;}void setKeyboardFocus(void*){}}gFocusMgr;
struct LLTool:Handler{bool clipMouseWhenDown(){return false;}};
struct UUID{int id=0;bool isNull()const{return id==0;}};
struct LLHUDIcon{bool error=true;UUID id{7};bool getScriptError(){return error;}UUID getSourceID(){return id;}}icon;
struct LLFloaterScriptDebug{static inline int opens=0;static void show(UUID id){assert(!id.isNull());++opens;}};
ICON_HELPER
struct LLToolPie:LLTool{int downs=0,ups=0,rightDowns=0,rightUps=0;bool mediaCapture=false;bool handleAnyMouseClick(int,int,MASK,EMouseClickType type,bool down)override{if(type==CLICK_LEFT){if(down){++downs;show_script_error_icon(&icon);return false;}if(mediaCapture){mediaCapture=false;return true;}++ups;}if(type==CLICK_RIGHT){if(down)++rightDowns;else++rightUps;}return false;}}pie;
struct LLToolMgr{bool ready=true;LLTool* tool=&pie;static LLToolMgr&instance(){static LLToolMgr value;return value;}static LLToolMgr*getInstance(){return &instance();}static bool instanceExists(){return true;}LLTool*nativeCurrentTool(){return ready?tool:nullptr;}LLTool*getCurrentTool(){return tool;}};
struct LLWindow{int captures=0,releases=0;void captureMouse(){++captures;}void releaseMouse(){++releases;}void setMouseClipping(bool){}};
struct LLUI{static LLUI*getInstance(){static LLUI ui;return&ui;}void resetMouseIdleTimer(){}};
struct Menu{void resetMenuTrigger(){}};Menu*gMenuBarView=nullptr;
struct LLPipeline{static constexpr int RENDER_DEBUG_FEATURE_UI=1;bool show=true;bool hasRenderDebugFeatureMask(int){return show;}}gPipeline;
struct LLViewerEventRecorder{static LLViewerEventRecorder&instance(){static LLViewerEventRecorder value;return value;}void setMouseGlobalCoords(int,int){}void logMouseEvent(std::string,std::string){}void setMouseLocalCoords(int,int){}std::string get_xui(){return{};}void clear_xui(){}};
struct ViewerInput{int globals=0;std::function<void()> onGlobal;void handleGlobalBindsMouse(EMouseClickType,MASK,bool){++globals;if(onGlobal)onGlobal();}}gViewerInput;
struct VSNativeSession{enum class Phase{Connected,Disconnected};bool live=true;static std::shared_ptr<VSNativeSession>active(){static auto value=std::make_shared<VSNativeSession>();return value;}Phase phase(){return live?Phase::Connected:Phase::Disconnected;}std::function<bool()>accountGuard(){return[this]{return live;};}};
struct LLViewerWindow;LLViewerWindow*gViewerWindow=nullptr;
struct LLViewerWindow{Handler root;Handler*mRootView=&root;LLWindow window;LLWindow*mWindow=&window;bool mNativeVulkan=true,mLeftMouseDown=false,mRightMouseDown=false,mMiddleMouseDown=false;std::vector<int>mPicks;std::function<void()>onPick;struct Scale{float mV[2]{1,1};}mDisplayScale;LLCoordGL mCurrentMousePoint;int fallbacks=0;void performPick(){mPicks.clear();if(onPick)onPick();}void handlePieMenu(int,int,MASK){++fallbacks;}bool nativeMouse(LLCoordGL,MASK,EMouseClickType,bool,bool*toolAction=nullptr);bool dispatchAnyMouseClick(LLWindow*,LLCoordGL,MASK,EMouseClickType,bool,bool&,const std::function<bool()>&);};
DISPATCH
NATIVE_MOUSE
int main(){LLViewerWindow window;gViewerWindow=&window;auto& tools=LLToolMgr::instance();
 // UI and actual mouse captor retain first refusal; Pie remains a real current owner.
 assert(!window.nativeMouse({9,8},0,CLICK_LEFT,true));assert(pie.downs==1&&LLFloaterScriptDebug::opens==1);assert(window.nativeMouse({9,8},0,CLICK_LEFT,false));assert(pie.ups==1);
 window.root.consumes=true;window.nativeMouse({},0,CLICK_LEFT,true);assert(pie.downs==1);window.root.consumes=false;
 pie.mediaCapture=true;gFocusMgr.capture=&pie;window.nativeMouse({},0,CLICK_LEFT,false);assert(!pie.mediaCapture&&pie.ups==1);gFocusMgr.capture=nullptr;
 LLTool other;other.consumes=true;tools.tool=&other;bool action=false;assert(window.nativeMouse({},0,CLICK_LEFT,true,&action)&&action);assert(pie.downs==1);
 // No synthetic Pie dispatch when no genuine tool is ready.
 tools.ready=false;assert(!window.nativeMouse({},0,CLICK_LEFT,true));assert(pie.downs==1);tools.ready=true;tools.tool=&pie;
 // Right-click fallback and OS release handling preserve original controller behavior.
 assert(window.nativeMouse({},0,CLICK_RIGHT,true));assert(window.fallbacks==1&&pie.rightDowns==1);assert(window.nativeMouse({},0,CLICK_RIGHT,false));assert(pie.rightUps==1);
 // Global voice bindings precede root existence and dispatch exactly once.
 const int before=gViewerInput.globals;window.mRootView=nullptr;assert(!window.nativeMouse({},0,CLICK_MIDDLE,true));assert(gViewerInput.globals==before+1);window.mRootView=&window.root;
 // Global binding reentry must not access a destroyed root or retarget tools.
 gViewerInput.onGlobal=[&]{*window.root.alive=false;gViewerWindow=nullptr;};const int previousDowns=pie.downs;assert(window.nativeMouse({},0,CLICK_LEFT,true));assert(pie.downs==previousDowns);gViewerInput.onGlobal={};*window.root.alive=true;gViewerWindow=&window;
 // Drained async picks or view callbacks cannot route the same click into a successor owner.
 window.mPicks={1};window.onPick=[&]{VSNativeSession::active()->live=false;};const int downs=pie.downs;assert(window.nativeMouse({},0,CLICK_LEFT,false));assert(pie.downs==downs);VSNativeSession::active()->live=true;window.onPick={};
 window.root.onClick=[&]{*window.root.alive=false;gViewerWindow=nullptr;};assert(window.nativeMouse({},0,CLICK_LEFT,true));assert(pie.downs==downs);
}
