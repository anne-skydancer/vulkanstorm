"""Check shared avatar LOD against the reference policy and mesh boundaries."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AvatarLODTest(unittest.TestCase):
    def test_reference_policy_across_detail_load_and_self_camera_modes(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        fixture = r'''
#include <cassert>
#include <cmath>
#include <array>
#include <algorithm>
#include "vsavatarlod.h"
// Reference clamp_rescale and updateJointLODs equations, independently
// evaluated with the original branch order (self, dummy, remote).
float rescale(float value,float low,float high,float minimum,float maximum){
 if(value<=low)return minimum;if(value>=high)return maximum;
 return minimum+(maximum-minimum)*(value-low)/(high-low);
}
float reference(float area,float factor,unsigned count,bool self,bool full){
 if(self){if(full)return 100000000.f;return area*.16f;}
 if(full)return 100000000.f;
 float lod=factor*.7f+(1.f-.7f);
 float lower=rescale(factor,0,1,.25f,.6f);
 float load=rescale(float(count),8,25,1,lower);
 return area*.16f*lod*lod*load*load;
}
int main(){
 const std::array<float,5> thresholds{20000,4000,500,80,0};
 auto select=[&](float area){unsigned index=0;while(index+1<thresholds.size()&&area<thresholds[index])++index;return index;};
 for(float area: {0.f,1.f,500.f,100000.f,100000000.f})
 for(float factor: {0.f,.25f,.5f,1.f,2.f,4.f})
 for(unsigned count: {0u,1u,7u,8u,9u,16u,24u,25u,26u,256u})
 for(bool self: {false,true})for(bool full: {false,true}){
  float expected=reference(area,factor,count,self,full);
  float actual=vs_avatar_lod_area(area,factor,count,self,full);
  assert(std::abs(expected-actual)<=std::max(.00001f,std::abs(expected)*.000001f));
  assert(select(expected)==select(actual));
 }
 for(float threshold: thresholds){
  const float raw=threshold/.16f;
  assert(select(vs_avatar_lod_area(raw,1,1,true,false))==select(threshold));
  if(threshold>0){
   assert(select(vs_avatar_lod_area(raw*.999f,1,1,true,false))>select(threshold));
   assert(select(vs_avatar_lod_area(raw*1.001f,1,1,true,false))==select(threshold));
  }
 }
 assert(vs_avatar_lod_area(10000,0,256,true,false)==1600);
 assert(vs_avatar_lod_area(10000,0,256,true,true)==100000000);
 assert(vs_avatar_lod_area(10000,1,25,false,false)<vs_avatar_lod_area(10000,1,8,false,false));
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary)/'avatar_lod.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                    '-I'+str(ROOT/'indra/newview'), str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)


if __name__ == '__main__':
    unittest.main()
