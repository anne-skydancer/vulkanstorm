"""Offline GPU benchmark of the alpha OIT capture + resolve shaders, no viewer launch.

Times the per-frame reset, the capture pass and the resolve pass with GL_TIME_ELAPSED
queries for two implementations on synthetic layer stacks at a given resolution:
  * "ppll"  -- per-pixel linked list: shaders from git ref OIT_BENCH_OLD_REF
               (default origin/vkstorm-devel), node pool 8 nodes/pixel, 24 sorted layers
  * "slots" -- image-array A-buffer: shaders from this working tree, 8 slots/pixel
  * "direct" -- no A-buffer: plain in-order source-over blending (the legacy cost floor)
OIT_BENCH_IMPLS selects implementations (default ppll,slots); OIT_BENCH_SCENES a JSON list of
[pattern, layers] pairs.
Each scenario draws L full-screen layers at alpha 0.5 with distinct depths, either over
every pixel ("full") or over 25% of 64x64 blocks ("sparse", a hair/particle clump proxy).

Environment: OIT_BENCH_W, OIT_BENCH_H (default 2560x1440), OIT_BENCH_FRAMES (default 20),
OIT_BENCH_OLD_REF. Pass --opengl PATH to select another GL DLL (Mesa).
"""
import os, statistics, subprocess, sys
from pathlib import Path
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
W = int(os.environ.get('OIT_BENCH_W', 2560)); H = int(os.environ.get('OIT_BENCH_H', 1440))
FRAMES = int(os.environ.get('OIT_BENCH_FRAMES', 20)); WARMUP = 5
OLD_REF = os.environ.get('OIT_BENCH_OLD_REF', 'origin/vkstorm-devel')
NODES_PER_PX = 8; OLD_MAX_LAYERS = 24; SLOTS = 8; META_LAYERS = 6

genbuf=fn('glGenBuffers',None,I,C.POINTER(U));bindbuf=fn('glBindBuffer',None,U,U)
storage=fn('glBufferStorage',None,U,S,P,U);data=fn('glBufferData',None,U,S,P,U)
base=fn('glBindBufferBase',None,U,U,U);sub=fn('glBufferSubData',None,U,S,S,P);barrier=fn('glMemoryBarrier',None,U)
gentex=fn('glGenTextures',None,I,C.POINTER(U));bindtex=fn('glBindTexture',None,U,U)
texstorage=fn('glTexStorage2D',None,U,I,U,I,I);texstorage3=fn('glTexStorage3D',None,U,I,U,I,I,I)
texparam=fn('glTexParameteri',None,U,U,I);image=fn('glBindImageTexture',None,U,U,I,C.c_ubyte,I,U,U)
cleartex=fn('glClearTexImage',None,U,I,U,U,P)
clearsub=fn('glClearTexSubImage',None,U,I,I,I,I,I,I,I,U,U,P)
genfbo=fn('glGenFramebuffers',None,I,C.POINTER(U));bindfbo=fn('glBindFramebuffer',None,U,U)
attachtex=fn('glFramebufferTexture2D',None,U,U,U,U,I)
geterror=fn('glGetError',U);uniform=fn('glUniform1i',None,I,I);uniform1f=fn('glUniform1f',None,I,C.c_float)
loc=fn('glGetUniformLocation',I,U,C.c_char_p);draw=fn('glDrawArrays',None,U,I,I)
genq=fn('glGenQueries',None,I,C.POINTER(U));beginq=fn('glBeginQuery',None,U,U);endq=fn('glEndQuery',None,U)
qresult=fn('glGetQueryObjectui64v',None,U,U,C.POINTER(C.c_uint64))
enable=fn('glEnable',None,U);disable=fn('glDisable',None,U);blendfunc=fn('glBlendFunc',None,U,U)
colormask=fn('glColorMask',None,C.c_ubyte,C.c_ubyte,C.c_ubyte,C.c_ubyte);finish=fn('glFinish',None)
clearcolor=fn('glClearColor',None,C.c_float,C.c_float,C.c_float,C.c_float);clear=fn('glClear',None,U)
TEX2D=0x0DE1;TEX2DARRAY=0x8C1A;R32UI=0x8236;RGBA32UI=0x8D70;RED_INT=0x8D94;UINT=0x1405
TIME_ELAPSED=0x88BF;QUERY_RESULT=0x8866;SSBO=0x90D2;ACB=0x92C0;ALL=0xFFFFFFFF

vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
tex=(U*6)();gentex(6,tex)
depth,color,head,meta,slots=tex[0],tex[1],tex[2],tex[3],tex[4]
bindtex(TEX2D,depth);texstorage(TEX2D,1,0x81A6,W,H);texparam(TEX2D,0x2801,0x2600);texparam(TEX2D,0x2800,0x2600)
one=C.c_float(1.0);cleartex(depth,0,0x1902,0x1406,C.byref(one))   # opaque depth: everything visible
bindtex(TEX2D,color);texstorage(TEX2D,1,0x881A,W,H)                # RGBA16F screen
bindtex(TEX2D,head);texstorage(TEX2D,1,R32UI,W,H)
bindtex(TEX2DARRAY,meta);texstorage3(TEX2DARRAY,1,R32UI,W,H,META_LAYERS)
zero=U(0);cleartex(meta,0,RED_INT,UINT,C.byref(zero))
bindtex(TEX2DARRAY,slots);texstorage3(TEX2DARRAY,1,RGBA32UI,W,H,SLOTS)
bindtex(TEX2D,depth)   # unit 0 sampler2D = opaque depth for both capture and resolve
fbo=U();genfbo(1,C.byref(fbo));bindfbo(0x8D40,fbo);attachtex(0x8D40,0x8CE0,TEX2D,color,0)
assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
fn('glViewport',None,I,I,I,I)(0,0,W,H);disable(0x0B71);enable(0x0BE2)
bufs=(U*2)();genbuf(2,bufs);nodes,counter=bufs[0],bufs[1]
node_cap=W*H*NODES_PER_PX
bindbuf(SSBO,nodes);storage(SSBO,node_cap*16,None,0);bindbuf(SSBO,0)
zero=U(0);bindbuf(ACB,counter);data(ACB,4,C.byref(zero),0x88E8);bindbuf(ACB,0)
assert geterror()==0

def show(ref, path):
    return subprocess.run(['git','show',f'{ref}:{path}'],cwd=root,capture_output=True,text=True,check=True).stdout
def shader_text(impl, path):
    return show(OLD_REF, path) if impl=='ppll' else (root/path).read_text()
def resolve_source(impl):
    src=shader_text(impl,'indra/newview/app_settings/shaders/class1/deferred/alphaOITResolveF.glsl')
    src=src[src.index('#extension'):].replace('/*[EXTRA_CODE_HERE]*/','')
    return '#version 430 core\n'+src
DIRECT_SOURCE='''#version 430 core
uniform float test_depth;
uniform int pattern;
out vec4 color;
void main()
{
    if (pattern == 1 && (((int(gl_FragCoord.x) >> 6) ^ (int(gl_FragCoord.y) >> 6)) & 3) != 0) discard;
    color = vec4(0.8, 0.5, 0.2, 0.5);
}
'''
def capture_source(impl):
    if impl=='direct': return DIRECT_SOURCE
    src=shader_text(impl,'indra/newview/app_settings/shaders/class2/deferred/alphaF.glsl')
    marker='bool oit_append(' if impl=='ppll' else 'void oit_append('
    start=src.index(marker);end=src.index('\n}',start)+2;helper=src[start:end]
    if impl=='ppll':
        decl='''layout(binding=0,r32ui) uniform coherent uimage2D oit_head;
layout(std430,binding=0) buffer OITNodePool {uint oit_nodes[];};
layout(binding=0,offset=0) uniform atomic_uint oit_counter;
uniform int oit_node_cap;
'''
        body='if (oit_append(c, test_depth)) discard; color = c;'
    else:
        decl='''layout(binding=0,r32ui) uniform coherent uimage2DArray oit_meta;
layout(binding=1,rgba32ui) uniform writeonly uimage2DArray oit_slots;
uniform int oit_slot_count;
#define OIT_TAIL_COLOR_SCALE 4096.0
#define OIT_TAIL_ALPHA_SCALE 65536.0
#define OIT_TAIL_LOG_SCALE   65536.0
'''
        body='oit_append(c, test_depth); discard;'
    return f'''#version 430 core
{decl}uniform sampler2D alpha_oit_opaque_depth;
uniform float test_depth;
uniform int pattern;
out vec4 color;
{helper}
void main()
{{
    if (pattern == 1 && (((int(gl_FragCoord.x) >> 6) ^ (int(gl_FragCoord.y) >> 6)) & 3) != 0) discard;
    vec4 c = vec4(0.8, 0.5, 0.2, 0.5);
    {body}
}}
'''
vertex='#version 430 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}'
progs={}
for impl in ('ppll','slots','direct'):
    cap=program([(0x8B31,vertex),(0x8B30,capture_source(impl))])
    res=None if impl=='direct' else program([(0x8B31,vertex),(0x8B30,resolve_source(impl))])
    progs[impl]=(cap,res)
queries=(U*3)();genq(3,queries)

