"""Exercise the shared wind field on rectangular regions and wrap boundaries."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]


class WorldWind(unittest.TestCase):
    def test_rectangular_interpolation_wrap_and_invalid_coordinates(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        fixture=r'''
#include <array>
#include <cassert>
#include <limits>
HEADER
int main() {
 std::array<float,256> x{},y{};
 for (unsigned j=0;j<16;++j) for(unsigned i=0;i<16;++i) {x[i+j*16]=float(i);y[i+j*16]=float(j);}
 auto value=vs_wind_velocity(x,y,LLVector3(20,40,100),256,512);
 assert(value.mV[0]==2.5f && value.mV[1]==2.5f && value.mV[2]==0);
 const auto wrapped=vs_wind_velocity(x,y,LLVector3(276,552,0),256,512);
 assert(wrapped.mV[0]==value.mV[0] && wrapped.mV[1]==value.mV[1]);
 value=vs_wind_velocity(x,y,LLVector3(-20,-40,0),256,512);assert(value.mV[0]==0 && value.mV[1]==0);
 value=vs_wind_velocity(x,y,LLVector3(250,505,0),256,512);assert(value.mV[0]==30 && value.mV[1]==30);
 value=vs_wind_velocity(x,y,LLVector3(std::numeric_limits<float>::quiet_NaN(),0,0),256,512);assert(value.mV[0]==0);
 value=vs_wind_velocity(x,y,LLVector3(20,40,0),0,512);assert(value.mV[0]==0);
 value=vs_wind_velocity(x,y,LLVector3(20,40,0),256,std::numeric_limits<float>::infinity());assert(value.mV[0]==0);
}
'''
        header=(ROOT/'indra/newview/vswindfield.h').read_text().replace('#include "v3math.h"','')
        adapter=r'''
#include <cmath>
struct LLVector3 {
 float mV[3];LLVector3(float x,float y,float z):mV{x,y,z}{}
 bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}
 LLVector3 operator*(float value)const{return {mV[0]*value,mV[1]*value,mV[2]*value};}
 static const LLVector3 zero;
};const LLVector3 LLVector3::zero{0,0,0};
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'wind.cpp';cpp.write_text(adapter+fixture.replace('HEADER',header.replace('#pragma once','')))
            binary=cpp.with_suffix('.exe')
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            tested=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(tested.returncode,0,tested.stdout+tested.stderr)


if __name__=='__main__': unittest.main()
