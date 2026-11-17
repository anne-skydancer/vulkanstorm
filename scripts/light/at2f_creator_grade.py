#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Stage AT2-F of the atmosphere redesign R (decided 2026-11-17): each Windlight sky as a filter on the physical sky, measured offline
over every shipped library sky (indra/newview/app_settings/windlight/skies) and, as NAMED extra rows outside the library figures, over
the built-in skies. See scripts/light/at2f_creator_grade.cpp for what is measured and doc/light/at2f-creator-grade.md for the results.

    python scripts/light/at2f_creator_grade.py [--quick] [--builtins] [--llsd FILE ...] [--out FILE]
    python scripts/light/at2f_creator_grade.py --from ROWS.tsv [--builtins]    (summarise a saved run without measuring again)
    python scripts/light/at2f_creator_grade.py --anchor

--quick measures every 8th sky of the name-sorted library (deterministic). --builtins adds, as named rows: the code default
(LLSettingsSky::defaults(), sun at 80 degrees, read through the LLSettingsSky getters' fallbacks) and Midday (Legacy) and Midnight from
the local asset cache (%LOCALAPPDATA%/Vulkanstorm_x64/cache). --llsd FILE adds one more sky, an LLSD notation or XML sky, as a named
row. --out writes the raw rows (tab-separated) to FILE. --anchor checks the Windlight twin against the GPU bench's figures
(scripts/light/at2f_anchor_gpu.py makes them) for six skies, and prints the physical sky's figures beside the bench's. Compiles the C++
with g++; no GPU, no viewer libraries.
"""
import math
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
SKIES = ROOT / 'indra' / 'newview' / 'app_settings' / 'windlight' / 'skies'
CACHE = Path(os.environ.get('LOCALAPPDATA', '')) / 'Vulkanstorm_x64' / 'cache'
# the draw paths (see the C++ enum Path): the key reflection_probe_ambiance absent, nonzero, or present and 0
CLASSIC, PBR, PBR_ZERO = 0, 1, 2
PATH_NAME = {CLASSIC: 'classic', PBR: 'PBR', PBR_ZERO: 'PBR, ambiance 0'}
# the cached built-ins: asset UUID -> label
CACHED = {'6c83e853-e7f8-cad7-8ee6-5f31c453721c': 'Midday (Legacy)', '8a01b97a-cb20-c1ea-ac63-f7ea84ad0090': 'Midnight'}
BUILTIN_PREFIX = 'BUILT-IN: '
# GPU anchor: today's (PBR path) and the bench's physical luminance at the zenith, the horizon and near the sun, and the bench's physical
# sky irradiance, from scripts/light/at2f_anchor_gpu.py (bench_atmosphere.py on the RTX 5070 Ti, 2026-11-17)
ANCHOR = {
    'Default': ((0.2786, 0.0782), (1.7111, 0.1573), (0.8024, 0.1265), 0.0962),
    'Sunset': ((0.1421, 0.0605), (0.4221, 0.1767), (2.0457, 0.4331), 0.1113),
    '[TOR] MIDDAY - Salmon': ((7.7388, 0.0820), (4.5206, 0.2512), (83.7561, 0.0933), 0.1064),
    '[TOR] MIDDAY - Pleasantrie': ((0.0757, 0.0945), (0.9377, 0.2212), (0.2566, 0.1328), 0.1191),
    'Alchemy Immortalis - Fog Lifting': ((0.2257, 0.0140), (1.9343, 0.1044), (2.7539, 0.1094), 0.0282),
    'Foggy': ((85.9320, 0.0143), (1.4920, 0.0537), (85.9320, 0.0143), 0.0179),
}
ANCHOR_TOL = 0.005                              # the bench prints four decimals; the twin runs in double, the GPU in float
ANCHOR_REL = 1e-3


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


def parse_notation(text):
    """A minimal LLSD notation reader (maps, arrays, strings, r/i/u values, booleans), enough for the cached sky assets."""
    s = text[text.index('\n') + 1:] if text.startswith('<?') else text
    pos = 0

    def skip():
        nonlocal pos
        while pos < len(s) and s[pos] in ' \t\r\n,':
            pos += 1

    def value():
        nonlocal pos
        skip()
        c = s[pos]
        if c == '{':
            pos += 1
            out = {}
            while True:
                skip()
                if s[pos] == '}':
                    pos += 1
                    return out
                k = value()
                skip()
                assert s[pos] == ':'
                pos += 1
                out[k] = value()
        if c == '[':
            pos += 1
            out = []
            while True:
                skip()
                if s[pos] == ']':
                    pos += 1
                    return out
                out.append(value())
        if c == "'":
            pos += 1
            start = pos
            while s[pos] != "'":
                pos += 2 if s[pos] == '\\' else 1
            pos += 1
            return s[start:pos - 1]
        start = pos
        while pos < len(s) and s[pos] not in ',]}':
            pos += 1
        tok = s[start:pos].strip()
        if tok[:1] == 'r':
            return float(tok[1:])
        if tok[:1] == 'i':
            return int(tok[1:])
        return tok

    return value()


def sc(x):
    return x[0] if isinstance(x, list) else x


def row_text(name, v):
    assert len(v) == 25
    return name.replace('\t', ' ') + '\t' + ' '.join(repr(float(x)) for x in v)


def values_of(d, sun_el_rad, path):
    """The 25 values of a sky for the C++ (see at2f_creator_grade.cpp's parse): library XML keys are flat, LLSD assets keep the haze
    fields in legacy_haze; a missing field takes the LLSettingsSky getter's fallback (llsettingssky.cpp:1228-1234)."""
    lh = d.get('legacy_haze', {}) if isinstance(d.get('legacy_haze'), dict) else {}

    def get(key, default):
        return d[key] if key in d else lh.get(key, default)

    return (list(d['sunlight_color'][:3]) + list(get('ambient', [0.25, 0.25, 0.25])[:3])
            + list(get('blue_density', [0.2447, 0.4487, 0.7599])[:3]) + list(get('blue_horizon', [0.4954, 0.4954, 0.6399])[:3])
            + [sc(get('haze_density', 0.7)), sc(get('haze_horizon', 0.19)), sc(d['cloud_shadow']),
               sc(get('density_multiplier', 0.0001)), sc(get('distance_multiplier', 0.8)), sc(d['max_y'])]
            + list(d['glow'][:3]) + [sc(d['gamma']), sun_el_rad, 0.0, path])


def library_rows(quick):
    """(rows, unreadable, no_sun_angle): the name-sorted library, every 8th sky with --quick."""
    out, bad, nosun = [], [], []
    files = sorted(SKIES.glob('*.xml'), key=lambda f: unquote(f.name))
    for i, f in enumerate(files):
        if quick and i % 8:
            continue
        name = unquote(f.name)[:-4]
        try:
            d = llsd(ET.parse(f).getroot()[0])
            if 'sun_angle' not in d:
                nosun.append(name)
                continue
            if 'reflection_probe_ambiance' in d:
                path = PBR if sc(d['reflection_probe_ambiance']) != 0 else PBR_ZERO
            else:
                path = CLASSIC
            out.append(row_text(name, values_of(d, d['sun_angle'], path)))
        except (KeyError, IndexError, AssertionError, ET.ParseError, TypeError, ValueError):
            bad.append(name)
    return out, bad, nosun


def sun_elevation_of_rotation(q):
    """The elevation (radians) of the sun direction x_axis * q (LLQuaternion x y z w): the z of the rotated unit x."""
    x, y, z, w = q
    return math.asin(max(-1.0, min(1.0, 2.0 * (x * z - w * y))))


def code_default():
    """LLSettingsSky::defaults() (llsettingssky.cpp:883) as the getters read it: no legacy haze (the getters' fallbacks), sun at 80 degrees
    (position 0), reflection_probe_ambiance present and 0 (a third path)."""
    d = {'sunlight_color': [0.7342, 0.7815, 0.8999], 'cloud_shadow': 0.2699, 'max_y': 1605.0, 'gamma': 1.0,
         'glow': [5.0, 0.0010, -0.4799]}
    return row_text(BUILTIN_PREFIX + 'code default (LLSettingsSky::defaults)', values_of(d, math.radians(80.0), PBR_ZERO))


def llsd_file_row(path, label=None):
    text = Path(path).read_text(encoding='utf8', errors='replace')
    if text.lstrip().startswith(('<?xml', '<llsd')):
        d = llsd(ET.fromstring(text.split('?>', 1)[-1] if text.startswith('<?xml') else text)[0])
    else:
        d = parse_notation(text)
    p = PBR_ZERO if 'reflection_probe_ambiance' in d and sc(d['reflection_probe_ambiance']) == 0 else (
        PBR if 'reflection_probe_ambiance' in d else CLASSIC)
    if 'sun_angle' in d:
        el = d['sun_angle']
    else:
        el = sun_elevation_of_rotation(d['sun_rotation'])
    return row_text(BUILTIN_PREFIX + (label or d.get('name', Path(path).stem)), values_of(d, el, p))


def builtin_rows(extra):
    rows = [code_default()]
    notes = []
    for uuid, label in CACHED.items():
        f = CACHE / uuid[0] / f'sl_cache_{uuid}_0.asset'
        if f.exists():
            rows.append(llsd_file_row(f, f'{label} (cache {uuid[:8]})'))
        else:
            notes.append(f'{label}: cache file {f.name} not found')
    for f in extra:
        rows.append(llsd_file_row(f))
    return rows, notes


def run(lines, extra, timeout):
    gpp = shutil.which('g++')
    if not gpp:
        print('g++ not found on PATH', file=sys.stderr)
        raise SystemExit(2)
    suffix = '.exe' if os.name == 'nt' else ''
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        data = tmp / 'skies.txt'
        data.write_text('\n'.join(lines) + '\n', encoding='utf8')
        exe = tmp / ('at2f_creator_grade' + suffix)
        subprocess.run([gpp, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'indra' / 'newview'),
                        str(ROOT / 'scripts' / 'light' / 'at2f_creator_grade.cpp'), '-o', str(exe)], check=True, timeout=600)
        try:
            r = subprocess.run([str(exe)] + extra + [str(data)], capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            print(f'the measuring program ran over {timeout} s and was stopped', file=sys.stderr)
            raise SystemExit(3)
    if r.returncode:
        sys.stdout.write(r.stdout + r.stderr)
        raise SystemExit(r.returncode)
    return r.stdout


def fin(xs):
    return [x for x in xs if math.isfinite(x)]


def pct(xs, p):
    xs = sorted(fin(xs))
    return xs[min(len(xs) - 1, int(p * (len(xs) - 1) + 0.5))] if xs else float('nan')


def med(xs):
    xs = fin(xs)
    return statistics.median(xs) if xs else float('nan')


def anchor():
    ok = True
    names = list(ANCHOR)
    by_name = {}
    for f in SKIES.glob('*.xml'):
        n = unquote(f.name)[:-4]
        if n in ANCHOR:
            by_name[n] = f
    lines = []
    for n in names:
        d = llsd(ET.parse(by_name[n]).getroot()[0])
        lines.append(row_text(n, values_of(d, d['sun_angle'], PBR)))
    out = run(lines, ['--anchor'], 600)
    print('twin (W: today, P: physical) against the GPU bench (W: today, P: the bench\'s physical mapping), PBR path, per direction')
    for l in out.splitlines():
        f = l.split('\t')
        n = f[1]
        z_ref, h_ref, s_ref, e_ref = ANCHOR[n]
        got = [tuple(float(x) for x in f[3 + i].split()) for i in range(3)]
        e_p = float(f[6]) / math.pi   # the bench prints E / pi (the probes' cosine-convolved radiance)
        good = True
        cells = []
        for label, g, ref in zip(('zenith', 'horizon', 'near-sun'), got, (z_ref, h_ref, s_ref)):
            this = abs(g[0] - ref[0]) <= ANCHOR_TOL + ANCHOR_REL * ref[0]
            good = good and this
            cells.append(f'{label} W {g[0]:.4f} (bench {ref[0]:.4f}) P {g[1]:.4f} (bench {ref[1]:.4f}, x{g[1] / ref[1] if ref[1] else float("nan"):.2f})')
        ok = ok and good
        print(f'  {n}: {f[2]}; ' + '; '.join(cells) + f'; P irradiance/pi {e_p:.4f} (bench {e_ref:.4f}, x{e_p / e_ref:.2f})  W {"OK" if good else "MISMATCH"}')
    return 0 if ok else 1


def parse_args(argv):
    opts = {'quick': False, 'builtins': False, 'anchor': False, 'out': None, 'from': None, 'llsd': []}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('--quick', '--builtins', '--anchor'):
            opts[a[2:]] = True
        elif a in ('--out', '--llsd', '--from'):
            if i + 1 >= len(argv) or argv[i + 1].startswith('--'):
                print(f'{a} needs a value', file=sys.stderr)
                raise SystemExit(2)
            if a in ('--out', '--from'):
                opts[a[2:]] = argv[i + 1]
            else:
                opts['llsd'].append(argv[i + 1])
            i += 1
        else:
            print(f'unknown argument {a}\n{__doc__}', file=sys.stderr)
            raise SystemExit(2)
        i += 1
    return opts


def line_of(d):
    return (f"{d['name']}: sun {d['sun_el']:.1f}, {PATH_NAME[int(d['path'])]}, k {d['k']:.4f}"
            f" (x{1 / d['k'] if d['k'] > 0 else float('inf'):.1f} physical light), dE gain {d['dE_gain']:.1f}, tint {d['dE_tint']:.1f}, "
            f"product {d['dE_prod']:.2f} (worst {d['dE_prod_max']:.1f} at {d['prod_sep']:.1f} deg from the sun), ratio lin {d['dE_rlin']:.2f} "
            f"(worst {d['dE_rlin_max']:.1f}), ratio log {d['dE_rlog']:.2f} (worst {d['dE_rlog_max']:.1f}), "
            f"card on W {d['cardW']:.3f}/{d['cardW_aur']:.3f}, on kW {d['cardC']:.3f}/{d['cardC_aur']:.3f}")


def summarise(out, lines, unread, nosun, inputs, builtin_names):
    cols = None
    data = []
    nights, undef = [], []
    for l in out.splitlines():
        f = l.split('\t')
        if f[0] == 'ROW' and cols is None:
            cols = f[1:]
        elif f[0] == 'ROW':
            data.append(dict(zip(cols, [f[1]] + [float(x) for x in f[2:]])))
        elif f[0] == 'NIGHT':
            nights.append(f[1])
        elif f[0] == 'UNDEF':
            undef.append(f[1])
    lib = [d for d in data if d['name'] not in builtin_names]
    blt = [d for d in data if d['name'] in builtin_names]
    lib_nights = [n for n in nights if n not in builtin_names]
    lib_undef = [n for n in undef if n not in builtin_names]
    n = len(lib)
    print(f'library skies read {len(lines) - len(builtin_names)}, unreadable {len(unread)}, without sun_angle (counted, not measured) {len(nosun)}, '
          f'night or twilight (sun below the horizon, not measured) {len(lib_nights)}, max_y 0 (undefined, not measured) {len(lib_undef)}, '
          f'measured {n}')
    for p in sorted({int(d['path']) for d in lib}):
        print(f"  draw path {PATH_NAME[p]}: {sum(1 for d in lib if int(d['path']) == p)} measured")
    nb = sum(1 for d in lib if d['blend'] > 0)
    print(f'skies whose sun/moon band weight is above 0 (sun elevation below about 0.16 degrees): {nb}, '
          f"largest {max((d['blend'] for d in lib), default=0):.3f}")
    print('(statistics drop non-finite values; dropped: ' + str(sum(1 for d in lib for k, v in d.items() if k != 'name' and not math.isfinite(v))) + ')')
    print(f"{'':54s}{'median':>8s}{'p90':>8s}{'max':>8s}   within dE 2")
    for key, label in (('dE_gain', 'physics alone, best gain'), ('dE_tint', 'physics alone, best gain + tint'),
                       ('dE_prod', 'filter table, product: mean over the sky'), ('dE_prod_max', 'filter table, product: worst cell'),
                       ('dE_rlin', 'filter table, ratio (linear): mean'), ('dE_rlin_max', 'filter table, ratio (linear): worst cell'),
                       ('dE_rlog', 'filter table, ratio (log): mean'), ('dE_rlog_max', 'filter table, ratio (log): worst cell'),
                       ('dE_phys', 'physical sky view, its own table error: mean'), ('dE_phys_max', 'physical sky view, its own table error: worst')):
        xs = fin([d[key] for d in lib])
        print(f'{label:54s}{med(xs):8.2f}{pct(xs, 0.9):8.2f}{max(xs):8.2f}   {sum(1 for x in xs if x <= 2.0)} of {len(xs)}')
    for key, sepkey, label in (('dE_prod_max', 'prod_sep', 'product'), ('dE_rlin_max', 'rlin_sep', 'ratio (linear)'), ('dE_rlog_max', 'rlog_sep', 'ratio (log)')):
        near = [d for d in lib if d[key] > 2.0]
        flat = sum(1 for d in near if d[sepkey] <= 2.56)
        mid = sum(1 for d in near if 2.56 < d[sepkey] <= 10.0)
        print(f"{label}: skies whose worst cell is above dE 2: {len(near)}; within the flat zone (2.56 deg of the sun) {flat}, 2.56 to 10 deg {mid}, "
              f"beyond 10 deg {len(near) - flat - mid}; worst cell above dE 5: {sum(1 for d in lib if d[key] > 5.0)}, above dE 10: {sum(1 for d in lib if d[key] > 10.0)}")
    for key, label in (('dE_rlin_max', 'ratio (linear)'), ('dE_rlog_max', 'ratio (log)')):
        print(f'worst {label} cells: ' + '; '.join(f"{d['name']} {d[key]:.0f}" for d in sorted(lib, key=lambda d: -d[key])[:6]))

    # energy, per draw path
    print('energy factor k (cast light = k x W), and "x N physical light" = 1 / k (the creator\'s sky holds N times the physical light):')
    for p in sorted({int(d['path']) for d in lib}):
        ks = [d['k'] for d in lib if int(d['path']) == p and d['k'] > 0]
        allp = [d for d in lib if int(d['path']) == p]
        print(f"  {PATH_NAME[p]}: {len(allp)} skies, k > 0 for {len(ks)}; k median {med(ks):.4f}, p10 {pct(ks, 0.1):.4f}, p90 {pct(ks, 0.9):.4f}; "
              f"x N median {med([1 / k for k in ks]):.1f} (p10 {pct([1 / k for k in ks], 0.1):.1f}, p90 {pct([1 / k for k in ks], 0.9):.1f}); "
              f"below 0.1: {sum(1 for k in ks if k < 0.1)}, below 0.01: {sum(1 for k in ks if k < 0.01)}, below 0.001: {sum(1 for k in ks if k < 0.001)}")

    # the zero-k skies, partitioned from the inputs
    zero = [d for d in lib if d['k'] == 0.0]
    total0 = len(zero) + len(lib_undef)
    print(f'skies with k = 0 (or undefined): {total0} of {n + len(lib_undef)} (measured and undefined)')
    print(f'  max_y 0 (undefined, no direction): {len(lib_undef)}')
    cats = {}
    for d in zero:
        i = inputs[d['name']]
        sun0 = max(i['sun']) <= 0.0
        dm0 = i['dm'] <= 0.0
        wpos, ppos = d['EW'] > 0.0, d['EP'] > 0.0
        cats.setdefault((dm0, sun0, wpos, ppos), []).append(d['name'])
    for (dm0, sun0, wpos, ppos), names in sorted(cats.items(), key=lambda kv: -len(kv[1])):
        print(f"  {len(names):3d}: density_multiplier 0 {'yes' if dm0 else 'no '}, sunlight 0 {'yes' if sun0 else 'no '}, today's sky "
              f"{'lit' if wpos else 'black'}, physical sky {'lit' if ppos else 'black'}")
    n_w_p0 = sum(len(v) for (dm0, sun0, wpos, ppos), v in cats.items() if wpos and not ppos)
    n_w_p0_sun = sum(len(v) for (dm0, sun0, wpos, ppos), v in cats.items() if wpos and not ppos and sun0)
    print(f"  W > 0 and P = 0 (a lit creator sky where physics gives nothing): {n_w_p0}, of them with sunlight 0: {n_w_p0_sun}, "
          f"with sunlight but no scattering from the mapping: {n_w_p0 - n_w_p0_sun}")
    n_both_black = sum(len(v) for (dm0, sun0, wpos, ppos), v in cats.items() if not wpos)
    print(f"  today's sky also black (W = 0): {n_both_black}; of them density_multiplier 0: "
          f"{sum(len(v) for (dm0, sun0, wpos, ppos), v in cats.items() if not wpos and dm0)}, sunlight 0: "
          f"{sum(len(v) for (dm0, sun0, wpos, ppos), v in cats.items() if not wpos and sun0)}")
    every = [d['name'] for d in zero] + lib_undef
    print(f"  the {len(every)} together, flags overlap: sunlight 0 {sum(1 for x in every if max(inputs[x]['sun']) <= 0)}, density_multiplier 0 "
          f"{sum(1 for x in every if inputs[x]['dm'] <= 0)}, max_y 0 {len(lib_undef)}; "
          f"neither sunlight 0, density_multiplier 0 nor max_y 0: {sum(1 for x in every if max(inputs[x]['sun']) > 0 and inputs[x]['dm'] > 0 and inputs[x]['my'] > 0)}")
    print(f"  in the whole library (measured, incl. night): sunlight 0 {sum(1 for i in inputs.values() if max(i['sun']) <= 0)}, density_multiplier 0 "
          f"{sum(1 for i in inputs.values() if i['dm'] <= 0)}, max_y 0 {sum(1 for i in inputs.values() if i['my'] <= 0)}")

    # starvation and the haze on objects
    st = [d for d in lib if d['starved'] > 0.5]
    sz = [d for d in lib if d['starved_zero'] > 0.5]
    print(f"colour-starved physical skies (a channel below 1e-3 of the strongest over most of the sky, P not 0): {len(st)} of {n}; "
          f"P exactly 0 over most of the sky: {len(sz)}; either: {len({d['name'] for d in st} | {d['name'] for d in sz})}")
    for key, b in (('ap8', 8), ('ap32', 32), ('ap128', 128)):
        ha = [d[key] for d in lib]
        print(f"haze on objects, share of W's light that a ratio bounded at {b}x the median W / P cannot carry: median {med(ha):.4f}, "
              f"p90 {pct(ha, 0.9):.4f}, max {max(ha):.3f}; skies above 1 %: {sum(1 for x in ha if x > 0.01)}, above 10 %: {sum(1 for x in ha if x > 0.1)}")

    # the filter's size
    ok = [d for d in lib if d['grade_hi'] > 0]
    rng = [d['grade_hi'] / d['grade_lo'] for d in ok]
    print(f"filter G = W / P (samples where both are above 1e-6; {len(ok)} skies): range hi/lo median {med(rng):.2f}, p90 {pct(rng, 0.9):.2f}, "
          f"max {max(rng):.3g}; share of the sky with W lit and P not: median {med([d['grade_undef'] for d in lib]):.3f}, "
          f"skies above 10 %: {sum(1 for d in lib if d['grade_undef'] > 0.1)}")
    tm = [d['tint_max'] for d in ok]
    print(f"filter's largest per-channel tint (channels above 1e-3 of the luminance in W and P): median {med(tm):.2f}, p90 {pct(tm, 0.9):.2f}, max {max(tm):.3g}")

    # the white card
    kpos = [d for d in lib if d['k'] > 0]
    for label, subset, kc, ka in (('on W (the visible sky, today\'s look; creator intent wins)', lib, 'cardW', 'cardW_aur'),
                                  ('on W, k > 0 skies only', kpos, 'cardW', 'cardW_aur'),
                                  ('on k x W (the cast light), k > 0 skies', kpos, 'cardC', 'cardC_aur')):
        over = [d for d in subset if d[kc] > 1.0 or d[ka] > 2.0]
        aur = [d for d in subset if 1.0 < d[ka] <= 2.0 and d not in over]
        print(f"white card {label}: {len(over)} of {len(subset)} over the rule; a further {len(aur)} use only the aureole's 2x allowance; "
              f"largest outside the aureole {max((d[kc] for d in subset), default=0):.3f}, inside {max((d[ka] for d in subset), default=0):.3f}")
    over_c = [d for d in kpos if d['cardC'] > 1.0 or d['cardC_aur'] > 2.0]
    if over_c:
        print('over the white-card rule on the cast light (k x W):')
        for d in sorted(over_c, key=lambda d: -max(d['cardC'], d['cardC_aur'] / 2.0)):
            print(f"  {d['name']}: outside the aureole {d['cardC']:.3f}, aureole {d['cardC_aur']:.3f}")
    ws = [d['wall_sun'] for d in kpos]
    wa = [d['wall_away'] for d in kpos]
    print(f"vertical wall, cast light / physical (k > 0 skies): facing the sun median {med(ws):.2f} (p10 {pct(ws, 0.1):.2f}, p90 {pct(ws, 0.9):.2f}); "
          f"facing away median {med(wa):.2f} (p10 {pct(wa, 0.1):.2f}, p90 {pct(wa, 0.9):.2f})")

    print('named library skies:')
    for d in lib:
        if d['name'] in ('Default', 'Foggy', 'Sunset'):
            print('  ' + line_of(d))
    worst = sorted(lib, key=lambda d: -d['dE_tint'])[:12]
    print('furthest from physics (dE after gain + tint):')
    for d in worst:
        rg = f"{d['grade_hi'] / d['grade_lo']:.3g}" if d['grade_hi'] > 0 else 'n/a'
        print(f"  {d['name']}: {d['dE_tint']:.1f} (gain only {d['dE_gain']:.1f}), filter range {rg}")
    over_w = [d for d in lib if d['cardW'] > 1.0 or d['cardW_aur'] > 2.0]
    print(f'the ten largest on W (the visible sky; {len(over_w)} over the rule in all):')
    for d in sorted(over_w, key=lambda d: -max(d['cardW'], d['cardW_aur'] / 2.0))[:10]:
        print(f"  {d['name']}: outside the aureole {d['cardW']:.3f}, aureole {d['cardW_aur']:.3f}")

    print('\nBUILT-IN SKIES (named rows, outside the library figures):')
    for d in blt:
        print('  ' + line_of(d))
        print(f"     EW {d['EW']:.4g}, EP {d['EP']:.4g}, gain {d['gain']:.3g}, ap32 {d['ap32']:.4f}, starved {d['starved']:.3f}, undefined share {d['grade_undef']:.3f}")
    for nme in nights + undef:
        if nme in builtin_names:
            print(f'  {nme}: sun below the horizon or max_y 0 (pass 2, not measured here)')
    return data


def main():
    opts = parse_args(sys.argv[1:])
    if opts['anchor']:
        return anchor()
    lines, unread, nosun = library_rows(opts['quick'])
    builtin_names = set()
    notes = []
    if opts['builtins'] or opts['llsd']:
        b, notes = builtin_rows(opts['llsd']) if opts['builtins'] else ([], [])
        if not opts['builtins']:
            b = [llsd_file_row(f) for f in opts['llsd']]
        builtin_names = {r.split('\t')[0] for r in b}
        lines += b
    inputs = {}
    for l in lines:
        nme, rest = l.split('\t')
        v = [float(x) for x in rest.split()]
        inputs[nme] = {'sun': v[0:3], 'dm': v[15], 'my': v[17]}
    if opts['from']:
        out = Path(opts['from']).read_text(encoding='utf8')
    else:
        out = run(lines, [], timeout=120 + 6 * len(lines))
    if opts['out']:
        Path(opts['out']).write_text(out, encoding='utf8')
    summarise(out, lines, unread, nosun, inputs, builtin_names)
    for nme in notes:
        print(nme)
    if nosun:
        print('without sun_angle: ' + ', '.join(nosun))
    return 0


if __name__ == '__main__':
    sys.exit(main())
