"""Real peer avatar identity/ownership and exact current coarse-model query."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class AvatarContextTest(unittest.TestCase):
 def test_actual_precise_coarse_region_account_and_legacy_owners(self):
  folder=Path(os.environ['VS_AVATAR_MODERATION_SOURCE'])if 'VS_AVATAR_MODERATION_SOURCE'in os.environ else ROOT/'indra/newview'
  header='\n'.join(l for l in(folder/'vsavatarcontext.h').read_text().splitlines()if not l.startswith('#'))
  source='\n'.join(l for l in(folder/'vsavatarcontext.cpp').read_text().splitlines()if not l.startswith('#include'))
  source+=method((folder/'vsnativecircuits.cpp').read_text(),'std::optional<std::pair<LLHost,VSCoarseSample<LLUUID>>> VSNativeSession::coarseAvatarLocation(')
  fixture=(HERE/'native_avatar_context_fixture.cpp').read_text().replace('HEADER',header).replace('METHODS',source)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'context.cpp';exe=Path(tmp)/'context.exe';cpp.write_text(fixture)
   build=subprocess.run([compiler,'-std=c++17','-DVS_NATIVE_VULKAN=1',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
