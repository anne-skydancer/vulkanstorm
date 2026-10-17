"""Compile actual native requested-catalogue admission, including material/media and mutable policy."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class MaterialCapabilityTests(unittest.TestCase):
 def test_primary_region_retains_requested_material_and_media_capabilities(self):
  text=(ROOT/'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8-sig')
  body=method(text,'void VSNativeSession::capabilities(');a=body.index('const LLSD requestedCapabilities=');loop=body[a:body.index('// LLSD messages',a)]
  shared=(ROOT/'indra/newview/vscapabilities.cpp').read_text(encoding='utf-8-sig');shared=shared[shared.index('void vs_build_capability_names('):]
  source=r'''
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <vector>
struct LLSD {bool array=false;std::string value;std::map<std::string,LLSD> fields;std::vector<LLSD> values;
 LLSD()=default;LLSD(std::string s):value(s){}static LLSD emptyArray(){LLSD a;a.array=true;return a;}
 bool isArray()const{return array;}std::string asString()const{return value;}void append(const char* s){array=true;values.emplace_back(s);}
 auto beginArray()const{return values.begin();}auto endArray()const{return values.end();}
 LLSD& operator[](const std::string& n){return fields[n];}const LLSD& operator[](const std::string& n)const{static LLSD empty;auto i=fields.find(n);return i==fields.end()?empty:i->second;}
};
bool http=false;bool use_http_inventory(){return http;}
struct AISAPI{static void getCapNames(LLSD& a){a.append("InventoryAPIv3");a.append("LibraryAPIv3");}};
bool webURL(const std::string& value){return value.rfind("http://",0)==0 || value.rfind("https://",0)==0;}
SHARED
LLSD admit(const LLSD& requested,const LLSD& caps){LLSD mCapabilities;
LOOP
 return mCapabilities;
}
int main(){
 LLSD caps;
 for(const char* name:{"RenderMaterials","ObjectMedia","ObjectMediaNavigate","ModifyMaterialParams","IncrementCOFVersion"})
  caps[name]=LLSD(std::string("https://fixture.invalid/")+name);
 caps["GetTexture"]=LLSD("file:///not-a-web-capability");caps["UnrequestedService"]=LLSD("https://fixture.invalid/private");
 http=false;const auto requestedWithoutHTTP=vs_native_capability_names();
 http=true;auto admitted=admit(requestedWithoutHTTP,caps);
 for(const char* name:{"RenderMaterials","ObjectMedia","ObjectMediaNavigate","ModifyMaterialParams"})
  assert(admitted.fields.count(name) && admitted[name].asString()==caps[name].asString());
 assert(!admitted.fields.count("GetTexture") && !admitted.fields.count("UnrequestedService"));
 assert(!admitted.fields.count("IncrementCOFVersion")); // Reply cannot acquire services not in the actual request.
 const auto requestedWithHTTP=vs_native_capability_names();http=false;
 admitted=admit(requestedWithHTTP,caps);assert(admitted.fields.count("IncrementCOFVersion")); // Reply must not drop a requested service after mutable policy changed.
 LLSD explicitRequest=LLSD::emptyArray();explicitRequest.append("ObjectMedia");
 admitted=admit(explicitRequest,caps);assert(admitted.fields.size()==1 && admitted.fields.count("ObjectMedia"));
}
'''.replace('SHARED',shared).replace('LOOP',loop)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'caps.cpp';exe=Path(tmp)/'caps.exe';cpp.write_text(source)
   r=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
