"""Original group/parcel/estate authority and exact lazy predicate order."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class AvatarModerationPolicy(unittest.TestCase):
 def test_all_original_authority_combinations_filtering_and_lazy_queries(self):
  folder=Path(os.environ['VS_AVATAR_MODERATION_SOURCE'])if 'VS_AVATAR_MODERATION_SOURCE'in os.environ else ROOT/'indra/newview'
  source=(folder/'llavataractions.cpp').read_text();methods='\n'.join(method(source,'bool LLAvatarActions::'+n+'(')for n in ['canLandFreezeOrEjectMultiple','canEstateKickOrTeleportHomeMultiple'])
  fixture=(HERE/'native_avatar_moderation_policy_fixture.cpp').read_text().replace('METHODS',methods)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'policy.cpp';exe=Path(tmp)/'policy.exe';cpp.write_text(fixture)
   build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
