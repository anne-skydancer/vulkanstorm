"""Compare shared tracker CPU geometry against the frozen original GL stream."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT
HERE=Path(__file__).resolve().parent
class TrackerGeometry(unittest.TestCase):
 def test_original_pulse_beam_shockwave_stream_all_directions_heights(self):
  folder=Path(os.environ['VS_TRACKER_TEST_SOURCE'])if 'VS_TRACKER_TEST_SOURCE'in os.environ else ROOT/'indra/newview'
  shared='\n'.join(l for l in(folder/'vstrackergeometry.h').read_text().splitlines()if not l.startswith('#'))
  sink='\n'.join(l for l in(folder/'vstrackermeshsink.h').read_text().splitlines()if not l.startswith('#'))
  fixture=(HERE/'native_tracker_geometry_fixture.cpp').read_text().replace('SHARED',shared).replace('MESH_SINK',sink)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'tracker.cpp';exe=Path(tmp)/'tracker.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
