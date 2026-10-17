"""Compile proposed shared capability catalogue against exact reference behavior."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
class CapabilitiesTest(unittest.TestCase):
 def test_reference_catalogue_and_native_union(self):
  legacy=(HERE/'native_capabilities_reference_fixture.cpp').read_text().replace('void LLViewerRegionImpl::buildCapabilityNames(', 'void reference(')
  shared=(ROOT/'indra/newview/vscapabilities.cpp').read_text(encoding='utf-8-sig');shared=shared[shared.index('void vs_build_capability_names('):]
  fixture='''#include <string>
#include <vector>
#include <set>
#include <cassert>
struct LLSD {std::string value;std::vector<LLSD> values;static LLSD emptyArray(){return {};};void append(const char* v){LLSD s;s.value=v;values.push_back(s);}std::string asString()const{return value;}auto beginArray()const{return values.begin();}auto endArray()const{return values.end();}};
bool http=false;bool use_http_inventory(){return http;}
struct AISAPI {static void getCapNames(LLSD& c){c.append("InventoryAPIv3");c.append("LibraryAPIv3");}};
'''+legacy+'\n'+shared+'''
std::set<std::string> names(const LLSD& a){std::set<std::string> result;for(auto i=a.beginArray();i!=a.endArray();++i)result.insert(i->asString());return result;}
int main(){for(bool enabled:{false,true}){http=enabled;LLSD referenceCaps,sharedCaps;reference(referenceCaps);vs_build_capability_names(sharedCaps);assert(names(referenceCaps)==names(sharedCaps));assert(sharedCaps.values.size()==referenceCaps.values.size());auto native=vs_native_capability_names();auto actual=names(native);assert(actual.size()==native.values.size());for(auto& n:names(sharedCaps))assert(actual.count(n));for(auto* n:{"ParcelPropertiesUpdate","ParcelVoiceInfoRequest","RegionExperiences","EstateChangeInfo","LandResources","DispatchOpenRegionSettings","InventoryAPIv3","LibraryAPIv3","FetchInventory2","RequestTaskInventory","Tracking","VETPBR"})assert(actual.count(n));assert(names(sharedCaps).count("FetchInventory2")==unsigned(enabled));}}
'''
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'caps.cpp';exe=Path(tmp)/'caps.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  self.assertEqual((ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8-sig').count('const LLSD wanted = vs_native_capability_names();'),1)
  self.assertEqual((ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8-sig').count('const LLSD requestedCapabilities=requested.isArray() ? requested : vs_native_capability_names();'),1)
  self.assertEqual((ROOT/'indra/newview/vsnativecircuits.cpp').read_text(encoding='utf-8-sig').count('vs_native_capability_names();'),1)
  self.assertIn('[weak, result, generation, regionEpoch, wanted]()',(ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8-sig'))
  self.assertIn('owner->capabilities(result, generation, regionEpoch, wanted);',(ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8-sig'))
  self.assertIn('requestRevision,result,adapter,wanted]()',(ROOT/'indra/newview/vsnativecircuits.cpp').read_text(encoding='utf-8-sig'))
  self.assertIn('const LLSD& requestedCapabilities=wanted;',(ROOT/'indra/newview/vsnativecircuits.cpp').read_text(encoding='utf-8-sig'))
if __name__=='__main__':unittest.main()
