#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Per-cascade count and time of named Tracy zones in the sun/moon shadow passes of a capture.

Each zone event is attributed to the `LLPipeline::renderShadowImpl` that contains it. A renderShadowImpl inside a
`LLPipeline::generateSunShadow` call with three or more passes is a main-view cascade (main0..main3); one inside a call
with fewer passes is a reflection-probe pass (probe0, probe1). Everything else is "outside". Values are per frame
(`FTM_FRAME` count), as calls/ms.

It drives `tracy-csvexport -u -f <name>` once per zone (about 1.5 minutes per zone for a 1.4 GB capture) and streams the
output, so nothing large is written. Used by doc/shadows/sun-shadow-cost-study.md section 2; `LLRenderPass::pushBatch`
as a zone name gives the draw calls per cascade.

    python -I scripts/perf/tracy_cascade_zones.py <tracy-csvexport.exe> <capture.tracy> <out.txt> <zone> [<zone> ...]

Pass absolute Windows paths for the executable and the capture. Run with TEMP/TMP in the repo's .fixture-tmp.
The output file is appended to, one line per zone.
"""

import bisect
import collections
import subprocess
import sys


def events(exe, cap, name):
    p = subprocess.Popen([exe, '-u', '-f', name, cap], stdout=subprocess.PIPE, text=True, bufsize=1 << 20,
                         encoding='utf8', errors='replace')
    for line in p.stdout:
        if line.startswith('name,'):
            continue
        # name,src_file,src_line,ns_since_start,exec_time_ns,thread
        a = line.rstrip().rsplit(',', 3)
        if len(a) < 4:
            continue
        try:
            yield a[0].split(',')[0], int(a[1]), int(a[2])
        except ValueError:
            continue
    p.wait()


def exact(exe, cap, name):
    """Events of exactly this zone name (-f matches substrings)."""
    return [(s, d) for n, s, d in events(exe, cap, name) if n == name]


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    exe, cap, out = argv[1], argv[2], argv[3]
    names = argv[4:]
    gen = sorted(exact(exe, cap, 'LLPipeline::generateSunShadow'))
    rsi = sorted(exact(exe, cap, 'LLPipeline::renderShadowImpl'))
    frames = max(len(exact(exe, cap, 'FTM_FRAME')), 1)
    gs = [g[0] for g in gen]
    per_gen = collections.defaultdict(list)
    for k, (s, d) in enumerate(rsi):
        i = bisect.bisect_right(gs, s) - 1
        if i >= 0 and s < gen[i][0] + gen[i][1]:
            per_gen[i].append(k)
    label = {}
    for i, ks in per_gen.items():
        kind = 'main' if len(ks) >= 3 else 'probe'
        for j, k in enumerate(ks):
            label[k] = '%s%d' % (kind, j)
    rs = [r[0] for r in rsi]
    cols = ['main0', 'main1', 'main2', 'main3', 'probe0', 'probe1']
    with open(out, 'a', encoding='utf8') as o:
        for name in names:
            cnt = collections.Counter()
            tim = collections.Counter()
            for s, d in exact(exe, cap, name):
                k = bisect.bisect_right(rs, s) - 1
                key = label.get(k) if (k >= 0 and s < rsi[k][0] + rsi[k][1]) else None
                key = key or 'outside'
                cnt[key] += 1
                tim[key] += d
            o.write('%-40s ' % name[:40]
                    + ' '.join('%s %7.1f/%6.3f' % (c, cnt[c] / frames, tim[c] / frames / 1e6) for c in cols)
                    + '  outside %7.1f/%6.3f\n' % (cnt['outside'] / frames, tim['outside'] / frames / 1e6))
            o.flush()
    with open(out, encoding='utf8') as f:
        print(f.read())
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
