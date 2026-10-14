"""Compare the native rigid-volume LOD route with reference distance policy."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class VolumeLODTest(unittest.TestCase):
    def test_distance_ramp_bias_static_dynamic_hud_and_selection(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        header = (ROOT/'indra/newview/vsvolumelod.h').read_text().replace('#pragma once', '')
        header = '\n'.join(row for row in header.splitlines() if not row.startswith('#include "'))
        native = method((ROOT/'indra/newview/vsworldscene.cpp').read_text(), 'S32 meshDetail(')
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <map>
using F32=float;using S32=int;constexpr float F_PI=3.14159265358979323846f;
template<class T>T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}
template<class T>T llmin(T a,T b){return std::min(a,b);}
float ll_round(float v,float step){return std::floor(v/step+.5f)*step;}
struct LLVolumeLODGroup{static int getDetailFromTan(float v){return v<=.03f?0:v<=.06f?1:v<=.24f?2:3;}};
struct LLVector3{float mV[3];LLVector3(float x,float y,float z):mV{x,y,z}{}
 LLVector3 operator-(const LLVector3& v)const{return {mV[0]-v.mV[0],mV[1]-v.mV[1],mV[2]-v.mV[2]};}
 void scaleVec(const LLVector3& v){for(int i=0;i<3;++i)mV[i]*=v.mV[i];}
 float length()const{return std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);}};
struct Saved{float factor=1;int selection=-1;bool dynamic=true;
 float getF32(const char*)const{return factor;}int getS32(const char*)const{return selection;}
 bool getBOOL(const char*)const{return dynamic;}}gSavedSettings;
HEADER
NATIVE
int reference(float distance,float radius,float factor,bool dynamic){
 distance*=1-factor*.1f;float ramp=factor*2;
 if(distance<ramp){distance*=1/ramp;distance*=distance;distance*=ramp;}
 distance*=F_PI/3;
 distance=std::max(ll_round(distance,.01f),.01f);radius=ll_round(radius,.01f);
 if(dynamic)return LLVolumeLODGroup::getDetailFromTan(ll_round(factor*radius/distance,.01f));
 return std::clamp(int(std::sqrt(radius)*factor*4),0,3);
}
int main(){LLVector3 eye{0,0,0};
 assert(vs_volume_rigged_lod_radius(4,true)==2);
 assert(vs_volume_rigged_lod_radius(4,false)==4);
 for(float factor:{0.f,.01f,.25f,.5f,1.f,2.f,4.f,16.f})for(bool dynamic:{false,true})
 for(float distance:{.001f,.01f,.1f,.5f,1.f,2.f,4.f,8.f,16.f,32.f,64.f,128.f,512.f})
 for(LLVector3 scale:{LLVector3{.1f,.1f,.1f},LLVector3{1,1,1},LLVector3{1,4,20}})
 for(LLVector3 bias:{LLVector3{1,1,1},LLVector3{.1f,.5f,2}}){
  gSavedSettings.factor=factor;gSavedSettings.dynamic=dynamic;gSavedSettings.selection=-1;
  LLVector3 scaled=scale;scaled.scaleVec(bias);LLVector3 position{distance,0,0};
  assert(meshDetail(position,scale,eye,false,&bias)==reference(distance,scaled.length(),factor,dynamic));
  assert(meshDetail(position,scale,eye,true,&bias)==3);
  for(int forced:{0,1,2,3,4}){gSavedSettings.selection=forced;
   assert(meshDetail(position,scale,eye,false,&bias,true)==std::min(forced,3));}
 }
}
'''.replace('HEADER', header).replace('NATIVE', native)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary)/'volume_lod.cpp'; exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)


if __name__ == '__main__':
    unittest.main()
