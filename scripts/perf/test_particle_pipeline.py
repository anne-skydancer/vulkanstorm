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


ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / 'indra/newview/app_settings/shaders/class1/objects'
programs = {name: program([(0x91B9, (SHADERS / ('particle'+name+'C.glsl')).read_text())])
            for name in ('Simulation', 'Order', 'Sort', 'Ranges', 'Suffix', 'RibbonLinks')}
gen = fn('glGenBuffers', None, I, C.POINTER(U))
bind = fn('glBindBuffer', None, U, U)
data = fn('glBufferData', None, U, S, P, U)
base = fn('glBindBufferBase', None, U, U, U)
read = fn('glGetBufferSubData', None, U, S, S, P)
barrier = fn('glMemoryBarrier', None, U)
dispatch = fn('glDispatchCompute', None, U, U, U)
location = fn('glGetUniformLocation', I, U, C.c_char_p)
uniform = fn('glUniform1ui', None, I, U)
uniformf = fn('glUniform1f', None, I, C.c_float)
uniform3 = fn('glUniform3f', None, I, C.c_float, C.c_float, C.c_float)
buffers = (U*8)(); gen(8, buffers)
SSBO = 0x90D2
MASK = 0xffffffff
checks = 0

def upload(slot, payload):
    bind(SSBO, buffers[slot])
    blob = C.create_string_buffer(payload)
    data(SSBO, len(payload), blob, 0x88E8)

def bind_slots(*slots):
    for binding, slot in enumerate(slots):
        base(SSBO, binding, buffers[slot])

