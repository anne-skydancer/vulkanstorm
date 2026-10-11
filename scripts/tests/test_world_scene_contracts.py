"""Compile production scene decoding, camera math and simulator admission without UI."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method
ROOT = Path(__file__).resolve().parents[2]
class WorldSceneContracts(unittest.TestCase):
    def compile(self, text):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'world.cpp';binary=source.with_suffix('.exe')
            source.write_text(text,encoding='utf-8')
            built=subprocess.run([compiler,'-std=c++20',str(source),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
    def test_extra_parameter_bounds_and_wire_floats(self):
        source=(ROOT/'indra/newview/vsworldscene.cpp').read_text(encoding='utf-8')
        definitions='\n'.join(method(source,name) for name in ('U32 littleU32(', 'F32 littleFloat(', 'bool baseVolume('))
        self.compile('''#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cmath>
using U8=uint8_t;using U16=uint16_t;using U32=uint32_t;using F32=float;
'''+definitions+'''
int main() {
 const U8 f[]={0,0,32,65};assert(littleFloat(f)==10.f);
 const U8 id[]={0x78,0x56,0x34,0x12};assert(littleU32(id)==0x12345678);
 assert(baseVolume({}));assert(baseVolume({0}));assert(!baseVolume({1}));
 assert(baseVolume({1,0x20,0,1,0,0,0,77}));
 assert(!baseVolume({1,0x30,0,1,0,0,0,77}));
 assert(!baseVolume({1,0x10,0,1,0,0,0,77}));
 assert(!baseVolume({1,0x20,0,255,255,255,255}));
 assert(!baseVolume({1,0x20,0,2,0,0,0,77}));
 assert(!baseVolume({0,77}));
}''')
    def test_camera_clip_depth_and_region_axes(self):
        source=(ROOT/'indra/newview/vsworldscene.cpp').read_text(encoding='utf-8')
        definitions=method(source,'bool finiteVector(')+'\n'+method(source,'VSWorldRenderer::View VSWorldScene::view(')
        self.compile('''#include <array>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <limits>
struct LLVector3 { float mV[3]; LLVector3(float x,float y,float z):mV{x,y,z}{} };
constexpr float F_PI=3.14159265358979323846f;
float llclamp(float v,float a,float b) {return std::clamp(v,a,b);}
struct VSWorldRenderer { struct View {std::array<float,16> projection{};}; };
struct VSWorldScene {static VSWorldRenderer::View view(const LLVector3&,unsigned,unsigned,float);};
'''+definitions+'''
int main() {
 auto m=VSWorldScene::view({10,20,30},800,400,128).projection;
 auto project=[&](float x,float y,float z) {
  std::array<float,4> p{};
  for(int r=0;r<4;++r)p[r]=m[r]*x+m[4+r]*y+m[8+r]*z+m[12+r];
  return p;
 };
 auto p=project(10.1f,20,30);assert(std::abs(p[2]/p[3]+1)<.001f);
 p=project(138,20,30);assert(std::abs(p[2]/p[3]-1)<.001f);
 p=project(20,21,31);assert(p[0]<0&&p[1]>0&&p[3]>0);
 auto bad=VSWorldScene::view({0,0,0},0,10,128).projection;
 for(float v:bad)assert(v==0);
}''')
    def test_authenticated_current_region_and_phase_admission(self):
        source=(ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8')
        definitions=method(source,'bool VSNativeSession::admit(')
        self.compile('''#include <cassert>
#include <set>
#include <string>
#include <sstream>
struct LLHost {int value=1;bool isOk()const{return value!=0;}bool operator==(const LLHost& b)const{return value==b.value;}};
std::ostringstream sink;
#define LL_WARNS(x) sink
#define LL_ENDL ""
const std::set<std::string> transport={"PacketAck"},session={"ChatFromSimulator"},worldMessages={"ObjectUpdate","KillObject"};
struct VSNativeSession {
 enum class Phase {Login,Connecting,Connected,Logout,Disconnected};
 LLHost mHost;Phase mPhase=Phase::Login;bool mChangingRegion=false;unsigned mRejected=0;
 bool admit(const std::string&,const LLHost&);
};
'''+definitions+'''
int main(){
 VSNativeSession s;assert(!s.admit("ObjectUpdate",{1}));
 s.mPhase=VSNativeSession::Phase::Connecting;assert(!s.admit("ObjectUpdate",{1}));
 s.mPhase=VSNativeSession::Phase::Connected;assert(s.admit("ObjectUpdate",{1}));
 assert(!s.admit("ObjectUpdate",{2}));assert(!s.admit("LayerData",{1}));
 s.mChangingRegion=true;assert(!s.admit("KillObject",{1}));
 s.mChangingRegion=false;s.mPhase=VSNativeSession::Phase::Logout;assert(!s.admit("ObjectUpdate",{1}));
}''')
if __name__=='__main__':unittest.main()
