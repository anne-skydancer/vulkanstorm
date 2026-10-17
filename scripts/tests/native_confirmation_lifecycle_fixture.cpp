#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <memory>
using S32=int;namespace boost{using std::bind;}using namespace std::placeholders;
struct LLSD{std::string text;int option=0;std::map<std::string,LLSD>values;LLSD&operator[](const char*name){return values[name];}const LLSD&operator[](const char*name)const{return values.at(name);}LLSD&operator=(const std::string&value){text=value;return *this;}std::string asString()const{return text;}};
HEADER
struct LLNotificationsUtil{
 using Callback=std::function<bool(const LLSD&,const LLSD&)>;
 static inline std::vector<Callback>pending;
 static S32 getSelectedOption(const LLSD&,const LLSD&response){return response.option;}
 static void add(const char*,const LLSD&,const LLSD&,Callback callback){pending.push_back(std::move(callback));}
};
SOURCE
int alive=0,confirmed=0;std::string password;
struct Listener:LLConfirmationManager::ListenerBase{std::function<void()>inside;Listener(){++alive;}~Listener(){--alive;}void confirmed(const std::string&value)override{++::confirmed;password=value;if(inside)inside();}};
int main(){
 using M=LLConfirmationManager;LLSD response,notification;
 M::confirm(M::TYPE_NONE,"action",new Listener);assert(!alive&&confirmed==1&&password.empty());
 for(auto kind:{M::TYPE_CLICK,M::TYPE_PASSWORD}){
  const int before=confirmed;
  M::confirm(kind,"action",new Listener);assert(alive==1);LLNotificationsUtil::pending.clear();assert(!alive&&confirmed==before);
  M::confirm(kind,"action",new Listener);response.option=1;response["message"]="ignored";LLNotificationsUtil::pending.back()(notification,response);assert(!alive&&confirmed==before);LLNotificationsUtil::pending.clear();
  auto*listener=new Listener;M::confirm(kind,"action",listener);response.option=0;response["message"]="secret";
  listener->inside=[&]{LLNotificationsUtil::pending.back()(notification,response);};
  LLNotificationsUtil::pending.back()(notification,response);assert(!alive);
  const int count=confirmed;LLNotificationsUtil::pending.back()(notification,response);assert(confirmed==count);
  assert(password==(kind==M::TYPE_PASSWORD?"secret":""));LLNotificationsUtil::pending.clear();
 }
 assert(confirmed==3&&!alive);
 M::confirm(M::TYPE_CLICK,"action",nullptr);assert(LLNotificationsUtil::pending.empty());
}
