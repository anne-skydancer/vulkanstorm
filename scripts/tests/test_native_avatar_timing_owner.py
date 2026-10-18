from pathlib import Path
import subprocess,shutil,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent/'fixtures/native_particles'
import sys
sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Owner(unittest.TestCase):
 def test_current_avatar_character_incarnation_and_whole_owner_refusal(self):
  source=(HERE/'timing_owner_fixture.cpp').read_text().replace('METHOD',method((ROOT/'indra/newview/vsworldscene.cpp').read_text(),'std::shared_ptr<const VSWorldRenderer::AvatarTiming> VSWorldScene::avatarTiming('));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
