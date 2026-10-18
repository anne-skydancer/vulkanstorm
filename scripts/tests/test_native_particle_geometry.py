from pathlib import Path
import importlib.util,subprocess,shutil,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent/'fixtures/native_particles'
class Geometry(unittest.TestCase):
 def test_original_billboard_ribbon_velocity_geometry(self):
  s=(ROOT/'indra/newview/vsparticlegeometry.h').read_text();kernel='\n'.join(l for l in s.splitlines()if not l.startswith('#'))
  fixture=(HERE/'geometry_fixture.cpp').read_text().replace('GOLD',(HERE/'geometry_reference.cpp').read_text()).replace('KERNEL',kernel)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'test.cpp').write_text(fixture);exe=p/'test.exe';built=subprocess.run([compiler,'-std=c++17',str(p/'test.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr);run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
