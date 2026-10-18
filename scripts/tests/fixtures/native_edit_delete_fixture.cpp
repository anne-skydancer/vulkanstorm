#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using S32=int;using U64=unsigned long long;
struct LLSD{int option=0;};
struct LLNotification {
 using Callback=std::function<bool(const LLSD&,const LLSD&)>;
 struct Params{std::string label;struct Functor{Callback callback;template<class F>void function(F fn){callback=fn;}}functor;explicit Params(std::string s):label(std::move(s)){};void name(const char*s){label=s;}};
 static int getSelectedOption(const LLSD&,const LLSD& response){return response.option;}
};
std::vector<std::string> events;LLNotification::Callback pending;
struct LLNotifications{static LLNotifications&instance(){static LLNotifications value;return value;}void add(const LLNotification::Params&p){events.push_back(p.label);pending=p.functor.callback;}void forceResponse(const LLNotification::Params&p,int option){events.push_back("force");LLSD response;response.option=option;p.functor.callback({},response);}};
void make_ui_sound(const char* name){events.push_back(name);}
struct Tools{bool visible=true;bool getVisible(){return visible;}}tools;Tools*gFloaterTools=&tools;
struct rlv_handler_t{static bool enabled;static bool isEnabled(){return enabled;}};bool rlv_handler_t::enabled=true;
bool allowed=true;bool rlvCanDeleteOrReturn(){return allowed;}
struct Object {bool attachment=false,move=true,copy=true,owner=true;bool isAttachment(){return attachment;}bool permMove(){return move;}bool permCopy(){return copy;}bool permYouOwner(){return owner;}};using LLViewerObject=Object;
struct Node{Object object;Object*getObject(){return &object;}struct Metadata{bool transient=false;}metadata;};
struct Model{std::vector<Node>nodes;auto begin(){return nodes.begin();}auto end(){return nodes.end();}};
// Controller iterators expose the actual retained node pointers.
struct View{Model*model=nullptr;struct It{std::vector<Node>::iterator i;Node*operator*(){return &*i;}void operator++(int){++i;}It&operator++(){++i;return*this;}bool operator!=(It b){return i!=b.i;}};It begin(){return{model->nodes.begin()};}It end(){return{model->nodes.end()};}};
struct LLObjectSelection{using iterator=View::It;};
int _1=1,_2=2;namespace boost {template<class F,class H>auto bind(F f,int,int,H handle){return[f,handle](const LLSD&a,const LLSD&b){return f(a,b,handle);};}}
struct ReferenceManager{View view;View*getSelection(){return &view;}void selectDelete();void deselectAll(){events.push_back("deselect");}void convertTransient(){events.push_back("convert");}static bool confirmDelete(const LLSD&,const LLSD&response,View*){if(!response.option)events.push_back("confirmed");return false;}};
REFERENCE
SHARED
struct VSObjectSelection;struct VSNativeSession{VSObjectSelection*selection=nullptr;std::shared_ptr<bool>live=std::make_shared<bool>(true);std::function<bool()>accountGuard(){auto l=live;return[l]{return*l;};}VSObjectSelection*existingObjectSelection(){return selection;}};
struct State{bool retired=false;U64 revision=10;std::weak_ptr<VSNativeSession>session;std::vector<Node>nodes;};
struct VSObjectSelection{std::shared_ptr<State>mImpl=std::make_shared<State>();Model model;std::function<void()>onConvert;void selectDelete();std::shared_ptr<const void>retainSelection(){return mImpl;}void deleteConfirmed(){events.push_back("confirmed");}void deselectAll(){events.push_back("deselect");}void convertTransient(){events.push_back("convert");bool change=false;for(auto&n:mImpl->nodes)if(n.metadata.transient){change=true;n.metadata.transient=false;}if(change)++mImpl->revision;if(onConvert)onConvert();}};
VSObjectSelection*active=nullptr;
struct VSEditSelection:View{VSEditSelection(){model=&active->model;}std::function<bool()>guard(){return[]{return true;};}};
struct VSEditDeleteManager{VSEditSelection view;VSObjectSelection&source;std::function<void()>beforeConvert;VSEditSelection*getSelection(){return &view;}bool canDeleteOrReturn(){return allowed;}void deselectAll(){source.deselectAll();}void convertTransient(){if(beforeConvert)beforeConvert();source.convertTransient();}};
NATIVE
int main(){
 for(int bits=0;bits<256;++bits){
  ReferenceManager reference;Model model;model.nodes.resize(2);model.nodes[0].object.attachment=bits&1;model.nodes[1].object.attachment=bits&2;model.nodes[0].object.move=bits&4;model.nodes[1].object.copy=bits&8;model.nodes[0].object.owner=bits&16;allowed=bits&32;tools.visible=bits&64;rlv_handler_t::enabled=bits&128;reference.view.model=&model;
  events.clear();reference.selectDelete();auto expected=events;
  VSObjectSelection native;active=&native;native.model=model;auto owner=std::make_shared<VSNativeSession>();owner->selection=&native;native.mImpl->session=owner;
  events.clear();pending={};native.selectDelete();assert(events==expected);
 }
 for(int scenario=0;scenario<7;++scenario){
  VSObjectSelection native;active=&native;native.model.nodes.resize(1);native.model.nodes[0].object.move=false;native.mImpl->nodes.resize(1);native.mImpl->nodes[0].metadata.transient=true;
  auto owner=std::make_shared<VSNativeSession>();owner->selection=&native;native.mImpl->session=owner;allowed=true;rlv_handler_t::enabled=true;
  if(scenario==5)native.onConvert=[&]{++native.mImpl->revision;};
  pending={};events.clear();native.selectDelete();assert(pending);auto callback=pending;
  if(scenario==1)++native.mImpl->revision;
  if(scenario==2)*owner->live=false;
  if(scenario==3)native.mImpl->retired=true;
  if(scenario==4){native.mImpl->session.reset();owner.reset();}
  LLSD response;response.option=scenario==6?1:0;
  callback({},response);callback({},response);assert(std::count(events.begin(),events.end(),"confirmed")==((scenario==0)?1:0));
 }
}
