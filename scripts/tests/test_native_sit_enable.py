from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class Sit(unittest.TestCase):
 def test_real_native_and_reference_availability_label_editing_and_rlva(self):
  s=(ROOT/'indra/newview/llviewermenu.cpp').read_text();methods='\n'.join(method(s,'bool '+name+'(')for name in ['enable_standup_self','sit_avatar_is_editing','sit_pick_allowed','show_sitdown_self','enable_sitdown_self','is_object_sittable','enable_object_sit']);source=(HERE/'fixtures/native_sit_enable_fixture.cpp').read_text().replace('METHODS',methods);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17','-DVS_NATIVE_VULKAN=1',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
