"""Execute actual replay wire capture and shared selection-channel admission."""
from pathlib import Path
import re,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
class ParcelEcho(unittest.TestCase):
 def test_real_request_capture_keeps_rectangle_snap_and_current_owner(self):
  source=(ROOT/'indra/newview/vsnativeimreplay.cpp').read_text()
  capture=method(source,'static void parcelRequest(')
  code='''#include <cassert>
#include <optional>
#include <string>
#include <stdexcept>
using LLUUID=int;using S32=int;using F32=float;int gAgentID=1,gAgentSessionID=2;
void require(bool yes,const char*message){if(!yes)throw std::runtime_error(message);}
struct LLMessageSystem{int agent=1,session=2,sequence=-1000010;bool snap=false;float west=4,south=4,east=8,north=8;
void getUUID(const char*,std::string key,int& out){out=key=="AgentID"?agent:session;}
void getS32(const char*,const char*,int&out){out=sequence;}void getBOOL(const char*,const char*,bool&out){out=snap;}
void getF32(const char*,std::string key,float&out){out=key=="West"?west:key=="South"?south:key=="East"?east:north;}};
struct Replay {struct ParcelWire{S32 sequence;bool snap;F32 west,south,east,north;};std::optional<ParcelWire>selectedParcelWire;bool waitingParcelSelection=false,expectedParcelSnap=false;
'''+capture+'''
};
int main(){Replay replay;LLMessageSystem request;auto send=[&]{Replay::parcelRequest(&request,reinterpret_cast<void**>(&replay));};
send();assert(!replay.selectedParcelWire);replay.waitingParcelSelection=true;
request.sequence=0;send();assert(!replay.selectedParcelWire);request.sequence=-1000010;
request.west=0;send();assert(!replay.selectedParcelWire);request.west=4;
request.snap=true;send();assert(!replay.selectedParcelWire);request.snap=false;send();
assert(replay.selectedParcelWire && replay.selectedParcelWire->sequence==-1000010 && !replay.selectedParcelWire->snap);
request.sequence=-1000011;send();assert(replay.selectedParcelWire->sequence==-1000010);
replay.selectedParcelWire.reset();replay.expectedParcelSnap=true;request.snap=true;send();assert(replay.selectedParcelWire->sequence==-1000011 && replay.selectedParcelWire->snap);
request.agent=99;bool refused=false;try{send();}catch(const std::runtime_error&){refused=true;}assert(refused);
}'''
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'echo.cpp';exe=Path(tmp)/'echo.exe';cpp.write_text(code)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_actual_unsigned_wire_encoding_keeps_all_permission_bits(self):
  source=(ROOT/'indra/newview/vsnativeimreplay.cpp').read_text()
  producer=method(source,'void publishReplayParcel(')
  self.assertIn('data["ParcelFlags"]=ll_sd_from_U32(flags);',producer)
  self.assertIn('data["AuctionID"]=ll_sd_from_U32(0);',producer)
  util=(ROOT/'indra/llcommon/llsdutil.cpp').read_text()
  reader=method((ROOT/'indra/llmessage/llsdmessagereader.cpp').read_text(),'void LLSDMessageReader::getU32(')
  code=r'''#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
using U8=unsigned char;using U32=std::uint32_t;using S32=int;
struct LLSD{using Binary=std::vector<U8>;Binary bytes;LLSD()=default;LLSD(int){}explicit LLSD(Binary value):bytes(std::move(value)){}const Binary&asBinary()const{return bytes;}};
'''+method(util,'LLSD ll_sd_from_U32(')+chr(10)+method(util,'U32 ll_U32_from_sd(')+r'''
LLSD getLLSD(const LLSD& message,const char*,const char*,S32){return message;}
struct LLSDMessageReader{LLSD mMessage;void getU32(const char*,const char*,U32&,S32);};
'''+reader+r'''
int main(){LLSDMessageReader reader;U32 actual=99;
for(U32 flags:{U32(0),U32(1),U32(134217729),U32(0x80000000),U32(0xffffffff)}){
reader.mMessage=ll_sd_from_U32(flags);reader.getU32("ParcelData","ParcelFlags",actual,0);assert(actual==flags);
const auto& bytes=reader.mMessage.asBinary();assert(bytes.size()==4 && bytes[0]==U8(flags>>24) && bytes[3]==U8(flags));}
reader.mMessage=LLSD(134217729);reader.getU32("ParcelData","ParcelFlags",actual,0);assert(actual==0);
}'''
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   import sys
   cpp=Path(tmp)/'unsigned.cpp';exe=Path(tmp)/'unsigned.exe';cpp.write_text(code)
   args=[compiler,'-std=c++17',str(cpp),'-o',str(exe)]
   if sys.platform=='win32':args.append('-lws2_32')
   built=subprocess.run(args,capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
