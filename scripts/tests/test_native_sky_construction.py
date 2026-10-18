"""Execute actual LL and PhotoTools EEP bindings, construction order, and all XUI."""
from pathlib import Path
import os,re,shutil,subprocess,tempfile,unittest,xml.etree.ElementTree as ET
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class SkyConstruction(unittest.TestCase):
 def folder(self):return Path(os.environ['VS_SKY_CONSTRUCTION_SOURCE'])if 'VS_SKY_CONSTRUCTION_SOURCE'in os.environ else ROOT/'indra/newview'
 def test_real_postbuild_enable_optional_quaternion_controls_and_callbacks(self):
  source=(self.folder()/'llpaneleditsky.cpp').read_text();header=(self.folder()/'llpaneleditsky.h').read_text();self.assertIn('bool mChildrenReady=false;',header)
  constants='\n'.join(re.findall(r'    const (?:std::string|F32)\s+[^;]+;',source))
  classes=[];methods=[];other=[]
  names=re.findall(r'const std::string\s+(FIELD_SKY_\w+|FIELD_REFLECTION_\w+)\(',source);allfields='{'+','.join(names)+'}'
  for cls in ['LLPanelSettingsSkyAtmosTab','LLPanelSettingsSkyCloudTab','LLPanelSettingsSkySunMoonTab','LLPanelSettingsSkyDensityTab']:
   post=method(source,'bool '+cls+'::postBuild(');methods+=[post,method(source,'void '+cls+'::setEnabled(')]
   callbacks=set(re.findall(r'\{\s*(on\w+)\(\);\s*\}',post))
   actual={'onSunAzimElevChanged','onMoonAzimElevChanged','onSunRotationChanged','onMoonRotationChanged'}if cls.endswith('SunMoonTab')else set()
   declarations=''.join('void '+n+'();'if n in actual else 'void '+n+'(){}'for n in sorted(callbacks))
   if actual:
    declarations+='LLVirtualTrackball*mSunRotation=nullptr,*mMoonRotation=nullptr;LLView*mSunLayout=nullptr,*mMoonLayout=nullptr;void refresh();'
    methods+=[method(source,'void '+cls+'::refresh(')]+[method(source,'void '+cls+'::'+n+'(')for n in sorted(actual)]
   else:
    declarations+='void refresh(){++refreshed;setEnabled(editable);}'
    other.append('{'+cls+' p;LLPanelSettingsSky*b=&p;b->setEnabled(false);p.controls["decoration"]={};b->setEnabled(true);assert(p.missing==0);for(const auto&n:'+allfields+')p.controls[n]={};assert(p.postBuild()&&p.mChildrenReady&&p.missing==0&&p.refreshed==1);}')
   classes.append('struct '+cls+':LLPanelSettingsSky{bool postBuild();void setEnabled(bool)override;'+declarations+'};')
  methods.append(method((ROOT/'indra/llui/llxuiparser.cpp').read_text(),'bool LLSimpleXUIParser::readBoolValue('))
  fixture=(HERE/'native_sky_construction_fixture.cpp').read_text().replace('CONSTANTS',constants).replace('CLASSES','\n'.join(classes)).replace('METHODS','\n'.join(methods)).replace('ALL_FIELDS',allfields).replace('OTHER_TABS','\n'.join(other))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'sky.cpp';exe=Path(tmp)/'sky.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_all_shipped_sky_layouts_retain_real_alternate_bindings(self):
  source=(self.folder()/'llpaneleditsky.cpp').read_text();lookup=dict(re.findall(r'const std::string\s+(\w+)\("([^"]+)"\)',source))
  families={'atmos':'LLPanelSettingsSkyAtmosTab','clouds':'LLPanelSettingsSkyCloudTab','sunmoon':'LLPanelSettingsSkySunMoonTab','density':'LLPanelSettingsSkyDensityTab'}
  count=0
  for family,cls in families.items():
   for prefix in ['panel_settings_sky_','panel_fs_settings_sky_']:
    filename=prefix+family+'.xml';base=ROOT/'indra/newview/skins/default/xui/en'/filename
    if not base.exists():continue
    tree=ET.parse(base).getroot();names={n.get('name')for n in tree.iter()};post=method(source,'bool '+cls+'::postBuild(')
    needed={lookup[n]for n in re.findall(r'getChild<\w+>\((\w+)\)',post)}
    self.assertFalse(needed-names,(filename,needed-names))
    if family=='sunmoon':
     self.assertTrue({'sun_azimuth','sun_elevation','moon_azimuth','moon_elevation','sunbeacon','moonbeacon'}<=names)
     if prefix.startswith('panel_fs'):self.assertTrue({'P_Sun_Setting','P_Moon_Setting'}<=names);self.assertFalse({'sun_rotation','moon_rotation'}&names)
     else:self.assertTrue({'sun_rotation','moon_rotation','sun_layout','moon_layout'}<=names)
    for path in(ROOT/'indra/newview/skins').rglob(filename):
     overlay=ET.parse(path).getroot();count+=1
     with self.subTest(path=path.relative_to(ROOT).as_posix()):
      self.assertFalse({n.get('name')for n in overlay.iter()if n.get('name')}-names)
  self.assertGreater(count,60)
 def test_all_environment_logarithmic_parameters_use_parser_boolean_spelling(self):
  for path in(ROOT/'indra/newview/skins').rglob('*settings*sky*.xml'):
   proposed=self.folder()/path.relative_to(ROOT)
   selected=proposed if proposed.exists()else path
   for node in ET.parse(selected).getroot().iter():
    if 'logarithmic'in node.attrib:
     with self.subTest(path=path.relative_to(ROOT).as_posix()):self.assertIn(node.get('logarithmic'),('true','false'))

if __name__=='__main__':unittest.main()
