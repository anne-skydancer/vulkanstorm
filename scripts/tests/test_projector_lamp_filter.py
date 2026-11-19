# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The lamps' soft-shadow filter (lamp shadows L1, no GL): the shader's floor and cap of the filter radius are the projector set of
llprojectorpool::LampFilter (llprojectorshadowpool.h, kProjectorLamp), the edge reconstruction and the 4 cm cap are gone from the
shader and from the G1 reference, and the lookup passes the projector's cap. The GPU gate (G1, the tile against a standalone map) is
scripts/shadows/projector_pool_g1.py; the CPU twins of the lookup are scripts/shadows/lamp_filter_twin.py."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
UTIL = ROOT / 'indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'
G1_REF = ROOT / 'scripts/shadows/projector_pool_g1_reference.glsl'
POOL = ROOT / 'indra/newview/llprojectorshadowpool.h'


def define(text, name):
    m = re.search(r'^#define %s\s+([0-9.]+)' % name, text, re.M)
    return float(m.group(1)) if m else None


def lamp_set(text, name):
    m = re.search(r'constexpr LampFilter %s\{\s*([0-9.]+)f,\s*([0-9.]+)f,\s*([0-9.]+)f\s*\};' % name, text)
    return tuple(float(v) for v in m.groups()) if m else None


class ProjectorLampFilter(unittest.TestCase):
    def test_shader_limits_are_the_projector_set(self):
        util = UTIL.read_text(encoding='utf-8')
        emitter, kernel_min, kernel_max = lamp_set(POOL.read_text(encoding='utf-8'), 'kProjectorLamp')
        self.assertEqual(define(util, 'ST_SPOT_KERNEL_MIN'), kernel_min)
        self.assertEqual(define(util, 'ST_SPOT_KERNEL_TEXELS_MAX'), kernel_max)
        self.assertEqual(emitter, 0.045)  # the light-size study's lamp base (4.5 cm times the softness)
        self.assertGreater(kernel_min, 1.0)  # a 1-texel floor drew sub-texel casters as hard squares
        self.assertLess(kernel_min, kernel_max)

    def test_point_light_set_is_prepared(self):
        pool = POOL.read_text(encoding='utf-8')
        emitter, kernel_min, kernel_max = lamp_set(pool, 'kPointLamp')
        self.assertEqual((emitter, kernel_min), lamp_set(pool, 'kProjectorLamp')[:2])
        self.assertLessEqual(kernel_max + 2.0, 16.0)  # the face guard of 16 texels covers the cap plus the bilinear footprint

    def test_no_reconstruction_and_no_metric_cap(self):
        for path in (UTIL, G1_REF):
            text = path.read_text(encoding='utf-8')
            for gone in ('ST_RV_', 'stRv', 'ST_SPOT_RADIUS_MAX_M'):
                self.assertNotIn(gone, text, '%s still in %s' % (gone, path.name))

    def test_lookup_passes_the_projector_cap(self):
        util = UTIL.read_text(encoding='utf-8')
        sample = util[util.index('vec3 sampleProjectorShadow('):]   # per colour channel since lamp shadows L3
        self.assertIn('pen, vec2(ST_SPOT_KERNEL_TEXELS_MAX))', sample)
        # the light-through filter takes the same cap (scripts/tests/test_light_through_twin.py checks the rest of it)
        self.assertIn('pen, vec2(ST_SPOT_KERNEL_TEXELS_MAX), SHADOW_BIAS_TEXELS * z_tex)', sample)


if __name__ == '__main__':
    unittest.main()
