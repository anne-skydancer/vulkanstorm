"""Exact estate-ban prompt/flags/scope plus delayed account/region ownership."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class AvatarModerationBan(unittest.TestCase):
 def test_reference_owner_manager_god_flags_and_retired_scope(self):
  folder=Path(os.environ['VS_AVATAR_MODERATION_SOURCE'])if 'VS_AVATAR_MODERATION_SOURCE'in os.environ else ROOT/'indra/newview'
  source=(folder/'llavataractions.cpp').read_text();methods='\n'.join(method(source,s)for s in ['void LLAvatarActions::estateBanMultiple(', 'bool LLAvatarActions::callbackEstateBan('])
  header=(ROOT/'indra/newview/vsnativeim.h').read_text();a=header.index('template<class Callback>');b=header.index('// Notification responders',a)
  fixture=(HERE/'native_avatar_moderation_ban_fixture.cpp').read_text().replace('PORTS',(HERE/'native_avatar_moderation_ban_ports.h').read_text()).replace('WRAPPERS',header[a:b]).replace('GOLD',(HERE/'moderation_ban_gold.cpp').read_text()).replace('METHODS',methods)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'ban.cpp';exe=Path(tmp)/'ban.exe';cpp.write_text(fixture)
   build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
