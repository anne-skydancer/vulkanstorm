# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check the CPU reference of the alpha OIT bench (scripts/perf/oit_bench_scenes.py): the exact
back-to-front composite, the scene coverage mirror of SCENE_GLSL, the stratified mask table
of the stochastic transparency emulation, the error statistics and the PNG writer. A Python
model of depth-sampled stochastic transparency with the same masks checks that the
estimator the bench's `stoch` implementation computes converges to the exact composite.
Round 2: the deep and occl scenes, the blue-noise map, the depth-aware upsample weights, the
blurred error, and Python models of the one-pass basic method (`stoch1`) and of the PPLL
slots + stochastic overflow hybrid (`hybrid`)."""
from pathlib import Path
import importlib.util
import math
import random
import struct
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('oit_bench_scenes', ROOT / 'scripts/perf/oit_bench_scenes.py')
scenes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scenes)


def dsst_pixel(frags, background, samples, rng, table):
    """Depth-sampled stochastic transparency for one pixel, as the bench's four passes."""
    total = 1.0
    for _, _, a in frags:
        total *= 1.0 - a
    alpha_total = 1.0 - total
    depth = [1.0] * samples                      # seeded with the opaque depth
    for z, _, a in frags:                        # stochastic depth pass, LEQUAL
        r = min(int(math.floor(a * samples + rng.random())), samples)
        mask = table[r][rng.randrange(len(table[r]))]
        for s in range(samples):
            if mask >> s & 1 and z <= depth[s]:
                depth[s] = z
    acc = [0.0, 0.0, 0.0]
    acc_a = 0.0
    for z, c, a in frags:                        # accumulation pass
        vis = sum(1 for d in depth if z <= d) / samples
        for k in range(3):
            acc[k] += vis * a * c[k]
        acc_a += vis * a
    if acc_a <= 0.0:
        return tuple(background)
    return tuple(acc[k] * alpha_total / acc_a + background[k] * (1.0 - alpha_total) for k in range(3))


class ExactComposite(unittest.TestCase):
    def test_two_layers_any_draw_order(self):
        layers = [scenes.Layer((1, 0, 0), 0.5, 0.7), scenes.Layer((0, 1, 0), 0.5, 0.3)]
        for order in (layers, layers[::-1]):
            rgb, total, count = scenes.exact_pixel('full', (0, 0, 0), order, 0, 0)
            self.assertEqual(count, 2)
            self.assertAlmostEqual(total, 0.75)
            # Red behind green: red 0.5 then green over it: (0.25, 0.5, 0).
            for got, want in zip(rgb, (0.25, 0.5, 0.0)):
                self.assertAlmostEqual(got, want)

    def test_opaque_nearest_wins(self):
        bg, layers = scenes.scene('opaque', 5)
        rgb, total, _ = scenes.exact_pixel('opaque', bg, layers, 3, 3)
        nearest = min(layers, key=lambda l: l.depth)
        self.assertEqual(total, 1.0)
        for got, want in zip(rgb, nearest.color):
            self.assertAlmostEqual(got, want)

    def test_depths_distinct(self):
        for n in (1, 2, 8, 32, 64):
            _, layers = scenes.scene('full', n)
            self.assertEqual(len({l.depth for l in layers}), n)


