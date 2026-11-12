# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU gates of unified lighting stage U0 (projector visibility), no viewer launch.

Reuses the scenes and GL context of bench_shadow_legacy.py and runs the shipped Stochastic shaders: the
util (class1/deferred/shadowStochasticUtil.glsl) next to the method-neutral block of shadowUtil.glsl, the
deferred sun light pass, and the temporal resolve. The reference is the same files at a git ref (default
origin/vkstorm-devel, the stage's base).

Gates:
  * the sun channel is bit identical to the base, raw (frames 0 to 3, two scenes) and resolved (24 frames);
  * the projector bands are gone: a projector over a bumped (35 degree normal map) ground and wall reads
    exactly 1 on every lit pixel, where the base's shading-normal receiver plane leaves dark columns;
  * the lamp's own housing does not black out its light, and everything else casts: r_excl is the projector's
    eye-to-face stretch plus a 5 cm margin (the real projectorExclusion of lllocallight.h). A plate inside it
    (the housing, in the near-clipped stretch) shades nothing; a shade plate 0.1 to 0.5 m in front of the face,
    a thin partition 0.4 m in front of it, and an object between the lamp and a table 1 m below it all cast;
  * the sun light pass costs at most the base's + 0.02 ms (interleaved, median of 3 rounds).

Environment: LOCAL_LIGHT_BASE_REF (default origin/vkstorm-devel), ST_FRAMES (timing frames, default 40),
SHADOW_BENCH_RES (as bench_shadow_legacy.py).
"""
import math, os, re, statistics, sys
from pathlib import Path
import numpy as np
_src = Path(__file__).with_name('bench_shadow_legacy.py').read_text()
exec(compile(_src.split('# --- timing + basic checks')[0], 'bench_prefix', 'exec'))

REF = os.environ.get('LOCAL_LIGHT_BASE_REF', 'origin/vkstorm-devel')
FRAMES = int(os.environ.get('ST_FRAMES', 40))
DEEP = 'class1/deferred/'
UTIL_NEW = (root / SHADERS / DEEP / 'shadowStochasticUtil.glsl').read_text()
UTIL_OLD = git_show(REF, SHADERS + DEEP + 'shadowStochasticUtil.glsl')
NOISE = (root / SHADERS / DEEP / 'stochasticNoiseUtil.glsl').read_text()   # the core's noise (unified lighting U1); unused by a base without it
RES_NEW = (root / SHADERS / DEEP / 'shadowStochasticResolveUtilF.glsl').read_text()
RES_OLD = git_show(REF, SHADERS + DEEP + 'shadowStochasticResolveUtilF.glsl')
RESOLVE_F = (root / SHADERS / DEEP / 'shadowStochasticResolveF.glsl').read_text()
ENGINE_RESOLVE = (root / SHADERS / DEEP / 'stochasticResolveUtilF.glsl').read_text()
UTIL_COMMON = (root / UTIL_F).read_text()
TOP, W_RES = None, None


def stoch_program(util, spot=True):
    sun = (root / SUN_F).read_text()
    defines = ['SUN_SHADOW'] + (['SPOT_SHADOW'] if spot else [])
    common = ['SHADOW_COMMON_ONLY', 'SHADOW_COMMON'] + S2_DEFINES
    return program([(0x8B31, VERTEX), (0x8B30, stage(sun, [])), (0x8B30, stage(UTIL_COMMON, defines + common)),
                    (0x8B30, stage(util, defines + S2_DEFINES)), (0x8B30, STUBS_F)] + ([(0x8B30, stage(NOISE, []))] if 'stNoiseStream' in util else []))


def bind_stoch(prog, sc, frame, emitter=0.075, excl=0.075):
    sc.bind(prog, LIGHT_SIZE)
    for unit in range(2, 8):
        bindsampler(unit, depth_fetch.value)       # shadowMap0-5: plain depth, compare off
    u1i(loc(prog, b'shadow_st_frame'), frame); u1f(loc(prog, b'spot_shadow_reach'), 128.0)
    u2f(loc(prog, b'spot_emitter_radius'), emitter, emitter); u2f(loc(prog, b'spot_shadow_excl'), excl, excl)   # -1 (absent) on the base


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
FAR_BOX = [((60.0, 0.0, 60.0), (61.0, 1.0, 61.0))]

# --- the sun channel is bit identical to the base ------------------------------------------------------
print('sun channel against', REF)
p_old, p_new = stoch_program(UTIL_OLD), stoch_program(UTIL_NEW)
for label, sun, boxes in (('slanted sun, boxes', [-0.5, 1.0, 0.2], BOXES), ('low sun, boxes', [-1.0, 0.35, 0.2], BOXES)):
    sc = Scene(W, H, boxes, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], sun, SPOTS, CLIP)
    same, total = 0, 0
    for frame in (0, 1, 2, 3):
        a, b = render(p_old, sc, frame), render(p_new, sc, frame)
        same += int(np.array_equal(a[..., 0], b[..., 0]) and np.array_equal(a[..., 1], b[..., 1])); total += 1
    check(same == total, f'{label}: sun (r) and AO (g) of the raw pass equal the base bit for bit ({same}/{total} frames)', 'sun: ')
    sc.release()


class Resolve:
    """The viewer's fragment resolve with the util text given: raw + history -> history."""
    def __init__(self, sc, util):
        self.sc = sc
        self.prog = program([(0x8B31, VERTEX), (0x8B30, stage(RESOLVE_F, [])), (0x8B30, stage(util, [])), (0x8B30, stage(ENGINE_RESOLVE, []))])
        self.hist = [make_tex(0x881A, sc.W, sc.H) for _ in range(2)]; self.fbo = [make_fbo([t]) for t in self.hist]
        self.idx = 0; self.valid = False
        use(self.prog)
        set_mat(self.prog, 'stoch_reproj', [[[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]])
        u1f(loc(self.prog, b'stoch_weight'), 0.125); u3f(loc(self.prog, b'shadow_st_channel_valid'), 1.0, 1.0, 1.0)
        set_mat(self.prog, 'inv_proj', [inverse(sc.proj)])
        for unit, name in enumerate(('shadow_st_raw', 'shadow_st_history', 'depthMap')):
            u1i(loc(self.prog, name.encode()), unit)

    def run(self):
        sc = self.sc; use(self.prog)
        u1i(loc(self.prog, b'stoch_history_valid'), 1 if self.valid else 0)
        for unit, tex in enumerate((sc.light_map, self.hist[self.idx], sc.gdepth)):
            active(0x84C0 + unit); bindtex(TEX2D, tex); bindsampler(unit, 0)
        active(0x84C0)
        bindfbo(FB, self.fbo[1 - self.idx]); viewport(0, 0, sc.W, sc.H); draw(4, 0, 3)
        self.idx = 1 - self.idx; self.valid = True
        return read_rgba(self.fbo[self.idx], sc.W, sc.H)

    def release(self):
        fn('glDeleteProgram', None, U)(self.prog); delfbo(2, (U * 2)(*self.fbo)); deltex(2, (U * 2)(*self.hist))


sc = Scene(W, H, BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], SPOTS, CLIP)
r_old, r_new = Resolve(sc, RES_OLD), Resolve(sc, RES_NEW)
same = True
for frame in range(1, 25):
    render(p_new, sc, frame)
    a, b = r_old.run(), r_new.run()
    same = same and np.array_equal(a[..., 0], b[..., 0]) and np.array_equal(a[..., 1], b[..., 1])
check(same, 'the resolved sun (r) and depth (g) equal the base bit for bit over 24 frames ', 'sun: ')
r_old.release(); r_new.release(); sc.release()


# --- the projector bands: a lit bumped surface reads exactly 1 ---------------------------------------------
def lit_mask(sc, gpos, slot, min_dist, wall_z=None):
    """receivers inside the projector's frustum, away from its eye, facing it (view space position, matrix of the slot)"""
    m = np.array(sc.shadow_matrix[4 + slot], np.float64)
    p = gpos[..., :3].reshape(-1, 3).astype(np.float64)
    h = (m @ np.concatenate([p, np.ones((len(p), 1))], axis=1).T).T
    w = h[:, 3]; ok = w > 0
    uvz = h[:, :3] / np.where(ok, w, 1.0)[:, None]
    inside = ok & (np.abs(uvz[:, :2] - 0.5) < 0.47).all(axis=1)
    eye = np.array(mvec(sc.view, list(sc.spots_def[slot][0]) + [1.0])[:3], np.float64)
    far = np.linalg.norm(p - eye, axis=1) > min_dist
    mask = (inside & far & (gpos[..., 3].reshape(-1) == 1.0)).reshape(gpos.shape[:2])
    if wall_z is not None:   # the wall's lit face and the ground in front of it, not the ground behind it or the wall's top
        world = (np.linalg.inv(np.array(sc.view, np.float64)) @ np.concatenate([p, np.ones((len(p), 1))], axis=1).T).T
        mask &= ((world[:, 2] > wall_z - 0.05) & (world[:, 1] < 5.9)).reshape(gpos.shape[:2])
    return mask


print('\nprojector bands (bump 35 degrees)')
WALL = [((-8.0, 0.0, -13.0), (8.0, 6.0, -12.5))]
OFF = ([30.0, 8.0, 30.0], [30.0, 0.0, 30.0])   # the second projector, out of view


def dark_pixels(prog, boxes, spot, bump):
    """lit receivers of the slot-0 projector below 1 over 4 frames (a surface with no occluder), and how many there are"""
    sc = Scene(W, H, boxes, [4.0, 5.0, -1.0], [0.0, 0.0, -14.0], [-0.5, 1.0, 0.2], [spot, OFF], CLIP, bump=bump)
    mask = lit_mask(sc, read_rgba(sc.gfbo, W, H), 0, 2.5, -12.5 if boxes is WALL else None)
    dark = sum(int((render(prog, sc, frame, excl=0.5)[..., 2][mask] < 1.0).sum()) for frame in (1, 2, 3, 4))
    sc.release()
    return dark, int(mask.sum())


# facing receivers: every lit pixel reads exactly 1 on the bumped surface
for label, boxes, spot in (('bumped ground', FAR_BOX, ([0.0, 6.0, -10.0], [0.0, 0.0, -10.0])),
                           ('bumped wall', WALL, ([0.0, 3.0, -6.0], [0.0, 2.5, -12.5]))):
    dark, n = dark_pixels(p_new, boxes, spot, 35.0)
    check(n > 20000 and dark == 0, f'{label}: {n} lit receivers, {dark} below 1', 'bands: ')
# grazing receivers (the projector a short way off a wall it looks along): the shading normal's bump moved the receiver
# plane and the normal offset, which striped the wall; the geometric normal does not feel the bump
for label, spot in (('wall, projector 0.5 m off it', ([-6.0, 3.0, -12.0], [6.0, 3.0, -12.45])),
                    ('wall, projector 1 m off it', ([-6.0, 3.0, -11.5], [6.0, 3.0, -12.45]))):
    res = {name: (dark_pixels(prog, WALL, spot, 35.0)[0], dark_pixels(prog, WALL, spot, 0.0)[0]) for name, prog in (('base', p_old), ('new', p_new))}
    print(f'  info {label}: lit pixels below 1, bumped / flat: base {res["base"][0]} / {res["base"][1]}, new {res["new"][0]} / {res["new"][1]}')
    check(res['base'][0] > res['base'][1], f'{label}: the base shows the bump ({res["base"][0] - res["base"][1]} more pixels below 1 than flat)', 'bands: ')
    check(res['new'][0] <= res['new'][1], f'{label}: the bump no longer changes the result ({res["new"][0]} bumped, {res["new"][1]} flat)', 'bands: ')

# --- what the exclusion zone is, and is not --------------------------------------------------------------
print('\nexclusion zone (the real projectorExclusion: apex-to-face + margin)')
FOV, FACE = math.radians(60.0), 0.5   # the bench's projectors are 60 degrees; a 0.5 m prim face
MARGIN = float(re.search(r'PROJECTOR_MARGIN = ([0-9.]+)f', (root / 'indra/newview/lllocallight.h').read_text()).group(1))
APEX = 0.5 * FACE / math.tan(0.5 * FOV)
EXCL = APEX + MARGIN
print(f'  info apex-to-face {APEX:.3f} m, margin {MARGIN} m, r_excl {EXCL:.3f} m')
EYE = [0.0, 6.0, -10.0]


def ground_light(boxes, eye, points=None, min_dist=2.5, wy_max=0.02):
    """mean projector (slot 0) light of the ground in view of the lamp, base and new; or at points (world)"""
    sc = Scene(W, H, boxes, [4.0, 5.0, -1.0], [0.0, 0.0, -10.0], [-0.5, 1.0, 0.2], [(eye, [eye[0], 0.0, eye[2]]), SPOTS[1]], CLIP)
    gpos = read_rgba(sc.gfbo, W, H)
    out = {}
    if points is None:
        ground = lit_mask(sc, gpos, 0, min_dist)
        wy = (np.linalg.inv(np.array(sc.view, np.float64)) @ np.concatenate([gpos[..., :3].reshape(-1, 3), np.ones((W * H, 1))], axis=1).T).T[:, 1].reshape(H, W)
        ground &= np.abs(wy) < wy_max
    for name, prog in (('base', p_old), ('new', p_new)):
        img = np.mean([render(prog, sc, f, excl=EXCL)[..., 2] for f in (1, 2, 3, 4)], axis=0)
        out[name] = float(img[ground].mean()) if points is None else [float(img[sc.project(pt)[1], sc.project(pt)[0]]) for pt in points]
    n = int(ground.sum()) if points is None else len(points)
    sc.release()
    return out, n


def plate(y, half=0.8):
    return [((-half, y - 0.02, EYE[2] - half), (half, y + 0.02, EYE[2] + half))]


# the lamp's housing, inside the eye-to-face stretch (where the real map holds nothing): no black-out
res, n = ground_light(plate(EYE[1] - 0.2), EYE)
check(n > 10000 and res['base'] < 0.05 and res['new'] > 0.999, f'a housing 0.2 m under the eye (the face is {APEX:.2f} m): base {res["base"]:.3f}, new {res["new"]:.3f}', 'excl: ')
# linked parts and others in front of the face cast
for label, d in (('a shade plate 0.1 m in front of the face', APEX + 0.1), ('a shade plate 0.5 m in front of the face', APEX + 0.5), ('a thin partition 0.4 m in front of the face', APEX + 0.4)):
    res, n = ground_light(plate(EYE[1] - d), EYE)
    check(n > 10000 and res['new'] < 0.05, f'{label}: it casts (mean light {res["new"]:.3f}; base {res["base"]:.3f})', 'excl: ')
# a table 1 m below the lamp: an object 0.55 m below the lamp shades the table under it, and not the table beside it
LAMP = [0.0, 1.0, -10.0]
box = [((-0.15, 0.40, -10.15), (0.15, 0.45, -9.85))]   # 0.55 m under the eye (past r_excl), a 0.55 m wide shadow on the table 1 m below
res, _ = ground_light(box, LAMP, points=[(0.0, 0.0, -10.0), (0.0, 0.0, -9.65), (0.45, 0.0, -10.0)])
check(res['new'][0] < 0.05 and res['new'][1] > 0.999 and res['new'][2] > 0.999,
      f'a table 1 m below the lamp: under an object between them {res["new"][0]:.3f}, beside it {res["new"][1]:.3f} and {res["new"][2]:.3f} (base {res["base"][0]:.3f})', 'excl: ')

# --- the sun pass costs at most the base's + 0.02 ms ----------------------------------------------------------
print('\nsun light pass cost')
for (Wc, Hc) in RESOLUTIONS:
    sc = Scene(Wc, Hc, BOXES, [0.0, 8.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], SPOTS, CLIP)
    rounds = {'base': [], 'new': []}
    progs = {'base': p_old, 'new': p_new}
    for _ in range(3):
        samples = {'base': [], 'new': []}
        for f in range(WARMUP + FRAMES):
            for name in ('base', 'new'):
                bind_stoch(progs[name], sc, f + 1)
                beginq(TIME_ELAPSED, queries[0]); draw(4, 0, 3); endq(TIME_ELAPSED)
                v = C.c_uint64(); qresult(queries[0], QUERY_RESULT, C.byref(v))
                if f >= WARMUP: samples[name].append(v.value / 1e6)
                sc.unbind()
        for name in rounds: rounds[name].append(statistics.median(samples[name]))
    finish(); assert geterror() == 0
    base, new = statistics.median(rounds['base']), statistics.median(rounds['new'])
    check(new <= base + 0.02, f'{Wc}x{Hc}: sun pass (sun + 2 projectors) base {base:.4f} ms, new {new:.4f} ms ({new - base:+.4f}; limit +0.02)', 'cost: ')
    sc.release()

fn('glDeleteProgram', None, U)(p_old); fn('glDeleteProgram', None, U)(p_new)
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
if failures:
    print('\nFAILED:\n' + '\n'.join(failures)); sys.exit(1)
print('\nall gates passed')
