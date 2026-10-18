#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
using U64=unsigned long long;
bool native=true,account=true,regionCurrent=true,authority=true;int regionID=1;
struct LLSD{};
struct LLViewerWindow{bool isNativeVulkan()const{return native;}}window;LLViewerWindow*gViewerWindow=&window;
struct VSRegionContext{int id;bool current()const{return regionCurrent&&id==regionID;}bool canManageEstate()const{return authority;}bool operator==(const VSRegionContext&o)const{return id==o.id;}};
std::optional<VSRegionContext>vs_current_region_context(){if(!regionCurrent)return {};return VSRegionContext{regionID};}
std::function<bool()>vs_native_im_guard(){const bool captured=account;return[captured]{return captured&&account;};}
template<class F>auto vs_native_im_account_callback(F f){const auto current=vs_native_im_guard();return[current,f]{if(current())f();};}
struct Base{virtual~Base()=default;};
struct Handle{Base*p;Base*get()const{return p;}};
struct Connection{std::shared_ptr<bool>live=std::make_shared<bool>(false);bool connected()const{return*live;}void disconnect(){*live=false;}};
struct Session{std::vector<std::pair<Connection,std::function<void()>>>slots;Connection observeCurrentRegion(std::function<void()>f){Connection c;*c.live=true;slots.push_back({c,f});return c;}void change(){for(auto&[c,f]:slots)if(c.connected())f();}}session;
struct VSNativeSession{static Session*active(){return &session;}};
struct Button{bool enabled=false;void setEnabled(bool b){enabled=b;}};
struct LLFloaterTopObjects:Base{
 U64 mReplyEpoch=0;Connection mNativeRegionConnection;std::function<bool()>mReplyCurrent;Button refresh;int clears=0;
 Handle getHandle(){return{this};}Button*getChildView(const char*){return&refresh;}void clearList(){++clears;}
 std::function<bool()>requestGuard(bool modify=false);bool modelCurrent()const;void retireReply();void onClose(bool);void beginReply();
};
METHODS
int main(){
 LLFloaterTopObjects view;
 assert(!view.modelCurrent());view.beginReply();assert(view.modelCurrent());auto name=view.requestGuard();auto modify=view.requestGuard(true);assert(name()&&modify());
 authority=false;assert(name()&&!modify());authority=true;
 view.beginReply();assert(view.modelCurrent()&&!name()&&!modify());auto old=view.requestGuard();
 view.onClose(false);assert(!view.modelCurrent()&&!old()&&view.clears==1&&view.refresh.enabled);assert(!view.mNativeRegionConnection.connected());
 view.beginReply();assert(view.modelCurrent());auto crossing=view.requestGuard();++regionID;session.change();assert(!view.modelCurrent()&&!crossing()&&view.clears==2);assert(view.refresh.enabled);
 view.beginReply();assert(view.modelCurrent());auto retired=view.requestGuard();account=false;assert(!view.modelCurrent()&&!retired());
 account=true;view.beginReply();assert(view.modelCurrent());regionCurrent=false;assert(!view.modelCurrent());regionCurrent=true;
 native=false;view.mReplyCurrent={};assert(view.modelCurrent());auto epoch=view.mReplyEpoch;view.onClose(false);assert(view.mReplyEpoch==epoch); // preserve GL lifetime policy
}
