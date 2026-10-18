from pathlib import Path
import shutil,subprocess,tempfile,unittest,re
ROOT=Path(__file__).resolve().parents[2]
S=ROOT/'indra/newview/llfloaterenvironmentadjust.cpp'
def body(source,name):
 start=source.index(name);end=source.index('{',start)+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
class PersonalLightingTests(unittest.TestCase):
 def test_real_mutations_have_live_account_and_setenv_guard(self):
  source=S.read_text()
  callbacks=re.findall(r'void LLFloaterEnvironmentAdjust::(on\w+Changed|onButtonReset)\(\)',source)
  self.assertGreaterEqual(len(callbacks),21)
  for name in callbacks:
   self.assertIn('if (!canEditEnvironment()) return;',body(source,'void LLFloaterEnvironmentAdjust::'+name+'()'))
  self.assertIn('vs_native_im_ui_callback(this',body(source,'void LLFloaterEnvironmentAdjust::onButtonReset()'))
  self.assertIn('if (!mEditingNative) gPipeline.mReflectionMapManager.resume();',source)
  header=S.with_suffix('.h').read_text();self.assertIn('scoped_connection mEventConnection',header);self.assertIn('scoped_connection mRlvConnection',header)
 def test_production_edit_policy_preserves_gl_and_native_restrictions(self):
  source=S.read_text();policy=body(source,'bool LLFloaterEnvironmentAdjust::canEditEnvironment() const')
  fixture=r'''
#include <cassert>
#include <functional>
const int RLV_BHVR_SETENV=1;
struct Handler{bool restricted=false;bool hasBehaviour(int)const{return restricted;}}gRlvHandler;
struct LLFloaterEnvironmentAdjust{bool mEditingNative=false;bool current=true;std::function<bool()>mAccountCurrent=[this]{return current;};bool canEditEnvironment()const;};
''' + policy + r'''
int main(){LLFloaterEnvironmentAdjust f;gRlvHandler.restricted=true;assert(f.canEditEnvironment());
 f.mEditingNative=true;assert(!f.canEditEnvironment());gRlvHandler.restricted=false;assert(f.canEditEnvironment());
 f.current=false;assert(!f.canEditEnvironment());f.current=true;assert(f.canEditEnvironment());
 gRlvHandler.restricted=true;assert(!f.canEditEnvironment());}
'''
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as d:
   cpp=Path(d)/'lighting.cpp';exe=Path(d)/'lighting.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(built.returncode,0,built.stdout+built.stderr);self.assertEqual(subprocess.run([str(exe)]).returncode,0)
if __name__=='__main__':unittest.main()
