from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest,re
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class EditMenu(unittest.TestCase):
 def test_original_delete_controller_and_native_confirmation_retirement_reentry(self):
  body=method((ROOT/'indra/newview/vsobjectselection.cpp').read_text(),'void VSObjectSelection::selectDelete()');shared=re.sub(r'^#.*$','',(ROOT/'indra/newview/vseditdelete.h').read_text(),flags=re.M)
  source=(HERE/'fixtures/native_edit_delete_fixture.cpp').read_text().replace('REFERENCE',(HERE/'fixtures/native_edit_delete_original.cpp').read_text()).replace('SHARED',shared).replace('NATIVE',body)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_actual_native_undo_redo_uuid_group_and_linked_part_wire(self):
  s=(ROOT/'indra/newview/vsobjectselection.cpp').read_text();methods=method(s,'void VSObjectSelection::undo()')+'\n'+method(s,'void VSObjectSelection::redo()')
  source=(HERE/'fixtures/native_edit_history_fixture.cpp').read_text().replace('METHODS',methods);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
