"""EEP properties use real panel callbacks; global bindings must actually exist."""
from pathlib import Path
import unittest,xml.etree.ElementTree as ET,os
from native_source_fixture import ROOT
class EnvironmentBindings(unittest.TestCase):
 def test_every_shipped_environment_binding_names_a_real_setting(self):
  names=set()
  for path in(ROOT/'indra/newview/app_settings').glob('settings*.xml'):
   node=ET.parse(path).getroot().find('map')
   if node is not None:names.update(child.text for child in node.findall('key'))
  count=0;retained=set();proposed=Path(os.environ['VS_ENVIRONMENT_BINDING_SOURCE'])if 'VS_ENVIRONMENT_BINDING_SOURCE'in os.environ else None
  for path in(ROOT/'indra/newview/skins').rglob('*.xml'):
   if not any(k in path.name for k in ['settings_sky','settings_water','fixedenvironment','region_environment','edit_ext_day','settings_edit','settings_picker']):continue
   count+=1;target=proposed/path.relative_to(ROOT)if proposed and(proposed/path.relative_to(ROOT)).exists()else path
   for node in ET.parse(target).getroot().iter():
    control=node.get('control_name')
    if control:
     with self.subTest(path=path.relative_to(ROOT).as_posix(),control=control):self.assertIn(control,names)
     retained.add(control)
  self.assertGreater(count,100);self.assertTrue({'sunbeacon','moonbeacon'}<=retained)
 def test_removed_property_bindings_retain_real_values_and_shared_commit_endpoints(self):
  controller=(ROOT/'indra/newview/llpaneleditwater.cpp').read_text()
  # The actual controllers own these property values; removing an invalid global
  # alias must never remove a widget or its normal commit route.
  for name in ['water_scale_above','water_scale_below','water_blur_multip']:
   self.assertIn(name,controller)
  for filename in ['panel_fs_settings_water.xml','panel_settings_water.xml']:
   path=ROOT/'indra/newview/skins/default/xui/en'/filename
   proposed=Path(os.environ['VS_ENVIRONMENT_BINDING_SOURCE'])if 'VS_ENVIRONMENT_BINDING_SOURCE'in os.environ else None
   target=proposed/path.relative_to(ROOT)if proposed and(proposed/path.relative_to(ROOT)).exists()else path
   tree=ET.parse(target).getroot()
   for name in ['water_scale_above','water_scale_below','water_blur_multip']:
    nodes=[node for node in tree.iter()if node.get('name')==name];self.assertEqual(len(nodes),1);self.assertNotIn('control_name',nodes[0].attrib)
if __name__=='__main__':unittest.main()
