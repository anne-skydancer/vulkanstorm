"""Frozen shared world-billboard geometry and native tag owner contracts."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).resolve().parent/'fixtures/native_name_tags'
class NativeNameTags(unittest.TestCase):
 def run_cpp(self,text):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'name_tags.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(text)
   built=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
   self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
 def test_frozen_gl_3d_image_geometry_and_uv_order(self):
  kernel=(ROOT/'indra/llrender/vsimage3dgeometry.h').read_text()
  kernel='\n'.join(line for line in kernel.splitlines()if not line.startswith('#'))
  text=(FIXTURES/'image3d_fixture.cpp').read_text().replace('REFERENCE',(FIXTURES/'image3d_gl_reference.cpp').read_text()).replace('CURRENT',kernel)
  self.run_cpp(text)
 def test_frozen_gl_rigging_envelope_threshold_joint_mapping_and_lifetime(self):
  from native_source_fixture import method
  source=(ROOT/'indra/newview/vsavatarrigginginfo.cpp').read_text()
  current=method(source,'std::shared_ptr<const VSAvatarRiggingInfo> VSAvatarRiggingInfo::create(')
  text=(FIXTURES/'rigging_fixture.cpp').read_text().replace('REFERENCE',(FIXTURES/'rigging_gl_reference.cpp').read_text()).replace('CURRENT',current)
  self.run_cpp(text)
 def test_actual_tag_retirement_releases_visible_owner_without_affecting_peer(self):
  from native_source_fixture import method
  current=method((ROOT/'indra/newview/llhudnametag.cpp').read_text(),'void LLHUDNameTag::markDead()')
  text=(FIXTURES/'tag_retirement_fixture.cpp').read_text().replace('CURRENT',current)
  self.run_cpp(text)
 def test_actual_joint_space_union_current_pose_budget_refusal_and_retirement(self):
  from native_source_fixture import method
  source=method((ROOT/'indra/newview/vsworldavatar.cpp').read_text(),'bool VSWorldAvatar::extendRiggingBounds(')
  self.run_cpp((FIXTURES/'aggregate_fixture.cpp').read_text().replace('CURRENT',source))
 def test_actual_immutable_publication_max_lod_refusal_and_geometry_retirement(self):
  from native_source_fixture import method
  source=method((ROOT/'indra/newview/vsworldscene.cpp').read_text(),'auto rigging=[&]')
  self.run_cpp((FIXTURES/'publication_fixture.cpp').read_text().replace('CURRENT',source))
 def test_actual_picking_requires_successful_draw_submission(self):
  from native_source_fixture import method
  source=method((ROOT/'indra/newview/llhudnametag.cpp').read_text(),'void LLHUDNameTag::finishNative(')
  self.run_cpp((FIXTURES/'presentation_fixture.cpp').read_text().replace('CURRENT',source))
 def test_actual_name_parser_accepts_original_tokens_without_interning_unknown_fields(self):
  from native_source_fixture import method
  source=method((ROOT/'indra/newview/vsavatarnametag.cpp').read_text(),'static std::unique_ptr<LLNameValue> parseNameValue(')
  self.run_cpp((FIXTURES/'name_value_fixture.cpp').read_text().replace('CURRENT',source))
 def test_actual_native_preferences_and_simulator_name_clamp(self):
  from native_source_fixture import method
  source=method((ROOT/'indra/newview/vsavatarnametag.cpp').read_text(),'void VSAvatarNameTag::refreshPreferences(')
  self.run_cpp((FIXTURES/'preferences_fixture.cpp').read_text().replace('CURRENT',source))
if __name__=='__main__':unittest.main()
