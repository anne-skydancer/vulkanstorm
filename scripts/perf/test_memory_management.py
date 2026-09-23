"""Exercise the production memory policy, face cursor and allocation ledger."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest


class MemoryManagementTest(unittest.TestCase):
    def test_policy_and_allocation_lifetimes(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which(os.environ.get('CXX', 'clang++'))
        self.assertIsNotNone(compiler, 'C++17 compiler required')
        code = r'''
#include "llmemorypolicy.h"
#include "lltexturefacescan.h"
#include "lltextureallocation.h"
#include <cassert>
#include <cmath>

bool near(float a, float b) { return std::abs(a-b) < 0.0001f; }
int main() {
    using namespace LLMemoryPolicy;
    // Pressure rises at a bounded rate, waits for recovery headroom, then recovers.
    assert(near(pressureFactor(1.f, 0.f, 1.f), 1.5f));
    assert(near(pressureFactor(1.5f, 0.f, 1.f), 2.f));
    assert(near(pressureFactor(2.f, 350.f, 30.f), 2.f));
    assert(near(pressureFactor(2.f, 1000.f, 1.f), 1.9f));
    assert(near(pressureFactor(2.f, 1000.f, 20.f), 1.f));
    assert(near(pressureFactor(1.f, 0.f, -1.f), 1.f));
    // Automatic mode does not inherit legacy low limits; manual mode still honors them.
    assert(near(sceneFactor(4096, 32768, 65536, 768, 2048, true), 1.f));
    assert(near(sceneFactor(4096, 32768, 65536, 768, 2048, false), 0.f));
    assert(sceneFactor(12000, 32768, 65536, 768, 2048, true) > 0.f);
    assert(near(sceneFactor(20000, 32768, 65536, 768, 2048, true), 0.f));
    assert(near(sceneFactor(4096, 32768, 4096, 768, 2048, true), 0.f));
    assert(std::isfinite(sceneFactor(1, 0, 0, 1000, 1, false)));
    assert(near(sceneFactor(1, 0, 0, 1000, 1, false), 0.f));

    LLTextureFaceScan scan;
    assert(scan.begin(65) == 0 && scan.length() == 32);
    float area=100; bool visible=true;
    scan.finish(32, area, visible);
    assert(scan.begin(65) == 32 && scan.length() == 32);
    area=1; visible=false;
    scan.finish(32, area, visible);
    assert(area == 100 && visible); // offscreen slice must not lose visible history
    assert(scan.begin(65) == 64 && scan.length() == 1);
    area=1; visible=false;
    scan.finish(1, area, visible);
    assert(area == 100 && visible && scan.cursor == 0);
    // The next complete rotation must age out old maxima and visibility.
    for (int i=0; i<3; ++i) {
        scan.begin(65); auto n=scan.length(); area=2; visible=false;
        scan.finish(n, area, visible);
    }
    assert(area == 2 && !visible);
    scan.begin(33); assert(scan.cursor == 0 && scan.maximum == 0);
    scan.reset(); assert(scan.begin(0) == 0 && scan.length() == 0);

    auto rgba = [](auto, auto w, auto h) { return std::uint64_t(w)*h*4; };
    LLTextureAllocation tex;
    tex.set(0,0,{8,2,1,1,64});
    tex.generate(31,rgba);
    assert(tex.levels.size() == 4); // 8x2,4x1,2x1,1x1; not truncated at height=1
    assert(tex.bytes == 64+16+8+4);
    tex.generate(31,rgba); assert(tex.bytes == 92); // no double counting
    tex.set(0,1,{4,1,1,1,16}); assert(tex.bytes == 92);
    // Redefining one mutable mip leaves other mip allocations intact.
    tex.set(0,0,{4,1,1,1,16}); assert(tex.bytes == 44);
    tex = {};
    for (unsigned face=1;face<=6;++face) tex.set(face,0,{2,2,1,1,16});
    tex.generate(31,rgba); assert(tex.bytes == 6*(16+4));
    tex.set(3,0,{2,2,1,1,16}); assert(tex.bytes == 120);
    tex = {}; tex.set(0,0,{4,4,1,12,64*12});
    tex.generate(1,rgba); assert(tex.bytes == (64+16)*12); // arrays and mip cap
    tex.generate(2,rgba); assert(tex.bytes == (64+16+4)*12);
    tex = {}; assert(tex.bytes == 0 && tex.levels.empty());
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp)/'memory_policy.cpp'
            exe = Path(tmp)/('memory_policy.exe' if os.name == 'nt' else 'memory_policy')
            source.write_text(code)
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I'+str(root/'indra/llcommon'), '-I'+str(root/'indra/llrender'),
                            '-I'+str(root/'indra/newview'), str(source), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


    def test_production_texture_ledger_and_formats(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which(os.environ.get('CXX', 'clang++'))
        self.assertIsNotNone(compiler)
        source = (root/'indra/llrender/llimagegl.cpp').read_text()
        begin = source.index('static LLMutex sTexMemMutex;')
        end = source.index('//statics', begin)
        ledger = source[begin:end]
        format_fn = re.search(r'^S64 LLImageGL::dataFormatVRAMBytes\([^\n]*\)\n\{.*?^\}', source, re.M|re.S).group()
        enums = sorted(set(re.findall(r'\bGL_[A-Z0-9_]+', format_fn)))
        defines = '\n'.join(f'#define {name} {i+1}' for i,name in enumerate(enums))
        code = r"""
