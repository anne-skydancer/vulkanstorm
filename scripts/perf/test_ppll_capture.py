"""Offscreen alpha OIT (image-array A-buffer) regression, no viewer launch; --opengl PATH selects Mesa.

Reuse the standalone Windows GL bootstrap and shader compiler.
Exercise the actual append function from each shader family against a 3x1 target:
detached depth snapshot, LEQUAL equality, occlusion, slot fill, tail overflow and reset, then the
resolve shader with the viewer's image unit layout: composite colour and the tail zeroed.
The 8-byte node encoding (alphaOITPackF.glsl: R11F_G11F_B10F premultiplied colour, 24-bit depth,
8-bit alpha) is checked against an independent Python encoder: exact bits, round-trip error, and
the order of two fragments 1e-6 apart in window depth.
"""
from pathlib import Path
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
barrier=fn('glMemoryBarrier',None,U)
gentex=fn('glGenTextures',None,I,C.POINTER(U));bindtex=fn('glBindTexture',None,U,U)
texstorage=fn('glTexStorage2D',None,U,I,U,I,I);texstorage3=fn('glTexStorage3D',None,U,I,U,I,I,I)
texparam=fn('glTexParameteri',None,U,U,I)
texsub=fn('glTexSubImage2D',None,U,I,I,I,I,I,U,U,P)
copy=fn('glCopyImageSubData',None,U,U,I,I,I,I,U,U,I,I,I,I,I,I,I)
image=fn('glBindImageTexture',None,U,U,I,C.c_ubyte,I,U,U)
cleartex=fn('glClearTexImage',None,U,I,U,U,P)
gettex=fn('glGetTexImage',None,U,I,U,U,P)
genfbo=fn('glGenFramebuffers',None,I,C.POINTER(U));bindfbo=fn('glBindFramebuffer',None,U,U)
attachtex=fn('glFramebufferTexture2D',None,U,U,U,U,I)
geterror=fn('glGetError',U);uniform=fn('glUniform1i',None,I,I);loc=fn('glGetUniformLocation',I,U,C.c_char_p)
draw=fn('glDrawArrays',None,U,I,I)
clearcolor=fn('glClearColor',None,C.c_float,C.c_float,C.c_float,C.c_float);clear=fn('glClear',None,U)
blendfunc=fn('glBlendFunc',None,U,U);colormask=fn('glColorMask',None,C.c_ubyte,C.c_ubyte,C.c_ubyte,C.c_ubyte)
TEX2D=0x0DE1;TEX2DARRAY=0x8C1A;R32UI=0x8236;RG32UI=0x823C;RED_INT=0x8D94;RG_INT=0x8228;UINT=0x1405
META_LAYERS=7;SLOT_LAYERS=2;W=3
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
textures=(U*3)();gentex(3,textures)   # live depth, detached depth snapshot, colour
for name,fmt in zip(textures,(0x81A6,0x81A6,0x8058)):
    bindtex(TEX2D,name);texstorage(TEX2D,1,fmt,W,1)
    texparam(TEX2D,0x2801,0x2600);texparam(TEX2D,0x2800,0x2600)
arrays=(U*2)();gentex(2,arrays)       # meta (count + tail), slots
bindtex(TEX2DARRAY,arrays[0]);texstorage3(TEX2DARRAY,1,R32UI,W,1,META_LAYERS)
bindtex(TEX2DARRAY,arrays[1]);texstorage3(TEX2DARRAY,1,RG32UI,W,1,SLOT_LAYERS)
fbo=U();genfbo(1,C.byref(fbo));bindfbo(0x8D40,fbo)
attachtex(0x8D40,0x8CE0,TEX2D,textures[2],0);attachtex(0x8D40,0x8D00,TEX2D,textures[0],0)
assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
fn('glViewport',None,I,I,I,I)(0,0,W,1);fn('glDisable',None,U)(0x0B71);fn('glEnable',None,U)(0x0BE2)
vertex='#version 430 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}'
# The node encoding, linked as its own fragment shader object as the viewer attaches it.
pack=(root/'indra/newview/app_settings/shaders/class1/deferred/alphaOITPackF.glsl').read_text()
pack_stage=(0x8B30,'#version 430 core\n'+pack[pack.index('*/')+2:])
# The viewer's resolve shader as compiled by the viewer (version line + extension).
resolve=(root/'indra/newview/app_settings/shaders/class1/deferred/alphaOITResolveF.glsl').read_text()
resolve='#version 430 core\n'+resolve[resolve.index('#extension'):].replace('/*[EXTRA_CODE_HERE]*/','')
resolve_prog=program([(0x8B31,vertex),(0x8B30,resolve),pack_stage])

