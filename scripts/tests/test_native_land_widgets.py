"""Exercise optional Sell Land badges and every shipped layout's real controls."""
from pathlib import Path
import re,shutil,subprocess,tempfile,unittest,xml.etree.ElementTree as ET
from native_source_fixture import ROOT,method
class LandWidgets(unittest.TestCase):
 def test_optional_reference_badges_never_create_dummy_controls(self):
  source=(ROOT/'indra/newview/llfloatersellland.cpp').read_text()
  badge=method(source,'void LLFloaterSellLandUI::setBadge(')
  code=r'''#include <cassert>
#include <map>
#include <string>
struct LLUICtrl{std::string value;void setValue(std::string s){value=s;}};
struct LLFloaterSellLandUI{
enum Badge{BADGE_OK,BADGE_NOTE,BADGE_WARN,BADGE_ERROR};
std::map<std::string,LLUICtrl> controls;
template<class T>T*findChild(const char* name,bool){auto p=controls.find(name);return p==controls.end()?nullptr:&p->second;}
void setBadge(const char*,Badge);
};
'''+badge+r'''
int main(){LLFloaterSellLandUI floater;
for(const char* name:{"step_price","step_sell_to","step_sell_objects"})for(auto badge:{LLFloaterSellLandUI::BADGE_OK,LLFloaterSellLandUI::BADGE_NOTE,LLFloaterSellLandUI::BADGE_WARN,LLFloaterSellLandUI::BADGE_ERROR})floater.setBadge(name,badge);
assert(floater.controls.empty());floater.controls["step_price"]={};
floater.setBadge("step_price",LLFloaterSellLandUI::BADGE_OK);assert(floater.controls["step_price"].value=="badge_ok.j2c");
floater.setBadge("step_price",LLFloaterSellLandUI::BADGE_NOTE);assert(floater.controls["step_price"].value=="badge_note.j2c");
floater.setBadge("step_price",LLFloaterSellLandUI::BADGE_ERROR);assert(floater.controls["step_price"].value=="badge_warn.j2c");
assert(floater.controls.size()==1);
}'''
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'badge.cpp';exe=Path(tmp)/'badge.exe';cpp.write_text(code)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_every_shipped_sell_layout_keeps_all_actual_required_fields(self):
  source=(ROOT/'indra/newview/llfloatersellland.cpp').read_text()
  required=set(re.findall(r'getChild(?:View|<[^>]+>)\("([^"]+)"',source))
  skins=ROOT/'indra/newview/skins';base=skins/'default/xui/en/floater_sell_land.xml'
  reference=ET.parse(base).getroot();names={n.get('name') for n in reference.iter()}
  self.assertGreater(len(required),10);self.assertFalse(required-names)
  paths=list(skins.rglob('floater_sell_land.xml'));self.assertGreater(len(paths),1)
  for path in paths:
   with self.subTest(path=path.relative_to(ROOT).as_posix()):
    overlay=ET.parse(path).getroot()
    # Sparse translations inherit the real English layout. Every named overlay
    # target must resolve there; it cannot manufacture a parallel control tree.
    self.assertFalse({n.get('name') for n in overlay.iter() if n.get('name')}-names)
    self.assertEqual(reference.tag,overlay.tag)
if __name__=='__main__':unittest.main()
