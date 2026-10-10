"""Execute the production friend-status callback without a native toast owner."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FriendStatusTest(unittest.TestCase):
    def test_friend_status_preserves_history_without_legacy_manager(self):
        source = (ROOT / 'indra/newview/llcallingcard.cpp').read_text()
        start = source.index('static void on_avatar_name_cache_notify(', source.index('void LLAvatarTracker::processNotify'))
        callback = source[start:source.index('void LLAvatarTracker::formFriendship', start)]
        fixture = r'''
#include <cassert>
#include <map>
#include <memory>
#include <string>
#define VS_NATIVE_VULKAN 1
struct LLUUID { std::string value; };
struct LLAvatarName {};
struct LLSD {
    template<class T> LLSD& operator=(const T&){return *this;}
    LLSD& operator[](const char*){return *this;}
    LLSD with(const char*,bool)const{return *this;}
};
struct Settings { std::map<std::string,bool> values;bool getBOOL(const char* n){return values[n];} } gSavedSettings,gSavedPerAccountSettings;
template<class T> struct LLCachedControl {
    Settings& group;std::string name;
    LLCachedControl(Settings& g,const char* n):group(g),name(n){}
    operator T()const{return group.values[name];}
};
struct VSNativeSession {enum class Phase{Connected,Disconnected};static bool native;static VSNativeSession* active(){static VSNativeSession s;return native?&s:nullptr;}Phase phase(){return Phase::Connected;}};
bool VSNativeSession::native=true;
struct VSPlainChat {int messages=0;void append(const std::string&){++messages;}}native_chat;
struct Viewer {VSPlainChat* nativeChat(){return &native_chat;}}viewer;
Viewer* gViewerWindow=&viewer;
struct LLLogChat {static int records;static void saveHistory(const char*,const std::string&,const LLUUID&,const std::string&){++records;}};
int LLLogChat::records=0;
namespace FSCommon {std::string getAvatarNameByDisplaySettings(const LLAvatarName&){return "Fixture Friend";}}
struct LLTrans {static std::string getString(const char* n){return n;}};
int sounds=0;
void make_ui_sound(const char*){++sounds;}
struct LLAvatarActions {static void startIM(const LLUUID&){};};
namespace boost {template<class...T> int bind(T...){return 0;}}
struct Notification {std::string getMessage(){return "Friend status";}};
using LLNotificationPtr=std::shared_ptr<Notification>;
struct LLNotifications {
    static LLNotifications& instance(){static LLNotifications n;return n;}
    template<class...T> LLNotificationPtr add(T...){return std::make_shared<Notification>();}
};
constexpr int IM_NOTHING_SPECIAL=0,CHAT_SOURCE_SYSTEM=1,CHAT_TYPE_RADAR=2;
struct LLIMMgr {static LLUUID computeSessionID(int,const LLUUID& id){return id;}};
struct LLIMModel {
    int notices=0;static LLIMModel& instance(){static LLIMModel m;return m;}
    void proccessOnlineOfflineNotification(const LLUUID&,const std::string&){++notices;}
};
struct LGGContactSets {
    bool notify=false;static LGGContactSets* getInstance(){static LGGContactSets c;return &c;}
    bool notifyForFriend(const LLUUID&){return notify;}
};
struct LLChat {std::string mText,mFromName;int mSourceType=0,mChatType=0;LLUUID mFromID;};
struct FSFloaterNearbyChat {int messages=0;void addMessage(const LLChat& c,bool history,const LLSD&){assert(history);assert(c.mFromName=="Fixture Friend");++messages;}}nearby;
struct LLFloaterReg {static bool available;template<class T>static T* getTypedInstance(const char*,const LLSD&){return available?&nearby:nullptr;}};
bool LLFloaterReg::available=true;
namespace LLNotificationsUI {
struct LLNotificationManager {
    int messages=0;static LLNotificationManager& instance(){assert(!VSNativeSession::native);static LLNotificationManager m;return m;}
    void onChat(const LLChat&,const LLSD&){++messages;}
};
}
struct FSKeywords {static FSKeywords* getInstance(){static FSKeywords k;return &k;}bool chatContainsKeyword(const LLChat&,bool){return false;}static void notify(const LLChat&){};};
''' + callback + r'''
int main(){
    LLUUID friend_id{"fixture"};LLAvatarName name;LLSD payload;
    gSavedSettings.values["OnlineOfflinetoNearbyChat"]=true;
    for(bool history:{false,true}){
        gSavedSettings.values["OnlineOfflinetoNearbyChatHistory"]=history;
        on_avatar_name_cache_notify(friend_id,name,true,payload);
        on_avatar_name_cache_notify(friend_id,name,false,payload);
    }
    assert(native_chat.messages==4 && nearby.messages==0 && sounds==4 && LLIMModel::instance().notices==4);
    LLFloaterReg::available=false;gViewerWindow=nullptr;
    on_avatar_name_cache_notify(friend_id,name,true,payload); // Retired UI never creates the legacy owner.
    LLFloaterReg::available=true;gViewerWindow=&viewer;VSNativeSession::native=false;
    gSavedSettings.values["OnlineOfflinetoNearbyChatHistory"]=false;
    on_avatar_name_cache_notify(friend_id,name,false,payload);
    assert(LLNotificationsUI::LLNotificationManager::instance().messages==1 && nearby.messages==0);
    gSavedSettings.values["OnlineOfflinetoNearbyChatHistory"]=true;
    on_avatar_name_cache_notify(friend_id,name,true,payload);assert(nearby.messages==1);
    VSNativeSession::native=true;gSavedSettings.values["OnlineOfflinetoNearbyChat"]=false;
    gSavedSettings.values["FSContactSetsNotificationNearbyChat"]=true;LGGContactSets::getInstance()->notify=true;
    gSavedSettings.values["OnlineOfflinetoNearbyChatHistory"]=false;
    gSavedPerAccountSettings.values["LogNearbyChat"]=true;
    on_avatar_name_cache_notify(friend_id,name,false,payload);assert(native_chat.messages==5 && LLLogChat::records==1);
}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'friend_status.cpp'
            executable = Path(directory) / 'friend_status.exe'
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
