#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Stage AT2-F of the atmosphere redesign R (decided 2026-11-17): the creator grade, measured offline over every shipped
Windlight sky (indra/newview/app_settings/windlight/skies). See scripts/light/at2f_creator_grade.cpp for what is measured and
doc/light/at2f-creator-grade.md for the results.

    python scripts/light/at2f_creator_grade.py [--quick] [--anchor] [--out FILE]

--quick measures every 8th sky (deterministic). --anchor checks the Windlight twin's zenith and horizon for Default.xml and Sunset.xml
against the GPU bench (doc/light/atmosphere/bench-2026-11-16.txt). --out writes the raw rows
(tab-separated) to FILE. Compiles the C++ with g++; no GPU, no viewer libraries.
"""
import statistics
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
SKIES = ROOT / 'indra' / 'newview' / 'app_settings' / 'windlight' / 'skies'
# bench-2026-11-16.txt (after 55a948fe33): today's zenith and horizon luminance at each file's own sun elevation
ANCHOR = {'Default.xml': (0.279, 1.711), 'Sunset.xml': (0.142, 0.422)}
ANCHOR_TOL = 0.005                              # the bench prints three decimals; the twin runs in double, the GPU in float


def llsd(node):
    tag = node.tag
    if tag == 'map':
        kids = list(node)
        return {kids[i].text: llsd(kids[i + 1]) for i in range(0, len(kids), 2)}
    if tag == 'array':
        return [llsd(k) for k in node]
    if tag == 'real':
        return float(node.text)
    if tag == 'integer':
        return int(node.text)
    return node.text


def row_of(f):
    d = llsd(ET.parse(f).getroot()[0])
    v = (d['sunlight_color'][:3] + d['ambient'][:3] + d['blue_density'][:3] + d['blue_horizon'][:3]
         + [d['haze_density'][0], d['haze_horizon'][0], d['cloud_shadow'][0], d['density_multiplier'][0], d['distance_multiplier'][0],
            d['max_y'][0]]
         + d['glow'][:3] + [d['gamma'][0], d.get('sun_angle', 0.0), d.get('east_angle', 0.0), 0.0])
    assert len(v) == 25
    return unquote(f.name)[:-4].replace('\t', ' ') + '\t' + ' '.join(repr(float(x)) for x in v)


def rows(quick, only=None):
    out, skipped = [], []
    for i, f in enumerate(sorted(SKIES.glob('*.xml'))):
        if only and unquote(f.name) != only:
            continue
        if quick and i % 8:
            continue
        try:
            out.append(row_of(f))
        except (KeyError, IndexError, AssertionError, ET.ParseError, TypeError):
            skipped.append(f.name)
    return out, skipped


def run(lines, extra):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        data = tmp / 'skies.txt'
        data.write_text('\n'.join(lines) + '\n', encoding='utf8')
        exe = tmp / 'at2f_creator_grade.exe'
        subprocess.run(['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'indra' / 'newview'),
                        str(ROOT / 'scripts' / 'light' / 'at2f_creator_grade.cpp'), '-o', str(exe)], check=True)
        r = subprocess.run([str(exe)] + extra + [str(data)], capture_output=True, text=True)
    if r.returncode:
        sys.stdout.write(r.stdout + r.stderr)
        raise SystemExit(r.returncode)
    return r.stdout


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p * (len(xs) - 1) + 0.5))]


def main():
    args = sys.argv[1:]
    if '--anchor' in args:
        ok = True
        for name, (z_ref, h_ref) in ANCHOR.items():
            lines, _ = rows(False, only=name)
            out = run(lines, ['--anchor'])
            sys.stdout.write(out)
            f = out.split('\t')
            zen, hor = float(f[3].split()[1]), float(f[4].split()[1])
            good = abs(zen - z_ref) <= ANCHOR_TOL and abs(hor - h_ref) <= ANCHOR_TOL
            ok = ok and good
            print(f"  GPU bench: zenith {z_ref}, horizon {h_ref} -> {'OK' if good else 'MISMATCH'}")
        return 0 if ok else 1
    quick = '--quick' in args
    lines, skipped = rows(quick)
    out = run(lines, [])
    if '--out' in args:
        Path(args[args.index('--out') + 1]).write_text(out, encoding='utf8')
    cols = None
    data = []
    for l in out.splitlines():
        f = l.split('\t')
        if f[0] == 'ROW' and cols is None:
            cols = f[1:]
        elif f[0] == 'ROW':
            data.append(dict(zip(cols, [f[1]] + [float(x) for x in f[2:]])))
    nights = [l.split('\t')[1] for l in out.splitlines() if l.startswith('NIGHT\t')]
    n = len(data)
    print(f'skies read {len(lines)}, skipped (unreadable) {len(skipped)}, night or twilight (not measured) {len(nights)}, measured {n}')
    for key, label in (('dE_gain', 'physics alone, best gain'), ('dE_tint', 'physics alone, best gain + tint'),
                       ('dE_lut', 'graded sky view (mean over the sky)'), ('dE_lut_max', 'graded sky view (worst 1x5 deg cell)'),
                       ('dE_lut_phys', 'physical sky view, its own table error (mean)'),
                       ('dE_lut_phys_max', 'physical sky view, its own table error (worst)')):
        xs = [d[key] for d in data]
        print(f'{label:46s} median {statistics.median(xs):6.2f}  p90 {pct(xs, 0.9):6.2f}  max {max(xs):6.2f}  '
              f'<= 2: {sum(1 for x in xs if x <= 2.0)} of {n}')
    near = [d for d in data if d['dE_lut_max'] > 2.0]
    print(f"skies whose worst sky-view cell is above dE 2: {len(near)}; of them within 10 degrees of the sun: "
          f"{sum(1 for d in near if d['lut_sep'] <= 10.0)}, within 1 degree of the horizon: {sum(1 for d in near if d['lut_el'] <= 1.0)}")
    st = [d for d in data if d['starved'] > 0.5]
    print(f"colour-starved physical skies (a channel below 1e-3 of the strongest over most of the sky): {len(st)} of {n}")
    ha = [d['ap_remainder'] for d in data]
    print(f"haze on objects, share a 32x-bounded ratio cannot carry: median {statistics.median(ha):.4f}, p90 {pct(ha, 0.9):.4f}, "
          f"max {max(ha):.3f}; skies above 1 %: {sum(1 for x in ha if x > 0.01)}")
    ks = [d['k'] for d in data]
    print(f"energy factor k (graded = k x creator): median {statistics.median(ks):.4f} (1/{1 / statistics.median(ks):.2f}), "
          f"p10 {pct(ks, 0.1):.4f}, p90 {pct(ks, 0.9):.4f}")
    rng = [d['grade_hi'] / max(d['grade_lo'], 1e-12) for d in data]
    print(f"grade range hi/lo in luminance: median {statistics.median(rng):.2f}, p90 {pct(rng, 0.9):.2f}, max {max(rng):.3g}")
    tm = [d['tint_max'] for d in data]
    print(f"grade's largest per-channel tint: median {statistics.median(tm):.2f}, p90 {pct(tm, 0.9):.2f}, max {max(tm):.3g}")
    over = [d for d in data if d['card'] > 1.0 or d['card_aureole'] > 2.0]
    aur = [d for d in data if 1.0 < d['card_aureole'] <= 2.0]
    print(f"white card on the graded sky: {len(over)} over the rule, {len(aur)} aureole exceedances within 2x; "
          f"largest outside the aureole {max(d['card'] for d in data):.3f}, inside {max(d['card_aureole'] for d in data):.3f}")
    ws = [d['wall_sun'] for d in data]
    wa = [d['wall_away'] for d in data]
    print(f"vertical wall, graded / physical: facing the sun median {statistics.median(ws):.2f} (p10 {pct(ws, 0.1):.2f}, p90 {pct(ws, 0.9):.2f}); "
          f"facing away median {statistics.median(wa):.2f} (p10 {pct(wa, 0.1):.2f}, p90 {pct(wa, 0.9):.2f})")
    for d in data:
        if d['name'] in ('Default', 'Foggy', 'Sunset'):
            print(f"  {d['name']}: sun {d['sun_el']:.1f}, k {d['k']:.4f}, dE gain {d['dE_gain']:.1f}, tint {d['dE_tint']:.1f}, "
                  f"sky view {d['dE_lut']:.2f} (worst {d['dE_lut_max']:.1f} at {d['lut_sep']:.1f} deg from the sun), "
                  f"card {d['card']:.3f}/{d['card_aureole']:.3f}")
    worst = sorted(data, key=lambda d: -d['dE_tint'])[:12]
    print('furthest from physics (dE after gain + tint):')
    for d in worst:
        print(f"  {d['name']}: {d['dE_tint']:.1f} (gain only {d['dE_gain']:.1f}), grade range {d['grade_hi'] / max(d['grade_lo'], 1e-12):.3g}")
    if over:
        print('over the white-card rule on the graded sky:')
        for d in sorted(over, key=lambda d: -max(d['card'], d['card_aureole'] / 2.0)):
            print(f"  {d['name']}: outside the aureole {d['card']:.3f}, aureole {d['card_aureole']:.3f}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
