#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The lookup through a wide lamp's cells equals the lookup through one virtual tile of the same texel grid (projector tile resolution).

Offline, no viewer, Windows with an OpenGL 4.0 driver. A wide lamp is split into gu x gv cells (llprojectorpool::gridOf), each a tile of
Nc texels with a guard of G texels on each side around an interior of Ni = Nc - 2G (the cell's frustum is widened by the guard, so a
tile holds the neighbour's content past its seam). This check builds

  * a virtual map of (gu Ni) x (gv Ni) texels from a depth picture (blocker rectangles, a lattice, noise, texels on every edge): what
    one tile of the same texel grid would hold, read by shadowStochasticUtil.glsl's stSpotFilterTile with the tile = the whole map, and
  * the cells' tiles cut from the same picture (the guard texels are the neighbour's, or the clamped edge outside the image), put
    at scattered origins of a big atlas whose other texels are blockers (so a read past a tile shows), read through the production
    stCellPick (the cell pick, the tile uv and the gradient mapping) and stSpotFilterTile,

for receivers over the whole lamp image, seams included, with random depths, receiver-plane gradients and penumbra scales (the search
radius runs from its 1 texel floor to the 12 texel cap; the guard holds 16). Both programs are compiled from the util file itself
(the code between its G1-BEGIN and G1-END markers).
Not covered: the glue in sampleProjectorShadow (the second pick for the offset receiver, the kn and z_tex recomputation, the
gradient divided by tscale), which lies outside the G1 block; the tolerance is not G1's max abs 0 (another texel route).

