# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
# <<bench-shared:begin>>
"""Offscreen stochastic transparency regression, no viewer launch; --opengl PATH selects Mesa.

Reuses the standalone Windows GL bootstrap and shader compiler (gl_test_context.py) and runs the
viewer's own stochastic transparency shaders (the stochastic engine's stochasticSampleF.glsl and
stochasticResolveUtilF.glsl, and the transparency's alphaStochasticF.glsl, alphaStochasticExpandF.glsl,
alphaStochasticCurrentF.glsl + alphaStochasticResolveF.glsl (the grain-measuring temporal resolve, with its mask), alphaStochasticCompositeF.glsl (with the smooth)) with the pass structure and GL state of LLPipeline /
LLDrawPoolAlpha::renderStochastic on a small target:
  * the mask table: the generator (a port of LLStochasticEngine::maskTable) gives exactly r
    bits in row r, nothing above S, varied rows, the same table on every run; the transparency's table
    (S = 8) and its blue-noise rows are the C++ ones (the hash in the C++ test);
  * mask coverage: the samples a fragment covers in the stochastic depth, read back per sample,
    are exactly the bits the independent Python model of st_mask predicts (pixel, face seed,
    triangle, stratified R = floor(alpha S + xi)), for 8 samples in one pass,
    4 in one; their count is R, with mean alpha S; different face seeds give unrelated masks;
  * total alpha: 1 - prod(1 - alpha) exactly, whatever the visibility;
  * visibility weighting and the composite: two layers whose estimate is exact at S = 8
    (alpha 0.5), a noisy pair whose mean over the pixels converges, an opaque occluder;
  * the glow: accumulated like the colour, corrected, added to the scene alpha after the
    composite has dimmed it;
  * faces on, a third of a pixel's depth in front of and behind a sloped opaque surface: the
    first two cover every sample, the third leaves the scene untouched, as sorted alpha draws
    them; the depth spread of 1.0.1.81943 (the centre's depth on every sample, with a
    slope-scaled offset in the passes) is shown to let the face behind through.
  * the grain-measuring resolve at S = 8 (8 samples in one pass: the GL 4.6 base): the grain of one layer is 0 and its colour sharp, two layers have grain,
    the history is dropped where the total alpha changes or the far layer slides, the smooth leaves edges alone;
  * the mask (the resolve and the composite draw only where something transparent was drawn) and the smooth folded into the composite:
    over frame sequences (transparency appearing, moving, scattered, gone the next frame) the image, the history and the last
    total alpha equal those of the passes before the mask (git ref c121dc100c: unmasked blend, separate smooth) bit for bit;
  * the NaN guard: NaN and infinite colours, alphas and transparencies injected into the passes never reach the
    history, the neighbours or the image.
;
  * sub-pixel camera pans (horizontal, vertical, diagonal): the transparency history is read with a Catmull-Rom filter at the exact
    reprojected position; frame 40 beats the nearest read and plain 8 samples, whole-pixel pans and the static view are unchanged;
  * blended legacy materials that can glare (materialF.glsl itself, plain and as its variant, with
    stand-ins for the lighting it links): the composite equals sorted alpha, an alpha 0 layer with
    matte maps leaves the opaque surface untouched, and shiny glass keeps its reflections (#328);
    with the alpha-only passes reading another face's normal and specular maps (TexSetup before
    2026-11-05) the alpha 0 layer darkens every pixel and the glass loses its reflections.
"""
from pathlib import Path
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
shaders = root / 'indra/newview/app_settings/shaders/class1/deferred'

gentex=fn('glGenTextures',None,I,C.POINTER(U));bindtex=fn('glBindTexture',None,U,U)
texparam=fn('glTexParameteri',None,U,U,I);teximage=fn('glTexImage2D',None,U,I,I,I,I,I,U,U,P)
teximage_ms=fn('glTexImage2DMultisample',None,U,I,U,I,I,C.c_ubyte)
genfbo=fn('glGenFramebuffers',None,I,C.POINTER(U));bindfbo=fn('glBindFramebuffer',None,U,U)
attachtex=fn('glFramebufferTexture2D',None,U,U,U,U,I);status=fn('glCheckFramebufferStatus',U,U)
drawbuffer=fn('glDrawBuffer',None,U);readbuffer=fn('glReadBuffer',None,U)
clearbuffer=fn('glClearBufferfv',None,U,I,C.POINTER(C.c_float))
geterror=fn('glGetError',U);loc=fn('glGetUniformLocation',I,U,C.c_char_p)
uniform1i=fn('glUniform1i',None,I,I);uniform1f=fn('glUniform1f',None,I,C.c_float)
uniform3f=fn('glUniform3f',None,I,C.c_float,C.c_float,C.c_float)
draw=fn('glDrawArrays',None,U,I,I);activetex=fn('glActiveTexture',None,U)
enable=fn('glEnable',None,U);disable=fn('glDisable',None,U)
blendsep=fn('glBlendFuncSeparate',None,U,U,U,U);depthfunc=fn('glDepthFunc',None,U);depthmask=fn('glDepthMask',None,C.c_ubyte)
offset=fn('glPolygonOffset',None,C.c_float,C.c_float);viewport=fn('glViewport',None,I,I,I,I)
readpixels=fn('glReadPixels',None,I,I,I,I,U,U,P);getint=fn('glGetIntegerv',None,U,C.POINTER(I))
clearcolor=fn('glClearColor',None,C.c_float,C.c_float,C.c_float,C.c_float);clear=fn('glClear',None,U)
cleardepth=fn('glClearDepth',None,C.c_double)
TEX2D=0x0DE1;TEXMS=0x9100;FB=0x8D40;COLOR0=0x8CE0;COLOR1=0x8CE1;DEPTH_ATT=0x8D00
R32UI=0x8236;RED_INT=0x8D94;UINT=0x1405;FLOAT=0x1406;RGBA=0x1908;RGBA16F=0x881A;RG16F=0x822F;RGBA32F=0x8814
D24=0x81A6;DEPTH_COMP=0x1902;NEAREST=0x2600;MIN=0x2801;MAG=0x2800
BLEND=0x0BE2;DEPTH_TEST=0x0B71;MULTISAMPLE=0x809D;POLY_OFFSET=0x8037;CULL=0x0B44
ZERO=0;ONE=1;SRC_ALPHA=0x0302;ONE_MINUS_SRC_ALPHA=0x0303;LEQUAL=0x0203;ALWAYS=0x0207;NONE=0
import os, sys
W,H=[int(v) for v in os.environ.get("STOCH_TEST_SIZE","64x32").split("x")]   # a size that is not a multiple of anything (70x37) exercises the partial edges
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
viewport(0,0,W,H);disable(CULL)
getint(0x910F,C.byref(max_ms:=I()))   # GL_MAX_DEPTH_TEXTURE_SAMPLES
assert max_ms.value>=8,('needs 8 depth samples',max_ms.value)

# ---- the mask table: LLStochasticEngine::maskTable, ported
SEEDS=2048;M64=(1<<64)-1
def mask_table(s):
    state=(0x9e3779b97f4a7c15^s)&M64;table=[]
    def nxt():
        nonlocal state
        state=(state+0x9e3779b97f4a7c15)&M64;z=state
        z=((z^(z>>30))*0xbf58476d1ce4e5b9)&M64;z=((z^(z>>27))*0x94d049bb133111eb)&M64
        return z^(z>>31)
    for r in range(s+1):
        for _ in range(SEEDS):
            bits=list(range(s));mask=0
            for i in range(r):
                j=i+nxt()%(s-i);bits[i],bits[j]=bits[j],bits[i];mask|=1<<bits[i]
            table.append(mask)
    return table
tables={s:mask_table(s) for s in (4,8,16)}
# the blue-noise tile (llstochasticbluenoise.h, scripts/perf/gen_stochastic_blue_noise.py): two more rows of the table texture
import re
_bn=(root/'indra/newview/llstochasticbluenoise.h').read_text()
BLUE=[int(v) for v in re.findall(r'\b\d+\b',_bn.split('BLUE_NOISE[BLUE_NOISE_SIZE * BLUE_NOISE_SIZE] = {',1)[1].split('};')[0])]
assert len(BLUE)==4096 and sorted(BLUE)==list(range(4096)),'the blue-noise tile holds 0..4095 once each'
NOISE_ROWS=2
for s,t in tables.items():
    assert len(t)==(s+1)*SEEDS
    for r in range(s+1):
        row=t[r*SEEDS:(r+1)*SEEDS]
        assert all(bin(m).count('1')==r and m>>s==0 for m in row),('table row',s,r)
        if 0<r<s: assert len(set(row))>1,('row not varied',s,r)
assert mask_table(8)==tables[8]
# the C++ generator (LLStochasticEngine::maskTable, llstochasticstate.h) makes the same tables: the
# FNV-1a hashes in its standalone test (indra/newview/tests/llstochastichistory_test.cpp, in ci-checks)
def fnv1a(words):
    h=0xcbf29ce484222325
    for w in words:
        for i in range(4):
            h^=(w>>(8*i))&0xff;h=(h*0x100000001b3)&M64
    return h
import re
cpp_test=(root/'indra/newview/tests/llstochastichistory_test.cpp').read_text()
for s,t in tables.items():
    m=re.search(r'constexpr unsigned long long MASK_HASH_%d = 0x([0-9a-f]+)ULL;'%s,cpp_test);assert m,('MASK_HASH in the C++ test',s)
    assert int(m.group(1),16)==fnv1a(t),('the Python mask table differs from the C++ one',s,hex(fnv1a(t)))
tables_tex={s:t+BLUE for s,t in tables.items()}   # what the viewer uploads: S + 1 mask rows, then the tile in NOISE_ROWS rows
m=re.search(r'constexpr unsigned long long TRANSPARENCY_TABLE_HASH = 0x([0-9a-f]+)ULL;',cpp_test);assert m,'TRANSPARENCY_TABLE_HASH in the C++ test'
assert int(m.group(1),16)==fnv1a(tables_tex[8]),('the transparency table differs from the C++ one',hex(fnv1a(tables_tex[8])))
print('mask tables: rows of exactly r bits for S = 4, 8, 16, varied and reproducible; the S = 8 table with the blue-noise tile is the C++ one')

# ---- the Python model of st_mask (alphaStochasticF.glsl), float32 at every step
def f32(x): return struct.unpack('<f',struct.pack('<f',x))[0]
def h32(v):
    x=(v*747796405+2891336453)&0xffffffff
    x=(((x>>((x>>28)+4))^x)*277803737)&0xffffffff
    return ((x>>22)^x)&0xffffffff
def model_mask(px,py,seed,prim,a,s,table,frame=0):
    """stoch_mask / st_mask, float32 at every step: the dither is the blue-noise tile's value of the pixel, the face's offset and the
    golden-ratio step of the frame; the subset is hashed from the face's seed (with the frame), the pixel and a constant."""
    sd=h32((seed*0x9e3779b9+0x7f4a7c15)&0xffffffff)
    sd=h32(sd^h32((prim+0x632be5ab)&0xffffffff))
    fseed=h32((sd+frame*0x9e3779b9)&0xffffffff) if frame else sd
    h=h32(px|(py<<16))
    at=(py&63)*64+(px&63)
    blue=f32(f32(BLUE[at]+0.5)/4096.0)
    face=f32((h32(sd^0x2545f491)>>8)/16777216.0)
    step=f32((((frame*0x9e3779b9)&0xffffffff)>>8)/16777216.0) if frame else 0.0
    t=f32(f32(blue+face)+step);xi=f32(t-math.floor(t))
    col=f32((h32(fseed^h^0x5bd1e995)>>8)/16777216.0)
    r=min(int(math.floor(f32(f32(min(max(a,0.),1.)*s)+xi))),s)
    c=min(int(f32(col*SEEDS)),SEEDS-1)
    return table[r*SEEDS+c]

# ---- programs: the viewer's shaders as the viewer compiles them (#version 420)
def viewer(name,version=420):
    text=(shaders/name).read_text()
    return ('#version %d\n'%version)+text.replace('/*[EXTRA_CODE_HERE]*/','')
engine=viewer('stochasticSampleF.glsl')   # the stochastic engine's sampling: the hash, the frame seed, the table lookup
helper=viewer('alphaStochasticF.glsl')    # the transparency's part: the face's identity and the pass
vertex='''#version 420
uniform float z; uniform float slope; uniform float slope_y;
void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2.0-1.0; gl_Position=vec4(p,z+slope*p.x+slope_y*p.y,1.0); }'''
# the alpha programs' ALPHA_STOCHASTIC variant: alpha alone, the sample mask
variant=program([(0x8B31,vertex),(0x8B30,'''#version 420
uniform float test_alpha; out vec4 frag_color; int alpha_stochastic_mask(float a);
void main(){ frag_color=vec4(0.0,0.0,0.0,max(test_alpha,0.0)); gl_SampleMask[0]=alpha_stochastic_mask(test_alpha); }'''),(0x8B30,helper),(0x8B30,engine)])
# the ordinary program: shaded colour and alpha (the accumulation)
shade=program([(0x8B31,vertex),(0x8B30,'''#version 420
uniform float test_alpha; uniform vec3 test_rgb; out vec4 frag_color;
void main(){ frag_color=vec4(test_rgb,test_alpha); }''')])
# the glow programs (emissiveF.glsl): the glow in green and alpha
glowprog=program([(0x8B31,vertex),(0x8B30,'''#version 420
uniform float test_glow; out vec4 frag_color;
void main(){ frag_color=max(vec4(0.0,test_glow,0.0,test_glow),vec4(0.0)); }''')])
screen_vertex='''#version 420
void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); gl_Position=vec4(p*2.0-1.0,0.0,1.0); }'''
expand=program([(0x8B31,screen_vertex),(0x8B30,viewer('alphaStochasticExpandF.glsl'))])
# the depth spread as released in 1.0.1.81943 (the pixel centre's depth on every sample), for the
# regression check below: with it the passes needed a slope-scaled polygon offset
BEFORE_REF='dd23ea50c4'
import subprocess
expand_before=program([(0x8B31,screen_vertex),(0x8B30,'#version 420\n'+subprocess.run(['git','show',
    BEFORE_REF+':indra/newview/app_settings/shaders/class1/deferred/alphaStochasticExpandF.glsl'],cwd=root,
    capture_output=True,text=True,check=True).stdout.replace('/*[EXTRA_CODE_HERE]*/',''))])
# glPolygonOffset of LLDrawPoolAlpha::renderStochastic: factor; units of the total alpha and the
# stochastic depth, of the accumulation, of the glow
VIEWER_OFFSET=(-1/256,-1.,-2.,-3.)
BEFORE_OFFSET=(-1.,-1.,-2.,-3.)
composite=program([(0x8B31,screen_vertex),(0x8B30,viewer('alphaStochasticCompositeF.glsl')),(0x8B30,viewer('stochasticResolveUtilF.glsl'))])
# the temporal accumulation: this frame's samples measured (averaged, their grain, the depths, the mask), blended with
# the history with the engine's resolve object attached; the composite smooths it (the engine's object attached for the frame weight)
current_prog=program([(0x8B31,screen_vertex),(0x8B30,viewer('alphaStochasticCurrentF.glsl'))])
resolve_prog=program([(0x8B31,screen_vertex),(0x8B30,viewer('alphaStochasticResolveF.glsl')),(0x8B30,viewer('stochasticResolveUtilF.glsl'))])
# The before: the shaders of OLD_REF (the viewer before the 2026-11-14 upgrade: S = 8, the hashed dither, the temporal pair without
# grain, the composite reading the history), from git, as the viewer compiled them then
import subprocess
OLD_REF=os.environ.get('STOCH_OLD_REF','1f38b56b50')
def old_viewer(name,version=420):
    text=subprocess.run(['git','show',OLD_REF+':indra/newview/app_settings/shaders/class1/deferred/'+name],cwd=root,capture_output=True,text=True,check=True).stdout
    return ('#version %d\n'%version)+text.replace('/*[EXTRA_CODE_HERE]*/','')
REF_REF=os.environ.get('STOCH_REF_REF','c121dc100c')   # the commit before the mask: unmasked blend, a smooth pass of its own
def ref_viewer(name,version=420):
    text=subprocess.run(['git','show',REF_REF+':indra/newview/app_settings/shaders/class1/deferred/'+name],cwd=root,capture_output=True,text=True,check=True).stdout
    return ('#version %d\n'%version)+text.replace('/*[EXTRA_CODE_HERE]*/','')
