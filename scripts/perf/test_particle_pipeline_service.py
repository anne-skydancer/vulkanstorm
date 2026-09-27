"""Real-GL validation of particle simulation and ordering foundations.
No viewer integration or performance claim; readback occurs only for assertions.
Windows: python scripts/perf/test_particle_pipeline.py [--opengl DLL]
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import math
from pathlib import Path
import random
import struct

parser = argparse.ArgumentParser()
parser.add_argument('--opengl', default='opengl32.dll')
args = parser.parse_args()
user = C.WinDLL('user32', use_last_error=True)
gdi = C.WinDLL('gdi32', use_last_error=True)
gl = C.WinDLL(args.opengl, use_last_error=True)
user.CreateWindowExW.argtypes = [W.DWORD,W.LPCWSTR,W.LPCWSTR,W.DWORD,C.c_int,C.c_int,C.c_int,C.c_int,W.HWND,W.HMENU,W.HINSTANCE,C.c_void_p]
user.CreateWindowExW.restype = W.HWND
user.GetDC.argtypes = [W.HWND]; user.GetDC.restype = W.HDC
user.ReleaseDC.argtypes = [W.HWND,W.HDC]
user.DestroyWindow.argtypes = [W.HWND]
class PFD(C.Structure):
    _fields_ = [('size',W.WORD),('version',W.WORD),('flags',W.DWORD),('pixel',C.c_ubyte),('color',C.c_ubyte),('rest',C.c_ubyte*18),('layerMask',W.DWORD),('visibleMask',W.DWORD),('damageMask',W.DWORD)]
pfd=PFD();pfd.size=C.sizeof(PFD);pfd.version=1;pfd.flags=0x24;pfd.color=24
assert C.sizeof(PFD)==40
window=user.CreateWindowExW(0,'STATIC','Particle compute validation',0,0,0,32,32,None,None,None,None)
assert window
hdc=user.GetDC(window)
gdi.ChoosePixelFormat.argtypes=[W.HDC,C.POINTER(PFD)]
gdi.SetPixelFormat.argtypes=[W.HDC,C.c_int,C.POINTER(PFD)]
assert gdi.SetPixelFormat(hdc,gdi.ChoosePixelFormat(hdc,C.byref(pfd)),C.byref(pfd))
gl.wglCreateContext.argtypes=[W.HDC];gl.wglCreateContext.restype=C.c_void_p
gl.wglMakeCurrent.argtypes=[W.HDC,C.c_void_p]
gl.wglDeleteContext.argtypes=[C.c_void_p]
gl.wglGetProcAddress.argtypes=[C.c_char_p];gl.wglGetProcAddress.restype=C.c_void_p
context=gl.wglCreateContext(hdc);assert context and gl.wglMakeCurrent(hdc,context)
def fn(name,result,*types):
    address=gl.wglGetProcAddress(name.encode())
    if address not in (None,0,1,2,3,C.c_void_p(-1).value): return C.WINFUNCTYPE(result,*types)(address)
    function=getattr(gl,name);function.restype=result;function.argtypes=list(types);return function
create_core=fn('wglCreateContextAttribsARB',C.c_void_p,W.HDC,C.c_void_p,C.POINTER(C.c_int))
attributes=(C.c_int*7)(0x2091,4,0x2092,5,0x9126,1,0)
core=create_core(hdc,None,attributes)
assert core, 'OpenGL 4.5 Core context unavailable'
assert gl.wglMakeCurrent(hdc,core)
gl.wglDeleteContext(context)
context=core
U=C.c_uint;I=C.c_int;P=C.c_void_p;S=C.c_ssize_t
get_string=fn('glGetString',C.c_char_p,U)
print('GL:',get_string(0x1F02).decode(),get_string(0x1F01).decode(),flush=True)
create_shader=fn('glCreateShader',U,U)
source_shader=fn('glShaderSource',None,U,I,C.POINTER(C.c_char_p),P)
compile_shader=fn('glCompileShader',None,U)
shader_iv=fn('glGetShaderiv',None,U,U,C.POINTER(I))
shader_log=fn('glGetShaderInfoLog',None,U,I,P,P)
create_program=fn('glCreateProgram',U)
attach=fn('glAttachShader',None,U,U);link=fn('glLinkProgram',None,U)
program_iv=fn('glGetProgramiv',None,U,U,C.POINTER(I))
program_log=fn('glGetProgramInfoLog',None,U,I,P,P)
use=fn('glUseProgram',None,U)
def program(stages):
    prog=create_program()
    for kind,text in stages:
        shader=create_shader(kind);source=C.c_char_p(text.encode());source_shader(shader,1,C.byref(source),None);compile_shader(shader)
        status=I();shader_iv(shader,0x8B81,C.byref(status))
        log=C.create_string_buffer(8192);shader_log(shader,len(log),None,log)
        assert status.value,log.value.decode()
        attach(prog,shader);fn('glDeleteShader',None,U)(shader)
    link(prog);status=I();program_iv(prog,0x8B82,C.byref(status))
    log=C.create_string_buffer(8192);program_log(prog,len(log),None,log)
    assert status.value,log.value.decode()
    return prog



# Exercise the actual native dispatcher, with viewer logging/flush/directory
# wrappers replaced but every GL call sent to the real current context.
import subprocess
import tempfile
import contextlib

ROOT = Path(__file__).resolve().parents[2]
API = [
 ('glGetIntegerv','void','GLenum, GLint*'),
 ('glGetIntegeri_v','void','GLenum, GLuint, GLint*'),
 ('glGetInteger64i_v','void','GLenum, GLuint, GLint64*'),
 ('glUseProgram','void','GLuint'),
 ('glBindBufferRange','void','GLenum, GLuint, GLuint, GLintptr, GLsizeiptr'),
 ('glBindBufferBase','void','GLenum, GLuint, GLuint'),
 ('glBindBuffer','void','GLenum, GLuint'),
 ('glGetError','GLenum',''),
 ('glCreateShader','GLuint','GLenum'),
 ('glShaderSource','void','GLuint, GLsizei, const char* const*, const GLint*'),
 ('glCompileShader','void','GLuint'),
 ('glGetShaderiv','void','GLuint, GLenum, GLint*'),
 ('glGetShaderInfoLog','void','GLuint, GLsizei, GLsizei*, char*'),
 ('glDeleteShader','void','GLuint'),
 ('glCreateProgram','GLuint',''),
 ('glAttachShader','void','GLuint, GLuint'),
 ('glLinkProgram','void','GLuint'),
 ('glGetProgramiv','void','GLuint, GLenum, GLint*'),
 ('glGetProgramInfoLog','void','GLuint, GLsizei, GLsizei*, char*'),
 ('glDeleteProgram','void','GLuint'),
 ('glGetUniformLocation','GLint','GLuint, const char*'),
 ('glBufferData','void','GLenum, GLsizeiptr, const void*, GLenum'),
 ('glDispatchCompute','void','GLuint, GLuint, GLuint'),
 ('glMemoryBarrier','void','GLbitfield'),
 ('glUniform1ui','void','GLint, GLuint'),
 ('glUniform1f','void','GLint, GLfloat'),
 ('glUniform3fv','void','GLint, GLsizei, const GLfloat*'),
 ('glUniformMatrix4fv','void','GLint, GLsizei, GLboolean, const GLfloat*'),
 ('glGetBufferSubData','void','GLenum, GLintptr, GLsizeiptr, void*'),
 ('glDeleteBuffers','void','GLsizei, const GLuint*'),
 ('glGenBuffers','void','GLsizei, GLuint*'),
 ('glGenVertexArrays','void','GLsizei, GLuint*'),
 ('glDeleteVertexArrays','void','GLsizei, const GLuint*'),
 ('glBindVertexArray','void','GLuint'),
 ('glUniform1i','void','GLint, GLint'),
 ('glMultiDrawArraysIndirectCountARB','void','GLenum, const void*, GLintptr, GLsizei, GLsizei'),
]
FIXTURE = r'''
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <cstdint>
#include <string>
#include <iostream>
using U32=std::uint32_t;
using GLint64=std::int64_t;
using GLintptr=std::intptr_t;
using GLsizeiptr=std::intptr_t;
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#define GL_SHADER_STORAGE_BUFFER_START 0x90D4
#define GL_SHADER_STORAGE_BUFFER_SIZE 0x90D5
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x200
#define GL_SHADER_STORAGE_BARRIER_BIT 0x2000
#define GL_COMMAND_BARRIER_BIT 0x0040
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_DRAW_INDIRECT_BUFFER_BINDING 0x8F43
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#define GL_PARAMETER_BUFFER_ARB 0x80EE
#define GL_PARAMETER_BUFFER_BINDING_ARB 0x80EF
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x0020
#define GL_FRAMEBUFFER_BARRIER_BIT 0x0400
#define GL_COMPUTE_SHADER 0x91B9
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_DYNAMIC_DRAW 0x88E8
#define LL_WARNS(tag) std::cerr
#define LL_INFOS(tag) std::cout
#define LL_ENDL std::endl
struct {
    float mGLVersion=4.5f;
    int mGLSLVersionMajor=4, mGLSLVersionMinor=50;
    bool mHasFragmentShaderInterlock=true, mHasBindlessTexture=true;
    bool mHasShaderDrawParameters=true, mHasIndirectParameters=true;
} gGLManager;
struct { void flush() {} } gGL;
constexpr int LL_PATH_APP_SETTINGS=0;
struct Directory {
    std::string root;
    std::string getExpandedFilename(int,const std::string& a,const std::string& b)
    { return root+"/"+a+"/"+b; }
} directory;
Directory* gDirUtilp=&directory;
'''
EXPORTS = r'''
#define API_EXPORT extern "C" __declspec(dllexport)
API_EXPORT void pipeline_capabilities(unsigned mask,float version,int glslMinor) {
    gGLManager.mGLVersion=version;
    gGLManager.mGLSLVersionMinor=glslMinor;
    gGLManager.mHasFragmentShaderInterlock=(mask&1)!=0;
    gGLManager.mHasBindlessTexture=(mask&2)!=0;
    gGLManager.mHasShaderDrawParameters=(mask&4)!=0;
    gGLManager.mHasIndirectParameters=(mask&8)!=0;
}
API_EXPORT int pipeline_init(const char* root) {
    directory.root=root;
    return LLParticlePipeline::initGL();
}
API_EXPORT int pipeline_pool(const void* bytes,unsigned count) {
    const auto* p=static_cast<const LLParticlePipeline::Particle*>(bytes);
    return LLParticlePipeline::initializePool({p,p+count});
}
API_EXPORT int pipeline_sources(const void* bytes,unsigned count) {
    const auto* p=static_cast<const LLParticlePipeline::Source*>(bytes);
    return LLParticlePipeline::publishSources({p,p+count},{});
}
API_EXPORT int pipeline_emit(const void* bytes,unsigned count,unsigned limit) {
    const auto* p=static_cast<const LLParticlePipeline::Particle*>(bytes);
    return LLParticlePipeline::emit({p,p+count},limit);
}
API_EXPORT int pipeline_advance(float dt) {
    return LLParticlePipeline::advance(dt,{0,0,0});
}
API_EXPORT int pipeline_view(float cellSize) {
    return LLParticlePipeline::prepareView({0,0,0},{1,0,0},cellSize);
}
API_EXPORT void pipeline_buffers(void* result) {
    *static_cast<LLParticlePipeline::ViewBuffers*>(result)=LLParticlePipeline::viewBuffers();
}
API_EXPORT int pipeline_intervals(const float* depths,unsigned count,unsigned domain) {
    return LLParticlePipeline::prepareAlphaIntervals({depths,depths+count},domain);
}
API_EXPORT void pipeline_alpha_buffers(void* result) {
    *static_cast<LLParticlePipeline::AlphaBuffers*>(result)=LLParticlePipeline::alphaBuffers();
}
API_EXPORT int pipeline_draw(unsigned interval,unsigned materials,int uniformLocation) {
    return LLParticlePipeline::drawAlphaInterval(interval,materials,uniformLocation);
}
API_EXPORT int pipeline_draw_glow(unsigned interval,unsigned materials,int uniformLocation) {
    return LLParticlePipeline::drawAlphaInterval(interval,materials,uniformLocation,true);
}
API_EXPORT int pipeline_project(const float* matrix,unsigned domain) {
    return LLParticlePipeline::prepareView({0,0,0},{1,0,0},1.f,matrix,domain);
}
API_EXPORT unsigned pipeline_demand(float ratio) { return LLParticlePipeline::textureDemand(ratio,{0,0,0}); }
API_EXPORT unsigned pipeline_bounds() { return LLParticlePipeline::boundsBuffer(); }
API_EXPORT int pipeline_pick(const float* start,const float* end,unsigned* source,float* fraction) {
    std::array<unsigned,2> result;
    bool hit=LLParticlePipeline::pick({start[0],start[1],start[2]},{end[0],end[1],end[2]},result,*fraction);
    if(hit) {source[0]=result[0];source[1]=result[1];} return hit;
}
API_EXPORT int pipeline_retire_region(const float* lower,const float* upper) {
    return LLParticlePipeline::retireRegion({lower[0],lower[1],lower[2]},{upper[0],upper[1],upper[2]});
}
API_EXPORT int pipeline_region_wind(const void* bytes) {
    const auto* source=static_cast<const LLParticlePipeline::Source*>(bytes);
    std::vector<LLParticlePipeline::WindVelocity> wind(512,{1.f,0.f});
    for(unsigned i=256;i<512;++i) wind[i]={7.f,0.f};
    LLParticlePipeline::Region a{{0,0,256,256},{0,1,0x43800000u,0}};
    LLParticlePipeline::Region b{{256,0,256,256},{256,1,0x43800000u,0}};
    return LLParticlePipeline::publishSources({*source},wind) && LLParticlePipeline::publishRegions({a,b});
}
API_EXPORT void pipeline_reload() { LLParticlePipeline::unloadShaders(); }
API_EXPORT void pipeline_destroy() { LLParticlePipeline::destroyGL(); }
'''
def bind_export(dll,name,result,*types):
    f=getattr(dll,name); f.restype=result; f.argtypes=types; return f

owned=(U*4)()
gen=fn('glGenBuffers',None,I,C.POINTER(U))
bind=fn('glBindBuffer',None,U,U)
data=fn('glBufferData',None,U,S,P,U)
base=fn('glBindBufferBase',None,U,U,U)
bind_range=fn('glBindBufferRange',None,U,U,U,S,S)
read=fn('glGetBufferSubData',None,U,S,S,P)
barrier=fn('glMemoryBarrier',None,U)
SSBO=0x90D2
guard_program=program([(0x91B9,'#version 430 core\nlayout(local_size_x=1) in; void main() {}')])
get_integer=fn('glGetIntegerv',None,U,C.POINTER(I))
get_indexed=fn('glGetIntegeri_v',None,U,U,C.POINTER(I))
get_indexed64=fn('glGetInteger64i_v',None,U,U,C.POINTER(C.c_int64))

def protect_state():
    gen(4,owned)
    for i,b in enumerate(owned):
        bind(SSBO,b); data(SSBO,8192,None,0x88E8)
        bind_range(SSBO,i,b,256,512)
    bind(SSBO,owned[0])
    use(guard_program)

def snapshot():
    current=I(); generic=I()
    get_integer(0x8B8D,C.byref(current)); get_integer(0x90D3,C.byref(generic))
    indices=[]
    for i in range(4):
        b=I(); start=C.c_int64(); size=C.c_int64()
        get_indexed(0x90D3,i,C.byref(b))
        get_indexed64(0x90D4,i,C.byref(start)); get_indexed64(0x90D5,i,C.byref(size))
        indices.append((b.value,start.value,size.value))
    return current.value,generic.value,indices

def download(buffer,size):
    previous=I(); get_integer(0x90D3,C.byref(previous))
    barrier(0x200)
    bind(SSBO,buffer); value=C.create_string_buffer(size)
    read(SSBO,0,size,value); bind(SSBO,previous.value)
    return value.raw

try:
    with tempfile.TemporaryDirectory(prefix='vulkanstorm-particle-native-') as temporary, contextlib.ExitStack() as cleanup:
        temp=Path(temporary)
        source=(ROOT/'indra/newview/llparticlepipeline.cpp').read_text()
        for header in ('llviewerprecompiledheaders.h','llgl.h','llrender.h','lldir.h'):
            source=source.replace('#include "'+header+'"','')
        declarations=''
        setup='extern "C" __declspec(dllexport) void pipeline_functions(void** functions) {\n'
        addresses=[]
        for i,(name,result,arguments) in enumerate(API):
            declarations += f'using Fn{i}={result}(APIENTRY*)({arguments});\nFn{i} native_{name};\n#define {name} native_{name}\n'
            setup += f'native_{name}=reinterpret_cast<Fn{i}>(functions[{i}]);\n'
            address=gl.wglGetProcAddress(name.encode())
            if address in (None,0,1,2,3,C.c_void_p(-1).value):
                address=C.cast(getattr(gl,name),P).value
            addresses.append(address)
        fixture=temp/'fixture.cpp'
        fixture.write_text(FIXTURE+declarations+source+setup+'}\n'+EXPORTS)
        dll_path=temp/'pipeline.dll'
        command=temp/'build.cmd'
        command.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'
            +f'cl /nologo /LD /EHsc /std:c++17 /Od /I"{ROOT / "indra/newview"}" "{fixture}" /Fe:"{dll_path}"\n')
        build=subprocess.run(['cmd','/c',str(command)],cwd=temp,text=True,capture_output=True)
        assert build.returncode==0,build.stdout+build.stderr
        service=C.CDLL(str(dll_path))
        free_library=C.WinDLL('kernel32').FreeLibrary
        free_library.argtypes=[P]
        cleanup.callback(free_library,service._handle)
        bind_export(service,'pipeline_functions',None,C.POINTER(P))((P*len(addresses))(*addresses))
        capabilities=bind_export(service,'pipeline_capabilities',None,U,C.c_float,I)
        init=bind_export(service,'pipeline_init',I,C.c_char_p)
        pool=bind_export(service,'pipeline_pool',I,P,U)
        publish=bind_export(service,'pipeline_sources',I,P,U)
        emit=bind_export(service,'pipeline_emit',I,P,U,U)
        advance=bind_export(service,'pipeline_advance',I,C.c_float)
        view=bind_export(service,'pipeline_view',I,C.c_float)
        handles=bind_export(service,'pipeline_buffers',None,P)
        intervals=bind_export(service,'pipeline_intervals',I,C.POINTER(C.c_float),U,U)
        alpha_handles=bind_export(service,'pipeline_alpha_buffers',None,P)
        draw_interval=bind_export(service,'pipeline_draw',I,U,U,I)
        draw_glow=bind_export(service,'pipeline_draw_glow',I,U,U,I)
        project=bind_export(service,'pipeline_project',I,P,U)
        bounds_handle=bind_export(service,'pipeline_bounds',U)
        demand_handle=bind_export(service,'pipeline_demand',U,C.c_float)
        pick=bind_export(service,'pipeline_pick',I,P,P,P,P)
        retire_region=bind_export(service,'pipeline_retire_region',I,P,P)
        region_wind=bind_export(service,'pipeline_region_wind',I,P)
        reload=bind_export(service,'pipeline_reload',None)
        destroy=bind_export(service,'pipeline_destroy',None)
        shader_root=str(ROOT/'indra/newview/app_settings').encode()
        protect_state(); expected_state=snapshot()
        for mask,version,glsl in [(14,4.5,50),(13,4.5,50),(11,4.5,50),(7,4.5,50),(15,4.3,50),(15,4.5,20)]:
            capabilities(mask,version,glsl)
            assert not init(shader_root), 'unsupported GPU path must remain unavailable'
            assert snapshot()==expected_state
            assert fn('glGetError',U)()==0
        capabilities(15,4.5,50)
        assert init(shader_root)
        assert snapshot()==expected_state
        source_blob=C.create_string_buffer(struct.pack('<20f8I4f',*([0.]*19+[256.]+[1,0,0,1,0,0,0,0]+[0.,0.,1.,0.])))
        for count in (0,1,65,8192):
            rows=[]
            for i in range(count):
                p=[0.]*36+[13,int(i==0 or i==count-1),1024,i%3,0,1,i-1 if i else 0xffffffff,13]+[0.,0.,1.,0.]
                p[4]=1.; p[7]=10.; p[32:34]=[.5,.5]
                rows.append(p)
            payload=C.create_string_buffer(b''.join(struct.pack('<36f8I4f',*p) for p in rows))
            assert pool(payload,count)
            assert not pool(payload,count), 'must reject overwriting a resident pool'
            assert not advance(.5), 'unpublished sources must not run'
            assert publish(source_blob,1)
            assert not advance(-1.)
            assert not advance(float('nan'))
            assert advance(.5)
            assert snapshot()==expected_state
            result=(U*6)()
            handles(result); assert not any(result), 'view must be generated before use'
            assert view(16.)
            assert snapshot()==expected_state
            handles(result)
            n=1
            while n<count: n*=2
            assert result[5]==n
            state=download(result[0],n*192)
            if count:
                first=struct.unpack_from('<36f8I4f',state)
                assert abs(first[0]-.5)<1e-6
                last=struct.unpack_from('<36f8I4f',state,(count-1)*192)
                if count>1: assert last[42:44]==(0,13),last[42:44]
            entries=list(struct.iter_unpack('<8I',download(result[2],n*32)))
            expected_live=0 if count==0 else (1 if count==1 else 2)
            assert sum(e[4]!=0xffffffff for e in entries)==expected_live
            assert not view(0.)
            # Real shader reload must preserve particle buffers and state.
            old_buffer=result[0]
            reload(); assert init(shader_root)
            assert advance(.5); assert view(16.)
            handles(result); assert result[0]==old_buffer
            if count:
                first=struct.unpack_from('<36f8I4f',download(result[0],192))
                assert abs(first[0]-1.)<1e-6
            # Source retirement via explicit absence: existing slots expire on GPU.
            assert publish(source_blob,0)
            assert advance(0.); assert view(16.)
            handles(result)
            entries=list(struct.iter_unpack('<8I',download(result[2],n*32)))
            assert all(e[4]==0xffffffff for e in entries)
            assert snapshot()==expected_state
            destroy(); destroy()
            handles(result); assert not any(result)
            assert init(shader_root)
            print('PASS native dispatcher capacity',count,flush=True)
        destroy()
        assert fn('glGetError',U)()==0
        # Windows requires unloading the native fixture before temporary cleanup.


        # Production GPU allocator: deterministic first-free slots, bounded
        # prefix admission, generation-safe reuse, cross-burst ribbon direction.
        def particle(source=0, generation=1, ribbon=True, life=2.):
            row=[0.]*36+[0,1,1024 if ribbon else 0,0,source,generation,0xffffffff,0]+[0.,0.,1.,0.]
            row[7]=life; row[32:34]=[.5,.5]
            return row
        def packed(rows):
            return C.create_string_buffer(b''.join(struct.pack('<36f8I4f',*p) for p in rows))
        def resident():
            assert view(16.)
            result=(U*6)(); handles(result)
            return list(struct.iter_unpack('<36f8I4f',download(result[0],result[5]*192)))
        for capacity in (1,64,128,8192):
            assert pool(packed([[0.]*36+[0]*8+[0.]*4]*capacity),capacity)
            assert publish(source_blob,1)
            assert advance(0.)
            assert emit(packed([particle()]*capacity),capacity,capacity)
            assert snapshot()==expected_state
            rows=resident()
            assert all(p[36:38]==(1,1) for p in rows)
            for i,p in enumerate(rows):
                assert p[42:44]==((i+1,1) if i+1<capacity else (0xffffffff,0)),(i,p)
            # A full pool must not overwrite live particles or change their age.
            before=rows
            assert emit(packed([particle(life=7.)]),1,capacity)
            assert resident()==before
            assert advance(2.)  # exact expiry remains alive
            assert all(p[37] for p in resident())
            assert advance(.01)
            assert not any(p[37] for p in resident())
            assert emit(packed([particle()]*capacity),capacity,capacity)
            assert all(p[36:38]==(2,1) for p in resident())
            destroy(); assert init(shader_root)
        # Interleaved source births and a second burst connecting the old tail.
        assert pool(packed([[0.]*36+[0]*8+[0.]*4]*8),8)
        two_sources=C.create_string_buffer(source_blob.raw[:128]*2)
        assert publish(two_sources,2)
        assert emit(packed([particle(0),particle(1),particle(0)]),3,8)
        assert emit(packed([particle(1),particle(0),particle(1)]),3,5)
        rows=resident()
        assert [p[37] for p in rows]==[1,1,1,1,1,0,0,0]
        assert rows[0][42:44]==(2,1) and rows[2][42:44]==(4,1)
        assert rows[1][42:44]==(3,1) and rows[3][42:44]==(0xffffffff,0)
        assert not emit(packed([particle(2)]),1,8)
        assert not emit(packed([particle(0,2)]),1,8)
        assert not emit(packed([particle(life=0.)]),1,8)
        assert resident()==rows
        # Source removal retires live slots; recycled sources cannot inherit tails.
        assert publish(source_blob,0); assert advance(0.)
        changed=bytearray(two_sources.raw[:256]); struct.pack_into('<I',changed,80,2)
        assert publish(C.create_string_buffer(bytes(changed)),2)
        assert emit(packed([particle(0,2)]),1,8)
        rows=resident(); assert rows[0][36:38]==(2,1)
        assert rows[0][42:44]==(0xffffffff,0)
        destroy(); assert init(shader_root)
        # A saturated generation is never recycled or wrapped into a stale handle.
        retired=particle(); retired[36]=0xffffffff; retired[37]=0
        assert pool(packed([retired]),1); assert publish(source_blob,1)
        assert emit(packed([particle()]),1,1)
        rows=resident(); assert rows[0][36:38]==(0xffffffff,0)
        destroy()
        assert snapshot()==expected_state
        assert fn('glGetError',U)()==0
        assert init(shader_root)
        rows=[]
        for x,hud in [(10,False),(5,False),(5,False),(-3,False),(8,True),(0,True)]:
            p=particle(ribbon=False); p[0]=x; p[36]=1; p[38]=0x40000000 if hud else 0
            rows.append(p)
        assert pool(packed(rows),len(rows)); assert view(16.)
        for domain,depths,expected in [
            (0,[],[(0,4)]),
            (0,[10,5,5,0,-4],[(0,0),(0,1),(1,0),(1,2),(3,1),(4,0)]),
            (1,[10,5,0,-4],[(4,0),(4,1),(5,0),(5,1),(6,0)])]:
            assert intervals((C.c_float*len(depths))(*depths),len(depths),domain)
            alpha=(U*5)(); alpha_handles(alpha); assert alpha[4]==len(expected)
            ranges=list(struct.iter_unpack('<4I',download(alpha[2],16*alpha[4])))
            assert [(r[0],r[1]) for r in ranges]==expected,ranges
            assert snapshot()==expected_state
        # Execute the production GPU counts directly; no host copy feeds draws.
        draw_program=program([(0x8B31,'#version 450 core\nuniform int particle_interval; void main(){gl_Position=vec4(float(particle_interval)*.01,0,0,1);}')])
        vao=U(); query=U()
        fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao))
        fn('glBindVertexArray',None,U)(vao)
        fn('glGenQueries',None,I,C.POINTER(U))(1,C.byref(query))
        indirect_count=fn('glMultiDrawArraysIndirectCountARB',None,U,P,S,I,I)
        fn('glEnable',None,U)(0x8C89) # rasterizer discard
        use(draw_program); bind(0x8F3F,alpha[3]); bind(0x80EE,alpha[2])
        for i,(_,count) in enumerate(expected):
            fn('glBeginQuery',None,U,U)(0x8C87,query)
            uniform_location=fn('glGetUniformLocation',I,U,C.c_char_p)(draw_program,b'particle_interval')
            before_draw=snapshot()
            assert draw_interval(i,owned[0],uniform_location)
            assert snapshot()==before_draw
            for token,value in [(0x85B5,vao.value),(0x8F43,alpha[3]),(0x80EF,alpha[2])]:
                actual=I(); get_integer(token,C.byref(actual)); assert actual.value==value
            fn('glEndQuery',None,U)(0x8C87)
            primitives=U(); fn('glGetQueryObjectuiv',None,U,U,C.POINTER(U))(query,0x8866,C.byref(primitives))
            assert primitives.value==count*2,(i,primitives.value,count)
            fn('glBeginQuery',None,U,U)(0x8C87,query)
            assert draw_glow(i,owned[0],uniform_location)
            fn('glEndQuery',None,U)(0x8C87)
            fn('glGetQueryObjectuiv',None,U,U,C.POINTER(U))(query,0x8866,C.byref(primitives))
            assert primitives.value==count*4,(i,primitives.value,count)
            assert snapshot()==before_draw
        bind(0x8F3F,0); bind(0x80EE,0)
        fn('glDisable',None,U)(0x8C89); use(guard_program)
        fn('glDeleteQueries',None,I,C.POINTER(U))(1,C.byref(query))
        fn('glDeleteVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao))
        fn('glDeleteProgram',None,U)(draw_program)
        assert not intervals((C.c_float*2)(1,2),2,0)
        alpha_handles(alpha); assert not any(alpha)
        destroy()
        assert snapshot()==expected_state
        assert fn('glGetError',U)()==0
        # Resident geometry follows the existing quad convention, including
        # byte-quantized colors and parent/source/detached ribbon endpoints.
        def geometry(rows, source=source_blob):
            assert init(shader_root)
            assert pool(packed(rows),len(rows)); assert publish(source,1)
            assert view(16.); assert intervals((C.c_float*0)(),0,0)
            a=(U*5)(); alpha_handles(a)
            return list(struct.iter_unpack('<16f',download(a[0],len(rows)*4*64)))
        p=particle(ribbon=False); p[0:3]=[0.,2.,0.]; p[32:34]=[2.,4.]
        p[24:28]=[.2,.5,1.,.75]; p[34]=128.
        verts=geometry([p])
        expected_positions=[(-1,2,2),(-1,2,-2),(1,2,2),(1,2,-2)]
        for i,v in enumerate(verts):
            assert v[:3]==expected_positions[i],v
            assert v[4:7]==(-1.,0.,0.)
            assert all(math.isclose(v[8+k],math.floor(p[24+k]*255+.5)/255,abs_tol=1e-6) for k in range(4))
            assert v[12:14]==((0. if i<2 else 1.),(1. if i%2==0 else 0.))
            assert math.isclose(v[14],128/255,abs_tol=1e-6)
        destroy()
        p[38]=32; p[4:7]=[1.,0.,0.]
        verts=geometry([p])
        assert [v[:3] for v in verts]==[(2.,2.,1.),(-2.,2.,1.),(2.,2.,-1.),(-2.,2.,-1.)]
        destroy()
        p[38]=0x40000000
        verts=geometry([p])
        right=(2/math.sqrt(5),-1/math.sqrt(5),0.)
        for i,v in enumerate(verts):
            expected=[p[k]+(-1 if i<2 else 1)*right[k]+((2 if i%2==0 else -2) if k==2 else 0) for k in range(3)]
            assert all(math.isclose(v[k],expected[k],abs_tol=1e-6) for k in range(3))
        destroy()
        p[38]=1024; p[42:44]=[1,7]
        q=particle(); q[0:3]=[3.,4.,5.]; q[36]=7; q[32]=6.; q[44:47]=[0.,1.,0.]
        verts=geometry([p,q])
        assert [v[:3] for v in verts[:4]]==[(3.,7.,5.),(3.,1.,5.),(0.,2.,1.),(0.,2.,-1.)]
        destroy()
        p[42:44]=[0xffffffff,0]; p[28]=4.
        source=bytearray(source_blob.raw[:128]); struct.pack_into('<4f',source,112,0.,0.,1.,1.)
        verts=geometry([p],C.create_string_buffer(bytes(source)))
        assert [v[:3] for v in verts]==[(0.,0.,2.),(0.,0.,-2.),(0.,2.,1.),(0.,2.,-1.)]
        destroy()
        verts=geometry([p])
        assert [v[:3] for v in verts]==[(0.,2.,1.),(0.,2.,-1.),(0.,2.,1.),(0.,2.,-1.)]
        destroy()
        assert snapshot()==expected_state
        assert fn('glGetError',U)()==0
        assert init(shader_root)
        near=particle(ribbon=False); near[36]=1; near[32:34]=[1.,1.]
        far=near.copy(); far[0]=10.
        hud=near.copy(); hud[38]=0x40000000
        assert pool(packed([near,far,hud]),3); assert publish(source_blob,1)
        identity=(C.c_float*16)(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
        assert project(identity,0)
        result=(U*6)(); handles(result)
        entries=list(struct.iter_unpack('<8I',download(result[2],result[5]*32)))
        assert [e[4] for e in entries if e[4]!=0xffffffff]==[0],entries
        bounds=list(struct.iter_unpack('<8f',download(bounds_handle(),3*32)))
        assert [b[3] for b in bounds]==[1.,0.,0.],bounds
        demand=demand_handle(2.)
        areas=struct.unpack('<8192f',download(demand,8192*4))
        assert abs(areas[near[39]]-40.)<1e-6,areas[near[39]]
        assert sum(x!=0 for x in areas)==1
        for corner in [(-.5,0,-.5),(.5,0,.5)]:
            assert all(bounds[0][k]<=corner[k]<=bounds[0][4+k] for k in range(3))
        source=(U*2)(); fraction=C.c_float()
        assert pick((C.c_float*3)(0,-2,0),(C.c_float*3)(0,2,0),source,C.byref(fraction))
        assert list(source)==[0,1] and abs(fraction.value-.5)<1e-6
        assert not pick((C.c_float*3)(3,-2,0),(C.c_float*3)(3,2,0),source,C.byref(fraction))
        assert retire_region((C.c_float*3)(-1,-1,0),(C.c_float*3)(1,1,0))
        rows=resident()
        assert [p[37] for p in rows[:3]]==[0,1,1], 'region retirement must spare HUD and remote particles'
        destroy(); assert init(shader_root)
        wind_particle=particle(ribbon=False); wind_particle[0:3]=[260.,1.,0.]; wind_particle[38]=8
        assert pool(packed([wind_particle]),1); assert region_wind(source_blob); assert advance(.5)
        rows=resident()
        assert math.isclose(rows[0][4],.7,abs_tol=1e-6),rows[0]
        assert math.isclose(rows[0][0],260.35,abs_tol=3e-5),rows[0]
        destroy()
        assert snapshot()==expected_state
        assert fn('glGetError',U)()==0
        print('PASS conservative GPU group bounds, view culling, on-demand nearest picking, region retirement and cross-region wind',flush=True)
        print('PASS resident billboard/velocity/HUD/ribbon geometry and appearance',flush=True)
        print('PASS GPU alpha intervals, equal-depth ties, domains and indirect draw counts',flush=True)
        print('PASS GPU birth allocation, admission, recycling and ribbon continuity',flush=True)
        print('PASS native dispatch, state restoration, residency, repair and reload',flush=True)
finally:
    fn('glDeleteBuffers',None,I,C.POINTER(U))(4,owned)
    fn('glDeleteProgram',None,U)(guard_program)
    gl.wglMakeCurrent(None,None); gl.wglDeleteContext(context)
    user.ReleaseDC(window,hdc); user.DestroyWindow(window)
