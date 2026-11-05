# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU benchmark for the projector shadow design (doc/shadows/shadow-model-rework.md §9.4).

Hidden GL window (gl_test_context.py), GL_TIME_ELAPSED medians, no viewer launch.

  tile     rendering one projector shadow tile of R x R texels (depth only, a perspective camera, a
           procedural box field of N triangles inside the cone, D24 like the viewer's maps) into
           a large atlas texture with the viewport and scissor on the tile, clear included. The
           baseline is the viewer's way: a whole 1920 x 1920 / 2560 x 2560 map per projector.
  sample   the lighting-side cost of the projector shadow term at 1080p / 1440p: a full-screen
           pass taking the 5-tap hardware-PCF term of a spot shadow (shadowUtil.glsl pcfSpotShadow's
           shape) from a separate map vs from a tile of a big atlas (rect offset + scale uniforms).
           Per light the cost scales with the screen area its light volume covers.
  fill     what the viewer pays now whether or not a projector is there: the sun pass evaluates the
           two projector shadows for every pixel (bench_shadow_pcss.py, detail 2 minus detail 1).

The CPU side of a shadow render (updateCull, stateSort, batch submission) is not in any offline
bench: see the document for the in-world protocol.
"""
import math, os, statistics, sys
from pathlib import Path
src = (Path(__file__).with_name('bench_shadow_costs.py')).read_text().split('\ndef kern():')[0]
exec(compile(src, 'bench_shadow_costs_head', 'exec'))
scissor_test = 0x0C11
scissor = fn('glScissor', None, I, I, I, I)
cleardepth_ = fn('glClear', None, U)

TILE_V = '''#version 430 core
uniform float extent; uniform uint boxes_per_row;
const vec3 corners[8] = vec3[8](vec3(0,0,0),vec3(1,0,0),vec3(0,1,0),vec3(1,1,0),vec3(0,0,1),vec3(1,0,1),vec3(0,1,1),vec3(1,1,1));
const int idx[36] = int[36](0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,1,4, 1,5,4, 2,6,3, 3,6,7, 0,4,2, 2,4,6, 1,3,5, 3,7,5);
uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }
void main() {
    uint b = uint(gl_VertexID) / 36u; uint v = uint(gl_VertexID) % 36u; uint h = hash(b);
    vec2 cell = vec2(b % boxes_per_row, b / boxes_per_row) / float(boxes_per_row);
    vec2 jit = vec2(h & 0xffu, (h >> 8) & 0xffu) / 255.0 / float(boxes_per_row);
    float size = (0.3 + float((h >> 16) & 0xffu) / 255.0) * extent / float(boxes_per_row) * 0.5;
    vec3 p = vec3(cell + jit, 0.2 + 0.6 * float((h >> 24) & 0xffu) / 255.0) * vec3(extent, extent, 1.0) + corners[idx[v]] * vec3(size, size, 0.05);
    gl_Position = vec4(p.xy / extent * 2.0 - 1.0, p.z * 2.0 - 1.0, 1.0);
}
'''
TILE_F = '#version 430 core\nvoid main() { }\n'
SAMPLE_F = '''#version 430 core
in vec2 vary_fragcoord;
uniform sampler2DShadow shadowMap;
uniform vec4 rect;      // atlas tile offset (xy) and scale (zw); 0,0,1,1 for a separate map
uniform vec2 res;
out vec4 frag_color;
void main() {
    vec3 stc = vec3(rect.xy + (0.5 + 0.35 * sin(vary_fragcoord * 40.0)) * rect.zw, 0.5);
    vec2 off = 1.0 / res; off.y *= 1.5;
    float s = texture(shadowMap, stc);
    s += texture(shadowMap, stc + vec3(off.x * 2.0, off.y, 0.0));
    s += texture(shadowMap, stc + vec3(off.x, -off.y, 0.0));
    s += texture(shadowMap, stc + vec3(-off.x, off.y, 0.0));
    s += texture(shadowMap, stc + vec3(-off.x * 2.0, -off.y, 0.0));
    frag_color = vec4(s * 0.2);
}
'''
FRAMES = int(os.environ.get('PROJ_BENCH_FRAMES', 40))

def make_depth(w, h, filt=0x2601):
    t = make_tex(0x81A6, w, h, filt)
    bindtex(TEX2D, t)
    texparam(TEX2D, 0x884C, 0x884E); texparam(TEX2D, 0x884D, 0x0203)   # compare ref to texture, LEQUAL
    return t

def bench_tiles():
    boxes_for = {0.05: 0.05, 0.25: 0.25, 1.0: 1.0}   # Mtri
    prog = program([(0x8B31, TILE_V), (0x8B30, TILE_F)])
    atlas_w = 8192
    atlas = make_depth(atlas_w, atlas_w)
    fbo = make_fbo([], atlas)
    own = {}
    print('\nprojector tile render (clear + depth-only raster of N triangles in the cone), ms:')
    print('  tile / map          0.05 Mtri   0.25 Mtri   1.0 Mtri   memory (D24 as 32 bit)')
    rows = [('viewer map 1920^2', 1920), ('viewer map 2560^2', 2560), ('atlas tile 2048^2', 2048), ('atlas tile 1024^2', 1024),
            ('atlas tile  512^2', 512), ('atlas tile  256^2', 256), ('atlas tile  128^2', 128)]
    for name, r in rows:
        line = f'  {name:19s}'
        for mt in (0.05, 0.25, 1.0):
            boxes = int(mt * 1e6 / 12); per_row = int(math.ceil(math.sqrt(boxes)))
            use(prog); u1f(loc(prog, b'extent'), 64.0); u1u(loc(prog, b'boxes_per_row'), per_row)
            def render():
                bindfbo(FB, fbo)
                enable(scissor_test); scissor(0, 0, r, r); viewport(0, 0, r, r)
                enable(0x0B71); depthfunc(0x0201); depthmask(1); enable(0x0B44)
                clear(0x100); draw(4, 0, boxes * 36)
                disable(0x0B44); disable(0x0B71); disable(scissor_test)
            line += f'  {timed(render, FRAMES):9.3f}  '
        line += f'  {r * r * 4 / 1048576:6.1f} MB'
        print(line, flush=True)
    assert geterror() == 0
    fb = U(fbo); delfbo(1, C.byref(fb)); tt = U(atlas); deltex(1, C.byref(tt))

def bench_sample(W, H):
    prog = program([(0x8B31, FS_VERTEX), (0x8B30, SAMPLE_F)])
    out = make_tex(0x881A, W, H); fo = make_fbo([out]); viewport(0, 0, W, H); use(prog)
    u1i(loc(prog, b'shadowMap'), 0)
    res = {}
    for label, size, tile in (('separate map', W, None), ('atlas tile 1024', 8192, 1024), ('atlas tile 512', 8192, 512), ('atlas tile 256', 8192, 256)):
        t = make_depth(size, size)
        active(0x84C0); bindtex(TEX2D, t)
        if tile is None:
            u4f = fn('glUniform4f', None, I, F, F, F, F); u4f(loc(prog, b'rect'), 0.0, 0.0, 1.0, 1.0); u2f(loc(prog, b'res'), size, size)
        else:
            u4f = fn('glUniform4f', None, I, F, F, F, F)
            u4f(loc(prog, b'rect'), 3 * tile / 8192.0, 2 * tile / 8192.0, tile / 8192.0, tile / 8192.0); u2f(loc(prog, b'res'), 8192, 8192)
        res[label] = timed(lambda: draw(4, 0, 3), FRAMES)
        tt = U(t); deltex(1, C.byref(tt))
    fb = U(fo); delfbo(1, C.byref(fb)); tt = U(out); deltex(1, C.byref(tt))
    return res

def main():
    print(f'median ms, {FRAMES} frames after {WARMUP} warmup')
    bench_tiles()
    print('\nfull-screen projector shadow term (5-tap hardware PCF), one light covering the whole screen, ms:')
    for W, H in RESOLUTIONS:
        r = bench_sample(W, H)
        print(f'  {W}x{H}: ' + ', '.join(f'{k} {v:.3f}' for k, v in r.items()), flush=True)
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
