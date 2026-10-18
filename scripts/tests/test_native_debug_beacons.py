"""Original debug feedback geometry, portable line encoding and depth states."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest,os
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class DebugBeaconGeometry(unittest.TestCase):
 def test_exact_original_cross_cube_vertices_and_order(self):
  folder=Path(os.environ['VS_BEACON_TEST_SOURCE'])if 'VS_BEACON_TEST_SOURCE'in os.environ else ROOT/'indra/newview'
  kernel=(folder/'vsdebugbeacongeometry.h').read_text();kernel='\n'.join(l for l in kernel.splitlines()if not l.startswith('#'))
  fixture=(HERE/'native_debug_beacon_geometry_fixture.cpp').read_text().replace('KERNEL',kernel)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'beacons.cpp';exe=Path(tmp)/'beacons.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_actual_two_pass_publication_retirement_refusal_and_frame_reuse(self):
  folder=Path(os.environ['VS_BEACON_TEST_SOURCE'])if 'VS_BEACON_TEST_SOURCE'in os.environ else ROOT/'indra/newview'
  source='\n'.join(l for l in(folder/'vsbeaconpresentation.cpp').read_text().splitlines()if not l.startswith('#include'))
  kernel='\n'.join(l for l in(folder/'vsdebugbeacongeometry.h').read_text().splitlines()if not l.startswith('#'))
  fixture=(HERE/'native_debug_beacon_publication_fixture.cpp').read_text().replace('KERNEL',kernel).replace('PUBLICATION',source)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'beacons.cpp';exe=Path(tmp)/'beacons.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
