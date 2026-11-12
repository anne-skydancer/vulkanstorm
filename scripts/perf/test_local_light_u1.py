# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU gates of unified lighting stage U1 (the light/shadow split), no viewer launch.

Reuses the scenes and GL context of bench_shadow_legacy.py and runs the shipped Stochastic shaders: the sun pass
(class2/deferred/sunLightF.glsl with the STOCHASTIC_SHADOW_SUN permutation), the shadow util and the core's noise
(class1/deferred/shadowStochasticUtil.glsl, stochasticNoiseUtil.glsl), the sun resolve, the shadow pool's local light ratio
pass (class2/deferred/localLightRatioF.glsl) with the light pool's weights (class2/deferred/localLightWeightUtil.glsl), and
the ratio's resolve (class1/deferred/localLightRatioResolveF.glsl). The reference is the same files at a git ref (pinned to
a SHA, the devel that holds stage U0).

Gates:
  * the sun channel (r) and AO (g) are bit identical to the base, raw (frames 0 to 3, two scenes) and resolved (24 frames,
    the history now RG16F);
  * with one projector in the table the ratio equals the base's projector channel within one fp16 step on every lit pixel;
    with two projectors and a point light of other colours, in tiles of 2 or 3 lights, it is the weight-averaged visibility
    per colour channel; with 32 lights in every tile as well; with a projector texture's average luminance in its weight;
  * a snapshot: the 8 additive passes (frames 1 to 8) read with the scale 1/8 match the base's 8-pass average of the
    projector channel;
  * the image: sum_i C_i R equals the base's sum_i C_i V_i (C_i = the light's diffuse contribution) except where a projector
    carries a texture (the stated difference, printed with and without the average-luminance weight);
  * where nothing occludes, R is exactly 1; the resolve converges, and a projector that changed takes this frame's value
    (the raw's alpha) while a history with a different depth is rejected;
  * the stage costs at most the base + 0.1 ms at 1080p with no mapped projector and with two (1440p and 4K are printed), and
    the cost of a many-light tile is reported.

Environment: LOCAL_LIGHT_BASE_REF (default the pinned SHA), ST_FRAMES (timing frames, default 40), SHADOW_BENCH_RES (as
bench_shadow_legacy.py; this script defaults to 1920x1080,2560x1440,3840x2160), ST_FULL_RECT=1 (INFO ONLY: the cost with
the projectors' frustums covering the whole screen, the scissor's worst case; it asserts nothing).
"""
import math, os, re, statistics, sys
from pathlib import Path
import numpy as np
os.environ.setdefault('SHADOW_BENCH_RES', '1920x1080,2560x1440,3840x2160')
_src = Path(__file__).with_name('bench_shadow_legacy.py').read_text()
exec(compile(_src.split('# --- timing + basic checks')[0], 'bench_prefix', 'exec'))

REF = os.environ.get('LOCAL_LIGHT_BASE_REF', '74fb9b53c5')   # the devel merge of stage U0
FRAMES = int(os.environ.get('ST_FRAMES', 40))
DEEP = SHADERS + 'class1/deferred/'
CLASS2 = SHADERS + 'class2/deferred/'
UTIL_NEW = (root / DEEP / 'shadowStochasticUtil.glsl').read_text()
UTIL_OLD = git_show(REF, DEEP + 'shadowStochasticUtil.glsl')
NOISE = (root / DEEP / 'stochasticNoiseUtil.glsl').read_text()
SUN_NEW = (root / SUN_F).read_text()
SUN_OLD = git_show(REF, SUN_F)
RES_NEW = (root / DEEP / 'shadowStochasticResolveUtilF.glsl').read_text()
RES_OLD = git_show(REF, DEEP + 'shadowStochasticResolveUtilF.glsl')
RESOLVE_F = (root / DEEP / 'shadowStochasticResolveF.glsl').read_text()
ENGINE_RESOLVE = (root / DEEP / 'stochasticResolveUtilF.glsl').read_text()
RATIO_F = (root / CLASS2 / 'localLightRatioF.glsl').read_text()
WEIGHT_F = (root / CLASS2 / 'localLightWeightUtil.glsl').read_text()
RATIO_RESOLVE_F = (root / DEEP / 'localLightRatioResolveF.glsl').read_text()
UTIL_COMMON = (root / UTIL_F).read_text()
STUBS_RATIO = '''#version 430 core
float calcLegacyDistanceAttenuation(float distance, float falloff)   // deferredUtil.glsl
{
    float dist_atten = 1.0 - clamp((distance + falloff)/(1.0 + falloff), 0.0, 1.0);
    dist_atten *= dist_atten;
    dist_atten *= 2.0;
    return dist_atten;
}
'''
texsub = fn('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)
_scissor = fn('glScissor', None, I, I, I, I)
_enable, _disable, _clearcolor, _clear = fn('glEnable', None, U), fn('glDisable', None, U), fn('glClearColor', None, F, F, F, F), fn('glClear', None, U)


def stoch_program(sun, util, new):
    defines = ['SUN_SHADOW', 'SPOT_SHADOW']
    common = ['SHADOW_COMMON_ONLY', 'SHADOW_COMMON'] + S2_DEFINES
    stages = [(0x8B31, VERTEX), (0x8B30, stage(sun, ['STOCHASTIC_SHADOW_SUN'] if new else [])), (0x8B30, stage(UTIL_COMMON, defines + common)),
              (0x8B30, stage(util, defines + S2_DEFINES)), (0x8B30, STUBS_F)]
    if new:
        stages.append((0x8B30, stage(NOISE, [])))
    return program(stages)


def ratio_program():
    defines = ['SUN_SHADOW', 'SPOT_SHADOW']
    common = ['SHADOW_COMMON_ONLY', 'SHADOW_COMMON'] + S2_DEFINES
    return program([(0x8B31, VERTEX), (0x8B30, stage(RATIO_F, ['STOCHASTIC_SHADOW_SUN'])), (0x8B30, stage(UTIL_COMMON, defines + common)),
                    (0x8B30, stage(UTIL_NEW, defines + S2_DEFINES)), (0x8B30, stage(NOISE, [])), (0x8B30, stage(WEIGHT_F, [])), (0x8B30, STUBS_F), (0x8B30, STUBS_RATIO)])


def bind_stoch(prog, sc, frame, emitter=0.075, excl=0.075):
    sc.bind(prog, LIGHT_SIZE)
    for unit in range(2, 8):
        bindsampler(unit, depth_fetch.value)
    u1i(loc(prog, b'shadow_st_frame'), frame); u1f(loc(prog, b'spot_shadow_reach'), 128.0)
    u2f(loc(prog, b'spot_emitter_radius'), emitter, emitter); u2f(loc(prog, b'spot_shadow_excl'), excl, excl)


def read_rgba(fbo, w, h):
    bindfbo(FB, fbo); readbuffer(COLOR0)
    buf = np.empty((h, w, 4), np.float32); readpixels(0, 0, w, h, 0x1908, 0x1406, buf.ctypes.data_as(C.c_void_p))
    return buf


def render(prog, sc, frame, **kw):
    bind_stoch(prog, sc, frame, **kw); draw(4, 0, 3); finish()
    img = read_rgba(sc.lfbo, sc.W, sc.H); sc.unbind(); return img


queries = (U * 1)(); genq(1, queries)
W, H = RESOLUTIONS[0]
CLIP = (10.0, 25.0, 60.0, 150.0)
BOXES = [((-5.0, 1.0, -15.0), (-3.0, 1.5, -13.0)), ((0.0, 3.0, -20.0), (2.0, 3.5, -18.0)), ((4.0, 0.0, -12.0), (6.0, 2.0, -10.0))]
SPOTS = [([-4.0, 6.0, -10.0], [-4.0, 0.0, -14.0]), ([1.0, 7.0, -15.0], [1.5, 0.0, -19.0])]
OFF = ([30.0, 8.0, 30.0], [30.0, 0.0, 30.0])
FAR_BOX = [((60.0, 0.0, 60.0), (61.0, 1.0, 61.0))]
p_old, p_new, p_ratio = stoch_program(SUN_OLD, UTIL_OLD, False), stoch_program(SUN_NEW, UTIL_NEW, True), ratio_program()


# --- the light table and tile list, as the viewer builds them ---------------------------------------------
class Light:
    def __init__(self, centre, size, colour, falloff, origin=None, matrix=None, slot=-1, gobo=1.0):
        self.centre, self.size, self.colour, self.falloff = centre, size, colour, falloff
        self.origin = origin if origin is not None else centre
        self.matrix, self.slot, self.gobo = matrix, slot, gobo

    def row(self):
        r = [*self.centre, self.size, *self.colour, self.falloff, *self.origin, 1.0 if self.matrix is not None else 0.0, self.gobo, 0.0, 0.0, 0.0]
        m = self.matrix if self.matrix is not None else [[0.0] * 4] * 4
        r += [m[i][j] for j in range(4) for i in range(4)]   # texels 4 to 7: the columns
        return r


TABLE_ROWS = 64


def upload_table(lights):
    rows = np.zeros((TABLE_ROWS, 8 * 4), np.float32)
    for i, light in enumerate(lights):
        rows[i] = light.row()
    tex = make_tex(0x8814, 8, TABLE_ROWS)
    bindtex(TEX2D, tex); texsub(TEX2D, 0, 0, 0, 8, TABLE_ROWS, 0x1908, 0x1406, rows.ctypes.data_as(C.c_void_p))
    return tex


def tile_data(lights_per_tile, W, H):
    tx, ty = (W + 31) // 32, (H + 31) // 32
    data = np.zeros((ty, tx * 33), np.uint8)
    for y in range(ty):
        for x in range(tx):
            lst = lights_per_tile(x, y)
            data[y, x * 33] = len(lst)
            data[y, x * 33 + 1:x * 33 + 1 + len(lst)] = lst
    tex = make_tex(0x8232, tx * 33, ty)
    bindtex(TEX2D, tex); fn('glPixelStorei', None, U, I)(0x0CF5, 1)
    texsub(TEX2D, 0, 0, 0, tx * 33, ty, 0x8D94, 0x1401, data.ctypes.data_as(C.c_void_p))
    return tex


def set_ratio_uniforms(sc, lights, table, tiles, frame, fade=(0.0, 0.0), changed=(0.0, 0.0)):
    bind_stoch(p_ratio, sc, frame)
    for unit, tex, name in ((8, table, b'local_light_table'), (9, tiles, b'local_light_tiles')):
        active(0x84C0 + unit); bindtex(TEX2D, tex); bindsampler(unit, 0); u1i(loc(p_ratio, name), unit)
    active(0x84C0)
    slot = [-1.0, -1.0]
    for i, l in enumerate(lights):
        if l.slot >= 0:
            slot[l.slot] = float(i)
    u2f(loc(p_ratio, b'spot_slot_light'), *slot); u2f(loc(p_ratio, b'spot_slot_changed'), *changed); u2f(loc(p_ratio, b'spot_shadow_fade'), *fade)


def run_ratio(sc, lights, frame, per_tile=None, fade=(0.0, 0.0), changed=(0.0, 0.0)):
    table = upload_table(lights)
    tiles = tile_data(per_tile or (lambda x, y: list(range(len(lights)))), sc.W, sc.H)
    set_ratio_uniforms(sc, lights, table, tiles, frame, fade, changed)
    draw(4, 0, 3); finish()
    img = read_rgba(sc.lfbo, sc.W, sc.H)
    sc.unbind()
    deltex(2, (U * 2)(table, tiles))
    return img


_blendfunc = fn('glBlendFunc', None, U, U)


def run_ratio_sum(sc, lights, frames=range(1, 9)):
    """a snapshot: the passes of `frames` added onto a target cleared to 0, read with the scale 1/len(frames)"""
    table = upload_table(lights)
    tiles = tile_data(lambda x, y: list(range(len(lights))), sc.W, sc.H)
    bindfbo(FB, sc.lfbo); _clearcolor(0.0, 0.0, 0.0, 0.0); _clear(0x4000)
    _enable(0x0BE2); _blendfunc(1, 1)
    for frame in frames:
        set_ratio_uniforms(sc, lights, table, tiles, frame)
        draw(4, 0, 3)
    _disable(0x0BE2); finish()
    img = read_rgba(sc.lfbo, sc.W, sc.H) / float(len(frames))
    sc.unbind()
    deltex(2, (U * 2)(table, tiles))
    return img


def projector_light(sc, slot, colour, gobo=1.0):
    eye = mvec(sc.view, list(sc.spots_def[slot][0]) + [1.0])[:3]
    return Light(eye, 14.0, colour, 0.0, matrix=sc.shadow_matrix[4 + slot], slot=slot, gobo=gobo)


def weights(sc, light, gpos, gnorm):
    """localLightW in numpy: colour * attenuation * n.l (* the projector texture's average luminance) inside the light's radius
    (and a projector's frustum), 0 elsewhere"""
    p = gpos[..., :3].astype(np.float64); n = gnorm[..., :3].astype(np.float64)
    n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-9)
    lv = np.array(light.centre, np.float64) - p
    dist = np.linalg.norm(lv, axis=-1)
    ok = dist < light.size
    if light.matrix is not None:
        m = np.array(light.matrix, np.float64)
        h = np.concatenate([p, np.ones(p.shape[:-1] + (1,))], axis=-1) @ m.T
        w = h[..., 3]; zz = h[..., 2]
        uv = h[..., :2] / np.where(w == 0, 1, w)[..., None]
        ok &= (zz >= 0) & (uv[..., 0] > 0) & (uv[..., 0] < 1) & (uv[..., 1] > 0) & (uv[..., 1] < 1)
        lv = np.array(light.origin, np.float64) - p
    x = 1.0 - np.clip((dist / light.size + light.falloff) / (1.0 + light.falloff), 0, 1)
    atten = 2.0 * x * x
    nl = np.maximum((n * lv / np.maximum(np.linalg.norm(lv, axis=-1, keepdims=True), 1e-9)).sum(-1), 0.0)
    tex = light.gobo if light.matrix is not None else 1.0
    return np.where(ok, atten * nl * tex, 0.0)[..., None] * np.array(light.colour, np.float64)


# --- the sun channel is bit identical to the base --------------------------------------------------------
print('sun channel against', REF)
for label, sun in (('slanted sun', [-0.5, 1.0, 0.2]), ('low sun', [-1.0, 0.35, 0.2])):
    sc = Scene(W, H, BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], sun, SPOTS, CLIP)
    same = 0
    for frame in (0, 1, 2, 3):
        a, b = render(p_old, sc, frame), render(p_new, sc, frame)
        same += int(np.array_equal(a[..., 0], b[..., 0]) and np.array_equal(a[..., 1], b[..., 1]))
    check(same == 4, f'{label}: sun (r) and AO (g) of the raw pass equal the base bit for bit ({same}/4 frames)', 'sun: ')
    pb = render(p_new, sc, 1)
    check(bool((pb[..., 2] == 1).all() and (pb[..., 3] == 1).all()), f'{label}: the sun pass writes 1 in the projector channels', 'sun: ')
    sc.release()


class Resolve:
    """The viewer's fragment resolve with the sources given: raw + history -> history, in the given history format."""
    def __init__(self, sc, util, fmt):
        self.sc = sc
        self.prog = program([(0x8B31, VERTEX), (0x8B30, stage(RESOLVE_F, [])), (0x8B30, stage(util, [])), (0x8B30, stage(ENGINE_RESOLVE, []))])
        self.hist = [make_tex(fmt, sc.W, sc.H) for _ in range(2)]; self.fbo = [make_fbo([t]) for t in self.hist]
        self.idx = 0; self.valid = False
        use(self.prog)
        set_mat(self.prog, 'stoch_reproj', [[[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]])
        u1f(loc(self.prog, b'stoch_weight'), 0.125); u3f(loc(self.prog, b'shadow_st_channel_valid'), 1.0, 1.0, 1.0); u1f(loc(self.prog, b'shadow_st_sun_valid'), 1.0)   # the base's name and the new one
        set_mat(self.prog, 'inv_proj', [inverse(sc.proj)])
        for unit, name in enumerate(('shadow_st_raw', 'shadow_st_history', 'depthMap')):
            u1i(loc(self.prog, name.encode()), unit)

    def run(self, raw=None):
        sc = self.sc; use(self.prog)
        u1i(loc(self.prog, b'stoch_history_valid'), 1 if self.valid else 0)
        for unit, tex in enumerate((raw or sc.light_map, self.hist[self.idx], sc.gdepth)):
            active(0x84C0 + unit); bindtex(TEX2D, tex); bindsampler(unit, 0)
        active(0x84C0)
        bindfbo(FB, self.fbo[1 - self.idx]); viewport(0, 0, sc.W, sc.H); draw(4, 0, 3)
        self.idx = 1 - self.idx; self.valid = True
        return self.hist[1 - self.idx], self.hist[self.idx]   # previous, latest

    def read(self):
        return read_rgba(self.fbo[self.idx], self.sc.W, self.sc.H)

    def release(self):
        fn('glDeleteProgram', None, U)(self.prog); delfbo(2, (U * 2)(*self.fbo)); deltex(2, (U * 2)(*self.hist))


class RatioResolve:
    """The ratio's resolve: raw ratio + history (RGB10_A2) -> history, validated against the sun history's previous frame."""
    def __init__(self, sc):
        self.sc = sc
        self.prog = program([(0x8B31, VERTEX), (0x8B30, stage(RATIO_RESOLVE_F, ['STOCH_RESOLVE'])), (0x8B30, stage(ENGINE_RESOLVE, []))])
        self.hist = [make_tex(0x8059, sc.W, sc.H) for _ in range(2)]; self.fbo = [make_fbo([t]) for t in self.hist]
        self.idx = 0; self.valid = False
        use(self.prog)
        set_mat(self.prog, 'stoch_reproj', [[[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]])
        u1f(loc(self.prog, b'stoch_weight'), 0.125); u1f(loc(self.prog, b'shadow_st_scale'), 1.0)
        set_mat(self.prog, 'inv_proj', [inverse(sc.proj)])
        for unit, name in enumerate(('shadow_st_raw', 'shadow_st_history', 'shadow_st_resolved', 'depthMap')):
            u1i(loc(self.prog, name.encode()), unit)

    def run(self, raw, sun_prev):
        sc = self.sc; use(self.prog)
        u1i(loc(self.prog, b'stoch_history_valid'), 1 if self.valid else 0)
        for unit, tex in enumerate((raw, self.hist[self.idx], sun_prev, sc.gdepth)):
            active(0x84C0 + unit); bindtex(TEX2D, tex); bindsampler(unit, 0)
        active(0x84C0)
        bindfbo(FB, self.fbo[1 - self.idx]); viewport(0, 0, sc.W, sc.H); draw(4, 0, 3)
        self.idx = 1 - self.idx; self.valid = True

    def read(self):
        return read_rgba(self.fbo[self.idx], self.sc.W, self.sc.H)

    def release(self):
        fn('glDeleteProgram', None, U)(self.prog); delfbo(2, (U * 2)(*self.fbo)); deltex(2, (U * 2)(*self.hist))


sc = Scene(W, H, BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], SPOTS, CLIP)
r_old, r_new = Resolve(sc, RES_OLD, 0x881A), Resolve(sc, RES_NEW, 0x822F)
same = True
for frame in range(1, 25):
    render(p_old, sc, frame); r_old.run()
    render(p_new, sc, frame); r_new.run()
    a, b = r_old.read(), r_new.read()
    same = same and np.array_equal(a[..., 0], b[..., 0]) and np.array_equal(a[..., 1], b[..., 1])
check(same, 'the resolved sun (r) and depth (g) equal the base bit for bit over 24 frames, in an RG16F history', 'sun: ')
r_old.release(); r_new.release(); sc.release()


# --- the ratio against the base's projector channels ----------------------------------------------------
print('\nlocal light ratio against the base projector channels')
sc = Scene(W, H, BOXES, [4.0, 5.0, -1.0], [0.0, 0.0, -14.0], [-0.5, 1.0, 0.2], SPOTS, CLIP)
gpos, gnorm = read_rgba(sc.gfbo, W, H), None
bindfbo(FB, sc.gfbo); readbuffer(COLOR0 + 1)
gnorm = np.empty((H, W, 4), np.float32); readpixels(0, 0, W, H, 0x1908, 0x1406, gnorm.ctypes.data_as(C.c_void_p))
valid_px = gpos[..., 3] == 1.0
L0, L1 = projector_light(sc, 0, (1.0, 0.3, 0.3)), projector_light(sc, 1, (0.3, 0.3, 1.0))
LP = Light(mvec(sc.view, [0.0, 3.0, -12.0, 1.0])[:3], 14.0, (0.8, 0.8, 0.8), 0.0)
w0 = weights(sc, L0, gpos, gnorm)
lit0 = (w0.sum(-1) > 1e-3) & valid_px
check(int(lit0.sum()) > 20000, f'projector 0 lights {int(lit0.sum())} pixels', 'ratio: ')

# one projector in the table: R is the base's visibility (w V / w in fp32, then stored in fp16: at most one fp16 step off
# the eighths the 8 rays give; measured 4.9e-4 at most)
exact, worst1, total = 0, 0.0, 0
for frame in (1, 2, 3, 4):
    base = render(p_old, sc, frame)
    rr = run_ratio(sc, [L0], frame)
    for c in range(3):
        d1 = np.abs(rr[..., c][lit0] - base[..., 2][lit0])
        exact += int((d1 == 0).sum()); worst1 = max(worst1, float(d1.max())); total += d1.size
check(worst1 < 1.5e-3 and exact > 0.999 * total, f'one projector: R equals the base projector channel on {total} lit values ({total - exact} off by a fp16 step, max {worst1:.5f} < 1.5e-3)', 'ratio: ')

# nothing occludes: R is exactly 1
sc_free = Scene(W, H, FAR_BOX, [4.0, 5.0, -1.0], [0.0, 0.0, -14.0], [-0.5, 1.0, 0.2], SPOTS, CLIP)
free = run_ratio(sc_free, [projector_light(sc_free, 0, (1, 1, 1)), projector_light(sc_free, 1, (1, 1, 1))], 1)
check(bool((free[..., :3] == 1.0).all()), 'a scene with nothing to occlude: R is exactly 1 on every pixel and channel', 'ratio: ')
sc_free.release()


def expected_ratio(ws, vis, member):
    """sum w V / sum w over the lights each pixel's tile lists, 1 where nothing lights it"""
    num = sum(ws[i] * (member[i][..., None] * vis[i][..., None]) for i in range(len(ws)))
    den = sum(ws[i] * member[i][..., None] for i in range(len(ws)))
    return np.where(den > 0, num / np.where(den > 0, den, 1), 1.0)


# two projectors and a point light, other colours, tile lists of 2 or 3 lights: R is the weighted visibility
lights = [L0, L1, LP]
pattern = lambda x, y: [0, 1, 2] if (x + y) % 3 == 0 else ([0, 2] if (x + y) % 3 == 1 else [1, 2])
yy, xx = np.mgrid[0:H, 0:W]
mod = (xx // 32 + yy // 32) % 3
members = [mod != 2, mod != 1, np.ones((H, W), bool)]
ws = [weights(sc, l, gpos, gnorm) for l in lights]
errs, worst = [], 0.0
for frame in (1, 2, 3):
    base = render(p_old, sc, frame)
    vis = [base[..., 2], base[..., 3], np.ones((H, W), np.float32)]
    rr = run_ratio(sc, lights, frame, per_tile=pattern)
    d = np.abs(rr[..., :3] - expected_ratio(ws, vis, members))[valid_px]
    worst = max(worst, float(d.max())); errs.append(float(d.mean()))
check(worst < 1.5e-3, f'two projectors and a point light, tile lists of 2 or 3 lights: R is the weighted visibility per channel (max error {worst:.5f} < 1.5e-3, mean {max(errs):.6f})', 'ratio: ')

# many lights: 2 projectors and 30 point lights of other colours in every tile (32 per tile, the tile cap)
many = [L0, L1] + [Light(mvec(sc.view, [(i % 6) * 2.0 - 5.0, 1.0 + (i // 6) * 0.8, -9.0 - (i % 5) * 2.0, 1.0])[:3], 5.0 + (i % 4),
                         ((i * 37) % 10 / 10.0 + 0.1, (i * 53) % 10 / 10.0 + 0.1, (i * 71) % 10 / 10.0 + 0.1), 0.0) for i in range(30)]
ws_many = [weights(sc, l, gpos, gnorm) for l in many]
base = render(p_old, sc, 2)
vis = [base[..., 2], base[..., 3]] + [np.ones((H, W), np.float32)] * 30
rr = run_ratio(sc, many, 2)
d = np.abs(rr[..., :3] - expected_ratio(ws_many, vis, [np.ones((H, W), bool)] * 32))[valid_px]
check(float(d.max()) < 1.5e-3, f'32 lights in every tile (2 projectors, 30 point lights): R is the weighted visibility (max error {float(d.max()):.5f} < 1.5e-3)', 'ratio: ')

# a projector texture, as its average luminance in the weight: the weights of the numpy model and the shader agree
Lg = projector_light(sc, 0, (1.0, 0.3, 0.3), gobo=0.5)
wg = weights(sc, Lg, gpos, gnorm)
base = render(p_old, sc, 2)
rr = run_ratio(sc, [Lg, L1, LP], 2)
d = np.abs(rr[..., :3] - expected_ratio([wg, ws[1], ws[2]], [base[..., 2], base[..., 3], np.ones((H, W), np.float32)], [np.ones((H, W), bool)] * 3))[valid_px]
check(float(d.max()) < 1.5e-3, f'projector 0 with an average luminance of 0.5: R is the weighted visibility with it (max error {float(d.max()):.5f} < 1.5e-3)', 'ratio: ')

# a snapshot: 8 additive passes read with the scale 1/8 are the base's 8-pass average of the projector channel
base8 = np.mean([render(p_old, sc, f)[..., 2] for f in range(1, 9)], axis=0)
sums = run_ratio_sum(sc, [L0])
d8 = np.abs(sums[..., 0] - base8)[lit0]
check(float(d8.max()) < 3.0e-3 and float(d8.mean()) < 3.0e-4, f'snapshot: 8 additive passes / 8 equal the base 8-pass average of the projector channel on {int(lit0.sum())} lit pixels (max {float(d8.max()):.5f} < 3e-3, mean {float(d8.mean()):.6f} < 3e-4)', 'snapshot: ')
shadowed = lit0 & (base8 < 0.95)
check(int(shadowed.sum()) > 500, f'and the average has {int(shadowed.sum())} pixels in a projector shadow to compare', 'snapshot: ')

# the image: sum_i C_i R against the base's sum_i C_i V_i
base = render(p_old, sc, 2)
rr = run_ratio(sc, lights, 2)
vis = [base[..., 2][..., None], base[..., 3][..., None], 1.0]
img0 = sum(w * v for w, v in zip(ws, vis))
img1 = sum(w for w in ws) * rr[..., :3]
d = np.abs(img1 - img0)[valid_px] * 255.0
peak = float(sum(ws)[valid_px].max()) * 255
print(f'  info image, no texture: mean {d.mean():.4f}/255, max {d.max():.3f}/255 (light up to {peak:.0f}/255)')
check(d.mean() < 0.001 and d.max() < 0.4, f'image without projector textures equals the base: mean {d.mean():.4f}/255 < 0.001, max {d.max():.3f}/255 < 0.4 (fp16 ratio)', 'image: ')
# a textured projector: a 50 % / 100 % checker the weights know only as its average (0.75)
m0 = np.array(L0.matrix, np.float64)
h = np.concatenate([gpos[..., :3].astype(np.float64), np.ones((H, W, 1))], axis=-1) @ m0.T
uv = h[..., :2] / np.where(h[..., 3] == 0, 1, h[..., 3])[..., None]
gobo = np.where(((np.floor(uv[..., 0] * 8) + np.floor(uv[..., 1] * 8)) % 2) == 0, 1.0, 0.5)[..., None]
img0g = ws[0] * gobo * vis[0] + ws[1] * vis[1] + ws[2]
for label, avg in (('untextured weights', 1.0), ('average luminance 0.75 in the weights', 0.75)):
    Lt = projector_light(sc, 0, (1.0, 0.3, 0.3), gobo=avg)
    rrt = run_ratio(sc, [Lt, L1, LP], 2)
    wt = [weights(sc, Lt, gpos, gnorm), ws[1], ws[2]]
    img1g = sum(wt) * rrt[..., :3]
    dg = np.abs(img1g - img0g)[valid_px] * 255.0
    print(f'  info image, projector 0 textured (checker 1 / 0.5), {label}: mean {dg.mean():.4f}/255, max {dg.max():.2f}/255, '
          f'{int((dg > 2).sum())} of {int(valid_px.sum())} pixels differ by more than 2/255 (the texture is not in the weights until the projector texture array, P3)')
sc.release()

# --- the ratio's resolve: converges; a changed projector takes this frame's value; a history of another depth is rejected ---
print('\nratio resolve')
sc = Scene(W, H, BOXES, [4.0, 5.0, -1.0], [0.0, 0.0, -14.0], [-0.5, 1.0, 0.2], SPOTS, CLIP)
sun_res = Resolve(sc, RES_NEW, 0x822F); rat = RatioResolve(sc)
L0, L1 = projector_light(sc, 0, (1.0, 0.3, 0.3)), projector_light(sc, 1, (0.3, 0.3, 1.0))
raws, outs = [], []
for frame in range(1, 33):
    render(p_new, sc, frame)
    sun_res.run()
    prev_sun = sun_res.hist[sun_res.idx ^ 1]
    r = run_ratio(sc, [L0, L1], frame)
    bindfbo(FB, sc.lfbo)   # the ratio pass drew into the scene's light map
    rat.run(sc.light_map, prev_sun)
    if frame >= 24:
        raws.append(r[..., :3]); outs.append(rat.read()[..., :3])
sel = (np.abs(np.mean(raws, axis=0) - 1.0).sum(-1) > 0.02) & valid_px   # pixels the shadows touch
var_raw = float(np.mean(np.var(np.stack(raws), axis=0)[sel]))
var_out = float(np.mean(np.var(np.stack(outs), axis=0)[sel]))
mean_diff = float(np.abs(np.mean(outs, axis=0) - np.mean(raws, axis=0))[sel].mean())
check(int(sel.sum()) > 2000 and var_out < 0.1 * var_raw, f'the resolved ratio is steadier than the raw one on {int(sel.sum())} shadowed pixels (frame-to-frame variance {var_out:.5f} < 0.1 x {var_raw:.5f})', 'resolve: ')
check(mean_diff < 0.003, f'and keeps its mean (mean difference to the raw mean {mean_diff:.4f} < 0.003)', 'resolve: ')
rat.release()

# synthetic raw: columns alternating 0 and 1 (a clamp box of the full range), a history of 0.5 everywhere
depth_lin = np.zeros((H, W, 2), np.float32)
depth_lin[..., 0] = 0.0
depth_lin[..., 1] = -gpos[..., 2]       # the linear view depth of the sun history's g channel
depth_good = make_tex(0x822F, W, H)
bindtex(TEX2D, depth_good); texsub(TEX2D, 0, 0, 0, W, H, 0x8227, 0x1406, depth_lin.ctypes.data_as(C.c_void_p))
depth_bad = make_tex(0x822F, W, H)     # zeros: every history texel's depth disagrees
flat = np.zeros((H, W, 4), np.float32); flat[..., :3] = 0.5
raw_tex = make_tex(0x881A, W, H)


def raw_upload(alpha):
    data = np.zeros((H, W, 4), np.float32)
    data[..., :3] = (np.arange(W) % 2).astype(np.float32)[None, :, None]
    data[..., 3] = alpha
    bindtex(TEX2D, raw_tex); texsub(TEX2D, 0, 0, 0, W, H, 0x1908, 0x1406, data.ctypes.data_as(C.c_void_p))
    return data


def resolve_case(alpha, depth):
    r = RatioResolve(sc)
    bindtex(TEX2D, raw_tex); texsub(TEX2D, 0, 0, 0, W, H, 0x1908, 0x1406, flat.ctypes.data_as(C.c_void_p))
    for _ in range(6):
        r.run(raw_tex, depth_good)                  # a history of 0.5
    data = raw_upload(alpha)
    r.run(raw_tex, depth)
    out = r.read()[..., :3]
    r.release()
    return out, data[..., :3]


interior = valid_px & (-gpos[..., 2] < 40.0)   # the depth test is 2 % of the depth: the far ground, at the depth buffer's resolution, is excluded
interior = interior.copy(); interior[:4] = False; interior[-4:] = False; interior[:, :4] = False; interior[:, -4:] = False
out, cur = resolve_case(1.0, depth_good)
dc = float(np.abs(out - cur)[interior].max())
check(dc < 2.5e-3, f'a changed projector (alpha 1) takes this frame\'s value: max difference to the raw {dc:.5f} < 2.5e-3', 'resolve: ')
out, cur = resolve_case(0.0, depth_good)
want = 0.5 + (cur - 0.5) / 8.0
dd = np.abs(out - want)[interior]
frac = float((dd < 2.5e-3).mean())
check(frac > 0.98, f'an unchanged one (alpha 0) blends 1/8 of this frame into the history: {frac * 100:.2f} % of {int(interior.sum())} pixels within 2.5e-3 of the blend (the rest are depth edges, where the history is rejected)', 'resolve: ')
out, cur = resolve_case(0.0, depth_bad)
dr = float(np.abs(out - cur)[interior].max())
check(dr < 2.5e-3, f'a history whose depth disagrees is rejected (the sun history\'s g): the result is this frame\'s, max difference {dr:.5f} < 2.5e-3', 'resolve: ')
deltex(3, (U * 3)(depth_good, depth_bad, raw_tex))
sun_res.release(); sc.release()


# --- cost ---------------------------------------------------------------------------------------------
def frustum_rect(scc, slots=(0, 1)):
    """the screen rectangle (x, y, w, h) the mapped projectors' frustums (with the sun pass's 0.05 margin) can light: the
    bounding box of the corners of each frustum through the camera; the whole screen where a corner reaches the camera plane"""
    x0 = y0 = 1.0e9; x1 = y1 = -1.0e9
    if os.environ.get('ST_FULL_RECT'):   # the worst case: the projectors' frustums cover the screen
        return 0, 0, scc.W, scc.H
    for slot in slots:
        inv = inverse(scc.shadow_matrix[4 + slot])
        for cx in (-0.05, 1.05):
            for cy in (-0.05, 1.05):
                for cz in (-0.05, 1.05):
                    v = mvec(inv, [cx, cy, cz, 1.0])
                    c = mvec(scc.proj, [v[0] / v[3], v[1] / v[3], v[2] / v[3], 1.0])
                    if c[3] <= 1.0e-4:
                        return 0, 0, scc.W, scc.H
                    sx, sy = (c[0] / c[3] * 0.5 + 0.5) * scc.W, (c[1] / c[3] * 0.5 + 0.5) * scc.H
                    x0, x1, y0, y1 = min(x0, sx), max(x1, sx), min(y0, sy), max(y1, sy)
    x0, y0 = max(int(math.floor(x0)) - 1, 0), max(int(math.floor(y0)) - 1, 0)
    x1, y1 = min(int(math.ceil(x1)) + 1, scc.W), min(int(math.ceil(y1)) + 1, scc.H)
    return x0, y0, max(x1 - x0, 0), max(y1 - y0, 0)


def timed_pass(draw_once):
    beginq(TIME_ELAPSED, queries[0]); draw_once(); endq(TIME_ELAPSED)
    v = C.c_uint64(); qresult(queries[0], QUERY_RESULT, C.byref(v)); return v.value / 1e6


FULL_RECT = bool(os.environ.get('ST_FULL_RECT'))
print('\nframe cost: sun pass + sun resolve (+ ratio pass + ratio resolve)' + ('   [ST_FULL_RECT: information only, nothing is asserted]' if FULL_RECT else ''))
CASES = (('no mapped projector (projectors out of view)', [OFF, OFF], 3), ('two mapped projectors', SPOTS, 3), ('two mapped projectors and 30 point lights, the worst case: every pixel of the screen runs the loop of 32 lights', SPOTS, 32))
for (Wc, Hc) in RESOLUTIONS:
    for label, spots, n_lights in CASES:
        scc = Scene(Wc, Hc, BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], spots, CLIP)
        ro, rn, rr_ = Resolve(scc, RES_OLD, 0x881A), Resolve(scc, RES_NEW, 0x822F), RatioResolve(scc)
        lt = [projector_light(scc, 0, (1, 1, 1)), projector_light(scc, 1, (1, 1, 1)),
              Light(mvec(scc.view, [0.0, 3.0, -12.0, 1.0])[:3], 14.0, (0.8, 0.8, 0.8), 0.0)]
        lt += [Light(mvec(scc.view, [(i % 6) * 2.0 - 5.0, 1.0 + (i // 6) * 0.8, -9.0 - (i % 5) * 2.0, 1.0])[:3], 5.0 + (i % 4), (0.6, 0.5, 0.4), 0.0) for i in range(n_lights - 3)]
        table = upload_table(lt); tiles = tile_data(lambda x, y: list(range(len(lt))), scc.W, scc.H)
        rounds = {'base': [], 'new': []}
        for _ in range(3):
            samples = {'base': [], 'new': []}
            for f in range(WARMUP + FRAMES):
                ms = 0.0
                bind_stoch(p_old, scc, f + 1); ms += timed_pass(lambda: draw(4, 0, 3)); scc.unbind()
                use(ro.prog); ms += timed_pass(lambda: ro.run()); s_base = ms
                ms = 0.0
                bind_stoch(p_new, scc, f + 1); ms += timed_pass(lambda: draw(4, 0, 3)); scc.unbind()
                ms += timed_pass(lambda: rn.run())
                if spots is SPOTS:
                    rect = (0, 0, scc.W, scc.H) if n_lights == 32 else frustum_rect(scc)
                    bindfbo(FB, scc.lfbo); _clearcolor(1.0, 1.0, 1.0, 0.0); ms += timed_pass(lambda: _clear(0x4000))   # the raw: R = 1 outside
                    set_ratio_uniforms(scc, lt, table, tiles, f + 1, fade=(-0.5, -0.5) if n_lights == 32 else (0.0, 0.0))   # a fade below 0 takes every pixel into the loop
                    _enable(0x0C11); _scissor(*rect)
                    ms += timed_pass(lambda: draw(4, 0, 3)); scc.unbind()
                    _disable(0x0C11)
                    bindfbo(FB, rr_.fbo[rr_.idx ^ 1]); ms += timed_pass(lambda: _clear(0x4000))   # the ratio history: 1 outside
                    _enable(0x0C11); _scissor(*rect)
                    ms += timed_pass(lambda: rr_.run(scc.light_map, rn.hist[rn.idx ^ 1]))
                    _disable(0x0C11)
                if f >= WARMUP:
                    samples['base'].append(s_base); samples['new'].append(ms)
            for k in rounds: rounds[k].append(statistics.median(samples[k]))
        finish(); assert geterror() == 0
        base, new = statistics.median(rounds['base']), statistics.median(rounds['new'])
        msg = f'{Wc}x{Hc}, {label}: base {base:.4f} ms, new {new:.4f} ms ({new - base:+.4f})'
        # the survey's gate: at most the base + 0.1 ms at 1080p (the first resolution); the other resolutions and the many-light case are reported
        gated = (Wc, Hc) == (1920, 1080) and n_lights == 3 and not FULL_RECT
        if gated:
            check(new <= base + 0.1, msg + ' (gate: base + 0.1 ms)', 'cost: ')
        else:
            print('  info ' + msg)
        deltex(2, (U * 2)(table, tiles))
        ro.release(); rn.release(); rr_.release(); scc.release()

fn('glDeleteProgram', None, U)(p_old); fn('glDeleteProgram', None, U)(p_new); fn('glDeleteProgram', None, U)(p_ratio)
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
if failures:
    print('\nFAILED:\n' + '\n'.join(failures)); sys.exit(1)
print('\nall gates passed')
