# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check the CPU side of the point-light Q0 faces bench (scripts/shadows/point_faces_model.py):
face masks, the face matrices, the per-group box test against the six face frusta, and the
affine tile map and clip planes of method D'."""
from pathlib import Path
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('point_faces_model', ROOT / 'scripts/shadows/point_faces_model.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

NEAR, FAR = 0.1, 40.0


def project(vp, p):
    v = [sum(vp[i][k]*c for k, c in enumerate((p[0], p[1], p[2], 1.0))) for i in range(4)]
    return v


def inside(vp, p):
    x, y, z, w = project(vp, p)
    return w > 0 and -w <= x <= w and -w <= y <= w and -w <= z <= w


class Masks(unittest.TestCase):
    def test_expansion(self):
        self.assertEqual(m.mask_faces(m.MASKS[1]), [0])
        self.assertEqual(m.mask_faces(m.MASKS[2]), [0, 1])
        self.assertEqual(m.mask_faces(m.MASKS[3]), [0, 2, 4])
        self.assertEqual(m.mask_faces(m.MASKS[6]), [0, 1, 2, 3, 4, 5])
        self.assertEqual(m.mask_faces(0), [])

    def test_popcount_matches_grid(self):
        for n, mask in m.MASKS.items():
            self.assertEqual(m.popcount(mask), n)
            self.assertEqual(len(m.mask_faces(mask)), n)


class Matrices(unittest.TestCase):
    def test_bases_are_right_handed_and_orthonormal(self):
        for f in range(6):
            v = m.face_view(f)
            r, u, b = v[0][:3], v[1][:3], v[2][:3]
            self.assertAlmostEqual(m._dot(r, r), 1.0)
            self.assertAlmostEqual(m._dot(r, u), 0.0)
            c = m._cross(r, u)
            for i in range(3):
                self.assertAlmostEqual(c[i], b[i])      # r x u = back = -forward

    def test_forward_axis_is_inside_only_its_face(self):
        vps = m.face_matrices(NEAR, FAR)
        for f, (fwd, _) in enumerate(m.FACES):
            p = [3.0*c for c in fwd]
            self.assertEqual([inside(vp, p) for vp in vps], [i == f for i in range(6)])


class BoxTest(unittest.TestCase):
    vps = m.face_matrices(NEAR, FAR)

    def mask(self, lo, hi):
        return m.group_mask(lo, hi, self.vps)

    def test_small_box_on_an_axis_touches_one_face(self):
        self.assertEqual(self.mask((4.9, -0.2, -0.2), (5.1, 0.2, 0.2)), 0b000001)
        self.assertEqual(self.mask((-0.2, -5.1, -0.2), (0.2, -4.9, 0.2)), 0b001000)

    def test_box_on_an_edge_touches_two_and_on_a_corner_three(self):
        self.assertEqual(m.popcount(self.mask((4.9, 4.9, -0.1), (5.1, 5.1, 0.1))), 2)
        self.assertEqual(m.popcount(self.mask((4.9, 4.9, 4.9), (5.1, 5.1, 5.1))), 3)

    def test_box_around_the_light_touches_all_six(self):
        self.assertEqual(self.mask((-1, -1, -1), (1, 1, 1)), m.ALL_FACES)

    def test_beyond_far_touches_none(self):
        self.assertEqual(self.mask((60, -1, -1), (62, 1, 1)), 0)

    def test_never_loses_a_point_inside_the_box(self):
        # a sampled point of the box that lies in a face's frustum must have that face in the mask
        gen = m.lcg(7)
        for _ in range(300):
            c = [(next(gen) - 0.5)*20 for _ in range(3)]
            h = [0.1 + next(gen)*3 for _ in range(3)]
            lo = [c[i] - h[i] for i in range(3)]
            hi = [c[i] + h[i] for i in range(3)]
            mask = self.mask(lo, hi)
            for _ in range(20):
                p = [lo[i] + next(gen)*(hi[i] - lo[i]) for i in range(3)]
                for f, vp in enumerate(self.vps):
                    if inside(vp, p):
                        self.assertTrue(mask >> f & 1, (lo, hi, p, f))


class TileMap(unittest.TestCase):
    def test_tile_origin_and_dims(self):
        self.assertEqual(m.atlas_dims(256), (768, 512))
        self.assertEqual([m.tile_origin(f, 256) for f in range(6)],
                         [(0, 0), (256, 0), (512, 0), (0, 256), (256, 256), (512, 256)])

    def test_affine_map_sends_clip_square_to_the_tile(self):
        # the map must put NDC [-1, 1] of a face on the tile's range of atlas NDC
        for f in range(6):
            col, row = f % 3, f // 3
            for w in (0.5, 1.0, 7.0):
                x0, y0 = m.apply_tile_map(f, -w, -w, w)
                x1, y1 = m.apply_tile_map(f, w, w, w)
                self.assertAlmostEqual(x0/w, -1.0 + 2.0*col/3.0)
                self.assertAlmostEqual(x1/w, -1.0 + 2.0*(col + 1)/3.0)
                self.assertAlmostEqual(y0/w, -1.0 + 2.0*row/2.0)
                self.assertAlmostEqual(y1/w, -1.0 + 2.0*(row + 1)/2.0)

    def test_affine_map_is_perspective_correct(self):
        # mapping homogeneous x and w, then dividing, equals the viewport mapping of the divided x
        sx, ox, sy, oy = m.tile_map(4)
        x, y, w = 0.3, -0.2, 2.5
        ax, ay = m.apply_tile_map(4, x, y, w)
        self.assertAlmostEqual(ax/w, sx*(x/w) + ox)
        self.assertAlmostEqual(ay/w, sy*(y/w) + oy)

    def test_clip_distances_flag_exactly_the_outside(self):
        w = 2.0
        self.assertTrue(all(d >= 0 for d in m.tile_clip_distances(0.0, 0.0, w)))
        self.assertTrue(all(d >= 0 for d in m.tile_clip_distances(w, -w, w)))      # on the edge
        self.assertLess(min(m.tile_clip_distances(w + 0.01, 0.0, w)), 0)
        self.assertLess(min(m.tile_clip_distances(0.0, -w - 0.01, w)), 0)
        self.assertLess(min(m.tile_clip_distances(-w - 0.01, 0.0, w)), 0)
        self.assertLess(min(m.tile_clip_distances(0.0, w + 0.01, w)), 0)


class Helpers(unittest.TestCase):
    def test_median(self):
        self.assertEqual(m.median([3, 1, 2]), 2)
        self.assertEqual(m.median([4, 1, 2, 3]), 2.5)

    def test_lcg_is_deterministic_and_in_range(self):
        a, b = m.lcg(3), m.lcg(3)
        for _ in range(100):
            v = next(a)
            self.assertEqual(v, next(b))
            self.assertTrue(0.0 <= v < 1.0)


if __name__ == '__main__':
    unittest.main()
