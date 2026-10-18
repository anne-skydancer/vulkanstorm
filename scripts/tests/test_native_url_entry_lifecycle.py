from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
from native_source_fixture import ROOT,HERE,method
class URLEntryLifecycle(unittest.TestCase):
 def test_actual_current_request_publication_busy_and_reentry(self):
  source=(ROOT/'indra/newview/llfloaterurlentry.cpp').read_text(encoding='utf-8-sig')
  methods='\n'.join(method(source,signature) for signature in ['void LLFloaterURLEntry::headerFetchComplete(', 'void LLFloaterURLEntry::onClose(', 'void LLFloaterURLEntry::draw()'])
  callback=method(source,'void LLFloaterURLEntry::onBtnOK(');a=callback.index('        const auto weak=self->getHandle()');b=callback.index('        LLCoros::instance().launch(',a);guard=callback[a:b]
  fixture=(HERE/'native_url_entry_fixture.cpp').read_text().replace('METHODS',methods).replace('REQUEST_GUARD',guard)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as directory:
   cpp=Path(directory)/'url.cpp';exe=Path(directory)/'url.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
