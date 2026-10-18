"""Actual shared connected UI preference, tab and experience-report controllers."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).resolve().parent/'fixtures/native_connected_policy'
class NativeConnectedPolicy(unittest.TestCase):
 def run_cpp(self,code):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'controller.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   built=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_bubble_console_suppression_uses_same_agent_busy_and_saved_policy(self):
  source=(ROOT/'indra/newview/fsconsoleutils.cpp').read_text()
  prefix=method(source,'bool FSConsoleUtils::ProcessChatMessage(').split('    std::string console_chat;')[0]+'    return false;\n}'
  self.run_cpp((FIXTURES/'bubbles_fixture.cpp').read_text().replace('METHOD',prefix))
 def test_console_preference_removes_real_nearby_toasts_without_creating_retired_channel(self):
  source=(ROOT/'indra/newview/fsfloaternearbychat.cpp').read_text()
  self.run_cpp((FIXTURES/'nearby_fixture.cpp').read_text().replace('REMOVE',method(source,'void FSFloaterNearbyChat::removeScreenChat(')).replace('UPDATE',method(source,'void FSFloaterNearbyChat::updateFSUseNearbyChatConsole(')))
 def test_actual_nearby_tab_restore_preserves_torn_off_layout_and_current_host(self):
  source=(ROOT/'indra/newview/fsfloaterimcontainer.cpp').read_text()
  self.run_cpp((FIXTURES/'tabs_fixture.cpp').read_text().replace('METHOD',method(source,'void FSFloaterIMContainer::initTabs(')))
 def test_experience_report_actions_keep_exact_profile_and_selected_event_identity(self):
  profile=(ROOT/'indra/newview/llfloaterexperienceprofile.cpp').read_text();log=(ROOT/'indra/newview/llpanelexperiencelog.cpp').read_text()
  self.run_cpp((FIXTURES/'experience_fixture.cpp').read_text().replace('PROFILE',method(profile,'void LLFloaterExperienceProfile::onReportExperience(')).replace('LOG',method(log,'void LLPanelExperienceLog::onReportExperience(')))
if __name__=='__main__':unittest.main()
