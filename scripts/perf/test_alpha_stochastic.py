# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offscreen stochastic transparency regression, no viewer launch; --opengl PATH selects Mesa.

Reuses the standalone Windows GL bootstrap and shader compiler (gl_test_context.py) and runs the
viewer's own stochastic transparency shaders (the stochastic engine's stochasticSampleF.glsl and
stochasticResolveUtilF.glsl, and the transparency's alphaStochasticF.glsl, alphaStochasticExpandF.glsl,
alphaStochasticResolveC.glsl (the temporal resolve on GL 4.3, a compute shader) or alphaStochasticCurrentF.glsl + alphaStochasticResolveF.glsl (GL 4.0 to 4.2), alphaStochasticCompositeF.glsl) with the pass structure and GL state of LLPipeline /
LLDrawPoolAlpha::renderStochastic on a small target:
  * the mask table: the generator (a port of LLStochasticEngine::maskTable) gives exactly r
    bits in row r, nothing above S, varied rows, the same table on every run;
  * mask coverage: the samples a fragment covers in the stochastic depth, read back per sample,
    are exactly the bits the independent Python model of st_mask predicts (pixel, face seed,
    triangle, stratified R = floor(alpha S + xi)), for 16 samples in two passes of 8, 8 in one,
    4 in one; their count is R, with mean alpha S; different face seeds give unrelated masks;
  * total alpha: 1 - prod(1 - alpha) exactly, whatever the visibility;
  * visibility weighting and the composite: two layers whose estimate is exact at S = 16
    (alpha 0.5), a noisy pair whose mean over the pixels converges, an opaque occluder;
  * the glow: accumulated like the colour, corrected, added to the scene alpha after the
    composite has dimmed it;
  * faces on, a third of a pixel's depth in front of and behind a sloped opaque surface: the
    first two cover every sample, the third leaves the scene untouched, as sorted alpha draws
    them; the depth spread of 1.0.1.81943 (the centre's depth on every sample, with a
    slope-scaled offset in the passes) is shown to let the face behind through.
;
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
import os
# STOCH_TEST_RESOLVE=fragment runs the GL 4.0 to 4.2 resolve (the pre-pass and the fragment resolve) on this GPU;
# the default is the compute resolve (GL 4.3). Both give the same image.
FRAGMENT_RESOLVE=os.environ.get("STOCH_TEST_RESOLVE","compute")=="fragment"
print("temporal resolve path:","fragment (GL 4.0 to 4.2)" if FRAGMENT_RESOLVE else "compute (GL 4.3)")
W,H=[int(v) for v in os.environ.get("STOCH_TEST_SIZE","64x32").split("x")]   # 70x37 exercises the compute resolve on partial workgroups
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
print('mask tables: rows of exactly r bits for S = 4, 8, 16, varied and reproducible')

# ---- the Python model of st_mask (alphaStochasticF.glsl), float32 at every step
def f32(x): return struct.unpack('<f',struct.pack('<f',x))[0]
def h32(v):
    x=(v*747796405+2891336453)&0xffffffff
    x=(((x>>((x>>28)+4))^x)*277803737)&0xffffffff
    return ((x>>22)^x)&0xffffffff
def model_mask(px,py,seed,prim,a,s,table):
    sd=h32((seed*0x9e3779b9+0x7f4a7c15)&0xffffffff)
    sd=h32(sd^h32((prim+0x632be5ab)&0xffffffff))
    h=h32(px|(py<<16))
    pv=(f32((h>>8)/16777216.0),f32((h32(h)>>8)/16777216.0))
    rot=(f32((sd>>8)/16777216.0),f32((h32(sd)>>8)/16777216.0))
    v=[]
    for p,q in zip(pv,rot):
        t=f32(p+q);v.append(f32(t-math.floor(t)))
    v[1]=f32((h32(sd^h^0x5bd1e995)>>8)/16777216.0)   # the subset: hashed from the seed and the pixel
    r=min(int(math.floor(f32(f32(min(max(a,0.),1.)*s)+v[0]))),s)
    col=min(int(f32(v[1]*SEEDS)),SEEDS-1)
    return table[r*SEEDS+col]