def run(name, count, **values):
    p = programs[name]; use(p)
    for key, value in values.items():
        loc = location(p, key.encode())
        assert loc >= 0, (name, key)
        if isinstance(value, tuple): uniform3(loc, *value)
        elif isinstance(value, float): uniformf(loc, value)
        else: uniform(loc, value)
    dispatch((count+63)//64, 1, 1)
    barrier(0x2000)  # next GPU stage consumes shader storage

def download(slot, size):
    # Test-only readback; no CPU inspection is used to drive the GPU stages.
    barrier(0x200)
    bind(SSBO, buffers[slot]); result = C.create_string_buffer(size)
    read(SSBO, 0, size, result)
    return result.raw

def ordered_float(v):
    bits = struct.unpack('<I', struct.pack('<f', 0.0 if v == 0.0 else v))[0]
    return (~bits if bits & 0x80000000 else bits ^ 0x80000000) & MASK

def sort(slot, n):
    bind_slots(slot)
    width = 2
    while width <= n:
        stride = width//2
        while stride:
            run('Sort', n, paddedCount=n, sortWidth=width, sortStride=stride)
            stride //= 2
        width *= 2

def close_enough(a, b):
    return math.isclose(a, b, rel_tol=4e-5, abs_tol=1e-4)

# Independent scalar reference following LLViewerPartGroup::updateParticles,
# LLViewerPartSource{Spiral,Chat,Beam}::updatePart and LLWind::getVelocity.
def reference(p, sources, winds, dt, shift):
    if not p[37]: return
    if p[40] >= len(sources): p[37] = 0; return
    s = sources[p[40]]
    flags, life = p[38], p[7]
    if (s[20] != p[41] or s[21] or flags == 0x80000000 or life <= 0
            or (s[22] == 2 and not s[23])):
        p[37] = 0; return
    old = p[3]; age = old+dt
    if age > life: p[37] = 0; return
    fraction = age/life
    p[:3] = [p[k]+shift[k] for k in range(3)]
    if flags & 16: p[:3] = [s[k]+p[12+k] for k in range(3)]
    if s[22] == 1:
        angle = 2*math.pi*old/life+p[11]
        p[:3] = [s[8]+math.sin(angle), s[9]+math.cos(angle), s[10]-.5+old/life]
    elif s[22] == 2:
        p[:3] = [(1-old/life)*s[8+k]+old/life*s[12+k] for k in range(3)]
    if flags & 8:
        wind = [0., 0., 0.]
        if s[25] and s[19] > 0:
            xy = [max(p[k]-s[16+k], 0.) % s[19] * 16/s[19] for k in range(2)]
            x,y = [int(math.floor(v)) for v in xy]
            dx,dy = xy[0]-x,xy[1]-y
            offset = s[24]+x+16*y
            for k in range(2):
                wind[k] = winds[offset][k]
                if x < 15 and y < 15:
                    wind[k] = (winds[offset][k]*(1-dx)*(1-dy)
                        + winds[offset+1][k]*dx*(1-dy)
                        + winds[offset+16][k]*dy*(1-dx)
                        + winds[offset+17][k]*dx*dy)
                wind[k] *= 2
        p[4:7] = [p[4+k]*(1-.1*dt)+.1*dt*wind[k] for k in range(3)]
    if flags & 64 and life > old:
        remaining = life-old; step = max(0., min(.1, dt/remaining))*5
        p[4:7] = [p[4+k]*(1-step)+step*(s[4+k]-p[k])/remaining for k in range(3)]
    if flags & 128:
        p[4:7] = [s[4+k]-s[k] for k in range(3)]
        p[:3] = [s[k]+fraction*p[4+k] for k in range(3)]
    else:
        p[:3] = [p[k]+dt*p[4+k]+.5*dt*dt*p[8+k] for k in range(3)]
        p[4:7] = [p[4+k]+dt*p[8+k] for k in range(3)]
    if flags & 4 and p[2] < s[2]:
        p[2] = 2*s[2]-p[2]; p[6] *= -.75
    if flags & 16: p[12:15] = [p[k]-s[k] for k in range(3)]
    if flags & 1: p[24:28] = [p[16+k]*(1-fraction)+p[20+k]*fraction for k in range(4)]
    if flags & 2: p[32:34] = [p[28+k]*(1-fraction)+p[30+k]*fraction for k in range(2)]
    p[34] = math.floor(((1-fraction)*p[15]+fraction*p[35])*255+.5)
    p[3] = age

rng = random.Random(27092026)
try:
    winds = [(float(x)/16, float(y)/8) for y in range(16) for x in range(16)]
    upload(2, b''.join(struct.pack('<2f', *v) for v in winds))
    sources = []
    for i in range(6):
        sources.append([2.,3.,1.,0., 10.,-5.,2.,0., 4.,5.,6.,0., 9.,8.,7.,0.,
                        0.,0.,0.,256., 7, int(i==5), (0,1,2,2,0,0)[i], int(i!=3),
                        0, int(i!=4), 0, 0])
    upload(1, b''.join(struct.pack('<20f8I4f', *s, 0., 0., 1., 0.) for s in sources))
    source_template = [s[:] for s in sources]
    for count in (0,1,63,64,65,513,8192):
        sources = [s[:] for s in source_template]
        upload(1, b''.join(struct.pack('<20f8I4f', *s, 0., 0., 1., 0.) for s in sources))
        n = 1 << max(0, (count-1).bit_length()) if count else 1
        rows = []
        for i in range(count):
            p = [0.]*36 + [11,1,0,i%7,(i//12)%6,7,MASK,0]+[0.,0.,1.,0.]
            p[:4] = [float((i%17)*16-8), float((i%19)*16), -2., 0.]
            p[4:8] = [1.,-.5,-1., 4. if i%13 else .125]
            p[8:12] = [.1,.2,-.3, .3]
            p[12:16] = [.5,.25,-.5,.125]
            p[16:20] = [.1,.2,.3,.9]; p[20:24] = [.9,.7,.5,.1]
            p[24:28] = p[16:20]
            p[28:32] = [.25,.5,1.,2.]; p[32:36] = [.25,.5,0.,.625]
            p[38] = (0,1,2,4,8,16,32,64,128,1024,255,0x40000003)[i%12]
            if i%97 == 8: p[41] = 6  # stale source generation
            if i%97 == 9: p[40] = 99 # missing source
            if i%97 == 10: p[38] = 0x80000000 # explicit death
            if i%97 == 11: p[7] = 0 # malformed lifetime
            if i%97 == 12: p[37] = 0 # unused slot
            rows.append(list(struct.unpack('<36f8I4f',struct.pack('<36f8I4f',*p))))
        payload = b''.join(struct.pack('<36f8I4f',*p) for p in rows)
        guard = b'\xcd'*192
        upload(0, payload+guard)
        bind_slots(0,1,2)
        # Multiple GPU-resident steps, including exact expiry, zero dt, rebasing.
        for dt,shift in ((0.,(0.,0.,0.)),(.125,(0.,0.,0.)),(.125,(0.,0.,0.)),
                         (.25,(-256.,16.,2.)),(.5,(0.,0.,0.))):
            if any(shift):
                for source in sources:
                    for start in (0,4,8,12,16):
                        for axis in range(3): source[start+axis] += shift[axis]
                upload(1, b''.join(struct.pack('<20f8I4f', *source, 0., 0., 1., 0.) for source in sources))
            run('Simulation', n, particleCount=count, sourceCount=len(sources),
                deltaTime=dt, originShift=shift)
            for p in rows: reference(p,sources,winds,dt,shift)
        result = download(0,len(payload)+len(guard))
        assert result[len(payload):] == guard, 'simulation dispatch tail overwrote guard'
        actual = [list(struct.unpack_from('<36f8I4f',result,i*192)) for i in range(count)]
        for i,(a,b) in enumerate(zip(actual,rows)):
            assert a[36:] == b[36:], (count,i,a[36:],b[36:])
            for k,(x,y) in enumerate(zip(a[:36],b[:36])):
                assert close_enough(x,y), ('simulation',count,i,k,x,y)
                checks += 1
        # Generate both orderings from GPU state, then sort without readback.
        upload(3,b'\xcd'*(n*32+32)); upload(4,b'\xcd'*(n*32+32))
        bind_slots(0,3,4)
        run('Order',n,particleCount=count,paddedCount=n,cameraPosition=(0.,0.,0.),
            cameraForward=(1.,0.,0.),cellSize=16.)
        for slot in (3,4): sort(slot,n)
        for slot in (3,4):
            result = download(slot,n*32+32)
            assert result[n*32:] == b'\xcd'*32, 'sort overwrote guard'
            entries = list(struct.iter_unpack('<8I',result[:n*32]))
            expected = []
            for i,p in enumerate(actual):
                if not p[37]: continue
                domain = int(bool(p[38] & 0x40000000))
                if slot == 3:
                    key = (domain, *[((math.floor(v/16)&MASK)^0x80000000) for v in p[:3]])
                else: key = (domain,(~ordered_float(p[0]))&MASK,i,0)
                expected.append((*key,i,p[39],p[36],domain))
            expected.sort(key=lambda e:(e[:4],e[4]))
            expected += [(MASK,)*8]*(n-len(expected))
            assert entries == expected, ('order',count,slot)
            checks += n
            # Spatial ranges and strictly adjacent material runs.
            upload(5,b'\xcd'*(n*16+16)); upload(6,bytes(n*16)); upload(7,bytes(n*16))
            if slot == 4:
                bind_slots(slot,6,7)
                run('Ranges',n,paddedCount=n,rangeMode=1)
                src,dst = 6,7; stride=1
                while stride<n:
                    bind_slots(src,dst)
                    run('Suffix',n,paddedCount=n,scanStride=stride)
                    src,dst=dst,src; stride*=2
                bind_slots(slot,5,src)
                run('Ranges',n,paddedCount=n,rangeMode=2)
            else:
                bind_slots(slot,5,6)
                run('Ranges',n,paddedCount=n,rangeMode=0)
            result = download(5,n*16+16)
            assert result[n*16:] == b'\xcd'*16, 'ranges overwrote guard'
            ranges = list(struct.iter_unpack('<4I',result[:n*16]))
            expected_ranges = [(0,0,0,0)]*n
            def group_key(e): return e[:4] if slot==3 else (e[5],e[7])
            i=0
            while i<n and entries[i][4]!=MASK:
                end=i+1
                while end<n and entries[end][4]!=MASK and group_key(entries[i])==group_key(entries[end]): end+=1
                expected_ranges[i]=(i,end-i,entries[i][5],entries[i][7])
                i=end
            assert ranges == expected_ranges, ('ranges',count,slot)
            checks += n
        assert fn('glGetError',U)() == 0
        print('PASS capacity',count,flush=True)
    # Repair dead-parent chains from snapshots, including a full-capacity chain,
    # generation reuse, invalid handles and malformed cycles. No slot reuse is
    # allowed until all repair passes have completed.
    for count in (1,65,8192):
        rows=[]
        for i in range(count):
            row=[0.]*36+[17,int(i==0 or i==count-1),1024,0,0,7,
                         i-1 if i else MASK,17 if i else 0]+[0.,0.,1.,0.]
            rows.append(row)
        if count>64:
            rows[10][42:44]=[9,16] # stale generation; breaks this chain
            rows[20][42:44]=[21,17]; rows[21][42:44]=[20,17] # dead cycle
            rows[30][42:44]=[count+1,17]
            rows[40][37]=1
        upload(0,b''.join(struct.pack('<36f8I4f',*p) for p in rows))
        upload(6,bytes(count*16)); upload(7,bytes(count*16))
        bind_slots(0,6,7)
        run('RibbonLinks',count,particleCount=count,linkMode=0)
        src,dst=7,6
        for _ in range((count-1).bit_length()):
            bind_slots(0,src,dst)
            run('RibbonLinks',count,particleCount=count,linkMode=1)
            src,dst=dst,src
        bind_slots(0,src,dst)
        run('RibbonLinks',count,particleCount=count,linkMode=2)
        actual=list(struct.iter_unpack('<4I',download(dst,count*16)))
        for i,p in enumerate(rows):
            parent,generation=p[42:44]; seen={i}
            while parent<count and rows[parent][36]==generation and parent not in seen:
                if rows[parent][37]: break
                seen.add(parent)
                parent,generation=rows[parent][42:44]
            else: parent,generation=MASK,0
            assert actual[i]==(parent,generation,0,0), ('ribbon',count,i,actual[i],parent,generation)
            checks+=1
        print('PASS ribbon capacity',count,flush=True)
    assert fn('glGetError',U)() == 0
    print('PASS:',checks,'simulation fields / ordering entries / ranges',flush=True)
finally:
    fn('glDeleteBuffers',None,I,C.POINTER(U))(8,buffers)
    for p in programs.values(): fn('glDeleteProgram',None,U)(p)
    gl.wglMakeCurrent(None,None); gl.wglDeleteContext(context)
    user.ReleaseDC(window,hdc); user.DestroyWindow(window)
