// Real console controller/HTTP callbacks with controlled account/view/circuit ports.
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
using U64=unsigned long long;
struct LLSD{enum Kind{Undefined,String,Map}kind=Undefined;std::string text;std::map<std::string,LLSD>values;LLSD()=default;LLSD(const std::string&s):kind(String),text(s){}bool isString()const{return kind==String;}bool isMap()const{return kind==Map;}std::string asString()const{return text;}bool has(const std::string&k)const{return values.count(k);}LLSD&operator[](const std::string&k){kind=Map;return values[k];}const LLSD&operator[](const std::string&k)const{static LLSD empty;auto i=values.find(k);return i==values.end()?empty:i->second;}};
std::ostringstream sink;std::ostream&operator<<(std::ostream&o,const LLSD&d){return o<<d.asString();}
#define LL_WARNS(x) sink
#define LL_INFOS() sink
#define LL_ENDL ""
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_APP
struct LLHost{int value=1;LLHost()=default;LLHost(int v):value(v){}LLHost(std::string s):value(s.empty()?0:std::stoi(s)){}bool isOk()const{return value!=0;}bool operator==(const LLHost&o)const{return value==o.value;}bool operator<(const LLHost&o)const{return value<o.value;}};
int regionEpoch=1,accountEpoch=1;
struct Region{int epoch=regionEpoch;bool current()const{return epoch==regionEpoch;}bool operator==(const Region&o)const{return epoch==o.epoch;}};
std::optional<Region>vs_current_region_context(){return Region{};}
std::function<bool()>vs_native_im_guard(){int e=accountEpoch;return[e]{return e==accountEpoch;};}
template<class Callback>auto vs_native_im_lifetime_callback(std::function<bool()>current,Callback callback){return[current,callback](LLSD d){if(current())callback(d);};}
struct LLView{struct State{LLView*p=nullptr;};std::shared_ptr<State>state=std::make_shared<State>();LLView(){state->p=this;}virtual~LLView(){state->p=nullptr;}struct Handle{std::weak_ptr<State>p;LLView*get()const{auto s=p.lock();return s?s->p:nullptr;}};Handle getHandle()const{return{state};}};
struct LLUICtrl:LLView{};struct LLFloater:LLView{LLFloater(const LLSD&){};};struct LLLineEditor:LLUICtrl{std::string text;std::string getText(){return text;}void clear(){text.clear();}};
struct LLTextEditor{std::vector<std::string>lines;void appendText(const std::string&s,bool){lines.push_back(s);}};
struct Connection{std::shared_ptr<bool>alive;void disconnect(){if(alive)*alive=false;}};struct Signal{std::vector<std::pair<std::shared_ptr<bool>,std::function<void(const std::string&)>>>slots;Connection connect(std::function<void(const std::string&)>cb){auto alive=std::make_shared<bool>(true);slots.push_back({alive,cb});return{alive};}void operator()(const std::string&s){for(auto&slot:slots)if(*slot.first)slot.second(s);}}sConsoleReplySignal;
struct Placeholder{};Placeholder _1;namespace boost{template<class T>auto bind(void(T::*m)(LLSD),T*p,Placeholder){return[p,m](LLSD d){(p->*m)(d);};}}
struct Window{bool native=true;bool isNativeVulkan()const{return native;}}window,*gViewerWindow=&window;
struct Agent{std::string async="http://sim/async",old="";std::string getRegionCapability(const std::string&name){return name=="SimConsoleAsync"?async:old;}}gAgent;
struct HttpRequest{std::string url;LLSD body;std::function<void(LLSD)>success,error;};std::vector<HttpRequest>http;
namespace LLCoreHttpUtil{struct HttpCoroutineAdapter{static inline const std::string HTTP_RESULTS_CONTENT="content";static void callbackHttpPost(const std::string&u,LLSD d,std::function<void(LLSD)>ok,std::function<void(LLSD)>fail){http.push_back({u,d,ok,fail});}};}
const std::string PROMPT="\n\n> ",UNABLE_TO_SEND_COMMAND="ERROR: The last command was not received by the server.",CONSOLE_UNAVAILABLE="ERROR: No console available for this region/simulator.";
struct LLFloaterRegionDebugConsole:LLFloater{
LLFloaterRegionDebugConsole(const LLSD&);~LLFloaterRegionDebugConsole();LLTextEditor*mOutput;std::function<bool()>mAsyncCurrent;U64 mRequestEpoch=0;Connection mReplySignalConnection;
std::function<bool()>requestGuard()const;void onClose(bool);void onInput(LLUICtrl*,const LLSD&);void onReplyReceived(const std::string&);void onConsoleSuccess(LLSD);void onConsoleError(LLSD);void onAsyncConsoleError(LLSD);
};
const std::set<std::string>transport={"PacketAck"},session={"ChatFromSimulator"},worldMessages={"ObjectUpdate"};
struct VSNativeSession{enum class Phase{Login,Connecting,Connected,Logout,Disconnected};LLHost mHost{1};Phase mPhase=Phase::Connected;bool mChangingRegion=false,mSeedReady=true,mHandshake=true,mCircuitAck=true;unsigned mRejected=0;struct Peer{bool acknowledged=false,handshake=false;};std::map<LLHost,Peer>mCircuits;static std::shared_ptr<VSNativeSession>active();bool admit(const std::string&,const LLHost&);
READY
};std::shared_ptr<VSNativeSession>owner=std::make_shared<VSNativeSession>();std::shared_ptr<VSNativeSession>VSNativeSession::active(){return owner;}
struct LLHTTPNode{using ResponsePtr=int;};struct ConsoleResponseNode{NODE};
struct LLEventPollImpl{int dispatched=0;std::string name;LLSD body;void handleMessage(const LLSD&);void handleMessage(const std::string&n,const LLSD&b){++dispatched;name=n;body=b;}};
METHODS
int main(){
LLTextEditor output;auto ui=std::make_unique<LLFloaterRegionDebugConsole>(LLSD{});ui->mOutput=&output;LLLineEditor input;input.text="help";ui->onInput(&input,{});assert(http.size()==1&&http[0].url==gAgent.async&&http[0].body.asString()=="help"&&input.text.empty());
sConsoleReplySignal("first");assert(output.lines.back()=="first"+PROMPT);auto oldFailure=http[0].error;ui->onClose(false);auto n=output.lines.size();oldFailure({});sConsoleReplySignal("closed");assert(output.lines.size()==n);
input.text="new";ui->onInput(&input,{});sConsoleReplySignal("new-result");assert(output.lines.back()=="new-result"+PROMPT);auto retired=http.back().error;++accountEpoch;retired({});assert(output.lines.back()=="new-result"+PROMPT);++regionEpoch;sConsoleReplySignal("prior-region");assert(output.lines.back()=="new-result"+PROMPT);
input.text="new-region";ui->onInput(&input,{});auto afterDelete=http.back().error;ui.reset();n=output.lines.size();afterDelete({});sConsoleReplySignal("retired");assert(output.lines.size()==n);
// The actual deprecated HTTP response path also retains its request owner.
window.native=false;gAgent.async="";gAgent.old="http://sim/old";auto sync=std::make_unique<LLFloaterRegionDebugConsole>(LLSD{});sync->mOutput=&output;input.text="status";sync->onInput(&input,{});auto a=http.back().success;input.text="stats";sync->onInput(&input,{});auto b=http.back().success;a(LLSD("a"));b(LLSD("b"));assert(output.lines[output.lines.size()-2]=="a"+PROMPT&&output.lines.back()=="b"+PROMPT);sync->onClose(false);n=output.lines.size();a(LLSD("late"));assert(output.lines.size()==n);window.native=true;
// Exact admitted wire body: a scalar reply is legitimate for this node only.
LLEventPollImpl poll;LLSD event;event["message"]=LLSD("SimConsoleResponse");event["body"]=LLSD("reply");poll.handleMessage(event);assert(poll.dispatched==1&&poll.body.asString()=="reply");event["message"]=LLSD("ParcelProperties");poll.handleMessage(event);assert(poll.dispatched==1);event["body"]=LLSD{};event["body"]["field"]=LLSD("map");poll.handleMessage(event);assert(poll.dispatched==2);
assert(owner->admit("SimConsoleResponse",{1}));assert(!owner->admit("SimConsoleResponse",{2}));owner->mChangingRegion=true;assert(!owner->admit("SimConsoleResponse",{1}));owner->mChangingRegion=false;owner->mPhase=VSNativeSession::Phase::Logout;assert(!owner->admit("SimConsoleResponse",{1}));owner->mPhase=VSNativeSession::Phase::Connected;
// Native HTTP node refuses forged/unknown simulator replies before broadcast.
int broadcast=0;auto c=sConsoleReplySignal.connect([&](const std::string&){++broadcast;});ConsoleResponseNode node;LLSD message;message["body"]=LLSD("reply");message["sender"]=LLSD("2");node.post(0,{},message);assert(broadcast==0);message["sender"]=LLSD("1");node.post(0,{},message);assert(broadcast==1);message["body"]=LLSD{};message["body"]["invalid"]=LLSD("map");node.post(0,{},message);assert(broadcast==1);c.disconnect();
}
