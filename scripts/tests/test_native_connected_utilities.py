"""Real filter-log and region-tracker callback owners across UI/account retirement."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeConnectedUtilities(unittest.TestCase):
 def test_actual_filter_and_map_callbacks_reject_retired_owner_account_and_duplicate_connection(self):
  code=(Path(__file__).parent/'fixtures/native_connected_utilities/lifetimes.cpp').read_text()
  for marker,file,signature in [('INIT','omnifilterengine.cpp','void OmnifilterEngine::ensureInitialized('),('LOG','omnifilter.cpp','void Omnifilter::connectLog('),('TRACKER','alfloaterregiontracker.cpp','std::function<void(const LLSD&,const LLSD&)> ALFloaterRegionTracker::regionAddedCallback(')]:
   code=code.replace(marker,method((ROOT/'indra/newview'/file).read_text(),signature))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'utility.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
