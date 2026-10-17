from native_source_fixture import production_sources as compose,ROOT,HERE,method
"""Execute actual production shared covenant request/parser/callback ownership."""
from pathlib import Path
import re,shutil,subprocess,tempfile,unittest
class CovenantLifetime(unittest.TestCase):
 def test_actual_request_delivery_retirement_reentry_and_notecard_parser(self):
  proposed=compose()
  source=proposed['indra/newview/vsestatecovenant.cpp']
  header=proposed['indra/newview/vsestatecovenant.h']
  clean=lambda s:re.sub(r'^#(?:include|pragma).*\n','',s,flags=re.M)
  source=clean(header)+'\n'+clean(source)
  fixture=(HERE/'native_covenant_lifecycle_fixture.cpp').read_text().replace('PRODUCTION',source)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'covenant.cpp';exe=Path(tmp)/'covenant.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
