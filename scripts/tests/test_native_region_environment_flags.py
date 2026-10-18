"""Actual simulator flag schema through shared environment permission publication."""
from pathlib import Path
import re,shutil,subprocess,tempfile,unittest,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts/tests'))
from native_source_fixture import ROOT,method
class HandshakeFlags(unittest.TestCase):
 def test_real_flags64_wire_and_shared_environment_enable(self):
  fixture=(ROOT/'indra/newview/vsr1replay.cpp').read_text()
  self.assertEqual(fixture.count('handshake["flags64"]=ll_sd_from_U64(REGION_FLAGS_ALLOW_ENVIRONMENT_OVERRIDE)'),3)
  self.assertNotIn('handshake["flags"]=ll_sd_from_U64(',fixture)
  source=(ROOT/'indra/newview/vsnativesession.cpp').read_text()
  decode=re.search(r'mFlags = body\.has\("flags64"\)[^;]+;',source).group()
  util=(ROOT/'indra/llcommon/llsdutil.cpp').read_text()
  get=method((ROOT/'indra/newview/vsregioncontext.cpp').read_text(),'bool VSRegionContext::getAllowEnvironmentOverride() const')
  refresh=method((ROOT/'indra/newview/llpanelenvironment.cpp').read_text(),'void LLPanelEnvironmentInfo::refreshFromEstate()')
  code=r'''#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include <optional>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
using U8=unsigned char;using U32=std::uint32_t;using U64=std::uint64_t;
constexpr U64 REGION_FLAGS_ALLOW_ENVIRONMENT_OVERRIDE=1ull<<9;
struct LLSD{using Binary=std::vector<U8>;Binary bytes;int integer=0;LLSD()=default;LLSD(int v):integer(v){}explicit LLSD(Binary value):bytes(std::move(value)){}const Binary&asBinary()const{return bytes;}int asInteger()const{return integer;}};
'''+method(util,'LLSD ll_sd_from_U64(')+'\n'+method(util,'U64 ll_U64_from_sd(')+r'''
struct Body{std::map<std::string,LLSD>data;bool has(std::string key)const{return data.count(key);}const LLSD&operator[](std::string key)const{return data.at(key);}};
struct VSRegionContext{U64 flags=0;bool getRegionFlag(U64 flag)const{return (flags&flag)!=0;}bool getAllowEnvironmentOverride()const;};
'''+get+r'''
VSRegionContext region;std::optional<VSRegionContext>vs_current_region_context(){return region;}
struct LLEstateInfoModel{static auto&instance(){static LLEstateInfoModel e;return e;}bool getAllowEnvironmentOverride()const{return false;}};
struct LLPanelEnvironmentInfo{bool mAllowOverride=false;unsigned refreshes=0;bool isRegion(){return false;}void refresh(){++refreshes;}void refreshFromEstate();};
'''+refresh+r'''
int main(){U64 mFlags=0;Body body;body.data["flags64"]=ll_sd_from_U64(REGION_FLAGS_ALLOW_ENVIRONMENT_OVERRIDE);
'''+decode+r'''
region.flags=mFlags;LLPanelEnvironmentInfo panel;panel.refreshFromEstate();assert(panel.mAllowOverride && panel.refreshes==1);
body.data.clear();body.data["flags"]=LLSD(512);
'''+decode+r'''
assert(mFlags==512);
body.data["flags"]=ll_sd_from_U64(REGION_FLAGS_ALLOW_ENVIRONMENT_OVERRIDE);
'''+decode+r'''
region.flags=mFlags;panel.refreshFromEstate();assert(!panel.mAllowOverride && panel.refreshes==2);
}'''
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'flags.cpp';exe=Path(tmp)/'flags.exe';cpp.write_text(code)
   args=[compiler,'-std=c++17',str(cpp),'-o',str(exe)]
   if sys.platform=='win32':args.append('-lws2_32')
   built=subprocess.run(args,capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
