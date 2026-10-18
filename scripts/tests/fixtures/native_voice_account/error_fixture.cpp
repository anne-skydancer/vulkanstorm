#include <cassert>
#include <string>
#include <map>
using LLUUID=std::string;
struct LLSD {
 std::string text;std::map<std::string,LLSD> values;
 LLSD& operator[](const std::string& key){return values[key];}
 const LLSD& operator[](const std::string& key)const{static LLSD empty;auto i=values.find(key);return i==values.end()?empty:i->second;}
 LLSD& operator=(const std::string& value){text=value;return *this;}
 bool has(const std::string& key)const{return values.contains(key);}
 std::string asUUID()const{return text;}
};
bool accountCurrent=true;
void vs_native_im_stamp_notification(LLSD& payload){payload["epoch"]="old-account";}
bool vs_native_im_notification_current(const LLSD& payload){return accountCurrent&&payload["epoch"].text=="old-account";}
struct LLIMModel {
 struct Session{std::string mErrorIdentity;};std::map<std::string,Session> sessions;
 static LLIMModel& instance(){static LLIMModel m;return m;}static LLIMModel* getInstance(){return &instance();}
 Session* findIMSession(const std::string& id){auto i=sessions.find(id);return i==sessions.end()?nullptr:&i->second;}
 std::string getName(const std::string& id){return id;}
};
struct LLFloater {int closed=0;void closeFloater(bool){++closed;}} floater;
struct FSFloaterIM {static LLFloater* findInstance(const std::string&){return &floater;}};
namespace LLTrans{std::string getString(const std::string& name){return name;}}
LLSD storedNotification;
struct LLIMMgr {
 bool hasSession(const LLUUID& id){return LLIMModel::instance().findIMSession(id)!=nullptr;}
 void showSessionForceClose(const std::string&,LLUUID);
 static bool onConfirmForceCloseError(const LLSD&,const LLSD&);
};
namespace LLNotificationsUtil {
 void add(const char*,const LLSD&,const LLSD& payload,bool(*)(const LLSD&,const LLSD&)){storedNotification["payload"]=payload;}
}
PRODUCER
RESPONDER
int main(){
 auto& model=LLIMModel::instance();LLIMMgr manager;
 model.sessions["wire-id"].mErrorIdentity="original-instance";
 manager.showSessionForceClose("test-error","wire-id");
 auto original=storedNotification;
 assert(original["payload"]["session_instance"].asUUID()=="original-instance");
 model.sessions.erase("wire-id");
 model.sessions["wire-id"].mErrorIdentity="new-instance";
 LLIMMgr::onConfirmForceCloseError(original,{});assert(floater.closed==0);
 // The original model may have remapped, but its obsolete wire-ID error
 // cannot close another active conversation or silently target a successor.
 model.sessions["new-wire-id"].mErrorIdentity="original-instance";
 LLIMMgr::onConfirmForceCloseError(original,{});assert(floater.closed==0);
 manager.showSessionForceClose("test-error","new-wire-id");
 accountCurrent=false;LLIMMgr::onConfirmForceCloseError(storedNotification,{});assert(floater.closed==0);
 accountCurrent=true;LLIMMgr::onConfirmForceCloseError(storedNotification,{});assert(floater.closed==1);
 model.sessions.erase("new-wire-id");LLIMMgr::onConfirmForceCloseError(storedNotification,{});assert(floater.closed==1);
}
