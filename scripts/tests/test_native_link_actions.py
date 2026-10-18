from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest,re
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class LinkActions(unittest.TestCase):
 def test_original_link_unlink_notifications_and_stale_confirmation(self):
  s=(ROOT/'indra/newview/vsobjectselection.cpp').read_text();body=method(s,'bool VSObjectSelection::unlinkObjects()').replace('VSObjectSelection::','Selection::')
  shared=(ROOT/'indra/newview/vslinkactions.h').read_text();shared=re.sub(r'^#.*$','',shared,flags=re.M)
  source=(HERE/'fixtures/native_link_actions_fixture.cpp').read_text().replace('REFERENCE',(HERE/'fixtures/native_link_actions_original.cpp').read_text()).replace('SHARED',shared).replace('UNLINK',body)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_original_enable_link_unlink_policy_matches_native(self):
  s=(ROOT/'indra/newview/vsobjectselection.cpp').read_text();native=method(s,'bool VSObjectSelection::enableLinkObjects()')+'\n'+method(s,'bool VSObjectSelection::enableUnlinkObjects()')
  source=(HERE/'fixtures/native_link_enable_fixture.cpp').read_text().replace('REFERENCE',(HERE/'fixtures/native_link_enable_original.cpp').read_text()).replace('NATIVE',native)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
