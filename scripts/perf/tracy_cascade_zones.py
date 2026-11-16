#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Per-cascade count and time of named Tracy zones in the sun/moon shadow passes of a capture.

Each zone event is attributed to the `LLPipeline::renderShadowImpl` that contains it:

- a renderShadowImpl inside a `LLPipeline::generateSunShadow` call with three or more passes is a main-view pass
  (main0..main3 are the four sun cascades);
- one inside a call with fewer passes is a reflection-probe pass (probe0, probe1);
- everything else is "outside".

Values are per frame (`FTM_FRAME` count), as calls/ms.

A zone may be given as `child@parent`, for example `LLRenderPass::pushBatch@shadow alpha blend`. Then only the child's
events that lie inside an event of the parent zone are counted (the draws of one pass).

**Valid for captures without projector shadows only** (shadow setting "sun" or "none"). In an `all` capture the
projector pool's tiles call renderShadowImpl inside generateSunShadow, after the cascades; their passes are reported as
`main4+`, but the main call's pass count then no longer separates main from probe calls reliably.

It drives `tracy-csvexport -u -f <name>` once per zone (about 1.5 minutes per zone for a 1.4 GB capture) and streams the
output, so nothing large is written. The per-event CSV is read by its header, so both exporter layouts work:

- Tracy 0.11.1 (the measuring build's `packages/bin`): name,src_file,src_line,ns_since_start,exec_time_ns,thread
- Tracy 0.15.0 (winget): the same plus a trailing `value` column.

Used by doc/shadows/sun-shadow-cost-study.md section 2; `LLRenderPass::pushBatch` gives the draw calls per cascade.

    python -I scripts/perf/tracy_cascade_zones.py <tracy-csvexport.exe> <capture.tracy> <out.txt> <zone> [<zone> ...]

Pass absolute Windows paths for the executable and the capture. Run with TEMP/TMP in the repo's .fixture-tmp.
The output file is appended to, one line per zone.
"""

import bisect
import collections
import csv
import io
import subprocess
import sys


def events(exe, cap, name):
    """(name, start_ns, duration_ns) of every event whose name contains `name` (tracy-csvexport -f is a substring match)."""
    p = subprocess.Popen([exe, '-u', '-f', name, cap], stdout=subprocess.PIPE, bufsize=1 << 20)
    reader = csv.reader(io.TextIOWrapper(p.stdout, encoding='utf8', errors='replace', newline=''))
    header = next(reader, None)
    if header is None:
        p.wait()
        return
    try:
        i_name = header.index('name')
        i_start = header.index('ns_since_start')
        i_dur = header.index('exec_time_ns')
    except ValueError:
        p.kill()
        raise SystemExit('unexpected tracy-csvexport header: %r' % (header,))
    for row in reader:
        try:
            yield row[i_name], int(row[i_start]), int(row[i_dur])
        except (IndexError, ValueError):
            continue
    p.wait()


def exact(exe, cap, name):
    """Events of exactly this zone name, sorted by start."""
    return sorted((s, d) for n, s, d in events(exe, cap, name) if n == name)


def inside(intervals, starts, s):
    k = bisect.bisect_right(starts, s) - 1
    return k if (k >= 0 and s < intervals[k][0] + intervals[k][1]) else -1


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    exe, cap, out = argv[1], argv[2], argv[3]
    specs = argv[4:]
    gen = exact(exe, cap, 'LLPipeline::generateSunShadow')
    rsi = exact(exe, cap, 'LLPipeline::renderShadowImpl')
    frames = max(len(exact(exe, cap, 'FTM_FRAME')), 1)
    gs = [g[0] for g in gen]
    per_gen = collections.defaultdict(list)
    for k, (s, d) in enumerate(rsi):
        i = inside(gen, gs, s)
        if i >= 0:
            per_gen[i].append(k)
    label = {}
    for i, ks in per_gen.items():
        kind = 'main' if len(ks) >= 3 else 'probe'
        for j, k in enumerate(ks):
            label[k] = '%s%d' % (kind, j) if (kind == 'probe' or j < 4) else 'main4+'
    rs = [r[0] for r in rsi]
    parents = {}
    cols = ['main0', 'main1', 'main2', 'main3', 'probe0', 'probe1']
    with open(out, 'a', encoding='utf8') as o:
        for spec in specs:
            name, _, parent = spec.partition('@')
            if parent and parent not in parents:
                ev = exact(exe, cap, parent)
                parents[parent] = (ev, [e[0] for e in ev])
            cnt = collections.Counter()
            tim = collections.Counter()
            for s, d in exact(exe, cap, name):
                if parent and inside(parents[parent][0], parents[parent][1], s) < 0:
                    continue
                k = inside(rsi, rs, s)
                key = label.get(k, 'outside') if k >= 0 else 'outside'
                cnt[key] += 1
                tim[key] += d
            extra = ''
            if cnt['main4+']:
                extra = '  main4+ %7.1f/%6.3f' % (cnt['main4+'] / frames, tim['main4+'] / frames / 1e6)
            o.write('%-48s ' % spec[:48]
                    + ' '.join('%s %7.1f/%6.3f' % (c, cnt[c] / frames, tim[c] / frames / 1e6) for c in cols)
                    + '  outside %7.1f/%6.3f' % (cnt['outside'] / frames, tim['outside'] / frames / 1e6) + extra + '\n')
            o.flush()
    with open(out, encoding='utf8') as f:
        print(f.read())
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
