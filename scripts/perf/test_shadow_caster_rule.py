# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offscreen checks of the shadow caster programs on this GL driver (no viewer launch; --opengl PATH selects another GL).

The shaders are assembled as LLShaderMgr assembles them (#version 420, the stage define, the program's defines, the objects
LLShaderMgr::attachShaderFeatures attaches: the Stochastic shadow method's rule shadowStochasticCasterF.glsl and the engine's
sampling stochasticSampleF.glsl; stubs for the skinning and texture-index helpers):

  1. every caster program, Legacy and Stochastic, compiles and links;
  2. blended faces under Stochastic (shadowAlphaMaskF with shadow_solid_alpha 1): the texels the GPU keeps are the ones a
     numpy model of shadow_caster_keeps predicts (the lines tests/llstochastichistory_test.cpp twins and
     scripts/tests/test_shadow_alpha_permutation.py pins), texel for texel, for opacities from 0 to 1, draw seeds, block
     origins (odd, negative, far), both sides and triangle indices in the draw (gl_PrimitiveID);
  3. mask faces under Stochastic (shadow_solid_alpha 0): the map is the model's: the creator's cutoff on the texture's alpha,
     the vertex colour's alpha (but fullbright), the stock 0.05 floor, solid above them, never the rule;
  4. the GPU keeps the golden texels that tests/llstochastichistory_test.cpp pins (computed from the Aurora Viewer's formulas
     by scripts/tests/gen_stochastic_goldens.py), so the GLSL, the C++ twin and the Aurora Viewer's formulas agree.

    python scripts/perf/test_shadow_caster_rule.py
    python scripts/perf/test_shadow_caster_rule.py --bench [REF]   # GPU time of the blended caster fragment: Legacy, this
                                                                   # tree's Stochastic, and REF's Stochastic when given
"""
import re
import statistics
import subprocess
import sys
from pathlib import Path

BENCH = '--bench' in sys.argv
BENCH_REF = None
if BENCH:
    i = sys.argv.index('--bench')
    if i + 1 < len(sys.argv) and not sys.argv[i + 1].startswith('--'):
        BENCH_REF = sys.argv.pop(i + 1)
    sys.argv.pop(i)
bootstrap = Path(__file__).with_name('gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DEFERRED = 'indra/newview/app_settings/shaders/class1/deferred/'
VS, FS = 0x8B31, 0x8B30
DEFINE = 'STOCHASTIC_SHADOW_ALPHA'


def tree_text(name):
    return (ROOT / DEFERRED / name).read_text(encoding='utf-8')


def ref_text(ref):
    return lambda name: subprocess.run(['git', 'show', ref + ':' + DEFERRED + name], cwd=ROOT, capture_output=True,
                                       text=True, check=True).stdout


def unit(stage, src, defines, extra=''):
    head = '#version 420\n#define %s_SHADER 1\n' % ('FRAGMENT' if stage == FS else 'VERTEX')
    return (stage, head + ''.join('#define %s 1\n' % d for d in defines) + src.replace('/*[EXTRA_CODE_HERE]*/', extra))


# LLShaderMgr's indexed-texture lookup (mIndexedTextureChannels 4) and the plain one, and stubs of the vertex helpers
INDEXED = ''.join('uniform sampler2D tex%d;\n' % i for i in range(4)) + 'flat in int vary_texture_index;\n' + \
    'vec4 diffuseLookup(vec2 texcoord)\n{\n' + \
    ''.join('\t%sif (vary_texture_index == %d) { return texture(tex%d, texcoord); }\n' % ('else ' if i else '', i, i) for i in range(4)) + \
    '\treturn vec4(1,0,1,1);\n}\n'
LOOKUP = 'uniform sampler2D tex0;\nvec4 diffuseLookup(vec2 texcoord)\n{\nreturn texture(tex0, texcoord);\n}\n'
STUB_V = {
    'skin': 'mat4 getObjectSkinnedTransform() { return mat4(1.0); }\n',
    'avskin': 'mat4 getSkinnedTransform() { return mat4(1.0); }\n',
    'tex': 'void passTextureIndex() {}\n',
    'indexed': 'flat out int vary_texture_index;\nvoid passTextureIndex() { vary_texture_index = 0; }\n',
    'gltf': 'vec2 texture_transform(vec2 t, vec4[2] k, mat4 s) { return t; }\n',
}


def build(text, vfile, ffile, defines, vstubs, lookup):
    """A caster program as the viewer links it: the rule and the engine's sampling come with STOCHASTIC_SHADOW_ALPHA"""
    units = [unit(VS, text(vfile), defines), unit(VS, ''.join(STUB_V[s] for s in vstubs), [])]
    units.append(unit(FS, text(ffile), defines, lookup))
    if DEFINE in defines:
        units.append(unit(FS, text('stochasticSampleF.glsl'), []))
        units.append(unit(FS, text('shadowStochasticCasterF.glsl'), []))
    return program(units)


