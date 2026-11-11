#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The offline library check of stage AT2-0 (item-4 plan): every shipped Windlight sky
(indra/newview/app_settings/windlight/skies) goes through LLScatter::fromLegacy (llatmosphericlight.h) and its sky
radiance is sampled; outside 10 degrees of the light no radiance may exceed the white-card limit lum(E_light) / pi (the measurable
"no whiteout"). Also reports which bounds the library hits and the aerial-perspective march's error (--acc).

    python scripts/light/check_atmosphere_library.py [--acc] [--quick]

Compiles scripts/light/atmosphere_library_check.cpp with g++ (no GPU, no viewer libraries). --quick checks every 8th sky.
Exit code 1 on a whiteout or a non-finite radiance.
"""
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
SKIES = ROOT / 'indra' / 'newview' / 'app_settings' / 'windlight' / 'skies'


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
    out = []
    files = sorted(SKIES.glob('*.xml'))
    for i, f in enumerate(files):
        if quick and i % 8:
            continue
        d = llsd(ET.parse(f).getroot()[0])
        try:
            v = (d['sunlight_color'][:3] + d['blue_density'][:3] + [d['haze_density'][0], d['density_multiplier'][0], d['max_y'][0], d['distance_multiplier'][0]]
                 + d['blue_horizon'][:3] + [d['haze_horizon'][0]] + d['ambient'][:3] + [d['glow'][0], d['glow'][2], d['sun_angle']])
        except (KeyError, IndexError):
            continue
        out.append(unquote(f.name)[:-4].replace('\t', ' ') + '\t' + ' '.join(repr(float(x)) for x in v))
    return out


def main():
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        data = tmp / 'skies.txt'
        data.write_text('\n'.join(rows('--quick' in args)) + '\n', encoding='utf8')
        exe = tmp / 'atmosphere_library_check.exe'
        subprocess.run(['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'indra' / 'newview'),
                        str(ROOT / 'scripts' / 'light' / 'atmosphere_library_check.cpp'), '-o', str(exe)], check=True)
        r = subprocess.run([str(exe), str(data)] + (['--acc'] if '--acc' in args else []))
        return r.returncode


if __name__ == '__main__':
    sys.exit(main())
