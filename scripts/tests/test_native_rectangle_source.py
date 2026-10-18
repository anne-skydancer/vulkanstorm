from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE));from native_source_fixture import method
class RectangleSource(unittest.TestCase):
 def test_original_vertex_frustum_policy_and_actual_native_cpu_sources(self):
  camera=(ROOT/'indra/newview/vsworldscene.cpp').read_text();kernel=(ROOT/'indra/newview/vsvolumevisibility.h').read_text();kernel='\n'.join(l for l in kernel.splitlines()if not l.startswith('#'))
  text=(HERE/'fixtures/native_rectangle_source.cpp').read_text().replace('KERNEL',kernel).replace('REFERENCE',(HERE/'fixtures/native_rectangle_source_original.cpp').read_text()).replace('VERTS',method(camera,'bool VSWorldScene::objectVertsVisible(')).replace('BOUNDS',method(camera,'std::optional<std::array<LLVector3,2>> VSWorldScene::sourceObjectBounds('))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory(dir=HERE)as tmp:
   p=Path(tmp);cpp=p/'main.cpp';exe=p/'main.exe';cpp.write_text(text);r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
