#include <cassert>
#include <map>
#include <string>
struct Settings{std::map<std::string,bool>v;}gSavedSettings;
template<class T>struct LLCachedControl{std::string name;LLCachedControl(Settings&,const char*n):name(n){}operator T()const{return gSavedSettings.v[name];}};
struct LLChat{int mSourceType=0;};struct LLSD{};constexpr int CHAT_SOURCE_AGENT=1;void*gConsole=nullptr;
struct Agent{bool busy=false;bool isDoNotDisturb(){return busy;}}gAgent;
struct FSConsoleUtils{static bool ProcessChatMessage(const LLChat&,const LLSD&);};
METHOD
int main(){int console;for(int mask=0;mask<64;++mask){gConsole=(mask&1)?&console:nullptr;gSavedSettings.v["FSUseNearbyChatConsole"]=mask&2;gSavedSettings.v["UseChatBubbles"]=mask&4;gSavedSettings.v["FSBubblesHideConsoleAndToasts"]=mask&8;gAgent.busy=mask&16;LLChat chat;chat.mSourceType=(mask&32)?CHAT_SOURCE_AGENT:0;bool expected=(mask&1)&&(mask&2)&&((mask&16)||((mask&4)&&(mask&8)&&(mask&32)));assert(FSConsoleUtils::ProcessChatMessage(chat,{})==expected);}}
