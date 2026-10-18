"""Exercise the actual multi-stage LandResources controller with late replies."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'indra/newview/llfloaterscriptlimits.cpp'
class LandResources(unittest.TestCase):
 def test_actual_post_parallel_get_failures_and_retirement(self):
  source=SOURCE.read_text()
  names=['void LLPanelScriptLimitsRegionMemory::retireNativeRequests()', 'bool LLPanelScriptLimitsRegionMemory::requestNativeResources(', 'void LLPanelScriptLimitsRegionMemory::requestNativeResourceStage(', 'void LLPanelScriptLimitsRegionMemory::finishNativeResourceStage(']
  methods='\n'.join(method(source,name) for name in names)
  fixture=Path(__file__).with_name('native_land_resources_fixture.cpp').read_text().replace('METHODS',methods)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as folder:
   cpp=Path(folder)/'resources.cpp';exe=Path(folder)/'resources.exe';cpp.write_text(fixture)
   result=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
