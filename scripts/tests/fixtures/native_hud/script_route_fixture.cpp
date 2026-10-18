#include <cassert>
#include <map>
#include <string>
#include <vector>
struct Settings{std::map<std::string,int>v;bool getBOOL(const char*k){return v[k];}int getS32(const char*k){return v[k];}}gSavedSettings,gSavedPerAccountSettings;
template<class T>struct LLCachedControl{Settings&s;const char*k;LLCachedControl(Settings&settings,const char*key):s(settings),k(key){}operator T()const{return s.v[k];}};
constexpr int CHAT_TYPE_DEBUG_MSG=8,CHAT_TYPE_OWNER=9;int gAgentID=42;
struct LLChat{int mChatType=0,mOwnerID=42,mFromID=0;};
struct LLFloater{static bool quit;static bool isQuitRequested(){return quit;}};bool LLFloater::quit=false;
struct LLGridManager{bool sl=true;static LLGridManager*getInstance(){static LLGridManager g;return &g;}bool isInSecondLife(){return sl;}};
std::vector<int> delivered;
struct LLFloaterScriptDebug{static bool routeChat(const LLChat&);static void addScriptLine(const LLChat&message){delivered.push_back(message.mFromID);}};
ROUTE
int main(){for(unsigned bits=0;bits<256;++bits)for(int type:{0,CHAT_TYPE_DEBUG_MSG,CHAT_TYPE_OWNER}){gSavedPerAccountSettings.v["FSllOwnerSayToScriptDebugWindow"]=bits&1;LLFloater::quit=bits&2;gSavedSettings.v["ShowScriptErrors"]=bits&4;gSavedSettings.v["ShowScriptErrorsLocation"]=(bits&8)?1:0;const bool owned=bits&16;LLGridManager::getInstance()->sl=bits&32;LLChat message{type,owned?42:99,1234};delivered.clear();const bool selected=type==CHAT_TYPE_DEBUG_MSG||(type==CHAT_TYPE_OWNER&&(bits&1));bool permit=selected&&!(bits&2)&&(!(type==CHAT_TYPE_DEBUG_MSG)||bool(bits&4));
#ifdef OPENSIM
 permit=permit&&(!(bits&32)||owned);
#else
 permit=permit&&owned;
#endif
 const bool consume=selected&&(!permit||bool(bits&8)||type==CHAT_TYPE_OWNER);assert(LLFloaterScriptDebug::routeChat(message)==consume);assert(delivered.size()==unsigned(permit));if(permit)assert(delivered[0]==1234);}}