class SceneCoverage(unittest.TestCase):
    def test_sparse_quarter(self):
        _, layers = scenes.scene('sparse', 1)
        covered = sum(scenes.fragment_alpha('sparse', layers[0], x, y) is not None
                      for y in range(0, 256, 4) for x in range(0, 256, 4))
        self.assertEqual(covered * 4, 64 * 64)

    def test_foliage_density_and_contrast(self):
        _, layers = scenes.scene('foliage', 32)
        counts = []
        alphas = set()
        for y in range(scenes.STRIPE_PERIOD):
            for x in range(scenes.STRIPE_PERIOD):
                n = 0
                for l in layers:
                    a = scenes.fragment_alpha('foliage', l, x, y)
                    if a is not None:
                        n += 1
                        alphas.add(a)
                counts.append(n)
        mean = sum(counts) / len(counts)
        self.assertAlmostEqual(mean, 32 * scenes.STRIPE_WIDTH / scenes.STRIPE_PERIOD, delta=1.0)
        self.assertGreaterEqual(min(counts), 8)
        self.assertGreater(sum(n > 8 for n in counts), 0.8 * len(counts))   # most pixels pass 8 slots
        self.assertEqual(alphas, {scenes.f32(scenes.FOLIAGE_CORE_ALPHA), scenes.f32(scenes.FOLIAGE_EDGE_ALPHA)})

    def test_pixel_key_is_sound(self):
        # Pixels sharing a key must receive identical fragments.
        for name in ('full', 'sparse', 'bright', 'foliage'):
            bg, layers = scenes.scene(name, 16)
            seen = {}
            for y in range(0, 200, 7):
                for x in range(0, 200, 3):
                    frags = tuple(scenes.fragment_alpha(name, l, x, y) for l in layers)
                    seen.setdefault(scenes.pixel_key(name, x, y), frags)
                    self.assertEqual(seen[scenes.pixel_key(name, x, y)], frags, (name, x, y))

    def test_window_cache_matches_direct(self):
        bg, layers = scenes.scene('foliage', 12)
        win = scenes.exact_window('foliage', bg, layers, 5, 9, 13, 11)
        for i, (rgb, total, count) in enumerate(win):
            x, y = 5 + i % 13, 9 + i // 13
            self.assertEqual((rgb, total, count), scenes.exact_pixel('foliage', bg, layers, x, y))


class MaskTable(unittest.TestCase):
    def test_popcount_and_uniformity(self):
        for samples in (4, 8, 16):
            table = scenes.mask_table(samples)
            self.assertEqual(len(table), samples + 1)
            for r, row in enumerate(table):
                self.assertEqual(len(row), 64)
                for mask in row:
                    self.assertLess(mask, 1 << samples)
                    self.assertEqual(bin(mask).count('1'), r)
        # Every sample is equally likely to be covered (chi-square-free check: within 4 sd).
        table = scenes.mask_table(8, entries=4096)
        for r in (1, 3, 4):
            hits = [sum(m >> s & 1 for m in table[r]) for s in range(8)]
            p = r / 8
            sd = math.sqrt(4096 * p * (1 - p))
            for h in hits:
                self.assertLess(abs(h - 4096 * p), 4 * sd)


class StochasticModel(unittest.TestCase):
    def frags(self, name, n):
        bg, layers = scenes.scene(name, n)
        out = []
        for l in layers:
            a = scenes.fragment_alpha(name, l, 0, 0)
            if a:
                out.append((l.depth, l.color, a))
        return bg, out

    def rmse(self, name, n, samples, trials=3000):
        bg, frags = self.frags(name, n)
        want, _, _ = scenes.exact_pixel(name, bg, [scenes.Layer(c, a, z) for z, c, a in frags], 0, 0)
        rng = random.Random(7)
        table = scenes.mask_table(samples)
        err = 0.0
        for _ in range(trials):
            got = dsst_pixel(frags, bg, samples, rng, table)
            err += sum((255 * (g - w)) ** 2 for g, w in zip(got, want))
        return math.sqrt(err / (3 * trials))

    def test_exact_cases(self):
        # One layer, and two alpha 0.5 layers at S = 8, are exact under the depth-based method.
        self.assertLess(self.rmse('opaque', 1, 8, 50), 1e-6)
        self.assertLess(self.rmse('full', 2, 8, 200), 1e-6)

    def test_converges_with_samples(self):
        r4, r8, r16 = (self.rmse('bright', 8, s) for s in (4, 8, 16))
        self.assertGreater(r4, r8)
        self.assertGreater(r8, r16)


def stochastic_depths(frags, samples, rng, table):
    """Pass D of the models: S depths seeded at 1.0, each fragment's stratified mask, LEQUAL."""
    depth = [1.0] * samples
    for z, _, a in frags:
        r = min(int(math.floor(a * samples + rng.random())), samples)
        mask = table[r][rng.randrange(len(table[r]))]
        for s in range(samples):
            if mask >> s & 1 and z <= depth[s]:
                depth[s] = z
    return depth


