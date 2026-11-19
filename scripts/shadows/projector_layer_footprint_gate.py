#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""C3 gate of the projector pool's avatar layer: the footprint copy equals the whole-tile copy outside the footprint.

Offline, no viewer, Windows with an OpenGL 4.3 driver (glCopyImageSubData, the call LLProjectorShadows::copyTileTexels issues).
An atlas S holds random D32F depth; an atlas L starts as S. Over many passes a rectangle (the "avatar's footprint") moves, grows,
vanishes or becomes the whole tile. Each pass:

  * the incremental path copies S -> L over the union of the old and the new rectangle (llprojectorpool::layerCopyRect), then
    draws inside the new rectangle: a constant depth written through the scissor (a stand-in for the avatar's raster; the scissor
    is the one the pool uses);
  * the reference L copies the WHOLE tile S -> L, then makes the same scissored draw.

Pass: after every pass the tile of L is bit-identical to the reference, so in particular outside the new rectangle L equals S.

    python -I scripts/shadows/projector_layer_footprint_gate.py [--passes 300] [--tile 256] [--seed 1]
"""
import argparse
import array
import ctypes as C
import random
import sys
from ctypes import wintypes as W


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--passes', type=int, default=300)
    ap.add_argument('--tile', type=int, default=256)
    ap.add_argument('--seed', type=int, default=1)
    args = ap.parse_args()
    if sys.platform != 'win32':
        print('SKIP: Windows only')
        return 0

    user = C.WinDLL('user32')
    gdi = C.WinDLL('gdi32')
    gl = C.WinDLL('opengl32')

    class PFD(C.Structure):
        _fields_ = [('size', W.WORD), ('version', W.WORD), ('flags', W.DWORD), ('pixel', C.c_ubyte), ('color', C.c_ubyte),
                    ('rest', C.c_ubyte * 18), ('layerMask', W.DWORD), ('visibleMask', W.DWORD), ('damageMask', W.DWORD)]

    user.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_int, C.c_int, C.c_int, C.c_int, W.HWND, W.HMENU,
                                     W.HINSTANCE, C.c_void_p]
    user.CreateWindowExW.restype = W.HWND
    user.GetDC.argtypes = [W.HWND]
    user.GetDC.restype = W.HDC
    pfd = PFD()
    pfd.size = C.sizeof(PFD)
    pfd.version = 1
    pfd.flags = 0x24
    pfd.color = 24
    win = user.CreateWindowExW(0, 'STATIC', 'gate', 0, 0, 0, 32, 32, None, None, None, None)
    hdc = user.GetDC(win)
    gdi.ChoosePixelFormat.argtypes = [W.HDC, C.POINTER(PFD)]
    gdi.SetPixelFormat.argtypes = [W.HDC, C.c_int, C.POINTER(PFD)]
    gdi.SetPixelFormat(hdc, gdi.ChoosePixelFormat(hdc, C.byref(pfd)), C.byref(pfd))
    gl.wglCreateContext.argtypes = [W.HDC]
    gl.wglCreateContext.restype = C.c_void_p
    gl.wglMakeCurrent.argtypes = [W.HDC, C.c_void_p]
    gl.wglGetProcAddress.argtypes = [C.c_char_p]
    gl.wglGetProcAddress.restype = C.c_void_p
    ctx = gl.wglCreateContext(hdc)
    gl.wglMakeCurrent(hdc, ctx)

    def fn(name, res, *t):
        a = gl.wglGetProcAddress(name.encode())
        if a:
            return C.WINFUNCTYPE(res, *t)(a)
        f = getattr(gl, name)
        f.restype = res
        f.argtypes = list(t)
        return f

    U, I = C.c_uint, C.c_int
    glGenTextures = fn('glGenTextures', None, I, C.POINTER(U))
    glBindTexture = fn('glBindTexture', None, U, U)
    glTexStorage2D = fn('glTexStorage2D', None, U, I, U, I, I)
    glTexSubImage2D = fn('glTexSubImage2D', None, U, I, I, I, I, I, U, U, C.c_void_p)
    glGetTexImage = fn('glGetTexImage', None, U, I, U, U, C.c_void_p)
    glCopyImageSubData = fn('glCopyImageSubData', None, U, U, I, I, I, I, U, U, I, I, I, I, I, I, I)
    glGenFramebuffers = fn('glGenFramebuffers', None, I, C.POINTER(U))
    glBindFramebuffer = fn('glBindFramebuffer', None, U, U)
    glFramebufferTexture2D = fn('glFramebufferTexture2D', None, U, U, U, U, I)
    glDrawBuffer = fn('glDrawBuffer', None, U)
    glReadBuffer = fn('glReadBuffer', None, U)
    glEnable = fn('glEnable', None, U)
    glDisable = fn('glDisable', None, U)
    glScissor = fn('glScissor', None, I, I, I, I)
    glViewport = fn('glViewport', None, I, I, I, I)
    glClear = fn('glClear', None, U)
    glClearDepth = fn('glClearDepth', None, C.c_double)
    glDepthMask = fn('glDepthMask', None, U)
    glDepthFunc = fn('glDepthFunc', None, U)
    glFinish = fn('glFinish', None)
    glGetString = fn('glGetString', C.c_char_p, U)
    GL_TEXTURE_2D, GL_D32F, GL_FB, GL_DEPTH_ATT, GL_SCISSOR, GL_DEPTH_BIT = 0x0DE1, 0x8CAC, 0x8D40, 0x8D00, 0x0C11, 0x100
    print('GL_RENDERER', glGetString(0x1F01).decode(), '| GL_VERSION', glGetString(0x1F02).decode())

    n = args.tile
    atlas = n * 2  # the tile sits at (n/2, n/2) in an atlas twice its size: the offsets are exercised
    ox = oy = n // 2
    rng = random.Random(args.seed)

    tex = (U * 3)()
    glGenTextures(3, tex)  # S, L (incremental), R (reference L)
    for i in range(3):
        glBindTexture(GL_TEXTURE_2D, tex[i])
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_D32F, atlas, atlas)
    fbo = (U * 3)()
    glGenFramebuffers(3, fbo)
    for i in range(3):
        glBindFramebuffer(GL_FB, fbo[i])
        glFramebufferTexture2D(GL_FB, GL_DEPTH_ATT, GL_TEXTURE_2D, tex[i], 0)
        glDrawBuffer(0)
        glReadBuffer(0)
    glViewport(0, 0, atlas, atlas)

    def upload(t, data):
        glBindTexture(GL_TEXTURE_2D, t)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, atlas, atlas, 0x1902, 0x1406, data.buffer_info()[0])

    def download(t):
        out = array.array('f', [0.0]) * (atlas * atlas)
        glBindTexture(GL_TEXTURE_2D, t)
        glGetTexImage(GL_TEXTURE_2D, 0, 0x1902, 0x1406, out.buffer_info()[0])
        return out

    S = array.array('f', [rng.random() for _ in range(atlas * atlas)])
    for t in tex:
        upload(t, S)

    def copy(dst, x0, y0, x1, y1):
        if x1 > x0 and y1 > y0:
            glCopyImageSubData(tex[0], GL_TEXTURE_2D, 0, ox + x0, oy + y0, 0, tex[dst], GL_TEXTURE_2D, 0, ox + x0, oy + y0, 0, x1 - x0, y1 - y0, 1)

    def draw(dst, x0, y0, x1, y1, depth):
        # the avatar's raster stand-in: a constant written over the scissor only (the property under test is the copy and the scissor)
        if x1 <= x0 or y1 <= y0:
            return
        glBindFramebuffer(GL_FB, fbo[dst])
        glEnable(GL_SCISSOR)
        glScissor(ox + x0, oy + y0, x1 - x0, y1 - y0)
        glDepthMask(1)
        glClearDepth(depth)
        glClear(GL_DEPTH_BIT)
        glDisable(GL_SCISSOR)

    def union(a, b):
        if a is None:
            return b
        if b is None:
            return a
        return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))

    prev = None  # the rectangle L's tile differs from S in (None: equal everywhere)
    failures = 0
    whole = partial = 0
    for p in range(args.passes):
        kind = rng.randrange(10)
        if kind == 0:
            now = None
        elif kind == 1:
            now = (0, 0, n, n)
        else:
            x, y = rng.randrange(n - 8), rng.randrange(n - 8)
            now = (x, y, min(n, x + 1 + rng.randrange(n // 3)), min(n, y + 1 + rng.randrange(n // 3)))
        value = rng.random()
        c = union(prev, now)
        if c == (0, 0, n, n):
            whole += 1
        else:
            partial += 1
        if c:
            copy(1, *c)
        if now:
            draw(1, *now, value)
        copy(2, 0, 0, n, n)
        if now:
            draw(2, *now, value)
        prev = now
        glFinish()
        L, R = download(tex[1]), download(tex[2])
        bad = 0
        for y in range(n):
            row = (oy + y) * atlas + ox
            if L[row:row + n] != R[row:row + n]:
                bad += 1
        if bad:
            failures += 1
            print(f'FAIL pass {p}: {bad} rows of the tile differ from the whole-tile reference')
    glFinish()
    # outside the last rectangle L equals S as well
    L = download(tex[1])
    outside_bad = 0
    for y in range(n):
        for x in range(n):
            inside = prev is not None and prev[0] <= x < prev[2] and prev[1] <= y < prev[3]
            i = (oy + y) * atlas + ox + x
            if not inside and L[i] != S[i]:
                outside_bad += 1
    if outside_bad:
        failures += 1
        print(f'FAIL: {outside_bad} texels outside the last footprint differ from S')
    print(f'{args.passes} passes ({partial} partial copies, {whole} whole-tile copies), tile {n}, atlas {atlas}')
    print('G-footprint', 'FAIL' if failures else 'PASS')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
