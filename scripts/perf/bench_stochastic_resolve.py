# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""GPU time per pass of stochastic transparency, three pipelines, offline (no viewer launch).

Runs the viewer's own shaders with the pass structure and GL state of LLPipeline / LLDrawPoolAlpha::renderStochastic
(the harness of test_alpha_stochastic.py: its shared blocks are executed here, so the two cannot differ):
  shipping  the shaders of the git ref STOCH_OLD_REF (default 1f38b56b50, the viewer before the 2026-11-14 upgrade): S = 8 samples in
            one pass of 8, the hashed dither, the averaging pre-pass into RGBA32F, the 1/8 resolve, the composite;
  c121      the shaders of STOCH_REF_REF (default c121dc100c, the upgrade before the mask): measuring pass, blend, a smooth pass, composite;
  now       the working tree: the measuring pass also writes the mask (R8) of where anything transparent was drawn, the blend and the
            composite (which smooths) draw only under it.
Scenes (the pan scenes of the test): T3 three textured layers, Q10 ten textured layers, S10 ten layers of hair strips, all full screen; SC a
scattered scene about 30 % covered: hair in the centre, a window in a corner. Static and with a camera pan of 1.3, 0.7 px a frame (history
reprojected). Per pass, the median of GL_TIME_ELAPSED queries over the frames after the history has formed, the lowest of BENCH_RUNS runs;
the passes are alpha (total alpha), depth, accumulation, measure/current, blend/resolve, smooth, composite. Nothing is read back.

    STOCH_TEST_SIZE=2560x1440 BENCH_FRAMES=60 BENCH_RUNS=3 python scripts/perf/bench_stochastic_resolve.py
"""
import os, re, sys, statistics
from pathlib import Path

FRAMES = int(os.environ.get('BENCH_FRAMES', '60'))
SCENES = os.environ.get('BENCH_SCENES', 'T3,Q10,S10,SC').split(',')
sys.argv = sys.argv[:1]   # the GL bootstrap reads --opengl
RUNS = int(os.environ.get('BENCH_RUNS', '3'))
WARM = 12   # the frames before the timed ones: the history has formed

test_path = Path(__file__).with_name('test_alpha_stochastic.py')
source = test_path.read_text(encoding='utf-8')
blocks = re.findall(r'# <<bench-shared:begin>>\n(.*?)# <<bench-shared:end>>\n', source, re.S)
assert len(blocks) == 3, len(blocks)
ns = {'__file__': str(test_path), '__name__': 'stochastic_shared'}
exec(compile('\n'.join(blocks), 'stochastic-shared', 'exec'), ns)
get = ns.__getitem__
W, H = get('W'), get('H')
print(f'size {W}x{H}, {FRAMES} frames per case, the first {WARM} not timed')

ns['layer'], ns['variant'], ns['shade'] = get('pan_layer'), get('pan_variant'), get('pan_shade')   # as the pans test swaps them
ns['set_opaque'](1.0)
ns['TIMERS'] = {}
KEYS = ['total alpha', 'stochastic depth', 'accumulation', 'measure', 'blend', 'smooth', 'composite']


def scene_layers(name):
    if name != 'SC':
        return get('pan_scene')(name), {}
    # scattered, about 30 % of the pixels: hair (6 layers of strips) in the centre, a window (2 textured layers) in a corner
    hx0, hx1, hy0, hy1 = int(W * 0.30), int(W * 0.70), int(H * 0.22), int(H * 0.77)
    wx0, wy1 = int(W * 0.80), int(H * 0.40)
    L = get('L')
    hair = get('pan_scene')('S10')[:6]
    window = [L(l[0], l[1], l[2], 201 + i, l[4], l[5]) for i, l in enumerate(get('pan_scene')('Q10')[:2])]
    rects = {l[3]: (hx0, hx1, hy0, hy1) for l in hair}
    rects.update({l[3]: (wx0, W, 0, wy1) for l in window})
    return hair + window, rects


def run(name, pipe, pan):
    layers, rects = scene_layers(name)
    old = pipe == 'old'
    total = 8
    rj = get('pan_reproj')(pan)
    T = get('new_temporal')(rj, old=old, ref=pipe == 'ref')
    series = {k: [] for k in KEYS}
    ns['PAN'][0] = (0.0, 0.0)
    orig = ns['layer']
    if rects:
        def cut_layer(prog, z, alpha=0., rgb=(1., 1., 1.), seed=0, glow=0., slope=None, slope_y=0.):
            ns['LAYER_SCISSOR'] = rects.get(seed)
            orig(prog, z, alpha, rgb, seed, glow, slope, slope_y)
            ns['LAYER_SCISSOR'] = None
        ns['layer'] = cut_layer
    for f in range(1, FRAMES + 1):
        ns['TEMPORAL_FRAME'][0] = f
        ns['PAN'][0] = (f * pan[0], f * pan[1])
        ns['stochastic'](layers, total, total, scene_rgba=(0.5, 0.5, 0.5, 0.0), temporal=T, pipe=pipe, readback=False)
        get('geterror')()   # the queries need the frame done
        t = ns['read_timers']()
        if f > WARM:
            for k in KEYS:
                series[k].append(sum(t.get(k, [0])) / 1e6)
    ns['layer'] = orig
    med = {k: statistics.median(v) for k, v in series.items()}
    med['total'] = statistics.median([sum(series[k][i] for k in KEYS) for i in range(len(series['composite']))])
    return med


PIPES = ('old', 'ref', 'new')
NAMES = {'old': 'shipping', 'ref': 'c121', 'new': 'now'}
rows = []
for name in SCENES:
    for pan in ((0.0, 0.0), (1.3, 0.7)):
        for pipe in PIPES:
            runs = [run(name, pipe, pan) for _ in range(RUNS)]
            best = {k: min(r[k] for r in runs) for k in runs[0]}   # the lowest median of the runs, per pass
            best['total'] = sum(best[k] for k in KEYS)   # the sum of the passes' lowest medians
            rows.append((name, pan, pipe, best))

print(f'lowest median GPU ms per pass of {RUNS} runs: shipping = the viewer before the upgrade (current -> measure column, resolve -> blend column; no smooth)')
print(f'{"scene":5s} {"pan":9s} {"":8s} ' + ' '.join(f'{k[:10]:>10s}' for k in KEYS) + f' {"total":>8s}')
for name, pan, pipe, med in rows:
    print(f'{name:5s} {pan[0]:3.1f},{pan[1]:3.1f}   {NAMES[pipe]:8s} ' + ' '.join(f'{med[k]:10.3f}' for k in KEYS) + f' {med["total"]:8.3f}')
print('now - shipping and now - c121, ms (the budget is +0.43 ms over shipping on full-screen scenes):')
for i in range(0, len(rows), 3):
    (name, pan, _, b), (_, _, _, c), (_, _, _, a) = rows[i], rows[i + 1], rows[i + 2]
    print(f'  {name:5s} {pan[0]:3.1f},{pan[1]:3.1f}  vs shipping {a["total"] - b["total"]:+7.3f}  vs c121 {a["total"] - c["total"]:+7.3f}  (shipping {b["total"]:.3f}, c121 {c["total"]:.3f}, now {a["total"]:.3f})')
sys.exit(0)
