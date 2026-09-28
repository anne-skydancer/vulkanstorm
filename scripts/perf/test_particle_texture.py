"""Real-GL test of the production bindless texture snapshot cache (Windows)."""
from pathlib import Path
helper=Path(__file__).with_name('test_particle_ordered_blend.py')
exec(compile(helper.read_text().split('# Isolated semantic validation;')[0],str(helper),'exec'))
import subprocess,tempfile,contextlib
ROOT=Path(__file__).resolve().parents[2]
viewer=(ROOT/'indra/newview/llparticleviewer.cpp').read_text()
selection=viewer[viewer.index('LLImageGL* publishedParticleImage('):viewer.index('bool updateMaterials()')]
API=[
 ('glClientWaitSync','GLenum','GLsync, GLbitfield, GLuint64'),
 ('glMakeTextureHandleNonResidentARB','void','GLuint64'),
 ('glDeleteTextures','void','GLsizei, const GLuint*'),
 ('glDeleteSync','void','GLsync'), ('glFenceSync','GLsync','GLenum, GLbitfield'), ('glFinish','void',''),
 ('glGetTextureLevelParameteriv','void','GLuint, GLint, GLenum, GLint*'),
 ('glGetTextureParameteriv','void','GLuint, GLenum, GLint*'),
 ('glGetTextureParameterfv','void','GLuint, GLenum, GLfloat*'),
 ('glCreateTextures','void','GLenum, GLsizei, GLuint*'),
 ('glTextureStorage2D','void','GLuint, GLsizei, GLenum, GLsizei, GLsizei'),
 ('glTextureParameteri','void','GLuint, GLenum, GLint'),
 ('glTextureParameterf','void','GLuint, GLenum, GLfloat'),
 ('glTextureParameterfv','void','GLuint, GLenum, const GLfloat*'),
 ('glCopyImageSubData','void','GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei'),
 ('glMemoryBarrier','void','GLbitfield'), ('glGetTextureHandleARB','GLuint64','GLuint'),
 ('glMakeTextureHandleResidentARB','void','GLuint64'), ('glGetError','GLenum','')]
