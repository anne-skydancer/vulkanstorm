#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""G1 of the projector shadow pool (spec stage P2): the lookup in a tile of the atlas equals the lookup in a standalone map.

Offline, no viewer, Windows with an OpenGL 4.0 driver. For each tile size N the same depth picture (blocker rectangles,
a lattice, per-texel noise, open areas) is put into

  * a standalone N x N D32F map with CLAMP_TO_EDGE, read by 261253bd43's stSpotFilter (projector_pool_g1_reference.glsl:
    the reference, unchanged), and
  * a tile of a big D32F atlas whose other texels hold blockers everywhere (so any read past the tile edge shows),
    read by shadowStochasticUtil.glsl's stSpotFilterTile (the code under test, compiled from the file itself, between
    its G1-BEGIN and G1-END markers),

at the atlas origin, an interior offset and the far corner, and every receiver's visibility is compared. The receivers cover the whole
tile including its edges (the edge band is most of a small tile), with random depths, receiver-plane gradients and
penumbra scales that make the search radius run from its 1 texel floor to its 10 texel cap.

Pass: (a) the tile in the atlas equals the same code run on the standalone map, max abs difference 0 (this is the atlas
handling: gather corner, edge clamp, no read past the tile); (b) the tile lookup is within 2 ulp of the 261253bd43
reference source (--ref-tol). (b) is not 0 because the two sources are different compilations of the same arithmetic;
on the measured NVIDIA driver they differ in the last bit of about 1 receiver in 130 (1.2e-7).

    python scripts/shadows/projector_pool_g1.py [--sizes 32,64,256,1024] [--atlas 4096] [--grid 512] [--seed 1]

