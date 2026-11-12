# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline 2-D model of the S1 caster coverage through the 8-ray march, the 3 x 3 clamp and the EMA 1/8.

A study aid for the design of stage AA-S1 (doc/shadows/shadow-method.md), not a CI gate: nothing runs it
automatically and it asserts nothing. Receiver pixels read 8 rays that land on `pix/texel`-sized caster texels
(jit: the rays' lateral spread in texels); the caster keeps a texel when alpha > fract(IGN + 0.618 frame).
Rows (RMS error of the converged value, 3 x 3 clamp at stride 1 and 3):
  a=0.5  pix/texel=2 jit=1.0: 0.051 / 0.048     pix/texel=4 jit=0.5: 0.120 / 0.052
  a=0.5  pix/texel=8 jit=0.5: 0.158 / 0.084     pix/texel=8 jit=1.0: 0.059 / 0.052
  a=0.85 pix/texel=4 jit=0.5: 0.135 / 0.047     pix/texel=8 jit=0.5: 0.153 / 0.113 (bias 0.059 / 0.069)
Rays spread over a texel or more behave as the 1-D model (about 0.05); at extreme contact the stride-1 clamp
raises the error to 0.12 to 0.16 but does not collapse the value to binary (about 0.5). Stride 3 recovers
0.04 to 0.11 (bias up to 0.07 for a=0.85).
"""
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


def main():
    for a in (0.3, 0.5, 0.85):
        for k in (1, 2, 4, 8):
            for jit in (0.5, 1.0):
                for cs in (1, 3):
                    r = run(a, k, jit, cs)
                    print(f"a={a} pix/texel={k} jit={jit} clampstride={cs}: rms={r[0]:.3f} tstd={r[1]:.3f} bias={r[2]:.3f} frame2frame={r[3]:.3f}")


if __name__ == '__main__':
    main()
