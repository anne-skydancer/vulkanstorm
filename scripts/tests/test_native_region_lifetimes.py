"""Request-owned region mutations, covenant assets and picker cancellation."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest,os
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class RegionLifetime(unittest.TestCase):
 def source(self):
  # Proposal validation uses an explicit off-tree override. CI always reads the
  # actual checked-out production source; it never reapplies an editing script.
  folder=Path(os.environ['VS_REGION_TEST_SOURCE'])if 'VS_REGION_TEST_SOURCE'in os.environ else ROOT/'indra/newview'
  return (folder/'llfloaterregioninfo.cpp').read_text()
 def execute(self,code):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'region.cpp';exe=Path(tmp)/'region.exe';cpp.write_text(code)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_actual_covenant_completion_account_region_item_asset_and_authority(self):
  source=self.source();request='struct RegionCovenantRequest'+source.split('struct RegionCovenantRequest',1)[1].split('\n};',1)[0]+'\n};'
  fixture=(HERE/'native_region_lifetime_fixture.cpp').read_text().replace('GUARD',method(source,'std::function<bool()> region_info_guard(')).replace('\nREQUEST\n','\n'+request+'\n').replace('CALLBACK',method(source,'void LLPanelEstateCovenant::onLoadComplete('));self.execute(fixture)
 def test_actual_picker_payload_owned_until_callback_or_cancel(self):
  source=self.source();header=(ROOT/'indra/newview/vsnativeim.h').read_text();a=header.index('template<class Callback>');b=header.index('// Notification responders',a)
  fixture=(HERE/'native_estate_picker_fixture.cpp').read_text().replace('WRAPPERS',header[a:b]).replace('PICKER',method(source,'bool LLPanelEstateAccess::accessAddCore2('));self.execute(fixture)
 def test_shared_region_controller_reads_authentic_model_without_transient_message(self):
  source=self.source();fixture=(HERE/'native_region_model_ui_fixture.cpp').read_text().replace('CONTROLLER',method(source,'void LLFloaterRegionInfo::processRegionInfo('));self.execute(fixture)
 def test_scheduler_real_guard_retirement_and_time_bounds(self):
  folder=Path(os.environ['VS_REGION_TEST_SOURCE'])if 'VS_REGION_TEST_SOURCE'in os.environ else ROOT/'indra/newview'
  source=(folder/'llfloaterregionrestartschedule.cpp').read_text()
  signatures=['std::function<bool()> LLFloaterRegionRestartSchedule::requestGuard(', 'void LLFloaterRegionRestartSchedule::onClose(', 'void LLFloaterRegionRestartSchedule::onCommitHours(', 'void LLFloaterRegionRestartSchedule::onCommitMinutes(']
  fixture=(HERE/'native_region_schedule_fixture.cpp').read_text().replace('METHODS','\n'.join(method(source,sig)for sig in signatures));self.execute(fixture)
if __name__=='__main__':unittest.main()
