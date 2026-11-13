# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GPU check and benchmark of stage R1 (real-light shadows): the sun march reach and the world-anchored seeds.

Runs the shipped shaders (class1/deferred/shadowStochasticUtil.glsl, stochasticNoiseUtil.glsl) through
bench_shadow_stochastic.py's machinery. The "before" variant is the same util with the previous reach (4, 8, 16 and 32 m by
cascade) put back, so every number compares the stage with the code it replaces.

Checks:
  * penumbra: the converged 10-90 % width of an 8 m high blocker, near the camera (cascade 0) and far from it, before and
    after. The reach is a property of the light: near the camera the width is the physical disc's (the quadratic node spacing
    gives 0.85 of it) and no narrower than before; far from the camera it is the pixel footprint's, the same as before;
  * cost: the sun pass, before and after, at each resolution (SHADOW_BENCH_RES);
  * seeds: a forward receiver seen from two cameras draws the same two numbers (the sun and the local light streams) at a
    point of the world, whatever the camera does; with the pixel seeds (the deferred sun pass) it does not. The numbers are
    uniform in [0, 1).
Environment: SHADOW_BENCH_RES (as bench_shadow_legacy.py), ST_FRAMES, PEN_L (the light size of the penumbra scene).
"""
import math, os, statistics, sys
from pathlib import Path
import numpy as np
_s = Path(__file__).with_name('bench_shadow_stochastic.py').read_text()
exec(compile(_s.split("print('stochastic: the util on its own')")[0], 'bench_st_prefix', 'exec'))
NEW_UTIL = UTIL_ST
NEW_REACH = ('    float t_max = SHADOW_FOOTPRINT_M / half_l;\n'
             '    int steps = clamp(int(ceil(min(reach, t_max) * half_l / texel_m)), 2, ST_STEPS_MAX);')
OLD_REACH = ('    float t_max = min(reach, SHADOW_FOOTPRINT_M / half_l);\n'
             '    int steps = clamp(int(ceil(t_max * half_l / texel_m)), 2, ST_STEPS_MAX);')
assert NEW_REACH in NEW_UTIL
VARS = {'before': NEW_UTIL.replace(NEW_REACH, OLD_REACH), 'after': NEW_UTIL}

# --- penumbra near and far ---------------------------------------------------------------------
PL = float(os.environ.get('PEN_L', 0.037)); HB = 8.0; T = 0.05
CASES = [('near', (-1.5, -0.3), (0.0, 2.5)), ('far', (-1.5, -0.3), (-24.0, -20.0))]
boxes = [((x[0], HB, z[0]), (x[1], HB + T, z[1])) for _, x, z in CASES]
W, H = 1920, 1080
sc = Scene(W, H, boxes, [0.0, 2.0, 6.0], [0.0, 0.0, -10.0], [0.0, 1.0, 0.0], [([30, 8, 30], [30, 0, 30]), ([31, 8, 30], [31, 0, 30])], (6.0, 16.0, 54.0, 128.0))
print(f'penumbra: L = {PL} (sun_scale {PL / 0.00925:.1f}), blocker {HB} m up; physical disc 10-90 % width {100 * DISC_10_90 * HB * PL:.1f} cm; '
      'cascade texels cm ' + ', '.join(f'{100 * t:.2f}' for t in cascade_texels(sc)))
phys = DISC_10_90 * HB * PL
width = {}
for name, util in VARS.items():
    UTIL_ST = util; prog = stoch_program(False)
    mean = None
    for f in range(1, 129):
        bind_stoch(prog, sc, PL, f); draw(4, 0, 3)
        img = read_rgba(sc.lfbo, W, H)[:, :, 0].astype(np.float64); mean = img if mean is None else mean + img
    sc.unbind(); mean /= 128
    row = f'  {name:<7}'
    for cname, xr, zr in CASES:
        edge = zr[1]; xs = [xr[0] + 0.2 + (xr[1] - xr[0] - 0.4) * k / 15 for k in range(16)]
        zs = [edge - 0.6 + 0.004 * i for i in range(301)]
        prof = []
        for gz in zs:
            v = []
            for gx in xs:
                x, y = sc.project((gx, 0.0, gz))
                if 0 <= x < W and 0 <= y < H: v.append(mean[y, x])
            prof.append(sum(v) / max(1, len(v)))
        lo = next(zs[i] for i, p in enumerate(prof) if p >= 0.1); hi = next(zs[i] for i, p in enumerate(prof) if p >= 0.9)
        width[(name, cname)] = hi - lo
        row += f' {cname}: {100 * (hi - lo):5.1f} cm (ends {prof[0]:.2f}/{prof[-1]:.2f})'
        check(prof[0] < 0.02 and prof[-1] > 0.98, f'{name}, {cname}: umbra 0 and lit 1 at the ends ({prof[0]:.3f}/{prof[-1]:.3f})', 'penumbra: ')
    print(row, flush=True)
    fn('glDeleteProgram', None, U)(prog)
sc.release()
nearw, farw = width[('after', 'near')], width[('after', 'far')]
check(0.80 <= nearw / phys <= 0.90, f'near the camera the width is {100 * nearw:.1f} cm, {nearw / phys:.2f} of the physical {100 * phys:.1f} cm (the quadratic nodes give about 0.85)', 'penumbra: ')
check(nearw >= width[('before', 'near')] - 0.005, f'near the camera it is no narrower than before ({100 * width[("before", "near")]:.1f} cm)', 'penumbra: ')
check(abs(farw - width[('before', 'far')]) <= 0.01, f'far from the camera it is the pixel footprint, as before ({100 * farw:.1f} cm against {100 * width[("before", "far")]:.1f} cm)', 'penumbra: ')

# --- cost ---------------------------------------------------------------------------------------
BOXES = [((-5.0, 1.0, -15.0), (-3.0, 1.5, -13.0)), ((0.0, 3.0, -20.0), (2.0, 3.5, -18.0)), ((4.0, 0.0, -12.0), (6.0, 2.0, -10.0)),
         ((-1.0, 6.0, -9.0), (1.0, 6.3, -7.0)), ((-8.0, 12.0, -30.0), (-2.0, 12.5, -24.0))]
SPOTS = [([-4.0, 6.0, -10.0], [-4.0, 0.0, -14.0]), ([1.0, 7.0, -15.0], [1.5, 0.0, -19.0])]
print('\ncost, the sun pass, median ms')
for (W, H) in RESOLUTIONS:
    sc = Scene(W, H, BOXES, [0.0, 3.0, 4.0], [0.0, 0.0, -16.0], [-0.5, 1.0, 0.2], SPOTS, (2.6, 16.1, 54.1, 128.0))
    progs = {}
    for name, util in VARS.items():
        UTIL_ST = util; progs[name] = stoch_program(False)
    rounds = {n: [] for n in VARS}
    for rep in range(5):
        for name in VARS:
            p = progs[name]; bind_stoch(p, sc, LIGHT_SIZE, 1)
            rounds[name].append(timed(lambda f: (u1i(loc(p, b'shadow_st_frame'), f + 1), draw(4, 0, 3))))
            sc.unbind()
    med = {n: statistics.median(rounds[n]) for n in VARS}
    print(f'{W}x{H}: before {med["before"]:.3f}, after {med["after"]:.3f}: {med["after"] - med["before"]:+.3f} ms', flush=True)
    for p in progs.values(): fn('glDeleteProgram', None, U)(p)
    sc.release()

# --- world-anchored seeds -----------------------------------------------------------------------
UTIL_ST = NEW_UTIL
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
rng = np.random.default_rng(7)
world = np.array([300.0, 40.0, -120.0]) + rng.uniform(-3.0, 3.0, (512, 3))
L = np.array([0.35, 0.8, 0.45]); L /= np.linalg.norm(L)       # the light's direction in the world


def basis(l):
    t1 = np.cross(l, [1.0, 0.0, 0.0] if abs(l[0]) < 0.9 else [0.0, 1.0, 0.0]); t1 /= np.linalg.norm(t1)
    return t1, np.cross(l, t1)


def rot(axis, deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return {0: np.array([[1, 0, 0], [0, c, -s], [0, s, c]]), 1: np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])}[axis]


CAMERAS = [(np.eye(3), np.zeros(3)), (rot(1, 25.0) @ rot(0, -10.0), np.array([2.0, -1.0, 3.5]))]   # modelview [R | t]


def noise_of(R, t, seeds, xoff):
    P = world @ R.T + t                                   # view space
    use(prog)
    for i in range(512): u3f(loc(prog, b'pts[%d]' % i), *P[i])
    u1i(loc(prog, b'xoff'), xoff); u1i(loc(prog, b'shadow_st_frame'), 0)
    if seeds:
        C = -R.T @ t; t1, t2 = basis(L)                  # as LLPipeline::bindShadowMaps
        u4f(loc(prog, b'shadow_seed_u'), *(R @ t1), float(t1 @ C)); u4f(loc(prog, b'shadow_seed_v'), *(R @ t2), float(t2 @ C))
    else:
        u4f(loc(prog, b'shadow_seed_u'), 0, 0, 0, 0); u4f(loc(prog, b'shadow_seed_v'), 0, 0, 0, 0)
    bindfbo(FB, sc.lfbo); viewport(0, 0, sc.W, sc.H); draw(4, 0, 3)
    img = read_rgba(sc.lfbo, sc.W, sc.H)
    out = np.empty((512, 4))
    for i in range(512): out[i] = img[i // 64, xoff + i % 64]
    return out


print('\nseeds: the same 512 points of the world, seen from two cameras (rotated 25 and -10 degrees, moved 4 m), frame 0')
for label, seeds, offs in (('world seeds (forward programs)', True, (0, 9)), ('pixel seeds (control)', False, (0, 9))):
    a = noise_of(*CAMERAS[0], seeds, offs[0]); b = noise_of(*CAMERAS[1], seeds, offs[1])
    same = float(np.mean(np.all(np.abs(a - b) < 1e-4, axis=1)))
    print(f'  {label:<32} points with the same two numbers in both views: {100 * same:5.1f} %')
    if seeds:
        check(same >= 0.95, f'world seeds: {100 * same:.1f} % of the points draw the same numbers from both cameras (>= 95 %; the rest sit on a 4 mm cell edge)', 'seeds: ')
        m, sd = a.mean(axis=0), a.std(axis=0)
        check(all(0.40 < v < 0.60 for v in m) and all(0.22 < v < 0.36 for v in sd) and a.min() >= 0.0 and a.max() < 1.0,
              f'world seeds: uniform in [0, 1): mean {np.round(m, 2).tolist()}, std {np.round(sd, 2).tolist()}', 'seeds: ')
        check(abs(np.corrcoef(a[:, 0], a[:, 2])[0, 1]) < 0.1, 'world seeds: the sun and local light streams are decorrelated', 'seeds: ')
    else:
        check(same < 0.1, f'pixel seeds slide with the screen: {100 * same:.1f} % of the points keep their numbers', 'seeds: ')
fn('glDeleteProgram', None, U)(prog); sc.release()
gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
if failures:
    print('\nFAILED:\n' + '\n'.join(failures)); sys.exit(1)
