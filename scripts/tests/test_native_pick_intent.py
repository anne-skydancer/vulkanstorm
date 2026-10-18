from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE));from native_source_fixture import method
class PickIntent(unittest.TestCase):
 def compile(self,text):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory(dir=HERE)as temp:
   p=Path(temp);cpp=p/'main.cpp';exe=p/'main.exe';cpp.write_text(text)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_actual_picker_retirement_and_reentrant_successor(self):
  source=(ROOT/'indra/newview/lltoolobjpicker.cpp').read_text();text=(HERE/'fixtures/native_pick_intent_picker.cpp').read_text()
  for marker,sig in [('GUARD','std::function<bool()> LLToolObjPicker::nativePickGuard('),('LOST','void LLToolObjPicker::onMouseCaptureLost('),('SET','void LLToolObjPicker::setExitCallback('),('CALLBACK','void LLToolObjPicker::pickCallback(')]:text=text.replace(marker,method(source,sig))
  self.compile(text)
 def test_actual_schedule_composes_operation_root_and_account(self):
  source=(ROOT/'indra/newview/llviewerwindow.cpp').read_text();text=(HERE/'fixtures/native_pick_intent_schedule.cpp').read_text().replace('SCHEDULE',method(source,'void LLViewerWindow::schedulePick('));self.compile(text)
if __name__=='__main__':unittest.main()
