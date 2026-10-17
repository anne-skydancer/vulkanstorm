"""Execute the production primitive/glTF tangent-transform contracts."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]


class NativeMaterialBasisTests(unittest.TestCase):
    def test_reference_rotation_mirrors_animation_and_degenerate_scale(self):
        source=r'''
#include "vsnormalbasis.h"
#include <cassert>
void check(const std::array<float,4>& value,float x,float y,float sign) {
 assert(std::abs(value[0]-x)<1.e-6f && std::abs(value[1]-y)<1.e-6f && value[2]==sign);
}
int main() {
 constexpr float quarter=1.57079632679489661923f;
 check(VSNormalBasis::primitive(2,3,0),1,0,1);
 check(VSNormalBasis::primitive(-2,3,0),-1,0,-1);
 check(VSNormalBasis::primitive(2,-3,quarter),0,1,-1);
 check(VSNormalBasis::imported(quarter),0,-1,1);
 check(VSNormalBasis::animate(VSNormalBasis::primitive(2,3,0),{0,-1,1,0}),0,1,1);
 check(VSNormalBasis::animate(VSNormalBasis::primitive(2,3,quarter/2),{2,0,0,4}),
       1/std::sqrt(5.f),-2/std::sqrt(5.f),1);
 check(VSNormalBasis::primitive(0,0,0),1,0,0);
}
'''
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary);cpp=path/'normal_basis.cpp';cpp.write_text(source)
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(path/'normal_basis.exe')],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(path/'normal_basis.exe')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__=='__main__':unittest.main()
