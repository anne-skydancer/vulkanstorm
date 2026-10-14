"""Compare the native cloth kernel with the reference deferred vertex shader.

The reference evaluates eye-space normals, wind, gravity and skinned depth.
The native kernel evaluates the equivalent dot products in world space.
This checks both displacement and the perturbed normal, including coverage,
period wrapping, view orientation, gravity and the underlying-body clamp.
"""
from pathlib import Path
import math
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def normalized(v):
    length = math.sqrt(dot(v, v))
    return tuple(x/length for x in v)


class AvatarClothTest(unittest.TestCase):
    def test_reference_shader_displacement_and_normals(self):
        shader = (ROOT/'indra/newview/app_settings/shaders/class1/deferred/avatarV.glsl').read_text()
        coefficients = tuple(float(v.strip()) for v in re.search(
            r'gMinMaxConstants\s*=\s*vec4\(([^)]+)\)', shader)[1].split(','))
        # Keep the independent shader translation tied to the reference source.
        for expression in ('dot(norm, gWindDir.xyz)', 'pos.x * vec3(0.015',
                           'temp1.z = temp1.z - gPiConstants.w',
                           'sinWave.xyz = sinWave.xyz * clothing.w',
                           'offsetPos = clothing * sinWave.x', 'norm += temp2.xyz*2.0'):
            self.assertIn(expression, shader)
        pi_constants = tuple(float(v.strip()) for v in re.search(
            r'gPiConstants\s*=\s*vec4\(([^)]+)\)', shader)[1].split(','))
        header = (ROOT/'indra/newview/vsavatarcloth.h').read_text().replace('#pragma once', '').replace('#include "v3math.h"', '')
        adapter = r'''
#include <iostream>
#include <iomanip>
#include <cmath>
using F32=float;
struct LLVector3 {
 float mV[3]{};LLVector3()=default;
 LLVector3(float x,float y,float z):mV{x,y,z}{}
 LLVector3 operator-(const LLVector3& b)const{return {mV[0]-b.mV[0],mV[1]-b.mV[1],mV[2]-b.mV[2]};}
 LLVector3 operator+(const LLVector3& b)const{return {mV[0]+b.mV[0],mV[1]+b.mV[1],mV[2]+b.mV[2]};}
 float operator*(const LLVector3& b)const{return mV[0]*b.mV[0]+mV[1]*b.mV[1]+mV[2]*b.mV[2];}
 LLVector3 operator*(float f)const{return {mV[0]*f,mV[1]*f,mV[2]*f};}
 LLVector3& operator+=(const LLVector3& b){*this=*this+b;return *this;}
 void normalize(){float n=std::sqrt(*this**this);if(n>0)*this=*this*(1/n);}
};
LLVector3 operator*(float f,const LLVector3& v){return v*f;}
'''
        cases = []
        for at in ((0, 1, 0), normalized((1, 2, -1)), normalized((-3, 1, 2))):
            for normal in ((0, 0, 1), (0, 0, -1), normalized((1, 2, 3))):
                for phase in (-17., -.5, 0., 3., 21.):
                    for coverage in (0., .25, 1.):
                        for strength in (0., .025, .5):
                            cases.append((at, normal, phase, coverage, strength))
        lines = []
        for at, normal, phase, coverage, strength in cases:
            numbers = (*at, *normal, phase, coverage, strength)
            lines.append(' '.join(str(v) for v in numbers))
        main = r'''
int main(){std::cout<<std::setprecision(9);float ax,ay,az,nx,ny,nz,phase,coverage,strength;
 while(std::cin>>ax>>ay>>az>>nx>>ny>>nz>>phase>>coverage>>strength){
 VSAvatarCloth c;c.eye={10,-4,3};c.at={ax,ay,az};c.wind={.6f,0,.8f};
 c.phase=phase;c.frequency=8.25f;c.strength=strength;LLVector3 n{nx,ny,nz};
 float displacement=vs_avatar_cloth(c,{12,7,8},n,coverage);
 std::cout<<displacement<<' '<<n.mV[0]<<' '<<n.mV[1]<<' '<<n.mV[2]<<'\n';
 }}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary)/'avatar_cloth.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(adapter+header+main)
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            ran = subprocess.run([str(exe)], input='\n'.join(lines), capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stderr)
        rows = [tuple(map(float, row.split())) for row in ran.stdout.splitlines()]
        self.assertEqual(len(rows), len(cases))
        for case, actual in zip(cases, rows):
            at, normal, phase, coverage, strength = case
            facing = dot(normal, (.6, 0, .8))
            # Eye-space Z is -dot(world-position minus eye, view-forward).
            eye_z = -dot((2, 11, 5), at)
            wave_input = (facing + eye_z*.015)*8.25-phase
            reduced = wave_input*pi_constants[0]
            bias = (facing*2+1)*strength

            def wave(value):
                x = (value-math.floor(value))*pi_constants[1]-3.141592
                square = x*x
                v = -square*coefficients[3]+coefficients[2]
                v = v*-square+coefficients[1]
                v = v*-square+coefficients[0]
                return v*x*strength+bias

            primary = max((wave(reduced)+min(-.7*normal[2], .2)*1.5)*coverage, -1.)
            secondary = max(wave(reduced-pi_constants[3])*coverage, -1.)
            perturbed = normalized(tuple(normal[i]+2*(normal[i]+(.6, 0, .8)[i]*secondary) for i in range(3)))
            for expected, value in zip((primary, *perturbed), actual):
                self.assertAlmostEqual(expected, value, delta=2e-5, msg=str(case))


if __name__ == '__main__':
    unittest.main()
