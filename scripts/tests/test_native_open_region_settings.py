from native_source_fixture import production_sources as compose,ROOT,HERE,method
"""Compare the production full event handler with the reference, including admission."""
from pathlib import Path
import re,subprocess,tempfile,shutil,unittest
class OpenRegionSettings(unittest.TestCase):
 def test_full_handler_and_authenticated_native_side_effects(self):
  path='indra/newview/llpanelopenregionsettings.cpp'
  original=method((ROOT/path).read_text(encoding='utf-8-sig'),'void post(')
  proposed=method(compose()[path],'void post(')
  names=sorted(set(re.findall(r'regionlimits->(set\w+)\(',original)))
  setters='\n'.join(f'void {name}(double v){{values["{name}"]=v;}}' for name in names)
  source=(HERE/'native_open_region_settings_fixture.cpp').read_text().replace('SETTERS',setters).replace('ORIGINAL',original.replace('void post(','void original(',1).replace(') const',')',1)).replace('PROPOSED',proposed.replace('void post(','void proposed(',1).replace(') const',')',1))
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as directory:
   cpp=Path(directory)/'settings.cpp';exe=Path(directory)/'settings.exe';cpp.write_text(source)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
