from pathlib import Path
import importlib.util,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
class Motion(unittest.TestCase):
 def test_actual_editor_motion_preview_preserves_server_clock_velocity_and_selection(self):
  header=(ROOT/'indra/newview/vsworldmotion.h').read_text();compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'vsworldmotion.h').write_text(header);(p/'v3math.h').write_text((HERE/'fixtures/native_transform_motion_vector.h').read_text());(p/'llquaternion.h').write_text((HERE/'fixtures/native_transform_motion_quaternion.h').read_text());(p/'main.cpp').write_text((HERE/'fixtures/native_transform_motion_fixture.cpp').read_text());exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
