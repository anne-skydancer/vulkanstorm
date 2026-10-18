#include <cassert>
#include <functional>
#include <algorithm>
#include <sstream>
#include <stdexcept>
using VX_t=int;const int VX=0;
#define LL_WARNS(x) std::ostringstream()
#define LL_ENDL ""
const int UI_CURSOR_ARROW=0,UI_CURSOR_WAIT=1;bool live=true;auto vs_native_im_guard(){return []{return live;};}
struct LLImageRaw {bool invalid=false;bool isBufferInvalid(){return invalid;}};
struct VSVulkanContext {enum class SnapshotLayer {Color};int calls=0,width=0,height=0,fail=0,expire=0;bool throwError=false;bool snapshot(LLImageRaw&,int w,int h,float dpi,bool ui,bool hud,SnapshotLayer layer,std::function<void()>){assert(!ui&&!hud&&dpi==1.5f&&layer==SnapshotLayer::Color);width=w;height=h;++calls;if(throwError)throw std::runtime_error("device lost");if(calls==expire)live=false;return calls!=fail;}};
struct LLPipeline {enum {RENDER_DEBUG_FEATURE_UI=1};static inline bool sShowHUDAttachments=true;static void toggleRenderDebugFeature(int);};struct Pipeline {bool ui=true;bool hasRenderDebugFeatureMask(int){return ui;}}gPipeline;void LLPipeline::toggleRenderDebugFeature(int){gPipeline.ui=!gPipeline.ui;}
struct LLViewerWindow {VSVulkanContext context;int cursor=0;auto* nativeContext(){return &context;}struct Scale {float mV[2]={1.5f,1.5f};};Scale getDisplayScale(){return{};}void setCursor(int v){cursor=v;}};
SNAPSHOT
int main(){LLViewerWindow w;LLImageRaw image;assert(vs_native_simple_snapshot(w,image,128,128,3));assert(w.context.calls==3&&w.context.width==128&&w.context.height==128&&gPipeline.ui&&LLPipeline::sShowHUDAttachments&&w.cursor==UI_CURSOR_ARROW);
 w.context={};w.context.fail=2;assert(!vs_native_simple_snapshot(w,image,64,64,2));assert(gPipeline.ui&&LLPipeline::sShowHUDAttachments);w.context={};w.context.throwError=true;assert(!vs_native_simple_snapshot(w,image,64,64,1)&&w.cursor==UI_CURSOR_ARROW);
 w.context={};w.context.expire=1;assert(!vs_native_simple_snapshot(w,image,64,64,3)&&w.context.calls==1);live=true;w.context={};assert(vs_native_simple_snapshot(w,image,64,64,0)&&w.context.calls==1);assert(!vs_native_simple_snapshot(w,image,0,64,1));}
