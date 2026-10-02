"""Offscreen PPLL regression, no viewer launch; --opengl PATH selects Mesa.

Reuse the existing Windows GL bootstrap and shader compiler, stopping before
its mesh tests. Exercise the actual append function from each shader family.
"""
from pathlib import Path
bootstrap = Path(__file__).with_name('test_gl_compute_mesh.py').read_text().split('shader_path=')[0]
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
genbuf=fn('glGenBuffers',None,I,C.POINTER(U));bindbuf=fn('glBindBuffer',None,U,U)
storage=fn('glBufferStorage',None,U,S,P,U);data=fn('glBufferData',None,U,S,P,U)
base=fn('glBindBufferBase',None,U,U,U);read=fn('glGetBufferSubData',None,U,S,S,P)
sub=fn('glBufferSubData',None,U,S,S,P);barrier=fn('glMemoryBarrier',None,U)
gentex=fn('glGenTextures',None,I,C.POINTER(U));bindtex=fn('glBindTexture',None,U,U)
texstorage=fn('glTexStorage2D',None,U,I,U,I,I);texparam=fn('glTexParameteri',None,U,U,I)
texsub=fn('glTexSubImage2D',None,U,I,I,I,I,I,U,U,P)
copy=fn('glCopyImageSubData',None,U,U,I,I,I,I,U,U,I,I,I,I,I,I,I)
image=fn('glBindImageTexture',None,U,U,I,C.c_ubyte,I,U,U)
cleartex=fn('glClearTexImage',None,U,I,U,U,P)
genfbo=fn('glGenFramebuffers',None,I,C.POINTER(U));bindfbo=fn('glBindFramebuffer',None,U,U)
attachtex=fn('glFramebufferTexture2D',None,U,U,U,U,I)
geterror=fn('glGetError',U);uniform=fn('glUniform1i',None,I,I);loc=fn('glGetUniformLocation',I,U,C.c_char_p)
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
textures=(U*4)();gentex(4,textures)
for name,fmt in zip(textures,(0x81A6,0x81A6,0x8236,0x8058)):
    bindtex(0x0DE1,name);texstorage(0x0DE1,1,fmt,3,1)
    texparam(0x0DE1,0x2801,0x2600);texparam(0x0DE1,0x2800,0x2600)
fbo=U();genfbo(1,C.byref(fbo));bindfbo(0x8D40,fbo)
attachtex(0x8D40,0x8CE0,0x0DE1,textures[3],0);attachtex(0x8D40,0x8D00,0x0DE1,textures[0],0)
assert fn('glCheckFramebufferStatus',U,U)(0x8D40)==0x8CD5
fn('glViewport',None,I,I,I,I)(0,0,3,1);fn('glDisable',None,U)(0x0B71)
buffers=(U*3)();genbuf(3,buffers)
bindbuf(0x90D2,buffers[0]);storage(0x90D2,8*16,None,0);base(0x90D2,0,buffers[0])
immutable=I();fn('glGetBufferParameteriv',None,U,U,C.POINTER(I))(0x90D2,0x821F,C.byref(immutable));assert immutable.value==1
bindbuf(0x92C0,buffers[1]);data(0x92C0,4,None,0x88E8);base(0x92C0,0,buffers[1])
bindbuf(0x8F37,buffers[2]);data(0x8F37,4,None,0x88E1)
vertex='#version 430 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}'
files=['class1/deferred/fullbrightF.glsl','class2/deferred/alphaF.glsl','class2/deferred/pbralphaF.glsl','class3/deferred/materialF.glsl']
checks=0
for rel in files:
    source=(root/'indra/newview/app_settings/shaders'/rel).read_text()
    start=source.index('bool oit_append(');end=source.index('\n}',start)+2
    helper=source[start:end]
    declarations='''#version 430 core
layout(binding=0,r32ui) uniform coherent uimage2D oit_head;
layout(std430,binding=0) buffer OITNodePool {uint oit_nodes[];};
layout(binding=0,offset=0) uniform atomic_uint oit_counter;
uniform int oit_node_cap;
uniform sampler2D alpha_peel_depth;
uniform float test_depth;
out vec4 color;
'''
    prog=program([(0x8B31,vertex),(0x8B30,declarations+helper+'\nvoid main(){if(oit_append(vec4(1.,0.,0.,0.5),test_depth))discard;color=vec4(0.,1.,0.,1.);}')])
    for cycle in range(6):
        depth=(C.c_float*3)(0.25,0.5,0.75) if cycle%2==0 else (C.c_float*3)(0.75,0.5,0.25)
        bindtex(0x0DE1,textures[0]);texsub(0x0DE1,0,0,0,3,1,0x1902,0x1406,depth)
        copy(textures[0],0x0DE1,0,0,0,0,textures[1],0x0DE1,0,0,0,0,3,1,1)
        # The live draw attachment changes; the detached snapshot must not.
        changed=(C.c_float*3)(0.,0.,0.);texsub(0x0DE1,0,0,0,3,1,0x1902,0x1406,changed)
        bindtex(0x0DE1,textures[1]);snapshot=(C.c_float*3)()
        fn('glGetTexImage',None,U,I,U,U,P)(0x0DE1,0,0x1902,0x1406,snapshot)
        assert all(abs(a-b)<1e-6 for a,b in zip(snapshot,depth))
        for cap in (8,1):
            barrier(0xFFFFFFFF);zero=U(0);empty=U(0xffffffff)
            bindbuf(0x92C0,buffers[1]);sub(0x92C0,0,4,C.byref(zero));cleartex(textures[2],0,0x8D94,0x1405,C.byref(empty))
            base(0x92C0,0,buffers[1]);base(0x90D2,0,buffers[0])
            image(0,textures[2],0,0,0,0x88BA,0x8236)
            use(prog);uniform(loc(prog,b'oit_node_cap'),cap);uniform(loc(prog,b'alpha_peel_depth'),0)
            # Exactly equal to the stored middle depth, testing LEQUAL precisely.
            fn('glUniform1f',None,I,C.c_float)(loc(prog,b'test_depth'),snapshot[1])
            fn('glDrawArrays',None,U,I,I)(4,0,3);barrier(0xFFFFFFFF)
            # Read a GPU snapshot, never map/read the live allocator.
            bindbuf(0x8F36,buffers[1]);bindbuf(0x8F37,buffers[2])
            fn('glCopyBufferSubData',None,U,U,S,S,S)(0x8F36,0x8F37,0,0,4)
            count=U();read(0x8F37,0,4,C.byref(count))
            bindtex(0x0DE1,textures[2]);heads=(U*3)();fn('glGetTexImage',None,U,I,U,U,P)(0x0DE1,0,0x8D94,0x1405,heads)
            assert count.value==2,('heads',list(heads))
            assert heads[0 if cycle%2==0 else 2]==0xffffffff,'hidden fragment allocated a node'
            assert sum(h!=0xffffffff for h in heads)==min(cap,2)
            bindtex(0x0DE1,textures[1]);assert geterror()==0;checks+=1
    fn('glDeleteProgram',None,U)(prog)
print(f'{checks} PPLL cases passed: four append shaders, detached depth, equality, occlusion, overflow, reset; immutable pool verified.')
gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)

