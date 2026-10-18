#include <cassert>
#include <map>
#include <string>
struct Settings{std::map<std::string,bool>v;bool getBOOL(const char*k){return v[k];}}gSavedSettings;
struct LLFloater{void*host=nullptr;void*lastHost=nullptr;bool visible=false;unsigned reparents=0;void*getHost(){return host;}void setHost(void*p){host=p;if(p)lastHost=p;}static bool isVisible(LLFloater*p){return p->visible;}};
struct FSFloaterContacts:LLFloater{static FSFloaterContacts*getInstance(){static FSFloaterContacts f;return&f;}};
struct FSFloaterNearbyChat:LLFloater{static FSFloaterNearbyChat*getInstance(){static FSFloaterNearbyChat f;return&f;}};
struct FloaterView{void addChild(LLFloater*p){++p->reparents;}}view;FloaterView*gFloaterView=&view;constexpr int IM_NOTHING_SPECIAL=0;
struct FSFloaterIMContainer{unsigned adds=0;void addFloater(LLFloater*p,bool,int){p->host=this;++adds;}void initTabs();};
METHOD
int main(){for(bool contactsTorn:{false,true})for(bool chatTorn:{false,true}){auto*chat=FSFloaterNearbyChat::getInstance();auto*contacts=FSFloaterContacts::getInstance();*chat={};*contacts={};gSavedSettings.v["ContactsTornOff"]=contactsTorn;gSavedSettings.v["ChatHistoryTornOff"]=chatTorn;FSFloaterIMContainer container;container.initTabs();assert(chat->lastHost==&container||chat->host==&container);assert(chat->host==(chatTorn?nullptr:&container));assert(chat->reparents==unsigned(chatTorn));assert(contacts->host==(contactsTorn?nullptr:&container));auto adds=container.adds;container.initTabs();assert(container.adds==adds);assert(gSavedSettings.v["ChatHistoryTornOff"]==chatTorn);}}
