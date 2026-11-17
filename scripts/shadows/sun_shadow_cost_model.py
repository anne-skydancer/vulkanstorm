#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline cost model for the sun/moon shadow cost study (doc/shadows/sun-shadow-cost-study.md).

No GPU and no viewer: it combines the measured Rooftops Ultra numbers (Tracy, main thread, still view, sun minus none)
with the assumptions the study names, and prints the tables of the study:

  1. where the cascade time goes (measured), and the cost per draw it implies;
  2. the level geometry of today's stable cascades (S1) for the cascade-count and shadow-distance options;
  3. the caster sizes a small-caster cull would drop per level;
  4. each option's CPU saving on its own (ranges; [measured] inputs, [inferred] factors);
  5. the recommended order, cumulative, and the order without the cache, with frame time and fps.

Every number that is not measured is a named assumption (A..), printed next to the result. The level geometry
is a port of llshadowcascadefit.h (splitDistances, sliceSphere, quantizeRadius, snapShadowDistance).

--check tests self-consistency only: sums, the S1 port against values #542 quotes, and that no plan stage adds cost.
The measured inputs (CASCADE, DRAWS, ZONES) are typed-in constants, not re-read from a capture. CASCADE comes from the
Rooftops report's per-cascade table, ZONES from this study's attribution (scripts/perf/tracy_cascade_zones.py). The two
attributions differ slightly (sort 4.07 against 4.18 ms; the tool also counts nested zones of the same name). Run:

    python scripts/shadows/sun_shadow_cost_model.py           # all tables
    python scripts/shadows/sun_shadow_cost_model.py --check   # invariants
