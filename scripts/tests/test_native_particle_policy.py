from pathlib import Path
import subprocess,shutil,tempfile,unittest,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent/'fixtures/native_particles';sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Policy(unittest.TestCase):
 def test_real_source_mutes_attachment_avatar_limits_and_retirement(self):
  body=method((ROOT/'indra/newview/vsparticleeffects.cpp').read_text(),'std::function<bool()> VSParticleEffects::sourcePolicy(');source=(HERE/'source_policy_fixture.cpp').read_text().replace('POLICY',body);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