ref_current=program([(0x8B31,screen_vertex),(0x8B30,ref_viewer('alphaStochasticCurrentF.glsl'))])
ref_resolve=program([(0x8B31,screen_vertex),(0x8B30,ref_viewer('alphaStochasticResolveF.glsl')),(0x8B30,ref_viewer('stochasticResolveUtilF.glsl'))])
ref_smooth=program([(0x8B31,screen_vertex),(0x8B30,ref_viewer('alphaStochasticSmoothF.glsl')),(0x8B30,ref_viewer('stochasticResolveUtilF.glsl'))])
ref_composite=program([(0x8B31,screen_vertex),(0x8B30,ref_viewer('alphaStochasticCompositeF.glsl'))])
VARIANT_OLD={}       # a new variant program -> its twin of the old pipeline (the before / after comparison)
old_helper=old_viewer('alphaStochasticF.glsl');old_engine=old_viewer('stochasticSampleF.glsl')
old_composite=program([(0x8B31,screen_vertex),(0x8B30,old_viewer('alphaStochasticCompositeF.glsl'))])
old_current=program([(0x8B31,screen_vertex),(0x8B30,old_viewer('alphaStochasticCurrentF.glsl'))])
old_resolve_full=program([(0x8B31,screen_vertex),(0x8B30,old_viewer('alphaStochasticResolveF.glsl')),(0x8B30,old_viewer('stochasticResolveUtilF.glsl'))])
VARIANT_OLD[variant]=program([(0x8B31,vertex),(0x8B30,'''#version 420
uniform float test_alpha; out vec4 frag_color; int alpha_stochastic_mask(float a);
void main(){ frag_color=vec4(0.0,0.0,0.0,max(test_alpha,0.0)); gl_SampleMask[0]=alpha_stochastic_mask(test_alpha); }'''),(0x8B30,old_helper),(0x8B30,old_engine)])
# per pixel, the samples of the stochastic depth nearer than the opaque depth, as bits
readmask=program([(0x8B31,screen_vertex),(0x8B30,'''#version 420
uniform sampler2DMS st_depth; uniform sampler2D opaque; uniform int samples; out uvec4 bits;
void main(){ ivec2 px=ivec2(gl_FragCoord.xy); float o=texelFetch(opaque,px,0).r; uint m=0u;
  for(int s=0;s<samples;++s){ if(texelFetch(st_depth,px,s).r<o) m|=1u<<uint(s); } bits=uvec4(m,0u,0u,0u); }''')])

drawbuffers=fn('glDrawBuffers',None,I,C.POINTER(U))
def tex2d(fmt,ext_fmt,ext_type,data=None,w=W,h=H):
    t=U();gentex(1,C.byref(t));bindtex(TEX2D,t);teximage(TEX2D,0,fmt,w,h,0,ext_fmt,ext_type,data)
    texparam(TEX2D,MIN,NEAREST);texparam(TEX2D,MAG,NEAREST);texparam(TEX2D,0x813D,0);return t
def texms(fmt,n):
    t=U();gentex(1,C.byref(t));bindtex(TEXMS,t);teximage_ms(TEXMS,n,fmt,W,H,1);return t
def fbo(*attachments):
    f=U();genfbo(1,C.byref(f));bindfbo(FB,f)
    for point,target,t in attachments: attachtex(FB,point,target,t,0)
    assert status(FB)==0x8CD5,hex(status(FB));return f
def bind_unit(unit,target,t): activetex(0x84C0+unit);bindtex(target,t)
def clearf(index,values): clearbuffer(0x1800,index,(C.c_float*4)(*values))

opaque_depth=tex2d(D24,DEPTH_COMP,FLOAT)
opaque_fbo=fbo((DEPTH_ATT,TEX2D,opaque_depth))
scene=tex2d(RGBA16F,RGBA,FLOAT)
scene_fbo=fbo((COLOR0,TEX2D,scene))
bits_tex=tex2d(R32UI,RED_INT,UINT)
bits_fbo=fbo((COLOR0,TEX2D,bits_tex))
mask_tex={}
for s,t in tables.items():
    data=(U*len(tables_tex[s]))(*tables_tex[s]);mask_tex[s]=tex2d(R32UI,RED_INT,UINT,data,SEEDS,s+1+NOISE_ROWS)
old_mask_tex={s:tex2d(R32UI,RED_INT,UINT,(U*len(t))(*t),SEEDS,s+1) for s,t in tables.items()}   # the old pipeline's: the mask rows alone
targets={}
for n in (2,4,8):
    accum=texms(RGBA16F,n);trans=texms(RG16F,n);depth=texms(D24,n)
    targets[n]=(fbo((COLOR0,TEXMS,accum),(COLOR1,TEXMS,trans),(DEPTH_ATT,TEXMS,depth)),accum,trans,depth)
COLOR2=0x8CE2;R8=0x8229
assert geterror()==0

def set_opaque(z,occluder=None,slope=0.,slope_y=0.):
    """The scene depth: z everywhere, or the sloped plane, or z with an occluder (x0, depth)."""
    bindfbo(FB,opaque_fbo);drawbuffer(NONE);depthmask(1);enable(DEPTH_TEST);depthfunc(ALWAYS)
    cleardepth(z);clear(0x100)
    if slope or slope_y:
        use(shade);uniform1f(loc(shade,b'z'),z);uniform1f(loc(shade,b'slope'),slope);uniform1f(loc(shade,b'slope_y'),slope_y);draw(4,0,3)
    if occluder:
        x0,zo=occluder;fn('glEnable',None,U)(0x0C11);fn('glScissor',None,I,I,I,I)(x0,0,W-x0,H)
        cleardepth(zo);clear(0x100);disable(0x0C11)
    depthfunc(LEQUAL)

# GPU timers of the passes (the bench, scripts/perf/bench_stochastic_resolve.py, sets TIMERS to a dict): a no-op otherwise
TIMERS=None
class timed:
    def __init__(self,name): self.name=name;self.q=None
    def __enter__(self):
        if TIMERS is not None:
            self.q=U();fn('glGenQueries',None,I,C.POINTER(U))(1,C.byref(self.q));fn('glBeginQuery',None,U,U)(0x88BF,self.q)
    def __exit__(self,*a):
        if self.q is not None:
            fn('glEndQuery',None,U)(0x88BF);TIMERS.setdefault(self.name,[]).append(self.q)
def read_timers():
    """the nanoseconds of every pass of the frames since the last call, by pass name: [[frame 1, frame 2, ...] ...]"""
    out={}
    for k,qs in TIMERS.items():
        vals=[]
        for q in qs:
            r=C.c_uint64();fn('glGetQueryObjectui64v',None,U,U,C.POINTER(C.c_uint64))(q,0x8866,C.byref(r));vals.append(r.value)
            fn('glDeleteQueries',None,I,C.POINTER(U))(1,C.byref(q))
        out[k]=vals
    TIMERS.clear();return out
LAYER_SCISSOR=None   # (x0, x1) or (x0, x1, y0, y1): the layers cover these columns (rectangle) only (the temporal tests move it)
SCISSOR_SEEDS=None   # with LAYER_SCISSOR: only the layers of these seeds are cut (None: all)
TEMPORAL_FRAME=[0]   # the frame in the masks' seeds (st_frame)
def layer(prog,z,alpha=0.,rgb=(1.,1.,1.),seed=0,glow=0.,slope=0.,slope_y=0.):
    if isinstance(slope,tuple): slope,slope_y=slope
    use(prog);uniform1f(loc(prog,b'z'),z);uniform1f(loc(prog,b'slope'),slope);uniform1f(loc(prog,b'slope_y'),slope_y)
    if prog==variant or prog in VARIANT_OLD.values(): uniform1f(loc(prog,b'test_alpha'),alpha);uniform1i(loc(prog,b'st_seed'),seed);uniform1i(loc(prog,b'st_frame'),TEMPORAL_FRAME[0])
    elif prog==shade: uniform1f(loc(prog,b'test_alpha'),alpha);uniform3f(loc(prog,b'test_rgb'),*rgb)
    else: uniform1f(loc(prog,b'test_glow'),glow)
    cut=LAYER_SCISSOR and (SCISSOR_SEEDS is None or seed in SCISSOR_SEEDS)
    if cut:
        enable(0x0C11);r=LAYER_SCISSOR   # (x0, x1) the columns, or (x0, x1, y0, y1) a rectangle
        y0,y1=(r[2],r[3]) if len(r)==4 else (0,H)
        fn('glScissor',None,I,I,I,I)(r[0],y0,r[1]-r[0],y1-y0)
    draw(4,0,3)
    if cut: disable(0x0C11)

def spread(n,prog):
    f=targets[n][0];bindfbo(FB,f);drawbuffer(NONE);disable(BLEND);disable(POLY_OFFSET)
    enable(DEPTH_TEST);depthfunc(ALWAYS);depthmask(1)
    use(prog);bind_unit(0,TEX2D,opaque_depth);uniform1i(loc(prog,b'depthMap'),0);draw(4,0,3)
    depthfunc(LEQUAL)

