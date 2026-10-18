"""Actual attention wire/motion kernels versus frozen reference effect paths."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest,sys,re
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'scripts/tests'))
from native_source_fixture import method
class Attention(unittest.TestCase):
 def compile(self,source):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   folder=Path(tmp);cpp=folder/'test.cpp';exe=folder/'test.exe';cpp.write_text(source)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
 def test_original_look_motion_branches(self):
  src=(HERE/'native_attention_reference.cpp').read_text();body=method(src,'void LLHUDEffectLookAt::update(')
  start=body.index('            LLMotion* head_motion =');end=body.index('\n        }\n    }',start)
  body=body[start:end].replace('((LLVOAvatar*)(LLViewerObject*)mSourceObject)->','avatar.').replace('LLMotion*','auto*').replace('ANIM_AGENT_HEAD_ROT','1').replace('disable_look_at()','disable')
  kernel='\n'.join(line for line in(ROOT/'indra/newview/vsattentionmotion.h').read_text().splitlines()if not line.startswith('#'))
  self.compile('''#include <vector>
#include <cassert>
struct Motion{bool stopped=false;bool isStopped(){return stopped;}};
struct Avatar{bool exists=false;Motion motion;std::vector<int>calls;Motion*findMotion(int id){calls.push_back(10+id);return exists?&motion:nullptr;}void stopMotion(int id){calls.push_back(20+id);}void startMotion(int id){calls.push_back(30+id);}};
KERNEL
void reference(Avatar&avatar,bool disable){BODY}
int main(){for(bool exists:{false,true})for(bool stopped:{false,true})for(bool disable:{false,true}){Avatar gold;gold.exists=exists;gold.motion.stopped=stopped;auto native=gold;reference(gold,disable);vs_attention_motion(native,false,true,disable,1,2);assert(gold.calls==native.calls);}
Avatar avatar;vs_attention_motion(avatar,true,true,false,1,2);assert(avatar.calls==std::vector<int>{32});avatar.calls.clear();vs_attention_motion(avatar,true,false,false,1,2);assert(avatar.calls.empty());vs_attention_motion(avatar,false,false,false,1,2);assert(avatar.calls==std::vector<int>{21});}
'''.replace('KERNEL',kernel).replace('BODY',body))
 def test_original_wire_layout_and_update_boundaries(self):
  helper=(ROOT/'indra/newview/vsattentionpolicy.cpp').read_text();wire=method(helper,'std::array<U8,57> vs_attention_wire(')
  header=(ROOT/'indra/newview/vsattentionpolicy.h').read_text();due=method(header,'inline bool vs_attention_update_due(');limited=method(header,'inline bool vs_attention_limited(')
  point=(HERE/'native_attention_reference.cpp').read_text();pack=method(point,'void LLHUDEffectPointAt::packData(')
  start=pack.index('    U8 packed_data[PKT_SIZE];');end=pack.index('    mLastSendTime',start);pack=pack[start:end]
  pack=pack.replace('mesgsys->addBinaryDataFast(_PREHASH_TypeData, packed_data, PKT_SIZE);','std::array<U8,57>out;std::copy(packed_data,packed_data+57,out.begin());return out;')
  self.compile('''#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string_view>
using U8=uint8_t;using F32=float;using F64=double;constexpr int PKT_SIZE=57,SOURCE_AVATAR=0,TARGET_OBJECT=16,TARGET_POS=32,POINTAT_TYPE=56,MVT_LLUUID=1,MVT_LLVector3d=2,MVT_U8=3;
struct LLUUID{U8 mData[16]{};static const LLUUID null;};const LLUUID LLUUID::null{};
struct LLVector3{float v[3]{};};struct LLVector3d{double mdV[3]{};};float dist_vec_squared(const LLVector3&a,const LLVector3&b){float sum=0;for(int i=0;i<3;++i){auto d=a.v[i]-b.v[i];sum+=d*d;}return sum;}
struct VSAttentionPolicy{std::string_view name;};
void htolememcpy(void*d,const void*s,int,size_t n){std::memcpy(d,s,n);}
WIRE
DUE
LIMITED
struct Obj{LLUUID mID;};
std::array<U8,57>reference(Obj*mSourceObject,Obj*mTargetObject,LLVector3d mTargetOffsetGlobal,U8 mTargetType){PACK}
int main(){Obj source,target;for(unsigned i=0;i<16;++i){source.mID.mData[i]=i;target.mID.mData[i]=31-i;}
for(unsigned type=0;type<11;++type)for(bool object:{false,true})for(double value:{0.,-1.,1e30,-1e-20}){LLVector3d position{{value,value*2,value*.5}};auto result=vs_attention_wire(source.mID,object?target.mID:LLUUID::null,position,type);assert(result==reference(&source,object?&target:nullptr,position,type));for(unsigned i=0;i<3;++i){uint64_t bits;std::memcpy(&bits,&position.mdV[i],8);for(unsigned j=0;j<8;++j)assert(result[32+i*8+j]==((bits>>(j*8))&255));}assert(result[56]==type);}
LLVector3 origin{},position{{.05f,0,0}};assert(!vs_attention_update_due(position,origin,1,0));position.v[0]=std::nextafter(.05f,1.f);assert(!vs_attention_update_due(position,origin,.25f,0));assert(vs_attention_update_due(position,origin,std::nextafter(.25f,1.f),0));
for(auto name:{"None","Idle","Respond","Conversation","FreeLook","AutoListen"})assert(!vs_attention_limited({name},true));for(auto name:{"Hover","Select","Focus","Mouselook","Clear"}){assert(vs_attention_limited({name},true));assert(!vs_attention_limited({name},false));}}
'''.replace('WIRE',wire).replace('DUE',due).replace('LIMITED',limited).replace('PACK',pack))
if __name__=='__main__':unittest.main()
