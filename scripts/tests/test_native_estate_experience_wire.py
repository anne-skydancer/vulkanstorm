"""Actual estate experience binary protocol rejects malformed spans atomically."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class EstateExperienceWire(unittest.TestCase):
 def test_actual_decoder_counts_binary_uuid_spans_and_atomic_publication(self):
  folder=Path(os.environ['VS_ESTATE_WIRE_SOURCE'])if 'VS_ESTATE_WIRE_SOURCE'in os.environ else ROOT/'indra/newview'
  source=(folder/'llfloaterregioninfo.cpp').read_text();methods='\n'.join(method(source,s)for s in ['LLSD LLDispatchSetEstateExperience::getIDs(', 'bool LLDispatchSetEstateExperience::operator()('])
  fixture=(HERE/'native_estate_experience_wire_fixture.cpp').read_text().replace('METHODS',methods)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'experience.cpp';exe=Path(tmp)/'experience.exe';cpp.write_text(fixture)
   build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
