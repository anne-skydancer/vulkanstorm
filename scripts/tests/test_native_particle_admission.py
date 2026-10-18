from pathlib import Path
import subprocess,shutil,tempfile,unittest,sys,importlib.util
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent/'fixtures/native_particles';sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Admission(unittest.TestCase):
 def test_real_simulator_refusal_caps_destructor_and_legacy_admission(self):
  source=(ROOT/'indra/newview/llviewerpartsim.cpp').read_text();body='\n'.join(method(source,s)for s in('LLViewerPart::~LLViewerPart()','void LLViewerPartSim::addPart(','void LLViewerPartSim::addPartSource('));body=body.replace('    LLViewerPartSim::decPartCount();','')
  fixture=(HERE/'admission_fixture.cpp').read_text().replace('METHODS',body);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(fixture);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17','-DVS_NATIVE_VULKAN=1',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
