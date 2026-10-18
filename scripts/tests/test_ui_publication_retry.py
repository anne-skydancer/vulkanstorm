"""Exercise local font-atlas reuse and complete-frame capture admission."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class UIPublicationRetryTest(unittest.TestCase):
    def test_solid_rectangle_and_triangle_retry_without_incomplete_capture(self):
        source = (ROOT/'indra/newview/vsuiresources.cpp').read_text(encoding='utf-8')
        methods = '\n'.join(method(source, name) for name in
            ('void VSUIResources::rectangle(', 'void VSUIResources::triangle('))
        self.compile(r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
using U8=unsigned char;
template<class T>struct LLPointer:std::shared_ptr<T>{LLPointer(T* p):std::shared_ptr<T>(p){}};
struct LLImageRaw{LLImageRaw(int,int,int){}void clear(int,int,int,int){}};
struct LLRectf{float mLeft,mTop,mRight,mBottom;float getWidth()const{return mRight-mLeft;}float getHeight()const{return mTop-mBottom;}};
struct LLColor4{float mV[4];};
float nativeVertexColor(float c){return U8(std::clamp(c,0.f,1.f)*255)/255.f;}
int nativeBlend(){return 2;}
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct VSUIRenderer{
 using Image=std::shared_ptr<int>;
 struct Vertex{float x,y,u,v,r,g,b,a;};
 struct Packet{int blend;Image image;std::array<float,4> bounds,clip,color;std::optional<std::array<Vertex,6>> triangles;};
};
struct VSUIResources{
 struct Asset{VSUIRenderer::Image image;};
 struct Entry{std::shared_ptr<Asset> asset;};
 struct Impl{bool active=true;unsigned deferred_draw_batches=0;float dpi=2,logical_height=10;
  std::array<float,4> clip{1,2,3,4};std::map<std::string,Entry> assets;std::vector<VSUIRenderer::Packet> packets;};
 std::unique_ptr<Impl> mImpl=std::make_unique<Impl>();
 bool refuse=true;unsigned uploads=0;
 bool publish(const std::string& key,const LLImageRaw&){++uploads;if(refuse)return false;
  mImpl->assets[key]={std::make_shared<Asset>(Asset{std::make_shared<int>(int(uploads))})};return true;}
 void rectangle(const LLRectf&,const LLColor4&);
 void triangle(const std::array<std::array<float,6>,3>&);
};
METHODS
int main(){
 for(bool triangle:{false,true}){
  VSUIResources ui;
  auto draw=[&]{if(triangle)ui.triangle({{{2,4,.1f,.2f,.3f,.4f},{4,4,.2f,.3f,.4f,.5f},{2,6,.3f,.4f,.5f,.6f}}});
   else ui.rectangle({2,6,4,4},{{.1f,.2f,.3f,.4f}});};
  draw();draw();assert(ui.mImpl->packets.empty()&&ui.mImpl->assets.empty());
  assert(ui.mImpl->deferred_draw_batches==2&&ui.uploads==2);
  ui.refuse=false;ui.mImpl->deferred_draw_batches=0;draw();
  assert(ui.mImpl->packets.size()==1&&ui.mImpl->deferred_draw_batches==0);
  auto original=ui.mImpl->packets.front().image;draw();
  assert(ui.uploads==3&&ui.mImpl->packets.size()==2&&ui.mImpl->packets.back().image==original);
  assert(ui.mImpl->packets.front().clip==ui.mImpl->clip&&ui.mImpl->packets.front().blend==2);
  if(triangle){auto v=(*ui.mImpl->packets[0].triangles)[0];assert(v.x==1&&v.y==8&&v.r==nativeVertexColor(.1f));
   auto tail=(*ui.mImpl->packets[0].triangles)[5];assert(tail.x==1&&tail.y==7);}
  ui.mImpl->assets.clear();ui.refuse=true;draw();
  assert(ui.mImpl->packets.size()==2&&ui.mImpl->deferred_draw_batches==1&&*original==3);
  ui.refuse=false;draw();assert(ui.mImpl->packets.back().image!=original);
 }
}
'''.replace('METHODS', methods))

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

    def test_local_font_pages_reuse_atlas_and_coalesce_updates(self):
        source = (ROOT/'indra/newview/vsuiresources.cpp').read_text()
        methods = '\n'.join(method(source, name) for name in
            ('void VSUIResources::fontBatch(', 'std::vector<VSUIRenderer::Packet> VSUIResources::finish(',
             'void VSUIResources::releaseFontPages('))
        page = source[source.index('struct FontPage'):source.index('static std::vector<std::uint8_t> fontPixels')]
        pixels = method(source, 'static std::vector<std::uint8_t> fontPixels(')
        diagnostic = (ROOT/'indra/newview/vsvkdiag.cpp').read_text()
        start = diagnostic.index('require(font_packets.front().image')
        end = diagnostic.index('packets.insert(packets.end(),font_packets.begin()', start)
        checks = diagnostic[start:end].replace('ui->', 'renderer->')
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
 struct Texture{std::vector<U8> pixels;unsigned w,h;};
 using Image=std::shared_ptr<const Texture>;using FontAtlas=std::shared_ptr<Texture>;
 struct Vertex{float x,y,u,v,r,g,b,a;};
 struct Packet{int blend;Image image;std::array<float,4> clip;std::optional<std::array<Vertex,6>> triangles;};
 unsigned allocations=0,updates=0;bool refuse=false;
 struct Statistics{unsigned residentImageBytes;};
 Statistics statistics()const{return {allocations*32};}
 static std::uintptr_t generation(const Image& image){return reinterpret_cast<std::uintptr_t>(image.get());}
 FontAtlas createFontAtlas(unsigned w,unsigned h,const std::vector<U8>& bytes){
  if(refuse)throw std::runtime_error("allocation refused");
  ++allocations;return std::make_shared<Texture>(Texture{bytes,w,h});
 }
 void updateFontAtlas(const FontAtlas& atlas,const std::vector<U8>& bytes){++updates;atlas->pixels=bytes;}
 static std::array<unsigned,2> imageExtent(const Image& atlas){return {atlas->w,atlas->h};}
};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void require(bool value,const char* message){check(value,message);}
int nativeBlend(){return 0;}
struct VSUIResources {
 struct Impl {
  struct Asset{unsigned width,height;VSUIRenderer::Image image;};
  struct Entry{std::shared_ptr<Asset> asset;};
  PAGE
  PIXELS
  std::map<LLImageRaw*,FontPage> font_pages;std::map<std::string,Entry> assets;
  std::vector<VSUIRenderer::Packet> packets;std::array<float,4> clip{0,0,10,10};
  bool active=true;float dpi=2,logical_height=10;VSUIRenderer renderer;
 };
 std::unique_ptr<Impl> mImpl=std::make_unique<Impl>();
 void fontBatch(LLImageRaw*,S32,const LLVector4a*,const LLVector2*,const LLColor4U*,S32);
 std::vector<VSUIRenderer::Packet> finish();void releaseFontPages();
};
METHODS
int main(){
 LLVector4a positions[6];LLVector2 uv[6];LLColor4U colors[6];
 positions[0].values[0]=6;positions[0].values[1]=4;uv[0].mV[1]=.25f;
 for(int components:{2,4}){
  VSUIResources ui;LLImageRaw raw(2,2,components);
  auto draw=[&](int generation){ui.fontBatch(&raw,generation,positions,uv,colors,6);};
  ui.mImpl->renderer.refuse=true;bool failed=false;
  try{draw(1);}catch(const std::runtime_error&){failed=true;}
  assert(failed&&ui.mImpl->packets.empty());
  ui.mImpl->renderer.refuse=false;draw(1);
  auto* renderer=&ui.mImpl->renderer;const auto atlasBytes=renderer->statistics().residentImageBytes;
  auto original=ui.mImpl->packets.front().image;
  for(int generation=2;generation<=100;++generation){raw.bytes[0]=generation;draw(generation);}
  assert(ui.mImpl->renderer.allocations==1&&ui.mImpl->renderer.updates==0);
  auto packets=ui.finish();assert(packets.size()==100&&ui.mImpl->renderer.updates==1);
  auto font_packets=packets;
  CHECKS
  assert(original==packets.back().image&&original->pixels[8]==100);
  assert(original->pixels[9]==(components==2?100:255)&&original->pixels[11]==255);
  assert((*packets[0].triangles)[0].x==3&&(*packets[0].triangles)[0].y==8);
  assert((*packets[0].triangles)[0].v==.75f);
  ui.mImpl->active=true;draw(100);ui.finish();assert(ui.mImpl->renderer.updates==1);
  ui.releaseFontPages();assert(ui.mImpl->assets.empty()&&ui.mImpl->font_pages.empty());
  assert(original->pixels[8]==100); // queued draw survives reset
  ui.mImpl->active=true;draw(101);ui.finish();assert(ui.mImpl->renderer.allocations==2);
  assert(original!=ui.mImpl->assets.begin()->second.asset->image);
 }
}
'''
        self.compile(fixture.replace('PAGE', page).replace('PIXELS', pixels).replace('METHODS', methods).replace('CHECKS', checks))

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
