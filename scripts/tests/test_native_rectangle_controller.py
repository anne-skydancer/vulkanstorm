"""Compile the real native rectangle owner; no GPU or viewer process."""
from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'scripts/tests'))
from native_source_fixture import method
class Rectangle(unittest.TestCase):
 def compile(self,text):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   folder=Path(tmp);cpp=folder/'test.cpp';exe=folder/'test.exe';cpp.write_text(text)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
 def test_real_rectangle_node_order_transaction_refusal_retirement(self):
  source=(ROOT/'indra/newview/vsobjectselection.cpp').read_text()
  signatures=['bool VSObjectSelection::updateRectangle(','bool VSObjectSelection::commitRectangle(','void VSObjectSelection::clearRectangle(','std::vector<VSWorldScene::ObjectHandle> VSObjectSelection::rectangleHighlights(']
  actual='\n'.join(method(source,signature)for signature in signatures)
  self.compile((HERE/'fixtures/native_rectangle_controller.cpp').read_text().replace('METHODS',actual).replace('REFERENCE',(HERE/'fixtures/native_rectangle_filter_reference.cpp').read_text()))
 def test_real_release_before_commit_preserves_highlights_through_composite_transition(self):
  source=(ROOT/'indra/newview/lltoolselectrect.cpp').read_text()
  actual=(HERE/'fixtures/native_rectangle_release.cpp').read_text().replace('UP',method(source,'bool LLToolSelectRect::handleMouseUp(')).replace('DESELECT',method(source,'void LLToolSelectRect::handleDeselect('))
  self.compile('#define VS_NATIVE_VULKAN 1\n'+actual)
 def test_actual_hover_reaches_real_tool_and_refuses_retired_root_account_or_window(self):
  source=(ROOT/'indra/newview/vsviewerwindow.cpp').read_text()
  self.compile((HERE/'fixtures/native_rectangle_hover.cpp').read_text().replace('ACTUAL',method(source,'void LLViewerWindow::nativeHoverUpdate(')))
if __name__=='__main__':unittest.main()