def basic_pixel(frags, background, samples, rng, table):
    """stoch1: per sample the colour of the nearest covering fragment; the covered samples'
    mean scaled to the exact total alpha."""
    trans = 1.0
    for _, _, a in frags:
        trans *= 1.0 - a
    alpha_total = 1.0 - trans
    depth = [1.0] * samples
    color = [None] * samples
    for z, c, a in frags:
        r = min(int(math.floor(a * samples + rng.random())), samples)
        mask = table[r][rng.randrange(len(table[r]))]
        for s in range(samples):
            if mask >> s & 1 and z <= depth[s]:
                depth[s] = z
                color[s] = c
    won = [c for c in color if c is not None]
    if not won:
        return tuple(background)
    return tuple(sum(c[k] for c in won) / len(won) * alpha_total + background[k] * (1.0 - alpha_total)
                 for k in range(3))


def hybrid_pixel(frags, background, samples, slots, rng, table):
    """hybrid: the first `slots` fragments in arrival order are kept exactly; with no more than
    that the composite is the exact sort, past it every fragment (slot or tail) is weighted by
    its visibility in the S stochastic depths and the sum scaled to the exact total alpha."""
    if len(frags) <= slots:
        layers = [scenes.Layer(c, a, z) for z, c, a in frags]
        return scenes.exact_pixel('full', background, layers, 0, 0)[0]
    depth = stochastic_depths(frags, samples, rng, table)
    trans = 1.0
    acc = [0.0, 0.0, 0.0]
    acc_a = 0.0
    for z, c, a in frags:
        vis = sum(1 for d in depth if z <= d) / samples
        for k in range(3):
            acc[k] += vis * a * c[k]
        acc_a += vis * a
        trans *= 1.0 - a
    alpha_total = 1.0 - trans
    return tuple(acc[k] * alpha_total / acc_a + background[k] * trans for k in range(3))


class Round2Scenes(unittest.TestCase):
    def test_perspective_depth(self):
        self.assertAlmostEqual(scenes.perspective_depth(scenes.DEEP_NEAR), 0.0)
        self.assertAlmostEqual(scenes.perspective_depth(scenes.DEEP_FAR), 1.0)
        zs = [scenes.perspective_depth(d) for d in (1, 10, 40, 40.05, 100)]
        self.assertEqual(zs, sorted(zs))

    def test_deep_layers_need_more_than_16_bits(self):
        # The deep scene's layers are distinct in a 24-bit depth and collapse in a 16-bit one:
        # the case the d16 option must be judged on.
        _, layers = scenes.scene('deep', 8)
        z = [l.depth for l in layers]
        self.assertEqual(len({round(v * 16777215) for v in z}), 8)
        self.assertLess(len({round(v * 65535) for v in z}), 8)

    def test_occl_hides_layers_behind_the_bars(self):
        bg, layers = scenes.scene('occl', 8)
        gap = scenes.exact_pixel('occl', bg, layers, 0, 0)
        bar = scenes.exact_pixel('occl', bg, layers, scenes.OCCL_BAR, 0)
        self.assertEqual(gap[2], 8)
        self.assertEqual(bar[2], sum(l.depth <= scenes.OCCL_DEPTH for l in layers))
        self.assertTrue(0 < bar[2] < 8)
        # No layer sits on the bar depth (a LEQUAL tie would depend on the driver's rounding).
        for n in (1, 2, 4, 8, 16, 32, 64):
            self.assertTrue(all(abs(l.depth - scenes.OCCL_DEPTH) > 1e-4 for l in scenes.scene('occl', n)[1]))

    def test_occl_pixel_key_and_window(self):
        bg, layers = scenes.scene('occl', 8)
        win = scenes.exact_window('occl', bg, layers, 3, 0, 40, 2)
        for i, got in enumerate(win):
            x, y = 3 + i % 40, i // 40
            self.assertEqual(got, scenes.exact_pixel('occl', bg, layers, x, y))


