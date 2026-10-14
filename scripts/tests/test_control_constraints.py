"""Verify shared control-avatar extent rules and recursive scale restoration."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]


class ControlConstraints(unittest.TestCase):
    def test_offset_size_attachment_and_joint_restoration(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        source=(ROOT/'indra/llmath/v3math.cpp').read_text()
        helpers=method(source,'LLVector3 point_to_box_offset(')+method(source,'bool box_valid_and_non_zero(')
        header=(ROOT/'indra/newview/vscontrolconstraints.h').read_text()
        header='\n'.join(line for line in header.splitlines() if not line.startswith(('#include','#pragma')))
        fixture=r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>
using S32=int;using F32=float;
struct LLVector3 {
 float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{}
 bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}
 void clear(){mV[0]=mV[1]=mV[2]=0;}
 float& operator[](unsigned index){return mV[index];}float operator[](unsigned index)const{return mV[index];}
 LLVector3 operator-(const LLVector3& other)const{return {mV[0]-other.mV[0],mV[1]-other.mV[1],mV[2]-other.mV[2]};}
 LLVector3 operator*(float factor)const{return {mV[0]*factor,mV[1]*factor,mV[2]*factor};}
 friend LLVector3 operator*(float factor,const LLVector3& value){return value*factor;}
 bool operator==(const LLVector3&)const=default;
 float length()const{return std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);}
};
struct LLJoint {
 LLVector3 scale{1,1,1};std::vector<LLJoint*> mChildren;
 const LLVector3& getScale()const{return scale;}void setScale(const LLVector3& value){scale=value;}
};
HELPERS
HEADER
int main() {
 LLVector3 bounds[]={{5,-1,-1},{7,1,1}};
 auto result=vs_control_constraints({},bounds,{},1,false,3,64);
 assert(result.position==LLVector3(-2,0,0) && result.scale==1);
 LLVector3 fixed[]={{3,-1,-1},{5,1,1}};
 result=vs_control_constraints({},fixed,LLVector3(-2,0,0),1,false,3,64);
 assert(result.position==LLVector3(-2,0,0)); // Removing the old fixup avoids drift.
 result=vs_control_constraints({},bounds,{},1,true,3,64);assert(result.position==LLVector3{});
 LLVector3 huge[]={{-100,-1,-1},{100,1,1}};
 result=vs_control_constraints({},huge,{},1,false,3,64);assert(std::abs(result.scale-.32f)<1e-6f);
 LLVector3 shrunk[]={{-32,-1,-1},{32,1,1}};
 result=vs_control_constraints({},shrunk,{},.32f,false,3,64);assert(std::abs(result.scale-.32f)<1e-6f);
 result=vs_control_constraints({},nullptr,{},1,false,3,64);assert(result.scale==1 && result.position==LLVector3{});
 LLJoint pelvis,collision,limb;pelvis.mChildren={&collision,&limb};
 vs_scale_avatar_joints(&pelvis,.5f);assert(pelvis.scale==LLVector3(.5f,.5f,.5f) && collision.scale==pelvis.scale && limb.scale==pelvis.scale);
 vs_scale_avatar_joints(&pelvis,2);assert(pelvis.scale==LLVector3(1,1,1) && collision.scale==pelvis.scale && limb.scale==pelvis.scale);
}
'''.replace('HELPERS',helpers).replace('HEADER',header)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'constraints.cpp';cpp.write_text(fixture);binary=cpp.with_suffix('.exe')
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            tested=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(tested.returncode,0,tested.stdout+tested.stderr)


if __name__=='__main__': unittest.main()
