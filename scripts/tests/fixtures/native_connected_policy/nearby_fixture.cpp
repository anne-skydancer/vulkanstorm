#include <cassert>
#include <vector>
struct LLSD{bool v;bool asBoolean()const{return v;}};
struct Console{bool visible=false;void setVisible(bool v){visible=v;}};Console*gConsole;
namespace LLNotificationsUI{constexpr int NEARBY_CHAT_CHANNEL_UUID=32;struct LLScreenChannelBase{std::vector<int>toasts{1,2};unsigned clears=0;void removeToastsFromChannel(){toasts.clear();++clears;}};struct LLChannelManager{bool found=true;LLScreenChannelBase channel;inline static bool exists=true;inline static unsigned gets=0;static bool instanceExists(){return exists;}static LLChannelManager*getInstance(){++gets;static LLChannelManager m;return&m;}LLScreenChannelBase*findChannelByID(int id){assert(id==NEARBY_CHAT_CHANNEL_UUID);return found?&channel:nullptr;}};}
struct FSFloaterNearbyChat{inline static bool FSUseNearbyChatConsole=false;static void removeScreenChat();static void updateFSUseNearbyChatConsole(const LLSD&);};
REMOVE
UPDATE
int main(){Console console;gConsole=&console;auto*manager=LLNotificationsUI::LLChannelManager::getInstance();FSFloaterNearbyChat::updateFSUseNearbyChatConsole({true});assert(console.visible&&manager->channel.toasts.empty()&&manager->channel.clears==1);manager->channel.toasts={3};FSFloaterNearbyChat::updateFSUseNearbyChatConsole({false});assert(!console.visible&&manager->channel.toasts.size()==1&&manager->channel.clears==1);gConsole=nullptr;FSFloaterNearbyChat::updateFSUseNearbyChatConsole({true});assert(manager->channel.clears==2);LLNotificationsUI::LLChannelManager::exists=false;auto gets=LLNotificationsUI::LLChannelManager::gets;FSFloaterNearbyChat::updateFSUseNearbyChatConsole({true});assert(LLNotificationsUI::LLChannelManager::gets==gets);}
