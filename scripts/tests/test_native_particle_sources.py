from pathlib import Path
import subprocess,shutil,tempfile,unittest,importlib.util,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent/'fixtures/native_particles';sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class SourcePositions(unittest.TestCase):
 def test_chat_and_wrist_beam_exact_reference_and_expired_source(self):
  original=(HERE/'source_positions_reference.cpp').read_text();current=(ROOT/'indra/newview/llviewerpartsource.cpp').read_text();signatures=('void LLViewerPartSourceChat::updatePart(','void LLViewerPartSourceBeam::updatePart(')
  source=(HERE/'source_positions_fixture.cpp').read_text().replace('GOLD','\n'.join(method(original,s)for s in signatures)).replace('PEER',method(current,'bool LLViewerPartSource::sourcePosition(')+'\n'+'\n'.join(method(current,s)for s in signatures))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17','-DVS_NATIVE_VULKAN=1',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
