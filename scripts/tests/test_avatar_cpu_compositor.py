"""Exercise production compositor sampling, masks, blending and channel writes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class AvatarCPUCompositor(unittest.TestCase):
    def test_reference_blending_orientation_masks_and_replacement(self):
        source = (ROOT / 'indra/llappearance/vstexlayercpu.cpp').read_text()
        production = '\n'.join(method(source, signature) for signature in (
            'std::array<F32,4> sample(', 'U8 unorm(', 'bool LLTexLayer::renderCPU('))
        fixture = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
using U8=uint8_t;using U16=uint16_t;using S32=int;using F32=float;
template<class T> using LLPointer=std::shared_ptr<T>;
template<class T>T llclamp(T value,T low,T high){return std::clamp(value,low,high);}
int ll_round(float value){return int(std::round(value));}
bool is_approx_zero(float value){return std::abs(value)<1.e-6f;}
constexpr int IMG_DEFAULT_AVATAR=0;
struct LLImageRaw {
 int width,height,components;std::vector<U8> pixels;
 LLImageRaw(int w,int h,int c):width(w),height(h),components(c),pixels(w*h*c){}
 int getWidth()const{return width;}int getHeight()const{return height;}
 int getComponents()const{return components;}
 U8* getData(){return pixels.data();}const U8* getData()const{return pixels.data();}
};
struct LLColor4 {float mV[4]={1,1,1,1};};
using LLTexLayerCPUAllocate=std::function<LLPointer<LLImageRaw>(int,int,int)>;
struct LLTexLayerCPUTexture {
 std::function<LLPointer<LLImageRaw>(int)> load;LLTexLayerCPUAllocate allocate;
 std::function<void(const std::string&)> reportFailure;
 LLPointer<LLImageRaw> operator()(int id)const{return load(id);}
 bool fail(const std::string& reason)const{if(reportFailure)reportFailure(reason);return false;}
};
LLPointer<LLImageRaw> staticImage(const std::string&,const LLTexLayerCPUAllocate&){return {};}
struct Param {
 float weight=.5f;bool multiply=false,skip=false;
 bool getSkip()const{return skip;}bool getMultiplyBlend()const{return multiply;}
 bool prepareCPU(LLPointer<LLImageRaw>& image,float& effective,const LLTexLayerCPUAllocate&){image={};effective=weight;return true;}
};
struct Local {int getID()const{return 1;}};
struct Info {
 int mLocalTexture=0;std::string mStaticImageFileName;
 bool mStaticImageIsMask=false,mUseLocalTextureAlphaOnly=false,mWriteAllChannels=false;
 std::string getName()const{return "Fixture layer";}
};
struct LLTexLayer {
 Info* mInfo;Local* mLocalTextureObject;std::vector<Param*> mParamAlphaList;
 LLPointer<LLImageRaw> mCPUMorphMask;bool mMorphMasksValid=false,morph=false;
 LLColor4 color;
 bool hasMorph()const{return morph;}
 bool findNetColor(LLColor4* output){*output=color;return true;}
 bool renderCPU(LLImageRaw&,const LLTexLayerCPUTexture&,bool alphaOnly=false);
};
PRODUCTION
void expect(const LLImageRaw& image,std::array<int,4> expected){
 for(unsigned channel=0;channel<4;++channel)assert(std::abs(int(image.pixels[channel])-expected[channel])<=1);
}
int main(){
 auto image=std::make_shared<LLImageRaw>(1,1,4);image->pixels={255,0,0,128};
 LLTexLayerCPUTexture assets{[&](int){return image;},[](int w,int h,int c){return std::make_shared<LLImageRaw>(w,h,c);}};
 Info info;Local local;LLTexLayer layer{&info,&local};LLImageRaw target(1,1,4);
 target.pixels={0,0,255,255};assert(layer.renderCPU(target,assets));expect(target,{128,0,127,191});
 // Destination-alpha masking follows the reference separate mask pass.
 Param param;layer.mParamAlphaList={&param};layer.morph=true;
 target.pixels={0,0,255,255};assert(layer.renderCPU(target,assets));expect(target,{64,0,191,80});
 assert(layer.mCPUMorphMask&&layer.mMorphMasksValid&&layer.mCPUMorphMask->pixels[0]==64);
 auto oldMask=layer.mCPUMorphMask;param.weight=1;
 target.pixels={0,0,255,255};assert(layer.renderCPU(target,assets));
 assert(oldMask->pixels[0]==64&&layer.mCPUMorphMask!=oldMask);
 // Replace writes all channels, and alpha-only composition preserves RGB.
 info.mWriteAllChannels=true;layer.mParamAlphaList.clear();
 target.pixels={0,0,255,255};assert(layer.renderCPU(target,assets));expect(target,{255,0,0,128});
 target.pixels={10,20,30,255};assert(layer.renderCPU(target,assets,true));expect(target,{10,20,30,128});
 // Stored bottom-row-first images preserve both rows and texel centers.
 LLImageRaw rows(1,2,4);rows.pixels={255,0,0,255,0,255,0,255};
 assert(sample(rows,.5f,.25f,false)[0]==1&&sample(rows,.5f,.75f,false)[1]==1);
 const auto middle=sample(rows,.5f,.5f,false);assert(middle[0]==.5f&&middle[1]==.5f);
 assets.allocate=[](int,int,int)->LLPointer<LLImageRaw>{return {};};
 layer.mParamAlphaList={&param};assert(!layer.renderCPU(target,assets));
 std::string failure;assets.reportFailure=[&](const std::string& value){failure=value;};
 assets.load=[](int)->LLPointer<LLImageRaw>{return {};};
 const auto unchanged=target.pixels;assert(!layer.renderCPU(target,assets));
 assert(failure=="Local texture unavailable: Fixture layer"&&target.pixels==unchanged);
}
'''.replace('PRODUCTION', production)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'compositor.cpp'
            binary = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++20', str(cpp), '-o', str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