Exit code 0 on pass, 1 on a difference. Needs numpy.
"""
import argparse
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
UTIL = ROOT / 'indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'
REFERENCE = Path(__file__).with_name('projector_pool_g1_reference.glsl')
CONTEXT = ROOT / 'scripts/perf/gl_test_context.py'

ap = argparse.ArgumentParser()
ap.add_argument('--sizes', default='32,64,256,1024')
ap.add_argument('--atlas', type=int, default=4096)
ap.add_argument('--grid', type=int, default=512, help='receivers per side (grid x grid per tile)')
ap.add_argument('--seed', type=int, default=1)
ap.add_argument('--time', action='store_true', help='also time the lookup (GPU ms per 2560x1440 full-screen pass, median of 9 x 20)')
ap.add_argument('--ref-tol', type=float, default=2.5e-7, help='allowed difference to the 261253bd43 source (2 ulp of 1.0)')
opts = ap.parse_args()
sys.argv = [sys.argv[0]]  # the GL context helper parses the command line itself
exec(compile(CONTEXT.read_text(), str(CONTEXT), 'exec'))  # window, context, fn(), program()


def section(text, begin, end):
    i = text.index(chr(10), text.index(begin)) + 1  # from the line after the marker
    j = text.index(end, i)
    return text[i:j]


util = UTIL.read_text(encoding='utf-8')
defines = '\n'.join(l for l in util.splitlines() if re.match(r'#define (ST_|SHADOW_)', l))
under_test = section(util, '// <G1-BEGIN>', '// <G1-END>')
reference = REFERENCE.read_text(encoding='utf-8')
reference = reference.replace('#define ST_SPOT_KERNEL_MAX 10.0', '#define ST_SPOT_KERNEL_MAX 10.0')

PREAMBLE = '''#version 400
#pragma optionNV(unroll all)
#pragma optionNV(fastmath off)
#pragma optionNV(fastprecision off)
#define SHADOW_BIAS_TEXELS 1.0
#define SHADOW_PLANE_BIAS_MAX_TEXELS 8.0
''' + defines + '\n'

# one program for the reference (standalone map) and one for the tile lookup, the same receivers
RECEIVER = '''
uniform vec2 grid;
uniform float n_tile;
out vec4 oc;
float hash(vec2 p, float s) { return fract(sin(dot(p, vec2(12.9898, 78.233)) + s * 37.719) * 43758.5453); }
void main()
{
    vec2 id = floor(gl_FragCoord.xy);
    vec3 c = vec3((id + 0.5) / grid, 0.2 + 0.7 * hash(id, 1.0));
    vec2 g = vec2(hash(id, 2.0) - 0.5, hash(id, 3.0) - 0.5) * 0.04;
    float z_tex = 0.0015;
    vec2 pen = vec2(mix(2.0, 30.0, hash(id, 4.0)), mix(2.0, 30.0, hash(id, 5.0)));
    float z_bias = SHADOW_BIAS_TEXELS * z_tex;
%s
}
'''
REF_BODY = '    oc = vec4(stSpotFilter(map, vec2(n_tile), c, g, z_bias, z_tex, pen));'
TILE_BODY = '    oc = vec4(stSpotFilterTile(c, g, z_bias, z_tex, pen, proj_shadow_kernel_max));'
VS = '#version 400\nvoid main(){ vec2 p = vec2((gl_VertexID & 1) * 4 - 1, (gl_VertexID >> 1) * 4 - 1); gl_Position = vec4(p, 0.0, 1.0); }'

# the helpers the reference shares with the code under test (StSpot, stSpotDisc, stSpotGradient are not needed here)
HELPERS = '''
vec2 stSpotDisc(int k, int n)
{
    float r = sqrt((float(k) + 0.5) / float(n));
    float phi = float(k) * ST_GOLDEN;
    return r * vec2(cos(phi), sin(phi));
}
'''
# the code under test carries its own stSpotDisc: take the reference's from there, not twice
ref_src = (PREAMBLE + 'uniform sampler2D map;\nuniform sampler2D proj_shadow_atlas;\nuniform mat4 proj_shadow_matrix;\n'
           'uniform vec4 proj_shadow_tile;\nuniform float proj_shadow_kernel_max;\n' + HELPERS + reference + RECEIVER % REF_BODY)
# the largest radius (texels, per axis) is an argument of the lookup: sampleProjectorShadow derives it in metres (ST_SPOT_RADIUS_MAX_M);
# the test feeds it as a uniform
tile_src = PREAMBLE + 'uniform vec2 proj_shadow_kernel_max;\n' + under_test + RECEIVER % TILE_BODY

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
glUniform1f = fn('glUniform1f', None, I, C.c_float)
glUniform2f = fn('glUniform2f', None, I, C.c_float, C.c_float)
glUniform4f = fn('glUniform4f', None, I, C.c_float, C.c_float, C.c_float, C.c_float)
glGetTexImage = fn('glGetTexImage', None, U, I, U, U, P)
glGenVertexArrays = fn('glGenVertexArrays', None, I, C.POINTER(U))
glBindVertexArray = fn('glBindVertexArray', None, U)
glDisable = fn('glDisable', None, U)
glFinish = fn('glFinish', None)
glGetError = fn('glGetError', U)

TEX = 0x0DE1
GL_DEPTH32F = 0x8CAC
GL_DEPTH = 0x1902
GL_FLOAT = 0x1406
GL_R32F = 0x822E
GL_RED = 0x1903


def texture(fmt, w, h, data, fmt_in, wrap_clamp=True):
    t = U()
    glGenTextures(1, C.byref(t))
    glBindTexture(TEX, t.value)
    glTexImage2D(TEX, 0, fmt, w, h, 0, fmt_in, GL_FLOAT, data.ctypes.data if data is not None else None)
    for pname, value in ((0x2801, 0x2600), (0x2800, 0x2600), (0x2802, 0x812F), (0x2803, 0x812F), (0x884C, 0)):
        glTexParameteri(TEX, pname, value)  # NEAREST, CLAMP_TO_EDGE, compare mode NONE
    return t.value


def depth_picture(n, rng):
    """A depth map of n x n texels: open (1.0) with blocker rectangles, a lattice and a band of noise."""
    z = np.ones((n, n), np.float32)
    for _ in range(max(6, n // 8)):
        w, h = rng.integers(1, max(2, n // 3), 2)
        x, y = rng.integers(0, n, 2)
        z[y:y + h, x:x + w] = rng.uniform(0.05, 0.85)
    z[n // 4:n // 4 + max(2, n // 8):1, :][:, ::3] = 0.4         # a lattice of columns
    z[n // 2:n // 2 + max(2, n // 16), ::2] = 0.35               # and of rows
    noise = rng.random((max(2, n // 8), n)) < 0.4
    band = z[3 * n // 4:3 * n // 4 + noise.shape[0], :]
    band[noise] = rng.uniform(0.1, 0.8, noise.sum())
    # texels on the very edge: some blocked, so an edge tap matters
    z[0, ::5] = 0.3
    z[-1, 1::5] = 0.3
    z[::7, 0] = 0.3
    z[1::7, -1] = 0.3
    return z


def run(prog, grid, out_tex, out_fb, bindings, n):
    glBindFramebuffer(0x8D40, out_fb)
    glViewport(0, 0, grid, grid)
    glDisable(0x0B71)  # depth test
    use(prog)
    for unit, name, tex in bindings:
        glActiveTexture(0x84C0 + unit)
        glBindTexture(TEX, tex)
        glUniform1i(glGetUniformLocation(prog, name.encode()), unit)
    glUniform2f(glGetUniformLocation(prog, b'grid'), float(grid), float(grid))
    glUniform1f(glGetUniformLocation(prog, b'n_tile'), float(n))
    return prog


def read_output(tex, grid):
    buf = np.empty((grid, grid), np.float32)
    glBindTexture(TEX, tex)
    glGetTexImage(TEX, 0, GL_RED, GL_FLOAT, buf.ctypes.data)
    return buf


def gpu_ms(prog_setup, draws=20, rounds=9):
    import statistics
    q = U()
    fn('glGenQueries', None, I, C.POINTER(U))(1, C.byref(q))
    begin, end = fn('glBeginQuery', None, U, U), fn('glEndQuery', None, U)
    get64 = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))
    samples = []
    for r in range(rounds + 1):
        prog_setup()
        glDrawArrays(4, 0, 3)  # warm up
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


def main():
    rng = np.random.default_rng(opts.seed)
    vao = U()
    glGenVertexArrays(1, C.byref(vao))
    glBindVertexArray(vao.value)
    ref_prog = program([(0x8B31, VS), (0x8B30, ref_src)])
    tile_prog = program([(0x8B31, VS), (0x8B30, tile_src)])
    out_tex = texture(GL_R32F, opts.grid, opts.grid, None, GL_RED)
    out_fb = U()
    glGenFramebuffers(1, C.byref(out_fb))
    glBindFramebuffer(0x8D40, out_fb.value)
    glFramebufferTexture2D(0x8D40, 0x8CE0, TEX, out_tex, 0)
    assert glCheckFramebufferStatus(0x8D40) == 0x8CD5

    A = opts.atlas
    failures = 0
    print('G1: tile lookup against the standalone map, atlas %d x %d, %d x %d receivers per case' % (A, A, opts.grid, opts.grid))
    for n in [int(s) for s in opts.sizes.split(',')]:
        picture = depth_picture(n, rng)
        standalone = texture(GL_DEPTH32F, n, n, picture, GL_DEPTH)
        ref_prog_bound = run(ref_prog, opts.grid, out_tex, out_fb.value, [(0, 'map', standalone)], n)
        glDrawArrays(4, 0, 3)
        glFinish()
        expected = read_output(out_tex, opts.grid)
        # the code under test on the standalone map itself (tile = the whole map): separates the atlas handling from
        # the arithmetic of the two sources
        prog = run(tile_prog, opts.grid, out_tex, out_fb.value, [(0, 'proj_shadow_atlas', standalone)], n)
        glUniform4f(glGetUniformLocation(prog, b'proj_shadow_tile'), 0.0, 0.0, float(n), 1.0 / n)
        glUniform2f(glGetUniformLocation(prog, b'proj_shadow_kernel_max'), 10.0, 10.0)
        glDrawArrays(4, 0, 3)
        glFinish()
        same_code = read_output(out_tex, opts.grid)
        d0 = np.abs(same_code - expected)
        print('  N=%-4d tile code on the standalone map vs reference: max abs diff %.9g, %d differing' % (n, d0.max(), np.count_nonzero(d0)))
        for label, (ox, oy) in (('atlas origin', (0, 0)), ('interior offset', (A // 2, A // 4)), ('far corner', (A - n, A - n))):
            if ox + n > A or oy + n > A:
                continue
            atlas = np.zeros((A, A), np.float32)  # blockers everywhere: a read past the tile edge shows
            atlas[oy:oy + n, ox:ox + n] = picture
            atlas_tex = texture(GL_DEPTH32F, A, A, atlas, GL_DEPTH)
            prog = run(tile_prog, opts.grid, out_tex, out_fb.value, [(0, 'proj_shadow_atlas', atlas_tex)], n)
            glUniform4f(glGetUniformLocation(prog, b'proj_shadow_tile'), float(ox), float(oy), float(n), 1.0 / A)
            glUniform2f(glGetUniformLocation(prog, b'proj_shadow_kernel_max'), 10.0, 10.0)
            glDrawArrays(4, 0, 3)
            glFinish()
            actual = read_output(out_tex, opts.grid)
            diff = np.abs(actual - expected)
            d_same = np.abs(actual - same_code)
            print('  N=%-4d %-16s vs the same code on the standalone map: max abs diff %.9g, %d differing' % (n, label, d_same.max(), np.count_nonzero(d_same)))
            partial = np.count_nonzero((expected > 0.0) & (expected < 1.0))
            # the tile in the atlas must equal the same code on a standalone map exactly; the 261253bd43 source is a different
            # compilation of the same arithmetic, which the NVIDIA compiler rounds differently in the last bit of a few receivers
            ok = bool(d_same.max() == 0.0) and bool(diff.max() <= opts.ref_tol) and glGetError() == 0
            failures += 0 if ok else 1
            print('  N=%-4d %-16s max abs diff %.9g, %d differing of %d receivers (%.0f %% lit, %.0f %% in penumbra)  %s' % (
                n, label, diff.max(), np.count_nonzero(diff), diff.size, 100 * float(np.mean(expected == 1.0)),
                100 * partial / diff.size, 'PASS' if ok else 'FAIL'))
            glBindTexture(TEX, 0)
            fn('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(U(atlas_tex)))
        # the radius cap sampleProjectorShadow derives in metres (clamp(0.04 m k N / w, 1, 12) texels per axis): any value in
        # [1, 12], the axes different, atlas against standalone, with the same code
        for km in ((5.0, 5.0), (1.0, 1.0), (12.0, 1.0), (2.5, 7.0)):
            run(tile_prog, opts.grid, out_tex, out_fb.value, [(0, 'proj_shadow_atlas', standalone)], n)
            glUniform4f(glGetUniformLocation(tile_prog, b'proj_shadow_tile'), 0.0, 0.0, float(n), 1.0 / n)
            glUniform2f(glGetUniformLocation(tile_prog, b'proj_shadow_kernel_max'), km[0], km[1])
            glDrawArrays(4, 0, 3)
            glFinish()
            km_standalone = read_output(out_tex, opts.grid)
            ox, oy = A // 2, A // 4
            if ox + n > A or oy + n > A:
                continue
            atlas = np.zeros((A, A), np.float32)
            atlas[oy:oy + n, ox:ox + n] = picture
            atlas_tex = texture(GL_DEPTH32F, A, A, atlas, GL_DEPTH)
            run(tile_prog, opts.grid, out_tex, out_fb.value, [(0, 'proj_shadow_atlas', atlas_tex)], n)
            glUniform4f(glGetUniformLocation(tile_prog, b'proj_shadow_tile'), float(ox), float(oy), float(n), 1.0 / A)
            glUniform2f(glGetUniformLocation(tile_prog, b'proj_shadow_kernel_max'), km[0], km[1])
            glDrawArrays(4, 0, 3)
            glFinish()
            km_atlas = read_output(out_tex, opts.grid)
            dk = np.abs(km_atlas - km_standalone)
            ok = bool(dk.max() == 0.0) and glGetError() == 0
            failures += 0 if ok else 1
            print('  N=%-4d kernel max %-10s atlas vs standalone: max abs diff %.9g  %s' % (n, '%g,%g' % km, dk.max(), 'PASS' if ok else 'FAIL'))
            glBindTexture(TEX, 0)
            fn('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(U(atlas_tex)))
        fn('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(U(standalone)))
    if opts.time:
        W = 2560
        big = texture(GL_R32F, W, 1440, None, GL_RED)
        fbo = U()
        glGenFramebuffers(1, C.byref(fbo))
        glBindFramebuffer(0x8D40, fbo.value)
        glFramebufferTexture2D(0x8D40, 0x8CE0, TEX, big, 0)
        n = 1024
        picture = depth_picture(n, np.random.default_rng(opts.seed))
        standalone = texture(GL_DEPTH32F, n, n, picture, GL_DEPTH)
        atlas = np.zeros((A, A), np.float32)
        atlas[A // 4:A // 4 + n, A // 2:A // 2 + n] = picture
        atlas_tex = texture(GL_DEPTH32F, A, A, atlas, GL_DEPTH)

        def setup_ref():
            run(ref_prog, W, big, fbo.value, [(0, 'map', standalone)], n)
            glViewport(0, 0, W, 1440)
            glUniform2f(glGetUniformLocation(ref_prog, b'grid'), float(W), 1440.0)

        def setup_tile():
            prog = run(tile_prog, W, big, fbo.value, [(0, 'proj_shadow_atlas', atlas_tex)], n)
            glViewport(0, 0, W, 1440)
            glUniform2f(glGetUniformLocation(prog, b'grid'), float(W), 1440.0)
            glUniform4f(glGetUniformLocation(prog, b'proj_shadow_tile'), float(A // 2), float(A // 4), float(n), 1.0 / A)
            glUniform2f(glGetUniformLocation(prog, b'proj_shadow_kernel_max'), 10.0, 10.0)
        t_ref, t_tile = gpu_ms(setup_ref), gpu_ms(setup_tile)
        print('lookup GPU time, 2560x1440 full screen, N=1024 (same receivers): standalone map %.3f ms, tile in the atlas %.3f ms (%+.1f %%)' % (
            t_ref, t_tile, 100 * (t_tile / t_ref - 1)))
    print('G1', 'PASS' if failures == 0 else 'FAIL (%d cases)' % failures)
    return 0 if failures == 0 else 1


sys.exit(main())
