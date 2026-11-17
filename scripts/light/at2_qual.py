#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""AT2 qualification, phase 1 (offline, CPU): upstream Firestorm's sky pass against the filtered physical sky. Driver for
scripts/light/at2_qual.cpp; the report is doc/light/at2-qualification-phase1.md.

    python scripts/light/at2_qual.py --anchor                 # the upstream twin against the GPU reference (doc/light/atmosphere/qual/anchor-gpu.tsv)
    python scripts/light/at2_qual.py [--quick] --out ROWS.tsv  # every library sky and the built-ins; --quick: every 8th library sky
    python scripts/light/at2_qual.py --from ROWS.tsv           # summarise a saved run
    python scripts/light/at2_qual.py --from ROWS.tsv --panos   # also write the panorama PNGs (doc/light/atmosphere/qual/)
"""
import math
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
VARIANTS = ['plain', 'ns5', 'ns10', 'ns20']
VNAME = {'plain': 'table only', 'ns5': 'near-sun 5 deg', 'ns10': 'near-sun 10 deg', 'ns20': 'near-sun 20 deg'}
ANCH_TOL, ANCH_REL = 2e-4, 1e-4


def extend(row, moon=0.5):
    """at2f's 25-value row -> the 28-value row of at2_qual.cpp: + moon_brightness, the up light's elevation (sun by day, moon opposite by night),
    sun_up."""
    name, rest = row.split('\t')
    v = [float(x) for x in rest.split()]
    sun_el = math.degrees(math.asin(math.sin(v[22])))
    sun_up = 1 if sun_el >= 0 else 0
    light = sun_el if sun_up else -sun_el
    return name + '\t' + ' '.join(repr(x) for x in v + [moon, light, float(sun_up)])


def run(rows, extra, timeout):
    import os, shutil
    gpp = shutil.which('g++')
    suffix = '.exe' if os.name == 'nt' else ''
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'skies.txt').write_text('\n'.join(rows) + '\n', encoding='utf8')
        exe = tmp / ('at2_qual' + suffix)
        subprocess.run([gpp, '-std=c++20', '-O2', '-pthread', '-Wall', '-Wextra', '-Wno-unused-function', '-I', str(ROOT / 'indra' / 'newview'),
                        str(ROOT / 'scripts' / 'light' / 'at2_qual.cpp'), '-o', str(exe)], check=True, timeout=600)
        r = subprocess.run([str(exe)] + [str(tmp / a[1:]) if a.startswith('@') else a for a in extra] + [str(tmp / 'skies.txt')],
                           capture_output=True, text=True, timeout=timeout)
        if r.returncode:
            sys.stdout.write(r.stdout + r.stderr)
            raise SystemExit(r.returncode)
        return r.stdout


def anchor():
    ref = {}
    for l in (QUAL / 'anchor-gpu.tsv').read_text(encoding='utf8').splitlines():
        f = l.split('\t')
        ref[(f[0][:-4], f[1])] = [float(x) for x in f[-1].split()[1:]]
    names = sorted({k[0] for k in ref})
    by = {}
    for f in m.SKIES.glob('*.xml'):
        n = m.unquote(f.name)[:-4]
        if n in names:
            by[n] = f
    rows = []
    for n in names:
        d = m.llsd(m.ET.parse(by[n]).getroot()[0])
        rows.append(extend(m.row_text(n, m.values_of(d, d['sun_angle'], m.PBR))))
    out = run(rows, ['--anchor'], 600)
    worst, bad = 0.0, 0
    for l in out.splitlines():
        f = l.split('\t')
        got = [float(x) for x in f[3].split()]
        want = ref[(f[1], f[2])]
        err = max(abs(a - b) for a, b in zip(got, want))
        ok = all(abs(a - b) <= ANCH_TOL + ANCH_REL * abs(b) for a, b in zip(got, want))
        worst = max(worst, err)
        bad += not ok
        print(f"  {f[1]:42s} {f[2]:11s} twin {got[0]:.4f} {got[1]:.4f} {got[2]:.4f}  GPU {want[0]:.4f} {want[1]:.4f} {want[2]:.4f}  max err {err:.1e} {'OK' if ok else 'MISMATCH'}")
    print(f'{len(ref)} directions, {len(names)} skies; largest absolute difference {worst:.1e}; mismatches {bad}')
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


def parse(out):
    cols, data, undef = None, [], []
    for l in out.splitlines():
        f = l.split('\t')
        if f[0] == 'ROW' and cols is None:
            cols = f[1:]
        elif f[0] == 'ROW':
            data.append(dict(zip(cols, [f[1]] + [float(x) for x in f[2:]])))
        elif f[0] == 'UNDEF':
            undef.append(f[1])
    return data, undef


def reason(d, v):
    """Why a sky misses the thresholds, from where its worst cell is and what the physical sky has there."""
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
    if d['sun_up'] < 0.5:
        why.append('night')
    return ', '.join(why) or 'mid sky'


def summarise(data, undef, builtin_names, lines_total):
    lib = [d for d in data if d['name'] not in builtin_names]
    blt = [d for d in data if d['name'] in builtin_names]
    day = [d for d in lib if d['sun_up'] > 0.5]
    night = [d for d in lib if d['sun_up'] < 0.5]
    print(f'library skies measured {len(lib)} (day {len(day)}, night {len(night)}); undefined (max_y 0) {len([u for u in undef if u not in builtin_names])}; built-ins {len(blt)}')
    for p in sorted({int(d['path']) for d in lib}):
        print(f'  draw path {m.PATH_NAME[p]}: {sum(1 for d in lib if int(d["path"]) == p)}')
    print(f"{'':36s}{'median':>8s}{'p90':>8s}{'max':>8s}  mean<=2  mean<=5  worst<=2  worst<=5")
    for grp, name in ((lib, 'all'), (day, 'day'), (night, 'night')):
        for v in VARIANTS:
            mn = [d[v + '_mean'] for d in grp]
            wr = [d[v + '_worst'] for d in grp]
            print(f'{name:5s} {VNAME[v]:16s} mean   {med(mn):8.3f}{pct(mn, .9):8.3f}{max(mn):8.3f}  {sum(x <= 2 for x in mn):6d}  {sum(x <= 5 for x in mn):6d}')
            print(f'{name:5s} {VNAME[v]:16s} worst  {med(wr):8.3f}{pct(wr, .9):8.3f}{max(wr):8.3f}  {"":6s}  {"":6s}  {sum(x <= 2 for x in wr):6d}   {sum(x <= 5 for x in wr):6d}   (of {len(grp)})')
    for v in ('plain', 'ns10'):
        bad = sorted([d for d in lib if d[v + '_worst'] > 5 or d[v + '_mean'] > 2], key=lambda d: -d[v + '_worst'])
        print(f'\nfailing at mean 2 or worst 5, {VNAME[v]}: {len(bad)}')
        causes = {}
        for d in bad:
            causes.setdefault(reason(d, v), []).append(d['name'])
        for c, ns in sorted(causes.items(), key=lambda kv: -len(kv[1])):
            print(f'  {len(ns):4d}  {c}')
        for d in bad[:5]:
            print(f"    {d['name']}: mean {d[v + '_mean']:.2f}, worst {d[v + '_worst']:.1f} at {d[v + '_sep']:.1f} deg from the light, el {d[v + '_el']:.1f}; {reason(d, v)}")
    print('\nknown differences (our current shaders, L3 band, against upstream):')
    diff = [d for d in lib if d['cur_worst'] > 0.5]
    print(f'  skies where our current sky differs from upstream by more than dE 0.5 in some cell: {len(diff)} of {len(lib)}; with band weight above 0: {sum(1 for d in lib if d["band"] > 0)}')
    for d in sorted(diff, key=lambda d: -d['cur_worst'])[:12]:
        print(f"    {d['name']}: sun {d['sun_el']:.2f} band {d['band']:.3f} mean {d['cur_mean']:.2f} worst {d['cur_worst']:.1f} at {d['cur_sep']:.1f} deg el {d['cur_el']:.1f}")
    print('\nbuilt-ins:')
    for d in blt:
        print(f"  {d['name']}: sun {d['sun_el']:.2f}, {m.PATH_NAME[int(d['path'])]}; " + '; '.join(f"{VNAME[v]} mean {d[v + '_mean']:.2f} worst {d[v + '_worst']:.1f} ({d[v + '_sep']:.1f} deg, el {d[v + '_el']:.0f})" for v in VARIANTS) + f"; our current vs upstream worst {d['cur_worst']:.1f}")


def panos(data, rows, builtin_names, n_deg):
    lib = [d for d in data if d['name'] not in builtin_names]
    worst = [d['name'] for d in sorted([d for d in lib if d['sun_up'] > 0.5], key=lambda d: -d['ns10_worst'])[:10]]
    named = ['Default', 'Sunset', 'Foggy', '[TOR] MIDDAY - Pleasantrie', 'Alchemy Immortalis - Fog Lifting', 'Phototools - Midday', 'A-12AM', 'A-6PM']
    named = [n for n in named if any(d['name'] == n for d in lib)]
    chosen = list(dict.fromkeys(worst + named + [d['name'] for d in data if d['name'] in builtin_names and d['sun_up'] > 0.5]
                                + [d['name'] for d in data if d['name'] in builtin_names and d['sun_up'] < 0.5]))
    # a night sky and two typical ones from the library
    typ = [d['name'] for d in lib if d['sun_up'] > 0.5 and d['name'] not in chosen]
    chosen += typ[::max(1, len(typ) // 4)][:3]
    chosen += [d['name'] for d in lib if d['sun_up'] < 0.5 and d['name'] not in chosen][:1]
    sel = [r for r in rows if r.split('\t')[0] in chosen]
    QUAL.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        run(sel, ['--pano', str(out), str(n_deg)], 3600)
        for i, r in enumerate(sel):
            name = r.split('\t')[0]
            raw = (out / f'{i}.rgb').read_bytes()
            safe = ''.join(c if c.isalnum() or c in '-_' else '_' for c in name.replace('BUILT-IN: ', 'builtin_'))[:60]
            png(QUAL / f'pano-{safe}.png', 1536, 128, raw)
    print(f'{len(sel)} panoramas written to {QUAL}')
    return [r.split('\t')[0] for r in sel]


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
    if opt('--from'):
        out = Path(opt('--from')).read_text(encoding='utf8')
    else:
        out = run(rows, [], 7200)
    if opt('--out'):
        Path(opt('--out')).write_text(out, encoding='utf8')
    data, undef = parse(out)
    print(f'library skies read {len(lines)}, unreadable {len(unread)}, without sun_angle {len(nosun)}')
    summarise(data, undef, names, len(rows))
    for n in notes:
        print(n)
    if '--panos' in argv:
        panos(data, rows, names, 10.0)
    return 0


if __name__ == '__main__':
    sys.exit(main())