# Independent reference encoder, written from the GL_R11F_G11F_B10F format definition:
# 5-bit exponent (bias 15), 6/6/5 mantissa bits, no sign; this encoder rounds to nearest even
# and clamps above the largest finite value instead of producing infinity.
def f32(x): return struct.unpack('<f',struct.pack('<f',x))[0]
def ref_pack_uf(v,mbits):
    if not v>0: return 0
    if v<2.0**-14: return round(v*2.0**(14+mbits))   # exact scaling; round() is ties-to-even
    m,e=math.frexp(v); E=e-1                         # v = (2m) * 2^E, 2m in [1,2)
    q=round((2*m-1)*2**mbits)
    if q==2**mbits: q=0;E+=1
    if E>15: return (30<<mbits)|(2**mbits-1)
    return ((E+15)<<mbits)|q
def ref_unpack_uf(p,mbits):
    e,m=p>>mbits,p&(2**mbits-1)
    return m*2.0**(-14-mbits) if e==0 else (1+m/2**mbits)*2.0**(e-15)
def ref_pack_rgb(r,g,b): return ref_pack_uf(r,6)|(ref_pack_uf(g,6)<<11)|(ref_pack_uf(b,5)<<22)
def ref_depth24(z): return round(f32(min(max(z,0.),1.)*16777215.0))   # fp32 product, ties-to-even
def ref_key(z,a): return (ref_depth24(z)<<8)|round(f32(min(max(a,0.),1.)*255.0))

# Round trip through the shader encoder: a compute pass packs each value into all three
# channels and unpacks the result; the bits must equal the reference encoder's, and the
# decoded value must lie within half a mantissa step (relative) of the input, or half a
# denormal step (absolute) below 2^-14, or at the clamp above the largest finite value.
values=[0.0,1e-4,0.5,1.0,100.0,60000.0,-1.0,1e-7,6.1e-5,3.14159,1e6,0.00390625,64000.0,65100.0]
values=[f32(v) for v in values]
compute='#version 430 core\nlayout(local_size_x=1) in;\n'
compute+='layout(std430,binding=0) readonly buffer In { float vin[]; };\n'
compute+='layout(std430,binding=1) writeonly buffer Out { uvec4 vout[]; };\n'
compute+='uint oit_pack_rgb(vec3 c); vec3 oit_unpack_rgb(uint p);\n'
compute+='void main(){ uint i=gl_GlobalInvocationID.x; float x=vin[i]; uint p=oit_pack_rgb(vec3(x));\n'
compute+='  vec3 u=oit_unpack_rgb(p); vout[i]=uvec4(p,floatBitsToUint(u.r),floatBitsToUint(u.g),floatBitsToUint(u.b)); }\n'
compute_prog=program([(0x91B9,compute),(0x91B9,pack_stage[1])])
genbuf=fn('glGenBuffers',None,I,C.POINTER(U));bindbuf=fn('glBindBuffer',None,U,U)
bufdata=fn('glBufferData',None,U,S,P,U);bindbase=fn('glBindBufferBase',None,U,U,U)
getbuf=fn('glGetBufferSubData',None,U,S,S,P)
ssbo=(U*2)();genbuf(2,ssbo);SSBO=0x90D2
vin=(C.c_float*len(values))(*values)
bindbuf(SSBO,ssbo[0]);bufdata(SSBO,C.sizeof(vin),vin,0x88E4)
bindbuf(SSBO,ssbo[1]);bufdata(SSBO,16*len(values),None,0x88E8)
bindbase(SSBO,0,ssbo[0]);bindbase(SSBO,1,ssbo[1])
use(compute_prog);fn('glDispatchCompute',None,U,U,U)(len(values),1,1);barrier(0xFFFFFFFF)
vout=(U*(4*len(values)))();bindbuf(SSBO,ssbo[1]);getbuf(SSBO,0,C.sizeof(vout),vout)
roundtrip=0
for i,v in enumerate(values):
    p=vout[4*i]
    assert p==ref_pack_rgb(v,v,v),('pack bits',v,hex(p),hex(ref_pack_rgb(v,v,v)))
    for c,mbits,shift in ((0,6,0),(1,6,11),(2,5,22)):
        ref=ref_unpack_uf((p>>shift)&0x7FF,mbits)
        got=struct.unpack('<f',struct.pack('<I',vout[4*i+1+c]))[0]
        assert got==ref,('shader unpack',v,c,got,ref)
        largest=(2-2.0**-mbits)*2.0**15
        if v<=0: assert ref==0.0,(v,ref)
        elif v>=largest: assert ref==largest,(v,ref)
        elif v<2.0**-14: assert abs(ref-v)<=2.0**(-15-mbits),('denormal error',v,c,ref)
        else: assert abs(ref-v)<=v*2.0**(-1-mbits),('relative error',v,c,ref,abs(ref-v)/v)
    roundtrip+=1