def frame(impl, layers, pattern):
    cap,res=progs[impl]
    clearcolor(0,0,0,1);clear(0x4000)
    # --- reset ---
    beginq(TIME_ELAPSED,queries[0])
    if impl=='direct':
        pass
    elif impl=='ppll':
        barrier(0x200|0x100)   # BUFFER_UPDATE | TEXTURE_UPDATE
        bindbuf(ACB,counter);sub(ACB,0,4,C.byref(zero));bindbuf(ACB,0)
        empty=U(0xFFFFFFFF);cleartex(head,0,RED_INT,UINT,C.byref(empty))
        barrier(0x20|0x1000)   # SHADER_IMAGE_ACCESS | ATOMIC_COUNTER
        image(0,head,0,0,0,0x88BA,R32UI);base(SSBO,0,nodes);base(ACB,0,counter)
    else:
        image(0,0,0,1,0,0x88BA,R32UI);image(1,0,0,1,0,0x88B9,RGBA32UI)
        barrier(0x20|0x100)
        clearsub(meta,0,0,0,0,W,H,1,RED_INT,UINT,C.byref(zero))   # count layer only; the resolve zeroes the tail it read
        barrier(0x20)
        image(0,meta,0,1,0,0x88BA,R32UI);image(1,slots,0,1,0,0x88B9,RGBA32UI)
    endq(TIME_ELAPSED)
    # --- capture: L layers, distinct depths, standard alpha blend for any fall-through ---
    beginq(TIME_ELAPSED,queries[1])
    use(cap);uniform(loc(cap,b'pattern'),pattern)
    if impl!='direct': uniform(loc(cap,b'alpha_oit_opaque_depth'),0)
    if impl=='ppll': uniform(loc(cap,b'oit_node_cap'),node_cap)
    elif impl=='slots': uniform(loc(cap,b'oit_slot_count'),SLOTS)
    blendfunc(0x0302,0x0303);colormask(1,1,1,1)
    for i in range(layers):
        uniform1f(loc(cap,b'test_depth'),0.05+0.9*((i*7919)%layers)/max(layers,1))
        draw(4,0,3)
    barrier(0x20|0x2000|0x1000)   # IMAGE | SHADER_STORAGE | ATOMIC_COUNTER
    endq(TIME_ELAPSED)
    # --- resolve: premultiplied over, alpha channel masked ---
    beginq(TIME_ELAPSED,queries[2])
    if impl=='direct':
        endq(TIME_ELAPSED);finish()
        out=[]
        for q in queries:
            v=C.c_uint64();qresult(q,QUERY_RESULT,C.byref(v));out.append(v.value/1e6)
        assert geterror()==0
        return out
    use(res);uniform(loc(res,b'depthMap'),0)
    if impl=='ppll':
        uniform(loc(res,b'oit_max_pixel_layers'),OLD_MAX_LAYERS)
        image(0,head,0,0,0,0x88B8,R32UI);base(SSBO,0,nodes)
    else:
        uniform(loc(res,b'oit_slot_count'),SLOTS)
        image(0,meta,0,1,0,0x88B8,R32UI);image(1,slots,0,1,0,0x88B8,RGBA32UI);image(2,meta,0,1,0,0x88B9,R32UI)
    blendfunc(1,0x0303);colormask(1,1,1,0)
    draw(4,0,3)
    endq(TIME_ELAPSED)
    finish()
    out=[]
    for q in queries:
        v=C.c_uint64();qresult(q,QUERY_RESULT,C.byref(v));out.append(v.value/1e6)
    assert geterror()==0
    return out

import json
scenarios=json.loads(os.environ['OIT_BENCH_SCENES']) if 'OIT_BENCH_SCENES' in os.environ else [('full',1),('full',4),('full',8),('full',16),('full',32),('sparse',8),('sparse',24),('sparse',48)]
impls=os.environ.get('OIT_BENCH_IMPLS','ppll,slots').split(',')
print(f'{W}x{H}, {FRAMES} frames median after {WARMUP} warmup; ms per frame (reset / capture / resolve / total)')
print(f'ppll: {NODES_PER_PX} nodes/px pool ({node_cap*16>>20} MiB), {OLD_MAX_LAYERS} sorted layers; slots: {SLOTS} slots/px ({W*H*16*SLOTS>>20} MiB) + meta ({W*H*4*META_LAYERS>>20} MiB)')
print(f'{"scene":<12}{"impl":<7}{"reset":>8}{"capture":>9}{"resolve":>9}{"total":>8}  note')
for pattern_name,layers in scenarios:
    pattern=1 if pattern_name=='sparse' else 0
    row={}
    for impl in impls:
        samples=[]
        for f in range(WARMUP+FRAMES):
            t=frame(impl,layers,pattern)
            if f>=WARMUP: samples.append(t)
        med=[statistics.median(s[i] for s in samples) for i in range(3)]
        row[impl]=med
        note=''
        if impl=='ppll' and pattern==0 and layers>NODES_PER_PX: note=f'pool overflow: {layers-NODES_PER_PX} of {layers} layers fall through to in-place blend'
        if impl=='ppll' and layers>OLD_MAX_LAYERS: note+=' + weighted tail past 24'
        if impl=='slots' and layers>SLOTS: note=f'{layers-SLOTS} of {layers} layers in the tail'
        print(f'{pattern_name+" L="+str(layers):<12}{impl:<7}{med[0]:>8.3f}{med[1]:>9.3f}{med[2]:>9.3f}{sum(med):>8.3f}  {note}')
    if 'ppll' in row and 'slots' in row:
        a,b=sum(row["ppll"]),sum(row["slots"]);print(f'{"":<19}slots/ppll total = {b/a:.2f}x')
gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