# ---- programs: the viewer's shaders as the viewer compiles them (#version 420; 430 and COMPUTE_SHADER for the compute resolve)
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
composite=program([(0x8B31,screen_vertex),(0x8B30,viewer('alphaStochasticCompositeF.glsl'))])
# the temporal accumulation: the transparency's compute resolve (this frame's samples averaged into a
# shared tile) with the engine's resolve object compiled as a second compute object, as the viewer
# links them (GL 4.3, #version 430, COMPUTE_SHADER)
bindimage=fn('glBindImageTexture',None,U,U,I,C.c_ubyte,I,U,U);dispatch=fn('glDispatchCompute',None,U,U,U)
membarrier=fn('glMemoryBarrier',None,U)
COMPUTE=0x91B9;WRITE_ONLY=0x88B9;TEXTURE_FETCH_BARRIER=0x8;IMAGE_ACCESS_BARRIER=0x20
def viewer_compute(name): return viewer(name,430).replace('#version 430\n','#version 430\n#define COMPUTE_SHADER 1\n',1)
if FRAGMENT_RESOLVE:
    # this frame's samples averaged once (RGBA32F), then the transparency's resolve with the engine's resolve object
    current_prog=program([(0x8B31,screen_vertex),(0x8B30,viewer('alphaStochasticCurrentF.glsl'))])
    resolve_prog=program([(0x8B31,screen_vertex),(0x8B30,viewer('alphaStochasticResolveF.glsl')),(0x8B30,viewer('stochasticResolveUtilF.glsl'))])
else:
    resolve_prog=program([(COMPUTE,viewer_compute('alphaStochasticResolveC.glsl')),(COMPUTE,viewer_compute('stochasticResolveUtilF.glsl'))])
# per pixel, the samples of the stochastic depth nearer than the opaque depth, as bits
readmask=program([(0x8B31,screen_vertex),(0x8B30,'''#version 420
uniform sampler2DMS st_depth; uniform sampler2D opaque; uniform int samples; out uvec4 bits;
void main(){ ivec2 px=ivec2(gl_FragCoord.xy); float o=texelFetch(opaque,px,0).r; uint m=0u;
  for(int s=0;s<samples;++s){ if(texelFetch(st_depth,px,s).r<o) m|=1u<<uint(s); } bits=uvec4(m,0u,0u,0u); }''')])

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
    data=(U*len(t))(*t);mask_tex[s]=tex2d(R32UI,RED_INT,UINT,data,SEEDS,s+1)
targets={}
for n in (4,8):
    accum=texms(RGBA16F,n);trans=texms(RG16F,n);depth=texms(D24,n)
    targets[n]=(fbo((COLOR0,TEXMS,accum),(COLOR1,TEXMS,trans),(DEPTH_ATT,TEXMS,depth)),accum,trans,depth)
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

LAYER_SCISSOR=None   # (x0, x1): the layers cover these columns only (the temporal tests move it)
TEMPORAL_FRAME=[0]   # the frame in the masks' seeds (st_frame)
def layer(prog,z,alpha=0.,rgb=(1.,1.,1.),seed=0,glow=0.,slope=0.,slope_y=0.):
    if isinstance(slope,tuple): slope,slope_y=slope
    use(prog);uniform1f(loc(prog,b'z'),z);uniform1f(loc(prog,b'slope'),slope);uniform1f(loc(prog,b'slope_y'),slope_y)
    if prog==variant: uniform1f(loc(prog,b'test_alpha'),alpha);uniform1i(loc(prog,b'st_seed'),seed);uniform1i(loc(prog,b'st_frame'),TEMPORAL_FRAME[0])
    elif prog==shade: uniform1f(loc(prog,b'test_alpha'),alpha);uniform3f(loc(prog,b'test_rgb'),*rgb)
    else: uniform1f(loc(prog,b'test_glow'),glow)
    if LAYER_SCISSOR: enable(0x0C11);fn('glScissor',None,I,I,I,I)(LAYER_SCISSOR[0],0,LAYER_SCISSOR[1]-LAYER_SCISSOR[0],H)
    draw(4,0,3)
    if LAYER_SCISSOR: disable(0x0C11)

