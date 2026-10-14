"""Check shared tree LOD thresholds, distance weighting and stop/restart policy."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class VegetationLODTest(unittest.TestCase):
    def test_tree_reference_distance_and_angle_policy(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        header = (ROOT/'indra/newview/vsvegetationlod.h').read_text().replace('#pragma once', '')
        header = '\n'.join(row for row in header.splitlines() if not row.startswith('#include "'))
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
using S32=int;using F32=float;constexpr float F_PI=3.14159265358979323846f,F_ALMOST_ZERO=.0001f;
template<class T>T llmin(T a,T b){return std::min(a,b);}
template<class T>T llmax(T a,T b){return std::max(a,b);}
float ll_round(float v,float step){return std::floor(v/step+.5f)*step;}
struct LLVolumeLODGroup{static int getDetailFromTan(float v){return v<=.03f?0:v<=.06f?1:v<=.24f?2:3;}};
HEADER
int reference(float angle,float factor,float distance,float diameter,float billboard,float ratio,float weighting){
 if(factor<F_ALMOST_ZERO)return 4;
 int trunk=4;const float angles[]{30,20,15,F_ALMOST_ZERO};
 for(int i=0;i<4;++i)if(angle*factor>angles[i]){trunk=i;break;}
 float range=std::max(distance*weighting*(F_PI/3),.01f);
 float tangent=factor*64*(billboard*ratio*diameter)/range;
 int volume=LLVolumeLODGroup::getDetailFromTan(ll_round(tangent,.01f));
 trunk=std::max(trunk,3-volume);return std::min(trunk,4);
}
int main(){
 for(float angle:{0.f,.0001f,14.99f,15.f,15.01f,20.f,20.01f,30.f,30.01f,180.f})
 for(float factor:{0.f,.00001f,.0001f,.5f,1.f,2.f,16.f})
 for(float distance:{.01f,.1f,1.f,8.f,32.f,256.f,1024.f})
 for(float diameter:{.1f,1.f,16.f})for(float billboard:{.5f,2.f})
 for(float ratio:{1.f,4.f})for(float weighting:{.2f,.8f,1.f})
  assert(vs_tree_lod(angle,factor,distance,diameter,billboard,ratio,weighting)==
         reference(angle,factor,distance,diameter,billboard,ratio,weighting));
 assert(vs_vegetation_stopped(0));assert(!vs_vegetation_stopped(1));
 assert(vs_tree_lod(180,0,1,10,1,1,1)==4);
 assert(vs_tree_lod(180,1,1,10,1,1,1)==0);
}
'''.replace('HEADER', header)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary)/'vegetation_lod.cpp'; exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)


if __name__ == '__main__':
    unittest.main()
