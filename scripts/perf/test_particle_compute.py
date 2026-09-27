"""Exercise the production particle geometry shader in a real GL 4.3 Core context.

Windows: python scripts/perf/test_particle_compute.py [--opengl path/to/opengl32.dll]
Checks random billboard geometry, winding, packed attributes, UVs and untouched padding/index
storage, dispatch tails, repeat generation and subsequent CPU overwrite.
This is a GPU correctness test, not a viewer performance benchmark.
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
attributes=(C.c_int*7)(0x2091,4,0x2092,3,0x9126,1,0)
core=create_core(hdc,None,attributes)
assert core, 'OpenGL 4.3 Core context unavailable'
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

shader_path=Path(__file__).resolve().parents[2]/'indra/newview/app_settings/shaders/class1/objects/particleGeometryC.glsl'
compute=program([(0x91B9,shader_path.read_text())])
gen=fn('glGenBuffers',None,I,C.POINTER(U));bind=fn('glBindBuffer',None,U,U)
data=fn('glBufferData',None,U,S,P,U);base=fn('glBindBufferBase',None,U,U,U)
read=fn('glGetBufferSubData',None,U,S,S,P)
subdata=fn('glBufferSubData',None,U,S,S,P)
barrier=fn('glMemoryBarrier',None,U);dispatch=fn('glDispatchCompute',None,U,U,U)
uniform=fn('glUniform1ui',None,I,U);location=fn('glGetUniformLocation',I,U,C.c_char_p)
uniform3=fn('glUniform3f',None,I,C.c_float,C.c_float,C.c_float)
uniform4=fn('glUniform4ui',None,I,U,U,U,U)
get_error=fn('glGetError',U)
buffers=(U*3)();gen(3,buffers);SSBO=0x90D2

def normalize(v):
    size=math.sqrt(sum(x*x for x in v))
    return tuple(x/size for x in v)

def cross(a,b):
    return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])

def unit_or(v, fallback):
    length=math.sqrt(sum(x*x for x in v))
    return tuple(x/length for x in v) if length else fallback

def reference(center,sx,sy,camera,axis,parent,parent_axis,ps,flags):
    if flags & 1:
        return [tuple(c[j]+sign*a[j]*scale*.5 for j in range(3))
                for c,a,scale,sign in ((parent,parent_axis,ps,1),(parent,parent_axis,ps,-1),
                                      (center,axis,sx,1),(center,axis,sx,-1))]
    if flags & 4: camera=(-1.,0.,0.)
    at=unit_or(tuple(a-b for a,b in zip(center,camera)),(0,1,0))
    right=unit_or(cross(at,(0,0,1)),(1,0,0))
    up=unit_or(cross(right,at),(0,0,1))
    if flags & 2:
        velocity=unit_or(axis,(0,0,0))
        projection=tuple(sum(a*b for a,b in zip(velocity,v)) for v in (right,up))
        if sum(x*x for x in projection)>1e-12:
            x,y=normalize(projection)
            new_up=unit_or(tuple(x*r+y*u for r,u in zip(right,up)),up)
            right=unit_or(tuple(y*r-x*u for r,u in zip(right,up)),right)
            up=new_up
    return [tuple(center[j]+sign_x*right[j]*sx*.5+sign_y*up[j]*sy*.5 for j in range(3))
            for sign_x,sign_y in [(-1,1),(-1,-1),(1,1),(1,-1)]]

rng=random.Random(27092026)
checks=0
try:
    use(compute)
    for count in (1,63,64,65,511,16384):
        # Deliberate padding between planar arrays detects incorrect stride/layout
        # and preserves the UV area, which the compute shader must not touch.
        vertices=count*4
        offsets=(8,8+vertices*4+8,8+vertices*8+8+vertices*2+8,
                 8+vertices*8+8+vertices*2+8+vertices+8)
        size=offsets[3]+vertices+12
        initial=b'\xcd'*(size*4)
        bind(SSBO,buffers[1]);blob=C.create_string_buffer(initial)
        data(SSBO,len(initial),blob,0x88E0);base(SSBO,1,buffers[1])
        for camera in ((0.,0.,0.),(20.,-5.,12.)):
            rows=[]
            chunk=max(1,(count+6)//7)
            cameras=[tuple(v+.25*g for v in camera) for g in range((count+chunk-1)//chunk)]
            for i in range(count):
                pos=tuple(C.c_float(rng.uniform(-100,100)).value for _ in range(3))
                sx=C.c_float(rng.uniform(-4,4)).value
                sy=C.c_float(rng.uniform(-4,4)).value
                flags=(0,2,1,4,6,5,2,0)[i%8]
                axis=tuple(C.c_float(rng.uniform(-1,1)).value for _ in range(3))
                parent=tuple(C.c_float(rng.uniform(-100,100)).value for _ in range(3))
                parent_axis=tuple(C.c_float(x).value for x in normalize((.2,.3,.5)))
                ps=C.c_float(rng.uniform(-4,4)).value
                if i%16==0: sx=sy=0.
                if i%16==6: axis=(0.,0.,0.) # zero follow velocity
                view=cameras[i//chunk]
                if i%16==7: pos=view # coincident camera
                if i%16==15: pos=(view[0],view[1],view[2]+5.) # vertical view
                if i%16==14: axis=tuple(a-b for a,b in zip(pos,view)) # camera-parallel velocity
                color=rng.getrandbits(32);glow=rng.getrandbits(32)
                rows.append((pos,sx,sy,color,glow,axis,parent,parent_axis,ps,flags,rng.getrandbits(32),rng.getrandbits(32)))
            payload=b''.join(struct.pack('<15f5I',*p,sx,*axis,sy,*parent,ps,*pa,flags|((i//chunk)<<3),col,glow,pc,pg)
                             for i,(p,sx,sy,col,glow,axis,parent,pa,ps,flags,pc,pg) in enumerate(rows))
            bind(SSBO,buffers[0]);blob=C.create_string_buffer(payload)
            data(SSBO,len(payload),blob,0x88E0);base(SSBO,0,buffers[0])
            normal=tuple(C.c_float(x).value for x in normalize((.2,-.7,.4)))
            uniform(location(compute,b'particleCount'),count)
            uv_offset=8+vertices*8+8
            metadata=b''.join(struct.pack('<8I8f',offsets[0]+first*16,offsets[1]+first*16,
                offsets[2]+first*4,offsets[3]+first*4,first,uv_offset+first*8,0,0,
                *cameras[first//chunk],0.,*normal,0.) for first in range(0,count,chunk))
            bind(SSBO,buffers[2]);blob=C.create_string_buffer(metadata)
            data(SSBO,len(metadata),blob,0x88E0);base(SSBO,2,buffers[2])
            barrier(0x2000)
            dispatch((count+63)//64,1,1);barrier(0x1|0x200)
            bind(SSBO,buffers[1]);result=C.create_string_buffer(len(initial))
            read(SSBO,0,len(initial),result)
            actual=result.raw
            touched=set()
            for i,(pos,sx,sy,col,glow,axis,parent,pa,ps,flags,pc,pg) in enumerate(rows):
                expected=reference(pos,sx,sy,cameras[i//chunk],axis,parent,pa,ps,flags)
                for corner in range(4):
                    vertex=i*4+corner
                    for offset,values in ((offsets[0]+vertex*4,(*expected[corner],0.)),
                                          (offsets[1]+vertex*4,(*normal,0.))):
                        observed=struct.unpack_from('<4f',actual,offset*4)
                        assert all(abs(a-b)<3e-5 for a,b in zip(observed,values)), (count,i,observed,values)
                        touched.update(range(offset*4,(offset+4)*4))
                        checks+=1
                    uv=struct.unpack_from('<2f',actual,(uv_offset+vertex*2)*4)
                    assert uv==(float(corner>=2),float(not(corner&1)))
                    touched.update(range((uv_offset+vertex*2)*4,(uv_offset+vertex*2+2)*4))
                    for offset,value in ((offsets[2]+vertex,pc if flags&1 and corner<2 else col),
                                         (offsets[3]+vertex,pg if flags&1 and corner<2 else glow)):
                        assert struct.unpack_from('<I',actual,offset*4)[0]==value
                        touched.update(range(offset*4,(offset+1)*4))
                        checks+=1
            assert all(actual[i]==initial[i] for i in range(len(actual)) if i not in touched), 'padding/tail overwritten'
            assert get_error()==0
        # Buffer-pool reuse can upload new CPU-owned data after compute writes.
        # The update must replace all generated bytes without stale results.
        replacement=C.create_string_buffer(b'\xa5'*len(initial))
        subdata(SSBO,0,len(initial),replacement)
        read(SSBO,0,len(initial),result)
        assert result.raw==replacement.raw[:-1]
    print(f'PASS: {checks} geometry/attribute checks, all particle modes, padding, dispatch tails and buffer reuse')
finally:
    fn('glDeleteBuffers',None,I,C.POINTER(U))(3,buffers)
    fn('glDeleteProgram',None,U)(compute)
    gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context)
    user.ReleaseDC(window,hdc);user.DestroyWindow(window)
