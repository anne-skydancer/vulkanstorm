"""Compile frozen original speaking-indicator output against shared production geometry."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).resolve().parent/'fixtures/native_voice_visualizer'
class NativeVoiceVisualizer(unittest.TestCase):
 def test_actual_avatar_owner_rejects_retired_source_before_rendering(self):
  source=(ROOT/'indra/newview/vsworldavatar.cpp').read_text()
  self.run_cpp((FIXTURES/'owner_fixture.cpp').read_text().replace('METHOD',method(source,'void VSWorldAvatar::appendVoice(')))
 def test_original_avatar_visibility_gesture_mute_and_morph_policy(self):
  policy=(ROOT/'indra/newview/vsavatarvoicepolicy.h').read_text();policy=policy[policy.index('struct VSAvatarVoicePolicy'):]
  frozen=(FIXTURES/'original_avatar_update.cpp').read_text().replace('LLVOAvatar::idleUpdateVoiceVisualizer','Avatar::frozenUpdate')
  lip=(FIXTURES/'original_avatar_lipsync.cpp').read_text().replace('LLVOAvatar::idleUpdateLipSync','Avatar::frozenLip')
  self.run_cpp((FIXTURES/'policy_fixture.cpp').read_text().replace('FROZEN_UPDATE',frozen).replace('FROZEN_LIP',lip).replace('POLICY',policy))
 def test_native_geometry_orientation_depth_atomic_refusal_reuse_and_retirement(self):
  source=(ROOT/'indra/newview/llvoicevisualizer.cpp').read_text()
  code=(FIXTURES/'publication_fixture.cpp').read_text().replace('METHOD',method(source,'void LLVoiceVisualizer::appendNative('))
  self.run_cpp(code)
 def test_original_and_shared_wave_geometry_timing_color_and_preferences(self):
  source=(ROOT/'indra/newview/llvoicevisualizer.cpp').read_text()
  constants=source[source.index('const F32   HEIGHT_ABOVE_HEAD'):source.index('//------------------------------------------------------------------\n// Initialize the statics')].replace('LLVoiceClient::OVERDRIVEN_POWER_LEVEL','.9f')
  constants='constexpr int NUM_VOICE_SYMBOL_WAVES=7;\n'+constants
  frozen=(FIXTURES/'original_render.cpp').read_text().replace('void LLVoiceVisualizer::render()','void Owner::frozen()')
  current=method(source,'template<class Painter> void LLVoiceVisualizer::renderSymbol(').replace('LLVoiceVisualizer::renderSymbol','Owner::renderSymbol')
  code=(FIXTURES/'geometry_fixture.cpp').read_text().replace('CONSTANTS',constants).replace('FROZEN',frozen).replace('CURRENT',current)
  self.run_cpp(code)
 def run_cpp(self,code):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'visualizer.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   built=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
