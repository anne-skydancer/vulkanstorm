# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU check and benchmark of stage R1 (real-light shadows): the sun march reach and the world-anchored seeds.

Runs the shipped shaders (class1/deferred/shadowStochasticUtil.glsl, stochasticNoiseUtil.glsl) through
bench_shadow_stochastic.py's machinery.

Part 1, the sun march reach. The shipped reach is the cascade's (4, 8, 16 and 32 m). The bench also measures the candidates for
a reach that is one value for every cascade (the cull guard's footprint over the disc's angular radius), against the shipped one:
  uniform       the same step count over the longer march;
  uniform, full the steps follow from the whole reach (at most 8);
  uniform, 4    the steps follow from the whole reach, at most 4.
For sun scales 0.25, 1 and 4 (L = scale x 0.00925; 1 is the default sky) and blockers (thin plates) 1, 2, 4 and 8 m over the
receiver, near the camera (cascade 0): the converged 10-90 % width of the penumbra, and the same with the frame fixed at 0
(a forward program, no history: the profile of the fixed rays averaged along the edge); a 3 cm pole at 4 m: the occlusion
integrated across the shadow (physically the pole's width, whatever the light). A candidate passes when no blocker is narrower
than the shipped one (by more than 0.5 cm, one profile step) and its near width is as close to 0.85 of the physical width as the
shipped one. A 30 m blocker lies beyond the cascade's cull and is not measured. The cost of each, at the default light and at scale 4.
Part 2, the seeds: a forward receiver seen from two cameras draws the same numbers at a point of the world, far from the origin
too; at 4, 20 and 50 m a camera move of a fraction of a pixel keeps the numbers of most pixels (the cell follows the footprint),
where 4 mm cells would not; the numbers are uniform in [0, 1).
Environment: SHADOW_BENCH_RES (as bench_shadow_legacy.py), ST_FRAMES.
"""
import math, os, statistics, sys
from pathlib import Path
import numpy as np
_here = Path(__file__).resolve().parent
_s = (_here / 'bench_shadow_stochastic.py').read_text()
_MARK = "print('stochastic: the util on its own')"
assert _MARK in _s, 'bench_shadow_stochastic.py changed: update the split marker'
__file__ = str(_here / 'bench_shadow_stochastic.py')       # the prefix reads its siblings next to its own file
exec(compile(_s.split(_MARK)[0], 'bench_st_prefix', 'exec'))
SHIPPED = UTIL_ST
REACH = ('    float t_max = min(reach, SHADOW_FOOTPRINT_M / half_l);\n'
         '    int steps = clamp(int(ceil(t_max * half_l / texel_m)), 2, ST_STEPS_MAX);')
assert REACH in SHIPPED
UNIFORM = ('    float t_max = SHADOW_FOOTPRINT_M / half_l;\n'
           '    int steps = clamp(int(ceil(min(reach, t_max) * half_l / texel_m)), 2, ST_STEPS_MAX);')
FULL = ('    float t_max = SHADOW_FOOTPRINT_M / half_l;\n'
        '    int steps = clamp(int(ceil(t_max * half_l / texel_m)), 2, ST_STEPS_MAX);')
VARS = {'shipped': SHIPPED, 'uniform': SHIPPED.replace(REACH, UNIFORM), 'uniform, full': SHIPPED.replace(REACH, FULL),
        'uniform, 4': SHIPPED.replace(REACH, FULL).replace('#define ST_STEPS_MAX 8', '#define ST_STEPS_MAX 4')}
for n, u in VARS.items():
    assert n == 'shipped' or u != SHIPPED, n
progs = {}
for n, u in VARS.items():
    UTIL_ST = u
    progs[n] = stoch_program(False)

W, H = 1920, 1080
SCALES = [0.25, 1.0, 4.0]; HEIGHTS = [1, 2, 4, 8]; PLATE = 0.05
XR, ZR = (-1.5, -0.3), (0.0, 2.5)
SKY = [([30, 8, 30], [30, 0, 30]), ([31, 8, 30], [31, 0, 30])]


def profile(img, sc, edge, span=0.6, n=301, nx=16):
    xs = [XR[0] + 0.2 + (XR[1] - XR[0] - 0.4) * k / (nx - 1) for k in range(nx)]
    zs = [edge - span + 2 * span / (n - 1) * i for i in range(n)]
    out = []
    for gz in zs:
        v = [img[sc.project((gx, 0.0, gz))[1], sc.project((gx, 0.0, gz))[0]] for gx in xs]
        out.append(sum(v) / len(v))
    return zs, out


def width10_90(zs, prof):
    try:
        return next(zs[i] for i, p in enumerate(prof) if p >= 0.9) - next(zs[i] for i, p in enumerate(prof) if p >= 0.1)
    except StopIteration:
        return float('nan')


def render(prog, sc, pl, frames):
    mean = None
    for f in range(1, frames + 1):
        bind_stoch(prog, sc, pl, f); draw(4, 0, 3)
        img = read_rgba(sc.lfbo, W, H)[:, :, 0].astype(np.float64); mean = img if mean is None else mean + img
    bind_stoch(prog, sc, pl, 0); draw(4, 0, 3)
    f0 = read_rgba(sc.lfbo, W, H)[:, :, 0].astype(np.float64)
    sc.unbind()
    return mean / frames, f0


# --- part 1: penumbra ---------------------------------------------------------------------------
res = {}          # (variant, height or 'pole', scale) -> (converged width, frame 0 width) or the pole's integrated occlusion
for hb in HEIGHTS + ['pole']:
    if hb == 'pole':
        box = ((XR[0], 4.0, 1.0), (XR[1], 4.03, 1.03)); edge = 1.03
    else:
        box = ((XR[0], hb, ZR[0]), (XR[1], hb + PLATE, ZR[1])); edge = ZR[1]
    sc = Scene(W, H, [box], [0.0, 2.0, 6.0], [0.0, 0.0, -10.0], [0.0, 1.0, 0.0], SKY, (6.0, 16.0, 54.0, 128.0))
    for s in SCALES:
        pl = 0.00925 * s
        for n, prog in progs.items():
            mean, f0 = render(prog, sc, pl, 64)
            zs, p = profile(mean, sc, edge)
            if hb == 'pole':
                res[(n, hb, s)] = sum(1.0 - v for v in p) * (zs[1] - zs[0])
                check(p[0] > 0.98 and p[-1] > 0.98, f'{n}, pole, scale {s}: lit on both sides ({p[0]:.3f}/{p[-1]:.3f})', 'penumbra: ')
            else:
                zs0, p0 = profile(f0, sc, edge)
                res[(n, hb, s)] = (width10_90(zs, p), width10_90(zs0, p0))
                if n == 'shipped':
                    check(p[0] < 0.02 and p[-1] > 0.98, f'shipped, h {hb}, scale {s}: umbra 0 and lit 1 at the ends ({p[0]:.3f}/{p[-1]:.3f})', 'penumbra: ')
    sc.release()
print('\nnear penumbra, 10-90 % width in cm: converged | frame 0 (physical x 0.85 in the first column; the profile step is 0.4 cm)')
print(f'{"blocker/scale":<14}{"phys*0.85":>10}' + ''.join(f'{n:>18}' for n in VARS))
for hb in HEIGHTS:
    for s in SCALES:
        phys = 100 * 0.85 * DISC_10_90 * hb * 0.00925 * s
        print(f'{f"{hb} m, {s}":<14}{phys:>10.1f}' + ''.join(f'{100 * res[(n, hb, s)][0]:>10.1f} |{100 * res[(n, hb, s)][1]:>5.1f}' for n in VARS))
print('pole 3 cm at 4 m, integrated occlusion in cm (3.0 expected): ')
for s in SCALES:
    print(f'  scale {s}: ' + ', '.join(f'{n} {100 * res[(n, "pole", s)]:.2f}' for n in VARS))
for s in SCALES:
    p0 = res[('shipped', 'pole', s)]
    check(0.8 * 0.03 <= p0 <= 1.25 * 0.03, f'shipped, pole, scale {s}: integrated occlusion {100 * p0:.2f} cm is the pole\'s 3 cm', 'penumbra: ')
print('\ncandidate against shipped: no blocker narrower (by more than 0.5 cm, converged or frame 0), near width as close to 0.85 x physical')
for n in list(VARS)[1:]:
    bad = []
    for hb in HEIGHTS:
        for s in SCALES:
            phys85 = 0.85 * DISC_10_90 * hb * 0.00925 * s
            for k in (0, 1):
                a, b = res[(n, hb, s)][k], res[('shipped', hb, s)][k]
                if a < b - 0.005 or abs(a - phys85) > abs(b - phys85) + 0.005:
                    bad.append(f'{hb} m/{s}' + ('/f0' if k else ''))
    print(f'  {n:<14} {"PASS" if not bad else "FAIL at " + ", ".join(sorted(set(bad)))}')

# --- cost ---------------------------------------------------------------------------------------
BOXES = [((-5.0, 1.0, -15.0), (-3.0, 1.5, -13.0)), ((0.0, 3.0, -20.0), (2.0, 3.5, -18.0)), ((4.0, 0.0, -12.0), (6.0, 2.0, -10.0)),
         ((-1.0, 6.0, -9.0), (1.0, 6.3, -7.0)), ((-8.0, 12.0, -30.0), (-2.0, 12.5, -24.0))]
SPOTS = [([-4.0, 6.0, -10.0], [-4.0, 0.0, -14.0]), ([1.0, 7.0, -15.0], [1.5, 0.0, -19.0])]
print('\ncost of the sun pass, median ms (difference to shipped), default light and scale 4')
for (CW, CH) in RESOLUTIONS:
    sc = Scene(CW, CH, BOXES, [0.0, 3.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], SPOTS, (2.6, 16.1, 54.1, 128.0))
    for s in (1.0, 4.0):
        rounds = {n: [] for n in VARS}
        for rep in range(5):
            for n, p in progs.items():
                bind_stoch(p, sc, LIGHT_SIZE * s, 1)
                rounds[n].append(timed(lambda f: (u1i(loc(p, b'shadow_st_frame'), f + 1), draw(4, 0, 3))))
                sc.unbind()
        med = {n: statistics.median(rounds[n]) for n in VARS}
        print(f'{CW}x{CH} scale {s}: shipped {med["shipped"]:.3f}' + ''.join(f', {n} {med[n] - med["shipped"]:+.3f}' for n in list(VARS)[1:]), flush=True)
    sc.release()
for p in progs.values():
    fn('glDeleteProgram', None, U)(p)

# --- part 2: world-anchored seeds ---------------------------------------------------------------
UTIL_ST = SHIPPED
TEST_MAIN = '''
uniform vec3 pts[512]; uniform int xoff;
layout(location = 0) out vec4 o;
vec2 stNoise(int stream, vec3 pos);
void main()
{
    int i = int(gl_FragCoord.x) - xoff + 64 * int(gl_FragCoord.y);
    if (i < 0 || i >= 512) { o = vec4(-1.0); return; }
    o = vec4(stNoise(0, pts[i]), stNoise(2, pts[i]));
}
'''
sc = Scene(64 + 16, 8, [((0.0, 0.0, 0.0), (0.1, 0.1, 0.1))], [0.0, 3.0, 4.0], [0.0, 0.0, -16.0], [0.0, 1.0, 0.0], SPOTS, (2.6, 16.1, 54.1, 128.0))
defines = ['SUN_SHADOW']; common = ['SHADOW_COMMON_ONLY', 'SHADOW_COMMON'] + S2_DEFINES
prog = program([(0x8B31, VERTEX), (0x8B30, stage(TEST_MAIN, [])), (0x8B30, stage(UTIL_COMMON, defines + common)),
                (0x8B30, stage(UTIL_ST, defines + S2_DEFINES)), (0x8B30, stage(NOISE_ST, [])), (0x8B30, STUBS_F)])
L = np.array([0.35, 0.8, 0.45]); L /= np.linalg.norm(L)       # the light's direction in the world
PIXEL = 2.0 * math.tan(math.radians(30.0)) / 1080.0   # the world size of a pixel at 1 m depth: 60 degrees, 1080 rows (2 / (P[1][1] x height))


def basis(l):
    t1 = np.cross(l, [1.0, 0.0, 0.0] if abs(l[0]) < 0.9 else [0.0, 1.0, 0.0]); t1 /= np.linalg.norm(t1)
    return t1, np.cross(l, t1)


def rot(axis, deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return {0: np.array([[1, 0, 0], [0, c, -s], [0, s, c]]), 1: np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])}[axis]


def noise_of(P, R, t, seeds, xoff, px):
    """The two numbers of each view-space point of P under the modelview [R | t]; the seeds as LLShadowCascadeFit::worldSeedPlane."""
    use(prog)
    for i in range(512): u3f(loc(prog, b'pts[%d]' % i), *P[i])
    u1i(loc(prog, b'xoff'), xoff); u1i(loc(prog, b'shadow_st_frame'), 0)
    if seeds:
        C = -R.T @ t; t1, t2 = basis(L)
        u4f(loc(prog, b'shadow_seed_u'), *(R @ t1), float(t1 @ C)); u4f(loc(prog, b'shadow_seed_v'), *(R @ t2), float(t2 @ C))
        u1f(loc(prog, b'shadow_seed_px'), px)
    else:
        u4f(loc(prog, b'shadow_seed_u'), 0, 0, 0, 0); u4f(loc(prog, b'shadow_seed_v'), 0, 0, 0, 0); u1f(loc(prog, b'shadow_seed_px'), 0.0)
    bindfbo(FB, sc.lfbo); viewport(0, 0, sc.W, sc.H); draw(4, 0, 3)
    img = read_rgba(sc.lfbo, sc.W, sc.H)
    return np.array([img[i // 64, xoff + i % 64] for i in range(512)])


rng = np.random.default_rng(7)
# two cameras that look at the points from 10 to 13 m (one octave of the seed cell), offset and turned against each other
CAM_OFFSETS = [np.array([0.0, 0.0, 11.0]), np.array([-2.0, -1.0, 12.5])]


def look_at(c, target):
    fwd = (target - c) / np.linalg.norm(target - c)
    right = np.cross(fwd, [0.0, 1.0, 0.0]); right /= np.linalg.norm(right)
    return np.stack([right, np.cross(right, fwd), -fwd])      # the rows of the modelview's rotation; t = -R C


print('\nseeds: the same 512 points of the world, seen from two cameras (turned, moved), frame 0, at 3 distances from the origin')
for origin in ([300.0, 40.0, -120.0], [4090.0, 200.0, -3900.0], [300.0 + 256.0, 40.0, -120.0 + 256.0]):
    origin = np.array(origin)
    world = origin + rng.uniform(-1.5, 1.5, (512, 3))
    for label, seeds in (('world seeds', True), ('pixel seeds (control)', False)):
        outs = []
        for off, xoff in zip(CAM_OFFSETS, (0, 9)):
            c = origin + off; R = look_at(c, origin); t = -R @ c
            outs.append(noise_of(world @ R.T + t, R, t, seeds, xoff, PIXEL))
        a, b = outs
        same = float(np.mean(np.all(np.abs(a - b) < 1e-4, axis=1)))
        if seeds:
            check(same >= 0.90, f'{label} at {origin.tolist()}: {100 * same:.1f} % of the points draw the same numbers from both cameras (>= 90 %; the rest sit on a cell edge or an octave)', 'seeds: ')
            m, sd = a.mean(axis=0), a.std(axis=0)
            check(all(0.40 < v < 0.60 for v in m) and all(0.22 < v < 0.36 for v in sd) and a.min() >= 0.0 and a.max() < 1.0,
                  f'{label}: uniform in [0, 1): mean {np.round(m, 2).tolist()}, std {np.round(sd, 2).tolist()}', 'seeds: ')
            check(abs(np.corrcoef(a[:, 0], a[:, 2])[0, 1]) < 0.1, f'{label}: the sun and local light streams are decorrelated', 'seeds: ')
        else:
            check(same < 0.1, f'{label} at {origin.tolist()} slide with the screen: {100 * same:.1f} % of the points keep their numbers', 'seeds: ')

print('\nseeds at distance: a grid of pixel centres at depth d, the camera moved by 0.3 and 0.2 pixel (identity camera): pixels keeping their numbers')
for d in (4.0, 20.0, 50.0):
    fp = d * PIXEL
    gx, gy = np.meshgrid(np.arange(64) - 32.0, np.arange(8) - 4.0)
    P = np.stack([gx.ravel() * fp, gy.ravel() * fp, -d * np.ones(512)], axis=1) + np.array([200.0, 30.0, 0.0])
    P2 = P + np.array([0.3 * fp, 0.2 * fp, 0.0])
    keep = {}
    for label, px in (('cell follows the footprint', PIXEL), ('4 mm cells', 1.0e-9)):
        a = noise_of(P, np.eye(3), np.zeros(3), True, 0, px); b = noise_of(P2, np.eye(3), np.zeros(3), True, 0, px)
        keep[label] = float(np.mean(np.all(np.abs(a - b) < 1e-4, axis=1)))
    print(f'  {d:4.0f} m (pixel {100 * fp:.2f} cm): ' + ', '.join(f'{k} {100 * v:.0f} %' for k, v in keep.items()))
    check(keep['cell follows the footprint'] >= 0.45, f'at {d:.0f} m the numbers of {100 * keep["cell follows the footprint"]:.0f} % of the pixels survive a 0.3 pixel move (>= 45 %)', 'seeds: ')
fn('glDeleteProgram', None, U)(prog); sc.release()
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
if failures:
    print('\nFAILED:\n' + '\n'.join(failures)); sys.exit(1)
