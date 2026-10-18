from pathlib import Path
import subprocess,shutil,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent/'fixtures/native_particles'
class Timing(unittest.TestCase):
 def test_current_complete_ordered_nonblocking_gpu_results(self):
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);source=p/'timing.cpp';source.write_text('#include <functional>\n'+(HERE/'timing_fixture.cpp').read_text());exe=p/'timing.exe'
   run=subprocess.run([compiler,'-std=c++17','-I'+str(ROOT/'indra/newview'),str(source),'-o',str(exe)],text=True,capture_output=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
   run=subprocess.run([str(exe)],text=True,capture_output=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
