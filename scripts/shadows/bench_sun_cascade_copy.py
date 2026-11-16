#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""GPU cost of the sun cascade cache's per-frame copy (doc/shadows/sun-cascades-refresh.md, Costs).

The cached design keeps a static depth map S per level and copies it into the live map L every frame before
the dynamic casters (avatars, movers) are drawn into L, the projector pool's S -> L pattern
(llprojectorshadows.cpp, glCopyImageSubData). This times that copy on the viewer's sun map format
(GL_DEPTH_COMPONENT24, square, size sqrt(w*h)) for four levels, whole maps and half maps (the view's footprint
only), with a depth clear of the same maps as a reference. GL_TIME_ELAPSED, median of 60 frames after 10 warm-up.

Windows only (WGL, hidden window). Holds the shared GPU bench lock (.fixture-tmp/gpu-bench.lock) while timing.
Run: python scripts/shadows/bench_sun_cascade_copy.py
"""

import ctypes as C
import os
import statistics
import sys
import time
from ctypes import wintypes as W
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
# the main checkout's lock, also from a worktree in <repo>/work/<name> (AGENTS.md), so studies share one lock
MAIN = REPO.parents[1] if REPO.parent.name == "work" else REPO
LOCK = Path(os.environ.get("VKS_GPU_BENCH_LOCK", str(MAIN / ".fixture-tmp" / "gpu-bench.lock")))


def take_lock():
    while True:
        try:
            fd = os.open(str(LOCK), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, f"bench_sun_cascade_copy pid {os.getpid()}\n".encode())
            os.close(fd)
            return
        except FileExistsError:
            print("gpu-bench.lock is held; retrying in 60 s", flush=True)
            time.sleep(60)


def release_lock():
    try:
        LOCK.unlink()
    except FileNotFoundError:
        pass


user = C.WinDLL("user32", use_last_error=True)
gdi = C.WinDLL("gdi32", use_last_error=True)
gl = C.WinDLL("opengl32.dll", use_last_error=True)
user.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_int, C.c_int, C.c_int, C.c_int, W.HWND, W.HMENU, W.HINSTANCE, C.c_void_p]
user.CreateWindowExW.restype = W.HWND
user.GetDC.argtypes = [W.HWND]
user.GetDC.restype = W.HDC


class PFD(C.Structure):
    _fields_ = [("size", W.WORD), ("version", W.WORD), ("flags", W.DWORD), ("pixel", C.c_ubyte), ("color", C.c_ubyte),
                ("rest", C.c_ubyte * 18), ("layerMask", W.DWORD), ("visibleMask", W.DWORD), ("damageMask", W.DWORD)]


def make_context():
    pfd = PFD()
    pfd.size = C.sizeof(PFD)
    pfd.version = 1
    pfd.flags = 0x24
    pfd.color = 24
    window = user.CreateWindowExW(0, "STATIC", "sun cascade copy bench", 0, 0, 0, 32, 32, None, None, None, None)
    hdc = user.GetDC(window)
    gdi.ChoosePixelFormat.argtypes = [W.HDC, C.POINTER(PFD)]
    gdi.SetPixelFormat.argtypes = [W.HDC, C.c_int, C.POINTER(PFD)]
    assert gdi.SetPixelFormat(hdc, gdi.ChoosePixelFormat(hdc, C.byref(pfd)), C.byref(pfd))
    gl.wglCreateContext.argtypes = [W.HDC]
    gl.wglCreateContext.restype = C.c_void_p
    gl.wglMakeCurrent.argtypes = [W.HDC, C.c_void_p]
    gl.wglGetProcAddress.argtypes = [C.c_char_p]
    gl.wglGetProcAddress.restype = C.c_void_p
    ctx = gl.wglCreateContext(hdc)
    assert ctx and gl.wglMakeCurrent(hdc, ctx)


def fn(name, result, *types):
    address = gl.wglGetProcAddress(name.encode())
    if address not in (None, 0, 1, 2, 3, C.c_void_p(-1).value):
        return C.WINFUNCTYPE(result, *types)(address)
    f = getattr(gl, name)
    f.restype = result
    f.argtypes = list(types)
    return f


def main():
    make_context()
    U, I, P = C.c_uint, C.c_int, C.c_void_p
    get_string = fn("glGetString", C.c_char_p, U)
    gen_textures = fn("glGenTextures", None, I, C.POINTER(U))
    bind_texture = fn("glBindTexture", None, U, U)
    tex_storage = fn("glTexStorage2D", None, U, I, U, I, I)
    copy = fn("glCopyImageSubData", None, U, U, I, I, I, I, U, U, I, I, I, I, I, I, I)
    gen_fbos = fn("glGenFramebuffers", None, I, C.POINTER(U))
    bind_fbo = fn("glBindFramebuffer", None, U, U)
    fbo_tex = fn("glFramebufferTexture2D", None, U, U, U, U, I)
    clear_depth = fn("glClearDepth", None, C.c_double)
    clear = fn("glClear", None, U)
    viewport = fn("glViewport", None, I, I, I, I)
    gen_queries = fn("glGenQueries", None, I, C.POINTER(U))
    begin_query = fn("glBeginQuery", None, U, U)
    end_query = fn("glEndQuery", None, U)
    get_query = fn("glGetQueryObjectui64v", None, U, U, C.POINTER(C.c_uint64))
    finish = fn("glFinish", None)
    print("GL:", get_string(0x1F02).decode(), get_string(0x1F01).decode(), flush=True)

    GL_TEXTURE_2D, GL_DEPTH_COMPONENT24, GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT = 0x0DE1, 0x81A6, 0x8D40, 0x8D00
    GL_TIME_ELAPSED, GL_QUERY_RESULT, GL_DEPTH_BUFFER_BIT = 0x88BF, 0x8866, 0x100
    q = U()
    gen_queries(1, C.byref(q))

    def timed(work, frames=70, warm=10):
        samples = []
        for i in range(frames):
            begin_query(GL_TIME_ELAPSED, q)
            work()
            end_query(GL_TIME_ELAPSED)
            r = C.c_uint64()
            get_query(q, GL_QUERY_RESULT, C.byref(r))
            if i >= warm:
                samples.append(r.value / 1e6)
        return statistics.median(samples)

    rows = []
    for side, screen in ((1440, "1920x1080"), (1920, "2560x1440"), (2880, "3840x2160")):
        tex = (U * 8)()
        gen_textures(8, tex)
        for t in tex:
            bind_texture(GL_TEXTURE_2D, t)
            tex_storage(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, side, side)
        fbos = (U * 4)()
        gen_fbos(4, fbos)
        for k in range(4):
            bind_fbo(GL_FRAMEBUFFER, fbos[k])
            fbo_tex(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex[4 + k], 0)
        clear_depth(1.0)
        viewport(0, 0, side, side)

        def copy_all(frac):
            h = int(side * frac)
            for k in range(4):
                copy(tex[k], GL_TEXTURE_2D, 0, 0, 0, 0, tex[4 + k], GL_TEXTURE_2D, 0, 0, 0, 0, side, h, 1)

        def clear_all():
            for k in range(4):
                bind_fbo(GL_FRAMEBUFFER, fbos[k])
                clear(GL_DEPTH_BUFFER_BIT)

        finish()
        full = timed(lambda: copy_all(1.0))
        half = timed(lambda: copy_all(0.5))
        clr = timed(clear_all)
        mb = 4 * side * side * 4 / 1e6
        rows.append((screen, side, mb, full, half, clr))
        bind_fbo(GL_FRAMEBUFFER, 0)
        fn("glDeleteFramebuffers", None, I, C.POINTER(U))(4, fbos)
        fn("glDeleteTextures", None, I, C.POINTER(U))(8, tex)

    print("screen | map side | 4 maps (MB) | copy 4 maps whole (ms) | copy 4 half maps (ms) | clear 4 maps (ms)")
    for screen, side, mb, full, half, clr in rows:
        print(f"{screen} | {side} | {mb:6.1f} | {full:7.4f} | {half:7.4f} | {clr:7.4f}")


if __name__ == "__main__":
    if sys.platform != "win32":
        print("Windows only")
        sys.exit(0)
    take_lock()
    try:
        main()
    finally:
        release_lock()
