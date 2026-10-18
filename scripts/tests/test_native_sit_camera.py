from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Camera(unittest.TestCase):
 def test_actual_seat_render_offset_targets_and_retired_focus_during_query(self):
  s=(ROOT/'indra/newview/llagentcamera.cpp').read_text();start=s.index('    if(mSitCameraEnabled && mNativeFocus->sitQuery)');end=s.index('    if(!avatarEye.isFinite()',start);block=s[start:end].replace('return;','return false;').replace('LLUUID::null','0');source=(HERE/'fixtures/native_sit_camera_fixture.cpp').read_text().replace('BLOCK',block);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
