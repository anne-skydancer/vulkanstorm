# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offscreen stochastic transparency regression, no viewer launch; --opengl PATH selects Mesa.

Reuses the standalone Windows GL bootstrap and shader compiler (gl_test_context.py) and runs the
viewer's own stochastic transparency shaders (alphaStochasticF.glsl, alphaStochasticExpandF.glsl,
alphaStochasticCompositeF.glsl) with the pass structure and GL state of LLPipeline /
LLDrawPoolAlpha::renderStochastic on a small target:
  * the mask table: the generator (a port of LLPipeline::alphaStochasticMaskTable) gives exactly r
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
  * a face lying on an opaque surface (a decal on a sloped plane) is covered on every sample with
    the passes' polygon offset (and the check shows it is not without).
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
W,H=64,32
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
viewport(0,0,W,H);disable(CULL)
getint(0x9110,C.byref(max_ms:=I()))   # GL_MAX_DEPTH_TEXTURE_SAMPLES
assert max_ms.value>=8,('needs 8 depth samples',max_ms.value)

# ---- the mask table: LLPipeline::alphaStochasticMaskTable, ported
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
    r=min(int(math.floor(f32(f32(min(max(a,0.),1.)*s)+v[0]))),s)
    col=min(int(f32(v[1]*SEEDS)),SEEDS-1)
    return table[r*SEEDS+col]

# ---- programs: the viewer's shaders as the viewer compiles them (#version 420, no extra code)
def viewer(name):
    text=(shaders/name).read_text()
    return '#version 420\n'+text.replace('/*[EXTRA_CODE_HERE]*/','')
helper=viewer('alphaStochasticF.glsl')
vertex='''#version 420
uniform float z; uniform float slope;
void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2.0-1.0; gl_Position=vec4(p,z+slope*p.x,1.0); }'''
# the alpha programs' ALPHA_STOCHASTIC variant: alpha alone, the sample mask
variant=program([(0x8B31,vertex),(0x8B30,'''#version 420
uniform float test_alpha; out vec4 frag_color; int alpha_stochastic_mask(float a);
void main(){ frag_color=vec4(0.0,0.0,0.0,max(test_alpha,0.0)); gl_SampleMask[0]=alpha_stochastic_mask(test_alpha); }'''),(0x8B30,helper)])
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
composite=program([(0x8B31,screen_vertex),(0x8B30,viewer('alphaStochasticCompositeF.glsl'))])
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

def set_opaque(z,occluder=None,slope=0.):
    """The scene depth: z everywhere, or the sloped plane, or z with an occluder (x0, depth)."""
    bindfbo(FB,opaque_fbo);drawbuffer(NONE);depthmask(1);enable(DEPTH_TEST);depthfunc(ALWAYS)
    cleardepth(z);clear(0x100)
    if slope:
        use(shade);uniform1f(loc(shade,b'z'),z);uniform1f(loc(shade,b'slope'),slope);draw(4,0,3)
    if occluder:
        x0,zo=occluder;fn('glEnable',None,U)(0x0C11);fn('glScissor',None,I,I,I,I)(x0,0,W-x0,H)
        cleardepth(zo);clear(0x100);disable(0x0C11)
    depthfunc(LEQUAL)

def layer(prog,z,alpha=0.,rgb=(1.,1.,1.),seed=0,glow=0.,slope=0.):
    use(prog);uniform1f(loc(prog,b'z'),z);uniform1f(loc(prog,b'slope'),slope)
    if prog==variant: uniform1f(loc(prog,b'test_alpha'),alpha);uniform1i(loc(prog,b'st_seed'),seed)
    elif prog==shade: uniform1f(loc(prog,b'test_alpha'),alpha);uniform3f(loc(prog,b'test_rgb'),*rgb)
    else: uniform1f(loc(prog,b'test_glow'),glow)
    draw(4,0,3)

def spread(n):
    f=targets[n][0];bindfbo(FB,f);drawbuffer(NONE);disable(BLEND);disable(POLY_OFFSET)
    enable(DEPTH_TEST);depthfunc(ALWAYS);depthmask(1)
    use(expand);bind_unit(0,TEX2D,opaque_depth);uniform1i(loc(expand,b'depthMap'),0);draw(4,0,3)
    depthfunc(LEQUAL)

