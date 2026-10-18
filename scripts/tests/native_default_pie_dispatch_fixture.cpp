#include <cassert>
#include <memory>
#include <cmath>
using S32=int;using MASK=unsigned;
constexpr MASK MASK_ALT=1;constexpr int VX=0,VY=1;
enum EMouseClickType{CLICK_LEFT,CLICK_DOUBLELEFT,CLICK_RIGHT,CLICK_MIDDLE};
struct LLCoordGL{int mX=0,mY=0;};
int ll_round(float value){return int(std::round(value));}
struct UUID{int id=0;bool isNull()const{return id==0;}};
struct LLHUDIcon{bool error=true;UUID id{7};bool getScriptError(){return error;}UUID getSourceID(){return id;}}icon;
struct LLFloaterScriptDebug{static inline int opens=0;static void show(UUID id){assert(!id.isNull());++opens;}};
ICON_HELPER
struct Handler
{
 bool consumes=false;
 void screenPointToLocal(int x,int y,int* a,int* b){*a=x;*b=y;}
 bool handleAnyMouseClick(int,int,MASK,EMouseClickType,bool){return consumes;}
};
struct Focus{Handler* capture=nullptr;Handler* getMouseCapture(){return capture;}void setKeyboardFocus(void*){}}gFocusMgr;
struct LLPickInfo{LLPickInfo()=default;template<class...Args>LLPickInfo(Args&&...){}};
struct LLToolPie:Handler
{
 static inline bool exists=true;static LLToolPie& instance(){static LLToolPie p;return p;}static LLToolPie* getInstance(){return &instance();}static bool instanceExists(){return exists;}
 LLPickInfo pick;LLPickInfo& getPick(){return pick;}
 int downs=0,ups=0,updates=0,manualMedia=0;bool mediaCapture=false;
 bool hasNativeMediaCapture(){return mediaCapture;}void retireNativeMediaCapture(){mediaCapture=false;}
 void updateNativeMediaCapture(){++updates;}
 bool handleMouseDown(int,int,MASK){++downs;show_script_error_icon(&icon);return false;}
 bool handleMouseUp(int,int,MASK){++ups;return false;}
 bool handleNativeMediaClick(auto&,bool=false){++manualMedia;return false;}
};
bool actualPieSelected=true;
LLToolPie* nativeMediaTool(){return actualPieSelected?LLToolPie::getInstance():nullptr;}
struct LLToolPipette:Handler{static bool instanceExists(){return false;}static LLToolPipette& instance(){static LLToolPipette p;return p;}bool nativeActive(){return false;}};
struct LLFloaterCamera{static bool inFreeCameraMode(){return false;}};
struct VSNativeSession
{
 enum class Phase{Connected,Disconnected};bool connected=true;static inline bool exists=true;
 static std::shared_ptr<VSNativeSession> active(){static auto s=std::make_shared<VSNativeSession>();return exists?s:nullptr;}
 Phase phase(){return connected?Phase::Connected:Phase::Disconnected;}void mouseButton(bool){}void releaseMovement(){}
};
struct Agent{bool grabbed=false;bool leftButtonGrabbed(){return grabbed;}}gAgent;
struct ViewerInput{int globals=0;void handleGlobalBindsMouse(EMouseClickType,MASK,bool){++globals;}}gViewerInput;

struct LLViewerWindow
{
 Handler root;Handler* mRootView=&root;struct Scale{float mV[2]{1,1};}mDisplayScale;
 LLCoordGL mCurrentMousePoint;bool mLeftMouseDown=false,mRightMouseDown=false,mMiddleMouseDown=false,mNativeCameraDrag=false;
 LLPickInfo mLastPick;int picks=0;void nativePick(LLPickInfo&){++picks;}
 bool nativeMouse(LLCoordGL,MASK,EMouseClickType,bool);
};
NATIVE_MOUSE
int main()
{
 LLViewerWindow w;auto& pie=LLToolPie::instance();
 // Real default controller handles the icon; manual media interception must
 // not execute the same click a second time before this controller.
 assert(w.nativeMouse({9,8},0,CLICK_LEFT,true));
 assert(pie.downs==1&&pie.updates==1&&pie.manualMedia==0&&w.picks==0&&LLFloaterScriptDebug::opens==1);
 assert(w.nativeMouse({9,8},0,CLICK_LEFT,false));assert(pie.ups==1);
 icon.error=false;w.nativeMouse({9,8},0,CLICK_LEFT,true);assert(pie.downs==2&&LLFloaterScriptDebug::opens==1);
 icon.error=true;icon.id={};w.nativeMouse({9,8},0,CLICK_LEFT,true);assert(pie.downs==3&&LLFloaterScriptDebug::opens==1);
 w.root.consumes=true;w.nativeMouse({9,8},0,CLICK_LEFT,true);assert(pie.downs==3);w.root.consumes=false;
 // Capture keeps its paired release; no new world/default-tool click.
 pie.mediaCapture=true;gFocusMgr.capture=&pie;
 w.nativeMouse({9,8},0,CLICK_LEFT,false);assert(!pie.mediaCapture&&pie.ups==1);gFocusMgr.capture=nullptr;
 // Actual non-Pie owner remains distinct; no gToolNull/Pie alias.
 actualPieSelected=false;w.nativeMouse({9,8},0,CLICK_LEFT,true);assert(pie.downs==3&&w.picks==1);
 actualPieSelected=true;VSNativeSession::active()->connected=false;w.nativeMouse({9,8},0,CLICK_LEFT,true);assert(pie.downs==3);
 assert(gViewerInput.globals==8);
}
