"""Production panorama controller/readback/lifecycle and authenticated interest-list routes."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).parent/'fixtures/native_capture360'
class NativeCapture360(unittest.TestCase):
 def run_source(self,fixture,definitions):
  code=(HERE/fixture).read_text()
  for marker,source,signature in definitions:code=code.replace(marker,method((ROOT/'indra/newview'/source).read_text(),signature))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'capture.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],text=True,capture_output=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],text=True,capture_output=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
 def test_real_cube_controller_cardinal_frames_restore_state_and_refuse_partial_panorama(self):
  self.run_source('capture.cpp',[('CAPTURE','llfloater360capture.cpp','void LLFloater360Capture::capture360Images('),('FREEZE','llfloater360capture.cpp','void LLFloater360Capture::freezeWorld(')])
 def test_actual_native_offscreen_passes_are_exact_extent_and_no_present_ui_hud_or_stale_account(self):
  self.run_source('snapshot.cpp',[('SNAPSHOT','vssnapshot.cpp','bool vs_native_simple_snapshot(')])
 def test_actual_interest_list_neighbour_readiness_dedup_and_retired_callbacks(self):
  self.run_source('interest.cpp',[('INTEREST','vsnativecircuits.cpp','void VSNativeSession::updateInterestListMode(')])
if __name__=='__main__':unittest.main()
