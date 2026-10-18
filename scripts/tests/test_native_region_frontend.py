"""Real Region panel-type admission and nonmanufacturing native view access."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class RegionFrontend(unittest.TestCase):
 def test_actual_private_type_and_all_eight_panel_accessors(self):
  folder=Path(os.environ['VS_REGION_FRONTEND_SOURCE'])if 'VS_REGION_FRONTEND_SOURCE'in os.environ else ROOT/'indra/newview'
  source=(folder/'llfloaterregioninfo.cpp').read_text();methods=[method(source,'bool LLFloaterRegionInfo::isNativeInternalType(')]
  for name,result in [('Estate','LLPanelEstateInfo'),('Access','LLPanelEstateAccess'),('Covenant','LLPanelEstateCovenant'),('General','LLPanelRegionGeneralInfo'),('Environment','LLPanelRegionEnvironment'),('RegionTerrain','LLPanelRegionTerrainInfo'),('Experiences','LLPanelRegionExperiences'),('OpenSettings','LLPanelRegionOpenSettingsInfo')]:methods.append(method(source,result+'* LLFloaterRegionInfo::getPanel'+name+'('))
  fixture=(HERE/'native_region_frontend_fixture.cpp').read_text().replace('METHODS','\n'.join(methods));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'view.cpp';exe=Path(tmp)/'view.exe';cpp.write_text(fixture)
   build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
