# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline model of stacked Blending layers through the caster threshold of stage AA-S1.

Not a CI gate. threshold = fract(IGN(pixel) + hash(layer, 4 x 4 block) + golden phase) (shadowCasterThreshold());
prints the kept fraction against 1 - prod(1 - alpha_i), over 12 sets of layer ids. Rows:
  [0.5, 0.5]   expected 0.750  measured 0.751 (0.747 to 0.758)     one threshold for both layers: 0.500
  [0.3, 0.3, 0.3] expected 0.657  measured 0.660     [0.85, 0.5] expected 0.925  measured 0.926
"""
import numpy as np


def fr(x): return x-np.floor(x)
def ign(x,y): return fr(52.9829189*fr(0.06711056*x+0.00583715*y))
def hoskins(bx,by,layer):   # Dave Hoskins hash12 on (block + layer), no sin
    p3=fr(np.stack([bx+layer,by+layer,bx+layer])*0.1031)
    p3=p3+(p3*(p3[[1,2,0]]+33.33)).sum(0)
    return fr((p3[0]+p3[1])*p3[2])
X,Y=np.meshgrid(np.arange(160.0),np.arange(160.0))
def thr(layer,ph): return fr(ign(X,Y)+hoskins(np.floor(X/4),np.floor(Y/4),layer)+ph)
def blocked(alphas,layers,F=32):
    t=0
    for f in range(F):
        ph=fr(0.6180339887*f); k=np.zeros(X.shape,bool)
        for a,l in zip(alphas,layers): k|=a>thr(l,ph)
        t+=k.mean()
    return t/F


def main():
    for al in ([0.5, 0.5], [0.3, 0.3, 0.3], [0.5, 0.5, 0.5], [0.85, 0.5], [0.2] * 4, [0.5]):
        v = [blocked(al, [float(s * 7 + i * 131) for i in range(len(al))]) for s in range(12)]
        print(al, "expected %.3f  measured mean %.3f (min %.3f max %.3f over 12 layer-id sets)" % (1 - np.prod([1 - a for a in al]), np.mean(v), min(v), max(v)))
    print("same layer id (two faces of one batch):", blocked([0.5, 0.5], [5.0, 5.0]))


if __name__ == '__main__':
    main()