Pass: the largest difference is <= --tol (1e-4: the tile uv is the same texel position computed by another route, so the two differ in
the rounding of a gather's weights, and a texel flip at a tap on a cell boundary is a different, equal read), and a cell that is not
drawn (Nc 0) is reported by stCellPick. With --time: the GPU time of both lookups at 2560x1440 (the cost of the cell pick).

    python scripts/shadows/projector_cells_check.py [--grid 3,3] [--ni 96] [--seed 1] [--time]

Exit code 0 on pass, 1 on a difference. Needs numpy.
"""
import argparse
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
UTIL = ROOT / 'indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'
CONTEXT = ROOT / 'scripts/perf/gl_test_context.py'

ap = argparse.ArgumentParser()
ap.add_argument('--grid', default='3,3', help='cells across u, across v')
ap.add_argument('--ni', type=int, default=96, help='interior texels of a cell')
ap.add_argument('--guard', type=int, default=16)
ap.add_argument('--atlas', type=int, default=2048)
ap.add_argument('--receivers', type=int, default=384, help='receivers per side over the lamp image')
ap.add_argument('--seed', type=int, default=1)
ap.add_argument('--tol', type=float, default=1.0e-4)
ap.add_argument('--time', action='store_true')
opts = ap.parse_args()
sys.argv = [sys.argv[0]]
exec(compile(CONTEXT.read_text(), str(CONTEXT), 'exec'))  # window, context, fn(), program(), use()


def section(text, begin, end):
    i = text.index(chr(10), text.index(begin)) + 1
    j = text.index(end, i)
    return text[i:j]


util = UTIL.read_text(encoding='utf-8')
defines = '\n'.join(l for l in util.splitlines() if re.match(r'#define (ST_|SHADOW_)', l))
under_test = section(util, '// <G1-BEGIN>', '// <G1-END>')
PREAMBLE = '''#version 400
#pragma optionNV(unroll all)
#pragma optionNV(fastmath off)
#pragma optionNV(fastprecision off)
#define SHADOW_BIAS_TEXELS 1.0
#define SHADOW_PLANE_BIAS_MAX_TEXELS 8.0
''' + defines + '\nuniform vec2 proj_shadow_kernel_max;\n'
RECEIVER = '''
uniform vec2 grid;
out vec4 oc;
float hash(vec2 p, float s) { return fract(sin(dot(p, vec2(12.9898, 78.233)) + s * 37.719) * 43758.5453); }
void main()
{
    vec2 id = floor(gl_FragCoord.xy);
    vec3 c = vec3((id + 0.5) / grid, 0.2 + 0.7 * hash(id, 1.0));   // lamp image uv and depth
    vec2 g = vec2(hash(id, 2.0) - 0.5, hash(id, 3.0) - 0.5) * 0.04; // d depth / d uv of the lamp image (scaled below to a texel's)
    float z_tex = 0.0015;
    vec2 pen = vec2(mix(2.0, 30.0, hash(id, 4.0)), mix(2.0, 30.0, hash(id, 5.0)));
    float z_bias = SHADOW_BIAS_TEXELS * z_tex;
%s
}
'''
# the virtual tile: the whole virtual map is one tile; the gradient is per tile uv, which is the lamp's (g = 1 cell of the same texels)
VIRT_BODY = '    oc = vec4(stSpotFilterTile(proj_shadow_atlas, proj_shadow_tile, c, g * GRAD_SCALE, z_bias, z_tex, pen, proj_shadow_kernel_max));'
CELL_BODY = '''    vec4 tile; vec2 uvt, gres, tscale;
    if (!stCellPick(c.xy, tile, uvt, gres, tscale)) { oc = vec4(-1.0); return; }
    oc = vec4(stSpotFilterTile(proj_shadow_atlas, tile, vec3(uvt, c.z), g * GRAD_SCALE / tscale, z_bias, z_tex, pen, proj_shadow_kernel_max));'''
VS = '#version 400\nvoid main(){ vec2 p = vec2((gl_VertexID & 1) * 4 - 1, (gl_VertexID >> 1) * 4 - 1); gl_Position = vec4(p, 0.0, 1.0); }'

glGenTextures = fn('glGenTextures', None, I, C.POINTER(U))
glBindTexture = fn('glBindTexture', None, U, U)
glTexImage2D = fn('glTexImage2D', None, U, I, I, I, I, I, U, U, P)
glTexParameteri = fn('glTexParameteri', None, U, U, I)
glActiveTexture = fn('glActiveTexture', None, U)
glGenFramebuffers = fn('glGenFramebuffers', None, I, C.POINTER(U))
glBindFramebuffer = fn('glBindFramebuffer', None, U, U)
glFramebufferTexture2D = fn('glFramebufferTexture2D', None, U, U, U, U, I)
glCheckFramebufferStatus = fn('glCheckFramebufferStatus', U, U)
glViewport = fn('glViewport', None, I, I, I, I)
glDrawArrays = fn('glDrawArrays', None, U, I, I)
glGetUniformLocation = fn('glGetUniformLocation', I, U, C.c_char_p)
glUniform1i = fn('glUniform1i', None, I, I)
glUniform2f = fn('glUniform2f', None, I, C.c_float, C.c_float)
glUniform4f = fn('glUniform4f', None, I, C.c_float, C.c_float, C.c_float, C.c_float)
glUniform4fv = fn('glUniform4fv', None, I, I, P)
glGetTexImage = fn('glGetTexImage', None, U, I, U, U, P)
glGenVertexArrays = fn('glGenVertexArrays', None, I, C.POINTER(U))
glBindVertexArray = fn('glBindVertexArray', None, U)
glDisable = fn('glDisable', None, U)
glFinish = fn('glFinish', None)
glGetError = fn('glGetError', U)
TEX = 0x0DE1
GL_DEPTH32F, GL_DEPTH, GL_FLOAT, GL_R32F, GL_RED = 0x8CAC, 0x1902, 0x1406, 0x822E, 0x1903


def texture(fmt, w, h, data, fmt_in):
    t = U()
    glGenTextures(1, C.byref(t))
    glBindTexture(TEX, t.value)
    glTexImage2D(TEX, 0, fmt, w, h, 0, fmt_in, GL_FLOAT, data.ctypes.data if data is not None else None)
    for pname, value in ((0x2801, 0x2600), (0x2800, 0x2600), (0x2802, 0x812F), (0x2803, 0x812F), (0x884C, 0)):
        glTexParameteri(TEX, pname, value)
    return t.value


def depth_picture(w, h, rng):
    z = np.ones((h, w), np.float32)
    for _ in range(max(10, (w * h) // 600)):
        bw, bh = rng.integers(1, max(2, w // 4)), rng.integers(1, max(2, h // 4))
        x, y = rng.integers(0, w), rng.integers(0, h)
        z[y:y + bh, x:x + bw] = rng.uniform(0.05, 0.85)
    z[h // 4:h // 4 + max(2, h // 12), ::3] = 0.4
    z[h // 2:h // 2 + max(2, h // 24), ::2] = 0.35
    noise = rng.random((max(2, h // 8), w)) < 0.4
    band = z[3 * h // 4:3 * h // 4 + noise.shape[0], :]
    band[noise] = rng.uniform(0.1, 0.8, noise.sum())
    z[0, ::5] = 0.3
    z[-1, 1::5] = 0.3
    z[::7, 0] = 0.3
    z[1::7, -1] = 0.3
    return z


def main():
    gu, gv = [int(v) for v in opts.grid.split(',')]
    ni, G, A = opts.ni, opts.guard, opts.atlas
    nc = ni + 2 * G
    rng = np.random.default_rng(opts.seed)
    vao = U()
    glGenVertexArrays(1, C.byref(vao))
    glBindVertexArray(vao.value)
    # the virtual tile must be square for the filter (res = vec2(tile.z)): the cells are square tiles, the lamp image is gu x gv cells,
    # so test a square grid, or compare per axis with N = gu * ni (the cells' texel is the same on both axes)
    if gu != gv:
        sys.exit('this check compares against a square virtual tile: use --grid n,n')
    N = gu * ni
    picture = depth_picture(N, N, rng)
    padded = np.pad(picture, G, mode='edge')  # clamp-to-edge: the guard outside the lamp image repeats its border texels

    # the virtual tile and the cells in the atlas (scattered origins; blockers elsewhere)
    virt_tex = texture(GL_DEPTH32F, N, N, picture, GL_DEPTH)
    atlas = np.zeros((A, A), np.float32)
    cells = np.zeros((16, 4), np.float32)
    origins = [(x, y) for y in range(0, A - nc + 1, nc + 37) for x in range(0, A - nc + 1, nc + 53)]
    rng.shuffle(origins)
    for j in range(gv):
        for i in range(gu):
            ox, oy = origins[j * gu + i]
            # tile texel (tx, ty) holds virtual texel (i ni + tx - G, j ni + ty - G): padded index (i ni + tx, j ni + ty)
            atlas[oy:oy + nc, ox:ox + nc] = padded[j * ni:j * ni + nc, i * ni:i * ni + nc]
            cells[j * 4 + i] = (ox, oy, nc, ni)
    atlas_tex = texture(GL_DEPTH32F, A, A, atlas, GL_DEPTH)

    # the gradient: d depth / d tile uv. A receiver plane in lamp uv has g_lamp; the virtual tile (the same texels) has the same g
    grad_scale = 1.0
    virt_prog = program([(0x8B31, VS), (0x8B30, PREAMBLE + '#define GRAD_SCALE %.1f\n' % grad_scale + under_test + RECEIVER % VIRT_BODY)])
    cell_prog = program([(0x8B31, VS), (0x8B30, PREAMBLE + '#define GRAD_SCALE %.1f\n' % grad_scale + under_test + RECEIVER % CELL_BODY)])
    R = opts.receivers
    out_tex = texture(GL_R32F, R, R, None, GL_RED)
    fb = U()
    glGenFramebuffers(1, C.byref(fb))
    glBindFramebuffer(0x8D40, fb.value)
    glFramebufferTexture2D(0x8D40, 0x8CE0, TEX, out_tex, 0)
    assert glCheckFramebufferStatus(0x8D40) == 0x8CD5

    def run(prog, atlas_name_tex, setup, w=R, h=R):
        glBindFramebuffer(0x8D40, fb.value)
        glViewport(0, 0, w, h)
        glDisable(0x0B71)
        use(prog)
        glActiveTexture(0x84C0)
        glBindTexture(TEX, atlas_name_tex)
        glUniform1i(glGetUniformLocation(prog, b'proj_shadow_atlas'), 0)
        glUniform2f(glGetUniformLocation(prog, b'grid'), float(w), float(h))
        glUniform2f(glGetUniformLocation(prog, b'proj_shadow_kernel_max'), 10.0, 10.0)
        setup(prog)

    def read():
        buf = np.empty((R, R), np.float32)
        glBindTexture(TEX, out_tex)
        glGetTexImage(TEX, 0, GL_RED, GL_FLOAT, buf.ctypes.data)
        return buf

    def virt_setup(prog):
        glUniform4f(glGetUniformLocation(prog, b'proj_shadow_tile'), 0.0, 0.0, float(N), 1.0 / N)
        glUniform4f(glGetUniformLocation(prog, b'proj_shadow_grid'), 1.0, 1.0, 0.0, 0.0)

    def cell_setup(prog):
        glUniform4f(glGetUniformLocation(prog, b'proj_shadow_tile'), 0.0, 0.0, 0.0, 1.0 / A)
        glUniform4f(glGetUniformLocation(prog, b'proj_shadow_grid'), float(gu), float(gv), float(G), 0.0)
        glUniform4fv(glGetUniformLocation(prog, b'proj_shadow_cells'), 16, cells.ctypes.data)

    run(virt_prog, virt_tex, virt_setup)
    glDrawArrays(4, 0, 3)
    glFinish()
    expected = read()
    run(cell_prog, atlas_tex, cell_setup)
    glDrawArrays(4, 0, 3)
    glFinish()
    actual = read()
    diff = np.abs(actual - expected)
    seam = np.zeros((R, R), bool)  # receivers within the filter's reach (12 texels) of a seam
    pos = (np.arange(R) + 0.5) / R * N
    for s in range(1, gu):
        near = np.abs(pos - s * ni) < 14
        seam |= near[None, :] | near[:, None]
    partial = (expected > 0.0) & (expected < 1.0)
    print('cells %dx%d, Ni %d, Nc %d, guard %d, virtual tile %d: %d receivers, %.0f %% lit, %.0f %% in penumbra, %.0f %% within 14 texels of a seam' % (
        gu, gv, ni, nc, G, N, R * R, 100 * float(np.mean(expected == 1.0)), 100 * float(np.mean(partial)), 100 * float(np.mean(seam))))
    print('  all receivers: max abs diff %.3g, %d above 1e-5' % (diff.max(), int(np.count_nonzero(diff > 1e-5))))
    print('  seam receivers: max abs diff %.3g (%d receivers), 99.9th percentile %.3g' % (diff[seam].max(), int(seam.sum()), np.percentile(diff[seam], 99.9)))
    ok = bool(diff.max() <= opts.tol) and glGetError() == 0
    # a cell that is not drawn is reported
    cells_saved = cells.copy()
    cells[0, 2] = 0.0
    run(cell_prog, atlas_tex, cell_setup)
    glDrawArrays(4, 0, 3)
    glFinish()
    undrawn = read()
    cells[:] = cells_saved
    cell_of = np.floor((np.arange(R) + 0.5) / R * gu).astype(int)
    first_cell = (cell_of[None, :] == 0) & (cell_of[:, None] == 0)
    ok_undrawn = bool(np.all(undrawn[first_cell] == -1.0)) and bool(np.all(undrawn[~first_cell] >= 0.0))
    print('  an undrawn cell (Nc 0): reported for its receivers only: %s' % ('yes' if ok_undrawn else 'NO'))
    ok = ok and ok_undrawn
    print('CELLS', 'PASS' if ok else 'FAIL')

    if opts.time:
        import statistics
        W, H = 2560, 1440
        big = texture(GL_R32F, W, H, None, GL_RED)
        glBindFramebuffer(0x8D40, fb.value)
        glFramebufferTexture2D(0x8D40, 0x8CE0, TEX, big, 0)
        q = U()
        fn('glGenQueries', None, I, C.POINTER(U))(1, C.byref(q))
        begin, end = fn('glBeginQuery', None, U, U), fn('glEndQuery', None, U)
        get64 = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))

        def gpu_ms(prog, tex, setup, rounds=9, draws=20):
            samples = []
            for r in range(rounds + 1):
                run(prog, tex, setup, W, H)
                glDrawArrays(4, 0, 3)
                glFinish()
                begin(0x88BF, q.value)
                for _ in range(draws):
                    glDrawArrays(4, 0, 3)
                end(0x88BF)
                v = C.c_uint64()
                get64(q.value, 0x8866, C.byref(v))
                if r:
                    samples.append(v.value / 1e6 / draws)
            return statistics.median(samples)

        t_v = [gpu_ms(virt_prog, virt_tex, virt_setup) for _ in range(3)]
        t_c = [gpu_ms(cell_prog, atlas_tex, cell_setup) for _ in range(3)]
        print('lookup GPU time at %dx%d (median of 9 x 20, three repeats): one virtual tile %s ms, cells %s ms (%+.1f %%)' % (
            W, H, '/'.join('%.3f' % t for t in t_v), '/'.join('%.3f' % t for t in t_c), 100 * (statistics.median(t_c) / statistics.median(t_v) - 1)))
    return 0 if ok else 1


sys.exit(main())
