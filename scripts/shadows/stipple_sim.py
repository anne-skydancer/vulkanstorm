# SPDX-License-Identifier: GPL-3.0-or-later
# Offline model of H1 (2026-11-13): release's shadow alpha-mask column stipple vs the Stochastic dither, sampled
# one texel per ray. Result: stipple column profile peak-to-peak 1.000, dither 0.056 (averaged over 40 rows; a
# single row still has texel-scale grain, std 0.239 vs 0.285). A model of the viewer, not an in-world render.
import numpy as np
W=64
# shadow map: semi-transparent caster (alpha 0.5) covering whole map at depth 0.3; background depth 1.0
# vertex-shader target_pos_x = 0.5*(W-1)*ndc.x ; fragment at texel center i has ndc.x=(i+0.5)/W*2-1
def stipple_map(mode):
    d=np.ones((W,W))
    ii=np.arange(W)
    ndc=(ii+0.5)/W*2-1
    tpx=0.5*(W-1)*ndc
    if mode=='stipple':
        keep=~(np.mod(0.5*np.floor(tpx),1.0)<0.25)   # fract(0.5*floor(x))<0.25 -> discard
        keep=np.broadcast_to(keep[None,:],(W,W))
    else: # hashed per-texel dither, alpha 0.5 -> ~50% coverage
        y,x=np.mgrid[0:W,0:W]
        h=np.mod(52.9829189*np.mod(0.06711056*x+0.00583715*y,1.0),1.0)
        keep=h<0.5
    d[keep]=0.3
    return d
rng=np.random.default_rng(1)
def vis(d,u,v,frames=16):
    # receiver plane depth 0.6; 8 rays/frame, per-ray jitter in [-.5,.5) texel, one texelFetch each (end read)
    tot=0;n=0
    for f in range(frames):
        for k in range(8):
            j=rng.random(2)-0.5
            p=np.clip(np.floor(np.array([u,v])+j).astype(int),0,W-1)
            zs=d[p[1],p[0]]
            tot+= 0.0 if zs<0.6-0.01 else 1.0; n+=1
    return tot/n
for mode in ('stipple','dither'):
    d=stipple_map(mode)
    # receiver pixels step 0.37 texel across x, fixed row
    us=np.arange(20,40,0.37)
    vv=np.array([vis(d,u,30.2,64) for u in us])
    # fine averaging over rows too
    print(mode,'mean vis %.3f'%vv.mean(),'std along x %.3f'%vv.std(),'min %.2f max %.2f'%(vv.min(),vv.max()))
    print(' '.join('%.2f'%x for x in vv[:30]))
print('--- column profile: mean vis over 40 rows, receiver pixels each 0.25 texel across x (period 2 texels)')
for mode in ('stipple','dither'):
    d=stipple_map(mode)
    us=np.arange(20,24,0.25)
    prof=[np.mean([vis(d,u,v+0.3,16) for v in range(10,50)]) for u in us]
    print(mode,' '.join('%.2f'%x for x in prof),' peak-to-peak %.3f'%(max(prof)-min(prof)))
