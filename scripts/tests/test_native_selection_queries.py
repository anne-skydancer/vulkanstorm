from pathlib import Path
import importlib.util,shutil,subprocess,tempfile,unittest,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
HERE=Path(__file__).resolve().parent
def production_source():
 return (ROOT/'indra/newview/vsobjectselection.cpp').read_text(encoding='utf-8')
class Queries(unittest.TestCase):
 def test_reference_root_individual_group_validity_and_retirement(self):
  source=production_source();sigs=['std::optional<VSWorldScene::ObjectHandle> VSObjectSelection::firstRootObject(', 'S32 VSObjectSelection::rootObjectCount(', 'S32 VSObjectSelection::objectCount(', 'bool VSObjectSelection::selectionAllPCode(', 'bool VSObjectSelection::selectGetGroup(']
  fixture=(HERE/'selection_queries_fixture.cpp').read_text().replace('METHODS','\n'.join(method(source,s)for s in sigs))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'test.cpp').write_text(fixture);exe=p/'test.exe';built=subprocess.run([compiler,'-std=c++17',str(p/'test.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr);ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