def spread(n,prog):
    f=targets[n][0];bindfbo(FB,f);drawbuffer(NONE);disable(BLEND);disable(POLY_OFFSET)
    enable(DEPTH_TEST);depthfunc(ALWAYS);depthmask(1)
    use(prog);bind_unit(0,TEX2D,opaque_depth);uniform1i(loc(prog,b'depthMap'),0);draw(4,0,3)
    depthfunc(LEQUAL)

def stochastic(layers,total,n,scene_rgba=(0.,0.,0.,0.),use_offset=True,read_bits=None,before=False,temporal=None):
    """LLDrawPoolAlpha::renderStochastic over `layers` [(z, alpha, rgb, seed, glow, slope)], then
    the composite over a scene of `scene_rgba`; returns the scene's pixels (RGBA float)."""
    f,accum,trans,depth=targets[n];iterations=total//n
    bindfbo(FB,f);enable(MULTISAMPLE);disable(0x0C11)
    drawbuffer(COLOR1);clearf(0,(1.,0.,0.,0.));drawbuffer(COLOR0);clearf(0,(0.,0.,0.,0.))
    spread_prog,offsets=(expand_before,BEFORE_OFFSET) if before else (expand,VIEWER_OFFSET)
    spread(n,spread_prog)
    use(variant);uniform1i(loc(variant,b'st_samples_hw'),n);uniform1i(loc(variant,b'st_samples'),total)
    bind_unit(1,TEX2D,mask_tex[total]);uniform1i(loc(variant,b'stoch_masks'),1)
    def poly(i):
        if use_offset: enable(POLY_OFFSET);offset(offsets[0],offsets[i])
        else: disable(POLY_OFFSET)
    # 1. total alpha
    bindfbo(FB,f);drawbuffer(COLOR1);enable(BLEND);blendsep(ZERO,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
    depthmask(0);poly(1);use(variant);uniform1i(loc(variant,b'st_mode'),1)
    for z,a,rgb,seed,glow,slope in layers: layer(variant,z,a,seed=seed,slope=slope)
    bits=[]
    for it in range(iterations):
        if it: spread(n,spread_prog)
        # 2. stochastic depth
        bindfbo(FB,f);drawbuffer(NONE);depthmask(1);poly(1)
        use(variant);uniform1i(loc(variant,b'st_mode'),2);uniform1i(loc(variant,b'st_iteration'),it)
        for z,a,rgb,seed,glow,slope in layers: layer(variant,z,a,seed=seed,slope=slope)
        if read_bits:
            bindfbo(FB,bits_fbo);disable(BLEND);disable(DEPTH_TEST);disable(POLY_OFFSET);disable(MULTISAMPLE)
            use(readmask);bind_unit(2,TEXMS,depth);bind_unit(3,TEX2D,opaque_depth)
            uniform1i(loc(readmask,b'st_depth'),2);uniform1i(loc(readmask,b'opaque'),3);uniform1i(loc(readmask,b'samples'),n)
            draw(4,0,3);px=(U*(W*H))();readpixels(0,0,W,H,RED_INT,UINT,px);bits.append(list(px))
            enable(DEPTH_TEST);enable(MULTISAMPLE);bindfbo(FB,f)
        # 3. accumulation, then the glow of the glowing faces
        drawbuffer(COLOR0);depthmask(0);enable(BLEND);blendsep(SRC_ALPHA,ONE,ONE,ONE);poly(2)
        for z,a,rgb,seed,glow,slope in layers: layer(shade,z,a,rgb,slope=slope)
        drawbuffer(COLOR1);blendsep(ONE,ONE,ONE,ONE);poly(3)
        for z,a,rgb,seed,glow,slope in layers:
            if glow>0: layer(glowprog,z,glow=glow,slope=slope)
    disable(POLY_OFFSET);disable(MULTISAMPLE)
    # 3b. the temporal accumulation: this frame's colour blended with the history
    if temporal is not None and FRAGMENT_RESOLVE:
        nxt=1-temporal['idx']
        bindfbo(FB,temporal['curfbo']);drawbuffer(COLOR0);disable(BLEND);disable(DEPTH_TEST)
        use(current_prog);bind_unit(4,TEXMS,accum);uniform1i(loc(current_prog,b'alpha_st_accum'),4)
        uniform1i(loc(current_prog,b'st_samples_hw'),n);uniform1i(loc(current_prog,b'st_samples'),total)
        draw(4,0,3)
        bindfbo(FB,temporal['fbo'][nxt]);drawbuffer(COLOR0)
        use(resolve_prog);bind_unit(6,TEXMS,depth);bind_unit(7,TEX2D,temporal['tex'][temporal['idx']]);bind_unit(8,TEX2D,temporal['cur'])
        uniform1i(loc(resolve_prog,b'alpha_st_current'),8);uniform1i(loc(resolve_prog,b'alpha_st_depth'),6);uniform1i(loc(resolve_prog,b'alpha_st_history'),7)
        uniform1i(loc(resolve_prog,b'st_samples_hw'),n)
        fn('glUniformMatrix4fv',None,I,I,C.c_ubyte,C.POINTER(C.c_float))(loc(resolve_prog,b'stoch_reproj'),1,0,(C.c_float*16)(*temporal['reproj']))
        uniform1f(loc(resolve_prog,b'stoch_weight'),temporal.get('weight',1/8));uniform1i(loc(resolve_prog,b'stoch_history_valid'),1 if temporal['valid'] else 0)
        draw(4,0,3)
        temporal['idx']=nxt;temporal['valid']=True
    elif temporal is not None:
        nxt=1-temporal['idx']
        use(resolve_prog);bind_unit(4,TEXMS,accum);bind_unit(6,TEXMS,depth);bind_unit(7,TEX2D,temporal['tex'][temporal['idx']])
        uniform1i(loc(resolve_prog,b'alpha_st_accum'),4);uniform1i(loc(resolve_prog,b'alpha_st_depth'),6);uniform1i(loc(resolve_prog,b'alpha_st_history'),7)
        uniform1i(loc(resolve_prog,b'st_samples_hw'),n);uniform1i(loc(resolve_prog,b'st_samples'),total)
        fn('glUniformMatrix4fv',None,I,I,C.c_ubyte,C.POINTER(C.c_float))(loc(resolve_prog,b'stoch_reproj'),1,0,(C.c_float*16)(*temporal['reproj']))
        uniform1f(loc(resolve_prog,b'stoch_weight'),temporal.get('weight',1/8));uniform1i(loc(resolve_prog,b'stoch_history_valid'),1 if temporal['valid'] else 0)
        bindimage(0,temporal['tex'][nxt],0,0,0,WRITE_ONLY,RGBA16F)   # the shader's binding = 0
        dispatch((W+15)//16,(H+15)//16,1)
        # as LLPipeline::resolveAlphaStochasticTemporal: the stores are read by the composite's texture fetches and by the next
        # frame's resolve; the image-access bit orders the stores of a later frame after the fetches of this one
        membarrier(TEXTURE_FETCH_BARRIER|IMAGE_ACCESS_BARRIER);bindimage(0,0,0,0,0,WRITE_ONLY,RGBA16F)
        temporal['idx']=nxt;temporal['valid']=True
    # 4. composite over the scene, then the glow into its alpha
    bindfbo(FB,scene_fbo);drawbuffer(COLOR0);clearcolor(*scene_rgba);clear(0x4000)
    disable(DEPTH_TEST);enable(BLEND);blendsep(ONE,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
    use(composite);bind_unit(4,TEXMS,accum);bind_unit(5,TEXMS,trans)
    bind_unit(7,TEX2D,temporal['tex'][temporal['idx']] if temporal is not None else opaque_depth)
    uniform1i(loc(composite,b'alpha_st_accum'),4);uniform1i(loc(composite,b'alpha_st_trans'),5);uniform1i(loc(composite,b'alpha_st_resolved'),7)
    uniform1i(loc(composite,b'st_temporal'),1 if temporal is not None else 0)
    uniform1i(loc(composite,b'st_samples_hw'),n);uniform1i(loc(composite,b'st_samples'),total);uniform1i(loc(composite,b'st_glow'),0)
    draw(4,0,3)
    blendsep(ZERO,ONE,ONE,ONE);uniform1i(loc(composite,b'st_glow'),1);draw(4,0,3)
    enable(DEPTH_TEST);assert geterror()==0
    out=(C.c_float*(W*H*4))();readbuffer(COLOR0);readpixels(0,0,W,H,RGBA,FLOAT,out)
    pixels=[tuple(out[i*4:i*4+4]) for i in range(W*H)]
    return (pixels,bits) if read_bits else pixels

# ---- the alpha programs compile, plain and as their ALPHA_STOCHASTIC variant, with the defines the
# viewer gives them (llviewershadermgr.cpp), the indexed-texture lookup declared as the viewer
# injects it; compile only (the shared functions are linked from other objects in the viewer)
alpha_programs={
    'class2/deferred/alphaF.glsl':['USE_VERTEX_COLOR','HAS_ALPHA_MASK','USE_INDEXED_TEX','HAS_SUN_SHADOW'],
    'class1/deferred/fullbrightF.glsl':['HAS_ALPHA_MASK','IS_ALPHA','HAS_DIFFUSE_LOOKUP'],
    'class2/deferred/pbralphaF.glsl':['DIFFUSE_ALPHA_MODE 1','HAS_NORMAL_MAP','HAS_SPECULAR_MAP','HAS_EMISSIVE_MAP','USE_VERTEX_COLOR','HAS_SUN_SHADOW'],
    'class3/deferred/materialF.glsl':['DIFFUSE_ALPHA_MODE 1','HAS_NORMAL_MAP','HAS_SPECULAR_MAP','HAS_ALPHA_MASK','HAS_SUN_SHADOW'],
}
compiled=0
for rel,defines in alpha_programs.items():
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

checks=0
def L(z,a,rgb=(1.,1.,1.),seed=0,glow=0.,slope=0.): return (z,a,rgb,seed,glow,slope)

# ---- mask coverage: per sample, exactly the model's bits; R samples in all, mean alpha S
for total,n in ((16,8),(8,8),(4,4)):
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
print(f'{checks} coverage cases: every sample as the model predicts (16 in two passes of 8, 8, 4), R = floor or ceil of alpha S, mean alpha S')

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
for total,n in ((16,8),(8,8),(4,4)):
    pix=stochastic([L(0.3,0.5,seed=7),L(0.6,0.25,seed=9),L(0.45,0.1,seed=11)],total,n)
    want=1-(1-0.5)*(1-0.25)*(1-0.1)
    worst=max(abs(p[c]-want) for p in pix for c in range(3))
    assert worst<2e-3,('total alpha',total,worst);checks+=1
print(f'total alpha: 1 - prod(1 - alpha) = {want:.4f} on every pixel within 2e-3 (S = 16, 8, 4)')

# ---- visibility: red alpha 0.5 in front of green alpha 0.5 is exact at S = 16 and 8
for total,n in ((16,8),(8,8)):
    pix=stochastic([L(0.6,0.5,(0.,1.,0.),seed=3),L(0.3,0.5,(1.,0.,0.),seed=4)],total,n)
    worst=max(max(abs(p[0]-0.5),abs(p[1]-0.25),abs(p[2])) for p in pix)
    assert worst<2e-3,('two layers',total,worst);checks+=1
# a noisy pair (alpha 0.3 over 0.6): the mean over the pixels converges to the exact image
pix=stochastic([L(0.3,0.3,(1.,0.,0.),seed=5),L(0.6,0.6,(0.,1.,0.),seed=6)],16,8)
exact=(0.3,0.6*0.7,0.)
mean=[sum(p[c] for p in pix)/len(pix) for c in range(3)]
rmse=math.sqrt(sum((p[c]-exact[c])**2 for p in pix for c in range(3))/(3*len(pix)))
assert all(abs(m-e)<0.01 for m,e in zip(mean,exact)),('mean',mean,exact)
assert rmse<0.05,('rmse',rmse);checks+=1
print(f'visibility: two layers of alpha 0.5 exact at S = 16 and 8; alpha 0.3 over 0.6 at S = 16: mean '
      f'({mean[0]:.3f}, {mean[1]:.3f}) for ({exact[0]:.3f}, {exact[1]:.3f}), RMSE {rmse*255:.1f}/255')

# an opaque occluder on the right half, at window depth 0.7 (the layers: 0.65 and 0.8), hides the
# back layer there
set_opaque(1.0,occluder=(W//2,0.7))
pix=stochastic([L(0.6,0.5,(0.,1.,0.),seed=3),L(0.3,0.5,(1.,0.,0.),seed=4)],16,8,scene_rgba=(0.,0.,1.,0.))
for y in range(H):
    for x in range(W):
        p=pix[y*W+x]
        want=(0.5,0.25,0.25) if x<W//2 else (0.5,0.,0.5)
        assert all(abs(p[c]-want[c])<2e-3 for c in range(3)),('occluder',x,y,p,want)
checks+=1
print('occlusion: the layer behind the opaque surface leaves no colour and no coverage')

# ---- glow: the back layer's glow 0.8, visible on half its samples, over a scene glow of 0.5
set_opaque(1.0)
pix=stochastic([L(0.6,0.5,(0.,1.,0.),seed=3,glow=0.8),L(0.3,0.5,(1.,0.,0.),seed=4)],16,8,scene_rgba=(0.,0.,0.,0.5))
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
    for total,n in ((16,8),(8,8),(4,4)):
        worst=white_everywhere(stochastic([L(0.5,1.0,seed=8,slope=sl)],total,n,scene_rgba=BLUE))
        assert worst<2e-3,('decal on the plane',sl,total,worst)
        worst=white_everywhere(stochastic([L(0.5-ndc_shift(sl,1/3),1.0,seed=8,slope=sl)],total,n,scene_rgba=BLUE))
        assert worst<2e-3,('face in front of the plane',sl,total,worst)
        for fraction in (1/3,1/16):
            behind=stochastic([L(0.5+ndc_shift(sl,fraction),1.0,seed=8,slope=sl)],total,n,scene_rgba=BLUE)
            shown=sum(1 for p in behind if p!=BLUE)
            assert shown==0,('face behind the plane shows through it',sl,fraction,total,shown,W*H)
        checks+=1
print(f'{len(SLOPES)} sloped opaque planes, S = 16, 8, 4: a face on the plane or a third of a pixel\'s depth in front '
      'of it covers every pixel; one a third or a sixteenth of a pixel behind it leaves the scene exactly as sorted alpha does')

# a decal on 60 random planes (inside the depth range), every pixel covered at S = 16
rnd=random.Random(5);tried=0
while tried<60:
    sl=(rnd.uniform(-0.35,0.35),rnd.uniform(-0.18,0.18))
    if abs(sl[0])+abs(sl[1])>0.45: continue
    tried+=1;set_opaque(0.5,slope=sl[0],slope_y=sl[1])
    worst=white_everywhere(stochastic([L(0.5,1.0,seed=100+tried,slope=sl)],16,8,scene_rgba=BLUE))
    assert worst<2e-3,('decal on a random plane',sl,worst)
checks+=1
print('a decal on 60 random sloped planes: every pixel covered (S = 16)')

# the same decal beside a depth edge: an opaque occluder nearer the eye over the right half; the
# pixels next to the edge take the plane's gradient from their own side
sl=(0.05,-0.03)
set_opaque(0.5,slope=sl[0],slope_y=sl[1],occluder=(W//2,0.3))
pix=stochastic([L(0.5,1.0,seed=8,slope=sl)],16,8,scene_rgba=BLUE)
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
pix=stochastic([L(0.5+ndc_shift(sl,1/3),1.0,(1.,1.,1.),seed=8,slope=sl),L(0.4,0.5,(1.,0.,0.),seed=9,slope=sl)],16,8,scene_rgba=BLUE)
worst=max(max(abs(p[0]-0.5),abs(p[1]),abs(p[2]-0.5)) for p in pix)
assert worst<2e-3,('front face over a hidden layer',worst);checks+=1
print('a half-transparent face over an alpha 1 layer hidden behind the plane: exactly the face over the scene')

# the regression: the spread and offsets of 1.0.1.81943 let the face behind the plane through (a
# mesh body's blended skin through the dress fitted over it); the check must see it
leaks=[]
for sl in SLOPES:
    set_opaque(0.5,slope=sl[0],slope_y=sl[1])
    behind=stochastic([L(0.5+ndc_shift(sl,1/3),1.0,seed=8,slope=sl)],16,8,scene_rgba=BLUE,before=True)
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
        uniform1i(loc(mat_variant,b'st_mode'),mode);uniform1i(loc(mat_variant,b'st_iteration'),it)
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
    for total,n in ((16,8),(8,8),(4,4)):
        got=material_layer(0.0,dif,bump,spec,env,gloss,total,n,SKIN,fixed=True)
        worst=worst_rgb(got,ref)
        # one layer is exact (its accumulation passes on every sample, the total is exact); the
        # tolerance is the 16-bit float targets'
        assert worst<4e-3,('glaring material against sorted alpha',name,total,worst)
    bad=material_layer(0.0,dif,bump,spec,env,gloss,16,8,SKIN,fixed=False)
    before.append((name,worst_rgb(bad,ref),sum(1 for p,q in zip(bad,ref) if max(abs(p[c]-q[c]) for c in range(3))>4e-3)))
    checks+=1
untouched=material_sorted(0.0,red_clear,own_bump_matte,own_spec_matte,0.4,0.6,SKIN)
assert all(max(abs(p[c]-SKIN[c]) for c in range(3))<1e-3 for p in untouched),('sorted alpha should leave the skin as it is',untouched[0],untouched[W*H//2])
print(f'{len(CASES)} blended legacy materials that can glare (materialF.glsl), S = 16, 8, 4: the composite equals sorted '
      'alpha within 4e-3; an alpha 0 layer with matte maps leaves the opaque surface exactly as sorted alpha does')
for name,worst,count in before:
    print(f'  before (alpha-only passes reading another face\'s normal and specular maps instead of its own): {name}: {count} of {W*H} pixels differ, worst {worst:.3f}')
assert before[0][2]==W*H and before[1][2]==W*H,('the old binding should change every pixel under the alpha 0 layer',before)
assert before[2][2]==W*H,('the old binding should lose the shiny glass reflections',before)

# ---- the temporal accumulation (alphaStochasticResolveC.glsl, a compute shader, with the
# engine's resolve; the composite's st_temporal)
# 8 samples in one pass, the masks' seeds changing every frame (st_frame), the last frame's colour moved
# by the camera and clamped to this frame's 3 x 3 neighbourhood, blended at 1/8.
IDENT=[1.,0.,0.,0., 0.,1.,0.,0., 0.,0.,1.,0., 0.,0.,0.,1.]
def new_temporal(reproj=IDENT):
    tex=[tex2d(RGBA16F,RGBA,FLOAT) for _ in range(2)]
    if not FRAGMENT_RESOLVE: return dict(tex=tex,idx=0,valid=False,reproj=list(reproj))
    fbos=[fbo((COLOR0,TEX2D,x)) for x in tex]
    cur=tex2d(RGBA32F,RGBA,FLOAT);curfbo=fbo((COLOR0,TEX2D,cur))   # this frame's averaged samples
    return dict(tex=tex,fbo=fbos,cur=cur,curfbo=curfbo,idx=0,valid=False,reproj=list(reproj))
def rmse(pix,want,cols=None):
    xs=range(W) if cols is None else cols
    return math.sqrt(sum((pix[y*W+x][c]-want[c])**2 for y in range(H) for x in xs for c in range(3))/(3*H*len(xs)))
def frame_rmse(a,b): return math.sqrt(sum((p[c]-q[c])**2 for p,q in zip(a,b) for c in range(3))/(3*len(a)))
def scene(alphas=(0.2,0.4,0.5)): return [L(0.3,alphas[0],(1.,0.,0.),seed=5),L(0.45,alphas[1],(0.,1.,0.),seed=6),L(0.6,alphas[2],(0.,0.,1.),seed=7)]
def exact(alphas=(0.2,0.4,0.5)): return (alphas[0],alphas[1]*(1-alphas[0]),alphas[2]*(1-alphas[0])*(1-alphas[1]))
set_opaque(1.0)
want=exact()
# no history: the same image as the composite without the temporal accumulation
TEMPORAL_FRAME[0]=1
plain=stochastic(scene(),8,8,temporal=None)
T=new_temporal();first=stochastic(scene(),8,8,temporal=T)
assert frame_rmse(first,plain)<3e-3,('the first frame has no history',frame_rmse(first,plain))
checks+=1
# a static view: the noise averages over the frames
single8=[];single16=None
for f in range(1,9):
    TEMPORAL_FRAME[0]=f;single8.append(rmse(stochastic(scene(),8,8),want))
TEMPORAL_FRAME[0]=0;single16=rmse(stochastic(scene(),16,8),want)
T=new_temporal();errs={};prev=None
for f in range(1,41):
    TEMPORAL_FRAME[0]=f;pix=stochastic(scene(),8,8,temporal=T)
    if f in (1,8,16,40): errs[f]=rmse(pix,want)
    if f==39: prev=pix
flicker=frame_rmse(prev,pix)
TEMPORAL_FRAME[0]=1;a=stochastic(scene(),8,8);TEMPORAL_FRAME[0]=2;b=stochastic(scene(),8,8);flicker8=frame_rmse(a,b)
mean8=sum(single8)/len(single8)
assert errs[40]<0.55*mean8,('8 samples with the temporal accumulation against 8 samples',errs,mean8)
assert errs[40]<single16,('and against 16 samples in two passes',errs[40],single16)
assert flicker<0.25*flicker8,('the grain holds still between frames',flicker,flicker8)
checks+=1
print(f'temporal accumulation, a static view (three layers of alpha 0.2, 0.4, 0.5, rmse/255): 8 samples {mean8*255:.2f}, 16 samples {single16*255:.2f}; 8 samples '
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
for total,n in ((4,4),(8,8),(16,8)):
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
checks+=1
print('alpha programs: every interpolated fragment input is `centroid in`')

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
for total,n in ((4,4),(8,8),(16,8)):
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

print(f'{checks} stochastic transparency cases passed.')
gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
