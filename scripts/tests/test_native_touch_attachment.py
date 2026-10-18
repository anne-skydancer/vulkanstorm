from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Attachment(unittest.TestCase):
 def test_actual_linked_worn_item_selection_transient_and_retirement_callbacks(self):
  s=(ROOT/'indra/newview/llviewermenu.cpp').read_text();lookup=method(s,'std::optional<VSWorldScene::Attachment> native_worn_attachment(')
  def native(sig):
   text=method(s,sig);return text[:text.index('#endif')+len('#endif')]+'\n}'
  source=(HERE/'fixtures/native_touch_attachment_fixture.cpp').read_text().replace('LOOKUP',lookup).replace('ENABLE',native('bool enable_attachment_touch(')).replace('@@TOUCH@@',native('void handle_attachment_touch('));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
