#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Reference values for the AT2-F twin's anchor (scripts/light/at2f_creator_grade.py --anchor): today's sky luminance, the bench's
physical sky luminance (its Python mapping, camera 20 m) and the bench's physical sky irradiance at three directions per sky, read
from the GPU bench's 720 x 220 panorama (bench_atmosphere.py, the PBR path: sky_hdr_scale 2 sqrt(gamma)):
  zenith (top row, column W/2), horizon (row 30 = 2 deg, column W/4, 90 deg from the light), near the sun (column 389 = 14.75 deg of
  azimuth from the sun, the row nearest the sun's elevation).
Needs Windows and a GPU; run only when no viewer or build is running.

    python scripts/light/at2f_anchor_gpu.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
argv, sys.argv = sys.argv, sys.argv[:1]
import bench_atmosphere as b  # noqa: E402

SKIES = ['Default.xml', 'Sunset.xml', '[TOR] MIDDAY - Salmon.xml', '[TOR] MIDDAY - Pleasantrie.xml',
         'Alchemy Immortalis - Fog Lifting.xml', 'Foggy.xml']
W, H = 720, 220


def px(t, j, i):
    k = 4 * (j * W + i)
    return b.lum(t[k:k + 3])


def main():
    pano = b.Panorama(W, H)
    default = b.load_sky(b.sky_file('Default.xml'))
    gain, _ = b.calibrate(pano, default)
    b.CAL.update(sky_gain=gain)
    phys = dict(sky_gain=1.0, tint=(1.0, 1.0, 1.0))
    for name in SKIES:
        p = b.load_sky(b.sky_file(name))
        t, Et = pano.today(p)
        q, Eq = pano.proposed(p, b.map_sky(p, phys))
        sun_el = b.math.degrees(p['light_el'])
        j_sun = min(max(int((sun_el - b.EL0) / (b.EL1 - b.EL0) * H), 0), H - 1)
        for label, j, i in (('zenith', H - 1, W // 2), ('horizon', 30, W // 4), ('near-sun', j_sun, 389)):
            el = b.EL0 + (b.EL1 - b.EL0) * (j + 0.5) / H
            az = ((i + 0.5) / W * 2.0 - 1.0) * 180.0
            print(f'{name}\tsun {sun_el:.3f}\t{label}\tel {el:.3f}\taz {az:.2f}\ttoday {px(t, j, i):.4f}\tphysical {px(q, j, i):.4f}')
        print(f'{name}\tsun {sun_el:.3f}\tirradiance\ttoday {b.lum(Et):.4f}\tphysical {b.lum(Eq):.4f}')


if __name__ == '__main__':
    main()
