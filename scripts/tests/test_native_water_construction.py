"""Real parameter-before-children lifecycle and all actual water commit bindings."""
from pathlib import Path
import os,re,shutil,subprocess,tempfile,unittest,xml.etree.ElementTree as ET
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class WaterConstruction(unittest.TestCase):
 def test_virtual_enable_before_children_postbuild_and_real_property_commits(self):
  folder=Path(os.environ['VS_WATER_CONSTRUCTION_SOURCE'])if 'VS_WATER_CONSTRUCTION_SOURCE'in os.environ else ROOT/'indra/newview'
  source=(folder/'llpaneleditwater.cpp').read_text();header=(folder/'llpaneleditwater.h').read_text()
  constants='\n'.join(re.findall(r'    const std::string\s+FIELD_WATER_[^;]+;',source))
  state=re.search(r'bool mChildrenReady\s*=\s*false;',header);self.assertIsNotNone(state)
  signatures=['bool LLPanelSettingsWaterMainTab::postBuild(', 'void LLPanelSettingsWaterMainTab::setEnabled(']+['void LLPanelSettingsWaterMainTab::'+name+'('for name in ['onFogDensityChanged','onFogUnderWaterChanged','onFresnelScaleChanged','onFresnelOffsetChanged','onScaleAboveChanged','onScaleBelowChanged','onBlurMultipChanged']]
  fixture=(HERE/'native_water_construction_fixture.cpp').read_text().replace('CONSTANTS',constants).replace('CHILD_STATE',state[0]).replace('METHODS','\n'.join(method(source,sig)for sig in signatures))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'water.cpp';exe=Path(tmp)/'water.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_all_shipped_water_panels_define_every_real_bound_control(self):
  source=(ROOT/'indra/newview/llpaneleditwater.cpp').read_text();fields=set(re.findall(r'const std::string\s+FIELD_WATER_\w+\("([^"]+)"\)',source))
  self.assertGreaterEqual(len(fields),14)
  for filename in ['panel_settings_water.xml','panel_fs_settings_water.xml']:
   reference=ET.parse(ROOT/'indra/newview/skins/default/xui/en'/filename).getroot();names={n.get('name')for n in reference.iter()};self.assertFalse(fields-names)
   for path in(ROOT/'indra/newview/skins').rglob(filename):
    overlay=ET.parse(path).getroot()
    with self.subTest(path=path.relative_to(ROOT).as_posix()):self.assertFalse({n.get('name')for n in overlay.iter()if n.get('name')}-names)
if __name__=='__main__':unittest.main()
