# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""GPU time per pass of stochastic transparency, before and after the 2026-11-14 upgrade, offline (no viewer launch).

Runs the viewer's own shaders with the pass structure and GL state of LLPipeline / LLDrawPoolAlpha::renderStochastic
(the harness of test_alpha_stochastic.py: its shared blocks are executed here, so the two cannot differ):
  before  the shaders of the git ref STOCH_OLD_REF (default 1f38b56b50, the viewer before the upgrade): S = 8 samples in
          one pass of 8, the hashed dither, the averaging pre-pass into RGBA32F, the 1/8 resolve, the composite;
  after   the working tree: S = 4, the blue-noise / golden-ratio dither, the measuring pass, the grain-weighted blend, the
          smooth, the composite, with the NaN guard.
Scenes (the pan scenes of the test): T3 three textured layers, Q10 ten textured layers, S10 ten layers of hair strips; static
and with a camera pan of 1.3, 0.7 px a frame (history reprojected). Per pass, the median of GL_TIME_ELAPSED queries over the
frames after the history has formed; the passes are alpha (total alpha), depth, accumulation, measure/current, blend/resolve,
smooth, composite. Nothing is read back (the GPU time is the point; the errors are in test_alpha_stochastic.py).

    STOCH_TEST_SIZE=1920x1080 python scripts/perf/bench_stochastic_resolve.py
    STOCH_TEST_SIZE=2560x1440 BENCH_FRAMES=60 BENCH_SCENES=T3,Q10,S10 python scripts/perf/bench_stochastic_resolve.py
"""
import os, re, sys, statistics
from pathlib import Path

FRAMES = int(os.environ.get('BENCH_FRAMES', '60'))
SCENES = os.environ.get('BENCH_SCENES', 'T3,Q10,S10').split(',')
sys.argv = sys.argv[:1]   # the GL bootstrap reads --opengl
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


def run(name, pipe, pan):
    layers = get('pan_scene')(name)
    old = pipe == 'old'
    total = 8 if old else 4
    rj = get('pan_reproj')(pan)
    T = get('new_temporal')(rj, old=old)
    series = {k: [] for k in KEYS}
    ns['PAN'][0] = (0.0, 0.0)
    for f in range(1, FRAMES + 1):
        ns['TEMPORAL_FRAME'][0] = f
        ns['PAN'][0] = (f * pan[0], f * pan[1])
        ns['stochastic'](layers, total, total, scene_rgba=(0.5, 0.5, 0.5, 0.0), temporal=T, pipe=pipe, readback=False)
        get('geterror')()   # the queries need the frame done
        t = ns['read_timers']()
        if f > WARM:
            for k in KEYS:
                series[k].append(sum(t.get(k, [0])) / 1e6)
    med = {k: statistics.median(v) for k, v in series.items()}
    med['total'] = statistics.median([sum(series[k][i] for k in KEYS) for i in range(len(series['composite']))])
    return med


rows = []
for name in SCENES:
    for pan in ((0.0, 0.0), (1.3, 0.7)):
        for pipe in ('old', 'new'):
            rows.append((name, pan, pipe, run(name, pipe, pan)))

print('median GPU ms per pass: before = the viewer before the upgrade (current -> measure column, resolve -> blend column; no smooth)')
print(f'{"scene":5s} {"pan":9s} {"":6s} ' + ' '.join(f'{k[:10]:>10s}' for k in KEYS) + f' {"total":>8s}')
for name, pan, pipe, med in rows:
    print(f'{name:5s} {pan[0]:3.1f},{pan[1]:3.1f}   {"before" if pipe == "old" else "after":6s} ' + ' '.join(f'{med[k]:10.3f}' for k in KEYS) + f' {med["total"]:8.3f}')
print('after - before, ms (the budget is +0.43 ms of the 8.6 ms Stochastic frame; a saving is fine):')
for i in range(0, len(rows), 2):
    (name, pan, _, b), (_, _, _, a) = rows[i], rows[i + 1]
    print(f'  {name:5s} {pan[0]:3.1f},{pan[1]:3.1f}  total {a["total"] - b["total"]:+7.3f} ms  (before {b["total"]:.3f}, after {a["total"]:.3f})')
sys.exit(0)
