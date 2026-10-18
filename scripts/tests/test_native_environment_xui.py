"""Qualify all shipped overlays and preserve existing translated wording."""
from pathlib import Path
import html,re,unittest,xml.etree.ElementTree as ET
from native_source_fixture import ROOT
FILES=['floater_about_land.xml','floater_url_entry.xml','floater_ban_duration.xml',
 'floater_edit_ext_day_cycle.xml','floater_settings_picker.xml','floater_pick_track.xml',
 'panel_region_environment.xml','panel_region_experiences.xml','panel_experience_list_editor.xml',
 'panel_settings_water.xml','panel_settings_sky_atmos.xml','panel_settings_sky_clouds.xml',
 'panel_settings_sky_sunmoon.xml','panel_settings_sky_density.xml','menu_save_settings.xml']
def defects(expected,actual):
 if (expected.tag,expected.get('name'))!=(actual.tag,actual.get('name')):return True
 for child in actual:
  hits=[n for n in expected if (n.tag,n.get('name'))==(child.tag,child.get('name'))]
  if len(hits)!=1 or defects(hits[0],child):return True
 return False
class EnvironmentXUI(unittest.TestCase):
 def test_every_shipped_overlay_targets_real_reference_nodes(self):
  changes={};skins=ROOT/'indra/newview/skins';count=0
  for name in FILES:
   base=skins/'default/xui/en'/name
   self.assertTrue(base.exists(),str(base))
   reference=ET.parse(base).getroot()
   for path in skins.rglob(name):
    relative=path.relative_to(ROOT).as_posix();original=path.read_text(encoding='utf-8-sig')
    current=changes.get(relative,original);count+=1
    with self.subTest(path=relative):
     if path!=base:self.assertFalse(defects(reference,ET.fromstring(current)))
  self.assertGreater(count,len(FILES)) # Enumerate every shipped skin/language overlay, without sampling.
if __name__=='__main__':unittest.main()
