from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
class NativePieSliceAdmission(unittest.TestCase):
 def test_registered_callbacks_preserve_policies_without_unsafe_native_dispatch(self):
  source=(ROOT/'indra/newview/pieslice.cpp').read_text()
  methods='\n'.join(method(source,sig)for sig in('void PieSlice::initFromParams(', 'void PieSlice::updateEnabled(', 'void PieSlice::updateVisible(', 'void PieSlice::onCommit('))
  fixture=(HERE/'native_pie_slice_admission_fixture.cpp').read_text().replace('METHODS',methods)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'pie.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(fixture)
   r=subprocess.run([shutil.which('clang++')or shutil.which('g++'),'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
