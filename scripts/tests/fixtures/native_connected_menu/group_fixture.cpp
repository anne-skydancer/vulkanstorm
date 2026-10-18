#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <cassert>
using U64=unsigned long long;
struct LLUUID{int id=0;bool isNull()const{return id==0;}bool operator==(const LLUUID&)const=default;};
struct LLSD{std::string value;LLSD()=default;LLSD(const char*s):value(s){}LLSD(const std::string&s):value(s){}};
struct LLUICtrl{bool visible=false;std::string text;void setValue(const LLSD&s){text=s.value;}void setVisible(bool v){visible=v;}};
struct LLStartUp{static int getStartupState(){return 3;}};constexpr int STATE_STARTED=3;
struct LLGroupMgrGroupData{bool isGroupPropertiesDataComplete(){return false;}};struct LLGroupMgr{std::vector<int>requested;static LLGroupMgr*getInstance(){static LLGroupMgr m;return&m;}LLGroupMgrGroupData*getGroupData(LLUUID){return nullptr;}void sendGroupPropertiesRequest(LLUUID id){requested.push_back(id.id);}};
using Reply=std::function<void(const LLUUID&,const std::string&,bool)>;struct Cache{std::vector<Reply>replies;int getGroup(LLUUID,Reply f){replies.push_back(f);return 0;}}cache;Cache*gCacheName=&cache;
struct Connection{void operator=(int){}};
struct LLGroupActions{inline static std::vector<int>actions;static void join(LLUUID id){actions.push_back(id.id);}};
unsigned epoch=1;
struct LLInspectGroup:std::enable_shared_from_this<LLInspectGroup>{LLUUID mGroupID;U64 mRequestSerial=1;std::function<bool()>mAccountCurrent;Connection mNameConnection;LLUICtrl ctrl;std::function<void()>close;unsigned updates=0;template<class T>struct Handle{std::weak_ptr<LLInspectGroup>weak;LLInspectGroup*get()const{auto p=weak.lock();return p.get();}};template<class T>Handle<T>getDerivedHandle(){return{weak_from_this()};}template<class T>T*getChild(const char*){return&ctrl;}void closeFloater(){if(close)close();}void processGroupData(){++updates;}void requestUpdate();void nameUpdatedCallback(const LLUUID&,const std::string&,bool);void onClickJoin();};
REQUEST
NAME
JOIN
int main(){auto owner=std::make_shared<LLInspectGroup>();auto saved=epoch;owner->mAccountCurrent=[saved]{return saved==epoch;};owner->mGroupID={31};owner->requestUpdate();assert(LLGroupMgr::getInstance()->requested==std::vector<int>({31}));auto first=cache.replies.back();owner->mGroupID={32};++owner->mRequestSerial;owner->requestUpdate();first({31},"Old group",true);assert(owner->ctrl.text.empty());auto current=cache.replies.back();current({32},"Current",true);assert(owner->ctrl.text=="<nolink>Current</nolink>");++epoch;current({32},"Retired",true);assert(owner->ctrl.text=="<nolink>Current</nolink>");owner.reset();current({32},"Destroyed",true);owner=std::make_shared<LLInspectGroup>();saved=epoch;owner->mAccountCurrent=[saved]{return saved==epoch;};owner->mGroupID={81};owner->close=[&]{owner.reset();};auto*raw=owner.get();raw->onClickJoin();assert(LLGroupActions::actions==std::vector<int>({81}));owner=std::make_shared<LLInspectGroup>();saved=epoch;owner->mAccountCurrent=[saved]{return saved==epoch;};owner->mGroupID={91};owner->close=[] {++epoch;};owner->onClickJoin();assert(LLGroupActions::actions.size()==1);}
