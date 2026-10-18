"""All shipped Region/Estate overlays must target the actual shared hierarchy."""
from pathlib import Path
import unittest,xml.etree.ElementTree as ET,os
from native_source_fixture import ROOT
FILES=['floater_region_info.xml','panel_region_estate.xml','panel_region_access.xml','panel_region_covenant.xml','panel_region_general.xml','panel_region_open_region_settings.xml','panel_region_terrain.xml','panel_region_terrain_texture_transform.xml','panel_region_environment.xml','panel_region_debug.xml','panel_region_experiences.xml','floater_region_restart_schedule.xml','floater_telehub.xml','floater_top_objects.xml']
def defects(expected,actual):
 if(expected.tag,expected.get('name'))!=(actual.tag,actual.get('name')):return True
 for child in actual:
  hits=[n for n in expected if(n.tag,n.get('name'))==(child.tag,child.get('name'))]
  if len(hits)!=1 or defects(hits[0],child):return True
 return False
class RegionXUI(unittest.TestCase):
 def test_every_shipped_skin_and_language_targets_real_named_controls(self):
  skins=ROOT/'indra/newview/skins';count=0
  proposed=Path(os.environ['VS_REGION_TEST_SOURCE'])if 'VS_REGION_TEST_SOURCE'in os.environ else None
  for filename in FILES:
   base=skins/'default/xui/en'/filename;reference=ET.parse(base).getroot()
   for path in skins.rglob(filename):
    source=proposed/path.relative_to(ROOT)if proposed and(proposed/path.relative_to(ROOT)).exists()else path
    count+=1
    if path==base:continue # Reference may define anonymous repeatable parameter/layout children.
    with self.subTest(path=path.relative_to(ROOT).as_posix()):self.assertFalse(defects(reference,ET.parse(source).getroot()))
  self.assertGreater(count,len(FILES))
if __name__=='__main__':unittest.main()
