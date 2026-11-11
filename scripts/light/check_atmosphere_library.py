#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The offline library check of stage AT2-0 (item-4 plan): every shipped Windlight sky
(indra/newview/app_settings/windlight/skies) goes through LLScatter::fromLegacy (llatmosphericlight.h) and its sky
radiance is sampled; outside 10 degrees of the light no radiance may exceed the white-card limit lum(E_light) / pi (the measurable
"no whiteout"). Also reports which bounds the library hits and the aerial-perspective march's error (--acc).

The owner's decision (2026-11-11): the hazy skies whose aureole (10 to 20 degrees from the light) exceeds the card are ACCEPTED as
physical; the rule is strict beyond 20 degrees, and an aureole may exceed the card by at most 2x. This script fails (exit 1) on

  - a WHITEOUT line (an exceedance outside the aureole, or by more than 2x inside it), a non-finite radiance,
  - an aureole exceedance by a sky that is not in EXPECTED_AUREOLE (a new one: a bound changed),
  - on a full run, an expected sky that no longer exceeds (the list is stale),
  - a sky file that could not be read into the check (a skipped sky).

    python scripts/light/check_atmosphere_library.py [--acc] [--quick]

Compiles scripts/light/atmosphere_library_check.cpp with g++ (no GPU, no viewer libraries). --quick checks every 8th sky (deterministic).
"""
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
SKIES = ROOT / 'indra' / 'newview' / 'app_settings' / 'windlight' / 'skies'

# The skies whose aureole exceeds the white card, at the bounds of ScatterBounds (a full run of the 768 shipped skies).
EXPECTED_AUREOLE = {
    '[TOR] SCIFI - Arrakissed 2',
    '[TOR] SCIFI - Awesome atmo 2',
    '[TOR] SCIFI - Awesome atmo 3',
    'Surreal - Summer (Paulina)',
}


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


def rows(quick):
    """(rows, skipped): the check's input lines and the sky files that could not be read."""
    out, skipped = [], []
    files = sorted(SKIES.glob('*.xml'))
    for i, f in enumerate(files):
        if quick and i % 8:
            continue
        try:
            d = llsd(ET.parse(f).getroot()[0])
            v = (d['sunlight_color'][:3] + d['blue_density'][:3] + [d['haze_density'][0], d['density_multiplier'][0], d['max_y'][0], d['distance_multiplier'][0]]
                 + d['blue_horizon'][:3] + [d['haze_horizon'][0]] + d['ambient'][:3] + [d['glow'][0], d['glow'][2], d['sun_angle']])
            assert len(v) == 20
        except (KeyError, IndexError, AssertionError, ET.ParseError, TypeError):
            skipped.append(f.name)
            continue
        out.append(unquote(f.name)[:-4].replace('\t', ' ') + '\t' + ' '.join(repr(float(x)) for x in v))
    return out, skipped


def main():
    args = sys.argv[1:]
    quick = '--quick' in args
    lines, skipped = rows(quick)
    if skipped:
        print('SKIPPED skies (could not be read):', skipped)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        data = tmp / 'skies.txt'
        data.write_text('\n'.join(lines) + '\n', encoding='utf8')
        exe = tmp / 'atmosphere_library_check.exe'
        subprocess.run(['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'indra' / 'newview'),
                        str(ROOT / 'scripts' / 'light' / 'atmosphere_library_check.cpp'), '-o', str(exe)], check=True)
        r = subprocess.run([str(exe), str(data)] + (['--acc'] if '--acc' in args else []), capture_output=True, text=True)
    sys.stdout.write(''.join(l + '\n' for l in r.stdout.splitlines() if not l.startswith('AUREOLE')))
    seen = {l.split('\t')[1] for l in r.stdout.splitlines() if l.startswith('AUREOLE')}
    failed = r.returncode != 0 or bool(skipped)
    unexpected = sorted(seen - EXPECTED_AUREOLE)
    if unexpected:
        print('UNEXPECTED aureole exceedances (a bound changed, or add them to EXPECTED_AUREOLE with the owner):', unexpected)
        failed = True
    stale = sorted(EXPECTED_AUREOLE - seen)
    if stale and not quick:
        print('STALE entries of EXPECTED_AUREOLE (no longer exceed the card):', stale)
        failed = True
    print('aureole exceedances accepted (owner, 2026-11-11):', sorted(seen))
    print('FAILED' if failed else 'OK')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