# ---- 1. every caster program compiles and links
CASTERS = [   # label, vertex, fragment, extra defines, vertex stubs, the fragment's lookup
    ('alphamask', 'shadowAlphaMaskV.glsl', 'shadowAlphaMaskF.glsl', [], ['indexed'], INDEXED),
    ('alphamask_rigged', 'shadowAlphaMaskV.glsl', 'shadowAlphaMaskF.glsl', ['HAS_SKIN'], ['indexed', 'skin'], INDEXED),
    ('alphamask_fullbright', 'shadowAlphaMaskV.glsl', 'shadowAlphaMaskF.glsl', ['IS_FULLBRIGHT', 'DEPTH_CLAMP'], ['indexed'], INDEXED),
    ('gltfblend', 'pbrShadowAlphaMaskV.glsl', 'pbrShadowAlphaBlendF.glsl', [], ['tex', 'gltf'], ''),
    ('gltfblend_rigged', 'pbrShadowAlphaMaskV.glsl', 'pbrShadowAlphaBlendF.glsl', ['HAS_SKIN'], ['tex', 'gltf', 'skin'], ''),
    ('avatar_alpha', 'avatarAlphaShadowV.glsl', 'avatarAlphaShadowF.glsl', [], ['tex', 'avskin'], ''),
]
LEGACY_ONLY = [
    ('avatar_alphamask', 'avatarAlphaShadowV.glsl', 'avatarAlphaMaskShadowF.glsl', [], ['tex', 'avskin'], ''),
    ('gltfmask', 'pbrShadowAlphaMaskV.glsl', 'pbrShadowAlphaMaskF.glsl', [], ['tex', 'gltf'], ''),
]
built = 0
for stochastic in (False, True):
    for label, vfile, ffile, defines, vstubs, lookup in CASTERS + ([] if stochastic else LEGACY_ONLY):
        build(tree_text, vfile, ffile, ([DEFINE] if stochastic else []) + defines, vstubs, lookup)
        built += 1
print('1. %d caster programs (Legacy and Stochastic) compile and link on this driver' % built, flush=True)

