"""Compile actual parcel factories/postBuild and check all shipped tab names."""
from pathlib import Path
import re,shutil,subprocess,tempfile,unittest,xml.etree.ElementTree as ET
from native_source_fixture import ROOT,method

class LandFactories(unittest.TestCase):
 def test_every_real_specialized_panel_is_admitted_and_required(self):
  source=(ROOT/'indra/newview/llfloaterland.cpp').read_text()
  admission=(ROOT/'indra/newview/vsstartupui.cpp').read_text()
  header=(ROOT/'indra/newview/llfloaterland.h').read_text()
  ctor=method(source,'LLFloaterLand::LLFloaterLand(')
  entries=re.findall(r'mFactoryMap\["([^"]+)"\] =\s*LLCallbackMap\((\w+), this\)',ctor)
  self.assertEqual(len(entries),9)
  xml=ET.parse(ROOT/'indra/newview/skins/default/xui/en/floater_about_land.xml')
  tabs=xml.getroot().find("tab_container[@name='landtab']")
  self.assertEqual({e.attrib['name'] for e in tabs.findall('panel')},{n for n,_ in entries})
  predicate=admission[admission.index('[](std::string_view name) { return\n            name == "land_general_panel"'):]
  predicate=predicate[:predicate.index('; },')+3]
  decl=[];calls=[];checks=[];types=[];bodies=[]
  for name,callback in entries:
   body=method(source,'void* LLFloaterLand::'+callback+'(')
   member,kind=re.search(r'self->(\w+) = new (\w+)\(',body).groups()
   self.assertRegex(header,rf'\b{member}\s*=\s*nullptr;')
   types.append(f'struct {kind}:LLPanel {{explicit {kind}(int&){{}}}};')
   decl.append(f'{kind}*{member}=nullptr;static void* {callback}(void*);')
   calls.append(f'owner.tab.panels["{name}"]=static_cast<LLPanel*>(LLFloaterLand::{callback}(&owner));')
   checks.append(f'auto saved_{member}=owner.{member};owner.{member}=nullptr;assert(!owner.postBuild());owner.{member}=saved_{member};owner.tab.panels["{name}"]=&wrong;assert(!owner.postBuild());owner.tab.panels["{name}"]=owner.{member};assert(owner.postBuild());')
   bodies.append(body)
  code="""#include <cassert>
#include <map>
#include <string>
#include <string_view>
#include <sstream>
struct LLPanel {virtual ~LLPanel()=default;};
struct LLTabContainer:LLPanel {std::map<std::string,LLPanel*>panels;int selected=-1;LLPanel*getPanelByName(std::string name){return panels[name];}void selectTab(int value){selected=value;}};
namespace boost {template<class...T>int bind(T...){return 1;}}
constexpr int _2=2;
#define LL_WARNS(x) std::ostringstream()
#define LL_ENDL std::endl
"""+'\n'.join(types)+"""
struct LLFloaterLand {LLTabContainer tab;bool missingTab=false;LLTabContainer*mTabLand=nullptr;int mParcel=0,mVisibilityConnection=0;static constexpr int sLastTab=3;
template<class T>T*findChild(const char*,bool){return missingTab?nullptr:&tab;}void onVisibilityChanged(int){}int setVisibleCallback(int){return 1;}bool postBuild();
"""+'\n'.join(decl)+'};\n'+'\n'.join(bodies)+'\n'+method(source,'bool LLFloaterLand::postBuild()')+'\nint main(){auto admitted='+predicate+';'
  code+='assert(!admitted("land_unreviewed_panel"));'+''.join(f'assert(admitted("{name}"));' for name,_ in entries)
  code+='LLFloaterLand owner;assert(!owner.postBuild());'+''.join(calls)+'assert(owner.postBuild());assert(owner.tab.selected==3);LLPanel wrong;'+''.join(checks)+'owner.missingTab=true;assert(!owner.postBuild());}'
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'factories.cpp';exe=Path(tmp)/'factories.exe';cpp.write_text(code)
   result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
