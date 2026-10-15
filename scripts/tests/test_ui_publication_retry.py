"""Exercise production font/fill drawing when bounded image publication refuses."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class UIPublicationRetryTest(unittest.TestCase):
    def test_captures_wait_for_complete_frame_and_retry_state_resets(self):
        resources = (ROOT/'indra/newview/vsuiresources.cpp').read_text()
        context = (ROOT/'indra/newview/vsvulkancontext.cpp').read_text()
        begin = method(resources, 'void VSUIResources::begin(')
        lines = context.splitlines()
        index = next(i for i, line in enumerate(lines) if 'if (observer && resources->evidence()' in line)
        observe = '\n'.join(lines[index:index+2])
        self.compile(r'''
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
struct VSUIResources {
 struct Impl{bool active=false;unsigned deferred_draw_batches=3;float logical_height=0,logical_width=0,dpi=0;
 std::vector<int> packets;std::array<float,4> clip;};
 std::unique_ptr<Impl> mImpl=std::make_unique<Impl>();
 struct Value{unsigned count;int asInteger()const{return count;}};
 struct Evidence{unsigned count;Value operator[](const char*)const{return {count};}};
 Evidence evidence()const{return {mImpl->deferred_draw_batches};}
 void begin(unsigned,unsigned,float);
};
BEGIN
int main(){
 VSUIResources ui;auto* resources=&ui;
 int device=0,context=0,target=0,captures=0;struct{int mX=10,mY=10;}size;float dpi=1;
 std::vector<int> packets;
 std::function<void(int,int,int,int,int,float,std::vector<int>&)> observer;
 auto observe=[&]{OBSERVE};
 observe();assert(captures==0);
 observer=[&](int,int,int,int,int,float,std::vector<int>&){++captures;};
 observe();assert(captures==0);
 ui.begin(10,10,1);assert(ui.mImpl->deferred_draw_batches==0);
 observe();assert(captures==1);
}
'''.replace('BEGIN', begin).replace('OBSERVE', observe))

    def test_refusal_never_commits_a_missing_generation_and_can_retry(self):
        source = (ROOT/'indra/newview/vsuiresources.cpp').read_text()
        methods = '\n'.join(method(source, name) for name in
                            ('void VSUIResources::fontBatch(', 'void VSUIResources::rectangle('))
        fixture = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
using S32=int;using U8=std::uint8_t;
template<class T> struct LLPointer:std::shared_ptr<T> {
 LLPointer()=default;LLPointer(T* p):std::shared_ptr<T>(p){}
 LLPointer& operator=(T* p){this->reset(p,[](T*){});return *this;}
};
struct LLImageRaw {
 int w,h,components;std::vector<U8> bytes;
 LLImageRaw(int width,int height,int count):w(width),h(height),components(count),bytes(w*h*count,255){}
 int getWidth()const{return w;}int getHeight()const{return h;}int getComponents()const{return components;}
 const U8* getData()const{return bytes.data();}
 void clear(U8 r,U8 g,U8 b,U8 a){bytes={r,g,b,a};}
};
struct LLVector4a {float values[4]{};const float* getF32ptr()const{return values;}};
struct LLVector2 {float mV[2]{};};struct LLColor4U {U8 mV[4]{255,255,255,255};};
struct LLColor4 {float mV[4]{1,1,1,1};};
struct LLRectf {float mLeft=0,mTop=1,mRight=1,mBottom=0;float getWidth()const{return mRight-mLeft;}float getHeight()const{return mTop-mBottom;}};
struct VSUIRenderer {
 struct Texture{std::vector<U8> pixels;};using Image=std::shared_ptr<Texture>;
 struct Vertex{float x,y,u,v,r,g,b,a;};
 struct Packet{int blend;Image image;std::array<float,4> clip,bounds,color;std::optional<std::array<Vertex,6>> triangles;};
 static const std::vector<U8>& imagePixels(const Image& image){return image->pixels;}
};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int nativeBlend(){return 0;}float nativeVertexColor(float value){return value;}
struct VSUIResources {
 struct Impl {
  struct Asset{unsigned width,height;VSUIRenderer::Image image;};
  struct Entry{std::shared_ptr<Asset> asset;};
  struct Page{LLPointer<LLImageRaw> raw;S32 generation=-1;std::string key;};
  std::map<LLImageRaw*,Page> font_pages;std::map<std::string,Entry> assets;
  std::vector<VSUIRenderer::Packet> packets;std::array<float,4> clip{0,0,10,10};
  bool active=true;float dpi=1,logical_height=10;unsigned deferred_draw_batches=0;
 };
 std::unique_ptr<Impl> mImpl=std::make_unique<Impl>();bool refuse=false;int publications=0;
 bool publish(const std::string& key,const LLImageRaw& raw){
  ++publications;if(refuse)return false;
  auto image=std::make_shared<VSUIRenderer::Texture>();image->pixels.resize(raw.w*raw.h*4,255);
  for(int y=0;y<raw.h;++y)for(int x=0;x<raw.w;++x){
   auto* d=image->pixels.data()+(y*raw.w+x)*4;
   const auto* p=raw.bytes.data()+((raw.h-1-y)*raw.w+x)*raw.components;
   d[0]=p[0];d[1]=raw.components==2?p[0]:p[1];d[2]=raw.components==2?p[0]:p[2];d[3]=raw.components==2?p[1]:p[3];
  }
  mImpl->assets[key]={std::make_shared<Impl::Asset>(Impl::Asset{unsigned(raw.w),unsigned(raw.h),image})};return true;
 }
 bool patch(const std::string& key,unsigned x,unsigned y,unsigned w,unsigned h,const std::vector<U8>& bytes){
  if(refuse)return false;auto& asset=*mImpl->assets.at(key).asset;
  auto image=std::make_shared<VSUIRenderer::Texture>(*asset.image);
  for(unsigned row=0;row<h;++row)std::copy_n(bytes.data()+row*w*4,w*4,image->pixels.data()+((y+row)*asset.width+x)*4);
  asset.image=image;return true;
 }
 void fontBatch(LLImageRaw*,S32,const LLVector4a*,const LLVector2*,const LLColor4U*,S32);
 void rectangle(const LLRectf&,const LLColor4&);
};
METHODS
int main(){
 LLVector4a positions[6];LLVector2 uv[6];LLColor4U colors[6];
 for(int components:{2,4}){
  VSUIResources ui;LLImageRaw raw(1,1,components);
  auto draw=[&](int generation){ui.fontBatch(&raw,generation,positions,uv,colors,6);};
  ui.refuse=true;draw(1);assert(ui.mImpl->packets.empty()&&ui.mImpl->assets.empty()&&ui.mImpl->deferred_draw_batches==1);
  assert(ui.mImpl->font_pages.at(&raw).generation!=1);
  ui.refuse=false;draw(1);assert(ui.mImpl->packets.size()==1&&ui.mImpl->font_pages.at(&raw).generation==1);
  auto old=ui.mImpl->packets.front().image;raw.bytes[0]=17;
  ui.refuse=true;draw(2);assert(ui.mImpl->packets.size()==1&&old->pixels[0]==255&&ui.mImpl->deferred_draw_batches==2);
  ui.refuse=false;draw(2);assert(ui.mImpl->packets.size()==2&&ui.mImpl->packets.back().image->pixels[0]==17&&old->pixels[0]==255);
  auto key=ui.mImpl->font_pages.at(&raw).key;
  ui.mImpl->assets.erase(key);draw(2);assert(ui.mImpl->assets.count(key)&&ui.mImpl->packets.size()==3);
  raw.w=2;raw.bytes.resize(2*components,255);ui.refuse=true;draw(3);
  assert(ui.mImpl->packets.size()==3&&ui.mImpl->assets.at(key).asset->width==1&&ui.mImpl->deferred_draw_batches==3);
  ui.refuse=false;draw(3);assert(ui.mImpl->packets.size()==4&&ui.mImpl->assets.at(key).asset->width==2);
 }
 VSUIResources ui;ui.refuse=true;ui.rectangle(LLRectf{},LLColor4{});
 assert(ui.mImpl->packets.empty()&&ui.mImpl->assets.empty()&&ui.mImpl->deferred_draw_batches==1);
 ui.refuse=false;ui.rectangle(LLRectf{},LLColor4{});
 assert(ui.mImpl->packets.size()==1&&ui.mImpl->packets.front().image);
}
'''.replace('METHODS', methods)
        self.compile(fixture)

    def compile(self, fixture):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory)/'publication.cpp'; exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
            self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)


if __name__ == '__main__':
    unittest.main()
