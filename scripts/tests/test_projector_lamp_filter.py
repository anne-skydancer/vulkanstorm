# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The lamps' soft-shadow filter (lamp shadows L1, no GL): the shader's floor and cap of the filter radius are the projector set of
llprojectorpool::LampFilter (llprojectorshadowpool.h, kProjectorLamp), the edge reconstruction and the 4 cm cap are gone from the
shader and from the G1 reference, and the lookup passes the projector's cap. The point lights (L4) take the point set (kPointLamp) and
the faces' guard and near plane of llpointshadowpool.h, through the same filter with their own atlas. The GPU gate (G1, the tile against a standalone map) is
scripts/shadows/projector_pool_g1.py; the CPU twins of the lookup are scripts/shadows/lamp_filter_twin.py."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
UTIL = ROOT / 'indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'
G1_REF = ROOT / 'scripts/shadows/projector_pool_g1_reference.glsl'
POOL = ROOT / 'indra/newview/llprojectorshadowpool.h'
POINT = ROOT / 'indra/newview/llpointshadowpool.h'


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

    def test_point_light_set(self):
        pool = POOL.read_text(encoding='utf-8')
        util = UTIL.read_text(encoding='utf-8')
        faces = POINT.read_text(encoding='utf-8')
        emitter, kernel_min, kernel_max = lamp_set(pool, 'kPointLamp')
        self.assertEqual((emitter, kernel_min), lamp_set(pool, 'kProjectorLamp')[:2])  # the same physical lamp, the same base and floor
        self.assertEqual(define(util, 'ST_POINT_KERNEL_TEXELS_MAX'), kernel_max)
        guard = float(re.search(r'constexpr uint32_t kGuard = (\d+);', faces).group(1))
        near = float(re.search(r'constexpr float kNearM = ([0-9.]+)f;', faces).group(1))
        self.assertEqual(define(util, 'ST_POINT_GUARD'), guard)
        self.assertEqual(define(util, 'ST_POINT_NEAR'), near)
        self.assertLessEqual(kernel_max + 2.0, guard)  # the face guard covers the cap plus the bilinear footprint

    def test_point_lookup_passes_the_point_cap_and_atlas(self):
        util = UTIL.read_text(encoding='utf-8')
        sample = util[util.index('float samplePointShadow('):]
        self.assertIn('stSpotFilterTile(point_shadow_atlas, tile, c, grad,', sample)
        self.assertIn('pen, vec2(ST_POINT_KERNEL_TEXELS_MAX))', sample)

    def test_no_reconstruction_and_no_metric_cap(self):
        for path in (UTIL, G1_REF):
            text = path.read_text(encoding='utf-8')
            for gone in ('ST_RV_', 'stRv', 'ST_SPOT_RADIUS_MAX_M'):
                self.assertNotIn(gone, text, '%s still in %s' % (gone, path.name))

    def test_lookup_passes_the_projector_cap(self):
        util = UTIL.read_text(encoding='utf-8')
        sample = util[util.index('vec3 sampleProjectorShadow('):]   # per colour channel since lamp shadows L3
        self.assertIn('stSpotFilterTile(proj_shadow_atlas, tile,', sample)
        self.assertIn('pen, vec2(ST_SPOT_KERNEL_TEXELS_MAX))', sample)
        # the light-through filter takes the same cap (scripts/tests/test_light_through_twin.py checks the rest of it)
        self.assertIn('SHADOW_BIAS_TEXELS * z_tex, z_tex, pen,\n                               vec2(ST_SPOT_KERNEL_TEXELS_MAX));', sample)


if __name__ == '__main__':
    unittest.main()
