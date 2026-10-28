from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest,os
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE));from native_source_fixture import method
class SpatialIndex(unittest.TestCase):
 def compile(self,text):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  paths=[ROOT/os.environ['VS_TEST_BUILD_DIRECTORY']/'packages/include']if os.environ.get('VS_TEST_BUILD_DIRECTORY')else[ROOT/'build-vc180-64/packages/include',ROOT/'build-linux-x86_64/packages/include'];boost=next((p for p in paths if(p/'boost/geometry/index/rtree.hpp').exists()),None);self.assertIsNotNone(boost,'Autobuild Boost headers required')
  with tempfile.TemporaryDirectory(dir=HERE)as tmp:
   p=Path(tmp);cpp=p/'main.cpp';exe=p/'main.exe';cpp.write_text(text);r=subprocess.run([compiler,'-std=c++17','-O2','-I',str(ROOT/'indra/newview'),'-isystem',str(boost),str(ROOT/'indra/newview/vsspatialindex.cpp'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_actual_charged_spatial_queries_updates_refusal_and_retirement(self):
  self.compile((HERE/'fixtures/native_spatial_index.cpp').read_text())
 def test_actual_scene_intake_generation_circuits_and_cpu_fallback(self):
  scene=(ROOT/'indra/newview/vsworldscene.cpp').read_text();helpers='\n'.join(method(scene,s)for s in ['    VSSpatialIndex::Bounds selectionBounds(', '    void updateSelectionIndex(']);text=(HERE/'fixtures/native_spatial_scene.cpp').read_text().replace('HELPERS',helpers).replace('METHOD',method(scene,'std::optional<VSWorldScene::SelectionCandidates> VSWorldScene::selectionCandidates('));self.compile(text)
if __name__=='__main__':unittest.main()
