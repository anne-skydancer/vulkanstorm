"""Compile the real native app-URL gate and dispatcher, without a viewer or network."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class NativeAppURLTests(unittest.TestCase):
    def test_real_gate_routes_cpu_owners_and_retains_registry_trust_and_phase(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            self.fail('A C++ compiler is required for the production-body regression')
        source = (ROOT / 'indra/newview/llcommandhandler.cpp').read_text()
        gate = function(source, 'bool LLCommandDispatcher::isNativeConnectedCommand(')
        dispatch = function(source, 'bool LLCommandDispatcher::dispatch(')
        fixture = r'''
#include <cassert>
#include <string>
#include <vector>
#define VS_NATIVE_VULKAN 1
constexpr int CHAT_CHANNEL_DEBUG=2147483647;
struct LLSD {
 std::string value; std::vector<LLSD> children;
 LLSD()=default; LLSD(const char* s):value(s){} LLSD(const std::string& s):value(s){}
 LLSD(std::initializer_list<LLSD> values):children(values){}
 size_t size()const{return children.size();}
 const LLSD& operator[](size_t n)const{return children.at(n);}
 LLSD& operator[](const char*){return *this;}
 LLSD& operator=(const std::string& s){value=s;return *this;}
 std::string asString()const{return value;}
 int asInteger()const{try{return std::stoi(value);}catch(...){return 0;}}
};
struct LLUUID {
 std::string value; LLUUID(std::string s):value(s){}
 static bool validate(std::string s){return s.size()==36 && s[8]=='-' && s[13]=='-' && s[18]=='-' && s[23]=='-';}
 bool isNull()const{return value=="00000000-0000-0000-0000-000000000000";}
};
struct LLMediaCtrl{};
struct VSNativeSession {
 enum class Phase{Login,Connected,Stopping}; Phase state=Phase::Connected;
 static VSNativeSession* current;static VSNativeSession* active(){return current;}Phase phase()const{return state;}
}; VSNativeSession* VSNativeSession::current=nullptr;
struct LLTrans{static std::string getString(const char* s){return s;}};
struct LLNotificationsUtil{static int errors;static void add(const char*,const LLSD&){++errors;}};int LLNotificationsUtil::errors=0;
struct LLCommandHandlerRegistry {
 int calls=0;bool trust=false;std::string navigation;LLMediaCtrl* browser=nullptr;
 static LLCommandHandlerRegistry& instance(){static LLCommandHandlerRegistry r;return r;}
 bool dispatch(const std::string&,const LLSD&,const LLSD&,const std::string&,LLMediaCtrl* web,const std::string& nav,bool trusted){++calls;trust=trusted;navigation=nav;browser=web;return trusted;}
};
struct LLCommandDispatcher {
 static bool isNativeConnectedCommand(const std::string&,const LLSD&);
 static bool dispatch(const std::string&,const LLSD&,const LLSD&,const std::string&,LLMediaCtrl*,const std::string&,bool);
};
GATE
DISPATCH
int main(){
 const char* id="12345678-1234-1234-1234-123456789abc";
 auto allow=[](const char* cmd,LLSD args){assert(LLCommandDispatcher::isNativeConnectedCommand(cmd,args));};
 auto deny=[](const char* cmd,LLSD args){assert(!LLCommandDispatcher::isNativeConnectedCommand(cmd,args));};
 allow("help",{});allow("help",{"topic"});allow("search",{"people","someone"});
 allow("keybinding",{"jump"});allow("profile",{"test.resident"});allow("fshelp",{"showdebug","RenderQualityPerformance"});
 allow("balance",{"request"});allow("chat",{"5","hello"});allow("inventory",{"show"});allow("inventory",{"filters"});
 allow("inventory",{id,"select"});allow("objectim",{id});allow("sharewithavatar",{id});allow("agentself",{id,"about"});
 allow("agent",{id,"offerteleport"});allow("pick",{id,"edit"});
 deny("inventory",{id,"delete"});deny("agent",{id,"reportAbuse"});deny("pick",{"create"});deny("parcel",{id,"about"});
 deny("worldmap",{"region"});deny("openfloater",{"unknown"});allow("event",{"12","details"});allow("experience",{id,"profile"});
 allow("event",{"12","notify","enable"});allow("event",{"12","notify","disable"});
 deny("event",{"0","details"});deny("event",{"12","notify","unknown"});deny("experience",{id,"edit"});
 deny("objectim",{"not-an-id"});deny("sharewithavatar",{"00000000-0000-0000-0000-000000000000"});
 deny("chat",{"0","bad"});deny("chat",{"-1","bad"});deny("chat",{"2147483647","bad"});deny("fshelp",{"arbitrary","setting"});
 VSNativeSession owner;VSNativeSession::current=&owner;LLMediaCtrl browser;
 auto& registry=LLCommandHandlerRegistry::instance();
 assert(!LLCommandDispatcher::dispatch("balance",{"request"},{},"grid",&browser,"navigated",false));
 assert(registry.calls==1&&!registry.trust&&registry.navigation=="navigated"&&registry.browser==&browser);
 assert(LLCommandDispatcher::dispatch("balance",{"request"},{},"grid",&browser,"clicked",true));
 assert(registry.calls==2&&registry.trust&&registry.navigation=="clicked");
 owner.state=VSNativeSession::Phase::Stopping;
 LLCommandDispatcher::dispatch("inventory",{"show"},{},"grid",&browser,"clicked",true);
 assert(registry.calls==2&&LLNotificationsUtil::errors==1);
 owner.state=VSNativeSession::Phase::Login;
 LLCommandDispatcher::dispatch("help",{},{},"grid",&browser,"clicked",true);
 assert(registry.calls==2&&LLNotificationsUtil::errors==2);
 LLCommandDispatcher::dispatch("login",{},{},"grid",&browser,"clicked",true);assert(registry.calls==3);
 VSNativeSession::current=nullptr;
 LLCommandDispatcher::dispatch("worldmap",{},{},"grid",&browser,"clicked",true);assert(registry.calls==4);
}
'''.replace('GATE', gate).replace('DISPATCH', dispatch)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'app_url.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
