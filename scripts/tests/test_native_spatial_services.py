"""Compile current native spatial publications and region-stat authority consumers."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent

def method(source,signature):
 a=source.index(signature);start=source.index('{',a);n=0
 for b in range(start,len(source)):
  if source[b]=='{':n+=1
  elif source[b]=='}':
   n-=1
   if not n:return source[a:b+1]
 raise AssertionError(signature)
class SpatialServiceTest(unittest.TestCase):
 def test_lightweight_immutable_footprints(self):
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  source=(ROOT/'indra/newview/vsworldscene.cpp').read_text(encoding='utf-8-sig')
  methods='\n'.join(method(source,s) for s in ['std::shared_ptr<const VSResourceBudget::Reservation> VSWorldScene::reserveSpatialBytes(', 'std::optional<VSWorldScene::ObjectSnapshot> VSWorldScene::objectSnapshot(', 'std::vector<VSWorldScene::ObjectSnapshot> VSWorldScene::objectSnapshots()'])
  footprint=method(source,'std::vector<VSWorldScene::MapObjectSnapshot> VSWorldScene::mapObjectSnapshots()');self.assertNotIn('nameValues',footprint);self.assertNotIn('objectSnapshot(',footprint)
  fixture=(HERE/'native_spatial_services_fixture.cpp').read_text(encoding='utf-8-sig').replace('METHODS',methods).replace('FOOTPRINTS',footprint)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'footprints.cpp';exe=Path(tmp)/'footprints.exe';cpp.write_text(fixture)
   result=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
 def test_current_region_flags_capacity_and_authority_snapshot(self):
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  fixture=(HERE/'native_spatial_roots_fixture.cpp').read_text(encoding='utf-8-sig').split('METHODS')[0]
  fixture=fixture.replace('bool mShutdown=false;', 'bool mShutdown=false,mChangingRegion=false;U64 mFlags=0;')
  fixture=fixture.replace('    bool receiveCoarse(', '    bool applyRegionStats(const LLHost&,U64,U64,U64,U32);\n    bool receiveCoarse(')
  source=(ROOT/'indra/newview/vsnativecircuits.cpp').read_text(encoding='utf-8-sig')
  methods='\n'.join(method(source,signature) for signature in ['bool VSNativeSession::applyRegionStats(', 'std::vector<VSNativeSession::RegionSnapshot> VSNativeSession::regionSnapshots()'])
  main=(HERE/'native_region_stats_fixture.cpp').read_text(encoding='utf-8-sig')
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'stats.cpp';exe=Path(tmp)/'stats.exe';cpp.write_text(fixture+methods+'\n'+main)
   result=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