def stochastic(layers,total,n,scene_rgba=(0.,0.,0.,0.),use_offset=True,read_bits=None):
    """LLDrawPoolAlpha::renderStochastic over `layers` [(z, alpha, rgb, seed, glow, slope)], then
    the composite over a scene of `scene_rgba`; returns the scene's pixels (RGBA float)."""
    f,accum,trans,depth=targets[n];iterations=total//n
    bindfbo(FB,f);enable(MULTISAMPLE);disable(0x0C11)
    drawbuffer(COLOR1);clearf(0,(1.,0.,0.,0.));drawbuffer(COLOR0);clearf(0,(0.,0.,0.,0.))
    spread(n)
    use(variant);uniform1i(loc(variant,b'st_samples_hw'),n);uniform1i(loc(variant,b'st_samples'),total)
    bind_unit(1,TEX2D,mask_tex[total]);uniform1i(loc(variant,b'alpha_st_masks'),1)
    def poly(units):
        if use_offset: enable(POLY_OFFSET);offset(-1.,units)
        else: disable(POLY_OFFSET)
    # 1. total alpha
    bindfbo(FB,f);drawbuffer(COLOR1);enable(BLEND);blendsep(ZERO,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
    depthmask(0);poly(-1.);use(variant);uniform1i(loc(variant,b'st_mode'),1)
    for z,a,rgb,seed,glow,slope in layers: layer(variant,z,a,seed=seed,slope=slope)
    bits=[]
    for it in range(iterations):
        if it: spread(n)
        # 2. stochastic depth
        bindfbo(FB,f);drawbuffer(NONE);depthmask(1);poly(-1.)
        use(variant);uniform1i(loc(variant,b'st_mode'),2);uniform1i(loc(variant,b'st_iteration'),it)
        for z,a,rgb,seed,glow,slope in layers: layer(variant,z,a,seed=seed,slope=slope)
        if read_bits:
            bindfbo(FB,bits_fbo);disable(BLEND);disable(DEPTH_TEST);disable(POLY_OFFSET);disable(MULTISAMPLE)
            use(readmask);bind_unit(2,TEXMS,depth);bind_unit(3,TEX2D,opaque_depth)
            uniform1i(loc(readmask,b'st_depth'),2);uniform1i(loc(readmask,b'opaque'),3);uniform1i(loc(readmask,b'samples'),n)
            draw(4,0,3);px=(U*(W*H))();readpixels(0,0,W,H,RED_INT,UINT,px);bits.append(list(px))
            enable(DEPTH_TEST);enable(MULTISAMPLE);bindfbo(FB,f)
        # 3. accumulation, then the glow of the glowing faces
        drawbuffer(COLOR0);depthmask(0);enable(BLEND);blendsep(SRC_ALPHA,ONE,ONE,ONE);poly(-2.)
        for z,a,rgb,seed,glow,slope in layers: layer(shade,z,a,rgb,slope=slope)
        drawbuffer(COLOR1);blendsep(ONE,ONE,ONE,ONE);poly(-3.)
        for z,a,rgb,seed,glow,slope in layers:
            if glow>0: layer(glowprog,z,glow=glow,slope=slope)
    disable(POLY_OFFSET);disable(MULTISAMPLE)
    # 4. composite over the scene, then the glow into its alpha
    bindfbo(FB,scene_fbo);drawbuffer(COLOR0);clearcolor(*scene_rgba);clear(0x4000)
    disable(DEPTH_TEST);enable(BLEND);blendsep(ONE,ONE_MINUS_SRC_ALPHA,ZERO,ONE_MINUS_SRC_ALPHA)
    use(composite);bind_unit(4,TEXMS,accum);bind_unit(5,TEXMS,trans)
    uniform1i(loc(composite,b'alpha_st_accum'),4);uniform1i(loc(composite,b'alpha_st_trans'),5)
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

# ---- a decal: a face on a sloped opaque plane is covered on every sample with the offset only
slope=0.02
set_opaque(0.5,slope=slope)
pix=stochastic([L(0.5,1.0,seed=8,slope=slope)],8,8)
worst=max(abs(p[0]-1.0) for p in pix)
assert worst<2e-3,('decal with offset',worst)
pix=stochastic([L(0.5,1.0,seed=8,slope=slope)],8,8,use_offset=False)
lost=sum(1 for p in pix if p[0]<0.999)
assert lost>0,'without the offset the decal should lose samples (the test would not see it)'
checks+=1
print(f'decal on a sloped plane: covered on every pixel with the offset; without it {lost} of {W*H} pixels lose samples')

print(f'{checks} stochastic transparency cases passed.')
gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
