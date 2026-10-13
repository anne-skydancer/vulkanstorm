"""Production motion policy: authoritative state, selection, timing and correction."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class MotionContracts(unittest.TestCase):
    def test_global_origin_subtracts_before_float_conversion(self):
        root=Path(__file__).resolve().parents[2]
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);source=path/'origin.cpp';executable=path/'origin.exe'
            source.write_text('''
#include "vsworldcontracts.h"
#include <cassert>
int main(){
 auto near=VSWorldContracts::regionOffset(0xfffffe00ffffff00ULL,0xffffff00fffffe00ULL);
 assert(near[0]==256 && near[1]==-256 && near[2]==0 && near[3]==0);
 auto back=VSWorldContracts::regionOffset(0xffffff00fffffe00ULL,0xfffffe00ffffff00ULL);
 assert(back[0]==-256 && back[1]==256);
 auto zero=VSWorldContracts::regionOffset(0,0);
 assert(zero[0]==0 && zero[1]==0);
}
''')
            built=subprocess.run([compiler,'-std=c++17','-I',str(root/'indra/newview'),str(source),'-o',str(executable)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            self.assertEqual(subprocess.run([str(executable)],capture_output=True).returncode,0)
    def test_authority_and_motion_policy(self):
        root=Path(__file__).resolve().parents[2]
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        vector=r'''
#pragma once
#include <cmath>
struct LLVector3 {
 float mV[3]{};
 LLVector3(float x=0,float y=0,float z=0):mV{x,y,z}{}
 bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}
 float length()const{return std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);}
 LLVector3 operator+(const LLVector3& b)const{return {mV[0]+b.mV[0],mV[1]+b.mV[1],mV[2]+b.mV[2]};}
 LLVector3 operator*(float s)const{return {mV[0]*s,mV[1]*s,mV[2]*s};}
 LLVector3 operator/(float s)const{return *this*(1/s);}
 LLVector3& operator+=(const LLVector3& b){*this=*this+b;return *this;}
 bool operator!=(const LLVector3& b)const{return mV[0]!=b.mV[0]||mV[1]!=b.mV[1]||mV[2]!=b.mV[2];}
};
'''
        quaternion=r'''
#pragma once
#include "v3math.h"
struct LLQuaternion {
 float z=0,w=1;
 bool isFinite()const{return std::isfinite(z)&&std::isfinite(w);}
 void loadIdentity(){z=0;w=1;}
 void setQuat(float angle,const LLVector3& axis){z=axis.mV[2]*std::sin(angle*.5f);w=std::cos(angle*.5f);}
 LLQuaternion operator*(const LLQuaternion& b)const{return {z*b.w+w*b.z,w*b.w-z*b.z};}
 void normalize(){const float n=std::sqrt(z*z+w*w);z/=n;w/=n;}
 bool operator!=(const LLQuaternion& b)const{return z!=b.z||w!=b.w;}
};
'''
        fixture=r'''
#include "vsworldmotion.h"
#include <cassert>
#include <limits>
bool close(float a,float b){return std::abs(a-b)<.00001f;}
int main(){
 VSWorldMotion m;VSWorldMotion::Policy policy;
 assert(m.accept({}, {}, {10,0,0}, {}, {0,0,1},0));
 assert(m.update(.1,1,true,false,policy));
 assert(close(m.position().mV[0],1)&&close(m.authoritative().mV[0],0));
 assert(close(m.rotation().z,std::sin(.05f)));
 assert(!m.accept({99,0,0},{},{},{},{},-.1));
 m.freeze(true);assert(!m.update(.2,1,true,false,policy));assert(close(m.position().mV[0],1));
 m.freeze(false);assert(m.update(.3,1,true,false,policy));assert(close(m.position().mV[0],2));
 const auto position=m.position();assert(m.update(.4,1,true,true,policy));assert(!(m.position()!=position));
 assert(m.update(2,1,false,false,policy));assert(close(m.position().mV[0],3));
 m.update(4,1,false,false,policy);assert(close(m.position().mV[0],3));
 assert(close(m.authoritative().mV[0],0));
 VSWorldMotion accelerated;
 assert(accelerated.accept({}, {}, {10,0,0}, {9,0,0},{},0));
 accelerated.update(.1,1,true,false,policy);
 assert(close(accelerated.position().mV[0],1.035f)&&close(accelerated.velocity().mV[0],10.9f));
 VSWorldMotion dilated;dilated.accept({}, {}, {10,0,0}, {},{},0);
 dilated.update(1,.5f,true,false,policy);assert(close(dilated.position().mV[0],1));
 VSWorldMotion disabled;disabled.accept({2,0,0},{},{10,0,0},{},{},0);
 policy.interpolate=false;assert(!disabled.update(.1,1,true,false,policy));assert(close(disabled.position().mV[0],2));
 assert(!disabled.accept({std::numeric_limits<float>::quiet_NaN(),0,0},{},{},{},{},1));
 assert(close(disabled.authoritative().mV[0],2));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory=Path(directory)
            (directory/'v3math.h').write_text(vector)
            (directory/'llquaternion.h').write_text(quaternion)
            (directory/'vsworldmotion.h').write_bytes((root/'indra/newview/vsworldmotion.h').read_bytes())
            source=directory/'motion.cpp';source.write_text(fixture)
            executable=directory/'motion.exe'
            built=subprocess.run([compiler,'-std=c++17',str(source),'-o',str(executable)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(executable)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__=='__main__':unittest.main()
