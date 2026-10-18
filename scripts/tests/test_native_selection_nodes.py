"""Execute actual production node methods against current owner/budget/transport seams."""
from pathlib import Path
import importlib.util,shutil,subprocess,tempfile,unittest,sys
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'scripts/tests'))
from native_source_fixture import method
def proposed():return (ROOT/'indra/newview/vsobjectselection.cpp').read_text()
class Nodes(unittest.TestCase):
 def test_actual_face_family_budget_reentry_and_account_retirement(self):
  source=proposed();signatures=['std::optional<VSObjectSelection::NodeMetadata> VSObjectSelection::node(', 'bool VSObjectSelection::contains(', 'bool VSObjectSelection::selectOnly(', 'bool VSObjectSelection::selectFamily(', 'bool VSObjectSelection::deselectOnly(', 'bool VSObjectSelection::deselectAll(', 'bool VSObjectSelection::deselectFamily(', 'std::shared_ptr<const void> VSObjectSelection::retainSelection(', 'bool VSObjectSelection::setForceSelection(', 'bool VSObjectSelection::setAllowSelectAvatar(', 'bool VSObjectSelection::allowSelectAvatar(', 'void VSObjectSelection::promoteSelectionToRoot(', 'void VSObjectSelection::demoteSelectionToIndividuals(', 'void VSObjectSelection::reset(']
  methods='\n'.join(method(source,sig)for sig in signatures)
  fixture=(HERE/'native_selection_nodes_fixture.cpp').read_text().replace('METHODS',methods).replace('CURRENT',method(source,'bool current(const VSWorldScene::ObjectHandle& token)'))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   folder=Path(tmp);cpp=folder/'nodes.cpp';exe=folder/'nodes.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
