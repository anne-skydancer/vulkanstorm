# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GL check that a projector shadow does not depend on the camera's orientation, no viewer launch.

sampleSpotShadow (class1 deferred/shadowUtil.glsl) used to fade the projector shadow with the
sun's cascade splits (shadow_clip z and w): from (z + w) / 2 to w, on the pixel's view depth. The
splits follow what the camera sees (LLPipeline::generateSunShadow), so the same lit pixel at the
same distance from the camera was shadowed or not depending on pitch and yaw. It now fades with
the Euclidean camera distance over the last 20 % of spot_shadow_reach (128 m, capped by the draw
distance).

The shader sees the camera orientation only through shadow_clip (and, in the scene, the pixel's
position in view space), so the check renders a pixel inside a box's projector umbra at distances
of 8..125 m from the camera, for three camera azimuths around it and for the three sets of sun
splits the model (scripts/shadows/cascade_fit_model.py in PR #330) gives for different views:
    looking out over open land     5.3  32.2  108.1  256.0
    tilted 45 degrees down         0.7   2.9    9.2   21.4
    a skybox room                  0.6   2.2    6.9   16.0
Every combination must give the same shadow value with the new shader (the old one fails: the
value changes with the splits), and the value must be the spot shadow itself (0) up to 80 % of the
reach and 1 beyond it, with the fade in between.

Uses the viewer's class2 deferred/sunLightF.glsl + shadowUtil.glsl, PCF, and
bench_shadow_legacy.py's ray-cast scene. Exit status 0 when all checks pass. The OLD shader is the stored
reference scripts/perf/reference/shadowUtil_before_spot_fade.glsl (shadowUtil.glsl before the fix,
so the check keeps working once the fix is in HEAD), the NEW one the working tree.
"""
import math, subprocess, sys
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
exec(compile((HERE / 'bench_shadow_legacy.py').read_text().split('\nfailures = []')[0], str(HERE / 'bench_shadow_legacy.py'), 'exec'))

W, H = 1920, 1080
SPLITS = {'looking out': (5.3, 32.2, 108.1, 256.0), 'tilted down': (0.7, 2.9, 9.2, 21.4), 'skybox room': (0.6, 2.2, 6.9, 16.0)}
DISTANCES = (8.0, 16.0, 24.0, 40.0, 80.0, 100.0, 110.0, 120.0, 125.0)
AZIMUTHS = (0.0, 90.0, 200.0)
REACH = 128.0

def util_program(util_text, variant):
    sun = (root / SUN_F).read_text()
    local = VARIANT_DEFINES[variant]
    return program([(0x8B31, VERTEX), (0x8B30, stage(sun, local)), (0x8B30, stage(util_text, ['SUN_SHADOW', 'SPOT_SHADOW'] + local)), (0x8B30, STUBS_F)])

def shadow_at(prog, distance, azimuth, clip):
    """Projector shadow value (light map blue) at the ground point under a box that a spot light
    shadows, seen from `distance` metres away (from 20 degrees above it, or 1.7 m high when close), from the given azimuth."""
    a = math.radians(azimuth)
    # the eye rises with the distance (20 degrees above the ground point) so that a pixel is not a
    # grazing strip of ground wider than the umbra; the distance to the point stays `distance`
    h = max(1.7, distance * math.sin(math.radians(20.0)))
    flat = math.sqrt(max(distance * distance - h * h, 0.0))
    eye = [flat * math.sin(a), h, flat * math.cos(a)]
    box = [((-0.5, 2.0, -0.5), (0.5, 2.5, 0.5))]
    spots = [([0.0, 6.0, 0.0], [0.0, 0.0, 0.0])] * 2
    sc = Scene(W, H, box, eye, [0.0, 0.0, 0.0], [0.0, 1.0, 0.0], spots, clip)
    sc.bind(prog, LIGHT_SIZE)
    u4f(loc(prog, b'shadow_clip'), *clip)
    u1f(loc(prog, b'spot_shadow_reach'), REACH)
    draw(4, 0, 3); finish()
    img = np.frombuffer(sc.read_channel(0x1905), dtype=np.float32).reshape(H, W)
    x, y = sc.project([0.0, 0.0, 0.0])
    value = float(img[y, x])
    sc.unbind(); sc.release()
    return value

fails = []
def ok(cond, msg):
    print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
    if not cond: fails.append(msg)

def main():
    print('GL:', get_string(0x1F01).decode(), flush=True)
    old_text = (HERE / 'reference' / 'shadowUtil_before_spot_fade.glsl').read_text()
    new_text = (root / UTIL_F).read_text()
    for variant in ('tree',):
        name = 'PCF '
        old_prog, new_prog = util_program(old_text, variant), util_program(new_text, variant)
        print(f'\n{name}: shadow value at the umbra, by distance (columns: splits set; rows: old | new), azimuth 0')
        print('  distance   ' + '   '.join(f'{k:>26s}' for k in SPLITS))
        for d in DISTANCES:
            olds = [shadow_at(old_prog, d, 0.0, c) for c in SPLITS.values()]
            news = [shadow_at(new_prog, d, 0.0, c) for c in SPLITS.values()]
            print(f'  {d:7.0f} m  ' + '   '.join(f'{o:12.2f} | {n:9.2f}' for o, n in zip(olds, news)))
            # new: identical for every splits set and every azimuth
            allnew = [shadow_at(new_prog, d, az, c) for az in AZIMUTHS for c in SPLITS.values()]
            spread = max(allnew) - min(allnew)
            expected_fade = min(max((REACH - d) / (0.2 * REACH), 0.0), 1.0)
            expected = 1.0 - expected_fade   # an umbra (0) mixed to 1 by the fade
            ok(spread < 0.02, f'{name} {d:5.0f} m: identical for 3 azimuths x 3 split sets (spread {spread:.3f}); old spread {max(olds) - min(olds):.2f}')
            ok(abs(allnew[0] - expected) < 0.08, f'{name} {d:5.0f} m: umbra value {allnew[0]:.2f}, expected {expected:.2f} (fade {expected_fade:.2f})')
        # the old shader fails the orientation test somewhere in range
        spreads = [max(v) - min(v) for v in ([shadow_at(old_prog, d, 0.0, c) for c in SPLITS.values()] for d in DISTANCES)]
        ok(max(spreads) > 0.5, f'{name}: the old shader depends on the splits (largest spread {max(spreads):.2f}), so the test can fail')
    print(f'\n{"all checks passed" if not fails else str(len(fails)) + " check(s) failed"}')
    return 1 if fails else 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)
