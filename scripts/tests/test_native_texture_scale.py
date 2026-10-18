"""Actual original scale/texture policy compared with the shared peer kernels."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeTextureScale(unittest.TestCase):
 def test_original_snapshot_planar_stretch_four_channel_center_pivot_and_protocol_order(self):
  original=(ROOT/'indra/newview/llselectmgr.cpp').read_text();kernel=(ROOT/'indra/newview/vstexturescaleratio.h').read_text()
  code=(Path(__file__).parent/'fixtures/native_face_edit/texture_scale.cpp').read_text().replace('SAVE',method(original,'void LLSelectNode::saveTextureScaleRatios(')).replace('ADJUST',method(original,'void LLSelectMgr::adjustTexturesByScale(')).replace('KERNEL',kernel[kernel.index('struct VSTextureScaleRatios'):])
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'scale.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
 def test_selected_snapshot_refusal_and_reentrant_account_node_retirement(self):
  source=(ROOT/'indra/newview/vstexturescaleselection.cpp').read_text()
  code=(Path(__file__).parent/'fixtures/native_face_edit/texture_scale_owner.cpp').read_text().replace('CAPTURE',method(source,'bool vs_capture_selected_texture_ratios('))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'owner.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
 def test_prepared_adjustment_refuses_before_source_mutation_and_never_overwrites_new_input(self):
  source=(ROOT/'indra/newview/vstexturescaleselection.cpp').read_text();kernel=(ROOT/'indra/newview/vstexturescaleratio.h').read_text()
  code=(Path(__file__).parent/'fixtures/native_face_edit/texture_scale_prepared.cpp').read_text().replace('KERNEL',kernel[kernel.index('struct VSTextureScaleRatios'):]).replace('PREPARED',source[source.index('struct VSPreparedTextureAdjustment'):])
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'prepared.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
