"""Compiled actual voice HTTP/account and conversation responder lifetimes."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).resolve().parent/'fixtures/native_voice_account'
class NativeVoiceAccount(unittest.TestCase):
 def run_cpp(self,source):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'voice.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(source)
   built=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_actual_voice_remap_invalidates_only_registered_request_owner(self):
  source=(ROOT/'indra/newview/llvoicechannel.cpp').read_text()
  self.run_cpp((FIXTURES/'remap_fixture.cpp').read_text().replace('METHOD',method(source,'void LLVoiceChannel::updateSessionID(')))
 def test_group_call_request_survives_owner_retirement_and_account_replacement(self):
  source=(ROOT/'indra/newview/llvoicechannel.cpp').read_text()
  self.run_cpp((FIXTURES/'group_fixture.cpp').read_text().replace('GROUP_METHOD',method(source,'void LLVoiceChannelGroup::voiceCallCapCoro(')))
 def test_force_close_response_requires_exact_conversation_instance_and_account(self):
  source=(ROOT/'indra/newview/llimview.cpp').read_text()
  self.run_cpp((FIXTURES/'error_fixture.cpp').read_text().replace('PRODUCER',method(source,'void\nLLIMMgr::showSessionForceClose(')).replace('RESPONDER',method(source,'bool\nLLIMMgr::onConfirmForceCloseError(')))
 def test_parcel_voice_credentials_reject_retired_region_account_and_process(self):
  prefix=(FIXTURES/'group_fixture.cpp').read_text().split('struct LLVoiceChannel {')[0]
  prefix=prefix.replace(' std::string asString()const{return text;}',' bool has(const std::string& key)const{return values.contains(key);}\n std::string asString()const{return text;}')
  source=(ROOT/'indra/newview/llvoicevivox.cpp').read_text()
  check=method((ROOT/'indra/newview/llvoicevivox.h').read_text(),'void checkNativeAccountRevision(')
  self.run_cpp((FIXTURES/'parcel_fixture.cpp').read_text().replace('PREFIX',prefix).replace('CHECK_REVISION',check).replace('PARCEL_METHOD',method(source,'bool LLVivoxVoiceClient::requestParcelVoiceInfo(')))
if __name__=='__main__':unittest.main()