fixture=r'''
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <cstdint>
#include <iostream>
#define LL_WARNS(channel) std::cerr
#define LL_ENDL std::endl
using U64=std::uint64_t;
struct {bool mHasAnisotropic=true;} gGLManager;
struct LLImageGL {GLuint name=0; U64 revision=1;
 GLuint getTexName()const{return name;} U64 getContentRevision()const{return revision;}
 void updateBindStats()const{} };
template<class T> struct LLPointer {
 T* p=nullptr; LLPointer& operator=(T* value){p=value;return *this;}
 bool operator==(T* value)const{return p==value;}
};
'''
try:
    with tempfile.TemporaryDirectory(prefix='vulkanstorm-particle-texture-') as temporary,contextlib.ExitStack() as cleanup:
        temp=Path(temporary)
        header=(ROOT/'indra/newview/llparticletexture.h').read_text()
        for name in ('llgl.h','llimagegl.h','llpointer.h'): header=header.replace('#include "'+name+'"','')
        code=(ROOT/'indra/newview/llparticletexture.cpp').read_text()
        code=code.replace('#include "llviewerprecompiledheaders.h"','').replace('#include "llparticletexture.h"','')
        declarations='';setup='extern "C" __declspec(dllexport) void functions(void** f){\n';addresses=[]
        for i,(name,result,arguments) in enumerate(API):
            declarations+=f'using Fn{i}={result}(APIENTRY*)({arguments}); Fn{i} native_{name};\n#define {name} native_{name}\n'
            setup+=f'native_{name}=reinterpret_cast<Fn{i}>(f[{i}]);\n'
            address=gl.wglGetProcAddress(name.encode())
            if address in (None,0,1,2,3,C.c_void_p(-1).value): address=C.cast(getattr(gl,name),P).value
            addresses.append(address)
        setup+='}\n'
        exports=r'''
LLImageGL input;
LLParticleTexture* cache=nullptr;
extern "C" __declspec(dllexport) GLuint64 update(GLuint name,U64 revision) {
 input.name=name;input.revision=revision;
 if(!cache) cache=new LLParticleTexture;
 return cache->update(&input)?cache->handle():0;
}
extern "C" __declspec(dllexport) void release() {
 delete cache;cache=nullptr; LLParticleTexture::collect(true);
}
extern "C" __declspec(dllexport) GLuint selectSource(GLuint requested,GLuint particle,GLuint viewer) {
 LLImageGL a,b,c;a.name=requested;b.name=particle;c.name=viewer;
 auto* selected=publishedParticleImage(&a,&b,&c);
 return selected?selected->getTexName():0;
}
'''
        source=temp/'texture.cpp';source.write_text(fixture+declarations+header+code+selection+setup+exports)
        dll=temp/'texture.dll';cmd=temp/'build.cmd'
        cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'+f'cl /nologo /LD /EHsc /std:c++17 /Od /I"{ROOT / "build-vc170-64/packages/include"}" "{source}" /Fe:"{dll}"\n')
        build=subprocess.run(['cmd','/c',str(cmd)],cwd=temp,text=True,capture_output=True)
        assert build.returncode==0,build.stdout+build.stderr
        service=C.CDLL(str(dll));free=C.WinDLL('kernel32').FreeLibrary;free.argtypes=[P]
        cleanup.callback(free,service._handle)
        service.functions.argtypes=[C.POINTER(P)];service.functions((P*len(addresses))(*addresses))
        service.update.argtypes=[U,C.c_uint64];service.update.restype=C.c_uint64;service.release.argtypes=[]
        service.selectSource.argtypes=[U,U,U];service.selectSource.restype=U
        texture=U();fn('glGenTextures',None,I,C.POINTER(U))(1,C.byref(texture))
        bind=fn('glBindTexture',None,U,U);teximage=fn('glTexImage2D',None,U,I,I,I,I,I,U,U,P)
        param=fn('glTexParameteri',None,U,U,I);resident=fn('glIsTextureHandleResidentARB',C.c_ubyte,C.c_uint64)
        bind(0x0DE1,texture.value);param(0x0DE1,0x2801,0x2703);param(0x0DE1,0x2800,0x2601)
        pixels=(C.c_ubyte*16)(*([255,0,0,255]*4));mip=(C.c_ubyte*4)(0,255,0,255)
        teximage(0x0DE1,0,0x8058,2,2,0,0x1908,0x1401,pixels)
        teximage(0x0DE1,1,0x8058,1,1,0,0x1908,0x1401,mip)
        # Both fetched images can lack storage at startup. Use the resident
        # viewer default and exercise its actual bindless snapshot creation.
        assert service.selectSource(0,0,0)==0
        assert service.selectSource(0,0,texture.value)==texture.value
        assert service.selectSource(0,22,texture.value)==22
        assert service.selectSource(33,22,texture.value)==33
        fallback_handle=service.update(service.selectSource(0,0,texture.value),1)
        assert fallback_handle and resident(fallback_handle)
        handle=service.update(texture.value,1);assert handle and resident(handle)
        assert handle==fallback_handle
        assert service.update(texture.value,1)==handle
        # The source remains mutable; an unchanged allocation retains its handle.
        pixels=(C.c_ubyte*16)(*([0,0,255,255]*4));teximage(0x0DE1,0,0x8058,2,2,0,0x1908,0x1401,pixels)
        assert fn('glGetError',U)()==0
        assert service.update(texture.value,2)==handle
        vs='#version 450 core\nvoid main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexID],0,1);}'
        fs='''#version 450 core
#extension GL_ARB_bindless_texture : require
layout(std430,binding=0) readonly buffer Input {sampler2D image;};
uniform float level;out vec4 color;
void main(){color=textureLod(image,vec2(.5),level);}
'''
        output=U();fbo=U()
        fn('glGenTextures',None,I,C.POINTER(U))(1,C.byref(output));bind(0x0DE1,output)
        teximage(0x0DE1,0,0x8058,1,1,0,0x1908,0x1401,None)
        fn('glGenFramebuffers',None,I,C.POINTER(U))(1,C.byref(fbo))
        fn('glBindFramebuffer',None,U,U)(0x8D40,fbo)
        fn('glFramebufferTexture2D',None,U,U,U,U,I)(0x8D40,0x8CE0,0x0DE1,output,0)
        assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
        prog=program([(0x8B31,vs),(0x8B30,fs)]);use(prog)
        buffer=U();fn('glGenBuffers',None,I,C.POINTER(U))(1,C.byref(buffer));fn('glBindBuffer',None,U,U)(0x90D2,buffer)
        value=C.c_uint64(handle);fn('glBufferData',None,U,S,P,U)(0x90D2,8,C.byref(value),0x88E4)
        fn('glBindBufferBase',None,U,U,U)(0x90D2,0,buffer)
        vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
        fn('glViewport',None,I,I,I,I)(0,0,1,1)
        loc=fn('glGetUniformLocation',I,U,C.c_char_p)(prog,b'level')
        for level,expected in [(0,(0,0,255)),(1,(0,255,0))]:
            fn('glUniform1f',None,I,C.c_float)(loc,level);fn('glDrawArrays',None,U,I,I)(4,0,3)
            result=(C.c_ubyte*4)();fn('glReadPixels',None,I,I,I,I,U,U,P)(0,0,1,1,0x1908,0x1401,result)
            assert tuple(result[:3])==expected,(level,list(result))
        bind(0x0DE1,texture.value);param(0x0DE1,0x2802,0x812F)
        new_handle=service.update(texture.value,3);assert new_handle and new_handle!=handle
        service.release();assert fn('glGetError',U)()==0
        for internal,rgba,expected in [(0x8051,(255,0,0,255),(255,0,0,255)),
                                       (0x8C43,(128,128,128,255),(55,55,55,255)),
                                       (0x83F3,(0,255,0,255),(0,255,0,255))]:
            source=U();fn('glGenTextures',None,I,C.POINTER(U))(1,C.byref(source))
            bind(0x0DE1,source);param(0x0DE1,0x2801,0x2601);param(0x0DE1,0x2800,0x2601)
            pixels=(C.c_ubyte*64)(*(list(rgba)*16))
            teximage(0x0DE1,0,internal,4,4,0,0x1908,0x1401,pixels)
            value=C.c_uint64(service.update(source.value,1));assert value.value and resident(value)
            fn('glBindBuffer',None,U,U)(0x90D2,buffer)
            fn('glBufferData',None,U,S,P,U)(0x90D2,8,C.byref(value),0x88E4)
            fn('glUniform1f',None,I,C.c_float)(loc,0);fn('glDrawArrays',None,U,I,I)(4,0,3)
            result=(C.c_ubyte*4)();fn('glReadPixels',None,I,I,I,I,U,U,P)(0,0,1,1,0x1908,0x1401,result)
            assert all(abs(result[k]-expected[k])<=1 for k in range(4)),(internal,list(result),expected)
            service.release();fn('glDeleteTextures',None,I,C.POINTER(U))(1,C.byref(source))
            assert fn('glGetError',U)()==0
        fn('glDeleteFramebuffers',None,I,C.POINTER(U))(1,C.byref(fbo))
        fn('glDeleteTextures',None,I,C.POINTER(U))(1,C.byref(output))
        fn('glDeleteTextures',None,I,C.POINTER(U))(1,C.byref(texture))
        fn('glDeleteBuffers',None,I,C.POINTER(U))(1,C.byref(buffer))
        fn('glDeleteVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glDeleteProgram',None,U)(prog)
        assert fn('glGetError',U)()==0
        print('PASS production texture cache: mutable source, mips, content refresh, handle reuse and fence retirement',flush=True)
finally:
    gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context)
    user.ReleaseDC(window,hdc);user.DestroyWindow(window)
