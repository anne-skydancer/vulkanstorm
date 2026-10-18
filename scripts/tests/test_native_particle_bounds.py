from pathlib import Path
import subprocess,shutil,tempfile,unittest,importlib.util
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent/'fixtures/native_particles'
class Bounds(unittest.TestCase):
 def test_actual_original_ribbon_and_billboard_spatial_extent(self):
  kernel='\n'.join(l for l in (ROOT/'indra/newview/vsparticlebounds.h').read_text().splitlines()if not l.startswith('#'));source=(HERE/'bounds_fixture.cpp').read_text().replace('GOLD',(HERE/'bounds_reference.cpp').read_text()).replace('KERNEL',kernel);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
