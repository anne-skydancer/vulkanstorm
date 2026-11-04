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
  * "stoch" -- depth-sampled stochastic transparency (Enderton, Sintorn, Shirley, Luebke,
               "Stochastic Transparency", I3D 2010, the depth-based variant of their Sec. 3.3
               with alpha correction), S samples (OIT_BENCH_STOCH_SAMPLES: 4, 8 or 16, default 8),
               single-sample accumulation. Four passes, four timers:
      T  total alpha: clears the two single-sample targets, then every layer adds
         -ln(1 - a) into an R16F target (blend ONE, ONE). The log form makes the pass a sum
         (exactly order independent up to fp16 rounding, and its rounding error is damped by
         the transmittance where layers are dense); alpha_total = 1 - exp(-sum). 1 - a is
         clamped to 2^-14, so an alpha 1 layer counts as 0.99994.
      D  stochastic depth: a full-screen pass seeds a GL_TEXTURE_2D_MULTISAMPLE
         DEPTH_COMPONENT32F target (glTexStorage2DMultisample, S samples) from the opaque depth
         via gl_FragDepth, then every layer is drawn depth-only with LEQUAL and writes
         gl_SampleMask[0]: R = floor(a S + xi) samples, and the mask is a precomputed table
         entry with exactly R bits, each row 64 masks drawn uniformly from all C(S, R)
         subsets (oit_bench_scenes.mask_table). xi and the table column come from one pcg3d
         hash of (pixel x, pixel y, depth bits ^ seed), so the choice is per pixel and per
         fragment, stable from frame to frame and independent between layers.
      A  accumulation: every layer is shaded once per pixel and weighted by its visibility
         vis = count(z <= z_s over the S samples) / S (texelFetch on sampler2DMS; the
         fragment's own samples count, so a front layer has vis 1), adding
         (vis a rgb, vis a) into an RGBA16F target (blend ONE, ONE).
      C  composite: one full-screen pass outputs (acc.rgb alpha_total / acc.a, alpha_total)
         premultiplied over the screen with ONE, ONE_MINUS_SRC_ALPHA and the alpha channel
         masked, as the PPLL resolve does.
    All three geometry passes keep the capture's LEQUAL reject against the opaque depth
    snapshot; the depth passes place each layer's triangle at its depth so that D and A see
    the same window z (the triangles are flat, so the per-sample depth equals the pixel-centre
    depth; sloped geometry would need a bias). Each hardware sample count is probed before
    use (stoch_probe): masks fetched from a texture, as pass D fetches them, must write
    exactly their samples. Where S samples are above GL_MAX_DEPTH_TEXTURE_SAMPLES or fail
    the probe (NVIDIA 621.01 at 16), passes D and A run with S/2 samples twice (or S/4 four
    times) with a new seed into the same accumulation, the paper's iteration; the ratio in C
    normalises it. The multisample-accumulation variant (the paper's Sec. 3.4) is not
    emulated.
OIT_BENCH_IMPLS selects implementations (default ppll,slots,stoch); OIT_BENCH_SCENES a JSON list
of [pattern, layers] pairs.
Each scenario draws L full-screen layers with distinct, unsorted depths; the patterns
(oit_bench_scenes.py) are "full" (every pixel, alpha 0.5), "sparse" (25% of 64x64 blocks, a
hair/particle clump proxy), "bright" (bright alpha 0.22-0.37 layers over black), "foliage"
(thin 40%-coverage stripes, alpha 0.95 core / 0.15 edge, past 8 slots from 32 layers) and
"opaque" (alpha 1). All implementations shade the same scene_color() fragment function.

Correctness: after timing, one more frame per implementation is read back over a window of
OIT_BENCH_RMSE_WIN (default 256) square pixels and compared with the exact back-to-front
composite of the same fragments on the CPU: RMSE and max absolute error in 8-bit units of the
linear colour, over the alpha-covered pixels; stoch is measured at S = 4, 8 and 16 and its
total-alpha target against the exact 1 - prod(1 - a). OIT_BENCH_PNG_DIR, when set, receives
sRGB PNGs of that window (exact, each implementation, stoch at each S) for the scene
OIT_BENCH_PNG_SCENE (default "bright,4"). Checks print ok/FAIL; any FAIL exits 1. The study's
stage 1 gates (stoch GPU total <= 1.5x slots at full L=8, <= 1.0x at full L=32; RMSE <= 1 step
at S = 8 for two alpha 0.5 layers) are printed as info lines, not checks. stoch has no git
ref to pin: its shaders are emulations in this script, not viewer shaders.

Environment: OIT_BENCH_W, OIT_BENCH_H (default 2560x1440), OIT_BENCH_FRAMES (default 20),
OIT_BENCH_OLD_REF. Pass --opengl PATH to select another GL DLL (Mesa).
"""
import json, math, os, statistics, subprocess, sys
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
IMPLS = os.environ.get('OIT_BENCH_IMPLS', 'ppll,slots,stoch').split(',')
STOCH_SAMPLES = int(os.environ.get('OIT_BENCH_STOCH_SAMPLES', 8)); STOCH_RMSE_SAMPLES = (4, 8, 16)
assert STOCH_SAMPLES in STOCH_RMSE_SAMPLES, 'OIT_BENCH_STOCH_SAMPLES must be 4, 8 or 16'
PNG_DIR = os.environ.get('OIT_BENCH_PNG_DIR')
PNG_SCENE = tuple(f(v) for f, v in zip((str, int), os.environ.get('OIT_BENCH_PNG_SCENE', 'bright,4').split(',')))
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
texstorage_ms=fn('glTexStorage2DMultisample',None,U,I,U,I,I,C.c_ubyte);activetex=fn('glActiveTexture',None,U)
depthfunc=fn('glDepthFunc',None,U);drawbuffer=fn('glDrawBuffer',None,U);readbuffer=fn('glReadBuffer',None,U)
getint=fn('glGetIntegerv',None,U,C.POINTER(I));uniform1ui=fn('glUniform1ui',None,I,U)
texsub=fn('glTexSubImage2D',None,U,I,I,I,I,I,U,U,P)
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
for impl in [i for i in IMPLS if i!='stoch']:   # only the selected implementations; 'ppll' and 'slots16' need their git refs
    extra=[(0x8B30,pack_source())] if impl=='slots' else []
    cap=program([(0x8B31,vertex),(0x8B30,capture_source(impl))]+extra)
    res=None if impl=='direct' else program([(0x8B31,vertex),(0x8B30,resolve_source(impl))]+extra)
    progs[impl]=(cap,res)
queries=(U*3)();genq(3,queries)

# --- stoch: depth-sampled stochastic transparency (see the module docstring) ---
TEX2DMS=0x9100;R16F=0x822D;RGBA16F=0x881A;D32F=0x8CAC;DEPTH_TEST=0x0B71
max_ds=I();getint(0x910F,C.byref(max_ds));MAX_DEPTH_SAMPLES=max_ds.value   # GL_MAX_DEPTH_TEXTURE_SAMPLES
stoch_status={}   # hardware samples -> 'ok' or why that count is not used
def stoch_usable(hw):
    if hw not in stoch_status:
        if hw>MAX_DEPTH_SAMPLES: stoch_status[hw]=f'above GL_MAX_DEPTH_TEXTURE_SAMPLES ({MAX_DEPTH_SAMPLES})'
        else:
            try: stoch_resources(hw);stoch_status[hw]=stoch_probe(hw)
            except AssertionError as e: stoch_status[hw]=str(e)
    return stoch_status[hw]=='ok'
def stoch_plan(s):
    # Hardware samples and iterations for an effective sample count s: fewer samples, more
    # seeded iterations of passes D and A where s samples are unavailable or not independent.
    hw,iters=s,1
    while hw>1 and not stoch_usable(hw): hw//=2;iters*=2
    return hw,iters
def color_target(fmt):
    name=U();gentex(1,C.byref(name));bindtex(TEX2D,name);texstorage(TEX2D,1,fmt,W,H)
    texparam(TEX2D,0x2801,0x2600);texparam(TEX2D,0x2800,0x2600)
    target=U();genfbo(1,C.byref(target));bindfbo(0x8D40,target);attachtex(0x8D40,0x8CE0,TEX2D,name,0)
    assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
    return name.value,target.value
stoch_ms={}   # hardware samples -> (MS depth texture, its FBO, mask table texture)
def stoch_resources(hw):
    if hw in stoch_ms: return stoch_ms[hw]
    activetex(0x84C0+1)
    ms=U();gentex(1,C.byref(ms));bindtex(TEX2DMS,ms);texstorage_ms(TEX2DMS,hw,D32F,W,H,1)
    err=geterror();assert err==0,f'multisample depth allocation failed: S={hw}, GL error {err:#x}'
    target=U();genfbo(1,C.byref(target));bindfbo(0x8D40,target);attachtex(0x8D40,0x8D00,TEX2DMS,ms,0)
    drawbuffer(0);readbuffer(0)
    assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
    activetex(0x84C0+2)
    table=scenes.mask_table(hw);words=(U*(64*(hw+1)))(*[m for row in table for m in row])
    masks=U();gentex(1,C.byref(masks));bindtex(TEX2D,masks);texstorage(TEX2D,1,R32UI,64,hw+1)
    texparam(TEX2D,0x2801,0x2600);texparam(TEX2D,0x2800,0x2600)
    texsub(TEX2D,0,0,0,64,hw+1,RED_INT,UINT,words)
    activetex(0x84C0);bindfbo(0x8D40,fbo)
    assert geterror()==0
    stoch_ms[hw]=(ms.value,target.value,masks.value)
    return stoch_ms[hw]
def stoch_probe(hw):
    # Sample mask probe: a fragment whose gl_SampleMask[0] is a mask fetched from an R32UI
    # texture, as pass D fetches it, must write exactly the samples of that mask: each
    # single bit, then table masks with S/2 bits. NVIDIA 621.01 fails this at 16 samples (a
    # fetched mask writes only its low 4 bits, repeated in each group of 4 samples) although
    # a constant, uniform or buffer-sourced mask passes, so the probe must fetch from a texture.
    global probe_progs
    if 'probe_progs' not in globals():
        frag='\n'.join(['#version 430 core','uniform usampler2D probe_masks;',
                        'void main(){ gl_SampleMask[0] = int(texelFetch(probe_masks, ivec2(0, 0), 0).r); }'])
        comp='\n'.join(['#version 430 core','layout(local_size_x=1) in;','uniform sampler2DMS ms; uniform int n;',
                        'layout(std430,binding=0) writeonly buffer ProbeOut { float o[]; };',
                        'void main(){ for (int s = 0; s < n; s++) o[s] = texelFetch(ms, ivec2(1, 1), s).r; }'])
        out=U();genbuf(1,C.byref(out));bindbuf(SSBO,out);data(SSBO,4*64,None,0x88E8);bindbuf(SSBO,0)
        activetex(0x84C0+5)
        mask_tex=U();gentex(1,C.byref(mask_tex));bindtex(TEX2D,mask_tex);texstorage(TEX2D,1,R32UI,1,1)
        texparam(TEX2D,0x2801,0x2600);texparam(TEX2D,0x2800,0x2600);activetex(0x84C0)
        probe_progs=(program([(0x8B31,STOCH_VERTEX),(0x8B30,frag)]),program([(0x91B9,comp)]),out.value,mask_tex.value)
    draw_prog,read_prog,out,mask_tex=probe_progs
    ms_tex,ms_fbo,_=stoch_ms[hw]
    activetex(0x84C0+1);bindtex(TEX2DMS,ms_tex);activetex(0x84C0+5);bindtex(TEX2D,mask_tex);activetex(0x84C0)
    result='ok'
    for mask in [1<<k for k in range(hw)]+scenes.mask_table(hw)[hw//2][:8]:
        word=U(mask);activetex(0x84C0+5);texsub(TEX2D,0,0,0,1,1,RED_INT,UINT,C.byref(word));activetex(0x84C0)
        bindfbo(0x8D40,ms_fbo);enable(DEPTH_TEST);depthfunc(0x0207);clear(0x100)   # depth 1.0
        use(draw_prog);uniform(loc(draw_prog,b'probe_masks'),5);uniform1f(loc(draw_prog,b'test_depth'),0.25);draw(4,0,3)
        disable(DEPTH_TEST);depthfunc(0x0201);barrier(0xFFFFFFFF)
        use(read_prog);uniform(loc(read_prog,b'ms'),1);uniform(loc(read_prog,b'n'),hw);base(SSBO,0,out)
        fn('glDispatchCompute',None,U,U,U)(1,1,1);barrier(0xFFFFFFFF)
        got=(C.c_float*hw)();bindbuf(SSBO,out);fn('glGetBufferSubData',None,U,S,S,P)(SSBO,0,4*hw,got);bindbuf(SSBO,0)
        written=sum(1<<s for s in range(hw) if got[s]<0.5)
        if written!=mask:
            result=f'gl_SampleMask {mask:#x} wrote samples {written:#x}: samples not independent';break
    bindfbo(0x8D40,fbo);assert geterror()==0
    return result
STOCH_VERTEX='#version 430 core\nuniform float test_depth;\ninvariant gl_Position;\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,test_depth*2.-1.,1.);}'
STOCH_COMMON='''#version 430 core
uniform sampler2D alpha_oit_opaque_depth;   // unit 0: detached opaque depth snapshot
'''+scenes.SCENE_GLSL+'''
// The capture's reject: LEQUAL against the opaque depth, nothing for zero alpha.
vec4 stoch_fragment()
{
    vec4 c = scene_color();
    if (!(gl_FragCoord.z <= texelFetch(alpha_oit_opaque_depth, ivec2(gl_FragCoord.xy), 0).r)) discard;
    c.a = clamp(c.a, 0.0, 1.0);
    if (c.a <= 0.0) discard;
    return c;
}
'''
STOCH_TOTAL=STOCH_COMMON+'''
out vec4 total;
void main()
{
    vec4 c = stoch_fragment();
    total = vec4(-log(max(1.0 - c.a, 1.0 / 16384.0)), 0.0, 0.0, 0.0);
}
'''
STOCH_DEPTH=STOCH_COMMON+'''
uniform usampler2D stoch_masks;   // unit 2: row r = 64 masks with exactly r of S bits set
uniform int stoch_samples;
uniform uint stoch_seed;
// pcg3d (Jarzynski and Olano, "Hash Functions for GPU Rendering", JCGT 2020)
uvec3 stoch_pcg3d(uvec3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}
void main()
{
    vec4 c = stoch_fragment();
    uvec3 h = stoch_pcg3d(uvec3(uvec2(gl_FragCoord.xy), floatBitsToUint(gl_FragCoord.z) ^ stoch_seed));
    float xi = float(h.x >> 8) * (1.0 / 16777216.0);
    int r = min(int(c.a * float(stoch_samples) + xi), stoch_samples);   // stratified count
    if (r == 0) discard;
    gl_SampleMask[0] = int(texelFetch(stoch_masks, ivec2(int(h.y & 63u), r), 0).r);
}
'''
STOCH_ACCUM=STOCH_COMMON+'''
uniform sampler2DMS stoch_depth;   // unit 1
uniform int stoch_samples;
out vec4 accum;
void main()
{
    vec4 c = stoch_fragment();
    ivec2 p = ivec2(gl_FragCoord.xy);
    float z = gl_FragCoord.z;
    int n = 0;
    for (int s = 0; s < stoch_samples; s++)
    {
        n += int(z <= texelFetch(stoch_depth, p, s).r);
    }
    if (n == 0) discard;
    float w = c.a * float(n) / float(stoch_samples);
    accum = vec4(c.rgb * w, w);
}
'''
STOCH_SEED='''#version 430 core
uniform sampler2D alpha_oit_opaque_depth;
void main()
{
    gl_FragDepth = texelFetch(alpha_oit_opaque_depth, ivec2(gl_FragCoord.xy), 0).r;
}
'''
STOCH_COMPOSITE='''#version 430 core
uniform sampler2D stoch_total;   // unit 3
uniform sampler2D stoch_accum;   // unit 4
out vec4 frag_color;
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 acc = texelFetch(stoch_accum, p, 0);
    if (acc.a <= 0.0) discard;
    float alpha_total = 1.0 - exp(-texelFetch(stoch_total, p, 0).r);
    frag_color = vec4(acc.rgb * (alpha_total / acc.a), alpha_total);   // alpha correction
}
'''
if 'stoch' in IMPLS:
    total_tex,total_fbo=color_target(R16F)
    accum_tex,accum_fbo=color_target(RGBA16F)
    activetex(0x84C0+3);bindtex(TEX2D,total_tex);activetex(0x84C0+4);bindtex(TEX2D,accum_tex)
    activetex(0x84C0);bindtex(TEX2D,depth);bindfbo(0x8D40,fbo)
    stoch_progs={k:program([(0x8B31,STOCH_VERTEX),(0x8B30,src)]) for k,src in
                 (('T',STOCH_TOTAL),('D',STOCH_DEPTH),('A',STOCH_ACCUM),('seed',STOCH_SEED),('C',STOCH_COMPOSITE))}
    stoch_queries=(U*12)();genq(12,stoch_queries)
    for s in STOCH_RMSE_SAMPLES: stoch_plan(s)
    assert geterror()==0
def stoch_summary():
    plans=', '.join(f'S={s}: '+('{} hw'.format(stoch_plan(s)[0]) if stoch_plan(s)[1]==1 else '{} hw x {} iterations'.format(*stoch_plan(s))) for s in STOCH_RMSE_SAMPLES)
    return 'multisample depth with independent samples: '+', '.join(f'{k}: {v}' for k,v in sorted(stoch_status.items()))+'; plans '+plans

def frame_stoch(name, background, layers, samples, seed=0):
    # One stoch frame; returns the four pass times [T, D, A, C] in ms.
    hw,iters=stoch_plan(samples)
    ms_tex,ms_fbo,_=stoch_resources(hw)
    activetex(0x84C0+1);bindtex(TEX2DMS,ms_tex);activetex(0x84C0+2);bindtex(TEX2D,stoch_ms[hw][2]);activetex(0x84C0)
    bindfbo(0x8D40,fbo);colormask(1,1,1,1);clearcolor(*background,1);clear(0x4000)
    q=list(stoch_queries)
    # --- T: clear the total-alpha and accumulation targets, sum -ln(1 - a) ---
    beginq(TIME_ELAPSED,q[0])
    clearcolor(0,0,0,0);bindfbo(0x8D40,accum_fbo);clear(0x4000);bindfbo(0x8D40,total_fbo);clear(0x4000)
    prog=stoch_progs['T'];use(prog);uniform(uloc(prog,'alpha_oit_opaque_depth'),0)
    blendfunc(1,1);draw_layers(prog,name,layers)
    endq(TIME_ELAPSED)
    used=[q[0]]
    for it in range(iters):
        # --- D: seed the multisample depth from the opaque depth, then the stochastic masks ---
        beginq(TIME_ELAPSED,q[1+2*it])
        bindfbo(0x8D40,ms_fbo);enable(DEPTH_TEST);depthfunc(0x0207)   # ALWAYS
        prog=stoch_progs['seed'];use(prog);uniform(uloc(prog,'alpha_oit_opaque_depth'),0);draw(4,0,3)
        depthfunc(0x0203)   # LEQUAL
        prog=stoch_progs['D'];use(prog);uniform(uloc(prog,'alpha_oit_opaque_depth'),0)
        uniform(uloc(prog,'stoch_masks'),2);uniform(uloc(prog,'stoch_samples'),hw);uniform1ui(uloc(prog,'stoch_seed'),seed*977+it*7919)
        draw_layers(prog,name,layers)
        disable(DEPTH_TEST);depthfunc(0x0201)
        endq(TIME_ELAPSED)
        # --- A: shade every layer once, weighted by its visibility in the S depths ---
        beginq(TIME_ELAPSED,q[2+2*it])
        bindfbo(0x8D40,accum_fbo)
        prog=stoch_progs['A'];use(prog);uniform(uloc(prog,'alpha_oit_opaque_depth'),0)
        uniform(uloc(prog,'stoch_depth'),1);uniform(uloc(prog,'stoch_samples'),hw)
        blendfunc(1,1);draw_layers(prog,name,layers)
        endq(TIME_ELAPSED)
        used+=[q[1+2*it],q[2+2*it]]
    # --- C: alpha-corrected composite over the screen ---
    beginq(TIME_ELAPSED,q[-1])
    bindfbo(0x8D40,fbo)
    prog=stoch_progs['C'];use(prog);uniform(uloc(prog,'stoch_total'),3);uniform(uloc(prog,'stoch_accum'),4)
    blendfunc(1,0x0303);colormask(1,1,1,0);draw(4,0,3)
    endq(TIME_ELAPSED)
    t=read_queries(used+[q[-1]])
    return [t[0],sum(t[1:-1:2]),sum(t[2:-1:2]),t[-1]]
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
    if impl=='stoch': return frame_stoch(name,background,layers,STOCH_SAMPLES)
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
    if impl=='stoch':
        hw,iters=stoch_plan(STOCH_SAMPLES)
        print(f'stoch (bench emulation): S={STOCH_SAMPLES} ({hw} hardware samples x {iters} iteration{"s" if iters>1 else ""}): MS depth D32F {4*hw} B + total alpha R16F 2 B + accumulation RGBA16F 8 B = {4*hw+10} B/px ({W*H*(4*hw+10)>>20} MiB)')
        print(f'stoch: GL_MAX_DEPTH_TEXTURE_SAMPLES = {MAX_DEPTH_SAMPLES}; '+stoch_summary())
        print('stoch timers: alpha = clears + total alpha pass, depth = opaque seed + stochastic depth pass, accum = accumulation pass, comp = composite')
print(f'{"scene":<13}{"impl":<8}{"reset":>8}{"capture":>9}{"resolve":>9}{"alpha":>8}{"depth":>8}{"accum":>8}{"comp":>8}{"total":>8}{"rmse":>7}{"max":>7}  note')
results={}   # (pattern, layers) -> {impl: (timings, rmse, max)}
stoch_rmse={}   # (pattern, layers) -> {S: (rmse, max)}, plus 'alpha': max total-alpha error
png_written=[]
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
        if impl=='stoch':
            per_s={}
            for s in STOCH_RMSE_SAMPLES:
                frame_stoch(pattern_name,background,scene_layers,s)
                image_rgb=read_window(fbo)
                per_s[s]=scenes.error_stats(image_rgb,exact)
                if s==STOCH_SAMPLES:
                    totals=read_window(total_fbo,fmt=0x1903,channels=1)   # GL_RED
                    per_s['alpha']=max(abs((1.0-math.exp(-v[0]))-e[1]) for v,e in zip(totals,exact))
                if PNG_DIR and (pattern_name,layers)==PNG_SCENE:
                    png_written.append((f'stoch_s{s}',image_rgb))
            stoch_rmse[(pattern_name,layers)]=per_s
            rmse,worst=per_s[STOCH_SAMPLES]
        elif impl!='direct':
            frame(impl,pattern_name,background,scene_layers)
            image_rgb=read_window(fbo)
            rmse,worst=scenes.error_stats(image_rgb,exact)
            if PNG_DIR and (pattern_name,layers)==PNG_SCENE: png_written.append((impl,image_rgb))
        row[impl]=(med,rmse,worst)
        note=''
        if impl=='ppll' and layers*coverage>NODES_PER_PX: note=f'pool overflow: {layers*coverage-NODES_PER_PX:.0f} of {layers*coverage:.0f} fragments/px fall through to in-place blend'
        if impl=='ppll' and layers>OLD_MAX_LAYERS: note+=' + weighted tail past 24'
        if impl in SLOT_FORMAT and tail_px>0: note=f'tail on {100*tail_px:.0f}% of covered px'
        err=f'{rmse:>7.2f}{worst:>7.1f}' if rmse is not None else f'{"-":>7}{"-":>7}'
        if impl=='stoch':
            label=f'stoch{STOCH_SAMPLES}'
            times=f'{"-":>8}{"-":>9}{"-":>9}'+''.join(f'{v:>8.3f}' for v in med)
        else:
            label=impl
            times=f'{med[0]:>8.3f}{med[1]:>9.3f}{med[2]:>9.3f}'+f'{"-":>8}'*4
        print(f'{pattern_name+" L="+str(layers):<13}{label:<8}{times}{sum(med):>8.3f}{err}  {note}')
    results[(pattern_name,layers)]=row
    if PNG_DIR and (pattern_name,layers)==PNG_SCENE:
        os.makedirs(PNG_DIR,exist_ok=True)
        stem=f'{pattern_name}{layers}_{W}x{H}'
        for label,image_rgb in [('exact',[e[0] for e in exact])]+png_written:
            path=os.path.join(PNG_DIR,f'{stem}_{label}.png')
            scenes.write_png(path,RMSE_WIN,RMSE_WIN,[px[:3] for px in image_rgb])
            print(f'{"":<21}wrote {path}')
    if 'ppll' in row and 'slots' in row:
        a,b=sum(row["ppll"][0]),sum(row["slots"][0]);print(f'{"":<21}slots/ppll total = {b/a:.2f}x')
    if 'slots16' in row and 'slots' in row:
        a,b=sum(row["slots16"][0]),sum(row["slots"][0]);print(f'{"":<21}slots/slots16 total = {b/a:.2f}x')
    if 'stoch' in row and 'slots' in row:
        a,b=sum(row["slots"][0]),sum(row["stoch"][0]);print(f'{"":<21}stoch/slots total = {b/a:.2f}x')

if stoch_rmse:
    print(f'RMSE / max error, 8-bit units ({RMSE_WIN}x{RMSE_WIN} window); frags = mean fragments per covered pixel, tail = covered pixels past {SLOTS} slots')
    head=f'{"scene":<13}{"frags":>6}{"tail":>6}'+(f'{"slots":>13}' if 'slots' in IMPLS else '')
    head+=''.join(f'{"stoch S="+str(s):>13}' for s in STOCH_RMSE_SAMPLES)+f'{"alpha err":>11}'
    print(head)
    for key,per_s in stoch_rmse.items():
        name,layers=key
        bg,ls=scenes.scene(name,layers)
        counts=[c for _,_,c in scenes.exact_window(name,bg,ls,WIN_X,WIN_Y,RMSE_WIN,RMSE_WIN) if c]
        line=f'{name+" L="+str(layers):<13}{sum(counts)/max(len(counts),1):>6.1f}{100*sum(c>SLOTS for c in counts)/max(len(counts),1):>5.0f}%'
        if 'slots' in IMPLS: line+=f'{results[key]["slots"][1]:>7.2f} /{results[key]["slots"][2]:>4.0f}'
        line+=''.join(f'{per_s[s][0]:>7.2f} /{per_s[s][1]:>4.0f}' for s in STOCH_RMSE_SAMPLES)
        line+=f'{per_s["alpha"]*255:>9.2f}/255'
        print(line)
print('checks:')
if ('opaque',1) in results:
    for impl in IMPLS:
        if impl in ('direct','stoch'): continue   # stoch: checked at every S below
        _,rmse,worst=results[('opaque',1)][impl]
        check(worst<=1.0,f'{impl}: a single opaque layer is reproduced (max error {worst:.2f} <= 1 step)')
if ('opaque',1) in stoch_rmse:
    worst=max(stoch_rmse[('opaque',1)][s][1] for s in STOCH_RMSE_SAMPLES)
    check(worst<=1.0,f'stoch: a single opaque layer is reproduced at S = 4, 8, 16 (max error {worst:.2f} <= 1 step)')
if stoch_rmse:
    # Per scene non-increasing within 0.05 steps (exact scenes stay at the quantisation floor),
    # and strictly decreasing summed over the noisy scenes (RMSE above 0.5 steps at S = 4).
    bad=[f'{p} L={l}' for (p,l),r in stoch_rmse.items()
         if not (r[8][0]<=r[4][0]+0.05 and r[16][0]<=r[8][0]+0.05)]
    noisy=[r for r in stoch_rmse.values() if r[4][0]>0.5]
    sums=[sum(r[s][0] for r in noisy) for s in STOCH_RMSE_SAMPLES]
    check(not bad and (not noisy or sums[0]>sums[1]>sums[2]),
          f'stoch: RMSE decreases with S (sum over {len(noisy)} noisy scenes {sums[0]:.1f} > {sums[1]:.1f} > {sums[2]:.1f}'+(f'; not in {", ".join(bad)}' if bad else '')+')')
    worst=max(r['alpha'] for r in stoch_rmse.values())
    check(worst<=1/255,f'stoch: total alpha matches the exact 1 - prod(1 - a) (max error {worst*255:.3f}/255 <= 1/255)')
if 'slots' in IMPLS and 'OIT_BENCH_SCENES' not in os.environ:
    check(bool(tail_scenes),f'slots: the tail path is exercised ({", ".join(tail_scenes) or "no scene past 8 slots"})')
if 'stoch' in IMPLS and 'slots' in IMPLS:
    print('stage 1 gates (info, not checks):')
    for layers,limit in ((8,1.5),(32,1.0)):
        if ('full',layers) in results:
            r=sum(results[('full',layers)]['stoch'][0])/sum(results[('full',layers)]['slots'][0])
            print(f'info  GPU total stoch{STOCH_SAMPLES}/slots at full L={layers}: {r:.2f}x (gate <= {limit}x: {"met" if r<=limit else "not met"})')
    if ('full',2) in stoch_rmse:
        r=stoch_rmse[('full',2)][8][0]
        print(f'info  RMSE at S = 8, two alpha 0.5 layers: {r:.2f} steps (gate <= 1: {"met" if r<=1.0 else "not met"})')
    print('info  '+stoch_summary())
gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
sys.exit(1 if failures else 0)
