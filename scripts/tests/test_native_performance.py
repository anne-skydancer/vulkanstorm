"""Real completed-query coverage and account-owned native performance services."""
from pathlib import Path
import subprocess,shutil,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).parent/'fixtures/native_performance'
class NativePerformance(unittest.TestCase):
 def compile_run(self,code):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   source=Path(directory)/'performance.cpp';exe=source.with_suffix('.exe');source.write_text(code)
   p=subprocess.run([compiler,'-std=c++20','-I'+str(ROOT/'indra/newview'),str(source),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],cwd=directory,capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
 def test_one_physical_gpu_query_fans_out_without_polling_or_recycling_twice(self):
  self.compile_run((HERE/'queries.cpp').read_text())
 def test_current_attachment_measurement_ownership_budget_and_reparent_retirement(self):
  source=(ROOT/'indra/newview/vsworldscene.cpp').read_text()
  self.compile_run((HERE/'attachment.cpp').read_text().replace('METHOD',method(source,'std::shared_ptr<const VSWorldRenderer::AvatarTiming> VSWorldScene::attachmentTiming(')))
 def test_actual_persistence_retirement_saves_original_account_and_is_idempotent(self):
  source=(ROOT/'indra/newview/fsavatarrenderpersistence.cpp').read_text()
  names=['FSAvatarRenderPersistence::~FSAvatarRenderPersistence(','void FSAvatarRenderPersistence::init(','void FSAvatarRenderPersistence::retireAccount(','LLVOAvatar::VisualMuteSettings FSAvatarRenderPersistence::getAvatarRenderSettings(','void FSAvatarRenderPersistence::setAvatarRenderSettings(','void FSAvatarRenderPersistence::loadAvatarRenderSettings(','void FSAvatarRenderPersistence::saveAvatarRenderSettings(']
  self.compile_run((HERE/'persistence.cpp').read_text().replace('METHODS','\n'.join(method(source,name)for name in names)))
 def test_actual_shipped_menu_payloads_and_rejected_scene_or_wrong_actions(self):
  sources=[(ROOT/'indra/newview/fsfloaterperformance.cpp').read_text(),(ROOT/'indra/newview/fsfloateravatarrendersettings.cpp').read_text()]
  code=(HERE/'callbacks.cpp').read_text()
  code=code.replace('PERFORMANCE',method(sources[0],'bool FSFloaterPerformance::isNativeCallback(')).replace('EXCEPTIONS',method(sources[1],'bool FSFloaterAvatarRenderSettings::isNativeCallback('))
  self.compile_run(code)
if __name__=='__main__':unittest.main()