F = C.c_float
gentex = fn('glGenTextures', None, I, C.POINTER(U)); bindtex = fn('glBindTexture', None, U, U)
texparam = fn('glTexParameteri', None, U, U, I); teximage = fn('glTexImage2D', None, U, I, I, I, I, I, U, U, P)
genfbo = fn('glGenFramebuffers', None, I, C.POINTER(U)); bindfbo = fn('glBindFramebuffer', None, U, U)
attachtex = fn('glFramebufferTexture2D', None, U, U, U, U, I); status = fn('glCheckFramebufferStatus', U, U)
drawbuffer = fn('glDrawBuffer', None, U); readbuffer = fn('glReadBuffer', None, U)
activetex = fn('glActiveTexture', None, U); enable = fn('glEnable', None, U); disable = fn('glDisable', None, U)
depthfunc = fn('glDepthFunc', None, U); depthmask = fn('glDepthMask', None, C.c_ubyte)
viewport = fn('glViewport', None, I, I, I, I); clear = fn('glClear', None, U); cleardepth = fn('glClearDepth', None, C.c_double)
genbuf = fn('glGenBuffers', None, I, C.POINTER(U)); bindbuf = fn('glBindBuffer', None, U, U)
bufdata = fn('glBufferData', None, U, S, P, U); attribptr = fn('glVertexAttribPointer', None, U, I, U, C.c_ubyte, I, P)
enattrib = fn('glEnableVertexAttribArray', None, U); disattrib = fn('glDisableVertexAttribArray', None, U)
attribloc = fn('glGetAttribLocation', I, U, C.c_char_p); loc = fn('glGetUniformLocation', I, U, C.c_char_p)
u1i = fn('glUniform1i', None, I, I); u1f = fn('glUniform1f', None, I, F); u2f = fn('glUniform2f', None, I, F, F)
umat = fn('glUniformMatrix4fv', None, I, I, C.c_ubyte, C.POINTER(F)); useprog = fn('glUseProgram', None, U)
draw = fn('glDrawArrays', None, U, I, I); finish = fn('glFinish', None); geterror = fn('glGetError', U)
readpixels = fn('glReadPixels', None, I, I, I, I, U, U, P)
vao = U(); fn('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao)); fn('glBindVertexArray', None, U)(vao)
TEX2D = 0x0DE1; FB = 0x8D40; NEAREST = 0x2600; MIN = 0x2801; MAG = 0x2800
SIZE = 2048 if BENCH else 256


def texture(unit_index, internal, w, h, fmt, kind, data):
    t = U(); gentex(1, C.byref(t)); activetex(0x84C0 + unit_index); bindtex(TEX2D, t.value)
    for p in (MIN, MAG):
        texparam(TEX2D, p, NEAREST)
    teximage(TEX2D, 0, internal, w, h, 0, fmt, kind, data)
    activetex(0x84C0)
    return t


# the depth-only target (a shadow map)
dtex = U(); gentex(1, C.byref(dtex)); bindtex(TEX2D, dtex.value)
for p in (MIN, MAG):
    texparam(TEX2D, p, NEAREST)
teximage(TEX2D, 0, 0x81A6, SIZE, SIZE, 0, 0x1902, 0x1406, None)
fbo = U(); genfbo(1, C.byref(fbo)); bindfbo(FB, fbo.value); attachtex(FB, 0x8D00, TEX2D, dtex.value, 0)
drawbuffer(0); readbuffer(0)
assert status(FB) == 0x8CD5, 'incomplete depth target'
# the engine's table on unit 1: S + 1 = 9 mask rows (the casters read none) and the blue-noise tile in the two spare rows
bn = (ROOT / 'indra/newview/llstochasticbluenoise.h').read_text()
BLUE = [int(v) for v in re.findall(r'\b\d+\b', bn.split('BLUE_NOISE[BLUE_NOISE_SIZE * BLUE_NOISE_SIZE] = {', 1)[1].split('};')[0])]
assert len(BLUE) == 4096 and sorted(BLUE) == list(range(4096)), 'the tile holds the ranks 0 .. 4095 once each'
table = (C.c_uint * (2048 * 11))(*([0] * (2048 * 9) + BLUE))
texture(1, 0x8236, 2048, 11, 0x8D94, 0x1405, C.cast(table, P))
# the opacities on unit 0, one per map texel (RGBA32F, so the shader reads the model's float exactly); a full-map triangle
# samples texel (x, y) at fragment (x, y). Values near the floor (0.05) and near 0.1 and 0.2 (the floor after the vertex
# colour's alpha 0.5 and 0.25) are left out, so the interpolation of the vertex colour cannot move a texel across the floor.
rng = np.random.default_rng(20261118)
grid = np.concatenate([np.arange(0, 257) / 256.0, [0.03, 0.37, 0.6180339, 0.98, 0.999, 1.0]])
ALPHA = rng.choice(grid, size=(SIZE, SIZE)).astype(np.float32)
mixed = rng.random((SIZE, SIZE)).astype(np.float32)
ALPHA = np.where(rng.random((SIZE, SIZE)) < 0.5, ALPHA, mixed)
ALPHA[(np.abs(ALPHA - 0.05) < 1e-3) | (np.abs(ALPHA - 0.1) < 1e-3) | (np.abs(ALPHA - 0.2) < 1e-3)] = 0.5
rgba = np.zeros((SIZE, SIZE, 4), dtype=np.float32); rgba[..., :3] = 1.0; rgba[..., 3] = ALPHA
texture(0, 0x8814, SIZE, SIZE, 0x1908, 0x1406, rgba.ctypes.data_as(P))
IDENT = (F * 16)(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
vbo = U(); genbuf(1, C.byref(vbo)); bindbuf(0x8892, vbo.value)
viewport(0, 0, SIZE, SIZE); disable(0x0B44); enable(0x0B71); depthmask(1); cleardepth(1.0)


def use(prog, minimum_alpha, solid, st_seed, origin):
    useprog(prog)
    for a in range(4):
        disattrib(a)
    for attr, n, off in ((b'position', 3, 0), (b'diffuse_color', 4, 12), (b'texcoord0', 2, 28)):
        a = attribloc(prog, attr)
        if a >= 0:
            enattrib(a); attribptr(a, n, 0x1406, 0, 36, C.c_void_p(off))
    for name in (b'texture_matrix0', b'modelview_projection_matrix'):
        umat(loc(prog, name), 1, 0, IDENT)
    u1f(loc(prog, b'shadow_target_width'), float(SIZE)); u1f(loc(prog, b'minimum_alpha'), minimum_alpha)
    u1f(loc(prog, b'shadow_solid_alpha'), solid); u2f(loc(prog, b'shadow_tile_origin'), float(origin[0]), float(origin[1]))
    u1i(loc(prog, b'st_seed'), st_seed); u1i(loc(prog, b'tex0'), 0); u1i(loc(prog, b'stoch_masks'), 1)


def triangles(prims_before, front, vertex_alpha, z=0.0, layers=1):
    """prims_before triangles outside the view, then a triangle over the whole map (counter-clockwise when front), its
    texcoords taking texel (x, y) at fragment (x, y); layers such draws far to near"""
    verts = []
    corners = ((-1.0, -1.0, 0.0, 0.0), (3.0, -1.0, 2.0, 0.0), (-1.0, 3.0, 0.0, 2.0))
    for k in range(layers):
        lz = z if layers == 1 else 0.9 - 1.8 * k / (layers - 1)
        for _ in range(prims_before):
            for (x, y) in ((5.0, 5.0), (6.0, 5.0), (5.0, 6.0)):
                verts += [x, y, lz, 1.0, 1.0, 1.0, vertex_alpha, 0.0, 0.0]
        for (x, y, s, t) in (corners if front else (corners[0], corners[2], corners[1])):
            verts += [x, y, lz, 1.0, 1.0, 1.0, vertex_alpha, s, t]
    vb = (F * len(verts))(*verts); bufdata(0x8892, C.sizeof(vb), C.cast(vb, P), 0x88E4)
    return len(verts) // 9


def kept():
    out = (F * (SIZE * SIZE))(); readpixels(0, 0, SIZE, SIZE, 0x1902, 0x1406, out)
    return np.frombuffer(out, dtype=np.float32).reshape(SIZE, SIZE) < 1.0


# ---- the numpy model of shadow_caster_keeps (shadowStochasticCasterF.glsl) and stoch_xi24 (stochasticSampleF.glsl)
M32 = 0xffffffff
BLUE_A = np.array(BLUE, dtype=np.uint64)


def h32(v):
    x = (np.asarray(v, dtype=np.uint64) * 747796405 + 2891336453) & M32
    x = (((x >> ((x >> 28) + 4)) ^ x) * 277803737) & M32
    return (x >> 22) ^ x


def h32s(v):
    return int(h32(v & M32))


def model_rule(alpha, st_seed, prim, front, origin):
    ys, xs = np.mgrid[0:SIZE, 0:SIZE]
    cx = xs.astype(np.int64) - origin[0]; cy = ys.astype(np.int64) - origin[1]   # floor(gl_FragCoord - origin), whole origins
    bx = ((cx >> 1) & 0xffff).astype(np.uint64); by = ((cy >> 1) & 0xffff).astype(np.uint64)
    layer = h32s(h32s(((st_seed & M32) * 0x9e3779b9 + 0x7f4a7c15) & M32) ^ h32s((prim * 2 + (0 if front else 1) + 0x632be5ab) & M32))
    xi24 = ((2 * BLUE_A[(by & 63) * 64 + (bx & 63)] + 1) * 2048 + (layer >> 8)) & 0xffffff
    xi = xi24.astype(np.float32) / np.float32(16777216.0)
    pick = (h32(np.uint64(layer) ^ h32(bx | (by << 16)) ^ 0x5bd1e995) >> 8).astype(np.float32) / np.float32(16777216.0)
    a4 = np.clip(alpha, 0.0, 1.0).astype(np.float32) * np.float32(4.0)
    r = np.minimum(np.floor(a4 + xi), 4).astype(np.uint64)
    cnt = (0x14641 >> (4 * r)) & 0xf
    i = ((0xFB510 >> (4 * r)) & 0xf) + np.minimum((pick * cnt.astype(np.float32)).astype(np.uint64), cnt - 1)
    subset = (np.where(i >= 8, 0xFEDB7CA9, 0x65384210).astype(np.uint64) >> (4 * (i & 7))) & 0xf
    bit = (cx & 1).astype(np.uint64) | ((cy & 1).astype(np.uint64) << 1)
    return ((subset >> bit) & 1).astype(bool) | (alpha >= 1.0)


if BENCH:
    # K full layers of alpha 0.5 per draw, far to near (every fragment shaded), into the 2048 x 2048 depth-only map;
    # GL_TIME_ELAPSED, interleaved, medians of 40 draws (after 5 warm-up rounds)
    K = 16
    rgba[..., 3] = 0.5
    texture(0, 0x8814, SIZE, SIZE, 0x1908, 0x1406, rgba.ctypes.data_as(P))
    progs = {'legacy': build(tree_text, 'shadowAlphaMaskV.glsl', 'shadowAlphaMaskF.glsl', [], ['tex'], LOOKUP),
             'stochastic': build(tree_text, 'shadowAlphaMaskV.glsl', 'shadowAlphaMaskF.glsl', [DEFINE], ['tex'], LOOKUP)}
    solid = {'legacy': 1.0, 'stochastic': 1.0}
    if BENCH_REF:
        old = ref_text(BENCH_REF)
        pipe = subprocess.run(['git', 'show', BENCH_REF + ':indra/newview/pipeline.cpp'], cwd=ROOT, capture_output=True,
                              text=True, check=True).stdout
        solid['stochastic ' + BENCH_REF] = float(re.search(r'SHADOW_BLEND_SOLID_ALPHA = ([0-9.]+)f;', pipe).group(1))
        progs['stochastic ' + BENCH_REF] = program([unit(VS, old('shadowAlphaMaskV.glsl'), [DEFINE]), unit(VS, STUB_V['tex'], []),
                                                    unit(FS, old('shadowAlphaMaskF.glsl'), [DEFINE], LOOKUP)] +
                                                   [unit(s, old(n), []) for s, n in ((VS, 'shadowStochasticCasterV.glsl'),
                                                    (FS, 'stochasticSampleF.glsl'), (FS, 'shadowStochasticCasterF.glsl'))
                                                    if subprocess.run(['git', 'cat-file', '-e', BENCH_REF + ':' + DEFERRED + n],
                                                                      cwd=ROOT, stderr=subprocess.DEVNULL).returncode == 0])
    count = triangles(0, True, 1.0, layers=K)
    genq = fn('glGenQueries', None, I, C.POINTER(U)); beginq = fn('glBeginQuery', None, U, U); endq = fn('glEndQuery', None, U)
    getq = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))
    q = U(); genq(1, C.byref(q)); times = {n: [] for n in progs}
    depthfunc(0x0203)
    for rep in range(45):
        for name, prog in progs.items():
            use(prog, 0.0, solid[name], 12345, (0, 0))
            clear(0x100); finish()
            beginq(0x88BF, q.value); draw(0x0004, 0, count); endq(0x88BF)
            r = C.c_uint64(); getq(q.value, 0x8866, C.byref(r))
            if rep >= 5:
                times[name].append(r.value / 1e6)
    assert geterror() == 0, 'GL error'
    frags = K * SIZE * SIZE
    print('%d layers of alpha 0.5 over a %d x %d depth-only map: %.1f M caster fragments per draw' % (K, SIZE, SIZE, frags / 1e6))
    for name, v in times.items():
        med = statistics.median(v)
        print('  %-24s median %.3f ms (min %.3f, max %.3f): %.2f us per million fragments' % (name, med, min(v), max(v), med * 1e3 / (frags / 1e6)))
    sys.exit(0)

