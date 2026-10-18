from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Admission(unittest.TestCase):
 def test_actual_reply_admission_requires_live_ready_account_and_authenticated_current_or_neighbor_circuit(self):
  s=(ROOT/'indra/newview/vsnativesession.cpp').read_text();body=method(s,'bool VSNativeSession::admit(');ready=method((ROOT/'indra/newview/vsnativesession.h').read_text(),'bool regionReady() const');source=(HERE/'fixtures/native_sit_admission_fixture.cpp').read_text().replace('READY',ready).replace('ADMIT',body);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
