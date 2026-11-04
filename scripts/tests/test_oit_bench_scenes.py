"""Check the CPU reference of the alpha OIT bench (scripts/perf/oit_bench_scenes.py): the exact
back-to-front composite, the scene coverage mirror of SCENE_GLSL, the stratified mask table
of the stochastic transparency emulation, the error statistics and the PNG writer. A Python
model of depth-sampled stochastic transparency with the same masks checks that the
estimator the bench's `stoch` implementation computes converges to the exact composite."""
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
