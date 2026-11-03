"""Offscreen alpha OIT (image-array A-buffer) regression, no viewer launch; --opengl PATH selects Mesa.

Reuse the standalone Windows GL bootstrap and shader compiler.
Exercise the actual append function from each shader family against a 3x1 target:
detached depth snapshot, LEQUAL equality, occlusion, slot fill, tail overflow and reset, then the
resolve shader with the viewer's image unit layout: composite colour and the tail zeroed.
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
TEX2D=0x0DE1;TEX2DARRAY=0x8C1A;R32UI=0x8236;RGBA32UI=0x8D70;RED_INT=0x8D94;RGBA_INT=0x8D99;UINT=0x1405
META_LAYERS=7;SLOT_LAYERS=2;W=3
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
textures=(U*3)();gentex(3,textures)   # live depth, detached depth snapshot, colour
for name,fmt in zip(textures,(0x81A6,0x81A6,0x8058)):
    bindtex(TEX2D,name);texstorage(TEX2D,1,fmt,W,1)
    texparam(TEX2D,0x2801,0x2600);texparam(TEX2D,0x2800,0x2600)
arrays=(U*2)();gentex(2,arrays)       # meta (count + tail), slots
bindtex(TEX2DARRAY,arrays[0]);texstorage3(TEX2DARRAY,1,R32UI,W,1,META_LAYERS)
bindtex(TEX2DARRAY,arrays[1]);texstorage3(TEX2DARRAY,1,RGBA32UI,W,1,SLOT_LAYERS)
fbo=U();genfbo(1,C.byref(fbo));bindfbo(0x8D40,fbo)
attachtex(0x8D40,0x8CE0,TEX2D,textures[2],0);attachtex(0x8D40,0x8D00,TEX2D,textures[0],0)
assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
fn('glViewport',None,I,I,I,I)(0,0,W,1);fn('glDisable',None,U)(0x0B71);fn('glEnable',None,U)(0x0BE2)
vertex='#version 430 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}'
# The viewer's resolve shader as compiled by the viewer (version line + extension, no includes).
resolve=(root/'indra/newview/app_settings/shaders/class1/deferred/alphaOITResolveF.glsl').read_text()
resolve='#version 430 core\n'+resolve[resolve.index('#extension'):].replace('/*[EXTRA_CODE_HERE]*/','')
resolve_prog=program([(0x8B31,vertex),(0x8B30,resolve)])
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
layout(binding=1,rgba32ui) uniform writeonly uimage2DArray oit_slots;
uniform int oit_slot_count;
uniform sampler2D alpha_oit_opaque_depth;
uniform float test_depth;
uniform float test_alpha;
#define OIT_TAIL_COLOR_SCALE 4096.0
#define OIT_TAIL_ALPHA_SCALE 65536.0
#define OIT_TAIL_LOG_SCALE   65536.0
out vec4 color;
'''
    prog=program([(0x8B31,vertex),(0x8B30,declarations+helper+'\nvoid main(){oit_append(vec4(1.,0.,0.,test_alpha),test_depth);color=vec4(0.,1.,0.,1.);}')])
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
            image(0,0,0,1,0,0x88BA,R32UI);image(1,0,0,1,0,0x88B9,RGBA32UI)
            barrier(0xFFFFFFFF);zero=U(0)
            cleartex(arrays[0],0,RED_INT,UINT,C.byref(zero))   # counts + tail reset in one clear; slots never cleared
            barrier(0xFFFFFFFF)
            image(0,arrays[0],0,1,0,0x88BA,R32UI);image(1,arrays[1],0,1,0,0x88B9,RGBA32UI)
            use(prog);uniform(loc(prog,b'oit_slot_count'),slots);uniform(loc(prog,b'alpha_oit_opaque_depth'),0)
            # Exactly equal to the stored middle depth, testing LEQUAL precisely.
            fn('glUniform1f',None,I,C.c_float)(loc(prog,b'test_depth'),snapshot[1])
            alpha=loc(prog,b'test_alpha');fn('glUniform1f',None,I,C.c_float)(alpha,0.5)
            draw(4,0,3);draw(4,0,3)
            # A third, fully transparent fragment must take no slot and leave the tail alone.
            fn('glUniform1f',None,I,C.c_float)(alpha,0.0);draw(4,0,3);barrier(0xFFFFFFFF)
            meta=(U*(META_LAYERS*W))();bindtex(TEX2DARRAY,arrays[0]);gettex(TEX2DARRAY,0,RED_INT,UINT,meta)
            slot=(U*(SLOT_LAYERS*W*4))();bindtex(TEX2DARRAY,arrays[1]);gettex(TEX2DARRAY,0,RGBA_INT,UINT,slot)
            counts=list(meta[0:W]);tail=[list(meta[l*W:(l+1)*W]) for l in range(1,META_LAYERS)]
            for x in range(W):
                if x==hidden:
                    assert counts[x]==0,('hidden fragment took a slot',rel,cycle,slots,counts)
                    assert all(t[x]==0 for t in tail),('hidden fragment reached the tail',rel,cycle,slots,tail)
                    continue
                assert counts[x]==2,(rel,cycle,slots,counts)
                stored=min(2,slots)
                for layer in range(stored):
                    texel=slot[(layer*W+x)*4:(layer*W+x)*4+4]
                    assert texel[2]==struct.unpack('<I',struct.pack('<f',snapshot[1]))[0],('slot depth',rel,cycle,slots,layer,list(texel))
                    assert texel[0]==0x3C00 and texel[1]==0x38000000,('slot colour',rel,list(texel))   # half(1,0) / half(0,0.5)
                near=(~struct.unpack('<I',struct.pack('<f',snapshot[1]))[0])&0xFFFFFFFF
                expected=(0,0,0,0,0,0) if slots>=2 else (TAIL_R,0,0,TAIL_A,TAIL_LOG,near)
                assert tuple(t[x] for t in tail)==expected,('tail',rel,cycle,slots,[t[x] for t in tail])
            # Resolve with the viewer's binding layout (pipeline.cpp compositeAlphaOIT): meta read on
            # unit 0, slots read on unit 1, meta write-only on unit 2 for the tail reset. Two red
            # fragments at alpha 0.5 over black give 0.75 red whether both sat in slots or one in
            # the tail; the tail layers must be zero afterwards, the counts untouched.
            clearcolor(0.,0.,0.,1.);clear(0x4000)
            image(0,arrays[0],0,1,0,0x88B8,R32UI);image(1,arrays[1],0,1,0,0x88B8,RGBA32UI);image(2,arrays[0],0,1,0,0x88B9,R32UI)
            use(resolve_prog);uniform(loc(resolve_prog,b'oit_slot_count'),slots);uniform(loc(resolve_prog,b'depthMap'),0)
            blendfunc(1,0x0303);colormask(1,1,1,0)
            draw(4,0,3);barrier(0xFFFFFFFF);colormask(1,1,1,1)
            image(0,0,0,1,0,0x88B8,R32UI);image(1,0,0,1,0,0x88B8,RGBA32UI);image(2,0,0,1,0,0x88B9,R32UI)
            bindtex(TEX2DARRAY,arrays[0]);gettex(TEX2DARRAY,0,RED_INT,UINT,meta)
            assert list(meta[0:W])==counts,('resolve changed the counts',rel,cycle,slots,list(meta[0:W]))
            assert all(v==0 for v in meta[W:]),('resolve left a tail',rel,cycle,slots,list(meta[W:]))
            pixels=(C.c_ubyte*(W*4))();bindtex(TEX2D,textures[2]);gettex(TEX2D,0,0x1908,0x1401,pixels)
            for x in range(W):
                want=(0,0,0) if x==hidden else (191,0,0)
                assert all(abs(pixels[x*4+c]-want[c])<=1 for c in range(3)),('resolve colour',rel,cycle,slots,x,list(pixels[x*4:x*4+4]))
            bindtex(TEX2D,textures[1]);assert geterror()==0;checks+=1
    fn('glDeleteProgram',None,U)(prog)
fn('glDeleteProgram',None,U)(resolve_prog)
print(f'{checks} alpha OIT cases passed: four append shaders, detached depth, equality, occlusion, slot fill, tail overflow, zero alpha ignored, reset, resolve colour and tail reset.')
gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