"""

from __future__ import annotations

import argparse
import math
import sys

# ------------------------------------------------------------------ measured inputs (Rooftops Ultra, still, sun)
# doc/shadows/rooftops-ultra-shadow-split-2026-11-16.md section 2 (per cascade, ms per frame, Tracy attached)
CASCADE = [  # total, cull, sort, draw, avatar pool, rigged batches, skin palettes (nested in rigged)
    dict(total=2.71, cull=0.07, sort=1.72, draw=0.93, pool=0.46, rigged=0.32, palettes=0.20),
    dict(total=2.04, cull=0.38, sort=0.79, draw=0.86, pool=0.05, rigged=0.26, palettes=0.10),
    dict(total=2.31, cull=0.50, sort=0.80, draw=1.00, pool=0.04, rigged=0.14, palettes=0.00),
    dict(total=2.29, cull=0.46, sort=0.76, draw=1.08, pool=0.04, rigged=0.13, palettes=0.00),
]
# this study: per-event export of RT-still__sun.tracy (1888 frames), LLRenderPass::pushBatch inside each
# renderShadowImpl of the main generateSunShadow call (scripts/perf/tracy_cascade_zones.py, zone LLRenderPass::pushBatch)
DRAWS = [370.9, 1561.8, 2354.2, 2607.4]
PROBE_DRAWS = [49.7, 439.2]
DRAWS_ALL_PASSES = 8385.4      # every pushBatch of the frame
PUSHBATCH_MS = [0.063, 0.188, 0.256, 0.264]  # pushBatch zone time per cascade (the call itself, Tracy on)
FRAME_SUN = 31.9               # steady frame, still, sun (ms)
FRAME_NONE = 23.1              # steady frame, still, no shadows (ms)
SHARED_SORT = 1.5              # world-pass sort work the first cascade sort takes over (report section 2)
PROBE_PASSES = 0.4             # the probe call's two passes (report section 2)
# this study: per-event exports of RT-still__sun.tracy, zones attributed to the main cascades 0-3 (scripts/perf/tracy_cascade_zones.py)
ZONES = {
    'updateCull':                     (0.068, 0.384, 0.499, 0.455),
    'stateSort':                      (1.734, 0.824, 0.832, 0.785),
    'checkOcclusionAndRebuildMesh':   (0.143, 0.309, 0.234, 0.212),
    'StateSort: visible groups':      (0.054, 0.133, 0.159, 0.154),
    'postSort':                       (1.517, 0.337, 0.401, 0.386),
    'build render map':               (0.092, 0.317, 0.386, 0.371),
    'rebuild delayed upd groups':     (0.854, 0.000, 0.000, 0.000),
    'rebuildPriorityGroups':          (0.552, 0.010, 0.005, 0.005),
    'shadow simple':                  (0.257, 0.399, 0.443, 0.480),
    'shadow alpha blend':             (0.047, 0.235, 0.352, 0.380),
    'shadow alpha masked':            (0.019, 0.045, 0.050, 0.055),
    'shadow alpha grass (+material)': (0.120, 0.115, 0.090, 0.088),
    'shadow geom':                    (0.467, 0.056, 0.051, 0.050),
    'pushBatches (static opaque)':    (0.034, 0.153, 0.246, 0.278),
    'pushRiggedBatches':              (0.205, 0.169, 0.089, 0.084),
    'pushRiggedMaskBatches':          (0.114, 0.096, 0.052, 0.049),
    'pushMaskBatches':                (0.025, 0.068, 0.094, 0.100),
    'avatar pool renderShadow':       (0.457, 0.049, 0.044, 0.043),
    'updateSkinInfoMatrixPalette':    (0.201, 0.103, 0.000, 0.000),
}
ALPHA_BLEND_MS = sum(ZONES['shadow alpha blend'])  # the four main cascades

# #542 (doc/shadows/sun-cascades-refresh.md section 6): cascade CPU with the static/dynamic cache, still view
CACHE_RESULT = (2.0, 4.2)

# ------------------------------------------------------------------ assumptions (each printed where it is used)
A = {
    'A1 small casters, share of static and rigged draws in levels 1-3 below the cull size (draw infos batch many faces, so it may be low)': (0.05, 0.25),
    'A2 small-caster cull also skips their cull+sort work (share of it)': (0.0, 0.5),
    'A3 alpha-blend loop: share of the measured pass that is per-draw rebinding (the pass time carries about 7 Tracy zones per draw)': (0.20, 0.35),
    'A4b 5c: share of the net sort that becomes parallel (the sort writes shared pools; rebuildMesh is GL)': (0.0, 0.5),
    'A4 5c parallel efficiency against the ideal 6-wide split': (0.50, 0.75),
    'A5 G3 palette UBO: share of the rigged-batch time it removes': (0.30, 0.60),
    'A6 MDI: share of the static draw time it removes (textured passes merge per texture run only)': (0.40, 0.65),
    'A7 MDI: share of the rigged draw time it removes (needs G3; the avatar pool is not arena geometry)': (0.25, 0.50),
    'A9 shadow distance 192 -> 128: share of levels 2-3 time removed': (0.25, 0.45),
}


A1 = 'A1 small casters, share of static and rigged draws in levels 1-3 below the cull size (draw infos batch many faces, so it may be low)'
A3 = 'A3 alpha-blend loop: share of the measured pass that is per-draw rebinding (the pass time carries about 7 Tracy zones per draw)'
A7 = 'A7 MDI: share of the rigged draw time it removes (needs G3; the avatar pool is not arena geometry)'


# ------------------------------------------------------------------ level geometry (port of llshadowcascadefit.h)

def split_distances(near, far, exponent, levels=4):
    e = max(exponent, 0.1)
    d = [near] + [near + (far - near) * (i / float(levels)) ** e for i in range(1, levels + 1)]
    d[1] *= 1.25
    d[1] = min(d[1], d[2])
    return d


def slice_sphere(k, n, f):
    kk = 1.0 + k * k
    c = min(0.5 * (n + f) * kk, f)
    return c, math.sqrt((f - c) ** 2 + f * f * k * k)


def quantize_radius(r):
    if not r > 0.0:
        return r
    octave = 2.0 ** math.floor(math.log2(r))
    step = octave / 16.0
    return math.ceil(r / step) * step


def snap_shadow_distance(m):
    if not m > 32.0:
        return 32.0
    return min(math.floor(m / 32.0 + 0.5) * 32.0, 256.0)


def corner_k(vfov_deg, aspect):
    tv = math.tan(math.radians(vfov_deg) / 2.0)
    return 1.1 * math.hypot(aspect * tv, tv)  # corner rays widened by 5 % of the far-plane diagonal


def s1_levels(shadow_distance, res, levels=4, vfov=60.0, aspect=16.0 / 9.0, near=0.25, far_clip=256.0, exponent=3.0):
    k = corner_k(vfov, aspect)
    far = max(min(snap_shadow_distance(shadow_distance), far_clip), near + 1.0)
    d = split_distances(near, far, exponent, levels)
    out = []
    for j in range(levels):
        _, r = slice_sphere(k, 0.75 * d[j], 1.25 * d[j + 1])
        rq = quantize_radius(r)
        out.append(dict(j=j, radius=rq, texel=2.0 * rq / (res - 1.0), near=d[j], far=d[j + 1]))
    return d, out


# ------------------------------------------------------------------ tables

def rng(lo, hi):
    return '%.2f-%.2f' % (min(lo, hi), max(lo, hi))


def table_breakdown(out):
    out.append('1. Where the cascade time goes (Rooftops Ultra, still, sun; ms per frame) [measured]')
    out.append('   cascade  total  cull  sort  draw  dyn.draw  static.draw  draws  us/draw(draw row)  pushBatch zone')
    tot = dict(total=0.0, cull=0.0, sort=0.0, draw=0.0, dyn=0.0, stat=0.0)
    for j, c in enumerate(CASCADE):
        dyn = c['pool'] + c['rigged']
        stat = c['draw'] - dyn
        tot['total'] += c['total']; tot['cull'] += c['cull']; tot['sort'] += c['sort']; tot['draw'] += c['draw']
        tot['dyn'] += dyn; tot['stat'] += stat
        out.append('   %d        %.2f  %.2f  %.2f  %.2f  %.2f      %.2f         %6.0f  %.2f               %.3f'
                   % (j, c['total'], c['cull'], c['sort'], c['draw'], dyn, stat, DRAWS[j], 1000.0 * c['draw'] / DRAWS[j],
                      PUSHBATCH_MS[j]))
    out.append('   sum      %.2f  %.2f  %.2f  %.2f  %.2f      %.2f         %6.0f  %.2f               %.3f'
               % (tot['total'], tot['cull'], tot['sort'], tot['draw'], tot['dyn'], tot['stat'], sum(DRAWS),
                  1000.0 * tot['draw'] / sum(DRAWS), sum(PUSHBATCH_MS)))
    out.append('   draws in the 4 cascades: %.0f of %.0f per frame (%.0f %%); the 2 probe passes %.0f'
               % (sum(DRAWS), DRAWS_ALL_PASSES, 100.0 * sum(DRAWS) / DRAWS_ALL_PASSES, sum(PROBE_DRAWS)))
    out.append('   dyn.draw = avatar pool + rigged batches (skin palettes nested); static.draw = draw - dyn.draw')
    out.append('   Sub-zones per cascade (ms per frame; nested zones overlap their parents) [measured]:')
    for k, v in ZONES.items():
        out.append('     %-32s %s   sum %.3f' % (k, '  '.join('%.3f' % x for x in v), sum(v)))
    out.append('')
    return tot


def table_levels(out):
    out.append('2. Level geometry of the stable cascades (S1), 60 deg vertical fov, 16:9, split exponent 3 [port of llshadowcascadefit.h]')
    out.append('   (the viewer allocates BlurHappySize(sqrt(w*h)) = 1936^2 at 2560x1440, not 1920^2: texels about 0.8 % finer)')
    out.append('   map   distance  levels  texel per level (cm)                     level far edges (m)')
    for res in (1440, 1920):
        for sd in (192.0, 160.0, 128.0):
            for lv in (4, 3):
                d, lev = s1_levels(sd, res, levels=lv)
                out.append('   %4d  %5.0f     %d       %-40s %s' % (
                    res, sd, lv, ' / '.join('%.2f' % (100.0 * x['texel']) for x in lev),
                    ' / '.join('%.1f' % x for x in d[1:])))
    out.append('')


def table_small(out):
    out.append('3. Small-caster cull: a caster whose bounding diameter is below n texels of its level is not drawn into it')
    out.append('   (1920^2 maps, 192 m, 4 levels; sizes in cm) [port]')
    _, lev = s1_levels(192.0, 1920)
    out.append('   n texels   ' + '   '.join('level %d' % x['j'] for x in lev))
    for n in (0.5, 1.0, 2.0):
        out.append('   %.1f        ' % n + '   '.join('%7.2f' % (100.0 * n * x['texel']) for x in lev))
    out.append('   The filter footprint is 25 cm (SHADOW_FOOTPRINT_M): a level-3 caster below one texel is under 1/1 of a texel already,')
    out.append('   so its shadow is a single snapped texel that blinks as the grid moves; the cull removes it in a stable way.')
    out.append('')


def option_savings(tot):
    """Standalone CPU saving of each option at Rooftops still (ms per frame), (low, high)."""
    lv13_static = sum(c['draw'] - c['pool'] - c['rigged'] for c in CASCADE[1:])
    lv13_rigged = sum(c['rigged'] for c in CASCADE[1:])
    lv13_sortcull = sum(c['sort'] + c['cull'] for c in CASCADE[1:])
    rigged = sum(c['rigged'] for c in CASCADE)
    a1 = A['A1 small casters, share of static and rigged draws in levels 1-3 below the cull size (draw infos batch many faces, so it may be low)']
    a2 = A['A2 small-caster cull also skips their cull+sort work (share of it)']
    a3 = A['A3 alpha-blend loop: share of the measured pass that is per-draw rebinding (the pass time carries about 7 Tracy zones per draw)']
    a4 = A['A4 5c parallel efficiency against the ideal 6-wide split']
    a5 = A['A5 G3 palette UBO: share of the rigged-batch time it removes']
    a6 = A['A6 MDI: share of the static draw time it removes (textured passes merge per texture run only)']
    a7 = A['A7 MDI: share of the rigged draw time it removes (needs G3; the avatar pool is not arena geometry)']
    a9 = A['A9 shadow distance 192 -> 128: share of levels 2-3 time removed']
    a4b = A['A4b 5c: share of the net sort that becomes parallel (the sort writes shared pools; rebuildMesh is GL)']
    sort_net = tot['sort'] - SHARED_SORT
    reach_lo = (tot['cull'] + a4b[0] * sort_net) * 5.0 / 6.0
    reach_hi = (tot['cull'] + a4b[1] * sort_net) * 5.0 / 6.0
    lv23 = CASCADE[2]['total'] + CASCADE[3]['total']
    opts = [
        ('cache C1+C2 (#542)', tot['total'] - CACHE_RESULT[1], tot['total'] - CACHE_RESULT[0], '#542 model, quiet feed'),
        ('small-caster cull', a1[0] * (lv13_static + lv13_rigged),
         a1[1] * (lv13_static + lv13_rigged) + a1[1] * a2[1] * lv13_sortcull, 'A1, A2'),
        ('alpha-blend loop hoist', a3[0] * ALPHA_BLEND_MS, a3[1] * ALPHA_BLEND_MS, 'A3; measured pass %.2f ms' % ALPHA_BLEND_MS),
        ('5c parallel cull (+ part of the sort)', a4[0] * reach_lo, a4[1] * reach_hi, 'A4, A4b; net of the shared sort'),
        ('G3 palette UBO', a5[0] * rigged, a5[1] * rigged, 'A5'),
        ('G4-G6 shadow MDI (static+rigged)', a6[0] * tot['stat'] + a7[0] * rigged, a6[1] * tot['stat'] + a7[1] * rigged, 'A6, A7'),
        ('3 levels instead of 4', 0.4 * (CASCADE[0]['total'] - SHARED_SORT), 1.0 * (CASCADE[0]['total'] - SHARED_SORT),
         'level 0 net of the shared sort; its casters, avatars first, move into the wider level 1'),
        ('shadow distance 192 -> 128 m', a9[0] * lv23, a9[1] * lv23, 'A9 (tier default change)'),
        ('probe faces reuse the main levels', 0.85 * PROBE_PASSES, PROBE_PASSES, 'the probe call (measured 0.4)'),
        ('one cull for all levels (PSSM split range)', 0.4 * tot['cull'], 0.6 * tot['cull'], 'the octree walk once, binned'),
    ]
    return opts


def table_options(out, tot):
    out.append('4. CPU saving of each option on its own, Rooftops still, today\'s code (ms per frame of %.2f) [inferred from measured]'
               % tot['total'])
    for name, lo, hi, basis in option_savings(tot):
        out.append('   %-44s %-10s  (%s)' % (name, rng(lo, hi), basis))
    out.append('   Assumptions:')
    for k, v in A.items():
        out.append('     %s: %s' % (k, rng(*v)))
    out.append('')


def frame_and_fps(cascade_ms, cached):
    # with the cache, the static sort no longer runs in the cascades, so the world pass pays the shared sort again (#542 A5)
    f = FRAME_SUN - (sum(c['total'] for c in CASCADE) - cascade_ms) + (SHARED_SORT if cached else 0.0)
    return f, 1000.0 / f


def plan_rows(cached_path):
    """Cumulative cascade CPU (best, worst) per stage. The state is split into components so that each stage
    reduces only what is left: cull, sort (with the shared sort), stat = static draws, rig = rigged batches,
    pool = avatar pool. best pairs the low end of what is left with the high end of each saving."""
    a1 = A[A1]
    a2 = A['A2 small-caster cull also skips their cull+sort work (share of it)']
    a3 = A[A3]
    a4 = A['A4 5c parallel efficiency against the ideal 6-wide split']
    a4b = A['A4b 5c: share of the net sort that becomes parallel (the sort writes shared pools; rebuildMesh is GL)']
    a5 = A['A5 G3 palette UBO: share of the rigged-batch time it removes']
    a6 = A['A6 MDI: share of the static draw time it removes (textured passes merge per texture run only)']
    a7 = A[A7]
    rig = sum(c['rigged'] for c in CASCADE)
    pool = sum(c['pool'] for c in CASCADE)
    stat = sum(c['draw'] for c in CASCADE) - rig - pool
    lv13_static = sum(c['draw'] - c['pool'] - c['rigged'] for c in CASCADE[1:])
    lv13_rigged = sum(c['rigged'] for c in CASCADE[1:])
    lv13_cull = sum(c['cull'] for c in CASCADE[1:])
    lv13_sort = sum(c['sort'] for c in CASCADE[1:])
    # two states: best (index 0) and worst (index 1)
    st = [dict(cull=sum(c['cull'] for c in CASCADE), sort=sum(c['sort'] for c in CASCADE), stat=stat, rig=rig, pool=pool)
          for _ in range(2)]
    rows = []

    def add(name, cached):
        vals = [sum(x.values()) for x in st]
        rows.append((name, vals[0], vals[1], cached))

    add('today', False)
    # Q: the alpha-blend hoist first (a static-draw cost), then the small-caster cull on what is left of the static
    # draws of levels 1-3 and on their rigged attachments, and optionally on their cull and sort (A2)
    for i, hi in ((0, 1), (1, 0)):
        hoist = a3[hi] * ALPHA_BLEND_MS
        st[i]['stat'] -= hoist + a1[hi] * max(lv13_static - hoist, 0.0)
        st[i]['rig'] -= a1[hi] * lv13_rigged
        st[i]['cull'] -= a1[hi] * a2[hi] * lv13_cull
        st[i]['sort'] -= a1[hi] * a2[hi] * lv13_sort
    add('Q: code findings, alpha-blend hoist, small-caster cull', False)
    if cached_path:
        add('C1: camera-position levels (image stage)', False)
        # C2: per frame only the dynamic layer is culled, sorted and drawn, plus amortised refreshes and the copy.
        # #542 gives the cached total (CACHE_RESULT) on today's dynamic draws. Q's cut of the rigged attachments stays
        # (it is per frame); its static cut folds into the refreshes. The rest of the cached total is cull, sort,
        # refreshes and the copy, split half cull, half sort for 5c.
        for i in (0, 1):
            rest = max(CACHE_RESULT[i] - (rig + pool), 0.0)
            st[i]['stat'] = 0.0
            st[i]['cull'] = 0.5 * rest
            st[i]['sort'] = 0.5 * rest
        add("C2: static/dynamic cache (#542 C2; Q's static cut folds into it)", True)
        for i, hi in ((0, 1), (1, 0)):
            st[i]['cull'] -= a4[hi] * 5.0 / 6.0 * st[i]['cull']
            st[i]['sort'] -= a4[hi] * a4b[hi] * 5.0 / 6.0 * st[i]['sort']
        add('5c: parallel culls of the per-frame passes (+ part of the sort)', True)
    else:
        for i, hi in ((0, 1), (1, 0)):
            sort_net = max(st[i]['sort'] - SHARED_SORT, 0.0)
            st[i]['cull'] -= a4[hi] * 5.0 / 6.0 * st[i]['cull']
            st[i]['sort'] -= a4[hi] * a4b[hi] * 5.0 / 6.0 * sort_net
        add('5c: parallel culls of the four cascades (+ part of the sort)', False)
    for i, hi in ((0, 1), (1, 0)):
        st[i]['rig'] -= a5[hi] * st[i]['rig']
    add('G3: skin palettes in one UBO', cached_path)
    for i, hi in ((0, 1), (1, 0)):
        st[i]['stat'] -= a6[hi] * st[i]['stat']
        st[i]['rig'] -= a7[hi] * st[i]['rig']
    add('G4-G6: shadow MDI' + (' (rigged layer; refresh frames gain most)' if cached_path else ' (static + rigged)'), cached_path)
    return rows


def table_plan(out, tot):
    for title, cached in (('5a. Recommended order, cumulative cascade CPU (ms per frame), Rooftops still [inferred]', True),
                          ('5b. Without the cache (C2 declined): no image change [inferred]', False)):
        out.append(title)
        for name, best, worst, c in plan_rows(cached):
            f_b, fps_b = frame_and_fps(best, c)
            f_w, fps_w = frame_and_fps(worst, c)
            out.append('   %-66s %5.2f-%5.2f ms   frame %4.1f-%4.1f   fps %4.1f-%4.1f' % (name, best, worst, f_b, f_w, fps_b, fps_w))
        out.append('')
    out.append('   Frame without any sun shadow: %.1f ms (%.1f fps); no cascade change takes the frame below it.' % (FRAME_NONE, 1000.0 / FRAME_NONE))
    out.append('   With the cache the world pass pays the %.1f ms shared sort again (#542 A5), which the frame column includes.' % SHARED_SORT)
    out.append('')


def checks():
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(('ok   ' if cond else 'FAIL ') + msg)
        ok = ok and cond

    tot_total = sum(c['total'] for c in CASCADE)
    check(abs(tot_total - 9.35) < 0.01, 'the four cascades sum to 9.35 ms (#542 section 6)')
    for j, c in enumerate(CASCADE):
        check(abs(c['cull'] + c['sort'] + c['draw'] - c['total']) <= 0.02, 'cascade %d: cull + sort + draw = total' % j)
        check(c['pool'] + c['rigged'] <= c['draw'], 'cascade %d: dynamic draw within the draw row' % j)
    d, lev = s1_levels(192.0, 1920)
    check([round(x, 2) for x in d] == [0.25, 4.06, 24.22, 81.14, 192.0], 'S1 splits at 192 m are 0.25/4.06/24.22/81.14/192 (#542)')
    check(abs(100.0 * lev[0]['texel'] - 0.70) < 0.02, 'S1 level 0 texel 0.70 cm at 1920^2 (#542 table)')
    check(abs(100.0 * lev[1]['texel'] - 4.17) < 0.02, 'S1 level 1 texel 4.17 cm at 1920^2 (#542 table)')
    check(all(lev[j]['texel'] < lev[j + 1]['texel'] for j in range(3)), 'texels grow with the level')
    tot = dict(total=tot_total, cull=sum(c['cull'] for c in CASCADE), sort=sum(c['sort'] for c in CASCADE),
               stat=sum(c['draw'] - c['pool'] - c['rigged'] for c in CASCADE))
    for name, lo, hi, _ in option_savings(tot):
        check(0.0 <= lo <= hi <= tot_total, 'option "%s": 0 <= low <= high <= today' % name)
    for cached in (True, False):
        rows = plan_rows(cached)
        check(all(0.0 <= b <= w + 1e-9 for _, b, w, _ in rows), 'plan %s: best <= worst, nothing negative' % ('A' if cached else 'B'))
        check(all(rows[i + 1][1] <= rows[i][1] + 1e-9 and rows[i + 1][2] <= rows[i][2] + 1e-9 for i in range(len(rows) - 1)),
              'plan %s: no stage adds cost' % ('A' if cached else 'B'))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    if args.check:
        return 0 if checks() else 1
    out = []
    tot = table_breakdown(out)
    table_levels(out)
    table_small(out)
    table_options(out, tot)
    table_plan(out, tot)
    print('\n'.join(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
