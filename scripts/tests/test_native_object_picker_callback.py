from pathlib import Path
import importlib.util,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
def production_source():
 return (ROOT/'indra/newview/lltoolobjpicker.cpp').read_text(encoding='utf-8')
class ObjectPickerCallback(unittest.TestCase):
 def test_actual_result_delivery_reentrancy_and_replacement(self):
  body=method(production_source(),'void LLToolObjPicker::onMouseCaptureLost(')
  text='''#include <cassert>
#include <cstddef>
struct UUID{int value=0;void setNull(){value=0;}};
struct LLToolObjPicker{void(*mExitCallback)(void*)=nullptr;void*mExitCallbackData=nullptr;bool mPicked=false;UUID mHitObjectID;void onMouseCaptureLost();};
METHOD
struct Test{LLToolObjPicker*tool;int calls=0,replacement=0;};
void second(void*raw){auto*t=static_cast<Test*>(raw);++t->replacement;}
void first(void*raw){auto*t=static_cast<Test*>(raw);++t->calls;assert(t->tool->mPicked&&t->tool->mHitObjectID.value==73);t->tool->onMouseCaptureLost();t->tool->mExitCallback=second;t->tool->mExitCallbackData=t;}
int main(){LLToolObjPicker tool;Test state{&tool};tool.mPicked=true;tool.mHitObjectID.value=73;tool.mExitCallback=first;tool.mExitCallbackData=&state;tool.onMouseCaptureLost();assert(state.calls==1&&state.replacement==0&&!tool.mPicked&&tool.mHitObjectID.value==0);assert(tool.mExitCallback==second&&tool.mExitCallbackData==&state);tool.onMouseCaptureLost();assert(state.calls==1&&state.replacement==1&&!tool.mExitCallback&&!tool.mExitCallbackData);tool.onMouseCaptureLost();assert(state.replacement==1);}
'''.replace('METHOD',body)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory(dir=HERE)as temp:
   cpp=Path(temp)/'picker.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(text)
   result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