# ---- 2. blended faces: the GPU keeps the model's texels
prog = build(tree_text, 'shadowAlphaMaskV.glsl', 'shadowAlphaMaskF.glsl', [DEFINE, 'IS_FULLBRIGHT'], ['tex'], LOOKUP)
depthfunc(0x0207)   # every fragment of the one draw writes (GL_ALWAYS): what is kept is what is not discarded
cases = worst = 0
for st_seed, origin in ((12345, (0, 0)), (-7, (37, -12)), (2026, (-65535, 1)), (99, (64, 64))):
    for prim in (0, 1, 7):
        for front in (True, False):
            use(prog, 0.0, 1.0, st_seed, origin)
            count = triangles(prim, front, 1.0)
            clear(0x100); draw(0x0004, 0, count); finish()
            got, want = kept(), model_rule(ALPHA, st_seed, prim, front, origin)
            bad = int(np.count_nonzero(got != want)); worst = max(worst, bad); cases += 1
            if bad:
                print('DIFFERS: seed %d origin %s triangle %d %s: %d of %d texels' % (st_seed, origin, prim, 'front' if front else 'back', bad, SIZE * SIZE))
# the coverage follows the opacity: over the map, the share kept equals the mean opacity (each texel's own alpha)
use(prog, 0.0, 1.0, 4242, (0, 0)); count = triangles(0, True, 1.0); clear(0x100); draw(0x0004, 0, count); finish()
share, mean_alpha = float(kept().mean()), float(np.clip(ALPHA, 0, 1).mean())
print('2. blended faces: %d cases of %d texels, %s; the map keeps %.4f of the texels where the mean opacity is %.4f' % (
    cases, SIZE * SIZE, 'the GPU keeps exactly the model\'s texels' if worst == 0 else 'MISMATCHES above', share, mean_alpha), flush=True)
