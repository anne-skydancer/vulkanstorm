"""Compile actual shared Telehub wire/model paths, no real simulator operations."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class TelehubWireTest(unittest.TestCase):
 def test_real_parser_atomicity_root_grouping_and_wire(self):
  folder=Path(os.environ['VS_REGION_TEST_SOURCE'])if 'VS_REGION_TEST_SOURCE'in os.environ else ROOT/'indra/newview'
  source=(folder/'llfloatertelehub.cpp').read_text()
  methods='\n'.join(method(source,s)for s in ['std::vector<VSObjectContext> telehubSelection(', 'void sendTelehubRequest(', 'void LLFloaterTelehub::unpackTelehubInfo('])
  fixture=(HERE/'native_telehub_fixture.cpp').read_text().replace('METHODS',methods)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'telehub.cpp';exe=Path(tmp)/'telehub.exe';cpp.write_text(fixture)
   build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
