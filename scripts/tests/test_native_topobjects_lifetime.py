"""Actual TopObjects request/result retirement and complete row-consumer census."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class TopObjectsLifetime(unittest.TestCase):
 def source(self):
  folder=Path(os.environ['VS_TOPOBJECTS_SOURCE'])if 'VS_TOPOBJECTS_SOURCE'in os.environ else ROOT/'indra/newview'
  return(folder/'llfloatertopobjects.cpp').read_text()
 def test_real_request_result_epoch_close_crossing_account_and_permissions(self):
  source=self.source();signatures=['std::function<bool()> LLFloaterTopObjects::requestGuard(', 'bool LLFloaterTopObjects::modelCurrent(', 'void LLFloaterTopObjects::retireReply(', 'void LLFloaterTopObjects::onClose(', 'void LLFloaterTopObjects::beginReply(']
  fixture=(HERE/'native_topobjects_lifetime_fixture.cpp').read_text().replace('METHODS','\n'.join(method(source,sig)for sig in signatures))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'lifetime.cpp';exe=Path(tmp)/'lifetime.exe';cpp.write_text(fixture)
   build=subprocess.run([compiler,'-std=c++17','-DVS_NATIVE_VULKAN=1',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
 def test_every_row_consumer_uses_original_model_intent(self):
  source=self.source()
  for name in ['updateSelectionInfo','returnObjects','onReturnAll','showBeacon','onTeleportToObject','onKick','onProfile','onScriptInfo','onAvatarCheck']:
   body=method(source,'void LLFloaterTopObjects::'+name+'(').split('{',1)[1].lstrip();self.assertTrue(body.startswith('if(!modelCurrent())return;'),name)
  self.assertIn('modelCurrent() &&',method(source,'void LLFloaterTopObjects::onSelectionChanged('))
  for name in ['handle_land_reply','onRefresh']:
   self.assertIn('findTypedInstance<LLFloaterRegionInfo>',method(source,'void LLFloaterTopObjects::'+name+'('))
if __name__=='__main__':unittest.main()