ok = worst == 0 and abs(share - mean_alpha) < 0.01

# ---- 3. mask faces: the creator's cutoff, the vertex colour's alpha, the stock floor, solid; never the rule
mask_cases = mask_bad = 0
for fullbright in (False, True):
    prog = build(tree_text, 'shadowAlphaMaskV.glsl', 'shadowAlphaMaskF.glsl', [DEFINE] + (['IS_FULLBRIGHT'] if fullbright else []), ['tex'], LOOKUP)
    for cutoff in (0.0, 0.03, 0.3, 0.5, 0.75):
        for vertex_alpha in (1.0, 0.5, 0.25):
            use(prog, cutoff, 0.0, 4242, (37, -12))
            count = triangles(3, True, vertex_alpha)
            clear(0x100); draw(0x0004, 0, count); finish()
            alpha = ALPHA if fullbright else ALPHA * np.float32(vertex_alpha)
            want = (ALPHA >= np.float32(cutoff)) & (alpha >= np.float32(0.05))
            bad = int(np.count_nonzero(kept() != want)); mask_cases += 1
            if bad:
                mask_bad += 1
                print('DIFFERS: mask face, fullbright %s cutoff %.2f vertex alpha %.2f: %d texels' % (fullbright, cutoff, vertex_alpha, bad))