def stochastic(layers,total,n,scene_rgba=(0.,0.,0.,0.),use_offset=True,read_bits=None,before=False,temporal=None,pipe='new',readback=True):
    """LLDrawPoolAlpha::renderStochastic over `layers` [(z, alpha, rgb, seed, glow, slope)], then
    the composite over a scene of `scene_rgba`; returns the scene's pixels (RGBA float). pipe 'old': the passes of the viewer
    before the 2026-11-14 upgrade (OLD_REF: S = 8, the hashed dither, the temporal pair without grain, the composite reading the
    history), for the before / after comparison."""
    old=pipe=='old'
    ref=pipe=='ref'   # the passes of REF_REF (the commit before the mask): the image and the history are compared with it
    V=VARIANT_OLD[variant] if old else variant
    CMP=old_composite if old else (ref_composite if ref else composite)
    f,accum,trans,depth=targets[n];iterations=total//n
    bindfbo(FB,f);enable(MULTISAMPLE);disable(0x0C11)
    drawbuffer(COLOR1);clearf(0,(1.,0.,0.,0.));drawbuffer(COLOR0);clearf(0,(0.,0.,0.,0.))
    spread_prog,offsets=(expand_before,BEFORE_OFFSET) if before else (expand,VIEWER_OFFSET)
    spread(n,spread_prog)
    use(V);uniform1i(loc(V,b'st_samples_hw'),n);uniform1i(loc(V,b'st_samples'),total)
    bind_unit(1,TEX2D,(old_mask_tex if old else mask_tex)[total]);uniform1i(loc(V,b'stoch_masks'),1)
    def poly(i):
        if use_offset: enable(POLY_OFFSET);offset(offsets[0],offsets[i])
        else: disable(POLY_OFFSET)
    # 1. total alpha
    bindfbo(FB,f);drawbuffer(COLOR1);enable(BLEND);blendsep(ZERO,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
    depthmask(0);poly(1);use(V);uniform1i(loc(V,b'st_mode'),1)
    with timed('total alpha'):
        for z,a,rgb,seed,glow,slope in layers: layer(V,z,a,seed=seed,slope=slope)
    bits=[]
    for it in range(iterations):
        if it: spread(n,spread_prog)
        # 2. stochastic depth
        bindfbo(FB,f);drawbuffer(NONE);depthmask(1);poly(1)
        use(V);uniform1i(loc(V,b'st_mode'),2)
        with timed('stochastic depth'):
            for z,a,rgb,seed,glow,slope in layers: layer(V,z,a,seed=seed,slope=slope)
        if read_bits:
            bindfbo(FB,bits_fbo);disable(BLEND);disable(DEPTH_TEST);disable(POLY_OFFSET);disable(MULTISAMPLE)
            use(readmask);bind_unit(2,TEXMS,depth);bind_unit(3,TEX2D,opaque_depth)
            uniform1i(loc(readmask,b'st_depth'),2);uniform1i(loc(readmask,b'opaque'),3);uniform1i(loc(readmask,b'samples'),n)
            draw(4,0,3);px=(U*(W*H))();readpixels(0,0,W,H,RED_INT,UINT,px);bits.append(list(px))
            enable(DEPTH_TEST);enable(MULTISAMPLE);bindfbo(FB,f)
        # 3. accumulation, then the glow of the glowing faces
        drawbuffer(COLOR0);depthmask(0);enable(BLEND);blendsep(SRC_ALPHA,ONE,ONE,ONE);poly(2)
        with timed('accumulation'):
            for z,a,rgb,seed,glow,slope in layers: layer(shade,z,a,rgb,seed=seed,slope=slope)
        drawbuffer(COLOR1);blendsep(ONE,ONE,ONE,ONE);poly(3)
        for z,a,rgb,seed,glow,slope in layers:
            if glow>0: layer(glowprog,z,glow=glow,seed=seed,slope=slope)
    disable(POLY_OFFSET);disable(MULTISAMPLE)
    # 3b. the temporal accumulation
    if temporal is not None and old:
        # the old pair: this frame's samples averaged once (RGBA32F), then the resolve (the stochastic depth read in it)
        nxt=1-temporal['idx']
        with timed('measure'):
            bindfbo(FB,temporal['curfbo']);drawbuffer(COLOR0);disable(BLEND);disable(DEPTH_TEST)
            use(old_current);bind_unit(4,TEXMS,accum);uniform1i(loc(old_current,b'alpha_st_accum'),4)
            uniform1i(loc(old_current,b'st_samples_hw'),n);uniform1i(loc(old_current,b'st_samples'),total)
            draw(4,0,3)
        with timed('blend'):
            bindfbo(FB,temporal['fbo'][nxt]);drawbuffer(COLOR0)
            use(old_resolve_full);bind_unit(6,TEXMS,depth);bind_unit(7,TEX2D,temporal['tex'][temporal['idx']]);bind_unit(8,TEX2D,temporal['cur'])
            uniform1i(loc(old_resolve_full,b'alpha_st_current'),8);uniform1i(loc(old_resolve_full,b'alpha_st_depth'),6);uniform1i(loc(old_resolve_full,b'alpha_st_history'),7)
            uniform1i(loc(old_resolve_full,b'st_samples_hw'),n)
            fn('glUniformMatrix4fv',None,I,I,C.c_ubyte,C.POINTER(C.c_float))(loc(old_resolve_full,b'stoch_reproj'),1,0,(C.c_float*16)(*temporal['reproj']))
            uniform1f(loc(old_resolve_full,b'stoch_weight'),temporal.get('weight',1/8));uniform1i(loc(old_resolve_full,b'stoch_history_valid'),1 if temporal['valid'] else 0)
            draw(4,0,3)
        temporal['idx']=nxt;temporal['valid']=True
    # 3b. the temporal accumulation: this frame's samples measured (with the mask), blended with the history by the grain; 'ref':
    # the commit before the mask, which then smooths in a pass of its own
    elif temporal is not None:
        nxt=1-temporal['idx']
        CUR=ref_current if ref else current_prog;RES=ref_resolve if ref else resolve_prog
        _t=timed('measure');_t.__enter__()
        bindfbo(FB,temporal['curfbo']);drawbuffers(2 if ref else 3,(U*3)(COLOR0,COLOR1,COLOR2));disable(BLEND);disable(DEPTH_TEST)
        use(CUR);bind_unit(4,TEXMS,accum);bind_unit(5,TEXMS,trans);bind_unit(6,TEXMS,depth);bind_unit(11,TEX2D,opaque_depth);uniform1i(loc(CUR,b'depthMap'),11)
        uniform1i(loc(CUR,b'alpha_st_accum'),4);uniform1i(loc(CUR,b'alpha_st_trans'),5);uniform1i(loc(CUR,b'alpha_st_depth'),6)
        uniform1i(loc(CUR,b'st_samples_hw'),n);uniform1i(loc(CUR,b'st_samples'),total)
        draw(4,0,3)
        _t.__exit__();_t=timed('blend');_t.__enter__()
        bindfbo(FB,temporal['fbo'][nxt]);drawbuffers(2,(U*2)(COLOR0,COLOR1))
        use(RES);bind_unit(7,TEX2D,temporal['tex'][temporal['idx']]);bind_unit(8,TEX2D,temporal['cur']);bind_unit(9,TEX2D,temporal['aux'])
        bind_unit(10,TEX2D,temporal['extra'][temporal['idx']])
        uniform1i(loc(RES,b'alpha_st_current'),8);uniform1i(loc(RES,b'alpha_st_aux'),9);uniform1i(loc(RES,b'alpha_st_history'),7)
        uniform1i(loc(RES,b'alpha_st_extra'),10)
        if not ref: bind_unit(20,TEX2D,temporal['mask']);uniform1i(loc(RES,b'alpha_st_mask'),20)
        fn('glUniformMatrix4fv',None,I,I,C.c_ubyte,C.POINTER(C.c_float))(loc(RES,b'stoch_reproj'),1,0,(C.c_float*16)(*temporal['reproj']))
        uniform1f(loc(RES,b'stoch_weight'),temporal.get('weight',1/8));uniform1i(loc(RES,b'stoch_history_valid'),1 if temporal['valid'] else 0)
        draw(4,0,3)
        _t.__exit__()
        temporal['idx']=nxt;temporal['valid']=True
        if ref:
            _t=timed('smooth');_t.__enter__()
            bindfbo(FB,temporal['smoothfbo']);drawbuffer(COLOR0)
            use(ref_smooth);bind_unit(7,TEX2D,temporal['tex'][temporal['idx']]);bind_unit(9,TEX2D,temporal['aux']);bind_unit(10,TEX2D,temporal['extra'][temporal['idx']])
            uniform1i(loc(ref_smooth,b'alpha_st_history'),7);uniform1i(loc(ref_smooth,b'alpha_st_aux'),9);uniform1i(loc(ref_smooth,b'alpha_st_extra'),10)
            uniform1f(loc(ref_smooth,b'stoch_weight'),temporal.get('weight',1/8))
            draw(4,0,3)
            _t.__exit__()
    # 4. composite over the scene, then the glow into its alpha
    bindfbo(FB,scene_fbo);drawbuffer(COLOR0);clearcolor(*scene_rgba);clear(0x4000)
    disable(DEPTH_TEST);enable(BLEND);blendsep(ONE,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
    use(CMP);bind_unit(4,TEXMS,accum);bind_unit(5,TEXMS,trans)
    uniform1i(loc(CMP,b'alpha_st_accum'),4);uniform1i(loc(CMP,b'alpha_st_trans'),5)
    uniform1i(loc(CMP,b'st_temporal'),1 if temporal is not None else 0)
    if old or ref:
        resolved=(temporal['tex'][temporal['idx']] if old else temporal['cur']) if temporal is not None else opaque_depth   # 'ref': the smooth's colour drawn
        bind_unit(7,TEX2D,resolved);uniform1i(loc(CMP,b'alpha_st_resolved'),7)
        if ref: uniform1i(loc(CMP,b'st_moments'),0)
    else:
        # the smooth is in the composite: the history (this frame's, after the blend), the aux, the extra and the mask
        for unit,key,name in ((21,'history',b'alpha_st_history'),(22,'aux',b'alpha_st_aux'),(23,'extra',b'alpha_st_extra'),(24,'mask',b'alpha_st_mask')):   # units the other tests' bindings (up to 12) do not use
            if temporal is None: t=opaque_depth
            elif key=='history': t=temporal['tex'][temporal['idx']]
            elif key=='extra': t=temporal['extra'][temporal['idx']]
            else: t=temporal[key]
            bind_unit(unit,TEX2D,t);uniform1i(loc(CMP,name),unit)
        uniform1f(loc(CMP,b'stoch_weight'),(temporal or {}).get('weight',1/8))
    uniform1i(loc(CMP,b'st_samples_hw'),n);uniform1i(loc(CMP,b'st_samples'),total);uniform1i(loc(CMP,b'st_glow'),0)
    with timed('composite'):
        draw(4,0,3)
        blendsep(ZERO,ONE,ONE,ONE);uniform1i(loc(CMP,b'st_glow'),1)
        draw(4,0,3)
    enable(DEPTH_TEST);assert geterror()==0
    if not readback: return None
    out=(C.c_float*(W*H*4))();readbuffer(COLOR0);readpixels(0,0,W,H,RGBA,FLOAT,out)
    pixels=[tuple(out[i*4:i*4+4]) for i in range(W*H)]
    return (pixels,bits) if read_bits else pixels

def L(z,a,rgb=(1.,1.,1.),seed=0,glow=0.,slope=0.): return (z,a,rgb,seed,glow,slope)
# <<bench-shared:end>>
# ---- the alpha programs compile, plain and as their ALPHA_STOCHASTIC variant, with the defines the
# viewer gives them (llviewershadermgr.cpp), the indexed-texture lookup declared as the viewer
# injects it; compile only (the shared functions are linked from other objects in the viewer)
alpha_programs={
    'class2/deferred/alphaF.glsl':['USE_VERTEX_COLOR','HAS_ALPHA_MASK','USE_INDEXED_TEX','HAS_SUN_SHADOW'],
    'class1/deferred/fullbrightF.glsl':['HAS_ALPHA_MASK','IS_ALPHA','HAS_DIFFUSE_LOOKUP'],
    'class2/deferred/pbralphaF.glsl':['DIFFUSE_ALPHA_MODE 1','HAS_NORMAL_MAP','HAS_SPECULAR_MAP','HAS_EMISSIVE_MAP','USE_VERTEX_COLOR','HAS_SUN_SHADOW'],
    'class3/deferred/materialF.glsl':['DIFFUSE_ALPHA_MODE 1','HAS_NORMAL_MAP','HAS_SPECULAR_MAP','HAS_ALPHA_MASK','HAS_SUN_SHADOW'],
}
# the GLTF scene's blended variants (llviewershadermgr.cpp make_gltf_variant: ALPHA_BLEND, lit and
# UNLIT, single and MULTI_UV) take part in the same engine
gltf_blend=['MAX_NODES_PER_GLTF_OBJECT 64','MAX_MATERIALS_PER_GLTF_OBJECT 64','MAX_UBO_VEC4S 1024','ALPHA_BLEND']
alpha_programs_all=list(alpha_programs.items())+[
    ('class1/gltf/pbrmetallicroughnessF.glsl',gltf_blend+['HAS_SUN_SHADOW']),
    ('class1/gltf/pbrmetallicroughnessF.glsl',gltf_blend+['HAS_SUN_SHADOW','MULTI_UV']),
    ('class1/gltf/pbrmetallicroughnessF.glsl',gltf_blend+['UNLIT']),
]
compiled=0
for rel,defines in alpha_programs_all:
    text=(root/'indra/newview/app_settings/shaders'/rel).read_text()
    for as_variant in (False,True):
        extra=''.join('#define '+(d if ' ' in d else d+' 1')+'\n' for d in defines)
        if as_variant: extra+='#define ALPHA_STOCHASTIC 1\n'
        extra+='#define DIFFUSE_ALPHA_MODE_NONE 0\n#define DIFFUSE_ALPHA_MODE_BLEND 1\n#define DIFFUSE_ALPHA_MODE_MASK 2\n#define DIFFUSE_ALPHA_MODE_EMISSIVE 3\n'
        extra+='vec4 diffuseLookup(vec2 texcoord);\n'
        source='#version 420\n'+text.replace('#extension GL_ARB_shader_image_load_store : enable','').replace('/*[EXTRA_CODE_HERE]*/',extra)
        shader=create_shader(0x8B30);src=C.c_char_p(source.encode());source_shader(shader,1,C.byref(src),None);compile_shader(shader)
        ok=I();shader_iv(shader,0x8B81,C.byref(ok));log=C.create_string_buffer(16384);shader_log(shader,len(log),None,log)
        assert ok.value,(rel,'stochastic' if as_variant else 'plain',log.value.decode()[:2000])
        fn('glDeleteShader',None,U)(shader);compiled+=1
print(f'{compiled} alpha fragment shaders compile, plain and as their stochastic variants')
# (the GLTF blended variants run in the mask model below: lit and unlit, the viewer's own shader)

# ---- the sun N.L helper (sunShadowedDiffuse): every program that calls it, and deferredUtil.glsl that
# defines it, compile in every combination of blend / no blend, with / without HAS_SUN_SHADOW,
# with / without SHADOW_NL_MULTIPLY (compile only)
nl_matrix=[]
for rel,base in (('class2/deferred/alphaF.glsl',['USE_VERTEX_COLOR','HAS_ALPHA_MASK','USE_INDEXED_TEX']),
                 ('class3/deferred/materialF.glsl',['HAS_NORMAL_MAP','HAS_SPECULAR_MAP','HAS_ALPHA_MASK']),
                 ('class3/deferred/softenLightF.glsl',[]),
                 ('class1/deferred/deferredUtil.glsl',[])):
    text=(root/'indra/newview/app_settings/shaders'/rel).read_text()
    for blend in ((0,1) if 'materialF' in rel else (1,)):
        for sun in (0,1):
            for mult in (0,1):
                defs=list(base)+(['DIFFUSE_ALPHA_MODE 1'] if blend else ['DIFFUSE_ALPHA_MODE 0'])
                if sun: defs.append('HAS_SUN_SHADOW')
                if mult: defs.append('SHADOW_NL_MULTIPLY')
                extra=''.join('#define '+(d if ' ' in d else d+' 1')+'\n' for d in defs)
                extra+='#define DIFFUSE_ALPHA_MODE_NONE 0\n#define DIFFUSE_ALPHA_MODE_BLEND 1\n#define DIFFUSE_ALPHA_MODE_MASK 2\n#define DIFFUSE_ALPHA_MODE_EMISSIVE 3\n'
                extra+='#define GBUFFER_FLAG_SKIP_ATMOS 0.0\n#define GBUFFER_FLAG_HAS_ATMOS 0.34\n#define GBUFFER_FLAG_HAS_PBR 0.67\n#define GBUFFER_FLAG_HAS_HDRI 1.0\n#define GET_GBUFFER_FLAG(data, flag) (abs(data-flag)< 0.1)\nstruct GBufferInfo { vec4 albedo; vec4 specular; vec3 normal; vec4 emissive; float gbufferFlag; float envIntensity; };\n'
                extra+='vec4 diffuseLookup(vec2 texcoord);\n'
                source='#version 420\n'+text.replace('#extension GL_ARB_shader_image_load_store : enable','').replace('/*[EXTRA_CODE_HERE]*/',extra)
                shader=create_shader(0x8B30);src=C.c_char_p(source.encode());source_shader(shader,1,C.byref(src),None);compile_shader(shader)
                ok=I();shader_iv(shader,0x8B81,C.byref(ok));log=C.create_string_buffer(16384);shader_log(shader,len(log),None,log)
                assert ok.value,(rel,defs,log.value.decode()[:2000])
                fn('glDeleteShader',None,U)(shader);nl_matrix.append((rel,blend,sun,mult))
print(f'{len(nl_matrix)} sun N.L shader variants compile (alphaF, materialF, softenLightF, deferredUtil; blend, sun shadow, multiply)')

checks=0

# ---- mask coverage: per sample, exactly the model's bits; R samples in all, mean alpha S
for total,n in ((8,8),(4,4)):
    for a in (0.0,0.3,0.5,0.77,1.0):
        set_opaque(1.0)
        _,bits=stochastic([L(0.5,a,seed=12345)],total,n,read_bits=True)
        counts=[]
        for y in range(H):
            for x in range(W):
                full=model_mask(x,y,12345,0,a,total,tables[total])
                for it in range(total//n):
                    want=(full>>(it*n))&((1<<n)-1)
                    got=bits[it][y*W+x]
                    assert got==want,('coverage bits',total,n,a,x,y,it,hex(got),hex(want))
                counts.append(bin(full).count('1'))
        lo=math.floor(a*total);assert all(c in (lo,min(lo+1,total)) for c in counts),('R',total,a,set(counts))
        mean=sum(counts)/len(counts);assert abs(mean-a*total)<=0.05*total+1e-9,('mean R',total,a,mean)
        checks+=1
print(f'{checks} coverage cases: every sample as the model predicts (8 and 4, each in one pass), R = floor or ceil of alpha S, mean alpha S')


# ---- the GLTF scene's blended variants (pbrmetallicroughnessF.glsl itself, ALPHA_BLEND + ALPHA_STOCHASTIC, lit
# with HAS_SUN_SHADOW as the viewer builds it, and UNLIT): the alpha-only output through the same mask model.
# The lit shader multiplies in the vertex alpha a second time (a = basecolor.a * vertex_color.a), the unlit one
# does not; the model is given the alpha each shaded output carries. The lighting functions the shader links
# in the viewer are stand-ins here (they are not reached before the alpha-only return).
GLTF_STUBS='''#version 420
void mirrorClip(vec3 pos) {}
void waterClip(vec3 pos) {}
vec4 encodeNormal(vec3 n, float env, float flag) { return vec4(n, env); }
vec3 srgb_to_linear(vec3 c) { return c; }
vec3 linear_to_srgb(vec3 c) { return c; }
void calcAtmosphericVarsLinear(vec3 p, vec3 n, vec3 l, out vec3 sunlit, out vec3 amblit, out vec3 atten, out vec3 additive)
{ sunlit = vec3(1.0); amblit = vec3(0.3); atten = vec3(1.0); additive = vec3(0.0); }
vec4 applySkyAndWaterFog(vec3 pos, vec3 additive, vec3 atten, vec4 color) { return color; }
void calcHalfVectors(vec3 lv, vec3 n, vec3 v, out vec3 h, out vec3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist)
{ l = normalize(lv); h = normalize(l + v); nh = nl = nv = vh = 1.0; lightDist = length(lv); }
float calcLegacyDistanceAttenuation(float distance, float falloff) { return 1.0; }
float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen) { return 1.0; }
void sampleReflectionProbes(inout vec3 ambenv, inout vec3 glossenv, vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 amblit)
{ ambenv = amblit; glossenv = vec3(0.5); }
void calcDiffuseSpecular(vec3 baseColor, float metallic, inout vec3 diffuseColor, inout vec3 specularColor) { diffuseColor = baseColor; specularColor = vec3(0.04); }
vec3 pbrBaseLight(vec3 diffuseColor, vec3 specularColor, float metallic, vec3 pos, vec3 norm, float perceptualRoughness, vec3 light_dir,
                  vec3 sunlit, float scol, vec3 radiance, vec3 irradiance, vec3 colorEmissive, float ao, vec3 additive, vec3 atten)
{ return diffuseColor * sunlit * scol; }
vec3 pbrCalcPointLightOrSpotLight(vec3 diffuseColor, vec3 specularColor, float perceptualRoughness, float metallic, vec3 n, vec3 p, vec3 v,
                                  vec3 lp, vec3 ld, vec3 lightColor, float lightSize, float falloff, float is_pointlight, float ambiance)
{ return vec3(0.0); }
'''
GLTF_VARYINGS='''out vec3 vary_position; out vec4 vertex_color; out vec2 base_color_uv; out vec2 emissive_uv;
out vec3 vary_normal; out vec3 vary_tangent; flat out float vary_sign; out vec2 normal_uv; out vec2 metallic_roughness_uv;
out vec2 occlusion_uv; out vec3 vary_fragcoord;
void gltf_varyings(vec2 p, vec2 uv, float alpha, float zz)
{
    vary_position = vec3(p, -2.0); vertex_color = vec4(1.0, 1.0, 1.0, alpha);
    base_color_uv = uv; emissive_uv = uv; normal_uv = uv; metallic_roughness_uv = uv; occlusion_uv = uv;
    vary_normal = vec3(0.0, 0.0, 1.0); vary_tangent = vec3(1.0, 0.0, 0.0); vary_sign = 1.0; vary_fragcoord = vec3(p, 1.0);
    gl_Position = vec4(p, zz, 1.0);
}
'''
GLTF_VERTEX='#version 420\n'+GLTF_VARYINGS+'''uniform float z; uniform float slope; uniform float slope_y; uniform float test_alpha;
void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2.0-1.0; gltf_varyings(p, p*0.5+0.5, test_alpha, z+slope*p.x+slope_y*p.y); }'''
GLTF_LATTICE_VERTEX='#version 420\n'+GLTF_VARYINGS+'''uniform float z; uniform int cells; uniform vec2 goff; uniform float test_alpha;
void main(){ int cell=gl_VertexID/6; int k=gl_VertexID%6; int cx=cell%cells; int cy=cell/cells;
 vec2 c[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,0),vec2(1,1),vec2(0,1));
 vec2 p=(vec2(cx,cy)+c[k])/float(cells)*2.5-1.25+goff; gltf_varyings(p, c[k], test_alpha, z); }'''
gltf_text=(root/'indra/newview/app_settings/shaders/class1/gltf/pbrmetallicroughnessF.glsl').read_text()
def gltf_alpha_only(unlit,vertex=GLTF_VERTEX,text=gltf_text):
    """make_gltf_variant's blended variant as its alpha-only (ALPHA_STOCHASTIC) program"""
    defines=['MAX_NODES_PER_GLTF_OBJECT 64','MAX_MATERIALS_PER_GLTF_OBJECT 64','MAX_UBO_VEC4S 1024','ALPHA_BLEND','ALPHA_STOCHASTIC']
    defines+=['UNLIT'] if unlit else ['HAS_SUN_SHADOW']
    extra=''.join('#define '+(d if ' ' in d else d+' 1')+'\n' for d in defines)
    src='#version 420\n'+text.replace('/*[EXTRA_CODE_HERE]*/',extra)
    prog=program([(0x8B31,vertex),(0x8B30,src),(0x8B30,GLTF_STUBS),(0x8B30,helper),(0x8B30,engine)])
    use(prog);uniform1i(loc(prog,b'gltf_material_id'),-1);uniform1i(loc(prog,b'diffuseMap'),12)
    return prog
gltf_white=U();gentex(1,C.byref(gltf_white));bindtex(TEX2D,gltf_white)
teximage(TEX2D,0,0x8058,2,2,0,RGBA,0x1401,(C.c_ubyte*16)(*([255]*16)))
texparam(TEX2D,MIN,NEAREST);texparam(TEX2D,MAG,NEAREST);texparam(TEX2D,0x2802,0x812D);texparam(TEX2D,0x2803,0x812D)   # clamp to the (transparent) border
bind_unit(12,TEX2D,gltf_white)
gltf_variants={'lit':gltf_alpha_only(False),'unlit':gltf_alpha_only(True)}
def gltf_alpha(kind,a):
    a32=f32(a)
    return f32(a32*a32) if kind=='lit' else a32

gltf_cases=0
for kind,gprog in gltf_variants.items():
    saved_variant=variant;variant=gprog
    try:
        for total,n in ((8,8),(4,4)):
            for a in (0.0,0.3,0.5,0.77,1.0):
                set_opaque(1.0)
                _,bits=stochastic([L(0.5,a,seed=12345)],total,n,read_bits=True)
                eff=gltf_alpha(kind,a)
                for y in range(H):
                    for x in range(W):
                        full=model_mask(x,y,12345,0,eff,total,tables[total])
                        for it in range(total//n):
                            want=(full>>(it*n))&((1<<n)-1)
                            assert bits[it][y*W+x]==want,('GLTF coverage bits',kind,total,n,a,x,y,it,hex(bits[it][y*W+x]),hex(want))
                gltf_cases+=1
        # total alpha of two layers: 1 - prod(1 - alpha_eff), whatever the visibility
        set_opaque(1.0)
        pix=stochastic([L(0.3,0.6,seed=7),L(0.6,0.5,seed=9)],8,8)
        want=1-(1-gltf_alpha(kind,0.6))*(1-gltf_alpha(kind,0.5))
        worst=max(abs(p[c]-want) for p in pix for c in range(3))
        assert worst<2e-3,('GLTF total alpha',kind,worst,want);gltf_cases+=1
        # mode 3 (the depth of field pass, the rigged depth restore): every sample, under the cut nothing
        for a,cut in ((0.2,0.33),(0.5,0.33),(0.004,0.004),(0.003,0.004)):
            eff=gltf_alpha(kind,a);n=4;set_opaque(1.0)
            f=targets[n][0];bindfbo(FB,f);enable(MULTISAMPLE);drawbuffer(NONE);spread(n,expand)
            disable(BLEND);enable(DEPTH_TEST);depthmask(1);disable(POLY_OFFSET)
            use(gprog);uniform1i(loc(gprog,b'st_mode'),3);uniform1f(loc(gprog,b'st_min_alpha'),cut)
            uniform1i(loc(gprog,b'st_samples_hw'),n);uniform1i(loc(gprog,b'st_samples'),n)
            layer(gprog,0.5,a,seed=1)
            bindfbo(FB,bits_fbo);disable(DEPTH_TEST);disable(MULTISAMPLE)
            use(readmask);bind_unit(2,TEXMS,targets[n][3]);bind_unit(3,TEX2D,opaque_depth)
            uniform1i(loc(readmask,b'st_depth'),2);uniform1i(loc(readmask,b'opaque'),3);uniform1i(loc(readmask,b'samples'),n)
            draw(4,0,3);px=(U*(W*H))();readpixels(0,0,W,H,RED_INT,UINT,px)
            enable(DEPTH_TEST);enable(MULTISAMPLE)
            expect=(1<<n)-1 if eff>=f32(cut) else 0
            assert set(px)=={expect},('GLTF depth-only mode',kind,a,cut,set(px),expect);gltf_cases+=1
    finally:
        variant=saved_variant
print(f"{gltf_cases} GLTF blended variant cases (the viewer's pbrmetallicroughnessF.glsl, lit and unlit): every sample as the model predicts "
      '(S = 8, 4), total alpha, and the depth-only mode with its cut')

# different face seeds: unrelated masks; the same seed: the same mask
set_opaque(1.0)
_,b1=stochastic([L(0.5,0.5,seed=1)],8,8,read_bits=True)
_,b2=stochastic([L(0.5,0.5,seed=2)],8,8,read_bits=True)
_,b1again=stochastic([L(0.5,0.5,seed=1)],8,8,read_bits=True)
same=sum(1 for p,q in zip(b1[0],b2[0]) if p==q)/(W*H)
assert b1==b1again,'the same seed gave another mask'
assert same<0.1,('seeds 1 and 2 share masks on',same)
checks+=1
print(f'face seeds: the same seed repeats its masks; seeds 1 and 2 share the mask on {same*100:.1f} % of pixels (1/70 expected)')

# ---- total alpha: white layers over black give the total alpha whatever the visibility
set_opaque(1.0)
for total,n in ((8,8),(4,4)):
    pix=stochastic([L(0.3,0.5,seed=7),L(0.6,0.25,seed=9),L(0.45,0.1,seed=11)],total,n)
    want=1-(1-0.5)*(1-0.25)*(1-0.1)
    worst=max(abs(p[c]-want) for p in pix for c in range(3))
    assert worst<2e-3,('total alpha',total,worst);checks+=1
print(f'total alpha: 1 - prod(1 - alpha) = {want:.4f} on every pixel within 2e-3 (S = 8, 4)')

# ---- visibility: red alpha 0.5 in front of green alpha 0.5 is exact at S = 8
for total,n in ((8,8),):
    pix=stochastic([L(0.6,0.5,(0.,1.,0.),seed=3),L(0.3,0.5,(1.,0.,0.),seed=4)],total,n)
    worst=max(max(abs(p[0]-0.5),abs(p[1]-0.25),abs(p[2])) for p in pix)
    assert worst<2e-3,('two layers',total,worst);checks+=1
# a noisy pair (alpha 0.3 over 0.6): the mean over the pixels converges to the exact image
pix=stochastic([L(0.3,0.3,(1.,0.,0.),seed=5),L(0.6,0.6,(0.,1.,0.),seed=6)],8,8)
exact=(0.3,0.6*0.7,0.)
mean=[sum(p[c] for p in pix)/len(pix) for c in range(3)]
rmse=math.sqrt(sum((p[c]-exact[c])**2 for p in pix for c in range(3))/(3*len(pix)))
assert all(abs(m-e)<0.01 for m,e in zip(mean,exact)),('mean',mean,exact)
assert rmse<0.05,('rmse',rmse);checks+=1
print(f'visibility: two layers of alpha 0.5 exact at S = 8; alpha 0.3 over 0.6 at S = 8: mean '
      f'({mean[0]:.3f}, {mean[1]:.3f}) for ({exact[0]:.3f}, {exact[1]:.3f}), RMSE {rmse*255:.1f}/255')

# an opaque occluder on the right half, at window depth 0.7 (the layers: 0.65 and 0.8), hides the
# back layer there
set_opaque(1.0,occluder=(W//2,0.7))
pix=stochastic([L(0.6,0.5,(0.,1.,0.),seed=3),L(0.3,0.5,(1.,0.,0.),seed=4)],8,8,scene_rgba=(0.,0.,1.,0.))
for y in range(H):
    for x in range(W):
        p=pix[y*W+x]
        want=(0.5,0.25,0.25) if x<W//2 else (0.5,0.,0.5)
        assert all(abs(p[c]-want[c])<2e-3 for c in range(3)),('occluder',x,y,p,want)
checks+=1
print('occlusion: the layer behind the opaque surface leaves no colour and no coverage')

# ---- glow: the back layer's glow 0.8, visible on half its samples, over a scene glow of 0.5
set_opaque(1.0)
pix=stochastic([L(0.6,0.5,(0.,1.,0.),seed=3,glow=0.8),L(0.3,0.5,(1.,0.,0.),seed=4)],8,8,scene_rgba=(0.,0.,0.,0.5))
want=0.5*(1-0.75)+0.8*0.5
worst=max(abs(p[3]-want) for p in pix)
assert worst<3e-3,('glow',worst);checks+=1
print(f'glow: the scene glow dimmed by the total alpha plus the visible glow: {want:.3f} within 3e-3')

# ---- faces on, just in front of and just behind a sloped opaque surface, against sorted alpha
# (which tests the face's depth at the pixel centre against the opaque depth there, LEQUAL): a
# face on or in front shows on every sample, a face behind leaves the scene exactly as it is. The
# window depth per pixel of a plane of NDC slopes (kx, ky) is kx / W across x and ky / H across
# y; the faces sit a third of the steeper axis' step in front of or behind the plane.
SLOPES=((0.02,0.),(0.,0.02),(0.05,-0.03),(-0.2,0.1),(0.3,0.15))   # the planes stay inside the depth range
def pixel_step(sl): return max(abs(sl[0])/W,abs(sl[1])/H)
def ndc_shift(sl,fraction): return 2.0*fraction*pixel_step(sl)
BLUE=(0.,0.,1.,0.)
def white_everywhere(pix): return max(max(abs(p[0]-1.0),abs(p[1]-1.0),abs(p[2]-1.0)) for p in pix)
for sl in SLOPES:
    set_opaque(0.5,slope=sl[0],slope_y=sl[1])
    for total,n in ((8,8),(4,4)):
        worst=white_everywhere(stochastic([L(0.5,1.0,seed=8,slope=sl)],total,n,scene_rgba=BLUE))
        assert worst<2e-3,('decal on the plane',sl,total,worst)
        worst=white_everywhere(stochastic([L(0.5-ndc_shift(sl,1/3),1.0,seed=8,slope=sl)],total,n,scene_rgba=BLUE))
        assert worst<2e-3,('face in front of the plane',sl,total,worst)
        for fraction in (1/3,1/16):
            behind=stochastic([L(0.5+ndc_shift(sl,fraction),1.0,seed=8,slope=sl)],total,n,scene_rgba=BLUE)
            shown=sum(1 for p in behind if p!=BLUE)
            assert shown==0,('face behind the plane shows through it',sl,fraction,total,shown,W*H)
        checks+=1
print(f'{len(SLOPES)} sloped opaque planes, S = 8, 4: a face on the plane or a third of a pixel\'s depth in front '
      'of it covers every pixel; one a third or a sixteenth of a pixel behind it leaves the scene exactly as sorted alpha does')

# a decal on 60 random planes (inside the depth range), every pixel covered at S = 8
rnd=random.Random(5);tried=0
while tried<60:
    sl=(rnd.uniform(-0.35,0.35),rnd.uniform(-0.18,0.18))
    if abs(sl[0])+abs(sl[1])>0.45: continue
    tried+=1;set_opaque(0.5,slope=sl[0],slope_y=sl[1])
    worst=white_everywhere(stochastic([L(0.5,1.0,seed=100+tried,slope=sl)],8,8,scene_rgba=BLUE))
    assert worst<2e-3,('decal on a random plane',sl,worst)
checks+=1
print('a decal on 60 random sloped planes: every pixel covered (S = 8)')

# the same decal beside a depth edge: an opaque occluder nearer the eye over the right half; the
# pixels next to the edge take the plane's gradient from their own side
sl=(0.05,-0.03)
set_opaque(0.5,slope=sl[0],slope_y=sl[1],occluder=(W//2,0.3))
pix=stochastic([L(0.5,1.0,seed=8,slope=sl)],8,8,scene_rgba=BLUE)
for y in range(H):
    for x in range(W):
        p=pix[y*W+x];want=(1.,1.,1.) if x<W//2 else (0.,0.,1.)
        assert all(abs(p[c]-want[c])<2e-3 for c in range(3)),('decal beside a depth edge',x,y,p)
checks+=1
print('a decal beside a depth edge: covered up to the edge, hidden behind the occluder')

# a half-transparent face in front of the plane with an alpha 1 layer just behind it: the
# composite is the front face over the scene, as sorted alpha draws it
sl=(0.05,-0.03)
set_opaque(0.5,slope=sl[0],slope_y=sl[1])
pix=stochastic([L(0.5+ndc_shift(sl,1/3),1.0,(1.,1.,1.),seed=8,slope=sl),L(0.4,0.5,(1.,0.,0.),seed=9,slope=sl)],8,8,scene_rgba=BLUE)
worst=max(max(abs(p[0]-0.5),abs(p[1]),abs(p[2]-0.5)) for p in pix)
assert worst<2e-3,('front face over a hidden layer',worst);checks+=1
print('a half-transparent face over an alpha 1 layer hidden behind the plane: exactly the face over the scene')

# the regression: the spread and offsets of 1.0.1.81943 let the face behind the plane through (a
# mesh body's blended skin through the dress fitted over it); the check must see it
leaks=[]
for sl in SLOPES:
    set_opaque(0.5,slope=sl[0],slope_y=sl[1])
    behind=stochastic([L(0.5+ndc_shift(sl,1/3),1.0,seed=8,slope=sl)],8,8,scene_rgba=BLUE,before=True)
    leaks.append(sum(1 for p in behind if p!=BLUE))
assert all(k>0 for k in leaks),('the 1.0.1.81943 passes should show the face behind the plane',leaks)
# and without any offset its centre-depth spread loses samples of a face lying on the plane
set_opaque(0.5,slope=0.02)
pix=stochastic([L(0.5,1.0,seed=8,slope=(0.02,0.))],8,8,before=True,use_offset=False)
lost=sum(1 for p in pix if p[0]<0.999)
assert lost>0,'without the offset the centre-depth spread should lose samples of the decal'
checks+=1
print(f'before ({BEFORE_REF}: the centre depth on every sample, offset factor -1): the face behind the plane shows '
      f'on {", ".join(str(k) for k in leaks)} of {W*H} pixels; without the offset a decal loses samples on {lost} pixels')

# ---- a blended legacy material that can glare (materialF.glsl, class3, DIFFUSE_ALPHA_MODE_BLEND with
# normal and specular maps), the viewer's own shader, plain and as its alpha-only variant, over an
# opaque scene, against sorted alpha (the plain program blended SRC_ALPHA, ONE_MINUS_SRC_ALPHA over
# the scene). The lighting functions it links in the viewer are replaced by small stand-ins (a
# reflection of 0.8 grey, a white sun, no local lights): the glare term is materialF's own,
# max(diffuse alpha, legacy environment x env_intensity x specular map alpha) x vertex alpha.
# LLDrawPoolAlpha::TexSetup decides which maps the alpha-only passes see: the face's own normal and
# specular maps, or (before 2026-11-05) none, leaving on those units what another face bound.
MATERIAL_STUBS='''#version 420
vec4 applySkyAndWaterFog(vec3 pos, vec3 additive, vec3 atten, vec4 color) { return color; }
vec3 scaleSoftClipFragLinear(vec3 l) { return l; }
void calcAtmosphericVarsLinear(vec3 p, vec3 n, vec3 l, out vec3 sunlit, out vec3 amblit, out vec3 atten, out vec3 additive)
{ sunlit = vec3(1.0); amblit = vec3(0.3); atten = vec3(1.0); additive = vec3(0.0); }
void calcHalfVectors(vec3 lv, vec3 n, vec3 v, out vec3 h, out vec3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist)
{ l = normalize(lv); h = normalize(l + v); nh = clamp(dot(n, h), 0.0, 1.0); nl = clamp(dot(n, l), 0.0, 1.0);
  nv = clamp(dot(n, v), 0.0, 1.0); vh = clamp(dot(v, h), 0.0, 1.0); lightDist = length(lv); }
vec3 srgb_to_linear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }
vec3 linear_to_srgb(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }
void mirrorClip(vec3 pos) {}
void waterClip(vec3 pos) {}
float getAmbientClamp() { return 1.0; }
vec4 encodeNormal(vec3 n, float env, float flag) { return vec4(n, env); }
float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen) { return 1.0; }
float sunShadowedDiffuse(float da, float s) { return min(da, s); }
void sampleReflectionProbesLegacy(inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv, vec2 tc, vec3 pos,
    vec3 norm, float glossiness, float envIntensity, bool transparent, vec3 amblit)
{ ambenv = amblit; glossenv = vec3(0.5); legacyenv = vec3(0.8); }
void applyGlossEnv(inout vec3 color, vec3 glossenv, vec4 spec, vec3 pos, vec3 norm) { color += glossenv * spec.rgb * spec.a * 0.25; }
void applyLegacyEnv(inout vec3 color, vec3 legacyenv, vec4 spec, vec3 pos, vec3 norm, float env) { color = mix(color, legacyenv, env); }
'''
MATERIAL_VERTEX='''#version 420
uniform float z;
out vec3 vary_position; out vec4 vertex_color; out vec2 vary_texcoord0; out vec2 vary_texcoord1; out vec2 vary_texcoord2;
out vec3 vary_normal; out vec3 vary_tangent; flat out float vary_sign;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    gl_Position = vec4(p, z, 1.0);
    vec2 uv = p * 0.5 + 0.5;
    vary_position = vec3(p, -2.0); vertex_color = vec4(1.0);
    vary_texcoord0 = uv; vary_texcoord1 = uv; vary_texcoord2 = uv;
    vary_normal = vec3(0.0, 0.0, 1.0); vary_tangent = vec3(1.0, 0.0, 0.0); vary_sign = 1.0;
}'''
def material_program(as_variant):
    text=(root/'indra/newview/app_settings/shaders/class3/deferred/materialF.glsl').read_text()
    extra='#define DIFFUSE_ALPHA_MODE 1\n#define HAS_NORMAL_MAP 1\n#define HAS_SPECULAR_MAP 1\n#define HAS_SUN_SHADOW 1\n'
    if as_variant: extra+='#define ALPHA_STOCHASTIC 1\n'
    stages=[(0x8B31,MATERIAL_VERTEX),(0x8B30,'#version 420\n'+text.replace('/*[EXTRA_CODE_HERE]*/',extra)),(0x8B30,MATERIAL_STUBS)]
    if as_variant: stages.extend([(0x8B30,helper),(0x8B30,engine)])
    return program(stages)
mat_plain=material_program(False); mat_variant=material_program(True)

def rgba_tex(r,g,b,a,w=W,h=H,ramp=False):
    """RGBA8 texture of one colour, or with alpha rising 0 -> a across x (ramp)"""
    data=(C.c_ubyte*(w*h*4))()
    for y in range(h):
        for x in range(w):
            i=(y*w+x)*4; data[i:i+4]=[r,g,b,int(round(a*x/(w-1))) if ramp else a]
    t=U();gentex(1,C.byref(t));bindtex(TEX2D,t);teximage(TEX2D,0,0x8058,w,h,0,RGBA,0x1401,data)
    texparam(TEX2D,MIN,NEAREST);texparam(TEX2D,MAG,NEAREST);return t
TEX_UNITS={'diffuseMap':6,'bumpMap':7,'specularMap':8,'lightFunc':9}
def bind_material(prog,diffuse,bump,spec,env,gloss):
    use(prog)
    for name,unit in TEX_UNITS.items(): uniform1i(loc(prog,name.encode()),unit)
    bind_unit(6,TEX2D,diffuse);bind_unit(7,TEX2D,bump);bind_unit(8,TEX2D,spec);bind_unit(9,TEX2D,light_func)
    uniform1f(loc(prog,b'env_intensity'),env)
    fn('glUniform4f',None,I,C.c_float,C.c_float,C.c_float,C.c_float)(loc(prog,b'specular_color'),1.,1.,1.,gloss)
    uniform1f(loc(prog,b'emissive_brightness'),0.);uniform1i(loc(prog,b'sun_up_factor'),1)
    uniform3f(loc(prog,b'sun_dir'),0.,0.6,0.8);uniform3f(loc(prog,b'moon_dir'),0.,0.6,0.8)
    # stage L3: the ground light and the sun's direction the shaders read now
    uniform3f(loc(prog,b'primary_light_dir'),0.,0.6,0.8);uniform3f(loc(prog,b'primary_light_color'),1.,1.,1.);uniform1f(loc(prog,b'primary_shadow_strength'),1.)
light_func=rgba_tex(255,255,255,255)
red_clear=rgba_tex(200,40,40,0)            # the layer's diffuse: alpha 0 (a tattoo or applier layer)
red_fade=rgba_tex(200,40,40,255,ramp=True) # alpha 0 -> 1 across the face
own_bump_matte=rgba_tex(128,128,255,0)     # its own normal map: no glossiness (alpha 0)
own_spec_matte=rgba_tex(0,0,0,0)           # its own specular map: no environment (alpha 0)
own_bump_shiny=rgba_tex(128,128,255,255)   # shiny glass (#328): glossy normal map,
own_spec_shiny=rgba_tex(255,255,255,255)   # full environment
# what another face left on those units: a shiny face's maps under a matte layer, a matte face's
# under shiny glass
STALE={True:(own_bump_matte,own_spec_matte),False:(own_bump_shiny,own_spec_shiny)}

def alpha_only_binds_maps(env,gloss,fixed):
    """LLDrawPoolAlpha::TexSetup in the alpha-only passes: the face's own maps when its material
    can glare (alphaOnlyCanGlare); before the fix, never"""
    return fixed and (env>0. or gloss>0.)

def material_layer(z,diffuse,bump,spec,env,gloss,total,n,scene_rgba,fixed):
    """renderStochastic over one blended material face, then the composite"""
    f,accum,trans,depth=targets[n];iterations=total//n
    bindfbo(FB,f);enable(MULTISAMPLE);drawbuffer(COLOR1);clearf(0,(1.,0.,0.,0.));drawbuffer(COLOR0);clearf(0,(0.,0.,0.,0.))
    spread(n,expand);enable(POLY_OFFSET)
    own=alpha_only_binds_maps(env,gloss,fixed)
    ab,asp=(bump,spec) if own else STALE[spec==own_spec_shiny]
    def variant_pass(mode,it=0):
        bind_material(mat_variant,diffuse,ab,asp,env,gloss);uniform1f(loc(mat_variant,b'z'),z)
        uniform1i(loc(mat_variant,b'st_mode'),mode)
        uniform1i(loc(mat_variant,b'st_samples_hw'),n);uniform1i(loc(mat_variant,b'st_samples'),total)
        uniform1i(loc(mat_variant,b'st_seed'),21);bind_unit(1,TEX2D,mask_tex[total]);uniform1i(loc(mat_variant,b'stoch_masks'),1)
        draw(4,0,3)
    bindfbo(FB,f);drawbuffer(COLOR1);enable(BLEND);blendsep(ZERO,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
    depthmask(0);offset(VIEWER_OFFSET[0],VIEWER_OFFSET[1]);variant_pass(1)
    for it in range(iterations):
        if it: spread(n,expand);enable(POLY_OFFSET)
        bindfbo(FB,f);drawbuffer(NONE);disable(BLEND);depthmask(1);offset(VIEWER_OFFSET[0],VIEWER_OFFSET[1]);variant_pass(2,it)
        drawbuffer(COLOR0);depthmask(0);enable(BLEND);blendsep(SRC_ALPHA,ONE,ONE,ONE);offset(VIEWER_OFFSET[0],VIEWER_OFFSET[2])
        bind_material(mat_plain,diffuse,bump,spec,env,gloss);uniform1f(loc(mat_plain,b'z'),z);draw(4,0,3)
    disable(POLY_OFFSET);disable(MULTISAMPLE)
    bindfbo(FB,scene_fbo);drawbuffer(COLOR0);clearcolor(*scene_rgba);clear(0x4000)
    disable(DEPTH_TEST);enable(BLEND);blendsep(ONE,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
    use(composite);bind_unit(4,TEXMS,accum);bind_unit(5,TEXMS,trans)
    uniform1i(loc(composite,b'alpha_st_accum'),4);uniform1i(loc(composite,b'alpha_st_trans'),5)
    uniform1i(loc(composite,b'st_samples_hw'),n);uniform1i(loc(composite,b'st_samples'),total);uniform1i(loc(composite,b'st_glow'),0)
    draw(4,0,3);enable(DEPTH_TEST)
    out=(C.c_float*(W*H*4))();readbuffer(COLOR0);readpixels(0,0,W,H,RGBA,FLOAT,out)
    return [tuple(out[i*4:i*4+4]) for i in range(W*H)]

def material_sorted(z,diffuse,bump,spec,env,gloss,scene_rgba):
    """sorted alpha: the plain program over the scene, SRC_ALPHA, ONE_MINUS_SRC_ALPHA, tested
    against the opaque depth at the pixel centre"""
    bindfbo(FB,scene_fbo);drawbuffer(COLOR0);clearcolor(*scene_rgba);clear(0x4000)
    disable(DEPTH_TEST);enable(BLEND);blendsep(SRC_ALPHA,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
    bind_material(mat_plain,diffuse,bump,spec,env,gloss);uniform1f(loc(mat_plain,b'z'),z);draw(4,0,3)
    enable(DEPTH_TEST)
    out=(C.c_float*(W*H*4))();readbuffer(COLOR0);readpixels(0,0,W,H,RGBA,FLOAT,out)
    return [tuple(out[i*4:i*4+4]) for i in range(W*H)]

def worst_rgb(a,b): return max(max(abs(p[c]-q[c]) for c in range(3)) for p,q in zip(a,b))
SKIN=(0.6,0.4,0.3,0.)
set_opaque(0.8)
CASES=[('alpha 0 layer, glossy material, matte maps (a body layer)',red_clear,own_bump_matte,own_spec_matte,0.4,0.6),
       ('alpha 0 layer, environment only, matte maps',red_clear,own_bump_matte,own_spec_matte,0.5,0.0),
       ('alpha 0 shiny glass, its reflections (#328)',red_clear,own_bump_shiny,own_spec_shiny,0.5,0.6),
       ('alpha 0 -> 1 fade, glossy material, matte maps',red_fade,own_bump_matte,own_spec_matte,0.4,0.6),
       ('alpha 0 -> 1 fade, shiny maps',red_fade,own_bump_shiny,own_spec_shiny,0.5,0.6)]
before=[]
for name,dif,bump,spec,env,gloss in CASES:
    ref=material_sorted(0.0,dif,bump,spec,env,gloss,SKIN)
    for total,n in ((8,8),(4,4)):
        got=material_layer(0.0,dif,bump,spec,env,gloss,total,n,SKIN,fixed=True)
        worst=worst_rgb(got,ref)
        # one layer is exact (its accumulation passes on every sample, the total is exact); the
        # tolerance is the 16-bit float targets'
        assert worst<4e-3,('glaring material against sorted alpha',name,total,worst)
    bad=material_layer(0.0,dif,bump,spec,env,gloss,8,8,SKIN,fixed=False)
    before.append((name,worst_rgb(bad,ref),sum(1 for p,q in zip(bad,ref) if max(abs(p[c]-q[c]) for c in range(3))>4e-3)))
    checks+=1
untouched=material_sorted(0.0,red_clear,own_bump_matte,own_spec_matte,0.4,0.6,SKIN)
assert all(max(abs(p[c]-SKIN[c]) for c in range(3))<1e-3 for p in untouched),('sorted alpha should leave the skin as it is',untouched[0],untouched[W*H//2])
print(f'{len(CASES)} blended legacy materials that can glare (materialF.glsl), S = 8, 4: the composite equals sorted '
      'alpha within 4e-3; an alpha 0 layer with matte maps leaves the opaque surface exactly as sorted alpha does')
for name,worst,count in before:
    print(f'  before (alpha-only passes reading another face\'s normal and specular maps instead of its own): {name}: {count} of {W*H} pixels differ, worst {worst:.3f}')
assert before[0][2]==W*H and before[1][2]==W*H,('the old binding should change every pixel under the alpha 0 layer',before)
assert before[2][2]==W*H,('the old binding should lose the shiny glass reflections',before)

# ---- the temporal accumulation (three fragment passes, with the
# engine's resolve; the composite's st_temporal)
# 8 samples in one pass (S = 8), the masks' seeds changing every frame (st_frame), the last frame's colour moved
# by the camera and clamped to this frame's 3 x 3 neighbourhood, blended at 1/8.
# <<bench-shared:begin>>
IDENT=[1.,0.,0.,0., 0.,1.,0.,0., 0.,0.,1.,0., 0.,0.,0.,1.]
def new_temporal(reproj=IDENT,old=False,ref=False):
    if old:   # the old pipeline's history: RGBA16F x2 and the averaged samples in RGBA32F
        tex=[tex2d(RGBA16F,RGBA,FLOAT) for _ in range(2)];fbos=[fbo((COLOR0,TEX2D,x)) for x in tex]
        cur=tex2d(RGBA32F,RGBA,FLOAT);curfbo=fbo((COLOR0,TEX2D,cur))
        return dict(tex=tex,fbo=fbos,cur=cur,curfbo=curfbo,idx=0,valid=False,reproj=list(reproj))
    tex=[tex2d(RGBA16F,RGBA,FLOAT) for _ in range(2)]
    extra=[tex2d(RG16F,RGBA,FLOAT) for _ in range(2)]
    fbos=[fbo((COLOR0,TEX2D,x),(COLOR1,TEX2D,e)) for x,e in zip(tex,extra)]
    cur=tex2d(RGBA16F,RGBA,FLOAT);aux=tex2d(RGBA16F,RGBA,FLOAT)   # this frame's averaged accumulation, its grain / total alpha / depths
    if ref:   # the commit before the mask: a smooth pass draws over cur
        curfbo=fbo((COLOR0,TEX2D,cur),(COLOR1,TEX2D,aux));smoothfbo=fbo((COLOR0,TEX2D,cur))
        return dict(tex=tex,extra=extra,fbo=fbos,cur=cur,aux=aux,curfbo=curfbo,smoothfbo=smoothfbo,idx=0,valid=False,reproj=list(reproj))
    mask=tex2d(R8,RGBA,FLOAT)   # 1 where anything transparent was drawn
    curfbo=fbo((COLOR0,TEX2D,cur),(COLOR1,TEX2D,aux),(COLOR2,TEX2D,mask))
    return dict(tex=tex,extra=extra,fbo=fbos,cur=cur,aux=aux,mask=mask,curfbo=curfbo,idx=0,valid=False,reproj=list(reproj))
def rmse(pix,want,cols=None):
    xs=range(W) if cols is None else cols
    return math.sqrt(sum((pix[y*W+x][c]-want[c])**2 for y in range(H) for x in xs for c in range(3))/(3*H*len(xs)))
def frame_rmse(a,b): return math.sqrt(sum((p[c]-q[c])**2 for p,q in zip(a,b) for c in range(3))/(3*len(a)))
def scene(alphas=(0.2,0.4,0.5)): return [L(0.3,alphas[0],(1.,0.,0.),seed=5),L(0.45,alphas[1],(0.,1.,0.),seed=6),L(0.6,alphas[2],(0.,0.,1.),seed=7)]
def exact(alphas=(0.2,0.4,0.5)): return (alphas[0],alphas[1]*(1-alphas[0]),alphas[2]*(1-alphas[0])*(1-alphas[1]))
# <<bench-shared:end>>
set_opaque(1.0)
want=exact()
# no history: the same image as the composite without the temporal accumulation
TEMPORAL_FRAME[0]=1
plain=stochastic(scene(),8,8,temporal=None)
T=new_temporal();first=stochastic(scene(),8,8,temporal=T)
# the first frame has no history: its image is the smooth of this frame's samples alone, nearer the exact image than the samples are
assert rmse(first,want)<=rmse(plain,want)+1e-3,('the first frame (smoothed) is no worse than the samples',rmse(first,want),rmse(plain,want))
checks+=1
# a static view: the noise averages over the frames
single8=[]
for f in range(1,9):
    TEMPORAL_FRAME[0]=f;single8.append(rmse(stochastic(scene(),8,8),want))
T=new_temporal();errs={};prev=None
for f in range(1,41):
    TEMPORAL_FRAME[0]=f;pix=stochastic(scene(),8,8,temporal=T)
    if f in (1,8,16,40): errs[f]=rmse(pix,want)
    if f==39: prev=pix
flicker=frame_rmse(prev,pix)
TEMPORAL_FRAME[0]=1;a=stochastic(scene(),8,8);TEMPORAL_FRAME[0]=2;b=stochastic(scene(),8,8);flicker8=frame_rmse(a,b)
mean8=sum(single8)/len(single8)
assert errs[40]<0.55*mean8,('8 samples with the temporal accumulation against 8 samples',errs,mean8)
assert flicker<0.25*flicker8,('the grain holds still between frames',flicker,flicker8)
checks+=1
print(f'temporal accumulation, a static view (three layers of alpha 0.2, 0.4, 0.5, rmse/255): 8 samples {mean8*255:.2f}; 8 samples '
      f'accumulated: frame 1 {errs[1]*255:.2f}, 8 {errs[8]*255:.2f}, 16 {errs[16]*255:.2f}, 40 {errs[40]*255:.2f}; '
      f'rms change between frames {flicker*255:.2f} against {flicker8*255:.2f} without')
# the camera moves 3 pixels: the history follows (reprojected), where without it the edges ghost
def moved(reproj_shift):
    global LAYER_SCISSOR
    T=new_temporal();LAYER_SCISSOR=(8,40)
    for f in range(1,25):
        TEMPORAL_FRAME[0]=f;stochastic(scene(),8,8,temporal=T)
    LAYER_SCISSOR=(11,43);T['reproj']=[1.,0.,0.,0., 0.,1.,0.,0., 0.,0.,1.,0., -2.0*reproj_shift/W,0.,0.,1.]
    TEMPORAL_FRAME[0]=25;pix=stochastic(scene(),8,8,temporal=T);LAYER_SCISSOR=None
    return pix
edge_cols=list(range(9,15))+list(range(39,46))
inside=lambda x:11<=x<43
def region_error(pix):
    e=0.;n=0
    for y in range(H):
        for x in edge_cols:
            w=want if inside(x) else (0.,0.,0.)
            e+=sum((pix[y*W+x][c]-w[c])**2 for c in range(3));n+=3
    return math.sqrt(e/n)
good=region_error(moved(3));wrong=region_error(moved(0))
assert good<0.75*wrong,('the history follows the camera',good,wrong)
checks+=1
print(f'camera moved 3 pixels, the columns at the edges of the layers (rmse/255): history reprojected {good*255:.2f}, not reprojected {wrong*255:.2f}')
# an alpha change is never ghosted: the correction uses this frame's total alpha
T=new_temporal()
for f in range(1,25):
    TEMPORAL_FRAME[0]=f;stochastic(scene(),8,8,temporal=T)
TEMPORAL_FRAME[0]=25;changed=stochastic(scene((0.05,0.1,0.1)),8,8,temporal=T);w2=exact((0.05,0.1,0.1))
TEMPORAL_FRAME[0]=25;plain2=stochastic(scene((0.05,0.1,0.1)),8,8)
e_t=rmse(changed,w2);e_p=rmse(plain2,w2)
assert e_t<2.0*e_p+0.01,('after an alpha change the first frame is no worse than without history',e_t,e_p)
checks+=1
print(f'alpha 0.2/0.4/0.5 -> 0.05/0.1/0.1 after 24 frames: first frame rmse/255 {e_t*255:.2f} with the history, {e_p*255:.2f} without')
TEMPORAL_FRAME[0]=0
# ---- the grain-measuring resolve (S = 8): what alphaStochasticCurrentF.glsl measures and what the blend and the smooth do with it
import numpy as np
getteximage=fn('glGetTexImage',None,U,I,U,U,P)
def read_tex(tex,fmt=RGBA,channels=4):
    bind_unit(11,TEX2D,tex);out=(C.c_float*(W*H*4))();getteximage(TEX2D,0,fmt,FLOAT,out)
    return np.frombuffer(out,dtype=np.float32).reshape(H,W,4).copy()
def run_frames(layers_fn,frames,T,first=1,n=8):
    for f in range(first,first+frames):
        TEMPORAL_FRAME[0]=f;pix=stochastic(layers_fn(f),n,n,temporal=T)
    return pix
set_opaque(1.0)
# one layer covers the pixel: every sample holds its colour, the grain is 0 and the colour is exact (sharp, no history)
T=new_temporal();one=lambda f:[L(0.4,0.5,(0.8,0.4,0.1),seed=21)]
run_frames(one,3,T);aux=read_tex(T['aux']);grain1=float(aux[...,0].max())
assert grain1<1e-3,('one layer has no grain',grain1)
sharp=stochastic(one(0),8,8,temporal=T);plain1=stochastic(one(0),8,8)
assert frame_rmse(sharp,plain1)<3e-3,('one layer takes no history',frame_rmse(sharp,plain1))
# the left-out part of the history of such a pixel is all of it (this frame's weight is 1)
assert float(read_tex(T['extra'][T['idx']])[...,1].min())>=0.999,'one layer: the whole history is left out'
# two layers: the samples differ, the grain is measured
T=new_temporal();two=lambda f:[L(0.3,0.5,(1.,0.,0.),seed=22),L(0.5,0.5,(0.,0.,1.),seed=23)]
run_frames(two,3,T);aux=read_tex(T['aux']);grain2=float(aux[...,0].mean())
assert grain2>20*max(grain1,1e-5) and grain2>1e-3,('two layers have grain',grain1,grain2)
assert np.isfinite(aux).all()
# the depths: 1 - the nearest stochastic depth and 1 - the farthest transparent one (not the opaque depth behind the pixel: the layer at clip z 0.5 is window depth 0.75), as stored: the layer at clip z 0.3 is window depth 0.65
near=float(aux[...,2].max());hit=aux[...,2]>1e-3;far=float(aux[...,3][hit].min())   # (over the pixels some layer covers)
assert abs(near-0.35)<2e-3 and abs(far-0.25)<2e-3,('the depths are measured',near,far)
checks+=1
print(f'grain: one layer {grain1:.2e} (sharp, takes no history), two layers {grain2:.4f} (mean standard error of alpha x colour); depths 1 - z near {near:.3f}, far {far:.3f}')
# the last frame is left out where the total alpha changes ...
T=new_temporal();run_frames(lambda f:scene(),12,T)
extra=read_tex(T['extra'][T['idx']]);quiet_left=float(extra[...,1].mean())
pix=stochastic(scene((0.05,0.1,0.1)),8,8,temporal=T);extra=read_tex(T['extra'][T['idx']]);changed_left=float(extra[...,1].mean())
assert changed_left>0.9 and quiet_left<0.9,('the history is dropped where the total alpha changed',quiet_left,changed_left)
# ... and where the camera has slid the far layer more than 1/4 px past the near one (a reprojection that shifts x with depth)
def depth_shift(px_apart):
    c=px_apart*2.0/W/0.6   # clip x += c * clip z: the layers at z 0.3 (clip -0.4) and z 0.6 (clip 0.2), the nearest and the farthest transparent surface, end up 0.6 c apart
    return [1.,0.,0.,0., 0.,1.,0.,0., c,0.,1.,0., 0.,0.,0.,1.]   # column-major: column 2, row 0
slid_left={}
for apart in (0.0,0.1,2.0):
    T=new_temporal(depth_shift(apart));run_frames(lambda f:scene(),12,T)
    slid_left[apart]=float(read_tex(T['extra'][T['idx']])[...,1].mean())
assert slid_left[0.0]<0.5 and slid_left[2.0]>0.9,('the history is dropped where the far layer slides',slid_left)
checks+=1
print(f'left out of the last frame (mean over the pixels): static {quiet_left:.2f}, total alpha 0.2/0.4/0.5 -> 0.05/0.1/0.1 {changed_left:.2f}; far layer slid 0 px {slid_left[0.0]:.2f}, 0.1 px {slid_left[0.1]:.2f}, 2 px {slid_left[2.0]:.2f}')
# the smooth: the grain of a broad layer is averaged away, the edge of the layer stays: the columns just outside it (total alpha 0 against 0.7)
# are black and the columns just inside it are the layer's colour, not a blend of the two
T=new_temporal();LAYER_SCISSOR=(16,48)
last=run_frames(lambda f:scene(),30,T);LAYER_SCISSOR=None
def column_err(pix,cols,w): return math.sqrt(sum((pix[y*W+x][c]-w[c])**2 for y in range(H) for x in cols for c in range(3))/(3*H*len(cols)))
outside=column_err(last,[14,15],(0.,0.,0.));inside=column_err(last,[17,18],want)
assert outside<0.01 and inside<0.04,('the smooth keeps the edge of the layer',outside,inside)
plain_in=column_err(stochastic(scene(),8,8),[17,18],want)
assert inside<0.5*plain_in,('and the layer is smoother than its 8 samples',inside,plain_in)
checks+=1
print(f'edge of a layer after 30 frames (rmse/255): the two columns outside {outside*255:.2f}, the two inside {inside*255:.2f} against {plain_in*255:.2f} for the 8 samples alone')

# ---- the mask and the folded smooth (the composite does the like-neighbour averaging, the resolve and the composite draw only under
# the mask): against the passes of REF_REF (unmasked blend, a smooth pass of its own) the image, the history, its last total alpha and the part left
# out are the same bit for bit over a frame sequence: nothing, transparency appearing, moving, scattered (hair in the middle, a window in the
# corner, an alpha 0 glow face, a face too faint to move the transparency), then gone the next frame, then back elsewhere
set_opaque(1.0)
def layers_of(kind):
    if kind=='all': return scene()
    if kind=='hair': return [L(0.3,0.5,(1.,0.,0.),seed=22),L(0.5,0.5,(0.,0.,1.),seed=23)]
    if kind=='window': return [L(0.35,0.3,(0.2,0.6,1.),seed=51)]
    if kind=='glow0': return [L(0.4,0.0,(1.,1.,1.),seed=52,glow=0.7)]       # alpha 0, a glow only
    if kind=='faint': return [L(0.4,3e-4,(1.,0.5,0.),seed=53)]              # too faint to move the half-float transparency
    return []
def seq_frames():
    seq=[]
    full=None
    for f in range(3): seq.append([('none',full)])
    for f in range(4): seq.append([('all',full)])
    for k in range(4): seq.append([('all',(4+3*k,28+3*k))])                 # moving
    for k in range(4): seq.append([('hair',(W//2-8,W//2+8,H//2-6,H//2+6)),('window',(W-14,W,0,10)),('glow0',(2,12,H-8,H)),('faint',(20,44,4,12))])   # scattered
    for f in range(2): seq.append([('none',full)])                              # gone the next frame
    for k in range(3): seq.append([('window',(2+k,22+k,H-12,H)),('hair',(W-20,W-4,2,14))])   # back elsewhere
    seq.append([('all',full)]);seq.append([('none',full)])
    return seq
def run_mask_sequence(reproj):
    global LAYER_SCISSOR
    Tn=new_temporal(reproj);Tr=new_temporal(reproj,ref=True);frames=0;worst=0.;masked=[]
    for f,groups in enumerate(seq_frames(),1):
        TEMPORAL_FRAME[0]=f
        outs={}
        for pipe,Tx in (('new',Tn),('ref',Tr)):
            # the layers of every group, each in its own rectangle: stochastic() cuts layers by LAYER_SCISSOR, so a group is one call's layers
            # drawn through the same pass structure: the groups' layers are concatenated with their rectangles set per layer below
            allr=[(l,rect) for kind,rect in groups for l in layers_of(kind)]
            RECTS.clear();RECTS.update({l[3]:rect for l,rect in allr})
            outs[pipe]=np.array(stochastic([l for l,_ in allr],8,8,scene_rgba=(0.1,0.2,0.3,0.0),temporal=Tx,pipe=pipe),dtype=np.float32).reshape(H,W,4)
        assert np.array_equal(outs['new'],outs['ref']),('the masked image is the unmasked one',f,float(np.abs(outs['new']-outs['ref']).max()))
        for i in (0,1):
            for key in ('tex','extra'):
                assert np.array_equal(read_tex(Tn[key][i]),read_tex(Tr[key][i])),('the history is the unmasked one',f,key,i)
        assert np.array_equal(read_tex(Tn['aux']),read_tex(Tr['aux'])),('aux',f)
        if any(k=='glow0' for k,_ in groups): assert outs['new'][...,3].max()>0.1,('the glow face shows in the scene alpha',f)
        masked.append(float(read_tex(Tn['mask'])[...,0].mean()));frames+=1
    return frames,masked
RECTS={}
_orig_layer=layer
def rect_layer(prog,z,alpha=0.,rgb=(1.,1.,1.),seed=0,glow=0.,slope=0.,slope_y=0.):
    global LAYER_SCISSOR
    LAYER_SCISSOR=RECTS.get(seed);_orig_layer(prog,z,alpha,rgb,seed,glow,slope,slope_y);LAYER_SCISSOR=None
layer=rect_layer
try:
    n_frames,coverage=run_mask_sequence(IDENT)
    # the same with the camera panning 1.3, 0.7 px a frame (the history reprojected)
    n_frames2,_=run_mask_sequence([1.,0.,0.,0., 0.,1.,0.,0., 0.,0.,1.,0., -2.0*1.3/W,-2.0*0.7/H,0.,1.])
finally:
    layer=_orig_layer;LAYER_SCISSOR=None;TEMPORAL_FRAME[0]=0
checks+=1
print(f'mask and folded smooth: {n_frames} + {n_frames2} frames (nothing, appearing, moving, scattered with hair, a window, an alpha 0 glow face and a face too faint to move the transparency, gone the next frame, back elsewhere), static and panning: image, history, last total alpha and part left out identical to the passes before the mask (git {REF_REF}); mask covers {min(coverage)*100:.0f} to {max(coverage)*100:.0f} % of the pixels over the sequence')

# ---- the NaN guard (Aurora Viewer PR 3663's set): NaN and infinite colours, alphas and transparencies never reach the history, the neighbours or the image
NAN=float('nan');INF=float('inf')
def finite_all(a): return bool(np.isfinite(a).all())
def check_finite(T,pix,what):
    assert finite_all(np.array(pix)),(what,'the image holds a value that is not finite')
    for name in ('cur','aux'): assert finite_all(read_tex(T[name])),(what,name)
    for i in (0,1):
        assert finite_all(read_tex(T['tex'][i])),(what,'history',i)
        assert finite_all(read_tex(T['extra'][i])),(what,'extra',i)
good=lambda f:scene()
def injected(rgb=(1.,0.,0.),alpha=0.4,columns=(8,24)):
    """the three layers of scene() and a fourth, injected one over the columns"""
    def layers(f): return scene()+[L(0.2,alpha,rgb,seed=31)]
    return layers
cases={'NaN colour':dict(rgb=(NAN,1.,1.)),'infinite colour':dict(rgb=(INF,INF,INF)),'negative infinite colour':dict(rgb=(-INF,0.,0.)),
       'NaN alpha':dict(alpha=NAN),'infinite alpha':dict(alpha=INF),'NaN in one channel only':dict(rgb=(0.2,NAN,0.2))}
for what,kw in cases.items():
    T=new_temporal();LAYER_SCISSOR=(8,24);SCISSOR_SEEDS={31}
    for f in range(1,7):
        TEMPORAL_FRAME[0]=f;pix=stochastic(injected(**kw)(f),8,8,temporal=T)
        check_finite(T,pix,what)
    # the injection stops: the pixels come back, the neighbours were never poisoned, the history has recovered
    LAYER_SCISSOR=None
    for f in range(7,40):
        TEMPORAL_FRAME[0]=f;pix=stochastic(good(f),8,8,temporal=T)
    check_finite(T,pix,what+' (afterwards)')
    assert rmse(pix,want)<0.06,(what,'the image recovers after the injection',rmse(pix,want))
    checks+=1
# an injected NaN in the composite's own samples (no temporal accumulation): the same guard
for what,kw in cases.items():
    T=None;LAYER_SCISSOR=(8,24);SCISSOR_SEEDS={31}
    TEMPORAL_FRAME[0]=1;pix=stochastic(injected(**kw)(1),8,8)
    assert finite_all(np.array(pix)),(what,'without the history');LAYER_SCISSOR=None
# the pixels outside the injected columns are the clean scene's (the neighbourhood is not poisoned): same frame, with and without the injected layer
TEMPORAL_FRAME[0]=3;T=new_temporal();LAYER_SCISSOR=(8,24);SCISSOR_SEEDS={31};bad=stochastic(injected(rgb=(NAN,NAN,NAN))(3),8,8,temporal=T);LAYER_SCISSOR=None
T=new_temporal();clean=stochastic(good(3),8,8,temporal=T)
far_cols=list(range(32,W))
assert max(abs(bad[y*W+x][c]-clean[y*W+x][c]) for y in range(H) for x in far_cols for c in range(3))<1e-3,'a NaN layer changed pixels far from it'
checks+=1
print('NaN guard: NaN and infinite colours (three forms), NaN and infinite alpha, one NaN channel: the image, the history, the aux target and the neighbours stay finite over 6 injected frames, and the image recovers within 33 frames of clean ones')
SCISSOR_SEEDS=None;TEMPORAL_FRAME[0]=0
# ---- edge pixels: attributes at the centroid, not the pixel centre (the lattice on layered hair)
# The stochastic passes draw into multisampled targets: a pixel that a triangle covers in part is
# shaded once for that triangle, and without `centroid` its attributes are interpolated at the pixel
# centre, which may lie outside the triangle (extrapolated). Sorted alpha is single-sampled and
# shades a pixel only when its centre is inside, so there the texture coordinates never leave the
# triangle. A mesh whose quads have UV islands of their own (hair cards) shows the texels beyond
# each island along every quad edge: a regular lattice of thin lines over the hair. The alpha
# programs' fragment inputs are `centroid in` (alphaF, fullbrightF, pbralphaF, materialF, emissiveF,
# pbrglowF). Here the stand-in texture is grey inside an island and pure red outside it.
CELLS_L=8;QUAD_PX=8   # lattice cells of QUAD_PX pixels (NDC: 2.5 x the screen, offset per layer)
lat_vertex='''#version 420
uniform float z; uniform int cells; uniform vec2 goff; out vec2 vuv;
void main(){ int cell=gl_VertexID/6; int k=gl_VertexID%6; int cx=cell%cells; int cy=cell/cells;
 vec2 c[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,0),vec2(1,1),vec2(0,1)); vuv=c[k];
 vec2 p=(vec2(cx,cy)+c[k])/float(cells)*2.5-1.25+goff; gl_Position=vec4(p,z,1.0); }'''
lat_variant=program([(0x8B31,lat_vertex),(0x8B30,'''#version 420
uniform float test_alpha; in vec2 vuv; out vec4 frag_color; int alpha_stochastic_mask(float a);
void main(){ frag_color=vec4(0.0,0.0,0.0,test_alpha); gl_SampleMask[0]=alpha_stochastic_mask(test_alpha); }'''),(0x8B30,helper),(0x8B30,engine)])
def lat_shade_program(qualifier):
    return program([(0x8B31,lat_vertex),(0x8B30,'''#version 420
uniform float test_alpha; QUAL in vec2 vuv; out vec4 frag_color;
void main(){ bool outside=any(lessThan(vuv,vec2(0.0)))||any(greaterThan(vuv,vec2(1.0)));
 frag_color=vec4(outside?vec3(1.0,0.0,0.0):vec3(0.3),test_alpha); }'''.replace('QUAL',qualifier))])
lat_smooth=lat_shade_program('')
lat_centroid=lat_shade_program('centroid')
lat_calls=[0];lat_layers=[3]
def lat_layer(prog,z,alpha=0.,rgb=(1.,1.,1.),seed=0,glow=0.,slope=0.,slope_y=0.):
    i=lat_calls[0]%lat_layers[0];lat_calls[0]+=1
    use(prog);uniform1f(loc(prog,b'z'),z);uniform1i(loc(prog,b'cells'),CELLS_L)
    fn('glUniform2f',None,I,C.c_float,C.c_float)(loc(prog,b'goff'),(i*0.37%1.0)*2.5/CELLS_L,(i*0.61%1.0)*2.5/CELLS_L)
    uniform1f(loc(prog,b'test_alpha'),alpha)
    if prog is lat_variant: uniform1i(loc(prog,b'st_seed'),seed)
    draw(4,0,6*CELLS_L*CELLS_L)
def lattice_reddish(pixels):
    """pixels whose red exceeds their green: the texels outside a UV island reached the image"""
    return sum(1 for p in pixels if p[0]-p[1]>1e-3)
def lattice_stochastic(shade_prog,total,n):
    global layer,variant,shade
    saved=(layer,variant,shade);layer,variant,shade=lat_layer,lat_variant,shade_prog
    lat_calls[0]=0;lat_layers[0]=3
    try:
        set_opaque(1.0)
        return stochastic([L(0.7-0.1*i,0.5,seed=900+i) for i in range(3)],total,n,scene_rgba=(0.,0.,0.,0.))
    finally:
        layer,variant,shade=saved
# sorted alpha for reference: single-sampled, back to front, the pixel centre decides
bindfbo(FB,scene_fbo);drawbuffer(COLOR0);clearcolor(0.,0.,0.,0.);clear(0x4000)
disable(DEPTH_TEST);enable(BLEND);blendsep(SRC_ALPHA,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
lat_calls[0]=0;lat_layers[0]=3
for _ in range(3): lat_layer(lat_smooth,0.5,0.5)
enable(DEPTH_TEST)
out=(C.c_float*(W*H*4))();readbuffer(COLOR0);readpixels(0,0,W,H,RGBA,FLOAT,out)
sorted_reddish=lattice_reddish([tuple(out[i*4:i*4+4]) for i in range(W*H)])
assert sorted_reddish==0,('sorted alpha shades only where the centre is inside the triangle',sorted_reddish)
lattice_report=[]
for total,n in ((4,4),(8,8)):
    before=lattice_reddish(lattice_stochastic(lat_smooth,total,n))
    after=lattice_reddish(lattice_stochastic(lat_centroid,total,n))
    assert before>W*H//50,('without centroid the edge pixels should show the outside texels',total,before)
    assert after==0,('with centroid no pixel may show a texel outside its island',total,after)
    lattice_report.append((total,before,after));checks+=1
print('edge pixels (3 layers of 8-pixel quads with UV islands, a grey island over a red outside): sorted alpha shows '
      f'{sorted_reddish} of {W*H} pixels with the outside; stochastic transparency without centroid vs with it: '
      +', '.join(f'S = {t}: {b} -> {a}' for t,b,a in lattice_report))
# every non-flat fragment input of the alpha programs the stochastic passes draw is centroid
import re as _re
for rel in ('class2/deferred/alphaF.glsl','class1/deferred/fullbrightF.glsl','class2/deferred/pbralphaF.glsl',
            'class3/deferred/materialF.glsl','class1/deferred/emissiveF.glsl','class1/deferred/pbrglowF.glsl'):
    text=(root/'indra/newview/app_settings/shaders'/rel).read_text()
    plain=_re.findall(r'^[ \t]*in\s+(?:vec[234]|float|mat[234])\s+\w+',text,_re.M)
    assert not plain,(rel,'fragment inputs without centroid',plain)
    assert 'centroid in ' in text,rel
# the GLTF scene's blended variants, the viewer's own shader (its ST_IN macro) on the same lattice: the
# 2x2 diffuse texture is opaque inside the island and transparent outside it (clamp to border), so a
# sample shaded at an extrapolated coordinate has alpha 0 and its pixel loses the island's samples; the
# scene behind shows through as white. Without centroid (ST_IN replaced by `in`) pixels leak.
def glat_layer(prog,z,alpha=0.,rgb=(1.,1.,1.),seed=0,glow=0.,slope=0.,slope_y=0.):
    i=1   # one layer, its lattice off the pixel grid: a hole in it shows the scene
    use(prog);uniform1f(loc(prog,b'z'),z);uniform1i(loc(prog,b'cells'),CELLS_L)
    fn('glUniform2f',None,I,C.c_float,C.c_float)(loc(prog,b'goff'),(i*0.37%1.0)*2.5/CELLS_L,(i*0.61%1.0)*2.5/CELLS_L)
    uniform1f(loc(prog,b'test_alpha'),alpha)
    if prog in glat_variants: uniform1i(loc(prog,b'st_seed'),seed)
    draw(4,0,6*CELLS_L*CELLS_L)
assert '#define ST_IN centroid in' in gltf_text
glat_centroid=gltf_alpha_only(True,GLTF_LATTICE_VERTEX)
glat_smooth=gltf_alpha_only(True,GLTF_LATTICE_VERTEX,gltf_text.replace('#define ST_IN centroid in','#define ST_IN in'))
glat_variants=[glat_centroid,glat_smooth]
def gltf_lattice_white(gprog,total,n):
    global layer,variant,shade
    saved=(layer,variant,shade);layer,variant,shade=glat_layer,gprog,lat_centroid
    lat_calls[0]=0;lat_layers[0]=1
    try:
        set_opaque(1.0)
        pix=stochastic([L(0.5,1.0,seed=900)],total,n,scene_rgba=(1.,1.,1.,0.))
    finally:
        layer,variant,shade=saved
    return sum(1 for p in pix if abs(p[0]-0.3)>0.01)   # not the island's grey: some of the white scene showing through
glat_report=[]
for total,n in ((4,4),(8,8)):
    leaks=gltf_lattice_white(glat_smooth,total,n);clean=gltf_lattice_white(glat_centroid,total,n)
    assert leaks>0,('without centroid the GLTF variant should leak at the island edges',total,leaks)
    assert clean==0,('the GLTF blended variant must not shade outside its island',total,clean)
    glat_report.append((total,leaks,clean))
checks+=1
print('alpha programs: every interpolated fragment input is `centroid in`; the GLTF blended variant (the viewer\'s own shader) on the lattice: '
      +', '.join(f'S = {t}: {l} -> {c} leaking pixels' for t,l,c in glat_report)+' (without ST_IN centroid -> with)')

# ---- two layers whose rotations meet on one table entry (the subset of the mask table)
# The subset of a fragment was the table column of fract(pixel value + the layer's rotation), so
# two layers whose rotations fell in one of the 2048 columns (about one pair in a thousand, many
# pairs among hundreds of hair cards) had the same subset at every pixel of their overlap: their
# masks coincide, the union of the two covers R samples instead of about 2R - R^2/S, and the layers
# behind them see too few covered samples. The subset is now hashed from the seed and the pixel.
def rot_y(seed):
    sd=h32((seed*0x9e3779b9+0x7f4a7c15)&0xffffffff);sd=h32(sd^h32(0x632be5ab))   # triangle 0
    return f32((h32(sd)>>8)/16777216.0)
order=sorted(range(1,6000),key=rot_y)
best=min(zip(order,order[1:]),key=lambda ab:abs(rot_y(ab[0])-rot_y(ab[1])))
SEED_A,SEED_B=best;SEED_C=7777
assert abs(rot_y(SEED_A)-rot_y(SEED_B))<2e-6,('colliding pair',best)
hashed_col='float col = float(stoch_hash(seed ^ h ^ 0x5bd1e995u) >> 8u) / 16777216.0;'
assert hashed_col in engine,'the hashed subset is in the shader'
# the shader as it was: the subset from the rotated value
engine_old=engine.replace(hashed_col,'float col = fract(float(stoch_hash(h) >> 8u) / 16777216.0 + float(stoch_hash(seed) >> 8u) / 16777216.0);')
assert engine_old!=engine
variant_old=program([(0x8B31,vertex),(0x8B30,"""#version 420
uniform float test_alpha; out vec4 frag_color; int alpha_stochastic_mask(float a);
void main(){ frag_color=vec4(0.0,0.0,0.0,max(test_alpha,0.0)); gl_SampleMask[0]=alpha_stochastic_mask(test_alpha); }"""),(0x8B30,helper),(0x8B30,engine_old)])
def coincide(prog,total,n):
    global variant
    saved=variant;variant=prog
    try:
        set_opaque(1.0)
        return stochastic([L(0.3,0.5,(1.,0.,0.),seed=SEED_A),L(0.45,0.5,(0.,1.,0.),seed=SEED_B),L(0.6,0.5,(0.,0.,1.),seed=SEED_C)],total,n)
    finally:
        variant=saved
want=(0.5,0.25,0.125)
subset_report=[]
for total,n in ((4,4),(8,8)):
    for name,prog in (('hashed',variant),('rotated',variant_old)):
        pix=coincide(prog,total,n)
        mean=[sum(p[c] for p in pix)/len(pix) for c in range(3)]
        err=max(abs(m-w) for m,w in zip(mean,want))
        if name=='hashed': assert err<0.01,('three layers, the front two colliding',total,mean,want)
        else: assert err>0.05,('the rotated subset should show the bias',total,mean)
        subset_report.append((total,name,mean[2],err))
    checks+=1
print(f'two layers whose subsets collide (seeds {SEED_A} and {SEED_B}) over a third: the back layer reads '
      +', '.join(f'S = {t} {nm}: {b:.3f} (exact {want[2]:.3f})' for t,nm,b,e in subset_report))

# ---- sub-pixel camera pans: the history is read with a Catmull-Rom filter at the exact reprojected position
# The nearest-texel read of before 2026-11-14 dropped the fraction of a pixel the camera moved every frame, and the
# 1/8 blend accumulated it as a smear 1 to 2 px behind the image (frame 40 rmse 13 to 17 /255 at 0.25, 0.5, 1.3, 2.5 px
# per frame, against 4 to 6 for plain 8 samples). True sub-pixel pans: the geometry is translated by frame * d pixels, the
# reprojection is the exact clip-space translation, the reference is sorted alpha at the same position and samples.
# The old resolve is the baseline: the current resolve source with the call of stoch_history_catmull_rom replaced by
# stoch_history_nearest (still in the util, for this test), so it needs no git history: whole-pixel pans (diagonal ones
# included) and the static view must equal it bit for bit. The pans are horizontal, vertical and diagonal (both filter axes).
# <<bench-shared:begin>>
import numpy as np
def old_text(name,version=420):
    text=open(os.path.join(root,'indra/newview/app_settings/shaders/class1/deferred',name),encoding='utf-8').read().replace('/*[EXTRA_CODE_HERE]*/','')
    if name!='stochasticResolveUtilF.glsl':
        assert 'stoch_history_catmull_rom' in text,name
        text=text.replace('stoch_history_catmull_rom','stoch_history_nearest')
    return ('#version %d\n'%version)+('#define COMPUTE_SHADER 1\n' if version==430 else '')+text
getteximage=fn('glGetTexImage',None,U,I,U,U,P);uniform2f=fn('glUniform2f',None,I,C.c_float,C.c_float);uniform4fv=fn('glUniform4fv',None,I,I,C.POINTER(C.c_float))
PAN_VS="""#version 420
uniform float z; uniform vec2 pan; uniform vec2 vp; uniform int kind; uniform float angle;
uniform vec4 strips[160];
out vec2 obj; out vec2 across;
void main(){
  int k = gl_VertexID % 6; int s = gl_VertexID / 6;
  vec2 c[6] = vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,0),vec2(1,1),vec2(0,1));
  vec2 q = c[k]; vec2 p;
  if (kind == 0) { p = vec2(mix(-1.0, 2.0, q.x) * vp.x, mix(-0.5, 1.5, q.y) * vp.y); across = vec2(0.0); }
  else {
    vec4 st = strips[s]; float u = q.x * 2.0 - 1.0; float v = (q.y * 2.0 - 1.0) * vp.y;
    vec2 dir = vec2(sin(angle), cos(angle)); vec2 nrm = vec2(cos(angle), -sin(angle));
    p = vec2(st.x, vp.y * 0.5) + nrm * (u * st.y) + dir * v; across = vec2(u, float(s));
  }
  obj = p;
  gl_Position = vec4((p + pan) / vp * 2.0 - 1.0, z, 1.0);
}"""
PAN_SHADE="""#version 420
uniform float test_alpha; uniform vec3 test_rgb; uniform float period; uniform vec2 tdir; uniform float phase;
in vec2 obj; in vec2 across; out vec4 frag_color;
void main(){
  float t = 0.55 + 0.45 * sin(6.2831853 * dot(obj, tdir) / period + phase);
  float e = 1.0 - 0.35 * across.x * across.x;     // hair: darker towards the strip's sides
  frag_color = vec4(test_rgb * (t * e), test_alpha);
}"""
PAN_VARIANT="""#version 420
uniform float test_alpha; out vec4 frag_color; int alpha_stochastic_mask(float a);
in vec2 obj; in vec2 across;
void main(){ frag_color=vec4(0.0,0.0,0.0,max(test_alpha,0.0)); gl_SampleMask[0]=alpha_stochastic_mask(test_alpha); }"""
pan_variant=program([(0x8B31,PAN_VS),(0x8B30,PAN_VARIANT),(0x8B30,helper),(0x8B30,engine)])
pan_shade=program([(0x8B31,PAN_VS),(0x8B30,PAN_SHADE)])
pan_variant_old=program([(0x8B31,PAN_VS),(0x8B30,PAN_VARIANT),(0x8B30,old_helper),(0x8B30,old_engine)])
VARIANT_OLD[pan_variant]=pan_variant_old
PAN=[(0.0,0.0)]
def pan_layer(prog,z,alpha=0.,rgb=(1.,1.,1.),seed=0,glow=0.,slope=None,slope_y=0.):
    g=slope;use(prog)
    uniform1f(loc(prog,b'z'),z);uniform2f(loc(prog,b'pan'),PAN[0][0],PAN[0][1]);uniform2f(loc(prog,b'vp'),float(W),float(H))
    uniform1i(loc(prog,b'kind'),g['kind']);uniform1f(loc(prog,b'angle'),g.get('angle',0.))
    count=1
    if g['kind']==1:
        arr=g['strips'];count=len(arr)//4;uniform4fv(loc(prog,b'strips'),count,(C.c_float*len(arr))(*arr))
    if prog in (pan_variant,pan_variant_old): uniform1f(loc(prog,b'test_alpha'),alpha);uniform1i(loc(prog,b'st_seed'),seed);uniform1i(loc(prog,b'st_frame'),TEMPORAL_FRAME[0])
    else:
        uniform1f(loc(prog,b'test_alpha'),alpha);uniform3f(loc(prog,b'test_rgb'),*rgb)
        uniform1f(loc(prog,b'period'),g['period']);uniform2f(loc(prog,b'tdir'),*g['tdir']);uniform1f(loc(prog,b'phase'),g['phase'])
    r=LAYER_SCISSOR   # (x0, x1) or (x0, x1, y0, y1), as layer() cuts
    if r:
        enable(0x0C11);y0,y1=(r[2],r[3]) if len(r)==4 else (0,H);fn('glScissor',None,I,I,I,I)(r[0],y0,r[1]-r[0],y1-y0)
    draw(4,0,6*count)
    if r: disable(0x0C11)
ref_ms=texms(RGBA32F,8);ref_fbo=fbo((COLOR0,TEXMS,ref_ms))
ref_out=tex2d(RGBA32F,RGBA,FLOAT);ref_out_fbo=fbo((COLOR0,TEX2D,ref_out))
avg_prog=program([(0x8B31,screen_vertex),(0x8B30,"""#version 420
uniform sampler2DMS src; out vec4 frag_color;
void main(){ ivec2 px=ivec2(gl_FragCoord.xy); vec4 s=vec4(0.0); for(int i=0;i<8;++i) s+=texelFetch(src,px,i); frag_color=s/8.0; }""")])
def sorted_reference(layers,rgba):
    # the exact image: sorted far to near over-blending at the 8 samples, averaged per pixel
    bindfbo(FB,ref_fbo);drawbuffer(COLOR0);enable(MULTISAMPLE);disable(0x0C11)
    clearcolor(*rgba);clear(0x4000);disable(DEPTH_TEST);disable(POLY_OFFSET)
    enable(BLEND);blendsep(SRC_ALPHA,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
    for z,a,rgb,seed,glow,geo in sorted(layers,key=lambda l:-l[0]): pan_layer(pan_shade,z,a,rgb,slope=geo)
    disable(BLEND);disable(MULTISAMPLE)
    bindfbo(FB,ref_out_fbo);use(avg_prog);bind_unit(9,TEXMS,ref_ms);uniform1i(loc(avg_prog,b'src'),9);draw(4,0,3)
    out=(C.c_float*(W*H*4))();readbuffer(COLOR0);readpixels(0,0,W,H,RGBA,FLOAT,out)
    enable(DEPTH_TEST)
    return np.frombuffer(out,dtype=np.float32).reshape(H,W,4).copy()
def pan_hue(rng):
    h=rng.uniform(0,1);sat=rng.uniform(0.5,0.9);v=rng.uniform(0.6,1.0)
    i=int(h*6)%6;f=h*6-int(h*6);p,q,t=v*(1-sat),v*(1-sat*f),v*(1-sat*(1-f))
    return [(v,t,p),(q,v,p),(p,v,t),(p,q,v),(t,p,v),(v,p,q)][i]
def pan_tex(rng,lo,hi):
    a=rng.uniform(0,math.pi);return dict(period=rng.uniform(lo,hi),tdir=(math.cos(a),math.sin(a)),phase=rng.uniform(0,6.28))
def pan_scene(name):
    if name=='T3':   # the three layers above, with texture detail (periods 5 to 11 px, colours that move with the surface)
        cols=[(1.,0.,0.),(0.,1.,0.),(0.,0.,1.)];alph=(0.2,0.4,0.5);zs=(0.3,0.45,0.6);per=[(7.,0.3),(11.,1.9),(5.,2.6)]
        return [L(zs[i],alph[i],cols[i],seed=5+i,slope=dict(kind=0,period=per[i][0],tdir=(math.cos(per[i][1]),math.sin(per[i][1])),phase=0.7*i)) for i in range(3)]
    rng=np.random.default_rng(1000+sum(map(ord,name))%97);zs=np.linspace(0.2,0.7,10);layers=[]
    for i in range(10):
        a=float(rng.uniform(0.2,0.6));rgb=pan_hue(rng)
        if name=='Q10': geo=dict(kind=0,**pan_tex(rng,4.,10.))   # ten full textured layers
        else:   # S10: ten layers of parallel hair strips, 1.5 to 5 px wide, about 60 % coverage, tilted -20 to 20 degrees
            ang=float(rng.uniform(-0.35,0.35));x=-260.0+rng.uniform(0,4);arr=[]
            while x<W+60 and len(arr)<640:
                hw=rng.uniform(0.75,2.5);x+=hw;arr+=[x,hw,0.,0.];x+=hw+rng.uniform(1.0,3.5)
            geo=dict(kind=1,strips=arr,angle=ang,**pan_tex(rng,6.,16.))
        layers.append(L(float(zs[i]),a,rgb,seed=101+i,slope=geo))
    return layers
def pan_reproj(d): return [1.,0.,0.,0., 0.,1.,0.,0., 0.,0.,1.,0., -2.0*d[0]/W,-2.0*d[1]/H,0.,1.]
PAN_MX,PAN_MY=max(8,W//10),max(4,H//10)   # the border left out: the columns that enter have no history
def gauss_lp(img,sigma=1.5):
    r=int(math.ceil(3*sigma));k=np.exp(-0.5*(np.arange(-r,r+1)/sigma)**2);k/=k.sum();out=img
    for ax in (0,1):
        pad=[(0,0)]*img.ndim;pad[ax]=(r,r);q=np.pad(out,pad,mode='edge')
        out=sum(k[i]*np.take(q,range(i,i+img.shape[ax]),axis=ax) for i in range(2*r+1))
    return out
def pan_err(img,ref):
    # rmse/255 on RGB, and of the same error image after a Gaussian blur of sigma 1.5 px (the smear's share)
    e=(img[...,:3]-ref[...,:3]).astype(np.float64);lp=gauss_lp(e);c=(slice(PAN_MY,H-PAN_MY),slice(PAN_MX,W-PAN_MX))
    return float(np.sqrt(np.mean(e[c]**2))*255),float(np.sqrt(np.mean(lp[c]**2))*255)
def history_finite(T):
    # the RGBA16F history (alpha x colour, alpha) of the last frame: nothing negative or NaN
    bind_unit(10,TEX2D,T['tex'][T['idx']]);out=(C.c_float*(W*H*4))();getteximage(TEX2D,0,RGBA,FLOAT,out)
    a=np.frombuffer(out,dtype=np.float32);assert np.isfinite(a).all() and (a>=0).all(),('the history holds a negative or NaN value',a.min())
def pan_run(layers,d,old_prog,frames=40,moving=False,ablate=False):
    global resolve_prog
    new_prog=resolve_prog;rj=IDENT if moving else pan_reproj(d)   # moving surfaces: the geometry pans and the camera does not, so nothing is reprojected
    Ts={'old':new_temporal(rj),'new':new_temporal(rj),'before':new_temporal(rj,old=True),'s4':new_temporal(rj)};plain=[];plain_before=[];last={}
    identical=True;worst=0.;differing=0;sc=(0.5,0.5,0.5,0.0)
    for f in range(1,frames+1):
        TEMPORAL_FRAME[0]=f;PAN[0]=(f*d[0],f*d[1])
        ref=sorted_reference(layers,sc)
        plain.append(pan_err(np.array(stochastic(layers,8,8,scene_rgba=sc),dtype=np.float32).reshape(H,W,4),ref))
        plain_before.append(pan_err(np.array(stochastic(layers,8,8,scene_rgba=sc,pipe='old'),dtype=np.float32).reshape(H,W,4),ref))
        imgs={}
        for key,prog in (('old',old_prog),('new',new_prog)):
            resolve_prog=prog
            imgs[key]=np.array(stochastic(layers,8,8,scene_rgba=sc,temporal=Ts[key]),dtype=np.float32).reshape(H,W,4)
        resolve_prog=new_prog
        imgs['before']=np.array(stochastic(layers,8,8,scene_rgba=sc,temporal=Ts['before'],pipe='old'),dtype=np.float32).reshape(H,W,4)
        if ablate: imgs['s4']=np.array(stochastic(layers,4,4,scene_rgba=sc,temporal=Ts['s4']),dtype=np.float32).reshape(H,W,4)   # the ablation: the new resolve at S = 4 (the upgrade as first made)
        dd=np.abs(imgs['old']-imgs['new']);identical=identical and not dd.any()
        worst=max(worst,float(dd.max()));differing=max(differing,int(np.count_nonzero(dd.max(axis=2))))
        history_finite(Ts['new'])
        if f==frames: last=dict(old=pan_err(imgs['old'],ref),new=pan_err(imgs['new'],ref),before=pan_err(imgs['before'],ref),s4=pan_err(imgs['s4'],ref) if ablate else None)
    return dict(plain=(float(np.mean([e[0] for e in plain])),float(np.mean([e[1] for e in plain]))),plain_before=(float(np.mean([e[0] for e in plain_before])),float(np.mean([e[1] for e in plain_before]))),before=last['before'],old=last['old'],new=last['new'],s4=last['s4'],identical=identical,worst=worst,differing=differing)
# <<bench-shared:end>>
if True:
    saved=(layer,variant,shade)
    old_resolve=program([(0x8B31,screen_vertex),(0x8B30,old_text('alphaStochasticResolveF.glsl')),(0x8B30,old_text('stochasticResolveUtilF.glsl'))])
    layer,variant,shade=pan_layer,pan_variant,pan_shade
    try:
        set_opaque(1.0);pan_rows=[]
        for name in ('T3','Q10','S10'):
            layers=pan_scene(name)
            for d in ((0.,0.),(1.,0.),(4.,0.),(1.,1.),(0.25,0.),(0.5,0.),(1.3,0.),(2.5,0.),(0.,0.5),(0.5,0.5),(1.3,0.7),(-0.5,0.25)):
                if d[1] and H<96: continue   # 40 frames of a vertical pan scroll a scene this short off the screen: run these at STOCH_TEST_SIZE=128x128 or more
                r=pan_run(layers,d,old_resolve,ablate=d in ((0.,0.),(0.5,0.),(2.5,0.)));pan_rows.append((name,d,r))
                if d[0]==int(d[0]) and d[1]==int(d[1]):   # a whole-pixel pan and the static view: the same image as the nearest read, bit for bit
                    # the hair strips' passes are not bit-deterministic on this GPU (the nearest resolve against itself differs by one
                    # half-float step on a pixel in 40 frames of 2048, which the history then carries to its neighbours), so there the bound is a few steps on under 2 % of the pixels
                    assert r['identical'] or (name=='S10' and r['worst']<=4/1024 and r['differing']<=W*H//50),('whole-pixel pan changed',name,d,r)
                else:
                    # at S = 8 the hair strips on a pan are a little better than the plain samples' smear share (the viewer before the upgrade is worse: 'before' against 'plain_before')
                    # (hair: the measured frame-40 / plain low-pass ratio is 0.89 to 0.96 over these pans; the bound sits just above the worst, 0.98)
                    assert r['new'][1]<(r['plain'][1]*0.98 if name=='S10' else r['plain'][1]),('the history beats plain 8 samples in low-pass error (hair: measured ratio at most 0.96, bound 0.98)',name,d,r['new'][1]/r['plain'][1],r)
                    if name=='S10': assert r['new'][0]<r['before'][0],('hair: the upgrade beats the viewer before it in rmse',name,d,r)
                    if name!='S10': assert r['new'][0]<r['plain'][0],('the history beats plain 8 samples in rmse on textured layers',name,d,r)
                    # (0.65 and 0.55 of the nearest read's error, set from the measured ratios of the resolve; the bound was 0.6 and 0.5 before the upgrade)
                    assert r['new'][0]<0.65*r['old'][0] and r['new'][1]<0.55*r['old'][1],('the smear is gone',name,d,r)
            checks+=1
    finally:
        layer,variant,shade=saved;TEMPORAL_FRAME[0]=0
    print('sub-pixel camera pans, frame 40 (plain samples: the mean over the 40 frames), rmse/255 (low-pass in brackets): scene px/frame (x, y) | plain 8 samples (before) | the viewer before the upgrade (S = 8, 1/8 history) | plain 8 samples (the new dither) | nearest history, S = 8 | the upgrade (S = 8, grain resolve)')
    for name,d,r in pan_rows:
        print(f"  {name:3s} {d[0]:5.2f} {d[1]:5.2f} | {r['plain_before'][0]:5.2f} ({r['plain_before'][1]:4.2f}) | {r['before'][0]:5.2f} ({r['before'][1]:4.2f}) | {r['plain'][0]:5.2f} ({r['plain'][1]:4.2f}) | {r['old'][0]:5.2f} ({r['old'][1]:4.2f}) | {r['new'][0]:5.2f} ({r['new'][1]:4.2f})"+(' | same image as the nearest read' if d[0]==int(d[0]) and d[1]==int(d[1]) else ''))
    print('ablation, frame 40, rmse/255: the new resolve (grain weight, smooth, box) at S = 8 (shipping) against the viewer before the upgrade (S = 8, 1/8 history) and the new resolve at S = 4 (the upgrade as first made): scene px/frame | before | new resolve S = 8 | new resolve S = 4')
    for name,d,r in pan_rows:
        if r['s4'] is not None: print(f"  {name:3s} {d[0]:5.2f} {d[1]:5.2f} | {r['before'][0]:5.2f} | {r['new'][0]:5.2f} | {r['s4'][0]:5.2f}")
    # moving surfaces (the geometry moves, the camera does not: the history is not reprojected and ghosts, limited by the clamp):
    # the viewer before the upgrade against the upgrade, 40 frames
    layer,variant,shade=pan_layer,pan_variant,pan_shade
    try:
        set_opaque(1.0);moving_rows=[]
        for name in ('T3','Q10','S10'):
            layers=pan_scene(name)
            for d in ((0.5,0.),(2.,0.)):
                r=pan_run(layers,d,old_resolve,moving=True);moving_rows.append((name,d,r))
                # (reported, not asserted: the numbers are in the doc; the history ghosts there, limited by the clamp)
        checks+=1
    finally:
        layer,variant,shade=saved;TEMPORAL_FRAME[0]=0
    print('moving surfaces, frame 40 (not reprojected), rmse/255 (low-pass in brackets): scene px/frame | plain 8 samples (before) | the viewer before the upgrade | plain 8 samples (the new dither) | the upgrade')
    for name,d,r in moving_rows:
        print(f"  {name:3s} {d[0]:5.2f} {d[1]:5.2f} | {r['plain_before'][0]:5.2f} ({r['plain_before'][1]:4.2f}) | {r['before'][0]:5.2f} ({r['before'][1]:4.2f}) | {r['plain'][0]:5.2f} ({r['plain'][1]:4.2f}) | {r['new'][0]:5.2f} ({r['new'][1]:4.2f})")
    # the NaN injection against the viewer before the upgrade: six frames of a layer with a NaN colour over 16 columns, then 33 clean frames
    set_opaque(1.0);before_nan={};injected_info={}
    saved_cut=(LAYER_SCISSOR,SCISSOR_SEEDS)
    for pipe,total in (('old',8),('new',8)):
        Tn=new_temporal(old=pipe=='old');LAYER_SCISSOR=(8,24);SCISSOR_SEEDS={31}
        for f in range(1,7):
            TEMPORAL_FRAME[0]=f;pix=stochastic(scene()+[L(0.2,0.4,(NAN,1.,1.),seed=31)],total,total,temporal=Tn,pipe=pipe)
        bad_injected=int(np.count_nonzero(~np.isfinite(np.array(pix))))
        # the accumulation of the injected frame and the history, not the image alone: the old resolve's clamp can hide a poisoned history from the image
        bad_cur=int(np.count_nonzero(~np.isfinite(read_tex(Tn['cur']))));bad_hist=sum(int(np.count_nonzero(~np.isfinite(read_tex(Tn['tex'][i])))) for i in (0,1))
        injected_info[pipe]=(bad_cur,bad_hist)
        LAYER_SCISSOR=None;SCISSOR_SEEDS=None
        for f in range(7,40):
            TEMPORAL_FRAME[0]=f;pix=stochastic(scene(),total,total,temporal=Tn,pipe=pipe)
        before_nan[pipe]=(bad_injected,int(np.count_nonzero(~np.isfinite(np.array(pix)))))
    LAYER_SCISSOR,SCISSOR_SEEDS=saved_cut;TEMPORAL_FRAME[0]=0
    assert before_nan['new']==(0,0),('the upgrade holds no value that is not finite',before_nan)
    # the injection reaches the old pipeline's accumulation or history (so the test cannot pass vacuously), and the upgrade holds none
    assert injected_info['old'][0]>0 or injected_info['old'][1]>0,('the NaN injection reaches the old pipeline',injected_info)
    assert injected_info['new']==(0,0),('the upgrade holds none in its accumulation or history',injected_info)
    print(f"NaN injection (a layer with a NaN colour over 16 of {W} columns, 6 frames, then 33 clean frames), values that are not finite in the image: before the upgrade {before_nan['old'][0]} then {before_nan['old'][1]}, the upgrade {before_nan['new'][0]} then {before_nan['new'][1]}; non-finite values in the accumulation and the two histories after the injected frames: before the upgrade {injected_info['old']}, the upgrade {injected_info['new']}")
print(f'{checks} stochastic transparency cases passed.')
gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
