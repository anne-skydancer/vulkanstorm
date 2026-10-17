from native_source_fixture import production_sources as compose,ROOT,HERE,method
from pathlib import Path
import re,subprocess,tempfile,shutil,unittest
HERE=Path(__file__).resolve().parent
class ConfirmationLifecycle(unittest.TestCase):
 def test_owned_once_and_released_without_response(self):
  header=(ROOT/'indra/newview/llconfirmationmanager.h').read_text(encoding='utf-8-sig')
  source=compose()['indra/newview/llconfirmationmanager.cpp'];source=re.sub(r'^#include.*$','',source,flags=re.M)
  fixture=(HERE/'native_confirmation_lifecycle_fixture.cpp').read_text().replace('HEADER',header).replace('SOURCE',source)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as directory:
   cpp=Path(directory)/'confirmation.cpp';exe=Path(directory)/'confirmation.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
