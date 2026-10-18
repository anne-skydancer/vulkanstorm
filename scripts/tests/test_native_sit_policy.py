from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class SitPolicy(unittest.TestCase):
 def test_exact_original_regular_force_sit_distances_restrictions_and_lazy_legacy_getters(self):
  s=(ROOT/'indra/newview/rlvactions.cpp').read_text();a=s.index('namespace\n{\nstruct ReferenceSitObject');b=s.index('// Handles: @showhovertextall',a);actual=s[a:b];gold=(HERE/'fixtures/native_sit_original_policy.cpp').read_text().replace('RlvActions::canSit','referenceSit').replace('!hasBehaviour(','!RlvActions::hasBehaviour(');source=(HERE/'fixtures/native_sit_policy_fixture.cpp').read_text().replace('REFERENCE',gold).replace('METHOD',actual);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
