"""Exercise the actual connected avatar reset route and authenticated wire payload."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class NativeAvatarResetTest(unittest.TestCase):
    def test_actual_owner_reset_and_broadcast_scope(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (ROOT/'indra/newview/vsnativesession.cpp').read_text()
        route = method(source, 'bool VSNativeSession::resetAvatar(')
        fixture = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <string>
#include <vector>
using U8=unsigned char;using S32=int;constexpr unsigned UUID_BYTES=16;
struct LLUUID {U8 mData[16]{};LLUUID(unsigned v=0){mData[0]=U8(v);}
 bool operator!=(const LLUUID& b)const{return !std::equal(mData,mData+16,b.mData);}
 static LLUUID generateNewID(){return LLUUID{99};}};
struct LLHUDObject {enum {LL_HUD_EFFECT_RESET_SKELETON=12};};
struct Character {LLUUID id{1};const LLUUID& getID()const{return id;}};
struct Avatar {Character owner;bool ready=true;int resets=0;bool resetAnimations=false;
 bool resetSkeleton(bool animations){if(!ready)return false;++resets;resetAnimations=animations;return true;}
 Character& character(){return owner;}};
struct Scene {Avatar self,other;Scene(){other.owner.id=LLUUID{3};}
 Avatar* avatarForObject(LLUUID id){return id.mData[0]==1?&self:id.mData[0]==3?&other:nullptr;}};
struct Context {Scene scene;Scene& world(){return scene;}};
struct Window {Context context;bool live=true;Context* nativeContext(){return live?&context:nullptr;}} window,*gViewerWindow=&window;
struct LLMessageSystem {int sends=0,host=0;bool sendOK=true;std::string message;
 std::map<std::string,LLUUID> uuids;std::map<std::string,std::vector<U8>> binary;int type=0;
 void newMessage(const char* value){message=value;}void nextBlock(const char*){}
 void addUUID(const char* key,LLUUID value){uuids[key]=value;}
 void addU8(const char*,U8 value){type=value;}void addF32(const char*,float){}
 void addBinaryData(const char* key,const U8* value,S32 size){binary[key]={value,value+size};}
 int sendReliable(int value){++sends;host=value;return sendOK?1:0;}} messages,*gMessageSystem=&messages;
struct VSNativeSession {enum class Phase {Login,Connected};Phase mPhase=Phase::Login;
 LLUUID mAgent{1},mSession{2};int mHost=42;bool resetAvatar(const LLUUID&,bool);};
ROUTE
int main(){VSNativeSession session;
 assert(!session.resetAvatar(LLUUID{1},false));assert(messages.sends==0);
 session.mPhase=VSNativeSession::Phase::Connected;window.live=false;
 assert(!session.resetAvatar(LLUUID{1},false));window.live=true;
 assert(!session.resetAvatar(LLUUID{9},false));
 window.context.scene.self.ready=false;assert(!session.resetAvatar(LLUUID{1},false));
 window.context.scene.self.ready=true;
 assert(session.resetAvatar(LLUUID{3},true));assert(window.context.scene.other.resets==1);
 assert(window.context.scene.other.resetAnimations&&messages.sends==0);
 assert(session.resetAvatar(LLUUID{1},false));assert(messages.sends==1&&messages.host==42);
 assert(messages.message=="ViewerEffect"&&messages.type==LLHUDObject::LL_HUD_EFFECT_RESET_SKELETON);
 assert(messages.uuids["AgentID"].mData[0]==1&&messages.uuids["SessionID"].mData[0]==2);
 auto data=messages.binary["TypeData"];assert(data.size()==17&&data[0]==1&&data[16]==0);
 assert(std::all_of(data.begin()+1,data.begin()+16,[](U8 byte){return byte==0;}));
 assert(session.resetAvatar(LLUUID{1},true));assert(messages.binary["TypeData"][16]==1);
 messages.sendOK=false;assert(!session.resetAvatar(LLUUID{1},true));
}
'''.replace('ROUTE', route)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'native_avatar_reset.cpp'
            exe=Path(directory)/'native_avatar_reset.exe'
            cpp.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)


if __name__ == '__main__':
    unittest.main()