print('3. mask faces: %d cases (cutoffs 0 to 0.75, vertex alpha 1 to 0.25, fullbright and not): %s' % (
    mask_cases, 'the map is the model\'s' if mask_bad == 0 else 'MISMATCHES above'), flush=True)
ok = ok and mask_bad == 0

# ---- 4. the GPU keeps the golden texels of tests/llstochastichistory_test.cpp (test_caster), which
# scripts/tests/gen_stochastic_goldens.py computes from the Aurora Viewer's formulas: layer s53 = casterLayer(5, 1, false)
# (draw 5, triangle 1, its back) at alpha 0.37, and casterLayer(5, 7, true) at alpha 0.6, cells from the origin
cpp = (ROOT / 'indra/newview/tests/llstochastichistory_test.cpp').read_text(encoding='utf-8')
golden = [(re.search(r'const uint16_t fixed\[16\] = \{([^}]*)\};', cpp), 16, 0.37, 1, False),
          (re.search(r'const uint8_t rows\[8\] = \{([^}]*)\};', cpp), 8, 0.6, 7, True)]
prog = build(tree_text, 'shadowAlphaMaskV.glsl', 'shadowAlphaMaskF.glsl', [DEFINE, 'IS_FULLBRIGHT'], ['tex'], LOOKUP)
golden_bad = 0
for m, n, a, prim, front in golden:
    assert m, 'the golden rows are not in the C++ test'
    want = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', m.group(1))]
    assert len(want) == n
    flat = np.zeros((SIZE, SIZE, 4), dtype=np.float32); flat[..., :3] = 1.0; flat[..., 3] = np.float32(a)
    texture(0, 0x8814, SIZE, SIZE, 0x1908, 0x1406, flat.ctypes.data_as(P))
    use(prog, 0.0, 1.0, 5, (0, 0))
    count = triangles(prim, front, 1.0)
    clear(0x100); draw(0x0004, 0, count); finish()
    got = kept()
    rows = [sum(int(got[y, x]) << x for x in range(n)) for y in range(n)]
    if rows != want:
        golden_bad += 1
        print('DIFFERS: golden rows (alpha %.2f, triangle %d %s): %s, the C++ test pins %s' % (
            a, prim, 'front' if front else 'back', [hex(v) for v in rows], [hex(v) for v in want]))
print('4. golden texels: %s' % ('the GPU keeps the C++ test\'s rows (alpha 0.37, triangle 1, back; alpha 0.6, triangle 7, front)'
                                if golden_bad == 0 else 'MISMATCHES above'), flush=True)
ok = ok and golden_bad == 0
assert geterror() == 0, 'GL error'
print('the shadow caster programs pass' if ok else 'FAILED')
sys.exit(0 if ok else 1)
