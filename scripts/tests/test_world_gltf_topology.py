"""Execute native topology conversion after the shared decoder prepares data."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WorldGLTFTopologyTest(unittest.TestCase):
    def test_prepared_triangles_are_not_converted_as_the_original_strip_again(self):
        source = (ROOT/'indra/newview/vsworldgltf.cpp').read_text()
        begin = source.index('std::vector<unsigned>     indices;', source.index('void VSWorldGLTF::append('))
        end = source.index('const unsigned group', begin)
        conversion = source[begin:end]
        fixture = r'''
#include <cassert>
#include <vector>
namespace LLRender {enum Mode {POINTS,LINES,LINE_LOOP,LINE_STRIP,TRIANGLES,TRIANGLE_STRIP,TRIANGLE_FAN};}
struct VSWorldRenderer {enum class Topology {Triangles,Lines,Points};};
struct Primitive {LLRender::Mode mGLMode,mMode;std::vector<unsigned> mIndexArray;};
void test(Primitive primitive,std::vector<unsigned> expected,VSWorldRenderer::Topology kind){
 struct Owner{const char* error=nullptr;}owner;
 CONVERSION
 assert(!owner.error && indices==expected && topology==kind);
}
int main(){using T=VSWorldRenderer::Topology;
 // Normals/tangents expanded strip/fan data to indexed triangles. The original
 // file mode stays unchanged; the reference draws the prepared triangle list.
 test({LLRender::TRIANGLES,LLRender::TRIANGLE_STRIP,{0,1,2,2,1,3}},{0,1,2,2,1,3},T::Triangles);
 test({LLRender::TRIANGLES,LLRender::TRIANGLE_FAN,{0,1,2,0,2,3}},{0,1,2,0,2,3},T::Triangles);
 // Supplied normals/tangents can retain the original topology.
 test({LLRender::TRIANGLE_STRIP,LLRender::TRIANGLE_STRIP,{0,1,2,3}},{0,1,2,2,1,3},T::Triangles);
 test({LLRender::TRIANGLE_FAN,LLRender::TRIANGLE_FAN,{0,1,2,3}},{0,1,2,0,2,3},T::Triangles);
 test({LLRender::POINTS,LLRender::POINTS,{0,1,2}},{0,1,2},T::Points);
 test({LLRender::LINES,LLRender::LINES,{0,1,2,3}},{0,1,2,3},T::Lines);
 test({LLRender::LINE_STRIP,LLRender::LINE_STRIP,{0,1,2}},{0,1,1,2},T::Lines);
 test({LLRender::LINE_LOOP,LLRender::LINE_LOOP,{0,1,2}},{0,1,1,2,2,0},T::Lines);
}
'''.replace('CONVERSION', conversion)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory)/'topology.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran = subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)


if __name__ == '__main__':
    unittest.main()
