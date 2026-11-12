# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline 2-D model of stMarch and its `behind` rule over a light-space depth map (stage AA-S1).

Not a CI gate: nothing runs it automatically. Scenes: a lace canopy (alpha 0.5, true visibility 0.50) over flat
or sloped ground, and an opaque caster over a roof over the ground (true visibility 0 near the caster's
outline). Modes: off (no `behind` rule), on (the rule before AA-S1), guard (the rule needs the surface to be
more than 10 % of the way from the hiding surface to the receiver, as shadowStochasticUtil.glsl now does).
Rows (lace flat / lace slope / roof near-outline), light disc half-width h as a fraction of the depth:
  off    h=0.2: 0.457 / 0.446 / 0.013     on     h=0.2: 0.202 / 0.198 / 0.000
  guard  h=0.2: 0.461 / 0.443 / 0.000     guard  h=0.1: 0.486 / 0.472 / 0.000
"""
import numpy as np, math
rng=np.random.default_rng(5)
def ign(x,y): return np.modf(52.9829189*np.modf(0.06711056*x+0.00583715*y)[0])[0]
N=640; ST=8; STEPS=8; BIAS=0.3; RPMAX=2.0
MODE = 'on'; GUARD = 0.1
def march(depth,cx,cz,g,h):
    P=len(cx); vis=np.zeros(P)
    for k in range(ST):
        j=np.modf(rng.random(P)+0.618034*k)[0]
        jit=np.modf(j*7.31+0.137*k)[0]-0.5
        r=math.sqrt((k+0.5)/ST); lat=h*r*math.cos(k*2.39996323)
        blocked=np.zeros(P,bool); behind=np.zeros(P,bool); zh=np.full(P,1e9)
        qx=cx.copy(); qz=cz.copy()
        def ref(x):
            px=np.clip(np.floor(x+jit),0,N-1).astype(int)
            return depth[px], cz-BIAS+np.clip(g*((px+0.5)-cx),-RPMAX,0.0)
        def okb(zs):
            if MODE=='off': return np.zeros(P,bool)
            if MODE=='on': return behind
            return behind&(zs>zh+GUARD*(cz-zh))
        for i in range(STEPS):
            un=((i+1+j)/(STEPS+1)) if i+1<STEPS else np.ones(P)
            t=cz*un*un; qnx=cx+lat*t; qnz=cz-t
            zs,rf=ref(qx); slack=0.25*(qz-qnz)+1e-4
            blocked|=(~blocked)&(zs<rf)&(zs<qz)&(zs>qnz-slack)
            blocked|=(~blocked)&okb(zs)&(zs<rf)&(zs>qz)
            nb=zs<qnz-slack; zh=np.where(nb,np.minimum(zh,zs),zh); behind=behind|nb
            qx,qz=qnx,qnz
        zs,rf=ref(qx)
        blocked|=((zs<qz)|(okb(zs)&(zs>qz)))&(zs<rf)
        vis+=~blocked
    return vis/ST
def lace(a,h,slope,F=140):
    x=np.arange(N,dtype=float); gz=30+slope*x
    cx=np.arange(0,N,2)+0.5; cz=gz[cx.astype(int)]; acc=0;n=0
    for f in range(F):
        thr=np.modf(ign(x,0.0)+0.618034*f)[0]
        can=(x>=100)&(x<300)&(a>thr)
        d=np.where(can,np.minimum(gz,10.0),gz); v=march(d,cx,cz,slope,h)
        if f>=20: acc=acc+v;n+=1
    v=acc/n; return v[(cx>130)&(cx<270)].mean()
def roof(h,F=100):
    x=np.arange(N,dtype=float); gz=np.full(N,30.0)
    cx=np.arange(150,260,1)+0.5; cz=np.full(len(cx),30.0); acc=0;n=0
    d=gz.copy(); d[100:300]=20.0; d[150:320]=10.0       # caster 10 over roof 20 over ground 30; map holds the caster
    for f in range(F):
        v=march(d,cx,cz,0.0,h)
        if f>=10: acc=acc+v;n+=1
    return (acc/n)[(cx<175)].mean(), (acc/n).max()


def main():
    global MODE
    for MODE in ('off', 'on', 'guard'):
        print(MODE)
        for h in (0.05, 0.1, 0.2):
            print(f"  h={h}: lace a=0.5 flat vis={lace(0.5,h,0.0):.3f} slope vis={lace(0.5,h,0.05):.3f} (true 0.50); roof-under-caster (true 0) near-outline vis={roof(h)[0]:.3f} max={roof(h)[1]:.3f}")


if __name__ == '__main__':
    main()
