"""Actual native material owner/callback source under refused and reentrant lifetimes."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeLegacyMaterialService(unittest.TestCase):
 def compile_run(self,code):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'material.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
 def test_prepared_put_budget_charge_and_account_circuit_source_epoch_cancellation(self):
  source=(ROOT/'indra/newview/vsmaterialassets.cpp').read_text();structs=source[source.index('struct VSMaterialAssets::Impl'):source.index('VSMaterialAssets::VSMaterialAssets(')]
  code=(Path(__file__).parent/'fixtures/native_face_edit/material_owner.cpp').read_text().replace('@@STRUCTS@@',structs)
  for key,sig in [('RESET','void VSMaterialAssets::reset('),('PREPARE','std::shared_ptr<VSMaterialAssets::PreparedPut> VSMaterialAssets::preparePut('),('PUT_PREPARED','bool VSMaterialAssets::put(const std::shared_ptr<PreparedPut>&'),('PUT','bool VSMaterialAssets::put(const VSWorldScene::ObjectHandle&')]:code=code.replace('@@'+key+'@@',method(source,sig))
  self.compile_run(code)
 def test_callback_detach_reentrant_same_id_account_retirement_and_successful_empty_region(self):
  source=(ROOT/'indra/newview/llmaterialmgr.cpp').read_text();state=source[source.index('struct LLMaterialMgr::NativeState'):source.index('void LLMaterialMgr::retireNativeAccount')]
  code=(Path(__file__).parent/'fixtures/native_face_edit/material_callbacks.cpp').read_text().replace('STATE',state).replace('IDLE',method(source,'void LLMaterialMgr::nativeIdle('));self.compile_run(code)
if __name__=='__main__':unittest.main()
