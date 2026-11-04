"""Offline GPU benchmark of the alpha OIT capture + resolve shaders, no viewer launch.

Times the per-frame reset, the capture pass and the resolve pass with GL_TIME_ELAPSED
queries for these implementations on synthetic layer stacks at a given resolution:
  * "ppll"  -- the original per-pixel linked list: shaders from git ref OIT_BENCH_OLD_REF
               (default ed92bed30d, the last devel commit with the linked list), 8 nodes/pixel,
               24 sorted layers
  * "slots" -- PPLL as the viewer has it since PR #283, a fixed-slot image array: shaders from
               this working tree, 8 slots/pixel, 8-byte RG32UI nodes (alphaOITPackF.glsl linked
               as its own fragment shader object), order-independent tail past the last slot
  * "slots16" -- the same with the 16-byte RGBA32UI node: shaders from git ref
               OIT_BENCH_SLOTS16_REF (default 6ceaf3196c, the last devel commit with that node)
  * "direct" -- no OIT: plain in-order source-over blending (the legacy cost floor; draw order
               is not depth order, so its image is not a reference and has no RMSE)
OIT_BENCH_IMPLS selects implementations (default ppll,slots); OIT_BENCH_SCENES a JSON list of
[pattern, layers] pairs.
Each scenario draws L full-screen layers with distinct, unsorted depths; the patterns
(oit_bench_scenes.py) are "full" (every pixel, alpha 0.5), "sparse" (25% of 64x64 blocks, a
hair/particle clump proxy), "bright" (bright alpha 0.22-0.37 layers over black), "foliage"
(thin 40%-coverage stripes, alpha 0.95 core / 0.15 edge, past 8 slots from 32 layers) and
"opaque" (alpha 1). All implementations shade the same scene_color() fragment function.

Correctness: after timing, one more frame per implementation is read back over a window of
OIT_BENCH_RMSE_WIN (default 256) square pixels and compared with the exact back-to-front
composite of the same fragments on the CPU: RMSE and max absolute error in 8-bit units of the
linear colour, over the alpha-covered pixels. Checks print ok/FAIL; any FAIL exits 1.

Environment: OIT_BENCH_W, OIT_BENCH_H (default 2560x1440), OIT_BENCH_FRAMES (default 20),
OIT_BENCH_OLD_REF. Pass --opengl PATH to select another GL DLL (Mesa).
"""
import json, os, statistics, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import oit_bench_scenes as scenes
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
W = int(os.environ.get('OIT_BENCH_W', 2560)); H = int(os.environ.get('OIT_BENCH_H', 1440))
FRAMES = int(os.environ.get('OIT_BENCH_FRAMES', 20)); WARMUP = 5
# Last devel commit with the linked-list PPLL (before #283); an immutable baseline.
OLD_REF = os.environ.get('OIT_BENCH_OLD_REF', 'ed92bed30d')
# Last devel commit with the 16-byte RGBA32UI slot node; the "slots16" baseline.
SLOTS16_REF = os.environ.get('OIT_BENCH_SLOTS16_REF', '6ceaf3196c')
IMPLS = os.environ.get('OIT_BENCH_IMPLS', 'ppll,slots').split(',')
NODES_PER_PX = 8; OLD_MAX_LAYERS = 24; SLOTS = 8; META_LAYERS = 7
RMSE_WIN = min(int(os.environ.get('OIT_BENCH_RMSE_WIN', 256)), W, H)
WIN_X = min(512, W - RMSE_WIN); WIN_Y = min(256, H - RMSE_WIN)   # covers both sparse block phases

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
F=C.c_float;uniform4f=fn('glUniform4f',None,I,F,F,F,F);uniform4i=fn('glUniform4i',None,I,I,I,I,I)
readpixels=fn('glReadPixels',None,I,I,I,I,U,U,P)
loc=fn('glGetUniformLocation',I,U,C.c_char_p);draw=fn('glDrawArrays',None,U,I,I)
genq=fn('glGenQueries',None,I,C.POINTER(U));beginq=fn('glBeginQuery',None,U,U);endq=fn('glEndQuery',None,U)
qresult=fn('glGetQueryObjectui64v',None,U,U,C.POINTER(C.c_uint64))
enable=fn('glEnable',None,U);disable=fn('glDisable',None,U);blendfunc=fn('glBlendFunc',None,U,U)
colormask=fn('glColorMask',None,C.c_ubyte,C.c_ubyte,C.c_ubyte,C.c_ubyte);finish=fn('glFinish',None)
clearcolor=fn('glClearColor',None,C.c_float,C.c_float,C.c_float,C.c_float);clear=fn('glClear',None,U)
TEX2D=0x0DE1;TEX2DARRAY=0x8C1A;R32UI=0x8236;RG32UI=0x823C;RGBA32UI=0x8D70;RED_INT=0x8D94;UINT=0x1405
TIME_ELAPSED=0x88BF;QUERY_RESULT=0x8866;SSBO=0x90D2;ACB=0x92C0;ALL=0xFFFFFFFF

vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
tex=(U*6)();gentex(6,tex)
depth,color,head,meta,slots,slots16=tex[0],tex[1],tex[2],tex[3],tex[4],tex[5]
bindtex(TEX2D,depth);texstorage(TEX2D,1,0x81A6,W,H);texparam(TEX2D,0x2801,0x2600);texparam(TEX2D,0x2800,0x2600)
one=C.c_float(1.0);cleartex(depth,0,0x1902,0x1406,C.byref(one))   # opaque depth: everything visible
bindtex(TEX2D,color);texstorage(TEX2D,1,0x881A,W,H)                # RGBA16F screen
bindtex(TEX2D,head);texstorage(TEX2D,1,R32UI,W,H)
bindtex(TEX2DARRAY,meta);texstorage3(TEX2DARRAY,1,R32UI,W,H,META_LAYERS)
zero=U(0);cleartex(meta,0,RED_INT,UINT,C.byref(zero))
# Slot arrays only for the selected implementations: node size and format per implementation.
SLOT_FORMAT={'slots':(slots,RG32UI,8),'slots16':(slots16,RGBA32UI,16)}
for impl,(name,fmt,_) in SLOT_FORMAT.items():
    if impl in IMPLS: bindtex(TEX2DARRAY,name);texstorage3(TEX2DARRAY,1,fmt,W,H,SLOTS)
bindtex(TEX2D,depth)   # unit 0 sampler2D = opaque depth for both capture and resolve
fbo=U();genfbo(1,C.byref(fbo));bindfbo(0x8D40,fbo);attachtex(0x8D40,0x8CE0,TEX2D,color,0)
assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
fn('glViewport',None,I,I,I,I)(0,0,W,H);disable(0x0B71);enable(0x0BE2)
bufs=(U*2)();genbuf(2,bufs);nodes,counter=bufs[0],bufs[1]
node_cap=W*H*NODES_PER_PX
if 'ppll' in IMPLS: bindbuf(SSBO,nodes);storage(SSBO,node_cap*16,None,0);bindbuf(SSBO,0)
zero=U(0);bindbuf(ACB,counter);data(ACB,4,C.byref(zero),0x88E8);bindbuf(ACB,0)
assert geterror()==0

def show(ref, path):
    return subprocess.run(['git','show',f'{ref}:{path}'],cwd=root,capture_output=True,text=True,check=True).stdout
def shader_text(impl, path):
    if impl=='ppll': return show(OLD_REF, path)
    if impl=='slots16': return show(SLOTS16_REF, path)
    return (root/path).read_text()
def pack_source():
    # The 8-byte node encoding, linked as its own fragment shader object as in the viewer.
    src=(root/'indra/newview/app_settings/shaders/class1/deferred/alphaOITPackF.glsl').read_text()
    return '#version 430 core\n'+src[src.index('*/')+2:]
def resolve_source(impl):
    src=shader_text(impl,'indra/newview/app_settings/shaders/class1/deferred/alphaOITResolveF.glsl')
    src=src[src.index('#extension'):].replace('/*[EXTRA_CODE_HERE]*/','')
    return '#version 430 core\n'+src
DIRECT_SOURCE='''#version 430 core
uniform float test_depth;
'''+scenes.SCENE_GLSL+'''
out vec4 color;
void main()
{
    color = scene_color();
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
        node='rg32ui' if impl=='slots' else 'rgba32ui'
        decl=f'''layout(binding=0,r32ui) uniform coherent uimage2DArray oit_meta;
