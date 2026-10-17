"""Compare native terrain coordinates to the reference producer, without a GPU."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class TerrainMaterials(unittest.TestCase):
    def test_composition_matches_reference_height_and_corner_policy(self):
        reference = (ROOT / 'indra/newview/llvlcomposition.cpp').read_text()
        bilinear = method(reference, 'F32 bilinear(')
        producer = method(reference, 'bool LLVLComposition::generateHeights(')
        producer = producer.replace('LLVLComposition::', 'Reference::')
        # Noise generation itself is unchanged/shared. A smooth, asymmetric
        # adapter makes argument, corner and coordinate mistakes observable.
        header = (ROOT / 'indra/newview/vsterrainmaterial.h').read_text()
        header = '\n'.join(line for line in header.splitlines()
                           if not line.startswith(('#pragma', '#include')))
        fixture = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <vector>
using F32=float; using S32=int;
template<class T>T llclamp(T a,T lo,T hi){return std::clamp(a,lo,hi);}
template<class T>T llmax(T a,T b){return std::max(a,b);}
template<class T>T llmin(T a,T b){return std::min(a,b);}
int ll_round(float v){return int(std::round(v));}
#define llassert assert
constexpr int SOUTHWEST=0,SOUTHEAST=1,NORTHWEST=2,NORTHEAST=3,VX=0,VY=1,ASSET_COUNT=4;
float noise2(float* p){return std::sin(p[0]*.17f)*std::cos(p[1]*.13f);}
float turbulence2(float* p,float f){float sum=0;for(;f>=1;f*=.5f){float v[2]={p[0]*f,p[1]*f};sum+=noise2(v)/f;}return sum;}
struct LLVector3{float mV[3];LLVector3(float x,float y,float z):mV{x,y,z}{}};
struct LLVector3d{double mdV[3];};
LLVector3d from_region_handle(int){return {{1048576,65536,0}};}
struct Region{int getHandle(){return 1;}};
struct Surface{Region region;Region* getRegion(){return &region;}
 float resolveHeightRegion(LLVector3 p){return p.mV[0]*.35f+p.mV[1]*.17f-17.f;}};
BILINEAR
struct Reference {
 bool mParamsReady=true;Surface surface;Surface* mSurfacep=&surface;
 int mWidth=256;float mScaleInv=1,mScale=1;
 std::array<float,4> mStartHeight{0,25,73,-12},mHeightRange{17,32,95,19};
 std::vector<float> data=std::vector<float>(256*256);float* mDatap=data.data();
 bool generateHeights(float,float,float,float);
};
PRODUCER
HEADER
int main(){Reference ref;assert(ref.generateHeights(0,0,256,256));
 const auto origin=from_region_handle(1);
 for(int y=0;y<256;++y)for(int x=0;x<256;++x){
  const auto value=VSTerrainMaterial::coordinates(origin.mdV[0],origin.mdV[1],float(x),float(y),
   ref.surface.resolveHeightRegion({float(x),float(y),0}),256,ref.mStartHeight,ref.mHeightRange);
  assert(std::abs(value[0]-ref.data[x+y*256])<.00002f);
  const float scale=(1.f/(4.9215f*7.f))*.2222222222f;
  float random[3]={float(std::fmod(float(origin.mdV[0]+x)*scale,256.f)),
   float(std::fmod(float(origin.mdV[1]+y)*scale,256.f)),0};
  assert(std::abs(value[1]-std::clamp(noise2(random)*.75f+.5f,0.f,1.f))<.000001f);
 }
 ref.mHeightRange.fill(0);
 for(float z:{-100.f,0.f,100.f}){
  auto v=VSTerrainMaterial::coordinates(0,0,0,0,z,256,ref.mStartHeight,ref.mHeightRange);
  assert(std::isfinite(v[0]) && v[0]>=0 && v[0]<=3);
 }
}
'''.replace('BILINEAR', bilinear).replace('PRODUCER', producer).replace('HEADER', header)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'terrain.cpp'
            binary = path.with_suffix('.exe')
            path.write_text(fixture)
            build = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra',
                                    '-Werror', '-Wno-unused-parameter', str(path), '-o', str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
