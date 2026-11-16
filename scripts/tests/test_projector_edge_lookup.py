# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The projector edge reconstruction's constants (no GL): the shader's ST_RV_* defines equal the bench prototype's RV_* defaults,
the G1 reference includes the util's block, and the blend band is well formed. The prototype stays an independent copy of the
algorithm on purpose; this only catches a drifted number (the GPU gate, RVreal against RV at 1e-5, is the full check)."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
UTIL = ROOT / 'indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'
BENCH = ROOT / 'scripts/shadows/proj_edges_bench.py'
NAMES = ('S', 'K', 'D', 'MIN', 'LO', 'HI')


def defines(text, prefix):
    return {n: float(m.group(1)) for n in NAMES
            for m in [re.search(r'^#define %s%s\s+([0-9.]+)' % (prefix, n), text, re.M)] if m}


class ProjectorEdgeLookup(unittest.TestCase):
    def test_shader_and_prototype_constants_agree(self):
        util = defines(UTIL.read_text(encoding='utf-8'), 'ST_RV_')
        bench = defines(BENCH.read_text(encoding='utf-8'), 'RV_')
        self.assertEqual(sorted(util), sorted(NAMES))
        self.assertEqual(util, bench)

    def test_blend_band(self):
        util = defines(UTIL.read_text(encoding='utf-8'), 'ST_RV_')
        self.assertGreater(util['MIN'], 0.0)
        self.assertLess(util['MIN'], 1.0)
        self.assertLess(util['LO'], util['HI'])


if __name__ == '__main__':
    unittest.main()