layout(binding=1,{node}) uniform writeonly uimage2DArray oit_slots;
uint oit_pack_rgb(vec3 c);
uint oit_pack_depth_alpha(float z, float a);
uniform int oit_slot_count;
#define OIT_TAIL_COLOR_SCALE 4096.0
#define OIT_TAIL_ALPHA_SCALE 65536.0
#define OIT_TAIL_LOG_SCALE   65536.0
'''
        body='oit_append(c, test_depth); discard;'
    return f'''#version 430 core
{decl}uniform sampler2D alpha_oit_opaque_depth;
uniform float test_depth;
{scenes.SCENE_GLSL}
out vec4 color;
{helper}
void main()
{{
    vec4 c = scene_color();
    {body}
}}
'''
vertex='#version 430 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}'
progs={}
for impl in IMPLS:   # only the selected implementations; 'ppll' and 'slots16' need their git refs
    extra=[(0x8B30,pack_source())] if impl=='slots' else []
    cap=program([(0x8B31,vertex),(0x8B30,capture_source(impl))]+extra)
    res=None if impl=='direct' else program([(0x8B31,vertex),(0x8B30,resolve_source(impl))]+extra)
    progs[impl]=(cap,res)
queries=(U*3)();genq(3,queries)
locations={}
def uloc(prog,name):
    key=(prog,name)
    if key not in locations: locations[key]=loc(prog,name.encode())
    return locations[key]

def draw_layers(prog, name, layers):
    """One full-screen triangle per layer, in draw order, with the scene uniforms."""
    uniform(uloc(prog,'pattern'),scenes.PATTERNS[name])
    for layer in layers:
        uniform4f(uloc(prog,'layer_color'),*layer.color,layer.alpha)
        uniform4i(uloc(prog,'stripe'),*layer.stripe)
        uniform1f(uloc(prog,'stripe_edge_alpha'),layer.edge_alpha)
        uniform1f(uloc(prog,'test_depth'),layer.depth)
        draw(4,0,3)

def read_queries(qs):
    finish()
    out=[]
    for q in qs:
        v=C.c_uint64();qresult(q,QUERY_RESULT,C.byref(v));out.append(v.value/1e6)
    assert geterror()==0
    return out

def frame(impl, name, background, layers):
    cap,res=progs[impl]
    bindfbo(0x8D40,fbo);colormask(1,1,1,1);clearcolor(*background,1);clear(0x4000)
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
        slot_tex,slot_fmt,_=SLOT_FORMAT[impl]
        image(0,0,0,1,0,0x88BA,R32UI);image(1,0,0,1,0,0x88B9,slot_fmt);image(2,0,0,1,0,0x88B9,R32UI)   # as the viewer: nothing bound before the clear
        barrier(0x20|0x100)
        clearsub(meta,0,0,0,0,W,H,1,RED_INT,UINT,C.byref(zero))   # count layer only; the resolve zeroes the tail it read
        barrier(0x20)
        image(0,meta,0,1,0,0x88BA,R32UI);image(1,slot_tex,0,1,0,0x88B9,slot_fmt)
    endq(TIME_ELAPSED)
    # --- capture: L layers, distinct depths, standard alpha blend for any fall-through ---
    beginq(TIME_ELAPSED,queries[1])
    use(cap)
    if impl!='direct': uniform(uloc(cap,'alpha_oit_opaque_depth'),0)
    if impl=='ppll': uniform(uloc(cap,'oit_node_cap'),node_cap)
    elif impl!='direct': uniform(uloc(cap,'oit_slot_count'),SLOTS)
    blendfunc(0x0302,0x0303);colormask(1,1,1,1)
    draw_layers(cap,name,layers)
    barrier(0x20|0x2000|0x1000)   # IMAGE | SHADER_STORAGE | ATOMIC_COUNTER
    endq(TIME_ELAPSED)
    # --- resolve: premultiplied over, alpha channel masked ---
    beginq(TIME_ELAPSED,queries[2])
    if impl=='direct':
        endq(TIME_ELAPSED)
        return read_queries(queries)
    use(res);uniform(uloc(res,'depthMap'),0)
    if impl=='ppll':
        uniform(uloc(res,'oit_max_pixel_layers'),OLD_MAX_LAYERS)
        image(0,head,0,0,0,0x88B8,R32UI);base(SSBO,0,nodes)
    else:
        slot_tex,slot_fmt,_=SLOT_FORMAT[impl]
        uniform(uloc(res,'oit_slot_count'),SLOTS)
        image(0,meta,0,1,0,0x88B8,R32UI);image(1,slot_tex,0,1,0,0x88B8,slot_fmt);image(2,meta,0,1,0,0x88B9,R32UI)
    blendfunc(1,0x0303);colormask(1,1,1,0)
    draw(4,0,3)
    endq(TIME_ELAPSED)
    return read_queries(queries)

def read_window(target_fbo, fmt=0x1908, channels=4):
    """The RMSE window of a float colour attachment, row-major, bottom row first."""
    finish();bindfbo(0x8D40,target_fbo)
    n=RMSE_WIN*RMSE_WIN;buf=(C.c_float*(n*channels))()
    readpixels(WIN_X,WIN_Y,RMSE_WIN,RMSE_WIN,fmt,0x1406,buf)
    assert geterror()==0
    return [tuple(buf[i*channels:i*channels+channels]) for i in range(n)]

def run_scene(impl, name, background, layers):
    samples=[]
    for f in range(WARMUP+FRAMES):
        t=frame(impl,name,background,layers)
        if f>=WARMUP: samples.append(t)
    return [statistics.median(s[i] for s in samples) for i in range(len(samples[0]))]

scenarios=json.loads(os.environ['OIT_BENCH_SCENES']) if 'OIT_BENCH_SCENES' in os.environ else [
    ('opaque',1),('full',1),('full',2),('full',4),('full',8),('full',16),('full',32),('full',64),
    ('sparse',8),('sparse',24),('sparse',48),('bright',4),('bright',8),('bright',16),
    ('foliage',16),('foliage',32),('foliage',64)]
scenarios=[(p,int(l)) for p,l in scenarios]
print(f'{W}x{H}, {FRAMES} frames median after {WARMUP} warmup; ms per frame (reset / capture / resolve / total)')
print(f'RMSE window {RMSE_WIN}x{RMSE_WIN} at ({WIN_X},{WIN_Y}): 8-bit units of linear colour over the alpha-covered pixels')
for impl in IMPLS:
    if impl=='ppll': print(f'ppll ({OLD_REF}): {NODES_PER_PX} nodes/px pool ({node_cap*16>>20} MiB), {OLD_MAX_LAYERS} sorted layers')
    if impl in SLOT_FORMAT:
        b=SLOT_FORMAT[impl][2];ref=SLOTS16_REF if impl=='slots16' else 'tree'
        print(f'{impl} ({ref}): {SLOTS} slots/px x {b} B + meta {4*META_LAYERS} B = {b*SLOTS+4*META_LAYERS} B/px ({W*H*b*SLOTS>>20} MiB + {W*H*4*META_LAYERS>>20} MiB)')
print(f'{"scene":<13}{"impl":<8}{"reset":>8}{"capture":>9}{"resolve":>9}{"total":>8}{"rmse":>7}{"max":>7}  note')
results={}   # (pattern, layers) -> {impl: (timings, rmse, max)}
tail_scenes=[]
failures=[]
def check(ok, text):
    print(('ok    ' if ok else 'FAIL  ')+text)
    if not ok: failures.append(text)
for pattern_name,layers in scenarios:
    background,scene_layers=scenes.scene(pattern_name,layers)
    exact=scenes.exact_window(pattern_name,background,scene_layers,WIN_X,WIN_Y,RMSE_WIN,RMSE_WIN)
    covered=[c for _,_,c in exact if c]
    tail_px=sum(c>SLOTS for c in covered)/max(len(covered),1)
    coverage=scenes.COVERAGE.get(pattern_name,1.0)   # screen-wide, for the shared PPLL pool
    if tail_px>0: tail_scenes.append(f'{pattern_name} L={layers}')
    row={}
    for impl in IMPLS:
        med=run_scene(impl,pattern_name,background,scene_layers)
        rmse=worst=None
        if impl!='direct':
            frame(impl,pattern_name,background,scene_layers)
            rmse,worst=scenes.error_stats(read_window(fbo),exact)
        row[impl]=(med,rmse,worst)
        note=''
        if impl=='ppll' and layers*coverage>NODES_PER_PX: note=f'pool overflow: {layers*coverage-NODES_PER_PX:.0f} of {layers*coverage:.0f} fragments/px fall through to in-place blend'
        if impl=='ppll' and layers>OLD_MAX_LAYERS: note+=' + weighted tail past 24'
        if impl in SLOT_FORMAT and tail_px>0: note=f'tail on {100*tail_px:.0f}% of covered px'
        err=f'{rmse:>7.2f}{worst:>7.1f}' if rmse is not None else f'{"-":>7}{"-":>7}'
        print(f'{pattern_name+" L="+str(layers):<13}{impl:<8}{med[0]:>8.3f}{med[1]:>9.3f}{med[2]:>9.3f}{sum(med):>8.3f}{err}  {note}')
    results[(pattern_name,layers)]=row
    if 'ppll' in row and 'slots' in row:
        a,b=sum(row["ppll"][0]),sum(row["slots"][0]);print(f'{"":<21}slots/ppll total = {b/a:.2f}x')
    if 'slots16' in row and 'slots' in row:
        a,b=sum(row["slots16"][0]),sum(row["slots"][0]);print(f'{"":<21}slots/slots16 total = {b/a:.2f}x')

print('checks:')
if ('opaque',1) in results:
    for impl in IMPLS:
        if impl=='direct': continue
        _,rmse,worst=results[('opaque',1)][impl]
        check(worst<=1.0,f'{impl}: a single opaque layer is reproduced (max error {worst:.2f} <= 1 step)')
if 'slots' in IMPLS:
    check(bool(tail_scenes),f'slots: the tail path is exercised ({", ".join(tail_scenes) or "no scene past 8 slots"})')
gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
sys.exit(1 if failures else 0)
