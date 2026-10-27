"""Compare both production primitive sinks with frozen original GL geometry.

No window or hardware is used. GPU rasterization remains covered by the staged
connected replay; these checks isolate geometry, inherited colour and topology.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from native_source_fixture import ROOT, HERE, method


class UIPrimitives(unittest.TestCase):
    def compile(self, fixture):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp)/'primitives.cpp'
            exe = Path(tmp)/'primitives.exe'
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Werror',
                '-I'+str(ROOT/'indra/llrender'), '-I'+str(HERE), str(cpp), '-o', str(exe)],
                capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)

    def test_inherited_colour_never_accesses_gl_storage(self):
        source = (ROOT/'indra/llrender/llrender.cpp').read_text(encoding='utf-8')
        names = ['color4ub', 'color4ubv', 'color4f', 'color4fv', 'color3f', 'color3fv',
                 'diffuseColor3f', 'diffuseColor3fv', 'diffuseColor4f', 'diffuseColor4fv',
                 'diffuseColor4ubv', 'diffuseColor4ub']
        methods = '\n'.join(method(source, 'void LLRender::'+name+'(') for name in names)
        declarations = '\n'.join(m.split('{', 1)[0].replace('LLRender::', '')+';' for m in
            (method(source, 'void LLRender::'+name+'(') for name in names))
        self.compile(r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
using F32=float;using GLfloat=float;using GLubyte=unsigned char;using U8=unsigned char;
template<class T>T llclamp(T v,T a,T b){return std::clamp(v,a,b);}
#define llassert(v) assert(v)
struct LLColor4{float mV[4];LLColor4(float r,float g,float b,float a):mV{r,g,b,a}{}};
struct LLColor4U{U8 mV[4]{};LLColor4U()=default;LLColor4U(U8 r,U8 g,U8 b,U8 a):mV{r,g,b,a}{}};
struct LLRender2D{inline static bool active=true;inline static LLColor4 color{1,1,1,1};
 static bool isNativeUI(){return active;}static void setNativeColor(const LLColor4& c){color=c;}};
struct LLShaderMgr{enum{DIFFUSE_COLOR};};struct LLVertexBuffer{enum{MAP_COLOR=1};};
struct LLGLSLShader{inline static LLGLSLShader* sCurBoundShaderPtr=nullptr;int mAttributeMask=1,uploads=0;
 std::array<float,4> uniform{};void uniform4f(int,float r,float g,float b,float a){++uploads;uniform={r,g,b,a};}
 void uniform4fv(int,int,const float* c){uniform4f(0,c[0],c[1],c[2],c[3]);}};
struct LLRender{LLColor4U* mColorsp=nullptr;int mCount=0;DECLARATIONS};
METHODS
int main(){
 LLRender draw; // Deliberately no GL vertex allocation or bound shader.
 for(int i=0;i<256;++i){draw.color4ub(i,255-i,37,129);
  assert(LLRender2D::color.mV[0]==i/255.f&&LLRender2D::color.mV[3]==129/255.f);}
 draw.color4f(-1.f,.5f,2.f,.25f);assert(LLRender2D::color.mV[0]==0&&LLRender2D::color.mV[1]==127/255.f&&LLRender2D::color.mV[2]==1);
 float f[4]={.1f,.2f,.3f,.4f};U8 b[4]={12,34,56,78};
 draw.color4fv(f);draw.color3f(f[0],f[1],f[2]);draw.color3fv(f);assert(LLRender2D::color.mV[3]==1);
 draw.color4ubv(b);assert(LLRender2D::color.mV[2]==56/255.f);
 draw.diffuseColor3f(.1f,.2f,.3f);assert(LLRender2D::color.mV[3]==1);
 draw.diffuseColor3fv(f);assert(LLRender2D::color.mV[2]==f[2]);
 draw.diffuseColor4f(1.5f,.2f,.3f,.4f);assert(LLRender2D::color.mV[0]==1.5f);
 draw.diffuseColor4fv(f);assert(LLRender2D::color.mV[3]==f[3]);
 draw.diffuseColor4ubv(b);assert(LLRender2D::color.mV[3]==78/255.f);
 draw.diffuseColor4ub(11,22,33,44);assert(LLRender2D::color.mV[3]==44/255.f);
 // The original GL vertex/uniform paths retain their existing semantics.
 LLRender2D::active=false;LLColor4U storage;draw.mColorsp=&storage;
 draw.color4ubv(b);assert(storage.mV[0]==12&&storage.mV[3]==78);
 LLGLSLShader shader;LLGLSLShader::sCurBoundShaderPtr=&shader;
 draw.color4fv(f);assert(storage.mV[1]==51&&shader.uploads==0);
 shader.mAttributeMask=0;draw.color4ubv(b);assert(shader.uploads==1&&shader.uniform[3]==78/255.f);
 draw.diffuseColor4fv(f);assert(shader.uploads==2&&shader.uniform[3]==f[3]);
}
'''.replace('DECLARATIONS', declarations).replace('METHODS', methods))

    def test_original_vertex_sequences_and_native_submission(self):
        source = (ROOT/'indra/llrender/llrender2dutils.cpp').read_text(encoding='utf-8')
        names = ['gl_arc_2d', 'gl_circle_2d', 'gl_corners_2d', 'gl_washer_2d',
                 'gl_washer_segment_2d', 'gl_rect_2d_simple']
        # NativePrimitive now lives beside the shared templates in vsuiprimitive.h;
        # its line expansion is the production LLRender2D::nativeLine definition.
        header = (ROOT/'indra/llrender/vsuiprimitive.h').read_text(encoding='utf-8')
        header = re.sub(r'^#include "[^"]+"[ \t]*$', '', header, flags=re.M)
        adapter = header + '\n' + method(source, 'void LLRender2D::nativeLine(')
        wrappers = '\n'.join(method(source, 'void '+name+'(') for name in names)
        fixture = (HERE/'ui_primitives_fixture.cpp').read_text(encoding='utf-8')
        fixture = fixture.replace('PRODUCTION_ADAPTER', adapter).replace('PRODUCTION_WRAPPERS', wrappers)
        self.compile(fixture)


if __name__ == '__main__':
    unittest.main()
