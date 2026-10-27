"""Exercise the shared inventory settings controller and account retirement."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


class InventorySettingsControllerTest(unittest.TestCase):
    def test_packaged_controller_without_optional_ok_and_retired_account(self):
        root = Path(__file__).resolve().parents[2]
        registration = (root / 'indra/newview/llviewerfloaterreg.cpp').read_text()
        self.assertRegex(registration, r'LLFloaterReg::add\("inventory_settings", "floater_inventory_settings.xml"')
        admission = (root / 'indra/newview/vsstartupui.cpp').read_text()
        self.assertIn('t == typeid(LLFloaterInventorySettings)', admission)
        self.assertIn('name == "inventory_settings"', admission)
        xui = ET.parse(root / 'indra/newview/skins/default/xui/en/floater_inventory_settings.xml').getroot()
        controls = {node.get('name'): node for node in xui.iter() if node.get('name')}
        self.assertNotIn('ok_btn', controls)
        color = controls['favorites_swatch']
        self.assertEqual(color.find('color_swatch.init_callback').get('function'), 'ScriptPref.getUIColor')
        self.assertEqual(color.find('color_swatch.commit_callback').get('function'), 'ScriptPref.applyUIColor')
        parameter = color.find('color_swatch.commit_callback').get('parameter')
        self.assertEqual(controls['favorites_color'].get('control_name'), 'InventoryFavoritesColorText')
        source = (root / 'indra/newview/llfloaterinventorysettings.cpp').read_text()
        methods = source[source.index('bool LLFloaterInventorySettings::postBuild()'):]
        reset = (root / 'indra/newview/vsnativeim.cpp').read_text()
        loop = reset[reset.index('    for (const char* name : {', reset.index('// Panels retain pointers')):]
        loop = loop[:loop.index('    // Retire conversation')]
        fixture = r'''
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
struct LLSD {std::string text;float number=0;LLSD()=default;LLSD(const char* s):text(s){}LLSD(float n):number(n){}
 bool asBoolean()const{return number!=0;}std::string asString()const{return text;}operator bool()const{return asBoolean();}};
struct LLColor4 {float value;LLColor4(float f=0):value(f){}LLColor4(const LLSD& s):value(s.number){}};
namespace boost {using std::bind;namespace placeholders {using std::placeholders::_1;using std::placeholders::_2;}}
using namespace boost::placeholders;
struct LLUICtrl {LLSD value;bool enabled=true;std::function<void()> callback;
 LLSD getValue(){return value;}void setEnabled(bool b){enabled=b;}
 template<class F>void setCommitCallback(F f){callback=f;}};
struct LLButton:LLUICtrl {};
struct LLColorSwatchCtrl:LLUICtrl {LLColor4 original;void setOriginal(LLColor4 c){original=c;}};
struct Settings {bool color=false;bool getBOOL(const char*){return color;}}gSavedSettings;
struct LLUIColorTable {std::map<std::string,LLColor4> colors;static LLUIColorTable& instance(){static LLUIColorTable t;return t;}
 void setColor(std::string name,LLColor4 value){colors[name]=value;}LLColor4 getColor(std::string name){return colors.at(name);}};
struct Host {void removeFloater(void*){}};
struct LLFloater {std::map<std::string,std::unique_ptr<LLUICtrl>> children;bool closed=false;virtual ~LLFloater()=default;
 template<class T>T* findChild(std::string name){auto i=children.find(name);return i==children.end()?nullptr:static_cast<T*>(i->second.get());}
 template<class T>T* getChild(std::string name){auto* p=findChild<T>(name);if(!p)throw std::runtime_error("missing required control: "+name);return p;}
 void closeFloater(bool){closed=true;}Host* getHost(){return nullptr;}LLSD getKey(){return {};}};
int retired=0;
struct OmnifilterEngine {std::vector<std::string> mLog;static bool instanceExists(){return true;}static OmnifilterEngine& instance(){static OmnifilterEngine e;return e;}};
struct LLFloaterInventorySettings:LLFloater {~LLFloaterInventorySettings(){++retired;}
 bool postBuild();void updateColorSwatch();void applyUIColor(LLUICtrl*,const LLSD&);void getUIColor(LLUICtrl*,const LLSD&);};
METHODS
struct LLFloaterReg {static std::map<std::string,std::vector<std::unique_ptr<LLFloater>>> owners;
 static std::vector<LLFloater*>getFloaterList(std::string name){std::vector<LLFloater*> result;for(auto& p:owners[name])result.push_back(p.get());return result;}
 static void destroyInstance(std::string name,LLSD){owners[name].clear();}};
std::map<std::string,std::vector<std::unique_ptr<LLFloater>>> LLFloaterReg::owners;
void retireAccount(){RESET_LOOP}
LLFloaterInventorySettings* open(bool ok){auto owner=std::make_unique<LLFloaterInventorySettings>();auto* p=owner.get();
 p->children["favorites_color"]=std::make_unique<LLUICtrl>();p->children["favorites_swatch"]=std::make_unique<LLColorSwatchCtrl>();
 if(ok)p->children["ok_btn"]=std::make_unique<LLButton>();
 LLFloaterReg::owners["inventory_settings"].push_back(std::move(owner));assert(p->postBuild());return p;}
int main(){
 const LLSD colorName("COLOR_PARAMETER");auto& table=LLUIColorTable::instance();table.setColor(colorName.asString(),LLColor4(.25f));
 auto* first=open(false);auto* swatch=first->getChild<LLColorSwatchCtrl>("favorites_swatch");
 assert(!swatch->enabled);first->getUIColor(swatch,colorName);assert(swatch->original.value==.25f);
 auto* checkbox=first->getChild<LLUICtrl>("favorites_color");checkbox->value=LLSD(1.f);checkbox->callback();assert(swatch->enabled);
 swatch->value=LLSD(.75f);first->applyUIColor(swatch,colorName);assert(table.getColor(colorName.asString()).value==.75f);
 gSavedSettings.color=true;OmnifilterEngine::instance().mLog.push_back("old account");retireAccount();assert(retired==1&&LLFloaterReg::getFloaterList("inventory_settings").empty());
 assert(OmnifilterEngine::instance().mLog.empty());
 assert(gSavedSettings.color&&table.getColor(colorName.asString()).value==.75f);
 auto* second=open(true);assert(second->getChild<LLColorSwatchCtrl>("favorites_swatch")->enabled);
 second->getUIColor(second->getChild<LLColorSwatchCtrl>("favorites_swatch"),colorName);
 assert(second->getChild<LLColorSwatchCtrl>("favorites_swatch")->original.value==.75f);
 second->getChild<LLButton>("ok_btn")->callback();assert(second->closed);retireAccount();assert(retired==2);
}
'''.replace('METHODS', methods).replace('RESET_LOOP', loop).replace('COLOR_PARAMETER', parameter)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'inventory_settings.cpp'
            path.write_text(fixture)
            executable = Path(directory) / 'inventory_settings.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
