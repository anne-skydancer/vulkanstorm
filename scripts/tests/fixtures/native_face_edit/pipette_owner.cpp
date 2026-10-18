#include <cassert>
#include <memory>
#include <functional>
#include <string>
#include <utility>
#define VS_NATIVE_VULKAN 1
struct LLView;
template<class T>struct LLHandle{T*ptr=nullptr;std::weak_ptr<bool>alive;LLHandle()=default;LLHandle(T*p,std::shared_ptr<bool>a):ptr(p),alive(a){}bool isDead()const{auto p=alive.lock();return !p||!*p;}T*get()const{return isDead()?nullptr:ptr;}};
struct LLView{std::shared_ptr<bool>alive=std::make_shared<bool>(true);bool visible=true;virtual~LLView(){*alive=false;}bool isInVisibleChain()const{return visible;}LLHandle<LLView>getHandle(){return{this,alive};}};
struct LLTool{bool captured=false;int releases=0;virtual~LLTool()=default;virtual void handleDeselect(){}virtual void onMouseCaptureLost(){}bool hasMouseCapture()const{return captured;}void setMouseCapture(bool value){if(!value&&captured){captured=false;++releases;onMouseCaptureLost();}else captured=value;}};
struct LLToolMgr{LLTool base;LLTool*selected=&base;bool ready=true;std::function<void()>onSet;static LLToolMgr&instance(){static LLToolMgr value;return value;}static LLToolMgr*getInstance(){return&instance();}LLTool*nativeCurrentTool(){return ready?selected:nullptr;}LLTool*getCurrentTool(){return selected;}bool nativeToolsReady()const{return ready;}void setTransientTool(LLTool*tool){auto*old=selected;selected=tool;if(old!=tool)old->handleDeselect();if(onSet)onSet();}void clearTransientTool(){auto*old=selected;selected=&base;if(old!=&base)old->handleDeselect();}};
struct Selection{int clears=0;void clearHighlights(){++clears;}};
struct VSNativeSession:std::enable_shared_from_this<VSNativeSession>{static inline std::shared_ptr<VSNativeSession>live;static auto active(){return live;}bool alive=true;Selection selection;bool regionReady()const{return alive;}auto*existingObjectSelection(){return &selection;}std::function<bool()>accountGuard(){return[weak=weak_from_this()]{auto p=weak.lock();return p&&p->alive;};}};
struct Window{bool native=true;bool isNativeVulkan()const{return native;}}window;auto*gViewerWindow=&window;
struct LLToolPipette:LLTool{LLHandle<LLView>mNativeConsumer;std::function<bool()>mNativeOperation,mNativeAccount;bool mNativeHighlightOwned=false,mSuccess=false;std::string mTooltipMsg;bool nativeActive()const;bool nativeConsumer(const LLView*)const;bool ownsNativeConsumer(const LLView*v)const{return mNativeConsumer.get()==v;}void setNativeActive(bool,LLView*consumer=nullptr,std::function<bool()>operation={});void handleDeselect()override;void onMouseCaptureLost()override;static LLToolPipette*getInstance(){static LLToolPipette value;return &value;}static LLToolPipette&instance(){return*getInstance();}};
struct LLTextureEntry{struct Color{float mV[4]{.2f,.3f,.4f,1};}color;const auto&getColor()const{return color;}};constexpr int VRED=0,VGREEN=1,VBLUE=2;
struct LLFloaterColorPicker:LLView{int changes=0;void selectCurRgb(float,float,float){++changes;}void onColorSelect(const LLTextureEntry&);void stopUsingPipette();};
ACTIVE
CONSUMER
SET
DESELECT
LOST
COLOR
STOP
int main(){auto owner=std::make_shared<VSNativeSession>();VSNativeSession::live=owner;auto&tool=LLToolPipette::instance();auto&tools=LLToolMgr::instance();LLFloaterColorPicker first,second;bool origin=true;
 tool.setNativeActive(true,&first,[&]{return origin;});assert(tool.nativeConsumer(&first)&&!tool.nativeConsumer(&second)&&tools.selected==&tool);
 LLTextureEntry sample;first.onColorSelect(sample);second.onColorSelect(sample);assert(first.changes==1&&second.changes==0);
 tool.setMouseCapture(true);origin=false;assert(!tool.nativeActive());first.stopUsingPipette();assert(tools.selected==&tools.base&&!tool.hasMouseCapture());
 origin=true;tool.setNativeActive(true,&first,[&]{return origin;});owner->alive=false;auto next=std::make_shared<VSNativeSession>();VSNativeSession::live=next;tool.setNativeActive(true,&second);assert(tool.nativeConsumer(&second));auto*current=tools.selected;auto releases=tool.releases;first.stopUsingPipette();assert(tools.selected==current&&tool.releases==releases);first.onColorSelect(sample);second.onColorSelect(sample);assert(first.changes==1&&second.changes==1);
 tool.setMouseCapture(true);tool.onMouseCaptureLost();assert(tools.selected==&tools.base&&!tool.nativeActive()&&!tool.hasMouseCapture());
 first.visible=false;tool.setNativeActive(true,&first);assert(!tool.nativeActive()&&tools.selected==&tools.base);
 tools.ready=false;tool.setNativeActive(true,&second);assert(!tool.nativeActive());tools.ready=true;
 tools.onSet=[&]{tools.onSet={};next->alive=false;};tool.setNativeActive(true,&second);assert(!tool.nativeActive()&&tools.selected==&tools.base);
 // Ordinary GL callback body is retained; native initiating-view checks do not change it.
 window.native=false;first.onColorSelect(sample);assert(first.changes==2);
}
