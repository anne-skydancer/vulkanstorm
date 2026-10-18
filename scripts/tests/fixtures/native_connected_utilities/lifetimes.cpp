#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <ctime>
struct LLSD{};int account=1;std::function<bool()>vs_native_im_guard(){auto generation=account;return[generation]{return generation>0&&generation==account;};}
struct LLFloater{virtual~LLFloater()=default;std::shared_ptr<LLFloater*>life=std::make_shared<LLFloater*>(this);std::weak_ptr<LLFloater*>getHandle(){return life;}};
template<class T>struct LLHandle{std::weak_ptr<T*>weak;LLHandle(std::weak_ptr<T*>w):weak(w){}T*get()const{auto p=weak.lock();return p?*p:nullptr;}};
struct Signal{struct Slot{bool live=true;std::function<void(time_t,const std::string&)>callback;};struct Connection{std::shared_ptr<Slot>slot;Connection()=default;Connection(std::shared_ptr<Slot>s):slot(s){}Connection(Connection&&)=default;Connection&operator=(Connection&&o){if(slot)slot->live=false;slot=std::move(o.slot);return*this;}~Connection(){if(slot)slot->live=false;}};std::vector<std::shared_ptr<Slot>>slots;Connection connect(std::function<void(time_t,const std::string&)>cb){auto p=std::make_shared<Slot>();p->callback=cb;slots.push_back(p);return{p};}void emit(){for(auto&p:slots)if(p->live)p->callback(1,"current");}};
struct OmnifilterEngine{Signal mLogSignal;std::string mNeedlesXMLPath;int reads=0;void init(){mNeedlesXMLPath="user/omnifilter.xml";++reads;}void ensureInitialized();static OmnifilterEngine&instance(){static OmnifilterEngine i;return i;}};
struct Omnifilter:LLFloater{Signal::Connection mLogConnection;int lines=0;void onLogLine(time_t,const std::string&){++lines;}void connectLog();};
struct ALFloaterRegionTracker:LLFloater{int edits=0;void onRegionAddedCallback(const LLSD&,const LLSD&){++edits;}std::function<void(const LLSD&,const LLSD&)>regionAddedCallback();};
INIT
LOG
TRACKER
int main(){auto&e=OmnifilterEngine::instance();e.ensureInitialized();assert(e.reads==1);e.ensureInitialized();assert(e.reads==1);std::function<void(const LLSD&,const LLSD&)>old;{Omnifilter o;o.connectLog();e.mLogSignal.emit();assert(o.lines==1);o.connectLog();e.mLogSignal.emit();assert(o.lines==2);ALFloaterRegionTracker t;old=t.regionAddedCallback();old({},{});assert(t.edits==1);account=2;e.mLogSignal.emit();old({},{});assert(o.lines==2&&t.edits==1);}account=3;old({},{});e.mLogSignal.emit();for(auto&s:e.mLogSignal.slots)assert(!s->live);}
