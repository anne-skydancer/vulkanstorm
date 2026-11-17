#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""AT2 qualification, phase 1 (offline, CPU): can a table hold upstream Firestorm's sky pass? Driver for scripts/light/at2_qual.cpp; the
report is doc/light/at2-qualification-phase1.md. Needs g++ on PATH (the AT2-F tool's requirement too).

    python scripts/light/at2_qual.py --anchor                  # the upstream twin against the GPU reference (doc/light/atmosphere/qual/anchor-gpu.tsv)
    python scripts/light/at2_qual.py [--quick] [--physical] --out ROWS.tsv   # every library sky and the built-ins; --quick: every 8th library sky
    python scripts/light/at2_qual.py --from ROWS.tsv           # summarise a saved run (blending and dome runs are measured again)
    python scripts/light/at2_qual.py --out ROWS.tsv --panos    # also write the panorama PNGs (doc/light/atmosphere/qual/)
"""
import hashlib
import math
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import at2f_creator_grade as m  # noqa: E402

ROOT = m.ROOT
QUAL = ROOT / 'doc' / 'light' / 'atmosphere' / 'qual'
VARIANTS = ['a', 'b', 'c', 'ns5', 'ns10', 'ns20']
VNAME = {'a': 'a: product table (final linear)', 'b': 'b: coded-domain table', 'c': 'c: whole-sky A/B (exact glow)',
         'ns5': 'ns5: a + glow zone 5 deg', 'ns10': 'ns10: a + glow zone 10 deg', 'ns20': 'ns20: a + glow zone 20 deg'}
ANCH_TOL, ANCH_REL = 2e-4, 1e-4
EPS_DEG = 0.002   # |sun elevation| below this: the day / night branch is float rounding (upstream decides with the float32 quaternion's z)
BLEND_PAIRS = [('A-6AM', 'A-12PM'), ('A-12PM', 'A-6PM'), ('A-6PM', 'A-12AM'), ('A-12AM', 'A-6AM'), ('Default', 'Sunset')]
BLEND_T = (0.25, 0.5, 0.75)


def extend(row, moon=0.5, branch=None):
    """at2f's 25-value row -> the 28-value row of at2_qual.cpp: + moon_brightness, the up light's elevation (sun by day, moon opposite by
    night), sun_up. branch 'day' or 'night' forces the branch (for skies with the sun at the horizon, where it is rounding)."""
    name, rest = row.split('\t')
    v = [float(x) for x in rest.split()]
    sun_el = math.degrees(math.asin(math.sin(v[22])))
    sun_up = 1 if sun_el >= 0 else 0
    if branch:
        sun_up = 1 if branch == 'day' else 0
        light = abs(sun_el)
        name += f' @{branch}'
    else:
        light = sun_el if sun_up else -sun_el
    return name + '\t' + ' '.join(repr(x) for x in v + [moon, light, float(sun_up)])


def lerp_row(ra, rb, t, name):
    """The parameters of two extended rows lerped (LLSettingsSky::blend lerps the colours and scalars and slerps the sun's rotation; here the
    sun's elevation is lerped, in one plane): the blended sky M."""
    va = [float(x) for x in ra.split('\t')[1].split()]
    vb = [float(x) for x in rb.split('\t')[1].split()]
    out = [x + (y - x) * t for x, y in zip(va, vb)]
    ea, eb = math.asin(math.sin(va[22])), math.asin(math.sin(vb[22]))
    el = ea + (eb - ea) * t
    out[22] = el
    out[24] = va[24]
    sun_up = 1.0 if el >= 0 else 0.0
    d = math.degrees(el)
    out[26] = d if sun_up else -d
    out[27] = sun_up
    return name + '\t' + ' '.join(repr(x) for x in out)


def run(rows, extra, timeout):
    gpp = shutil.which('g++')
    if not gpp:
        raise SystemExit('g++ not found on PATH: this tool compiles at2_qual.cpp with it (MinGW-w64 on Windows)')
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'skies.txt').write_text('\n'.join(rows) + '\n', encoding='utf8')
        exe = tmp / ('at2_qual.exe' if sys.platform == 'win32' else 'at2_qual')
        subprocess.run([gpp, '-std=c++20', '-O2', '-pthread', '-Wall', '-Wextra', '-Wno-unused-function', '-I', str(ROOT / 'indra' / 'newview'),
                        str(ROOT / 'scripts' / 'light' / 'at2_qual.cpp'), '-o', str(exe)], check=True, timeout=600)
        r = subprocess.run([str(exe)] + extra + [str(tmp / 'skies.txt')], capture_output=True, text=True, timeout=timeout)
        if r.returncode:
            sys.stdout.write(r.stdout + r.stderr)
            raise SystemExit(r.returncode)
        first = r.stdout.split('\n', 1)[0].split('\t')
        if first[0] != 'ROWS' or int(first[1]) != len(rows):
            raise SystemExit(f'the program read {first[1:]} rows, {len(rows)} were written (a row split or dropped)')
        return r.stdout


def anchor():
    ref, header = {}, []
    for l in (QUAL / 'anchor-gpu.tsv').read_text(encoding='utf8').splitlines():
        if l.startswith('#'):
            header.append(l)
            continue
        f = l.split('\t')
        ref[(f[0][:-4], f[1])] = [float(x) for x in f[-1].split()[1:]]
    print('\n'.join(header))
    names = sorted({k[0] for k in ref})
    by = {m.unquote(f.name)[:-4]: f for f in m.SKIES.glob('*.xml')}
    rows = []
    for n in names:
        if n not in by:
            raise SystemExit(f'anchor sky "{n}" (anchor-gpu.tsv) is not in {m.SKIES}')
        d = m.llsd(m.ET.parse(by[n]).getroot()[0])
        rows.append(extend(m.row_text(n, m.values_of(d, d['sun_angle'], m.PBR))))
    out = run(rows, ['--anchor'], 600)
    worst, bad, n_dir, sat = 0.0, 0, 0, 0
    for l in out.splitlines():
        f = l.split('\t')
        if f[0] != 'ANCHOR':
            continue
        if (f[1], f[2]) not in ref:
            raise SystemExit(f'the twin made a sample ({f[1]}, {f[2]}) that anchor-gpu.tsv does not have; rerun at2_qual_anchor_gpu.py')
        got = [float(x) for x in f[3].split()]
        want = ref[(f[1], f[2])]
        err = max(abs(a - b) for a, b in zip(got, want))
        ok = all(abs(a - b) <= ANCH_TOL + ANCH_REL * abs(b) for a, b in zip(got, want))
        worst = max(worst, err)
        bad += not ok
        n_dir += 1
        sat += max(want) >= 4.9999
        print(f"  {f[1]:42s} {f[2]:11s} twin {got[0]:.4f} {got[1]:.4f} {got[2]:.4f}  GPU {want[0]:.4f} {want[1]:.4f} {want[2]:.4f}  max err {err:.1e} {'OK' if ok else 'MISMATCH'}")
    print(f'{n_dir} directions, {len(names)} skies ({sat} saturate at 5.0); largest absolute difference {worst:.1e}; mismatches {bad}')
    return 1 if bad else 0


def png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def fin(xs):
    return [x for x in xs if math.isfinite(x)]


def pct(xs, p):
    xs = sorted(fin(xs))
    return xs[min(len(xs) - 1, int(p * (len(xs) - 1) + 0.5))] if xs else float('nan')


def med(xs):
    xs = fin(xs)
    return statistics.median(xs) if xs else float('nan')


def parse(out, prefix):
    cols, data = None, []
    for l in out.splitlines():
        f = l.split('\t')
        if f[0] == prefix and cols is None:
            cols = f[1:]
        elif f[0] == prefix:
            data.append(dict(zip(cols, [f[1]] + [float(x) for x in f[2:]])))
    return data


def reason(d, v):
    """Where a sky's worst cell is, and what else is true of the sky (an association, not a cause: P never enters)."""
    why = []
    sep, el = d[v + '_sep'], d[v + '_el']
    if sep <= 10.0:
        why.append('near sun')
    if el <= 5.0:
        why.append('horizon')
    if d['starved'] > 0.5 or d['starved_zero'] > 0.5:
        why.append('starved channel (P)')
    if d['sat'] > 0.2:
        why.append('clamped at 5 over %.0f%% of the sky' % (100 * d['sat']))
    return ', '.join(why) or 'mid sky'


def counts(grp, v):
    wr = [d[v + '_worst'] for d in grp]
    mn = [d[v + '_mean'] for d in grp]
    return (sum(x <= 2 for x in wr), sum(x <= 5 for x in wr), sum(x <= 2 for x in mn), max(mn), med(wr), pct(wr, .9), max(wr))


def summarise(data, inputs, builtin_names):
    allrows = [d for d in data if d['name'] not in builtin_names]
    undef = [d for d in allrows if inputs[d['name'].split(' @')[0]]['my'] <= 0]
    lib = [d for d in allrows if d not in undef]
    branch = [d for d in lib if ' @' in d['name']]
    main = [d for d in lib if ' @' not in d['name']]
    horizon = [d for d in main if abs(d['sun_el']) < EPS_DEG]
    clean = [d for d in main if abs(d['sun_el']) >= EPS_DEG]
    blt = [d for d in data if d['name'] in builtin_names]
    print(f'library skies measured {len(main)} (day {sum(1 for d in main if d["sun_up"] > .5)}, night {sum(1 for d in main if d["sun_up"] < .5)}); '
          f'max_y 0, black by clampHDRRange, not measured as skies {len({d["name"].split(" @")[0] for d in undef})}; |sun elevation| <= 0.0011 deg: {len(horizon)} (listed apart, both branches); built-ins {len(blt)}')
    for p in sorted({int(d['path']) for d in main}):
        print(f'  draw path {m.PATH_NAME[p]}: {sum(1 for d in main if int(d["path"]) == p)}')
    print(f'  A / B self-check, largest |A + B haze_glow - pre| (relative): {max(d["ab_err"] for d in data):.1e}')
    mid = sorted(abs(d['sun_el']) for d in main if abs(d['sun_el']) >= EPS_DEG)
    print(f'  smallest |sun elevation| above the epsilon: {mid[0]:.3f} deg; skies with the epsilon <= |el| < 0.17 deg: {sum(1 for x in mid if x < 0.17)}')
    for name, grp in ((f'all {len(main)} (sign rule at the horizon)', main), (f'{len(clean)} away from the horizon', clean)):
        print(f'\n{name}:')
        print(f"{'variant':32s} worst<=2 worst<=5  mean<=2  max mean  worst median / p90 / max   view-mean (el 0-40) median / p90 / max")
        for v in VARIANTS:
            c = counts(grp, v)
            vm = [d[v + '_vmean'] for d in grp]
            print(f'{VNAME[v]:32s} {c[0]:7d} {c[1]:8d} {c[2]:8d} {c[3]:9.3f}  {c[4]:6.2f} / {c[5]:6.2f} / {c[6]:6.1f}   {med(vm):.3f} / {pct(vm, .9):.3f} / {max(vm):.3f}')
    print(f'\nthe {len(horizon)} skies with the sun at the horizon (|el| <= 0.0011 deg), each in both branches (day: the sun up; night: the moon, no glow):')
    for v in ('a', 'b', 'c'):
        for br in ('day', 'night'):
            g = [d for d in branch if d['name'].endswith('@' + br)]
            c = counts(g, v)
            print(f'  {VNAME[v]:32s} {br:5s} worst<=2 {c[0]:3d}  worst<=5 {c[1]:3d}  of {len(g)};  worst median {c[4]:.2f}  max {c[6]:.1f}')
    for v in ('c',):
        print(f'  {VNAME[v]}: skies failing worst <= 5 in either branch:')
        for d in sorted([d for d in branch if d[v + '_worst'] > 5], key=lambda d: -d[v + '_worst']):
            print(f"     {d['name']}: worst {d[v + '_worst']:.1f} at {d[v + '_sep']:.1f} deg from the light, el {d[v + '_el']:.1f}")
    for v in VARIANTS:
        bad = sorted([d for d in clean if d[v + '_worst'] > 5], key=lambda d: -d[v + '_worst'])
        print(f'\nfailing worst-cell dE 5, {VNAME[v]}: {len(bad)}')
        for d in bad[:5]:
            print(f"    {d['name']}: worst {d[v + '_worst']:.1f} at {d[v + '_sep']:.1f} deg from the light, el {d[v + '_el']:.1f}, mean {d[v + '_mean']:.3f}; {reason(d, v)}")
    print('\nknown differences (our current shaders, the L3 band, against upstream; accepted by the owner: it looks better):')
    inband = [d for d in main if 0 < d['band'] < 1]
    diff = [d for d in inband if d['cur_worst'] > 0.5]
    print(f'  band weight 0 < w < 1: {len(inband)} skies, {len(diff)} differ by more than dE 0.5; worst-cell median over the {len(diff)}: '
          f'{med([d["cur_worst"] for d in diff]):.1f}, over the {len(inband)}: {med([d["cur_worst"] for d in inband]):.1f}, max {max(d["cur_worst"] for d in inband):.1f}')
    print(f"  band 0: {sum(1 for d in main if d['band'] == 0)} skies, largest difference {max(d['cur_worst'] for d in main if d['band'] == 0):.3f}; "
          f"band 1 (night): {sum(1 for d in main if d['band'] == 1)} skies, largest {max(d['cur_worst'] for d in main if d['band'] == 1):.3f}")
    print('\nbuilt-ins:')
    for d in blt:
        print(f"  {d['name']}: sun {d['sun_el']:.2f}, {m.PATH_NAME[int(d['path'])]}; " + '; '.join(f"{v} mean {d[v + '_mean']:.2f} worst {d[v + '_worst']:.1f}" for v in VARIANTS)
              + f"; our current vs upstream worst {d['cur_worst']:.1f}")
    return main, clean, blt


def blend_report(rows_by_name, library_names):
    rows = []
    labels = []
    for a, b in BLEND_PAIRS:
        for t in BLEND_T:
            rows += [rows_by_name[a], rows_by_name[b], lerp_row(rows_by_name[a], rows_by_name[b], t, f'{a} -> {b}')]
            labels.append((a, b, t))
    res = []
    for t in BLEND_T:
        trip = [r for r, l in zip(zip(*[iter(rows)] * 3), labels) if l[2] == t]
        flat = [x for tr in trip for x in tr]
        out = run(flat, ['--blend', str(t)], 1800)
        for l in out.splitlines():
            f = l.split('\t')
            if f[0] == 'BLEND':
                res.append(f[1:])
    print('\nday-cycle blending: M = the two keyframes\' parameters lerped at t; the reference is upstream\'s formula at M. mean / worst dE of')
    print('  a-lerp: bilinear of the lerp of the two a (product) tables;  c-lerp: lerp of the A and B tables with M\'s exact glow;  c-rebuild: M\'s own c tables')
    print(f"{'pair':28s}{'t':>5s}{'sun el':>8s}   {'a-lerp':>14s}   {'c-lerp':>14s}   {'c-rebuild':>14s}")
    agg = {'a-lerp': [], 'c-lerp': [], 'c-rebuild': []}
    for r in sorted(res, key=lambda r: (r[0], float(r[1]))):
        print(f'{r[0]:28s}{float(r[1]):5.2f}{float(r[8]):8.1f}   {float(r[2]):6.2f} /{float(r[3]):6.1f}   {float(r[4]):6.2f} /{float(r[5]):6.1f}   {float(r[6]):6.2f} /{float(r[7]):6.1f}')
        agg['a-lerp'].append(float(r[3]))
        agg['c-lerp'].append(float(r[5]))
        agg['c-rebuild'].append(float(r[7]))
    for k, v in agg.items():
        print(f'  {k}: worst cell median {med(v):.1f}, max {max(v):.1f}, within dE 5: {sum(x <= 5 for x in v)} of {len(v)}')


def dome_report(rows, inputs, builtin_names):
    out = run(rows, ['--dome'], 7200)
    d = parse(out, 'DOME')
    d = [x for x in d if x['name'] not in builtin_names and ' @' not in x['name'] and inputs[x['name']]['my'] > 0 and abs(x['sun_el']) >= EPS_DEG]
    n = len(d)
    print(f'\nviewer dome geometry and per-vertex evaluation ({n} skies away from the horizon; the sun on a slice vertex and half a slice off; the geometry worst cell is the worse of the two):')
    gm = [x['geo_mean'] for x in d]
    gw = [x['geo_worst'] for x in d]
    print(f'  bench geometry (direction x 15000 + 50 m) against the viewer dome (pi/8 cap seen from 0.96 R), upstream per pixel in both: mean dE median {med(gm):.2f}, p90 {pct(gm, .9):.2f}, '
          f'max {max(gm):.2f}; mean above 2: {sum(x > 2 for x in gm)}; worst-cell median {med(gw):.1f}, above 5: {sum(x > 5 for x in gw)}')
    for tag, k in (('aligned', 'vert_aligned'), ('half-slice', 'vert_half')):
        vm = [x[k + '_mean'] for x in d]
        vw = [x[k + '_worst'] for x in d]
        print(f"  release/devel look (our skyV per vertex, WLSkyDetail 64) against upstream per pixel, sun {tag}: mean dE median {med(vm):.2f}, p90 {pct(vm, .9):.2f}, max {max(vm):.2f}; "
              f'worst-cell <= 2: {sum(x <= 2 for x in vw)}, <= 5: {sum(x <= 5 for x in vw)} of {n}; worst median {med(vw):.1f}, max {max(vw):.1f}')
    return d


def panos(data, rows, builtin_names, variant):
    lib = [d for d in data if d['name'] not in builtin_names and ' @' not in d['name']]
    day = [d for d in lib if d['sun_up'] > 0.5 and abs(d['sun_el']) >= EPS_DEG]
    worst = [d['name'] for d in sorted(day, key=lambda d: -d[variant + '_worst'])[:10]]
    named = [n for n in ('Default', 'Sunset', 'Foggy', 'Midday', 'A-6AM', 'A-3PM', '[TOR] MIDDAY - Pleasantrie', 'Alchemy Immortalis - Fog Lifting')
             if any(d['name'] == n for d in lib)]
    chosen = list(dict.fromkeys(worst + named + [d['name'] for d in data if d['name'] in builtin_names]))
    typ = [d['name'] for d in day if d['name'] not in chosen]
    chosen += typ[::max(1, len(typ) // 4)][:3]
    chosen += [d['name'] for d in lib if d['sun_up'] < 0.5 and d['name'] not in chosen][:1]
    sel = [r for r in rows if r.split('\t')[0] in chosen]
    QUAL.mkdir(parents=True, exist_ok=True)
    kept, seen = [], {}
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        run(sel, ['--pano', str(out), variant], 3600)
        for i, r in enumerate(sel):
            name = r.split('\t')[0]
            raw = (out / f'{i}.rgb').read_bytes()
            h = hashlib.sha256(raw).hexdigest()
            if h in seen:
                print(f'  panorama of {name}: identical to {seen[h]}, not written again')
                continue
            seen[h] = name
            safe = ''.join(c if c.isalnum() or c in '-_' else '_' for c in name.replace('BUILT-IN: ', 'builtin_'))[:60]
            png(QUAL / f'pano-{safe}.png', 1536, 128, raw)
            kept.append(name)
    # the legend: the heat scale as a bar, dE 0 to 20 left to right, ticks (white) at dE 2, 5, 10 and 20 above a 16-pixel gradient
    W = 640
    bar = bytearray()
    ticks = {int(W * x / 20.0) for x in (2, 5, 10)} | {W - 1}
    for y in range(24):
        for x in range(W):
            if y < 8:
                bar += bytes((255, 255, 255) if x in ticks else (0, 0, 0))
            else:
                px = bytearray(3)
                heat_color(20.0 * x / (W - 1), px)
                bar += px
    png(QUAL / 'pano-legend-heat-scale.png', W, 24, bytes(bar))
    print(f'{len(kept)} panoramas written to {QUAL.relative_to(ROOT).as_posix()} (strips: upstream | ours, variant {variant} | dE heat map; legend pano-legend-heat-scale.png)')
    return kept


def heat_color(de, out):
    stops = [0, 2, 5, 10, 20]
    col = [(0, 0, 0), (0, 60, 200), (255, 220, 0), (230, 20, 20), (255, 255, 255)]
    k = 0
    while k < 3 and de > stops[k + 1]:
        k += 1
    t = min(max((de - stops[k]) / (stops[k + 1] - stops[k]), 0.0), 1.0)
    for i in range(3):
        out[i] = int(round(col[k][i] + (col[k + 1][i] - col[k][i]) * t))


def main():
    argv = sys.argv[1:]
    if '--anchor' in argv:
        return anchor()
    quick = '--quick' in argv
    opt = lambda k: argv[argv.index(k) + 1] if k in argv else None
    lines, unread, nosun = m.library_rows(quick)
    b, notes = m.builtin_rows([])
    names = {r.split('\t')[0] for r in b}
    rows = [extend(r) for r in lines + b]
    by_name = {r.split('\t')[0]: r for r in rows}
    inputs = {}
    for r in rows:
        nme, rest = r.split('\t')
        v = [float(x) for x in rest.split()]
        inputs[nme] = {'my': v[17]}
    # the skies with the sun at the horizon, in both branches
    both = []
    for r in rows:
        if r.split('\t')[0] not in names:
            v = [float(x) for x in r.split('\t')[1].split()]
            if abs(math.degrees(math.asin(math.sin(v[22])))) < EPS_DEG:
                raw25 = r.split('\t')[0] + '\t' + ' '.join(repr(x) for x in v[:25])
                both += [extend(raw25, branch='day'), extend(raw25, branch='night')]
    for r in both:
        inputs[r.split('\t')[0].split(' @')[0]] = inputs[r.split('\t')[0].split(' @')[0]]
    all_rows = rows + both
    if opt('--from'):
        out = Path(opt('--from')).read_text(encoding='utf8')
    else:
        out = run(all_rows, ['--physical'] if '--physical' in argv else [], 7200)
    if opt('--out'):
        Path(opt('--out')).write_text(out, encoding='utf8')
    data = parse(out, 'ROW')
    print(f'library skies read {len(lines)}, unreadable {len(unread)}, without sun_angle {len(nosun)}')
    main_rows, clean, blt = summarise(data, inputs, names)
    full, _, _ = m.library_rows(False)   # the blending pairs come from the whole library even with --quick
    blend_report({r.split('	')[0]: r for r in (extend(x) for x in full + b)}, None)
    dome_report(rows, inputs, names)
    for n in notes:
        print(n)
    if '--panos' in argv:
        panos(data, rows, names, 'c')
    return 0


if __name__ == '__main__':
    sys.exit(main())
