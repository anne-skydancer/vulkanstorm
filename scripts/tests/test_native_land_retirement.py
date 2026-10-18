"""Exercise actual shared registry, queued destruction and parcel-observer retirement."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,HERE,method
class LandRetirement(unittest.TestCase):
 def test_real_deferred_and_immediate_registry_destruction_release_observers(self):
  sources={name:(ROOT/name).read_text(encoding='utf-8-sig') for name in (
   'indra/llcommon/llmortician.cpp','indra/llui/llfloater.cpp','indra/llui/llfloaterreg.cpp','indra/newview/llfloaterbuyland.cpp')}
  methods='\n'.join(method(sources[path],signature) for path,signature in (
   ('indra/llcommon/llmortician.cpp','LLMortician::~LLMortician()'),
   ('indra/llcommon/llmortician.cpp','void LLMortician::updateClass()'),
   ('indra/llcommon/llmortician.cpp','void LLMortician::die()'),
   ('indra/llui/llfloaterreg.cpp','LLFloater* LLFloaterReg::removeInstance('),
   ('indra/llui/llfloaterreg.cpp','bool LLFloaterReg::destroyInstance('),
   ('indra/llui/llfloater.cpp','void LLFloater::destroy()'),
   ('indra/newview/llfloaterbuyland.cpp','LLFloaterBuyLandUI::~LLFloaterBuyLandUI()'),
   ('indra/newview/llfloaterbuyland.cpp','void LLFloaterBuyLandUI::onClose(')))
  replay=(ROOT/'indra/newview/vsnativeimreplay.cpp').read_text(encoding='utf-8-sig')
  self.assertIn('if(landRetirementPending)',replay)
  pending=replay[replay.index('if(landRetirementPending)'):replay.index('if(!(landDialogCheck&1))')]
  self.assertIn('require(landDialog.isDead()',pending)
  self.assertLess(pending.index('require(landDialog.isDead()'),pending.index('landDialogParcel.reset();'))
  frame=method((ROOT/'indra/newview/llappviewer.cpp').read_text(encoding='utf-8-sig'),'bool LLAppViewer::frameNativeVulkanLogin()')
  self.assertLess(frame.index('LLMortician::updateClass();'),frame.index('vs_native_replay_tick('))
  fixture=(HERE/'native_land_retirement_fixture.cpp').read_text().replace('METHODS',methods)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'retire.cpp';exe=Path(tmp)/'retire.exe';cpp.write_text(fixture)
   result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
