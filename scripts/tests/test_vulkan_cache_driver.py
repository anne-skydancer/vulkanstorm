"""Preserve actual driver output, nonzero exit and bounded timeout evidence."""
import json
from pathlib import Path
import subprocess,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from run_vulkan_texture_cache_tests import run_driver,validate_driver
class CacheDriver(unittest.TestCase):
    def test_success_exit_failure_and_timeout_remain_decisive(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for code in (0,37):
                folder=root/str(code);folder.mkdir()
                actual=run_driver([sys.executable,'-c',f'import sys;print("actual driver output",flush=True);sys.exit({code})'],folder,timeout=10)
                self.assertEqual(actual,code);self.assertIn('actual driver output',(folder/'driver-output.log').read_text())
                self.assertEqual(json.loads((folder/'driver.json').read_text())['return_code'],code)
                if code:
                    with self.assertRaises(ValueError):validate_driver(folder)
                else:
                    validate_driver(folder);(folder/'driver-output.log').unlink()
                    with self.assertRaises(ValueError):validate_driver(folder)
            folder=root/'timeout';folder.mkdir()
            with self.assertRaises(subprocess.TimeoutExpired):run_driver([sys.executable,'-c','import time;time.sleep(10)'],folder,timeout=.25)
            self.assertIs(json.loads((folder/'driver.json').read_text())['timed_out'],True)
            with self.assertRaises(ValueError):validate_driver(folder)
if __name__=='__main__':unittest.main()
