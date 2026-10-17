from native_source_fixture import production_sources as compose,ROOT,HERE,method
from pathlib import Path
import subprocess,shutil,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
class ParcelCPU(unittest.TestCase):
 def test_reference_boundary_rectangles_residency_and_late_echoes(self):
  changes=compose()
  source=(ROOT/'indra/newview/llviewerparcelmgr.cpp').read_text(encoding='utf-8-sig');reference=method(source,'void LLViewerParcelMgr::writeSegmentsFromBitmap(')
  fixture=(HERE/'native_parcel_cpu_fixture.cpp').read_text().replace('REFERENCE',reference)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'parcel.cpp';exe=Path(tmp)/'parcel.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17','-I',str(HERE),'-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
