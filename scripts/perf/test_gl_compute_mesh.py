"""Windows offscreen GPU test for the actual mesh command shader (no viewer login).
Run with --opengl PATH to test the packaged Mesa opengl32.dll instead of native GL.
Validates resident LOD command generation and direct/indirect rendered pixels.
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
window=user.CreateWindowExW(0,'STATIC','Compute mesh validation',0,0,0,32,32,None,None,None,None)
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
shader_path=Path(__file__).resolve().parents[2]/'indra/newview/app_settings/shaders/class1/objects/meshLODC.glsl'
compute=program([(0x91B9,shader_path.read_text())])
gen=fn('glGenBuffers',None,I,C.POINTER(U));bind=fn('glBindBuffer',None,U,U)
data=fn('glBufferData',None,U,S,P,U);base=fn('glBindBufferBase',None,U,U,U)
read=fn('glGetBufferSubData',None,U,S,S,P)
barrier=fn('glMemoryBarrier',None,U);dispatch=fn('glDispatchCompute',None,U,U,U)
uniform=fn('glUniform1ui',None,I,U);location=fn('glGetUniformLocation',I,U,C.c_char_p)
uniform3=fn('glUniform3f',None,I,C.c_float,C.c_float,C.c_float)
uniform4=fn('glUniform4f',None,I,C.c_float,C.c_float,C.c_float,C.c_float)
get_error=fn('glGetError',U)
buffers=(U*5)();gen(5,buffers);SSBO=0x90D2
for binding in (2,3,4):
    bind(SSBO,buffers[binding]);data(SSBO,4*10000,None,0x88E4);base(SSBO,binding,buffers[binding])

def f32(v): return C.c_float(v).value
def rounded(v): return f32(math.floor(f32(f32(v*100.)+.5))*f32(.01))
def reference(center_radius,camera,policy):
    x,y,z,r=map(f32,center_radius); scale,ramp,factor,dynamic=map(f32,policy)
    if not all(math.isfinite(v) for v in (x,y,z,r,scale,ramp,factor,dynamic)): return 3
    delta=[f32(p-c) for p,c in zip((x,y,z),camera)]
    d=f32(math.sqrt(f32(f32(f32(delta[0]*delta[0])+f32(delta[1]*delta[1]))+f32(delta[2]*delta[2]))))
    d=f32(rounded(d)*scale)
    if d<ramp and ramp>0:
        d=f32(d*f32(1/ramp));d=f32(d*d);d=f32(d*ramp)
    d=rounded(f32(d*f32(math.pi/3)))
    r=rounded(r)
    if dynamic:
        if not d: return 3
        angle=f32(f32(factor*r)/d)
        value=rounded(angle)
        return next((i for i,t in enumerate((.03,.06,.24)) if value<=f32(t)),3)
    return int(max(0,min(3,f32(f32(f32(math.sqrt(r))*factor)*4))))

rng=random.Random(914)
cases=[(rng.uniform(.01,500),0.,0.,rng.uniform(.01,10)) for _ in range(8192)]
# Exact and adjacent policy boundaries; axis aligned distances avoid a CPU/GPU
# square-root reduction-order ambiguity in tests of the policy itself.
for d in (1.,5.,20.,100.):
    for threshold in (.025,.035,.055,.065,.235,.245):
        for offset in (-.0001,0,.0001): cases.append((d,0.,0.,d*math.pi/3*(threshold+offset)))
cases += [(0.,0.,0.,1.),(1.,0.,0.,0.),(float('nan'),0.,0.,1.)]
ranges=[(3,0,1,0),(6,3,1,0),(9,9,1,0),(12,18,1,0)]
payload=b''.join(struct.pack('<4f20I',*row,*(n for r in ranges for n in r),0,0,0,0) for row in cases)
blob=C.create_string_buffer(payload);bind(SSBO,buffers[0]);data(SSBO,len(payload),blob,0x88E4);base(SSBO,0,buffers[0])
initial=b'\xcd'*(20*(len(cases)+1));blob=C.create_string_buffer(initial)
bind(SSBO,buffers[1]);data(SSBO,len(initial),blob,0x88E0);base(SSBO,1,buffers[1])
use(compute);uniform(location(compute,b'phase'),1);uniform(location(compute,b'candidateCount'),len(cases))
checks=0
for camera,policy in (((0,0,0),(1,2,1,1)),((2,0,0),(1,4,2,1)),((0,0,0),(.5,8,4,1)),((0,0,0),(1,2,.5,0))):
    uniform3(location(compute,b'cameraOrigin'),*camera);uniform4(location(compute,b'policy'),*policy)
    dispatch((len(cases)+63)//64,1,1);barrier(0x200|0x40)
    result=C.create_string_buffer(len(initial));read(SSBO,0,len(initial),result)
    assert result.raw[-20:]==b'\xcd'*20,'dispatch tail overwritten'
    for i,row in enumerate(cases):
        lod=reference(row,camera,policy);r=ranges[lod]
        actual=struct.unpack_from('<IIIiI',result.raw,20*i)
        assert actual==(r[0],r[2],r[1],0,0),(i,row,camera,policy,lod,actual)
    checks+=len(cases)
assert get_error()==0
# GPU elapsed time for a stable resident table, without uploading object data or
# reading decisions back. Warm up first; report timings rather than assert a
# hardware-specific performance threshold.
uniform4(location(compute,b'policy'),1,2,1,1)
query=(U*2)();fn('glGenQueries',None,I,C.POINTER(U))(2,query)
stamp=fn('glQueryCounter',None,U,U)
get_stamp=fn('glGetQueryObjectui64v',None,U,U,C.POINTER(C.c_uint64))
for _ in range(16):
    barrier(0x2000);dispatch((len(cases)+63)//64,1,1);barrier(0x40)
stamp(query[0],0x8E28)
for _ in range(128):
    barrier(0x2000);dispatch((len(cases)+63)//64,1,1);barrier(0x40)
stamp(query[1],0x8E28)
a=C.c_uint64();b=C.c_uint64();get_stamp(query[0],0x8866,C.byref(a));get_stamp(query[1],0x8866,C.byref(b))
print(f'GPU resident dispatch: {(b.value-a.value)/128/1000:.3f} us per {len(cases)} slots (128 repetitions)',flush=True)
# Exercise resident command consumption as real draws: distinct triangles for
# lowest, low, medium and high. Move only the camera uniform; reuse geometry and
# metadata, with no decision readback before the draw.
render=program([(0x8B31,'#version 430\nlayout(location=0) in vec2 p; void main(){gl_Position=vec4(p,0,1);}'),(0x8B30,'#version 430\nout vec4 color; void main(){color=vec4(1,0,0,1);}')])
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
mesh=(U*2)();gen(2,mesh)
vertices=[]
for size in (.2,.4,.6,.8): vertices.extend((-size,-size,size,-size,0,size))
blob=C.create_string_buffer(struct.pack('<24f',*vertices));bind(0x8892,mesh[0]);data(0x8892,96,blob,0x88E4)
fn('glVertexAttribPointer',None,U,I,U,C.c_ubyte,I,P)(0,2,0x1406,0,8,None);fn('glEnableVertexAttribArray',None,U)(0)
texture=U();fn('glGenTextures',None,I,C.POINTER(U))(1,C.byref(texture));fn('glBindTexture',None,U,U)(0x0DE1,texture)
fn('glTexImage2D',None,U,I,I,I,I,I,U,U,P)(0x0DE1,0,0x8058,32,32,0,0x1908,0x1401,None)
fbo=U();fn('glGenFramebuffers',None,I,C.POINTER(U))(1,C.byref(fbo));fn('glBindFramebuffer',None,U,U)(0x8D40,fbo)
fn('glFramebufferTexture2D',None,U,U,U,U,I)(0x8D40,0x8CE0,0x0DE1,texture,0)
assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
fn('glViewport',None,I,I,I,I)(0,0,32,32)
clear=fn('glClear',None,U);fn('glClearColor',None,C.c_float,C.c_float,C.c_float,C.c_float)(0,0,0,1)
read_pixels=fn('glReadPixels',None,I,I,I,I,U,U,P)
def pixels():
    image=C.create_string_buffer(32*32*4);read_pixels(0,0,32,32,0x1908,0x1401,image);return image.raw
for index_type,fmt,width in ((0x1403,'<12H',2),(0x1405,'<12I',4)):
    blob=C.create_string_buffer(struct.pack(fmt,*range(12)));bind(0x8893,mesh[1]);data(0x8893,len(blob)-1,blob,0x88E4)
    # Slot zero is a dead allocation. Slot one has all four resident ranges.
    row=struct.pack('<4f20I',0,0,0,1,*(n for lod in range(4) for n in (3,lod*3,1,0)),0,0,0,0)
    blob=C.create_string_buffer(bytes(96)+row);bind(SSBO,buffers[0]);data(SSBO,192,blob,0x88E4)
    for wanted,distance in ((0,100),(1,20),(2,8),(3,2)):
        use(compute);uniform(location(compute,b'candidateCount'),2)
        uniform3(location(compute,b'cameraOrigin'),distance,0,0);uniform4(location(compute,b'policy'),1,2,1,1)
        barrier(0x2000);dispatch(1,1,1);barrier(0x40)
        use(render);clear(0x4000);bind(0x8F3F,buffers[1])
        fn('glMultiDrawElementsIndirect',None,U,U,P,I,I)(4,index_type,C.c_void_p(20),1,20)
        actual=pixels()
        clear(0x4000);fn('glDrawElements',None,U,I,U,P)(4,3,index_type,C.c_void_p(wanted*3*width))
        assert actual==pixels(),('LOD draw pixel mismatch',wanted,index_type)
        clear(0x4000);fn('glMultiDrawElementsIndirect',None,U,U,P,I,I)(4,index_type,None,1,20)
        assert pixels()[(16*32+16)*4]==0,'dead slot rendered'
assert get_error()==0
# Recompile without replacing geometry/metadata, then exercise an aliased missing
# asset level and an intentionally empty lowest range.
compute=program([(0x91B9,shader_path.read_text())])
use(compute);uniform(location(compute,b'phase'),1)
remapped=((0,0,0,0),(3,6,1,0),(3,6,1,0),(3,9,1,0))
blob=C.create_string_buffer(struct.pack('<4f20I',0,0,0,1,*(n for r in remapped for n in r),0,0,0,0))
bind(SSBO,buffers[0]);data(SSBO,96,blob,0x88E4)
uniform(location(compute,b'candidateCount'),1);uniform4(location(compute,b'policy'),1,2,1,1)
for lod,distance in ((0,100),(1,20),(2,8),(3,2)):
    uniform3(location(compute,b'cameraOrigin'),distance,0,0)
    barrier(0x2000);dispatch(1,1,1);barrier(0x200)
    bind(SSBO,buffers[1]);result=C.create_string_buffer(20);read(SSBO,0,20,result)
    r=remapped[lod]
    assert struct.unpack('<IIIiI',result.raw)==(r[0],r[2],r[1],0,0),'remapped/empty LOD mismatch'
assert get_error()==0
print(f'PASS: {checks} resident LOD commands; camera-only updates; four LOD pixel comparisons for both index widths; dead slot; tail guard; reload; missing/empty LOD ranges',flush=True)
# Rigged objects reference shared animated avatar bounds, not their own center.
# Include normal/control avatars, multiple attachments, dead owners and bad IDs.
def upload(binding, payload):
    blob=C.create_string_buffer(payload)
    bind(SSBO,buffers[binding]);data(SSBO,len(payload),blob,0x88E4)
    base(SSBO,binding,buffers[binding])

def mesh_row(owner, ranges):
    return struct.pack('<4f20I',999,999,999,.01,*(n for r in ranges for n in r),owner,0,0,0)

rigged_ranges=[(3,lod*3,1,0) for lod in range(4)]
owner_ids=[1,1,2,2,3,4]
upload(0,b''.join(mesh_row(owner,rigged_ranges) for owner in owner_ids))
upload(1,bytes(20*(len(owner_ids)+1)))
upload(3,bytes(16))
rigged_checks=0
for radius in (0.,.5,1.,2.,4.):
    for distance in (2.,8.,20.,100.):
        upload(2,struct.pack('<36f',
            0,0,0,1, 0,0,0,1, radius,0,0,0,
            0,0,0,.5, 0,0,0,1, radius,0,0,0,
            0,0,0,1, 0,0,0,0, radius,0,0,0))
        use(compute);uniform(location(compute,b'avatarCount'),3)
        uniform(location(compute,b'candidateCount'),len(owner_ids))
        uniform3(location(compute,b'cameraOrigin'),distance,0,0)
        uniform4(location(compute,b'policy'),1,2,1,1)
        uniform(location(compute,b'phase'),0);barrier(0x2000);dispatch(1,1,1);barrier(0x2000)
        uniform(location(compute,b'phase'),1);dispatch(1,1,1);barrier(0x200|0x40)
        bind(SSBO,buffers[1]);result=C.create_string_buffer(20*(len(owner_ids)+1));read(SSBO,0,len(result),result)
        assert result.raw[-20:]==bytes(20),'rigged tail overwritten'
        for i,owner in enumerate(owner_ids):
            lod=reference((0,0,0,radius*(.5 if owner==2 else 1)),(distance,0,0),(1,2,1,1)) if owner<3 and radius>0 else 3
            assert struct.unpack_from('<IIIiI',result.raw,i*20)==(3,1,lod*3,0,0),(owner,radius,distance,lod)
            rigged_checks+=1

# Partial residency must draw immediately, while demand still reports the
# camera's desired High level. Publishing finer mappings changes only metadata.
for highest in range(4):
    partial=[rigged_ranges[min(lod,highest)] for lod in range(4)]
    upload(0,mesh_row(1,partial))
    upload(2,struct.pack('<12f',0,0,0,1,0,0,0,1,1,0,0,0))
    use(compute);uniform(location(compute,b'avatarCount'),1);uniform(location(compute,b'candidateCount'),1)
    uniform3(location(compute,b'cameraOrigin'),2,0,0)
    uniform(location(compute,b'phase'),0);barrier(0x2000);dispatch(1,1,1);barrier(0x2000)
    uniform(location(compute,b'phase'),1);dispatch(1,1,1);barrier(0x200|0x40)
    bind(SSBO,buffers[1]);command=C.create_string_buffer(20);read(SSBO,0,20,command)
    assert struct.unpack('<IIIiI',command.raw)==(3,1,highest*3,0,0),'partial residency did not select best ready level'
    bind(SSBO,buffers[4]);demand=C.create_string_buffer(4);read(SSBO,0,4,demand)
    assert struct.unpack('<I',demand.raw)==(3,),'streaming demand was clamped to available geometry'
print('PASS: progressive Lowest/Low/Medium/High publication with independent GPU High demand',flush=True)

# Execute the viewer's actual skinning function with resident indirect ranges.
# This checks changing joint palettes, different weights, and all four levels;
# it does not substitute for an in-world material/attachment integration test.
skin=(shader_path.parents[1]/'avatar/objectSkinV.glsl').read_text()
vertex='#version 430\n#define MAX_JOINTS_PER_MESH_OBJECT 2\n'+skin+'\nlayout(location=0) in vec2 p; void main(){gl_Position=getObjectSkinnedTransform()*vec4(p,0,1);}'
skinned=program([(0x8B31,vertex),(0x8B30,'#version 430\nout vec4 color; void main(){color=vec4(1,0,0,.5);}')])
weight_location=fn('glGetAttribLocation',I,U,C.c_char_p)(skinned,b'weight4')
assert weight_location>=0
weight_buffer=U();gen(1,C.byref(weight_buffer))
weights=[]
for i in range(12): weights.extend((.25,1.75,0,0) if i%3 else (.75,1.25,0,0))
blob=C.create_string_buffer(struct.pack('<48f',*weights));bind(0x8892,weight_buffer);data(0x8892,192,blob,0x88E4)
fn('glVertexAttribPointer',None,U,I,U,C.c_ubyte,I,P)(weight_location,4,0x1406,0,16,None)
fn('glEnableVertexAttribArray',None,U)(weight_location)
palette=fn('glUniformMatrix3x4fv',None,I,I,C.c_ubyte,P)
upload(0,mesh_row(1,rigged_ranges));upload(2,struct.pack('<12f',0,0,0,1,0,0,0,1,1,0,0,0))
# Reuse the existing uint32 index buffer and position attributes.
depth_texture=U();fn('glGenTextures',None,I,C.POINTER(U))(1,C.byref(depth_texture))
fn('glBindTexture',None,U,U)(0x0DE1,depth_texture)
fn('glTexImage2D',None,U,I,I,I,I,I,U,U,P)(0x0DE1,0,0x81A6,32,32,0,0x1902,0x1406,None)
fn('glFramebufferTexture2D',None,U,U,U,U,I)(0x8D40,0x8D00,0x0DE1,depth_texture,0)
assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
fn('glEnable',None,U)(0x0B71) # depth test
fn('glEnable',None,U)(0x0BE2) # alpha blend
fn('glBlendFunc',None,U,U)(0x0302,0x0303)
def depth_pixels():
    image=C.create_string_buffer(32*32*4);read_pixels(0,0,32,32,0x1902,0x1406,image);return image.raw
previous_pose=None
for shift in (-.1,.1):
    use(skinned)
    joints=(C.c_float*24)(1,0,0,shift,0,1,0,0,0,0,1,0, 1,0,0,-shift,0,1,0,shift,0,0,1,0)
    palette(location(skinned,b'matrixPalette[0]'),2,0,joints)
    for wanted,distance in ((0,100),(1,20),(2,8),(3,2)):
        use(compute);uniform(location(compute,b'avatarCount'),1);uniform(location(compute,b'candidateCount'),1)
        uniform3(location(compute,b'cameraOrigin'),distance,0,0)
        uniform(location(compute,b'phase'),0);barrier(0x2000);dispatch(1,1,1);barrier(0x2000)
        uniform(location(compute,b'phase'),1);dispatch(1,1,1);barrier(0x40)
        use(skinned);clear(0x4100);bind(0x8F3F,buffers[1])
        fn('glMultiDrawElementsIndirect',None,U,U,P,I,I)(4,0x1405,None,1,20)
        actual=pixels();actual_depth=depth_pixels()
        clear(0x4100);fn('glDrawElements',None,U,I,U,P)(4,3,0x1405,C.c_void_p(wanted*12))
        assert actual==pixels(),('skinned LOD pixel mismatch',wanted,shift)
        assert actual_depth==depth_pixels(),('skinned LOD depth mismatch',wanted,shift)
        assert any(actual[i] for i in range(0,len(actual),4)),'skinned draw empty'
    if previous_pose is not None: assert previous_pose!=actual,'joint palette had no effect'
    previous_pose=actual
assert get_error()==0
print(f'PASS: {rigged_checks} shared-avatar commands; animated bounds; normal/control radius; invalid owners; 8 production-skinning indirect/direct blended color and depth comparisons',flush=True)

# Grow a packed buffer around existing Lowest/High geometry. The old High
# range moves; preserve its attributes by GPU copy and rebase its indices.
copy_bind=fn('glCopyBufferSubData',None,U,U,S,S,S)
sub_data=fn('glBufferSubData',None,U,S,S,P)
copied=(U*3)();gen(3,copied)
old_vertices=vertices[:6]+vertices[18:24]
blob=C.create_string_buffer(struct.pack('<12f',*old_vertices))
bind(0x8F36,copied[0]);data(0x8F36,48,blob,0x88E4)
bind(0x8F37,copied[1]);data(0x8F37,96,None,0x88E4)
copy_bind(0x8F36,0x8F37,0,0,24);copy_bind(0x8F36,0x8F37,24,72,24)
blob=C.create_string_buffer(struct.pack('<12f',*vertices[6:18]));sub_data(0x8F37,24,48,blob)
rebased=list(range(3))+list(range(3,9))+[i-3+9 for i in (3,4,5)]
blob=C.create_string_buffer(struct.pack('<12H',*rebased))
bind(0x8893,copied[2]);data(0x8893,24,blob,0x88E4)
upload(0,struct.pack('<4f20I',0,0,0,1,*(n for r in rigged_ranges for n in r),0,0,0,0))
for wanted,distance in ((0,100),(1,20),(2,8),(3,2)):
    use(compute);uniform(location(compute,b'candidateCount'),1);uniform(location(compute,b'phase'),1)
    uniform3(location(compute,b'cameraOrigin'),distance,0,0);barrier(0x2000);dispatch(1,1,1);barrier(0x40)
    bind(0x8892,copied[1]);fn('glVertexAttribPointer',None,U,I,U,C.c_ubyte,I,P)(0,2,0x1406,0,8,None)
    use(render);clear(0x4100);bind(0x8F3F,buffers[1])
    fn('glMultiDrawElementsIndirect',None,U,U,P,I,I)(4,0x1403,None,1,20)
    actual=pixels();actual_depth=depth_pixels()
    bind(0x8892,mesh[0]);fn('glVertexAttribPointer',None,U,I,U,C.c_ubyte,I,P)(0,2,0x1406,0,8,None)
    clear(0x4100);fn('glDrawElements',None,U,I,U,P)(4,3,0x1403,C.c_void_p(wanted*6))
    assert actual==pixels() and actual_depth==depth_pixels(),('copied/refined range mismatch',wanted)
assert get_error()==0
print('PASS: four GPU-copied/rebased refinement ranges match original color and depth',flush=True)

gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
