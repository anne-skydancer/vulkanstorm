"""Check vertex-color ownership at the shared GL/native glTF boundary."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class GLTFMaterials(unittest.TestCase):
    def test_native_material_factor_is_applied_once(self):
        source=(ROOT/'indra/newview/gltf/primitive.cpp').read_text()
        block=method(source,'if (asset.mLoadIntoVRAM)')
        fixture=r'''
#include <cassert>
#include <vector>
struct Color {float value;Color(float v):value(v){}Color operator*(Color v){return Color(value*v.value);}};
using LLColor4=Color;using LLColor4U=Color;
float render(bool gl){struct Asset{bool mLoadIntoVRAM;}asset{gl};
 std::vector<Color> mColors{Color(.8f)};Color baseColor(.25f);
PRODUCTION_FACTOR_BLOCK
 // GL draws its baked stream; native draws the original stream with constants.
 return mColors[0].value*(gl?1.f:baseColor.value);
}
int main(){assert(render(false)==.2f);assert(render(true)==.2f);}
'''.replace('PRODUCTION_FACTOR_BLOCK',block)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as d:
            cpp=Path(d)/'factor.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
