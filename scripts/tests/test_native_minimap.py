"""Compile the production raw minimap rasters; compare pixels and bounded work."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[2]
def body(source, signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
class NativeMinimapTests(unittest.TestCase):
    def run_cpp(self, source):
        compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory()as d:
            cpp=Path(d)/'raster.cpp';exe=Path(d)/'raster.exe';cpp.write_text(source)
            result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_actual_shared_object_pixel_raster_clips_and_retains_chevron(self):
        source=(ROOT/'indra/newview/llnetmap.cpp').read_text()
        fixture=r'''
#include <cassert>
#include <vector>
#include <cstdint>
#include <cmath>
using S32=int;using U32=std::uint32_t;using U8=std::uint8_t;using F32=float;
constexpr int VX=0,VY=1,VZ=2;
int ll_round(float v){return int(std::floor(v+.5f));}
struct LLColor4U{U32 bits;U32 asRGBA()const{return bits;}};
struct LLVector3{float mV[3];};
struct Image{int side=8;std::vector<U32> data=std::vector<U32>(64,0);int getWidth(){return side;}int getHeight(){return side;}U8*getData(){return reinterpret_cast<U8*>(data.data());}};
struct LLImageDataLock{LLImageDataLock(Image*){}};
struct LLNetMap{Image image;Image*mObjectRawImagep=&image;float mObjectMapTPM=1;void renderPoint(const LLVector3&,const LLColor4U&,S32,S32=0);};
'''+body(source,'void LLNetMap::renderPoint(')+r'''
int main(){LLNetMap m;const LLColor4U color{0x78563412};m.renderPoint({{0,0,0}},color,2);for(int y=0;y<8;++y)for(int x=0;x<8;++x)assert(m.image.data[y*8+x]==((x>=3&&x<5&&y>=3&&y<5)?color.bits:0));
 m.image.data.assign(64,0);m.renderPoint({{-4,-4,0}},color,4);for(int y=0;y<8;++y)for(int x=0;x<8;++x)assert(m.image.data[y*8+x]==((x<2&&y<2)?color.bits:0));
 m.image.data.assign(64,0);m.renderPoint({{0,0,0}},color,3,1);assert(m.image.data[3*8+4]==color.bits&&m.image.data[4*8+4]==color.bits&&m.image.data[5*8+3]==color.bits&&m.image.data[5*8+5]==color.bits&&m.image.data[3*8+3]==0);
 auto before=m.image.data;m.renderPoint({{1000,0,0}},color,10);m.renderPoint({{0,-1000,0}},color,10);m.renderPoint({{0,0,0}},color,0);assert(m.image.data==before);
}
'''
        self.run_cpp(fixture)
    def test_actual_native_parcel_raster_unknown_chunks_collision_bounds_and_large_regions(self):
        source=(ROOT/'indra/newview/llnetmap.cpp').read_text()
        fixture=r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>
#include <memory>
#include <vector>
#include <algorithm>
using S32=int;using U32=std::uint32_t;using U8=std::uint8_t;using F32=float;constexpr int VX=0,VY=1;
constexpr int PARCEL_GRID_STEP_METERS=4,PARCEL_COLOR_MASK=7,PARCEL_FOR_SALE=4,PARCEL_AUCTION=5,PARCEL_SOUTH_LINE=128,PARCEL_WEST_LINE=64;
int ll_round(float v){return int(std::floor(v+.5f));}template<class T>T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}
struct LLColor3{template<class T>LLColor3(T){}};
struct LLColor4{static LLColor4 yellow;LLColor4(){}LLColor4(LLColor3,float){}};LLColor4 LLColor4::yellow;
struct LLColor4U{U8 r,g,b,a;LLColor4U(LLColor4):r(20),g(30),b(40),a(128){}LLColor4U(int R,int G,int B,int A):r(R),g(G),b(B),a(A){}U32 asRGBA()const{return r|(U32(g)<<8)|(U32(b)<<16)|(U32(a)<<24);}};
struct UIColor{LLColor4 get()const{return {};}};
struct LLUIColorTable{static LLUIColorTable&instance(){static LLUIColorTable t;return t;}UIColor getColor(const char*,LLColor4){return {};}};
struct LLVector3d{double x=0,y=0;};LLVector3d operator-(LLVector3d a,LLVector3d b){return {a.x-b.x,a.y-b.y};}
struct LLVector3{float mV[3];LLVector3(LLVector3d p):mV{float(p.x),float(p.y),0}{}};
LLVector3d from_region_handle(unsigned long long){return {};}
struct Overlay{mutable int calls=0;std::optional<U8>cell(unsigned x,unsigned y)const{++calls;if(x==1&&y==1)return U8(PARCEL_WEST_LINE|PARCEL_SOUTH_LINE);if(x==2&&y==1)return U8(PARCEL_FOR_SALE);if(x==3&&y==1)return U8(0);if(x==4&&y==1)return U8(PARCEL_AUCTION);return {};}};
struct Region{unsigned width=256,height=256;unsigned long long handle=1;std::shared_ptr<Overlay>parcelOverlay=std::make_shared<Overlay>();};
struct VSNativeSession{Region region;bool regionReady(){return true;}static std::shared_ptr<VSNativeSession>active();std::vector<Region>regionSnapshots(){return {region};}};
std::shared_ptr<VSNativeSession>session=std::make_shared<VSNativeSession>();std::shared_ptr<VSNativeSession>VSNativeSession::active(){return session;}
struct LLViewerParcelMgr{std::vector<U8>collision=std::vector<U8>(512,0);static LLViewerParcelMgr&instance(){static LLViewerParcelMgr m;return m;}unsigned long long getCollisionRegionHandle()const{return 1;}const U8*getCollisionBitmap()const{return collision.data();}size_t getCollisionBitmapSize()const{return collision.size();}};
struct Settings{bool getBOOL(const char*){return true;}}gSavedSettings;
template<class T>struct LLCachedControl{T value;LLCachedControl(Settings&,const char*):value(true){}operator T()const{return value;}};
struct Image{std::vector<U32>data=std::vector<U32>(1024,0);int getWidth(){return 32;}int getHeight(){return 32;}U8*getData(){return reinterpret_cast<U8*>(data.data());}};
struct LLNetMap{Image image;Image*mParcelRawImagep=&image;float mObjectMapTPM=1;LLVector3d mParcelImageCenterGlobal{16,16};void drawNativeParcels();};
'''+body(source,'void LLNetMap::drawNativeParcels()')+r'''
int main(){LLNetMap m;auto&manager=LLViewerParcelMgr::instance();manager.collision[8]=8;m.drawNativeParcels();
 assert(m.image.data[4*32+4]==LLColor4U(20,30,40,128).asRGBA());assert(m.image.data[5*32+5]==0);
 assert(m.image.data[5*32+9]==LLColor4U(255,255,128,192).asRGBA());assert(m.image.data[5*32+13]==LLColor4U(255,128,128,192).asRGBA());assert(m.image.data[5*32+17]==LLColor4U(128,0,255,102).asRGBA());
 manager.collision.resize(1);m.image.data.assign(1024,0);m.drawNativeParcels();assert(m.image.data[5*32+13]==0);
 session->region.width=256;session->region.height=512;m.mObjectMapTPM=.05f;m.mParcelImageCenterGlobal={320,320};m.image.data.assign(1024,0);m.drawNativeParcels();
 assert(m.image.data[26*32+5]==LLColor4U(20,30,40,128).asRGBA());assert(m.image.data[20*32+13]==LLColor4U(20,30,40,128).asRGBA());assert(m.image.data[13*32+5]==0&&m.image.data[28*32+13]==0);
 m.mObjectMapTPM=1;m.mParcelImageCenterGlobal={16,16};
 session->region.width=session->region.height=65536;session->region.parcelOverlay->calls=0;m.image.data.assign(1024,0);m.drawNativeParcels();assert(session->region.parcelOverlay->calls<200);assert(m.image.data[5*32+9]==LLColor4U(255,255,128,192).asRGBA());
 m.mParcelImageCenterGlobal={100000,100000};m.image.data.assign(1024,0);m.drawNativeParcels();for(auto pixel:m.image.data)assert(pixel==0);
}
'''
        self.run_cpp(fixture)
if __name__=='__main__':unittest.main()
