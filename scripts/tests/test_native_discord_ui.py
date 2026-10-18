"""Genuine shared Discord account lifetimes and original native self visibility policy."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeDiscordUI(unittest.TestCase):
 def compile_run(self,code):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'discord.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],cwd=directory,capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
 def test_actual_rpc_account_retirement_marker_privacy_and_reentrant_callbacks(self):
  source=(ROOT/'indra/newview/fsdiscordconnect.cpp').read_text()
  signatures=['bool FSDiscordConnect::checkMarkerFile(','void FSDiscordConnect::setMarkerFile(','void FSDiscordConnect::clearMarkerFile(','static void handleDiscordReady(','static void handleDiscordDisconnected(','void FSDiscordConnect::discordConnectCoro(','void FSDiscordConnect::discordDisconnectCoro(','void FSDiscordConnect::discordConnectedCoro(','void FSDiscordConnect::connectToDiscord(','void FSDiscordConnect::disconnectFromDiscord(','void FSDiscordConnect::checkConnectionToDiscord(','bool FSDiscordConnect::Tick(','void FSDiscordConnect::retireAccount(','void FSDiscordConnect::onNativeSelfFullyVisible(']
  self.compile_run((Path(__file__).parent/'fixtures/native_discord_ui/sdk.cpp').read_text().replace('METHODS','\n'.join(method(source,x)for x in signatures)))
 def test_actual_self_default_bake_definition_matches_native_owner(self):
  original=(ROOT/'indra/newview/llvoavatarself.cpp').read_text();native=(ROOT/'indra/newview/vsworldavatar.cpp').read_text()
  self.compile_run((Path(__file__).parent/'fixtures/native_discord_ui/defined.cpp').read_text().replace('ORIGINAL',method(original,'bool LLVOAvatarSelf::isTextureDefined(')).replace('NATIVE',method(native,'bool isTextureDefined(ETextureIndex te,U32) const override')))
 def test_original_self_loading_and_stability_policy_matches_native_real_callback_path(self):
  original=(ROOT/'indra/newview/llvoavatar.cpp').read_text();native=(ROOT/'indra/newview/vsworldavatar.cpp').read_text()
  code=(Path(__file__).parent/'fixtures/native_discord_ui/readiness.cpp').read_text()
  for marker,source,signature in [('ORIGINAL_UPDATE',original,'bool LLVOAvatar::updateIsFullyLoaded('),('ORIGINAL_PROCESS',original,'bool LLVOAvatar::processFullyLoadedChange('),('NATIVE_UPDATE',native,'bool VSWorldAvatar::updateFirstFullyVisible(')]:code=code.replace('\n'+marker+'\n','\n'+method(source,signature)+'\n')
  self.compile_run(code)
if __name__=='__main__':unittest.main()
