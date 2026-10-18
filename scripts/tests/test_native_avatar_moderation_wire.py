"""Exact reference moderation message bodies and actual delayed account guard."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class AvatarModerationWire(unittest.TestCase):
 def test_original_freeze_eject_kick_home_wire_and_retired_confirmations(self):
  folder=Path(os.environ['VS_AVATAR_MODERATION_SOURCE'])if 'VS_AVATAR_MODERATION_SOURCE'in os.environ else ROOT/'indra/newview'
  source=(folder/'llavataractions.cpp').read_text();methods=[]
  for s in ['void sendEstateOwnerMessage(const LLHost&', 'void sendEstateOwnerMessage(const LLViewerRegion*', 'void sendEstateOwnerMessage(const VSAvatarContext&']:methods.append(method(source,s))
  for name in ['landEjectMultiple','landFreezeMultiple','estateKickMultiple','estateTeleportHomeMultiple']:methods.append(method(source,'void LLAvatarActions::'+name+'('))
  for name in ['callbackLandEject','callbackLandFreeze','callbackEstateKick','callbackEstateTeleportHome']:methods.append(method(source,'bool LLAvatarActions::'+name+'('))
  header=(ROOT/'indra/newview/vsnativeim.h').read_text();a=header.index('template<class Callback>');b=header.index('// Notification responders',a)
  fixture=(HERE/'native_avatar_moderation_wire_fixture.cpp').read_text().replace('WRAPPERS',header[a:b]).replace('GOLD',(HERE/'moderation_wire_gold.cpp').read_text()).replace('METHODS','\n'.join(methods))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'wire.cpp';exe=Path(tmp)/'wire.exe';cpp.write_text(fixture)
   build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
