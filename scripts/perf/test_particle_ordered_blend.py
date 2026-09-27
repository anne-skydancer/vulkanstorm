"""Validate ordered bindless particle blend equations against fixed-function GL.
Windows: python scripts/perf/test_particle_ordered_blend.py [--opengl DLL]
Exercises the optional GL 4.5 interlock/bindless/draw-parameters GPU path.
This standalone semantic test does not measure viewer performance or lighting.
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
parser.add_argument('--target-format', choices=('rgba32f','rgba16f'), default='rgba32f')
args = parser.parse_args()
target_format = 0x8814 if args.target_format == "rgba32f" else 0x881A
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


# Isolated semantic validation; production particle submission is integrated separately.
vertex='''#version 450 core
#extension GL_ARB_shader_draw_parameters : require
#extension GL_ARB_bindless_texture : require
struct Material { sampler2D diffuse; uint src; uint dst; };
layout(std430,binding=0) readonly buffer Draws {Material draws[];};
flat out sampler2D drawTexture;
flat out uvec2 blendFactors;
layout(std430,binding=1) readonly buffer Depths { float drawDepths[]; };
void main(){
    uint drawIndex=uint(gl_DrawIDARB);
    drawTexture=draws[drawIndex].diffuse;
    blendFactors=uvec2(draws[drawIndex].src,draws[drawIndex].dst);
    vec2 p[3]=vec2[3](vec2(-1.,-1.),vec2(3.,-1.),vec2(-1.,3.));
    gl_Position=vec4(p[gl_VertexID],drawDepths[gl_DrawIDARB]*2.-1.,1.);
}'''
fragment='''#version 450 core
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_fragment_shader_interlock : require
layout(pixel_interlock_ordered) in;
// A writable virtual depth image is updated inside the same interlock as color.
// Early fixed-function depth writes would incorrectly survive alpha discard.
layout(binding=0,rgba32f) uniform coherent image2D destination;
layout(binding=1,r32f) uniform coherent image2D particleDepth;
uniform int depthMode; // 0 off, 1 LEQUAL read, 2 LEQUAL read/write
uniform int glowPass;
uniform float minimumAlpha;
// Match production: the single vertex module loads the material. Fragment
// helper linking must not change bindless sampling or blend metadata.
flat in sampler2D drawTexture;
flat in uvec2 blendFactors;
vec3 factor(uint code,vec4 s,vec4 d){
    if(code==0u)return vec3(1.);
    if(code==1u)return vec3(0.);
    if(code==2u)return d.rgb;
    if(code==3u)return s.rgb;
    if(code==4u)return vec3(1.)-d.rgb;
    if(code==5u)return vec3(1.)-s.rgb;
    if(code==7u)return vec3(s.a);
    return vec3(1.-s.a);
}
void main(){
    vec4 s=texture(drawTexture,vec2(.5));
    if(glowPass==0 && s.a<minimumAlpha) discard;
    beginInvocationInterlockARB();
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    if(depthMode==0 || gl_FragCoord.z<=imageLoad(particleDepth,pixel).r)
    {
        vec4 d=imageLoad(destination,pixel);
        vec4 result=glowPass!=0 ? vec4(d.rgb,d.a+s.a*0.25)
            : vec4(s.rgb*factor(blendFactors.x,s,d)+d.rgb*factor(blendFactors.y,s,d),d.a*(1.-s.a));
        imageStore(destination,pixel,result);
        if(depthMode==2 && glowPass==0)
            imageStore(particleDepth,pixel,vec4(gl_FragCoord.z));
    }
    endInvocationInterlockARB();
}'''
reference_vertex='''#version 450 core
uniform float drawDepth;
void main(){
    vec2 p[3]=vec2[3](vec2(-1.,-1.),vec2(3.,-1.),vec2(-1.,3.));
    gl_Position=vec4(p[gl_VertexID],drawDepth*2.-1.,1.);
}'''
reference_fragment='''#version 450 core
uniform sampler2D source;
out vec4 color;
uniform float minimumAlpha;
uniform int glowPass;
void main(){
    color=texture(source,vec2(.5));
    if(glowPass==0 && color.a<minimumAlpha) discard;
    if(glowPass!=0) color=vec4(0,0,0,color.a*0.25);
}
'''
# Regression coverage: native AMD linked successfully but misread a fragment
# SSBO containing samplers whenever a second fragment object was attached.
fragment_helper='#version 420\nfloat unusedParticleHelper(float v){return v;}\n'
interlock=program([(0x8B31,vertex),(0x8B30,fragment.replace("rgba32f",args.target_format)),(0x8B30,fragment_helper)])
reference=program([(0x8B31,reference_vertex),(0x8B30,reference_fragment)])
gen_texture=fn('glGenTextures',None,I,C.POINTER(U)); bind_texture=fn('glBindTexture',None,U,U)
texture_data=fn('glTexImage2D',None,U,I,I,I,I,I,U,U,P)
texture_sub=fn('glTexSubImage2D',None,U,I,I,I,I,I,U,U,P)
texture_param=fn('glTexParameteri',None,U,U,I)
texture_read=fn('glGetTexImage',None,U,I,U,U,P)
get_handle=fn('glGetTextureHandleARB',C.c_uint64,U)
resident=fn('glMakeTextureHandleResidentARB',None,C.c_uint64)
nonresident=fn('glMakeTextureHandleNonResidentARB',None,C.c_uint64)
gen_buffer=fn('glGenBuffers',None,I,C.POINTER(U)); bind_buffer=fn('glBindBuffer',None,U,U)
buffer_data=fn('glBufferData',None,U,S,P,U);base=fn('glBindBufferBase',None,U,U,U)
gen_fb=fn('glGenFramebuffers',None,I,C.POINTER(U)); bind_fb=fn('glBindFramebuffer',None,U,U)
attach=fn('glFramebufferTexture2D',None,U,U,U,U,I)
fb_param=fn('glFramebufferParameteri',None,U,U,I)
check_fb=fn('glCheckFramebufferStatus',U,U)
bind_image=fn('glBindImageTexture',None,U,U,I,C.c_ubyte,I,U,U)
barrier=fn('glMemoryBarrier',None,U)
blend=fn('glBlendFuncSeparate',None,U,U,U,U)
draw=fn('glDrawArrays',None,U,I,I);multi=fn('glMultiDrawArraysIndirect',None,U,P,I,I)
enable=fn('glEnable',None,U);disable=fn('glDisable',None,U)
textures=(U*6)();gen_texture(6,textures)
va=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(va));fn('glBindVertexArray',None,U)(va.value)
fbs=(U*2)();gen_fb(2,fbs)
buffers=(U*3)();gen_buffer(3,buffers)
colors=[(.23,.61,.17,.37),(.83,.11,.49,.63)]
handles=[]
for i,t in enumerate(textures):
    bind_texture(0x0DE1,t)
    texture_param(0x0DE1,0x2801,0x2600);texture_param(0x0DE1,0x2800,0x2600)
    color=(C.c_float*4)(*(colors[i] if i<2 else (0.,0.,0.,0.)))
    if i<4: texture_data(0x0DE1,0,0x8814 if i<2 else target_format,1,1,0,0x1908,0x1406,color)
    else: texture_data(0x0DE1,0,0x822E if i==4 else 0x8CAC,1,1,0,0x1903 if i==4 else 0x1902,0x1406,color)
    if i<2:
        handle=get_handle(t);resident(handle);handles.append(handle)
# Image destination is NOT attached to the interlock framebuffer.
bind_fb(0x8D40,fbs[0]);fb_param(0x8D40,0x9310,1);fb_param(0x8D40,0x9311,1)
fn('glDrawBuffer',None,U)(0);fn('glReadBuffer',None,U)(0)
assert check_fb(0x8D40)==0x8CD5
bind_fb(0x8D40,fbs[1]);attach(0x8D40,0x8CE0,0x0DE1,textures[3],0)
attach(0x8D40,0x8D00,0x0DE1,textures[5],0)
assert check_fb(0x8D40)==0x8CD5
fn('glViewport',None,I,I,I,I)(0,0,1,1)
valid=[0,1,2,3,4,5,7,9]
factors={0:1,1:0,2:0x306,3:0x300,4:0x307,5:0x301,7:0x302,9:0x303}
sequences=[[(0,s,d)] for s in valid for d in valid]
# Deliberately alternating textures and noncommuting blend modes.
sequences += [[(i%2,7,9) if i%3 else (i%2,1,3) for i in range(n)] for n in (2,17,257,8192)]
get_uniform=fn('glGetUniformLocation',I,U,C.c_char_p)
uniform_i=fn('glUniform1i',None,I,I); uniform_f=fn('glUniform1f',None,I,C.c_float)
def integer(prog,name,value): uniform_i(get_uniform(prog,name.encode()),value)
def floating(prog,name,value): uniform_f(get_uniform(prog,name.encode()),value)
try:
    cases=[(seq,[.5]*len(seq),0,False,False) for seq in sequences]
    # Depth-writing and read-only passes, with equality, rejection, transparent
    # texels and a separate additive glow pass (after all color contributions).
    for depth_mode in (1,2):
        for glow in (False,True):
            for transparent in (False,True):
                seq=[(0,7,9),(0,7,9),(1,0,1),(0,7,9),(1,7,9)]
                cases.append((seq,[.8,.3,.2,.3,.1],depth_mode,glow,transparent))
    for sequence,depths,depth_mode,glow,transparent in cases:
        bind_texture(0x0DE1,textures[1])
        color=(C.c_float*4)(*colors[1][:3],.01 if transparent else colors[1][3])
        texture_sub(0x0DE1,0,0,0,1,1,0x1908,0x1406,color)
        initial=(C.c_float*4)(.13,.47,.89,.73)
        for t in textures[2:4]:
            bind_texture(0x0DE1,t);texture_sub(0x0DE1,0,0,0,1,1,0x1908,0x1406,initial)
        depth_initial=(C.c_float*1)(.5)
        for i in (4,5):
            bind_texture(0x0DE1,textures[i]);texture_sub(0x0DE1,0,0,0,1,1,0x1903 if i==4 else 0x1902,0x1406,depth_initial)
        records=b''.join(struct.pack('<Q2I',handles[t],s,d) for t,s,d in sequence)
        bind_buffer(0x90D2,buffers[0]);buffer_data(0x90D2,len(records),C.create_string_buffer(records),0x88E8)
        base(0x90D2,0,buffers[0])
        depth_data=(C.c_float*len(depths))(*depths)
        bind_buffer(0x90D2,buffers[2]);buffer_data(0x90D2,C.sizeof(depth_data),depth_data,0x88E8)
        base(0x90D2,1,buffers[2])
        commands=struct.pack('<4I',3,1,0,0)*len(sequence)
        bind_buffer(0x8F3F,buffers[1]);buffer_data(0x8F3F,len(commands),C.create_string_buffer(commands),0x88E8)
        use(interlock);bind_fb(0x8D40,fbs[0]);disable(0x0BE2);disable(0x0B71)
        bind_image(0,textures[2],0,0,0,0x88BA,target_format)
        bind_image(1,textures[4],0,0,0,0x88BA,0x822E)
        integer(interlock,'depthMode',depth_mode); integer(interlock,'glowPass',0)
        floating(interlock,'minimumAlpha',.1 if transparent else 0.)
        multi(4,None,len(sequence),0)
        barrier(0x20|0x100|0x400)
        if glow:
            integer(interlock,'glowPass',1)
            multi(4,None,len(sequence),0)
            barrier(0x20|0x100|0x400)
        use(reference);bind_fb(0x8D40,fbs[1]);enable(0x0BE2)
        if depth_mode: enable(0x0B71)
        else: disable(0x0B71)
        fn('glDepthFunc',None,U)(0x0203) # LEQUAL
        fn('glDepthMask',None,C.c_ubyte)(depth_mode==2)
        integer(reference,'glowPass',0)
        floating(reference,'minimumAlpha',.1 if transparent else 0.)
        for (t,s,d),z in zip(sequence,depths):
            floating(reference,'drawDepth',z)
            bind_texture(0x0DE1,textures[t]);blend(factors[s],factors[d],0,0x303);draw(4,0,3)
        if glow:
            integer(reference,'glowPass',1); fn('glDepthMask',None,C.c_ubyte)(0)
            blend(0,1,1,1)
            for (t,s,d),z in zip(sequence,depths):
                floating(reference,'drawDepth',z)
                bind_texture(0x0DE1,textures[t]);draw(4,0,3)
        results=[]
        for t in textures[2:4]:
            bind_texture(0x0DE1,t);result=(C.c_float*4)();texture_read(0x0DE1,0,0x1908,0x1406,result)
            results.append(list(result))
        assert all(math.isclose(a,b,rel_tol=2e-5 if target_format==0x8814 else 2e-3,abs_tol=2e-6 if target_format==0x8814 else 1e-3) for a,b in zip(*results)),(sequence[:3],len(sequence),depth_mode,glow,transparent,results)
        depth_results=[]
        for i in (4,5):
            bind_texture(0x0DE1,textures[i]);result=C.c_float()
            texture_read(0x0DE1,0,0x1903 if i==4 else 0x1902,0x1406,C.byref(result))
            depth_results.append(result.value)
        assert math.isclose(*depth_results,abs_tol=1e-6),depth_results
        assert fn('glGetError',U)()==0
    print('PASS '+args.target_format+': 64 blend pairs, 2/17/257/8192 ordered draws, depth read/write, alpha discard and glow',flush=True)

finally:
    for handle in handles:nonresident(handle)
    fn('glDeleteTextures',None,I,C.POINTER(U))(6,textures)
    fn('glDeleteBuffers',None,I,C.POINTER(U))(3,buffers)
    fn('glDeleteFramebuffers',None,I,C.POINTER(U))(2,fbs)
    fn('glDeleteVertexArrays',None,I,C.POINTER(U))(1,C.byref(va))
    for p in (interlock,reference):fn('glDeleteProgram',None,U)(p)
    gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context)
    user.ReleaseDC(window,hdc);user.DestroyWindow(window)
