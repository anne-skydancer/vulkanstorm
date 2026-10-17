"""Execute primary-region capability admission so requested material/media services remain wired."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]
SERVICES=('RenderMaterials','ObjectMedia','ObjectMediaNavigate','ModifyMaterialParams')


class MaterialCapabilityTests(unittest.TestCase):
    def test_primary_region_retains_requested_material_and_media_capabilities(self):
        text=(ROOT/'indra/newview/vsnativesession.cpp').read_text()
        body=method(text,'void VSNativeSession::capabilities(')
        start=body.index('for (const char* name :')
        loop=body[start:body.index('// LLSD messages',start)]
        requested=text[text.index('for (const char* name :'):text.index('void VSNativeSession::capabilities(')]
        for name in SERVICES:self.assertIn('"'+name+'"',requested)
        source=r'''
#include <cassert>
#include <map>
#include <string>
struct Value {std::string value;std::string asString()const{return value;}};
bool webURL(const std::string& value){return value.rfind("http://",0)==0 || value.rfind("https://",0)==0;}
int main(){
 std::map<std::string,Value> caps,mCapabilities;
 for(const char* name:{"RenderMaterials","ObjectMedia","ObjectMediaNavigate","ModifyMaterialParams"})
  caps[name]={std::string("https://fixture.invalid/")+name};
 caps["GetTexture"]={"file:///not-a-web-capability"};caps["UnrequestedService"]={"https://fixture.invalid/private"};
 LOOP
 for(const char* name:{"RenderMaterials","ObjectMedia","ObjectMediaNavigate","ModifyMaterialParams"})
  assert(mCapabilities.count(name) && mCapabilities[name].asString()==caps[name].asString());
 assert(!mCapabilities.count("GetTexture") && !mCapabilities.count("UnrequestedService"));
}
'''.replace('LOOP',loop)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);cpp=root/'caps.cpp';cpp.write_text(source);exe=root/'caps.exe'
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()
