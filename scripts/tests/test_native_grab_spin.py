from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Spin(unittest.TestCase):
 def test_original_controller_spin_wire_modes_and_stale_identity(self):
  original=(HERE/'fixtures/native_grab_original_spin.cpp').read_text();proposal=(ROOT/'indra/newview/lltoolgrab.cpp').read_text();signatures=['void LLToolGrabBase::startSpin(','void LLToolGrabBase::stopSpin('];ref='\n'.join(method(original,s)for s in signatures).replace('LLToolGrabBase::','Reference::');native='\n'.join(method(proposal,s)for s in signatures).replace('LLToolGrabBase::','Native::');source=(HERE/'fixtures/native_grab_spin_fixture.cpp').read_text().replace('REFERENCE',ref).replace('NATIVE',native);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
