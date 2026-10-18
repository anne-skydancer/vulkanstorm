from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class Children(unittest.TestCase):
 def test_actual_snapshot_countertransform_identity_budget_and_retirement(self):
  s=(ROOT/'indra/newview/vsobjectselection.cpp').read_text();methods='\n'.join(method(s,'bool VSObjectSelection::'+name+'(')for name in ['saveUnselectedChildrenPosition','saveUnselectedChildrenRotation','resetChildrenRotationAndPosition','resetChildrenPosition']);source=(HERE/'fixtures/native_child_preservation_fixture.cpp').read_text().replace('METHODS',methods);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_original_full_quaternion_countertransform_20000_cases(self):
  s=(ROOT/'indra/newview/vsobjectselection.cpp').read_text();methods='\n'.join(method(s,'bool VSObjectSelection::'+name+'(')for name in ['saveUnselectedChildrenPosition','saveUnselectedChildrenRotation','resetChildrenRotationAndPosition','resetChildrenPosition']);source=(HERE/'fixtures/native_child_preservation_math_fixture.cpp').read_text().replace('METHODS',methods).replace('ORIGINAL',(HERE/'fixtures/native_child_preservation_original.cpp').read_text());compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
