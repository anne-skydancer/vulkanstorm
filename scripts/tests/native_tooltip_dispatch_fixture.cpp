#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
using S32=int;using MASK=int;
std::vector<std::string> calls;bool debug=false;
struct Settings{}gSavedSettings;
template<class T>struct LLCachedControl{LLCachedControl(Settings&,const char*,int){}operator bool()const{return debug;}};
struct LLRect{void intersectWith(const LLRect&){}bool pointInRect(int,int)const{return true;}};
struct LLToolTip{struct Params{struct Style{Style&add(){return *this;}void text(const char*){}}styled_message;LLRect sticky_rect;int max_width=0;int callback=0;};};
struct LLMouseHandler{std::string name;bool tips=false;LLMouseHandler(std::string n):name(n){}virtual~LLMouseHandler()=default;void screenPointToLocal(int x,int y,int*a,int*b){*a=x-2;*b=y-3;}virtual bool handleToolTip(int,int,MASK){calls.push_back(name);return tips;}};
struct LLView:LLMouseHandler{using LLMouseHandler::LLMouseHandler;
 struct Iter{LLView*p;bool end;LLView*operator*(){return p;}Iter&operator++(){end=true;return *this;}bool operator!=(const Iter&o){return end!=o.end;}void skipDescendants(){}};
 using tree_iterator_t=Iter;
 Iter beginTreeDFS(){return {this,false};}Iter endTreeDFS(){return {this,true};}
 bool getVisible(){return true;}LLRect getLocalRect(){return {};}void localRectToScreen(LLRect,LLRect*){}LLRect calcScreenRect(){return {};}bool hasAncestor(LLView*){return false;}};
struct LLUICtrl:LLView{using LLView::LLView;};struct LLFloater:LLUICtrl{using LLUICtrl::LLUICtrl;};
LLView *gMenuHolder=nullptr,*gFloaterView=nullptr,*gConsole=nullptr;
void append_xui_tooltip(LLView*,LLToolTip::Params&){calls.push_back("debug");}
struct LLTool:LLMouseHandler{using LLMouseHandler::LLMouseHandler;};
LLTool nativeTool("native"),legacyTool("legacy");
struct LLToolMgr{static LLToolMgr&instance(){static LLToolMgr m;return m;}static LLToolMgr*getInstance(){return &instance();}static bool instanceExists(){return true;}LLTool*nativeCurrentTool(){calls.push_back("native-query");return &nativeTool;}LLTool*getCurrentTool(){calls.push_back("legacy-query");return &legacyTool;}};
struct LLToolTipMgr{static LLToolTipMgr&instance(){static LLToolTipMgr m;return m;}void show(const LLToolTip::Params&){calls.push_back("show");}};
struct Window{bool hidden=false;bool isCursorHidden(){return hidden;}};
struct LLViewerWindow{bool mNativeVulkan=true;bool mMouseInWindow=true;struct{int mX=17,mY=23;}mCurrentMousePoint;Window window;Window*mWindow=&window;LLView*mRootView;void updateToolTip(bool,LLUICtrl*,LLMouseHandler*,MASK);};
METHOD
int main(){
 LLView root("root");LLUICtrl top("top");LLMouseHandler captor("captor");LLViewerWindow v;v.mRootView=&root;
 for(bool native:{false,true}){
 v.mNativeVulkan=native;std::string query=native?"native-query":"legacy-query",tool=native?"native":"legacy";
 calls.clear();v.updateToolTip(false,&top,&captor,0);assert(calls.empty());
 v.window.hidden=true;v.updateToolTip(true,&top,&captor,0);assert(calls.empty());v.window.hidden=false;
 v.updateToolTip(true,&top,&captor,0);assert((calls==std::vector<std::string>{"captor"}));
 calls.clear();top.tips=true;v.updateToolTip(true,&top,nullptr,0);assert((calls==std::vector<std::string>{"top",query}));
 calls.clear();top.tips=false;root.tips=true;v.updateToolTip(true,&top,nullptr,0);assert((calls==std::vector<std::string>{"top","root",query}));
 calls.clear();root.tips=false;v.updateToolTip(true,&top,nullptr,0);assert((calls==std::vector<std::string>{"top","root",query,tool}));
 calls.clear();v.updateToolTip(true,nullptr,nullptr,0);assert((calls==std::vector<std::string>{"root",query,tool}));
 calls.clear();debug=true;v.updateToolTip(true,&top,&captor,0);assert((calls==std::vector<std::string>{"debug","show"}));debug=false;
 }
}
