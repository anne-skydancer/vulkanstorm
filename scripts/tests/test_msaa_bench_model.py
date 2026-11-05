# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check the CPU side of the deferred MSAA bench (scripts/perf/msaa_bench_model.py): the
G-buffer layout and memory arithmetic the study's table quotes, the deterministic scenes,
the viewer's normal encoding, the edge-classification rule, and the reference resolves
the bench compares its GPU output with."""
from pathlib import Path
import importlib.util
import math
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('msaa_bench_model', ROOT / 'scripts/perf/msaa_bench_model.py')
model = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model)


class Layout(unittest.TestCase):
    def test_hdr_layout_matches_pipeline(self):
        names = [f for _, f in model.gbuffer_layout()]
        self.assertEqual(names, ['RGBA8', 'RGBA8', 'RGBA16', 'RGB16F', 'DEPTH_COMPONENT24'])
        self.assertEqual(model.bytes_per_sample(model.gbuffer_layout()), 28)

    def test_ldr_and_no_emissive(self):
        self.assertEqual(model.bytes_per_sample(model.gbuffer_layout(hdr=False, emissive=False)), 16)

    def test_stencil_same_size(self):
        self.assertEqual(model.bytes_per_sample(model.gbuffer_layout(stencil=True)),
                         model.bytes_per_sample(model.gbuffer_layout()))

    def test_every_format_has_gl_enum(self):
        self.assertEqual(set(model.FORMAT_BYTES), set(model.GL_FORMATS))


class Memory(unittest.TestCase):
    def test_single_sample_is_todays_frame(self):
        m = model.msaa_memory(2560, 1440, 1)
        self.assertEqual(m['resolve'], 0)
        self.assertEqual(m['total'], (28 + 8) * 2560 * 1440)

    def test_scales_with_samples(self):
        one = model.msaa_memory(1920, 1080, 1)
        for n in (2, 4, 8):
            m = model.msaa_memory(1920, 1080, n)
            self.assertEqual(m['gbuffer'], n * one['gbuffer'])
            self.assertEqual(m['screen'], n * one['screen'])
            self.assertEqual(m['resolve'], 12 * 1920 * 1080)

    def test_table_rows(self):
        rows = model.memory_table()
        self.assertEqual(len(rows), 8)
        row = [r for r in rows if r[0] == '1440p' and r[1] == 4][0]
        self.assertAlmostEqual(row[5], (36 * 4 + 12) * 2560 * 1440 / 2 ** 20)
        self.assertAlmostEqual(row[6], row[5] - 36 * 2560 * 1440 / 2 ** 20)
        self.assertEqual([r[6] for r in rows if r[1] == 1], [0.0, 0.0])


class Scenes(unittest.TestCase):
    def test_deterministic_and_sized(self):
        for name, (count, _, _) in model.SCENES.items():
            a = model.scene_triangles(name, seed=3)
            self.assertEqual(len(a), count)
            self.assertEqual(a[:5], model.scene_triangles(name, seed=3)[:5])

    def test_vertices_valid(self):
        for tri in model.scene_triangles('dense'):
            self.assertEqual(len(tri), 3)
            for x, y, z, nx, ny, nz, mid in tri:
                self.assertTrue(0.0 <= z <= 1.0)
                self.assertGreater(nz, 0.0)
                self.assertAlmostEqual(nx * nx + ny * ny + nz * nz, 1.0, places=9)
                self.assertTrue(0.0 <= mid < 4096.0)


class Normals(unittest.TestCase):
    def test_round_trip(self):
        for n in [(0, 0, 1), (0.6, 0, 0.8), (-0.3, 0.4, math.sqrt(0.75)), (0.9, -0.1, math.sqrt(0.18))]:
            d = model.decode_normal(model.encode_normal(n))
            for a, b in zip(n, d):
                self.assertAlmostEqual(a, b, places=9)


class Edges(unittest.TestCase):
    def sample(self, z=0.5, n=(0.5, 0.5), f=0.0, a=(0.2, 0.3, 0.4)):
        return (z, n, f, a)

    def test_interior_pixel_with_depth_slope_is_simple(self):
        s = [self.sample(z=0.5 + 1e-4 * i) for i in range(8)]
        self.assertFalse(model.is_edge(s))

    def test_silhouette_is_edge(self):
        s = [self.sample(), self.sample(), self.sample(z=0.9), self.sample()]
        self.assertTrue(model.is_edge(s))

    def test_normal_crease_flag_and_material_are_edges(self):
        self.assertTrue(model.is_edge([self.sample(), self.sample(n=(0.53, 0.5))]))
        self.assertTrue(model.is_edge([self.sample(), self.sample(f=1.0)]))
        self.assertTrue(model.is_edge([self.sample(), self.sample(a=(0.2, 0.3, 0.5))]))

    def test_thresholds_are_exclusive(self):
        s = [self.sample(z=0.5), self.sample(z=0.5 + model.EDGE_DEPTH_EPS * 0.99)]
        self.assertFalse(model.is_edge(s))

    def test_single_sample_never_edge(self):
        self.assertFalse(model.is_edge([self.sample()]))


class Resolves(unittest.TestCase):
    def test_box(self):
        self.assertEqual(model.box_resolve([(0, 0, 0, 0), (2, 4, 6, 1)]), (1, 2, 3, 0.5))

    def test_tonemap_weighted_equals_resolve_after_tonemap(self):
        # Same dominant channel in every sample: exactly tonemap -> mean -> inverse.
        cols = [(40.0, 2.0, 1.0, 1.0), (0.5, 0.1, 0.1, 0.0), (0.8, 0.3, 0.2, 0.0), (0.1, 0.0, 0.0, 0.0)]
        for e in (0.5, 1.0, 2.0):
            mapped = [model.reinhard(c, e) for c in cols]
            mean = tuple(sum(m[i] for m in mapped) / len(mapped) for i in range(3))
            want = model.inverse_reinhard(mean, e)
            got = model.tonemap_weighted_resolve(cols, e)
            for a, b in zip(want, got[:3]):
                self.assertAlmostEqual(a, b, places=9)

    def test_tonemap_weighted_tames_fireflies(self):
        cols = [(40.0, 40.0, 40.0, 0.0)] + [(0.2, 0.2, 0.2, 0.0)] * 3
        box = model.box_resolve(cols)[0]
        tw = model.tonemap_weighted_resolve(cols)[0]
        self.assertGreater(box, 10.0)
        self.assertLess(tw, 2.0)
        # Displayed result: a 1/4 coverage bright sample reads close to 1/4 bright.
        shown = model.reinhard((tw, tw, tw))[0]
        self.assertAlmostEqual(shown, (40 / 41 + 3 * 0.2 / 1.2) / 4, places=9)

    def test_uniform_samples_unchanged(self):
        c = (3.0, 1.0, 0.5, 0.25)
        self.assertEqual(model.tonemap_weighted_resolve([c] * 8), c)

    def test_resolve_pattern_has_one_bright_sample(self):
        for n in (2, 4, 8):
            cols = [model.resolve_sample_color(5, 3, s, n) for s in range(n)]
            self.assertEqual(sum(1 for c in cols if c[0] > 10), 1)


class Estimate(unittest.TestCase):
    def test_frame_estimate(self):
        t = {'gfill': 1.0, 'classify': 0.1, 'a_edge': 2.0, 'a_all': 5.0, 'b_edge': 1.5,
             'resolve_tone': 0.2, 'resolve_tone_all': 0.4, 'resolve_depth': 0.05, 'pixel': 1.2}
        self.assertAlmostEqual(model.frame_estimate(t, 4, 'a_edge'), 3.35)
        self.assertAlmostEqual(model.frame_estimate(t, 4, 'a_all'), 6.45)
        self.assertAlmostEqual(model.frame_estimate(t, 4, 'b_edge'), 2.6)
        self.assertAlmostEqual(model.frame_estimate(t, 1, 'pixel'), 2.2)


if __name__ == '__main__':
    unittest.main()
