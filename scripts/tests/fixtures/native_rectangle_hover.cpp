#include <cassert>
#include <functional>
#include <memory>
using S32=int;using MASK=int;
struct LLMouseHandler{virtual ~LLMouseHandler()=default;virtual void screenPointToLocal(int x,int y,int*a,int*b){*a=x;*b=y;}virtual bool handleHover(int,int,int){return false;}};
struct LLView:LLMouseHandler{std::shared_ptr<bool>alive=std::make_shared<bool>(true);struct Handle{std::shared_ptr<bool>alive;bool isDead()const{return !*alive;}};auto getHandle(){return Handle{alive};}bool hit=true,handled=false;int calls=0;std::function<void()>during;
 bool pointInView(int,int){return hit;}bool handleHover(int,int,int)override{++calls;if(during)during();return handled;}};
struct LLTool:LLMouseHandler{int calls=0;bool handled=true;std::function<void()>during;bool handleHover(int,int,int)override{++calls;if(during)during();return handled;}};
struct LLToolPie:LLTool{static bool instanceExists(){return false;}static LLToolPie*getInstance(){static LLToolPie p;return &p;}static auto&instance(){return *getInstance();}bool hasNativeMediaCapture(){return false;}};
struct LLToolMgr{LLTool*tool=nullptr;static bool instanceExists(){return true;}static auto&instance(){static LLToolMgr m;return m;}LLTool*nativeCurrentTool(){return tool;}};
struct Focus{LLMouseHandler*capture=nullptr;LLView*top=nullptr;auto getMouseCapture(){return capture;}auto getTopCtrl(){return top;}}gFocusMgr;
struct VSNativeSession:std::enable_shared_from_this<VSNativeSession>{enum class Phase{Connected};bool live=true;static inline std::shared_ptr<VSNativeSession>owner;static auto active(){return owner;}auto phase(){return Phase::Connected;}auto accountGuard(){return [s=shared_from_this()]{return s->live;};}};
constexpr int UI_CURSOR_ARROW=0;struct Window{void setCursor(int){}};
struct LLViewerWindow{LLView*mRootView=nullptr;Window window;Window*mWindow=&window;bool mMouseInWindow=true;struct Point{int mX=4,mY=5;}mCurrentMousePoint;int hoverUpdates=0,tooltips=0;
 void updateMouseHoverViews(int,int,int,bool){++hoverUpdates;}void updateToolTip(bool,LLView*,LLMouseHandler*,int){++tooltips;}void nativeHoverUpdate(MASK);};
LLViewerWindow*gViewerWindow=nullptr;
bool nativeMediaHover(LLViewerWindow&,LLViewerWindow::Point,MASK){return false;}
ACTUAL
int main(){auto account=std::make_shared<VSNativeSession>();VSNativeSession::owner=account;LLViewerWindow viewer;LLView root;LLTool tool;gViewerWindow=&viewer;viewer.mRootView=&root;LLToolMgr::instance().tool=&tool;
 viewer.nativeHoverUpdate(0);assert(tool.calls==1&&viewer.tooltips==1); // Actual tool, not a Pipette/media boolean shortcut.
 LLView top;top.handled=true;gFocusMgr.top=&top;viewer.nativeHoverUpdate(0);assert(top.calls==1&&tool.calls==1);gFocusMgr.top=nullptr;
 LLTool captor;gFocusMgr.capture=&captor;viewer.nativeHoverUpdate(0);assert(captor.calls==1&&tool.calls==1);gFocusMgr.capture=nullptr;
 root.during=[&]{*root.alive=false;};int tips=viewer.tooltips;viewer.nativeHoverUpdate(0);assert(tool.calls==1&&viewer.tooltips==tips);root.during={};*root.alive=true;
 tool.during=[&]{account->live=false;};viewer.nativeHoverUpdate(0);assert(tool.calls==2&&viewer.tooltips==tips);account->live=true;tool.during={};
 LLViewerWindow successor;tool.during=[&]{gViewerWindow=&successor;};viewer.nativeHoverUpdate(0);assert(viewer.tooltips==tips&&successor.tooltips==0);
}