class BlueNoise(unittest.TestCase):
    def test_permutation(self):
        b = scenes.blue_noise()
        self.assertEqual(sorted(b), [(r + 0.5) / 1024 for r in range(1024)])

    def test_flatter_than_white_noise(self):
        # Every 4x4 block of the thresholded map holds close to t * 16 points; white noise
        # does not. This is the property that makes blue-noise thresholds a dither.
        def worst(m):
            out = 0
            for t in (0.25, 0.5, 0.75):
                for by in range(0, 32, 4):
                    for bx in range(0, 32, 4):
                        c = sum(m[(by + j) * 32 + bx + i] < t for j in range(4) for i in range(4))
                        out = max(out, abs(c - 16 * t))
            return out
        rng = random.Random(3)
        white = [rng.random() for _ in range(1024)]
        self.assertLessEqual(worst(scenes.blue_noise()), 2.0)
        self.assertGreater(worst(white), 3.0)


class UpsampleWeights(unittest.TestCase):
    def test_bilinear_when_nothing_occludes(self):
        w = scenes.upsample_weights(0.75, 0.25, 0.5, [1.0] * 4)
        for got, want in zip(w, [0.25 * 0.75, 0.75 * 0.75, 0.25 * 0.25, 0.75 * 0.25]):
            self.assertAlmostEqual(got, want)
        self.assertAlmostEqual(sum(w), 1.0)

    def test_occluded_neighbour_dropped(self):
        w = scenes.upsample_weights(0.75, 0.75, 0.6, [1.0, 0.5, 1.0, 1.0])
        self.assertEqual(w[1], 0.0)
        self.assertAlmostEqual(sum(w), 1.0)

    def test_containing_neighbour_keeps_the_fragment(self):
        # Pixel p maps to half-res position p/2 - 0.25: fractions 0.75 (even p) or 0.25 (odd p),
        # so the texel containing p always has bilinear weight 9/16; if only it is unoccluded,
        # it takes all the weight.
        for fx, fy, k in ((0.75, 0.75, 3), (0.25, 0.25, 0), (0.75, 0.25, 1), (0.25, 0.75, 2)):
            seeds = [0.1] * 4
            seeds[k] = 1.0
            w = scenes.upsample_weights(fx, fy, 0.5, seeds)
            self.assertEqual(w[k], 1.0)
            full = scenes.upsample_weights(fx, fy, 0.5, [1.0] * 4)
            self.assertAlmostEqual(full[k], 9 / 16)

    def test_all_occluded(self):
        self.assertEqual(scenes.upsample_weights(0.5, 0.5, 0.9, [0.5] * 4), [0.0] * 4)