#include "lltextureallocation.h"
#include <unordered_map>
#include <cassert>
using U32=std::uint32_t; using U64=std::uint64_t; using S32=int; using S64=std::int64_t;
#define llassert assert
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 100
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 105
struct LLMutex { void lock() {} void unlock() {} };
struct LLMutexLock { explicit LLMutexLock(LLMutex*) {} };
struct Unit { U32 name=1; U32 getCurrTexture() {return name;} } unit;
struct GL { U32 getCurrentTexUnitIndex() {return 0;} Unit* getTexUnit(U32) {return &unit;} } gGL;
struct LLImageGL {
 static S64 dataFormatBytes(S32, S32 w, S32 h) {return S64(w)*h*4;}
 static S64 dataFormatVRAMBytes(S32,S32,S32);
 static U64 getTextureBytesAllocated();
};
namespace LLImageGLMemory {
void record_tex_image(U32,U32,U32,U32,U32,U32);
void alloc_tex_image(U32,U32,U32,U32);
void record_generated_mips(U32);
void free_tex_image(U32);
void free_tex_images(U32,const U32*);
void free_cur_tex_image();
}
""" + defines + '\n' + format_fn + '\n' + ledger + r"""
int main() {
 assert(LLImageGL::dataFormatVRAMBytes(GL_RGB8,8,2)==64);
 assert(LLImageGL::dataFormatVRAMBytes(GL_RGB16F,8,2)==128);
 assert(LLImageGL::dataFormatVRAMBytes(GL_RGB32F,8,2)==256);
 assert(LLImageGL::dataFormatVRAMBytes(GL_DEPTH_COMPONENT24,8,2)==64);
 assert(LLImageGL::dataFormatVRAMBytes(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,5,1)==16);
 assert(LLImageGL::dataFormatVRAMBytes(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,1,1)==16);
 record_tex_image(0,0,8,2,GL_RGB8,1);
 record_generated_mips(31);
 assert(LLImageGL::getTextureBytesAllocated()==92);
 record_generated_mips(31); assert(LLImageGL::getTextureBytesAllocated()==92);
 unit.name=2;
 for(U32 face=100;face<=105;++face) record_tex_image(face,0,2,2,GL_RGB8,1);
 record_generated_mips(31);
 assert(LLImageGL::getTextureBytesAllocated()==212);
 // Freeing one name leaves every other name intact. Duplicate frees are harmless.
 free_tex_image(1); free_tex_image(1);
 assert(LLImageGL::getTextureBytesAllocated()==120);
 U32 names[]={2}; free_tex_images(1,names);
 assert(LLImageGL::getTextureBytesAllocated()==0);
 unit.name=3; alloc_tex_image(4,4,GL_RGB8,12); record_generated_mips(2);
 assert(LLImageGL::getTextureBytesAllocated()==1008);
 free_cur_tex_image(); assert(LLImageGL::getTextureBytesAllocated()==0);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'ledger.cpp'; cpp.write_text(code)
            exe=Path(tmp)/('ledger.exe' if os.name=='nt' else 'ledger')
            subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',
                            '-I'+str(root/'indra/llrender'),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
