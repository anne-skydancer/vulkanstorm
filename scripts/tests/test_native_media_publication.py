"""Execute native media publication against refusal, resize and shared-consumer cases."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class MediaPublication(unittest.TestCase):
    def test_generation_and_dirty_retry(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        body=method((ROOT/'indra/newview/vsmediaimage.cpp').read_text(),'LLUIImagePtr vs_update_media_image(')
        fixture=r'''
#include "vsmediapixels.h"
#include <array>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
using S32=int;
constexpr int GL_UNSIGNED_BYTE=1,GL_RGB=2,GL_RGBA=3,GL_BGRA=4;
struct LLRect {int mLeft=0,mBottom=0,mRight=2,mTop=2;};
struct LLImageRaw {unsigned w,h;std::vector<std::uint8_t> pixels;
 LLImageRaw(unsigned x,unsigned y,int):w(x),h(y),pixels(x*y*4){}
 std::uint8_t* getData(){return pixels.data();}};
template<class T> struct LLPointer:std::shared_ptr<T>{LLPointer(T* p):std::shared_ptr<T>(p){}};
struct Texture {unsigned w,h;std::vector<std::uint8_t> pixels;};
using LLUIImagePtr=std::shared_ptr<Texture>;
struct VSUIRenderer {static std::array<unsigned,2> imageExtent(const LLUIImagePtr& t){return {t->w,t->h};}};
struct VSUIResources {
 LLUIImagePtr current;bool refuse=false;unsigned publishes=0,patches=0;
 LLUIImagePtr facade(const std::string&){return current;}
 LLUIImagePtr image(const std::string&){return current;}
 LLUIImagePtr publish(const std::string&,const LLImageRaw& raw){
  if(refuse)return {}; ++publishes; current=std::make_shared<Texture>(Texture{raw.w,raw.h,raw.pixels});return current;
 }
 bool patch(const std::string&,unsigned x,unsigned y,unsigned w,unsigned h,const std::vector<std::uint8_t>& p){
  if(refuse)return false; ++patches;auto next=std::make_shared<Texture>(*current);
  for(unsigned i=0;i<h;++i)std::memcpy(next->pixels.data()+((y+i)*next->w+x)*4,p.data()+i*w*4,w*4);
  current=next;return true;
 }
};
struct LLPluginClassMedia {
 int width=2,height=2;bool dirty=true,valid=true,bottom=true;int resets=0,depth=4;
 std::vector<std::uint8_t> data{10,20,30,40,50,60,70,80,11,21,31,41,51,61,71,81};
 LLRect rect;
 bool textureValid(){return valid;}int getWidth(){return width;}int getHeight(){return height;}
 int getBitsWidth(){return width;}int getBitsHeight(){return height;}
 std::uint8_t* getBitsData(){return data.data();}int getTextureDepth(){return depth;}
 int getTextureFormatType(){return GL_UNSIGNED_BYTE;}bool getTextureFormatSwapBytes(){return false;}
 int getTextureFormatInternal(){return GL_RGBA;}int getTextureFormatPrimary(){return GL_RGBA;}
 bool getTextureCoordsOpenGL(){return bottom;}
 bool getDirty(LLRect* out){*out=rect;return dirty;}void resetDirty(){dirty=false;++resets;}
};
BODY
int main(){
 VSUIResources resources;LLPluginClassMedia plugin;
 plugin.dirty=false;assert(!vs_update_media_image(resources,"media:test",plugin) && resources.publishes==0);
 plugin.dirty=true;resources.refuse=true;assert(!vs_update_media_image(resources,"media:test",plugin));
 assert(plugin.dirty && plugin.resets==0);
 resources.refuse=false;auto first=vs_update_media_image(resources,"media:test",plugin);
 assert(first && !plugin.dirty && plugin.resets==1 && resources.publishes==1);
 vs_update_media_image(resources,"media:test",plugin);assert(resources.patches==0);
 plugin.rect={1,0,2,1};plugin.data[4]=99;plugin.dirty=true;resources.refuse=true;
 assert(vs_update_media_image(resources,"media:test",plugin)==first && plugin.dirty);
 resources.refuse=false;vs_update_media_image(resources,"media:test",plugin);
 assert(!plugin.dirty && resources.patches==1 && first->pixels[4]==50);
 assert(resources.current->pixels[12]==99); // bottom source row -> canonical final row
 auto second=resources.current;plugin.width=1;plugin.height=1;plugin.dirty=false;
 assert(vs_update_media_image(resources,"media:test",plugin)==second && resources.publishes==1);
 plugin.dirty=true;resources.refuse=true;
 assert(vs_update_media_image(resources,"media:test",plugin)==second && plugin.dirty);
 resources.refuse=false;vs_update_media_image(resources,"media:test",plugin);
 assert(resources.publishes==2 && resources.current->w==1 && !plugin.dirty);
 plugin.depth=3;plugin.dirty=true;vs_update_media_image(resources,"media:test",plugin);assert(plugin.dirty);
}
'''.replace('BODY',body)
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'media_publication.cpp';source.write_text(fixture)
            exe=Path(temp)/'media_publication.exe'
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'indra/newview'),str(source),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
