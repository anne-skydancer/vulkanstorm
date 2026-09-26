"""Compile the actual render-target header/implementation against a fake GL driver.

Only project includes are replaced; production method bodies are not copied or
reimplemented. Tests exercise allocation, reuse and rollback without a GPU.
Run with Python and CMake (Visual Studio on Windows, a C++ compiler elsewhere).
"""
import pathlib
import re
import subprocess
import tempfile
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
if len(sys.argv) > 1:
    ROOT = pathlib.Path(sys.argv[1])

def without_includes(path):
    return re.sub(r'^#include[^\n]*\n', '', path.read_text(encoding='utf-8'), flags=re.M)

fixture = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>
#include <vector>
#ifdef _WIN32
#include <crtdbg.h>
#endif
using U32 = uint32_t; using S32 = int32_t;
using GLenum = U32; using GLuint = U32; using LLGLuint = U32; using GLsizei = int;
constexpr U32 GL_FRAMEBUFFER=1, GL_DRAW_FRAMEBUFFER=2, GL_FRAMEBUFFER_COMPLETE=3;
constexpr U32 GL_COLOR_ATTACHMENT0=10, GL_COLOR_ATTACHMENT1=11, GL_COLOR_ATTACHMENT2=12, GL_COLOR_ATTACHMENT3=13;
constexpr U32 GL_DEPTH_ATTACHMENT=20, GL_RGBA=21, GL_UNSIGNED_BYTE=22, GL_DEPTH_COMPONENT24=23;
constexpr U32 GL_DEPTH_COMPONENT=24, GL_UNSIGNED_INT=25, GL_NONE=0, GL_NO_ERROR=0;
constexpr U32 GL_COLOR_BUFFER_BIT=1, GL_DEPTH_BUFFER_BIT=2, GL_SCISSOR_TEST=26, GL_TEXTURE_2D=27, GL_BACK=28;
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE
#define LL_PROFILE_GPU_ZONE(x)
#define llassert(x) assert(x)
#define LL_WARNS() std::cerr
#define LL_ERRS() std::cerr
#define LL_ENDL std::endl
template<class T> T llmin(T a,T b) { return std::min(a,b); }
template<class T> T llmax(T a,T b) { return std::max(a,b); }
bool gDebugGL=false;
S32 gGLViewport[4]={0,0,800,600};
struct { int mGLMaxTextureSize=1024; } gGLManager;
U32 next_name=1, bound_fbo=0, error=0, fail_format=0;
bool fail_status=false;
std::set<U32> textures, framebuffers;
int image_allocations=0;
void ll_fail(const char*) { assert(false); }
void stop_glerror() {}
void clear_glerror() { error=0; }
U32 glGetError() { U32 e=error; error=0; return e; }
void glGenFramebuffers(int n,U32* out) { while(n--) { *out=next_name++; framebuffers.insert(*out++); } }
void glDeleteFramebuffers(int n,const U32* in) { while(n--) assert(framebuffers.erase(*in++)==1); }
void glBindFramebuffer(U32,U32 name) { bound_fbo=name; }
U32 glCheckFramebufferStatus(U32) { return fail_status ? 99 : GL_FRAMEBUFFER_COMPLETE; }
void glFramebufferTexture2D(U32,U32,U32,U32,int) {}
void glDrawBuffer(U32) {} void glReadBuffer(U32) {}
void glDrawBuffers(int,const U32*) {}
void glViewport(int,int,U32,U32) {} void glScissor(int,int,U32,U32) {}
void glClear(U32) {} void glGenerateMipmap(U32) {}
struct LLGLEnable { explicit LLGLEnable(U32) {} };
struct LLTexUnit {
    enum eTextureType { TT_TEXTURE, TT_RECT_TEXTURE };
    enum eTextureMipGeneration { TMG_NONE, TMG_AUTO };
    enum eTextureFilterOptions { TFO_BILINEAR, TFO_POINT, TFO_TRILINEAR, TFO_ANISOTROPIC };
    enum { TAM_MIRROR, TAM_CLAMP };
    static U32 getInternalType(eTextureType) { return GL_TEXTURE_2D; }
    void bindManual(eTextureType,U32,bool=false) {}
    void setTextureFilteringOption(eTextureFilterOptions) {}
    void setTextureAddressMode(int) {}
};
struct { LLTexUnit unit; LLTexUnit* getTexUnit(int) { return &unit; } void flush() {} } gGL;
struct LLImageGL {
    static void generateTextures(int n,U32* out) { while(n--) { *out=next_name++; textures.insert(*out++); } }
    static void deleteTextures(int n,const U32* in) { while(n--) assert(textures.erase(*in++)==1); }
    static void setManualImage(U32,int,U32 format,U32,U32,U32,U32,const void*,bool) {
        ++image_allocations; if (format==fail_format) error=98;
    }
    U32 getWidth() const { return 64; } U32 getHeight() const { return 64; }
    LLTexUnit::eTextureType getTarget() const { return LLTexUnit::TT_TEXTURE; }
    U32 getTexName() const { return 9000; }
};
'''

tests = r'''
void empty() {
    assert(textures.empty()); assert(framebuffers.empty());
    assert(LLRenderTarget::sBytesAllocated==0);
    assert(bound_fbo==LLRenderTarget::sCurFBO);
}
int main() {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    LLRenderTarget::sUseFBO=true;
    {
        LLRenderTarget t;
        assert(t.allocate(64,64,GL_RGBA,true));
        U32 texture=t.getTexture(); int calls=image_allocations;
        assert(t.allocate(64,64,GL_RGBA,true));
        assert(t.getTexture()==texture && image_allocations==calls);
        assert(t.allocate(64,64,77,true)); // same size, different color format
        assert(t.getTexture()!=texture && image_allocations>calls);
    }
    empty();
    for (U32 format : {GL_RGBA,GL_DEPTH_COMPONENT24}) {
        LLRenderTarget t;
        fail_format=format;
        assert(!t.allocate(64,64,GL_RGBA,true));
        assert(!t.isComplete()); empty();
        fail_format=0;
        assert(t.allocate(64,64,GL_RGBA,true)); // identical retry must really allocate
        assert(t.isComplete());
    }
    empty();
    for (bool debug : {false,true}) {
        gDebugGL=debug;
        for (U32 format : {GL_RGBA,0u}) {
            LLRenderTarget t;
            fail_status=true;
            assert(!t.allocate(64,64,format,true));
            assert(!t.isComplete()); empty();
            fail_status=false;
            assert(t.allocate(64,64,format,true));
        }
    }
    gDebugGL=false;
    empty();
    {
        LLRenderTarget t;
        assert(!t.allocate(0,64,GL_RGBA)); empty();
        assert(!t.allocate(64,64,0,false)); empty();
        assert(t.allocate(4096,4096,GL_RGBA));
        assert(t.getWidth()==1024 && t.getHeight()==1024);
        int calls=image_allocations;
        assert(t.allocate(4096,4096,GL_RGBA));
        assert(image_allocations==calls); // effective dimensions define reuse
        assert(t.addColorAttachment(77));
        calls=image_allocations;
        assert(t.allocate(4096,4096,GL_RGBA));
        assert(t.getNumTextures()==2 && image_allocations==calls); // preserve MRT reuse
        t.release(); t.release(); empty();
        assert(t.allocate(64,64,0,true)); // depth-only framebuffer
        assert(t.getDepth() && t.getNumTextures()==0);
    }
    empty();
    {
        LLRenderTarget t;
        LLImageGL borrowed;
        t.setColorAttachment(&borrowed);
        t.releaseColorAttachment(); // never delete externally owned texture
        assert(t.allocate(64,64,GL_RGBA));
    }
    empty();
    {
        LLRenderTarget owner, target;
        assert(owner.allocate(64,64,0,true));
        assert(target.allocate(64,64,GL_RGBA));
        owner.shareDepthBuffer(target);
        assert(target.getDepth()==0); // borrowed, not owned
        assert(target.allocate(64,64,GL_RGBA,true));
        assert(target.getDepth()!=0 && target.getDepth()!=owner.getDepth());
    }
    empty();
    std::cout << "Render-target reuse, format changes, failure retry, completeness, MRT and ownership checks passed\n";
}
'''

with tempfile.TemporaryDirectory(prefix='glrt-test-') as d:
    d=pathlib.Path(d)
    (d/'test.cpp').write_text(fixture + without_includes(ROOT/'indra/llrender/llrendertarget.h')
        + without_includes(ROOT/'indra/llrender/llrendertarget.cpp') + tests,encoding='utf-8')
    (d/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(glrt_test LANGUAGES CXX)\nadd_executable(glrt_test test.cpp)\ntarget_compile_features(glrt_test PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake','-S',str(d),'-B',str(d/'build')],check=True)
    subprocess.run(['cmake','--build',str(d/'build'),'--config','Debug'],check=True)
    binary=d/'build'/('Debug/glrt_test.exe' if sys.platform=='win32' else 'glrt_test')
    subprocess.run([str(binary)],check=True)
