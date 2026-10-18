#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <type_traits>
using S32=int;using LLUUID=int;
struct LLSD {int option=0;std::map<std::string,std::string> values;std::string& operator[](const char* key){return values[key];}};
std::string llformat(const char* text,int n){char result[64];snprintf(result,64,text,n);return result;}
struct Settings{bool confirm=true;int minimum=3;bool getBOOL(const char*)const{return confirm;}int getS32(const char*)const{return minimum;}}gSavedSettings;
struct Limits{int maximum=3;int getMaxLinkedPrims()const{return maximum;}}limits;Limits& vs_region_limits(){return limits;}
std::vector<std::string> events;
std::function<void(const LLSD&,const LLSD&)> pending;
namespace LLNotificationsUtil {
void add(const char* name){events.push_back(name);}
void add(const char* name,const LLSD& args){events.push_back(name);for(auto& p:args.values)events.push_back(p.first+":"+p.second);}
template<class F>void add(const char* name,const LLSD&,const LLSD&,F callback){events.push_back(name);if constexpr(std::is_invocable_v<F,const LLSD&,const LLSD&>)pending=callback;}
void add(const char* name,const LLSD&,const LLSD&,std::function<void(const LLSD&,const LLSD&)> callback){events.push_back(name);pending=std::move(callback);}
int getSelectedOption(const LLSD&,const LLSD& response){return response.option;}
}
struct Model {
 bool valid=true,modify=true,nonpermanent=true,owner=true,sameRegion=true;int count=4,roots=2;
 Model* getSelection(){return this;}int getObjectCount(){return count;}int getRootObjectCount(){return roots;}
 bool selectGetAllRootsValid(){events.push_back("valid");return valid;}
 bool selectGetRootsModify(){events.push_back("modify");return modify;}
 bool selectGetRootsNonPermanentEnforced(){events.push_back("permanent");return nonpermanent;}
 bool selectGetOwner(LLUUID&,std::string&){events.push_back("owner");return owner;}
 bool selectGetSameRegion(){events.push_back("region");return sameRegion;}
 void sendLink(){events.push_back("link");}void sendDelink(){events.push_back("delink");}
};
struct ReferenceManager:Model {
 static ReferenceManager* current;static ReferenceManager* getInstance(){return current;}
 Model* mSelectedObjects=this;bool linkObjects();bool unlinkObjects();
};ReferenceManager* ReferenceManager::current=nullptr;
REFERENCE
SHARED
namespace VSNativeSessionStub{struct Session;}
using VSNativeSession=VSNativeSessionStub::Session;
struct Selection;
namespace VSNativeSessionStub {struct Session{Selection* source=nullptr;bool live=true;Selection* existingObjectSelection(){return source;}std::function<bool()> accountGuard(){return [this]{return live;};}};}
struct Impl{std::weak_ptr<VSNativeSession> session;};
struct Selection {
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();bool current=true;int delinks=0;Model model;
 std::function<bool()> selectionGuard(){return [this]{return current;};}
 std::shared_ptr<const void> retainSelection(){return mImpl;}
 void sendDelink(){++delinks;}bool unlinkObjects();
};
struct VSLinkManager:Model {
 struct View{Selection* source=nullptr;std::function<bool()> guard(){return source->selectionGuard();}}selection;
 Selection& source;
 VSLinkManager(View,Selection& owner):selection{&owner},source(owner){static_cast<Model&>(*this)=owner.model;}
 void sendDelink(){source.sendDelink();}
};
UNLINK
int main(){
 for(int bits=0;bits<256;++bits)for(int count=0;count<8;++count){
  ReferenceManager ref;ref.valid=bits&1;ref.modify=bits&2;ref.nonpermanent=bits&4;ref.owner=bits&8;ref.sameRegion=bits&16;ref.count=count;ref.roots=(bits>>5)%4;
  ReferenceManager::current=&ref;Model native=ref;events.clear();bool a=ref.linkObjects();auto expected=events;events.clear();bool b=vs_link_objects(native);assert(a==b&&events==expected);
  gSavedSettings.confirm=bits&1;gSavedSettings.minimum=bits%5;
  events.clear();a=ref.unlinkObjects();expected=events;events.clear();b=vs_unlink_objects(native,[]{events.push_back("ConfirmUnlink");});assert(a==b&&events==expected);
 }
 for(int scenario=0;scenario<5;++scenario){
  Selection selection;auto owner=std::make_shared<VSNativeSession>();owner->source=&selection;selection.mImpl->session=owner;
  gSavedSettings.confirm=true;gSavedSettings.minimum=2;selection.model.count=4;selection.model.roots=1;
  pending={};assert(selection.unlinkObjects());assert(pending);auto callback=pending;LLSD notification,response;response.option=scenario==4?1:0;
  if(scenario==1)selection.current=false;
  if(scenario==2)owner->live=false;
  if(scenario==3){selection.mImpl->session.reset();owner.reset();}
  callback(notification,response);callback(notification,response);assert(selection.delinks==(scenario==0?1:0));
 }
}
