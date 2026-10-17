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
    def test_refused_mesh_publication_preserves_generation_and_retries(self):
        source=(ROOT/'indra/newview/vsworldrenderer.cpp').read_text(encoding='utf-8')
        publication=method(source, 'bool VSWorldRenderer::publishMesh(')
        budget=(ROOT/'indra/newview/vsresourcebudget.h').read_text(encoding='utf-8').replace('#pragma once','')
        self.compile(budget+"""
#include <cassert>
#include <vector>
struct VSWorldRenderer {
 enum class Topology {Triangles,Lines,Points};
 struct Vertex {float value;};
 struct Geometry {float value;std::shared_ptr<const VSResourceBudget::Reservation> lease;};
 using Mesh=std::shared_ptr<const Geometry>;
 VSResourceBudget budget{16};unsigned refusals=0,lastAttributes=0;
 Mesh upload(const std::vector<Vertex>& vertices,const std::vector<unsigned>& indices,Topology=Topology::Triangles,unsigned attributes=7) {
  lastAttributes=attributes;
  auto lease=budget.reserve(vertices.size()*sizeof(Vertex)+indices.size()*sizeof(unsigned));
  if(!lease){++refusals;return {};}
  return std::make_shared<Geometry>(Geometry{vertices[0].value,std::move(lease)});
 }
 bool publishMesh(Mesh&,const std::vector<Vertex>&,const std::vector<unsigned>&,Topology=Topology::Triangles,unsigned=7);
};
"""+publication+"""
int main() {
 VSWorldRenderer owner;VSWorldRenderer::Mesh current;
 assert(owner.publishMesh(current,{{6}},{0},VSWorldRenderer::Topology::Triangles,15));
 assert(owner.lastAttributes==15);
 auto blocker=owner.upload({{9}},{0});
 auto* original=current.get();std::weak_ptr<const VSWorldRenderer::Geometry> previous=current;
 bool dirty=!owner.publishMesh(current,{{7}},{0});
 assert(dirty&&current.get()==original&&current->value==6);
 assert(owner.refusals==1&&owner.budget.used()==16);
 blocker.reset();assert(owner.budget.used()==8);
 dirty=!owner.publishMesh(current,{{7}},{0});
 assert(!dirty&&current->value==7&&previous.expired());
 assert(owner.budget.used()==8);
 current.reset();assert(owner.budget.used()==0);
}
""")
    def test_extra_parameter_bounds_and_wire_floats(self):
        source=(ROOT/'indra/newview/vsworldscene.cpp').read_text(encoding='utf-8')
        definitions='\n'.join(method(source,name) for name in ('U32 littleU32(', 'F32 littleFloat(', 'bool baseVolume('))
        self.compile('''#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
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
    def test_mesh_extra_parameter_identity_and_bounds(self):
        source=(ROOT/'indra/newview/vsworldscene.cpp').read_text(encoding='utf-8')
        definitions='\n'.join(method(source,name) for name in ('U32 littleU32(', 'bool baseVolume(', 'bool volumeExtras('))
        self.compile('''#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>
using U8=uint8_t;using U16=uint16_t;using U32=uint32_t;
constexpr unsigned UUID_BYTES=16;constexpr U8 LL_SCULPT_TYPE_MASK=7,LL_SCULPT_TYPE_MESH=5;
struct LLUUID {U8 mData[16]{};bool isNull()const {for(auto c:mData)if(c)return false;return true;}};
constexpr int LL_PCODE_PATH_FLEXIBLE=128;
struct LLFlexibleObjectData {};
bool flexibleData(const U8*,U32 length,LLFlexibleObjectData&){return length==4||length==16;}
struct LLVolumeParams {LLUUID id;U8 type=0;bool setSculptID(const LLUUID& value,U8 t){id=value;type=t;return true;}
 LLUUID getSculptID()const{return id;}struct Profile {int getCurveType()const{return 0;}};
 Profile getProfileParams()const{return {};}bool setType(int,int){return true;}};
'''+definitions+'''
int main(){
 LLVolumeParams p;std::optional<LLFlexibleObjectData> flexible;
 auto decode=[&](const std::vector<U8>& bytes){return volumeExtras(bytes,p,flexible);};
 assert(decode({}));
 std::vector<U8> mesh={1,0x30,0,17,0,0,0};mesh.resize(24);mesh[7]=42;mesh[23]=5;
 assert(decode(mesh));assert(p.id.mData[0]==42&&p.type==5);
 auto invalid=mesh;invalid.pop_back();assert(!decode(invalid));
 invalid=mesh;invalid[7]=0;assert(!decode(invalid));
 // R2 accepts sculpt maps as well as mesh identities, preserving flags.
 auto sculpt=mesh;sculpt[23]=1;assert(decode(sculpt)&&p.type==1);
 sculpt[23]=0xc1;assert(decode(sculpt)&&p.type==0xc1);
 invalid=mesh;invalid[1]=0x10;assert(!decode(invalid));
 invalid=mesh;invalid[3]=255;assert(!decode(invalid));
 invalid=mesh;invalid.push_back(0);assert(!decode(invalid));
}''')
    def test_camera_basis_projection(self):
        source=(ROOT/'indra/newview/vsworldscene.cpp').read_text(encoding='utf-8')
        definitions=method(source,'bool finiteVector(')+'\n'+method(source,'VSWorldRenderer::View VSWorldScene::cameraView(')
        self.compile('''#include <array>
#include <cmath>
#include <algorithm>
#include <cassert>
struct LLVector3 {
 float mV[3];LLVector3(float x,float y,float z):mV{x,y,z}{}
 float normalize(){float n=std::sqrt((*this)*(*this));if(n)for(auto&v:mV)v/=n;return n;}
 float operator*(const LLVector3& b)const{return mV[0]*b.mV[0]+mV[1]*b.mV[1]+mV[2]*b.mV[2];}
 LLVector3 operator%(const LLVector3& b)const{return {mV[1]*b.mV[2]-mV[2]*b.mV[1],mV[2]*b.mV[0]-mV[0]*b.mV[2],mV[0]*b.mV[1]-mV[1]*b.mV[0]};}
};
constexpr float F_PI=3.14159265358979323846f;
float llclamp(float v,float a,float b){return std::clamp(v,a,b);}
struct VSWorldRenderer {struct View{std::array<float,16> projection{};};};
struct VSWorldScene {static VSWorldRenderer::View cameraView(const LLVector3&,const LLVector3&,const LLVector3&,unsigned,unsigned,float);};
'''+definitions+'''
int main(){
 auto m=VSWorldScene::cameraView({10,20,30},{0,1,0},{0,0,1},800,400,128).projection;
 auto project=[&](float x,float y,float z){std::array<float,4> p{};for(int r=0;r<4;++r)p[r]=m[r]*x+m[4+r]*y+m[8+r]*z+m[12+r];return p;};
 auto p=project(10,20.1f,30);assert(std::abs(p[2]/p[3]+1)<.001f);
 p=project(10,148,30);assert(std::abs(p[2]/p[3]-1)<.001f);
 p=project(11,30,31);assert(p[0]>0&&p[1]>0&&p[3]>0);
 auto bad=VSWorldScene::cameraView({0,0,0},{1,0,0},{1,0,0},800,400,128).projection;
 for(auto v:bad)assert(v==0);
}''')
    def test_authenticated_current_region_and_phase_admission(self):
        source=(ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8')
        definitions=method(source,'bool VSNativeSession::admit(')
        self.compile('''#include <cassert>
#include <set>
#include <map>
#include <string>
#include <sstream>
struct LLHost {int value=1;bool isOk()const{return value!=0;}bool operator==(const LLHost& b)const{return value==b.value;}bool operator<(const LLHost& b)const{return value<b.value;}};
std::ostringstream sink;
#define LL_WARNS(x) sink
#define LL_ENDL ""
const std::set<std::string> transport={"PacketAck"},session={"ChatFromSimulator"},worldMessages={"ObjectUpdate","KillObject","LayerData"};
struct VSNativeSession {
 enum class Phase {Login,Connecting,Connected,Logout,Disconnected};
 LLHost mHost;Phase mPhase=Phase::Login;bool mChangingRegion=false;unsigned mRejected=0;
 struct Region {bool acknowledged=false,handshake=false;};std::map<LLHost,Region> mCircuits;
 bool admit(const std::string&,const LLHost&);
};
'''+definitions+'''
int main(){
 VSNativeSession s;assert(!s.admit("ObjectUpdate",{1}));
 s.mPhase=VSNativeSession::Phase::Connecting;assert(s.admit("ObjectUpdate",{1}));assert(s.admit("LayerData",{1}));assert(!s.admit("LayerData",{2}));
 s.mPhase=VSNativeSession::Phase::Connected;assert(s.admit("ObjectUpdate",{1}));
 assert(!s.admit("ObjectUpdate",{2}));assert(s.admit("LayerData",{1}));
 s.mChangingRegion=true;assert(s.admit("KillObject",{1}));
 s.mCircuits[{2}]={};assert(!s.admit("ObjectUpdate",{2}));
 s.mCircuits[{2}].acknowledged=true;assert(!s.admit("ObjectUpdate",{2}));
 s.mCircuits[{2}].handshake=true;assert(s.admit("ObjectUpdate",{2}));
 s.mCircuits.erase({2});assert(!s.admit("ObjectUpdate",{2}));
 s.mChangingRegion=false;s.mPhase=VSNativeSession::Phase::Logout;assert(!s.admit("ObjectUpdate",{1}));
}''')
if __name__=='__main__':unittest.main()
