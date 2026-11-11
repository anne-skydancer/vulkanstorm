# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The stochastic transparency compute resolve (alphaStochasticResolveC.glsl) on image sizes that are not
multiples of the 16 x 16 workgroup, against a CPU model (the 3 x 3 clamp of this frame's values, edge
pixels clamped, history valid, identity reprojection, weight 1/8): 70x37, 17x1, 1x33, 300x200. No viewer
launch; needs a GL 4.3 context. Exit status 0 when every size matches.
"""
import os, subprocess, sys
SIZES = ('70x37', '17x1', '1x33', '300x200')
if 'SZ' not in os.environ:   # parent: one child (one GL context) per size
    for size in SIZES:
        subprocess.run([sys.executable, __file__] + sys.argv[1:], env=dict(os.environ, SZ=size), check=True)
    print(f'{len(SIZES)} sizes match the CPU model')
    sys.exit(0)
from pathlib import Path
import numpy as np
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
root = Path(__file__).resolve().parents[2]
shaders = root / 'indra/newview/app_settings/shaders/class1/deferred'
gentex=fn('glGenTextures',None,I,C.POINTER(U));bindtex=fn('glBindTexture',None,U,U)
texparam=fn('glTexParameteri',None,U,U,I);teximage=fn('glTexImage2D',None,U,I,I,I,I,I,U,U,P)
teximage_ms=fn('glTexImage2DMultisample',None,U,I,U,I,I,C.c_ubyte)
genfbo=fn('glGenFramebuffers',None,I,C.POINTER(U));bindfbo=fn('glBindFramebuffer',None,U,U)
attachtex=fn('glFramebufferTexture2D',None,U,U,U,U,I);status=fn('glCheckFramebufferStatus',U,U)
loc=fn('glGetUniformLocation',I,U,C.c_char_p);uniform1i=fn('glUniform1i',None,I,I);uniform1f=fn('glUniform1f',None,I,C.c_float)
draw=fn('glDrawArrays',None,U,I,I);activetex=fn('glActiveTexture',None,U);viewport=fn('glViewport',None,I,I,I,I)
readpixels=fn('glReadPixels',None,I,I,I,I,U,U,P);readbuffer=fn('glReadBuffer',None,U);drawbuffer=fn('glDrawBuffer',None,U)
clear=fn('glClear',None,U);cleardepth=fn('glClearDepth',None,C.c_double);disable=fn('glDisable',None,U);enable=fn('glEnable',None,U)
geterror=fn('glGetError',U)
bindimage=fn('glBindImageTexture',None,U,U,I,C.c_ubyte,I,U,U);dispatch=fn('glDispatchCompute',None,U,U,U);membarrier=fn('glMemoryBarrier',None,U)
TEX2D=0x0DE1;TEXMS=0x9100;FB=0x8D40;COLOR0=0x8CE0;DEPTH_ATT=0x8D00;RGBA=0x1908;RGBA16F=0x881A;FLOAT=0x1406;D24=0x81A6;NEAREST=0x2600
W,H=[int(v) for v in os.environ['SZ'].split('x')]; N=8
vao=U();fn('glGenVertexArrays',None,I,C.POINTER(U))(1,C.byref(vao));fn('glBindVertexArray',None,U)(vao)
viewport(0,0,W,H);disable(0x0B44)
def viewer(name,version=430):
    return ('#version %d\n#define COMPUTE_SHADER 1\n'%version)+(shaders/name).read_text().replace('/*[EXTRA_CODE_HERE]*/','')
resolve=program([(0x91B9,viewer('alphaStochasticResolveC.glsl')),(0x91B9,viewer('stochasticResolveUtilF.glsl'))])
sv='''#version 420
void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); gl_Position=vec4(p*2.0-1.0,0.5,1.0); }'''
fill=program([(0x8B31,sv),(0x8B30,'''#version 420
uniform sampler2D src; out vec4 c; void main(){ c=texelFetch(src,ivec2(gl_FragCoord.xy),0); }''')])
def tex2d(data=None):
    t=U();gentex(1,C.byref(t));bindtex(TEX2D,t);teximage(TEX2D,0,RGBA16F,W,H,0,RGBA,FLOAT,data)
    texparam(TEX2D,0x2801,NEAREST);texparam(TEX2D,0x2800,NEAREST);texparam(TEX2D,0x813D,0);return t
def texms(fmt):
    t=U();gentex(1,C.byref(t));bindtex(TEXMS,t);teximage_ms(TEXMS,N,fmt,W,H,1);return t
def fbo(*att):
    f=U();genfbo(1,C.byref(f));bindfbo(FB,f)
    for p,tg,t in att: attachtex(FB,p,tg,t,0)
    assert status(FB)==0x8CD5,hex(status(FB));return f
rng=np.random.default_rng(1)
cur=(rng.random((H,W,4),dtype=np.float32)*np.array([1,1,1,0.9],np.float32)+np.array([0,0,0,0.05],np.float32)).astype(np.float16).astype(np.float32)
hist=rng.random((H,W,4),dtype=np.float32).astype(np.float16).astype(np.float32)
srct=tex2d(cur.ctypes.data_as(P)); histt=tex2d(hist.ctypes.data_as(P)); outt=tex2d(None)
accum=texms(RGBA16F);depth=texms(D24)
f_ms=fbo((COLOR0,TEXMS,accum),(DEPTH_ATT,TEXMS,depth))
bindfbo(FB,f_ms);drawbuffer(COLOR0);fn('glDepthMask',None,C.c_ubyte)(1);cleardepth(0.5);clear(0x100);disable(0x0B71)
use(fill);activetex(0x84C0);bindtex(TEX2D,srct);uniform1i(loc(fill,b'src'),0);draw(4,0,3)
assert geterror()==0
f_out=fbo((COLOR0,TEX2D,outt))
use(resolve)
for u,(tg,t) in enumerate([(TEXMS,accum),(TEXMS,depth),(TEX2D,histt)]):
    activetex(0x84C0+u);bindtex(tg,t)
# sampler units as the viewer assigns them: by the uniform's own value
for n,u in ((b'alpha_st_accum',0),(b'alpha_st_depth',1),(b'alpha_st_history',2)): assert loc(resolve,n)>=0,n; uniform1i(loc(resolve,n),u)
uniform1i(loc(resolve,b'st_samples_hw'),N);uniform1i(loc(resolve,b'st_samples'),N)
ident=(C.c_float*16)(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
fn('glUniformMatrix4fv',None,I,I,C.c_ubyte,C.POINTER(C.c_float))(loc(resolve,b'stoch_reproj'),1,0,ident)
uniform1f(loc(resolve,b'stoch_weight'),1/8);uniform1i(loc(resolve,b'stoch_history_valid'),1)
bindimage(0,outt,0,0,0,0x88B9,RGBA16F)
dispatch((W+15)//16,(H+15)//16,1)
membarrier(0x8|0x20);bindimage(0,0,0,0,0,0x88B9,RGBA16F)
assert geterror()==0,hex(geterror())
bindfbo(FB,f_out);readbuffer(COLOR0);buf=(C.c_float*(W*H*4))();readpixels(0,0,W,H,RGBA,FLOAT,buf)
got=np.frombuffer(buf,np.float32).reshape(H,W,4)
# CPU model
pad=np.pad(cur,((1,1),(1,1),(0,0)),mode='edge')
mn=np.min([pad[1+dy:1+dy+H,1+dx:1+dx+W] for dy in (-1,0,1) for dx in (-1,0,1)],axis=0)
mx=np.max([pad[1+dy:1+dy+H,1+dx:1+dx+W] for dy in (-1,0,1) for dx in (-1,0,1)],axis=0)
exp=np.clip(hist,mn,mx)*(1-1/8)+cur*(1/8)
err=np.abs(got-exp)
print('size %dx%d: max abs error %.2e, mean %.2e'%(W,H,err.max(),err.mean()))
# the output is RGBA16F (half precision, relative 2^-11), the model float32: 2e-3 absolute for values below 2
assert err.max()<2e-3,('the compute resolve differs from the CPU model',W,H,np.argwhere(err.max(axis=2)>=2e-3)[:8].tolist())
