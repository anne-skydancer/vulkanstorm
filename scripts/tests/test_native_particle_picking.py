from pathlib import Path
import subprocess,shutil,tempfile,unittest,importlib.util
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent/'fixtures/native_particles'
class Picking(unittest.TestCase):
 def test_exact_original_segment_triangle_order_and_closest_particle(self):
  header=(ROOT/'indra/newview/vsparticlepick.h').read_text();kernel='\n'.join(l for l in header.splitlines()if not l.startswith('#'));source=(HERE/'pick_fixture.cpp').read_text().replace('GOLD',(HERE/'pick_reference.cpp').read_text()).replace('KERNEL',kernel);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