class BlurredError(unittest.TestCase):
    def window(self, w, h, f):
        exact = [((0.5, 0.5, 0.5), 0.5, 1)] * (w * h)
        got = [tuple(0.5 + f(i % w, i // w) for _ in range(3)) for i in range(w * h)]
        return got, exact

    def test_exact_is_zero(self):
        got, exact = self.window(8, 8, lambda x, y: 0.0)
        self.assertEqual(scenes.error_stats_blurred(got, exact, 8, 8), 0.0)

    def test_checkerboard_dither_vanishes_bias_stays(self):
        e = 10 / 255
        got, exact = self.window(8, 8, lambda x, y: e if (x + y) & 1 else -e)
        self.assertAlmostEqual(scenes.error_stats(got, exact)[0], 10.0)
        # The 3x3 binomial kernel's response to a +-e checkerboard is (4 - 8 + 4) e / 16 = 0; a
        # constant bias passes unchanged.
        self.assertAlmostEqual(scenes.error_stats_blurred(got, exact, 8, 8), 0.0)
        got, exact = self.window(8, 8, lambda x, y: e)
        self.assertAlmostEqual(scenes.error_stats_blurred(got, exact, 8, 8), 10.0)


class Round2Models(unittest.TestCase):
    def frags(self, name, n, x=0, y=0):
        bg, layers = scenes.scene(name, n)
        out = []
        for l in layers:
            a = scenes.fragment_alpha(name, l, x, y)
            if a and l.depth <= scenes.opaque_depth(name, x, y):
                out.append((l.depth, l.color, a))
        return bg, out, scenes.exact_pixel(name, bg, layers, x, y)[0]

    def errors(self, model, name, n, trials=1500, x=0, y=0):
        bg, frags, want = self.frags(name, n, x, y)
        rng = random.Random(11)
        table = scenes.mask_table(8)
        sq = 0.0
        mean = [0.0, 0.0, 0.0]
        for _ in range(trials):
            got = model(frags, bg, rng, table)
            sq += sum((255 * (g - w)) ** 2 for g, w in zip(got, want))
            for k in range(3):
                mean[k] += got[k] / trials
        bias = max(abs(255 * (m - w)) for m, w in zip(mean, want))
        return math.sqrt(sq / (3 * trials)), bias

    def test_hybrid_exact_within_slots(self):
        model = lambda f, bg, rng, t: hybrid_pixel(f, bg, 8, 8, rng, t)
        for name, n in (('full', 2), ('full', 8), ('bright', 8), ('occl', 12)):   # occl 12: 7 visible on a bar
            rmse, _ = self.errors(model, name, n, trials=20, x=scenes.OCCL_BAR)
            self.assertLess(rmse, 1e-6, (name, n))

    def test_hybrid_past_slots_is_the_depth_based_estimate(self):
        # Past the slots the hybrid's error is that of the depth-based method (the same pass D),
        # well below the noise of the basic one-pass method on the same pixel.
        hybrid = lambda f, bg, rng, t: hybrid_pixel(f, bg, 8, 8, rng, t)
        dsst = lambda f, bg, rng, t: dsst_pixel(f, bg, 8, rng, t)
        basic = lambda f, bg, rng, t: basic_pixel(f, bg, 8, rng, t)
        h, d, b = (self.errors(m, 'full', 16)[0] for m in (hybrid, dsst, basic))
        self.assertAlmostEqual(h, d, delta=0.15 * d)
        self.assertLess(h, b)

    def test_basic_single_layer_exact_and_consistent(self):
        basic = lambda f, bg, rng, t: basic_pixel(f, bg, 8, rng, t)
        self.assertLess(self.errors(basic, 'full', 1, trials=50)[0], 1e-6)
        # Alpha correction makes it biased at finite S, but the bias is a small part of its error.
        rmse, bias = self.errors(basic, 'full', 4, trials=3000)
        self.assertLess(bias, 0.5 * rmse)


class Output(unittest.TestCase):
    def test_error_stats(self):
        exact = [((0.5, 0.5, 0.5), 0.5, 1), ((0.2, 0.2, 0.2), 0.0, 0)]
        rmse, worst = scenes.error_stats([(0.5 + 1 / 255, 0.5, 0.5), (1.0, 1.0, 1.0)], exact)
        self.assertAlmostEqual(worst, 1.0)
        self.assertAlmostEqual(rmse, math.sqrt(1 / 3))

    def test_png_round_trip(self):
        import tempfile, os
        w, h = 3, 2
        rgb = [(x / 2, y, 0.25) for y in range(h) for x in range(w)]
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 't.png')
            scenes.write_png(path, w, h, rgb, encode=lambda v: int(round(255 * v)))
            with open(path, 'rb') as f:
                data = f.read()
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        pos, chunks = 8, {}
        while pos < len(data):
            n, = struct.unpack('>I', data[pos:pos + 4]); kind = data[pos + 4:pos + 8]
            body = data[pos + 8:pos + 8 + n]
            self.assertEqual(struct.unpack('>I', data[pos + 8 + n:pos + 12 + n])[0], zlib.crc32(kind + body) & 0xFFFFFFFF)
            chunks[kind] = body; pos += 12 + n
        self.assertEqual(struct.unpack('>IIBBBBB', chunks[b'IHDR']), (w, h, 8, 2, 0, 0, 0))
        raw = zlib.decompress(chunks[b'IDAT'])
        # Top PNG row is the last GL row (y = 1): green 255.
        self.assertEqual(list(raw[:1 + 3 * w]), [0, 0, 255, 64, 128, 255, 64, 255, 255, 64])


if __name__ == '__main__':
    unittest.main()