fn('glDeleteProgram',None,U)(compute_prog)
files=['class1/deferred/fullbrightF.glsl','class2/deferred/alphaF.glsl','class2/deferred/pbralphaF.glsl','class3/deferred/materialF.glsl']
# Fragment colour (1,0,0,0.5) twice per visible pixel, then once with alpha 0 (must be ignored);
# with one slot the second fragment is
# the only tail entry: rgb*a, a and -log2(1-a) in the capture shaders' fixed point, and its
# depth as inverted float bits.
TAIL_R=2048;TAIL_A=32768;TAIL_LOG=65536
checks=0
for rel in files:
    source=(root/'indra/newview/app_settings/shaders'/rel).read_text()
    start=source.index('void oit_append(');end=source.index('\n}',start)+2
    helper=source[start:end]
    declarations='''#version 430 core
layout(binding=0,r32ui) uniform coherent uimage2DArray oit_meta;
layout(binding=1,rg32ui) uniform writeonly uimage2DArray oit_slots;
uniform int oit_slot_count;
uniform sampler2D alpha_oit_opaque_depth;
uniform float test_depth;
uniform float test_alpha;
uniform vec3 test_rgb;
uint oit_pack_rgb(vec3 c);
uint oit_pack_depth_alpha(float z, float a);
#define OIT_TAIL_COLOR_SCALE 4096.0
#define OIT_TAIL_ALPHA_SCALE 65536.0
#define OIT_TAIL_LOG_SCALE   65536.0
out vec4 color;
'''
    prog=program([(0x8B31,vertex),(0x8B30,declarations+helper+'\nvoid main(){oit_append(vec4(test_rgb,test_alpha),test_depth);color=vec4(0.,1.,0.,1.);}'),pack_stage])
    rgb_loc=loc(prog,b'test_rgb');set_rgb=fn('glUniform3f',None,I,C.c_float,C.c_float,C.c_float)
    for cycle in range(6):
        depth=(C.c_float*3)(0.25,0.5,0.75) if cycle%2==0 else (C.c_float*3)(0.75,0.5,0.25)
        bindtex(TEX2D,textures[0]);texsub(TEX2D,0,0,0,W,1,0x1902,0x1406,depth)
        copy(textures[0],TEX2D,0,0,0,0,textures[1],TEX2D,0,0,0,0,W,1,1)
        # The live draw attachment changes; the detached snapshot must not.
        changed=(C.c_float*3)(0.,0.,0.);texsub(TEX2D,0,0,0,W,1,0x1902,0x1406,changed)
        bindtex(TEX2D,textures[1]);snapshot=(C.c_float*3)()
        gettex(TEX2D,0,0x1902,0x1406,snapshot)
        assert all(abs(a-b)<1e-6 for a,b in zip(snapshot,depth))
        hidden=0 if cycle%2==0 else 2   # fragment at the middle depth is behind this pixel's opaque depth
        for slots in (SLOT_LAYERS,1):
            # Unbind the image units first: NVIDIA (621.01) orders glClearTexImage after later
            # shader atomics while the texture is still bound as an image.
            image(0,0,0,1,0,0x88BA,R32UI);image(1,0,0,1,0,0x88B9,RG32UI)
            barrier(0xFFFFFFFF);zero=U(0)
            cleartex(arrays[0],0,RED_INT,UINT,C.byref(zero))   # counts + tail reset in one clear; slots never cleared
            barrier(0xFFFFFFFF)
            image(0,arrays[0],0,1,0,0x88BA,R32UI);image(1,arrays[1],0,1,0,0x88B9,RG32UI)
            use(prog);uniform(loc(prog,b'oit_slot_count'),slots);uniform(loc(prog,b'alpha_oit_opaque_depth'),0);set_rgb(rgb_loc,1.,0.,0.)
            # Exactly equal to the stored middle depth, testing LEQUAL precisely.
            fn('glUniform1f',None,I,C.c_float)(loc(prog,b'test_depth'),snapshot[1])
            alpha=loc(prog,b'test_alpha');fn('glUniform1f',None,I,C.c_float)(alpha,0.5)
            draw(4,0,3);draw(4,0,3)
            # A third, fully transparent fragment must take no slot and leave the tail alone.
            fn('glUniform1f',None,I,C.c_float)(alpha,0.0);draw(4,0,3);barrier(0xFFFFFFFF)
            meta=(U*(META_LAYERS*W))();bindtex(TEX2DARRAY,arrays[0]);gettex(TEX2DARRAY,0,RED_INT,UINT,meta)
            slot=(U*(SLOT_LAYERS*W*2))();bindtex(TEX2DARRAY,arrays[1]);gettex(TEX2DARRAY,0,RG_INT,UINT,slot)
            counts=list(meta[0:W]);tail=[list(meta[l*W:(l+1)*W]) for l in range(1,META_LAYERS)]
            for x in range(W):
                if x==hidden:
                    assert counts[x]==0,('hidden fragment took a slot',rel,cycle,slots,counts)
                    assert all(t[x]==0 for t in tail),('hidden fragment reached the tail',rel,cycle,slots,tail)
                    continue
                assert counts[x]==2,(rel,cycle,slots,counts)
                stored=min(2,slots)
                for layer in range(stored):
                    texel=slot[(layer*W+x)*2:(layer*W+x)*2+2]
                    # premultiplied (0.5,0,0); depth24 of the snapshot depth, alpha 0.5 -> 128
                    assert texel[1]==ref_key(snapshot[1],0.5),('slot depth/alpha',rel,cycle,slots,layer,list(texel))
                    assert texel[0]==ref_pack_rgb(0.5,0.,0.),('slot colour',rel,list(texel))
                near=(~struct.unpack('<I',struct.pack('<f',snapshot[1]))[0])&0xFFFFFFFF
                expected=(0,0,0,0,0,0) if slots>=2 else (TAIL_R,0,0,TAIL_A,TAIL_LOG,near)
                assert tuple(t[x] for t in tail)==expected,('tail',rel,cycle,slots,[t[x] for t in tail])
            # Resolve with the viewer's binding layout (pipeline.cpp compositeAlphaOIT): meta read on
            # unit 0, slots read on unit 1, meta write-only on unit 2 for the tail reset. Two red
            # fragments at alpha 0.5 over black give 0.75 red whether both sat in slots or one in
            # the tail; the tail layers must be zero afterwards, the counts untouched.
            clearcolor(0.,0.,0.,1.);clear(0x4000)
            image(0,arrays[0],0,1,0,0x88B8,R32UI);image(1,arrays[1],0,1,0,0x88B8,RG32UI);image(2,arrays[0],0,1,0,0x88B9,R32UI)
            use(resolve_prog);uniform(loc(resolve_prog,b'oit_slot_count'),slots);uniform(loc(resolve_prog,b'depthMap'),0)
            blendfunc(1,0x0303);colormask(1,1,1,0)
            draw(4,0,3);barrier(0xFFFFFFFF);colormask(1,1,1,1)
            image(0,0,0,1,0,0x88B8,R32UI);image(1,0,0,1,0,0x88B8,RG32UI);image(2,0,0,1,0,0x88B9,R32UI)
            bindtex(TEX2DARRAY,arrays[0]);gettex(TEX2DARRAY,0,RED_INT,UINT,meta)
            assert list(meta[0:W])==counts,('resolve changed the counts',rel,cycle,slots,list(meta[0:W]))
            assert all(v==0 for v in meta[W:]),('resolve left a tail',rel,cycle,slots,list(meta[W:]))
            pixels=(C.c_ubyte*(W*4))();bindtex(TEX2D,textures[2]);gettex(TEX2D,0,0x1908,0x1401,pixels)
            for x in range(W):
                want=(0,0,0) if x==hidden else (191,0,0)
                assert all(abs(pixels[x*4+c]-want[c])<=1 for c in range(3)),('resolve colour',rel,cycle,slots,x,list(pixels[x*4:x*4+4]))
            bindtex(TEX2D,textures[1]);assert geterror()==0;checks+=1
    # Depth order at the 24-bit step (6e-8): green at z, then red 1e-6 behind it, captured near
    # first. Back to front over black: red then green = (0.25, 0.5, 0); swapped would be (0.5, 0.25, 0).
    far=(C.c_float*3)(1.,1.,1.);bindtex(TEX2D,textures[1]);texsub(TEX2D,0,0,0,W,1,0x1902,0x1406,far)
    z_near=f32(0.5);z_far=f32(0.5+1e-6);assert ref_depth24(z_near)<ref_depth24(z_far)
    image(0,0,0,1,0,0x88BA,R32UI);image(1,0,0,1,0,0x88B9,RG32UI)
    barrier(0xFFFFFFFF);zero=U(0);cleartex(arrays[0],0,RED_INT,UINT,C.byref(zero));barrier(0xFFFFFFFF)
    image(0,arrays[0],0,1,0,0x88BA,R32UI);image(1,arrays[1],0,1,0,0x88B9,RG32UI)
    use(prog);uniform(loc(prog,b'oit_slot_count'),SLOT_LAYERS);uniform(loc(prog,b'alpha_oit_opaque_depth'),0)
    fn('glUniform1f',None,I,C.c_float)(loc(prog,b'test_alpha'),0.5)
    for rgb,z in (((0.,1.,0.),z_near),((1.,0.,0.),z_far)):
        set_rgb(rgb_loc,*rgb);fn('glUniform1f',None,I,C.c_float)(loc(prog,b'test_depth'),z);draw(4,0,3)
    barrier(0xFFFFFFFF)
    clearcolor(0.,0.,0.,1.);clear(0x4000)
    image(0,arrays[0],0,1,0,0x88B8,R32UI);image(1,arrays[1],0,1,0,0x88B8,RG32UI);image(2,arrays[0],0,1,0,0x88B9,R32UI)
    use(resolve_prog);uniform(loc(resolve_prog,b'oit_slot_count'),SLOT_LAYERS);uniform(loc(resolve_prog,b'depthMap'),0)
    blendfunc(1,0x0303);colormask(1,1,1,0);draw(4,0,3);barrier(0xFFFFFFFF);colormask(1,1,1,1)
    image(0,0,0,1,0,0x88B8,R32UI);image(1,0,0,1,0,0x88B8,RG32UI);image(2,0,0,1,0,0x88B9,R32UI)
    pixels=(C.c_ubyte*(W*4))();bindtex(TEX2D,textures[2]);gettex(TEX2D,0,0x1908,0x1401,pixels)
    for x in range(W):
        assert abs(pixels[x*4]-64)<=1 and abs(pixels[x*4+1]-128)<=1 and pixels[x*4+2]==0,('depth order',rel,x,list(pixels[x*4:x*4+4]))
    bindtex(TEX2D,textures[1]);assert geterror()==0;checks+=1
    fn('glDeleteProgram',None,U)(prog)
fn('glDeleteProgram',None,U)(resolve_prog)
print(f'{roundtrip} R11F_G11F_B10F round trips matched the reference encoder within half a mantissa step.')
print(f'{checks} alpha OIT cases passed: four append shaders, detached depth, equality, occlusion, slot fill, tail overflow, zero alpha ignored, reset, resolve colour and tail reset, depth order 1e-6 apart.')
gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
