# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline 2-D model of the S1 caster coverage through the 8-ray march, the 3x3 clamp and the EMA 1/8 (the gate of #486)."""
import numpy as np
rng=np.random.default_rng(2)
def ign(x,y): return np.modf(52.9829189*np.modf(0.06711056*x+0.00583715*y)[0])[0]
def run(a,k,jit,clampstride=1,ema=1/8,F=400,N=48,rays=8):
    # N x N receiver pixels, k pixels per caster texel; each ray lands at pixel pos + jitter(texels) -> texel
    py,px=np.mgrid[0:N,0:N].astype(float)
    h=np.full((N,N),1-a); vals=[]; flips=[]
    prev=None
    for f in range(1,F):
        tot=np.zeros((N,N))
        for r in range(rays):
            ox=rng.uniform(-jit,jit,(N,N)); oy=rng.uniform(-jit,jit,(N,N))
            tx=np.floor(px/k+ox); ty=np.floor(py/k+oy)
            thr=np.modf(ign(tx,ty)+0.6180339887*f)[0]
            tot+=(a>thr)
        cur=1-tot/rays
        s=clampstride
        pad=np.pad(cur,s,mode='edge'); st=[pad[s+dy*s:s+dy*s+N,s+dx*s:s+dx*s+N] for dy in(-1,0,1) for dx in(-1,0,1)]
        mn=np.min(st,0); mx=np.max(st,0)
        h=np.clip(h,mn,mx); h=h+(cur-h)*ema
        if f>150: vals.append(h.copy())
    v=np.array(vals); err=v-(1-a)
    # temporal flicker = std over time per pixel; bias = mean error
    return np.sqrt((err**2).mean()), v.std(0).mean(), abs(err.mean()), (np.abs(np.diff(v,axis=0))).mean()
for a in (0.3,0.5,0.85):
  for k in (1,2,4,8):
    for jit in (0.5,1.0):
      for cs in (1,3):
        r=run(a,k,jit,cs)
        print(f"a={a} pix/texel={k} jit={jit} clampstride={cs}: rms={r[0]:.3f} tstd={r[1]:.3f} bias={r[2]:.3f} frame2frame={r[3]:.3f}")
