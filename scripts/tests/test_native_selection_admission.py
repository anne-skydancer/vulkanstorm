"""Minimal flags/type metadata preserves current owners without pose/string work."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class AdmissionTest(unittest.TestCase):
 def test_actual_metadata_ancestry_hud_orphan_and_identity_retirement(self):
  folder=Path(os.environ['VS_SELECTION_ADMISSION_SOURCE'])if 'VS_SELECTION_ADMISSION_SOURCE'in os.environ else ROOT/'indra/newview';source=(folder/'vsworldscene.cpp').read_text();body=method(source,'std::optional<VSWorldScene::ObjectAdmission> VSWorldScene::objectAdmission(')
  fixture=(HERE/'native_selection_admission_fixture.cpp').read_text().replace('\nMETHOD\n','\n'+body+'\n').replace('CURRENT',method(source,'bool VSWorldScene::current(const ObjectHandle& token)')).replace('SELECT_METHOD',method((folder/'vsobjectselection.cpp').read_text(),'VSObjectSelection::SelectionTypeState VSObjectSelection::selectionType('));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'metadata.cpp';exe=Path(tmp)/'metadata.exe';cpp.write_text(fixture);build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr);run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
 def test_actual_peer_matches_all_original_permission_cases_and_force_unavailable(self):
  folder=Path(os.environ['VS_SELECTION_ADMISSION_SOURCE'])if 'VS_SELECTION_ADMISSION_SOURCE'in os.environ else ROOT/'indra/newview'
  current=(folder/'vsobjectselection.cpp').read_text();shared='\n'.join(line for line in(ROOT/'indra/newview/vsselectionpolicy.h').read_text().splitlines()if not line.startswith('#'))
  fixture=(HERE/'native_selection_peer_fixture.cpp').read_text().replace('REFERENCE_METHOD',(ROOT/'scripts/tests/native_selection_gl_reference.cpp').read_text()).replace('SHARED',shared).replace('CURRENT_METHOD',method(current,'bool VSObjectSelection::canSelect('))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'peer.cpp';exe=Path(tmp)/'peer.exe';cpp.write_text(fixture);build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr);run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
