"""Native local publication must preserve old generations on refusal."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]


class NativeLocalImageTests(unittest.TestCase):
    def test_actual_local_publication_retains_generation_and_facade(self):
        body=method((ROOT/'indra/newview/vsuiimageprovider.cpp').read_text(),'bool VSUIImageProvider::publishLocalImage(')
        source=r'''
#include <cassert>
#include <memory>
#include <map>
#include <string>
struct LLUUID {
 int value=0;bool isNull()const{return !value;}void setNull(){value=0;}
 std::string asString()const{return std::to_string(value);}
 bool operator<(const LLUUID& b)const{return value<b.value;}
 bool operator==(const LLUUID& b)const{return value==b.value;}
};
const LLUUID IMG_DEFAULT{999};
struct LLImageRaw {int w,h,c;int getWidth()const{return w;}int getHeight()const{return h;}int getComponents()const{return c;}};
struct Image {int w=0,h=0,generation=0,loaded=0;void setNativeExtent(int x,int y){w=x;h=y;}void onImageLoaded(){++loaded;}};
struct Connection {void disconnect(){}};
struct VSNativeSession {static VSNativeSession* active(){static VSNativeSession owner;return &owner;}int generation(){return 7;}};
struct VSUIImageProvider {
 struct Remote {bool local=false;LLUUID redirect;Connection redirectLoaded;
  std::shared_ptr<Image> pixels,facade;int discard=-1;std::shared_ptr<int> auxiliary;
  int fullWidth=0,fullHeight=0,components=0,readyGeneration=0;};
 struct Resources {bool refuse=false;int publications=0;std::shared_ptr<Image> publish(const std::string&,const LLImageRaw& raw){
  if(refuse){return {};}return std::make_shared<Image>(Image{raw.w,raw.h,++publications,0});}};
 struct Impl {bool retired=false;std::map<LLUUID,Remote> remote;Resources resources;bool evict(LLUUID){return false;}};
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();
 void getUIImageByID(LLUUID id,int){auto& value=mImpl->remote[id];if(!value.facade)value.facade=std::make_shared<Image>();}
 bool publishLocalImage(const LLUUID&,const LLImageRaw&);
};
BODY
int main(){
 VSUIImageProvider provider;
 assert(!provider.publishLocalImage({}, {32,16,4}));
 assert(!provider.publishLocalImage(IMG_DEFAULT, {32,16,4}));
 provider.mImpl->remote[{2}]={};
 assert(!provider.publishLocalImage({2},{32,16,4}));
 assert(!provider.mImpl->resources.publications);
 assert(provider.publishLocalImage({1},{32,16,4}));
 auto& value=provider.mImpl->remote[{1}];auto old=value.pixels;auto facade=value.facade;
 assert(value.local && value.discard==0 && value.readyGeneration==7 && facade->loaded==1);
 provider.mImpl->resources.refuse=true;
 assert(!provider.publishLocalImage({1},{16,32,4}));
 assert(value.pixels==old && value.facade==facade && value.fullWidth==32 && facade->loaded==1);
 provider.mImpl->resources.refuse=false;
 assert(provider.publishLocalImage({1},{16,32,4}));
 assert(value.pixels!=old && value.facade==facade && facade->loaded==2 && value.fullWidth==16);
 assert(old->generation==1 && old->w==32 && old->h==16);
 for(int id=3;id<=16384;++id)provider.mImpl->remote[{id}]={};
 assert(!provider.publishLocalImage({20000},{16,16,4}));
 assert(provider.mImpl->resources.publications==2);
}
'''.replace('BODY',body)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);cpp=root/'local.cpp';cpp.write_text(source);exe=root/'local.exe'
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__=='__main__':unittest.main()
