#include <cassert>
#include <functional>
#include <memory>
#include <vector>
#define VS_NATIVE_VULKAN 1
using S32=int;using MASK=int;
struct LLView{};
template<class T>struct LLHandle{std::shared_ptr<bool> alive;bool isDead()const{return !alive||!*alive;}};
struct Root{std::shared_ptr<bool> alive=std::make_shared<bool>(true);LLHandle<LLView> getHandle(){return {alive};}};
struct VSNativeSession{static std::shared_ptr<VSNativeSession> live;int epoch=1;static auto active(){return live;}int account(){return epoch;}bool current(int e){return live.get()==this&&epoch==e;}};
std::shared_ptr<VSNativeSession> VSNativeSession::live;
struct Window{bool minimized=false;int delay=0;bool getMinimized(){return minimized;}void delayInputProcessing(){++delay;}};
struct LLPickInfo{std::function<bool()>mNativeRequestCurrent;void(*mPickCallback)(const LLPickInfo&)=nullptr;};
struct LLViewerWindow{bool mNativeVulkan=true;Root*mRootView;Window*mWindow;std::vector<LLPickInfo>mPicks;void schedulePick(LLPickInfo&);};
SCHEDULE
int calls=0;void done(const LLPickInfo&){++calls;}
int main(){Root root;Window os;LLViewerWindow w{true,&root,&os};auto a=std::make_shared<VSNativeSession>();VSNativeSession::live=a;
 bool operation=true;LLPickInfo p{[&]{return operation;},done};w.schedulePick(p);assert(w.mPicks.size()==1&&os.delay==1&&p.mNativeRequestCurrent());operation=false;assert(!p.mNativeRequestCurrent());
 LLPickInfo stale{[]{return false;},done};w.schedulePick(stale);assert(w.mPicks.size()==1);os.minimized=true;w.schedulePick(stale);assert(calls==0);
 operation=true;LLPickInfo current{[&]{return operation;},done};w.schedulePick(current);assert(calls==1);++a->epoch;assert(!current.mNativeRequestCurrent());
 VSNativeSession::live=std::make_shared<VSNativeSession>();assert(!p.mNativeRequestCurrent());
 VSNativeSession::live.reset();LLPickInfo login{[]{return true;},done};w.schedulePick(login);assert(login.mNativeRequestCurrent());*root.alive=false;assert(!login.mNativeRequestCurrent());
 *root.alive=true;VSNativeSession::live=std::make_shared<VSNativeSession>();assert(!login.mNativeRequestCurrent());
}
