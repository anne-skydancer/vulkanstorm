"""Shared Aurora optional Teen binding and real Enforce Apply/payload contract."""
from pathlib import Path
import os,re,shutil,subprocess,tempfile,unittest,xml.etree.ElementTree as ET
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class AuroraContract(unittest.TestCase):
 def test_actual_shared_refresh_bind_commit_and_complete_update_without_dummy(self):
  p=Path(os.environ.get('VS_AURORA_SOURCE',ROOT/'indra/newview/llfloaterregioninfo.cpp'));s=p.read_text();methods='\n'.join(method(s,sig)for sig in ['void LLPanelRegionInfo::initCtrl(', 'void LLPanelRegionInfo::onChangeAnything(', 'void LLPanelRegionOpenSettingsInfo::onChangeAnything(', 'void LLPanelRegionInfo::enableButton(', 'bool LLPanelRegionOpenSettingsInfo::refreshFromRegion(', 'bool LLPanelRegionOpenSettingsInfo::postBuild(', 'void LLPanelRegionOpenSettingsInfo::onClickOrs('])
  base=ET.parse(ROOT/'indra/newview/skins/default/xui/en/panel_region_open_region_settings.xml').getroot();names={e.get('name')for e in base.iter()if e.get('name')};getters=set(re.findall(r'regionlimits->(\w+)\(',methods));getters.discard('getEnableTeenMode');getters='\n'.join('double '+name+'()const{return 0;}'for name in sorted(getters))
  fixture=(HERE/'native_region_aurora_fixture.cpp').read_text().replace('GETTERS',getters).replace('METHODS',methods).replace('NAMES','\n'.join(' view.controls["'+name+'"];'for name in sorted(names)))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'aurora.cpp';exe=Path(tmp)/'aurora.exe';cpp.write_text(fixture);build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr);run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
 def test_all_overlay_control_names_are_supported_by_real_reference_layout(self):
  root=ROOT/'indra/newview/skins';files=list(root.rglob('panel_region_open_region_settings.xml'));self.assertTrue(files)
  base=root/'default/xui/en/panel_region_open_region_settings.xml';names={e.get('name')for e in ET.parse(base).getroot().iter()if e.get('name')}
  self.assertIn('enforce_max_build',names)
  for p in files:
   with self.subTest(path=p.relative_to(ROOT)):
    for e in ET.parse(p).getroot().iter():
     if e.get('name'):self.assertIn(e.get('name'),names)
if __name__=='__main__':unittest.main()
