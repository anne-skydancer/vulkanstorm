"""Execute real loopback admission and real Region/TopObjects request producers."""
from pathlib import Path
import os,re,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class RegionReplayAdmission(unittest.TestCase):
 def test_actual_lambda_production_policy_and_actual_request_encoders(self):
  replay=Path(os.environ.get('VS_REGION_ADMISSION_CPP',ROOT/'indra/newview/vsnativereplay.cpp')).read_text()
  begin=replay.index('gMessageSystem->setMessageAdmission([weak]');end=replay.index('\n        });',begin)+len('\n        });')
  admission=replay[begin:end]
  session=(ROOT/'indra/newview/vsnativesession.cpp').read_text()
  sets='\n'.join(re.search(r'const std::set<std::string>\s+'+name+r'\s*=\s*\{[\s\S]*?\};',session)[0]for name in ['transport','worldMessages','session'])
  fixture=(HERE/'native_region_replay_admission_fixture.cpp').read_text().replace('SETS',sets).replace('PRODUCTION_ADMISSION',method(session,'bool VSNativeSession::admit(')).replace('DIAGNOSTIC_ADMISSION',admission)
  fixture=fixture.replace('REGION_REQUEST',method((ROOT/'indra/newview/llfloaterregioninfo.cpp').read_text(),'void LLFloaterRegionInfo::requestRegionInfo(')).replace('LAND_REQUEST',method((ROOT/'indra/newview/llfloatertopobjects.cpp').read_text(),'void LLFloaterTopObjects::onRefresh('))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'admission.cpp';exe=Path(tmp)/'admission.exe';cpp.write_text(fixture);build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr);run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
