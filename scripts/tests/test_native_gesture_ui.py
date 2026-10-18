"""Shared real gesture callbacks, menu payloads, and authenticated account/UI lifetimes."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest,json,xml.etree.ElementTree as ET
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeGestureUI(unittest.TestCase):
 def test_real_gesture_controllers_and_shipped_menu_contract(self):
  source=(ROOT/'indra/newview/llfloatergesture.cpp').read_text();code=(Path(__file__).parent/'fixtures/native_gesture_ui/controllers.cpp').read_text()
  for marker,signature in [('SHOW','class GestureShowCallback :'),('COPY','class GestureCopiedCallback :'),('LOADED','std::function<void(LLMultiGesture*)> LLFloaterGesture::gestureLoadedCallback('),('RENAME','void LLFloaterGesture::onGestureRename('),('POLICY','bool LLFloaterGesture::isNativeCallback(')]:
   code=code.replace('\n'+marker+'\n','\n'+method(source,signature)+(';'if marker in ['SHOW','COPY']else'')+'\n')
  assertions=[]
  for node in ET.parse(ROOT/'indra/newview/skins/default/xui/en/menu_gesture_gear.xml').getroot().iter():
   if node.get('function','').startswith('Gesture.'):
    assertions.append('assert(LLFloaterGesture::isNativeCallback('+json.dumps(node.get('function'))+',LLSD{{},'+json.dumps(node.get('parameter',''))+'}));')
  code=code.replace('POLICY_ASSERTIONS','\n'.join(assertions))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'gesture.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
