from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
def production_source():
 return (ROOT/'indra/newview/lltoolfocus.cpp').read_text()

class CameraPick(unittest.TestCase):
 def test_original_camera_pick_policy_and_order(self):
  current=method(production_source(),'template<class Peer>\nvoid LLToolCamera::applyPick(')
  reference=(HERE/'camera_pick_reference.cpp').read_text()
  text=(HERE/'camera_pick_fixture.cpp').read_text().replace('REFERENCE',reference).replace('CURRENT',current)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory(dir=HERE)as temp:
   cpp=Path(temp)/'pick.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(text)
   result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
