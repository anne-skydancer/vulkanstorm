"""Actual optional terrain query keeps current-owner guards and caller fallbacks."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class RegionHeightTest(unittest.TestCase):
 def test_actual_native_and_reference_height_guards(self):
  folder=Path(os.environ['VS_TRACKER_TEST_SOURCE'])if 'VS_TRACKER_TEST_SOURCE'in os.environ else ROOT/'indra/newview'
  body=method((folder/'vsregioncontext.cpp').read_text(),'std::optional<F32> vs_region_height(')
  fixture=(HERE/'native_region_height_fixture.cpp').read_text().replace('METHOD',body)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'height.cpp';exe=Path(tmp)/'height.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
