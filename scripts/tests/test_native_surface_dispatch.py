from pathlib import Path
import subprocess,shutil,tempfile,unittest,importlib.util,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent;sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class SurfaceDispatch(unittest.TestCase):
 def test_actual_surface_refresh_retains_target_and_resets_failed_result(self):
  s=(ROOT/'indra/newview/llviewerwindow.cpp').read_text();body=method(s,'void LLPickInfo::getSurfaceInfo(');body=body[:body.index('    LLVector4a tangent;')]+'}\n';source=(HERE/'fixtures/native_surface_dispatch.cpp').read_text().replace('METHOD',body);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17','-DVS_NATIVE_VULKAN=1',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
