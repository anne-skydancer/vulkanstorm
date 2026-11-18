# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The golden values of indra/newview/tests/llstochastichistory_test.cpp (test_dither and test_caster), computed apart from its
C++ twins and from the viewer's header: the Aurora Viewer's formulas (crates/aurora-render/src/shaders/object.wgsl st_mask,
shadow_alpha.wgsl sst_seed and sst_keep, with the blue-noise tile's dither of owner decision D3 in place of sst_keep's hashed
one; Anne Skydancer, GPL-3.0-or-later), rounded to float32 where the shaders round, on the Aurora Viewer's tile and on the
engine's mask table (splitmix64, as LLStochasticEngine::maskTable and scripts/perf/test_alpha_stochastic.py make it).

The tile is made by scripts/perf/gen_stochastic_blue_noise.py (a port of the Aurora Viewer's void_and_cluster) and checked
against the SHA-256 of the Aurora Viewer's oit_stochastic_tile.bin; --tile reads that file instead.

    python scripts/tests/gen_stochastic_goldens.py [--tile PATH/oit_stochastic_tile.bin] [--check]

It prints every value the C++ test pins, in the C++ test's notation, and the coverage its properties bound. --check also
finds each of those values in the C++ test as it pins them (spaces aside) and checks the coverage against the bounds the
test states; it exits 1 on the first one that differs, so a golden value cannot drift from the Aurora Viewer's formulas."""
import argparse
import hashlib
import importlib.util
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CPP_TEST = ROOT / 'indra/newview/tests/llstochastichistory_test.cpp'
M32 = 0xffffffff
M64 = (1 << 64) - 1
SEEDS = 2048


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def h(v):
    """stoch_hash, st_hash, sst_hash: a PCG hash"""
    x = (v * 747796405 + 2891336453) & M32
    x = (((x >> ((x >> 28) + 4)) ^ x) * 277803737) & M32
    return ((x >> 22) ^ x) & M32


def layer_seed(tie, prim):
    """st_mask's layer, sst_seed without the frame"""
    return h(h((tie * 0x9e3779b9 + 0x7f4a7c15) & M32) ^ h((prim + 0x632be5ab) & M32))


def frame_seed(seed, frame):
    return h((seed + frame * 0x9e3779b9) & M32) if frame else seed


def caster_layer(draw_seed, prim, front):
    """the caster's layer: sst_seed(tie = the draw's identity, prim * 2 + side)"""
    return layer_seed(draw_seed, (prim * 2 + (0 if front else 1)) & M32)


def mask_table(s):
    """LLStochasticEngine::maskTable(s): row r holds SEEDS random r-subsets of the s bits"""
    state = (0x9e3779b97f4a7c15 ^ s) & M64
    table = []

    def nxt():
        nonlocal state
        state = (state + 0x9e3779b97f4a7c15) & M64
        z = state
        z = ((z ^ (z >> 30)) * 0xbf58476d1ce4e5b9) & M64
        z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) & M64
        return z ^ (z >> 31)

    for r in range(s + 1):
        for _ in range(SEEDS):
            bits, mask = list(range(s)), 0
            for i in range(r):
                j = i + nxt() % (s - i)
                bits[i], bits[j] = bits[j], bits[i]
                mask |= 1 << bits[i]
            table.append(mask)
    return table


def load_tile(path):
    if path:
        data = Path(path).read_bytes()
        assert len(data) == 8192, 'a tile file holds 4096 16-bit ranks'
        ranks = [data[2 * i] | (data[2 * i + 1] << 8) for i in range(4096)]
    else:
        spec = importlib.util.spec_from_file_location('blue', ROOT / 'scripts/perf/gen_stochastic_blue_noise.py')
        blue = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(blue)
        ranks = blue.generate()
    digest = hashlib.sha256(b''.join(r.to_bytes(2, 'little') for r in ranks)).hexdigest()
    assert digest == '8c547004839c1d151a35efb2528733d5e8eed0387ca39cafce969fa932e9c0ad', 'not the Aurora Viewer tile: ' + digest
    return ranks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tile', help="the Aurora Viewer's oit_stochastic_tile.bin (default: generated)")
    ap.add_argument('--check', action='store_true', help='check that the C++ test pins these values')
    args = ap.parse_args()
    tile = load_tile(args.tile)
    pinned = []   # the C++ test's spelling of each value, spaces aside

    def pin(*fragments):
        pinned.extend(re.sub(r'\s+', '', f) for f in fragments)

    def xi24_at(px, py, face_seed, frame):
        step = (((frame * 0x9e3779b9) & M32) >> 8) if frame else 0
        return ((2 * tile[(py & 63) * 64 + (px & 63)] + 1) * 2048 + (face_seed >> 8) + step) & 0xffffff

    def r_of(a, s, xi):
        return min(int(math.floor(f32(f32(f32(a) * s) + f32(xi / 16777216.0)))), s)

    def pick(seed, px, py):
        return f32((h(seed ^ h((px | (py << 16)) & M32) ^ 0x5bd1e995) >> 8) / 16777216.0)

    def mask(table, a, face_seed, frame, px, py, s):
        r = r_of(min(max(a, 0.0), 1.0), s, xi24_at(px, py, face_seed, frame))
        column = min(int(f32(pick(frame_seed(face_seed, frame), px, py) * SEEDS)), SEEDS - 1)
        return table[r * SEEDS + column]

    entries, off, cnt = [], [], []
    for r in range(5):
        off.append(len(entries))
        cnt.append(0)
        for m in range(16):
            if bin(m).count('1') == r:
                entries.append(m)
                cnt[r] += 1

    def keeps(cx, cy, layer, a):
        """sst_keep with the tile's dither at the block (D3) and a fixed pattern"""
        bx, by = (cx >> 1) & 0xffff, (cy >> 1) & 0xffff
        r = r_of(min(max(a, 0.0), 1.0), 4, xi24_at(bx, by, layer, 0))
        i = off[r] + min(int(f32(pick(layer, bx, by) * cnt[r])), cnt[r] - 1)
        return (entries[i] >> ((cx & 1) | ((cy & 1) << 1))) & 1

    def lit(layers, size=128):
        """the share of a size x size region's texels that no layer (seed, alpha) casts (128: one tile period of blocks)"""
        free = sum(1 for y in range(size) for x in range(size) if not any(keeps(x, y, l, a) for l, a in layers))
        return free / (size * size)

    s53 = layer_seed(5, 3)
    t8 = mask_table(8)
    s, s_far = layer_seed(123456, 9), layer_seed(0xcafef00d, 77)
    pin(*('hash(%s) == 0x%08xu' % (k, h(v)) for k, v in (('0', 0), ('1', 1), ('7', 7), ('0xdeadbeefu', 0xdeadbeef))))
    pin('s53 == 0x%08xu' % s53, 'layerSeed(123456, 0) == 0x%08xu' % layer_seed(123456, 0),
        'frameSeed(s53, 1) == 0x%08xu' % frame_seed(s53, 1), 'frameSeed(s53, 8) == 0x%08xu' % frame_seed(s53, 8))
    pin(*('xi24At(%d, %d, s53, %d) == %du' % (x, y, f, xi24_at(x, y, s53, f)) for x, y, f in ((0, 0, 0), (17, 42, 0), (63, 63, 0), (5, 9, 1), (5, 9, 2))))
    pin('xi24At(1000, 2000, 0xdeadbeefu, 123456789u) == %du' % xi24_at(1000, 2000, 0xdeadbeef, 123456789),
        'xi24At(65535, 1, 0x416ef572u, 0xffffffffu) == %du' % xi24_at(65535, 1, 0x416ef572, 0xffffffff))
    pin(*('rOf(0.37f, %d, xi24At(%d, %d, s53, 0)) == %d' % (n, x, y, r_of(0.37, n, xi24_at(x, y, s53, 0))) for x, y in ((0, 0), (17, 42)) for n in (8, 4)))
    pin(*('pick(s53, %d, %d) == (float)%r' % (x, y, pick(s53, x, y)) for x, y in ((0, 0), (17, 42), (65535, 1))))
    pin('mask(t8, 0.37f, s53, 0, 0, 0, 8) == 0x%02xu' % mask(t8, 0.37, s53, 0, 0, 0, 0 + 8),
        'mask(t8, 0.5f, s53, 0, 17, 42, 8) == 0x%02xu' % mask(t8, 0.5, s53, 0, 17, 42, 8),
        'mask(t8, 0.9f, s, 0, 100, 7, 8) == 0x%02xu' % mask(t8, 0.9, s, 0, 100, 7, 8),
        'mask(t8, 0.37f, s53, 1, 3, 4, 8) == 0x%02xu' % mask(t8, 0.37, s53, 1, 3, 4, 8),
        'mask(t8, 0.37f, s53, 2, 3, 4, 8) == 0x%02xu' % mask(t8, 0.37, s53, 2, 3, 4, 8),
        'mask(t8, 0.25f, s_far, 0xfffffffeu, 2560, 1369, 8) == 0x%02xu' % mask(t8, 0.25, s_far, 0xfffffffe, 2560, 1369, 8))
    print('// test_dither')
    print('hash(0) == 0x%08xu && hash(1) == 0x%08xu && hash(7) == 0x%08xu && hash(0xdeadbeefu) == 0x%08xu' % (h(0), h(1), h(7), h(0xdeadbeef)))
    print('s53 == 0x%08xu && layerSeed(123456, 0) == 0x%08xu' % (s53, layer_seed(123456, 0)))
    print('frameSeed(s53, 1) == 0x%08xu && frameSeed(s53, 8) == 0x%08xu' % (frame_seed(s53, 1), frame_seed(s53, 8)))
    print('xi24At(0, 0, s53, 0) == %du && xi24At(17, 42, s53, 0) == %du && xi24At(63, 63, s53, 0) == %du' % (
        xi24_at(0, 0, s53, 0), xi24_at(17, 42, s53, 0), xi24_at(63, 63, s53, 0)))
    print('xi24At(5, 9, s53, 1) == %du && xi24At(5, 9, s53, 2) == %du' % (xi24_at(5, 9, s53, 1), xi24_at(5, 9, s53, 2)))
    print('xi24At(1000, 2000, 0xdeadbeefu, 123456789u) == %du && xi24At(65535, 1, 0x416ef572u, 0xffffffffu) == %du' % (
        xi24_at(1000, 2000, 0xdeadbeef, 123456789), xi24_at(65535, 1, 0x416ef572, 0xffffffff)))
    print('rOf(0.37f, 8|4, xi24At(0, 0, s53, 0)) == %d|%d, rOf(0.37f, 8|4, xi24At(17, 42, s53, 0)) == %d|%d' % (
        r_of(0.37, 8, xi24_at(0, 0, s53, 0)), r_of(0.37, 4, xi24_at(0, 0, s53, 0)),
        r_of(0.37, 8, xi24_at(17, 42, s53, 0)), r_of(0.37, 4, xi24_at(17, 42, s53, 0))))
    print('pick(s53, 0, 0) == (float)%r && pick(s53, 17, 42) == (float)%r && pick(s53, 65535, 1) == (float)%r' % (
        pick(s53, 0, 0), pick(s53, 17, 42), pick(s53, 65535, 1)))
    print('mask(t8, 0.37f, s53, 0, 0, 0, 8) == 0x%02xu && mask(t8, 0.5f, s53, 0, 17, 42, 8) == 0x%02xu && mask(t8, 0.9f, s, 0, 100, 7, 8) == 0x%02xu &&' % (
        mask(t8, 0.37, s53, 0, 0, 0, 8), mask(t8, 0.5, s53, 0, 17, 42, 8), mask(t8, 0.9, s, 0, 100, 7, 8)))
    print('mask(t8, 0.37f, s53, 1, 3, 4, 8) == 0x%02xu && mask(t8, 0.37f, s53, 2, 3, 4, 8) == 0x%02xu &&' % (
        mask(t8, 0.37, s53, 1, 3, 4, 8), mask(t8, 0.37, s53, 2, 3, 4, 8)))
    print('mask(t8, 0.25f, s_far, 0xfffffffeu, 2560, 1369, 8) == 0x%02xu' % mask(t8, 0.25, s_far, 0xfffffffe, 2560, 1369, 8))

    print('// test_caster')
    print('casterLayer(5, 1, false) == s53: %s' % (caster_layer(5, 1, False) == s53))
    rows = [sum(keeps(x, y, s53, 0.37) << x for x in range(16)) for y in range(16)]
    print('fixed[16] (alpha 0.37, layer s53) = { %s }' % ', '.join('0x%04x' % v for v in rows))
    layer = caster_layer(5, 7, True)
    print('casterLayer(5, 7, true) == 0x%08xu && casterLayer(5, 7, false) == 0x%08xu' % (layer, caster_layer(5, 7, False)))
    rows = [sum(keeps(x, y, layer, 0.6) << x for x in range(8)) for y in range(8)]
    print('rows[8] (alpha 0.6, casterLayer(5, 7, true)) = { %s }' % ', '.join('0x%02x' % v for v in rows))
    print('casterKeeps(-1, -1) %d, (-2, 5) %d, (-65536, 3) %d, (70000, -9) %d (s53, alpha 0.5)' % (
        keeps(-1, -1, s53, 0.5), keeps(-2, 5, s53, 0.5), keeps(-65536, 3, s53, 0.5), keeps(70000, -9, s53, 0.5)))
    pin('casterLayer(5, 1, false) == s53' if caster_layer(5, 1, False) == s53 else 'casterLayer(5, 1, false) != s53')
    pin('fixed[16] = { %s }' % ', '.join('0x%04x' % sum(keeps(x, y, s53, 0.37) << x for x in range(16)) for y in range(16)))
    pin('layer == 0x%08xu' % layer, 'casterLayer(5, 7, false) == 0x%08xu' % caster_layer(5, 7, False))
    pin('rows[8] = { %s }' % ', '.join('0x%02x' % v for v in rows))
    pin(*('%scasterKeeps(%d, %d, s53, 0.5f)' % ('' if keeps(x, y, s53, 0.5) else '!', x, y) for x, y in ((-1, -1), (-2, 5), (-65536, 3), (70000, -9))))
    worst = 0.0
    for a in (0.03, 0.1, 0.3, 0.5, 0.8, 0.98):
        light = lit([(s53, a)])
        worst = max(worst, abs(light - (1.0 - a)))
        print('caster alpha %.2f: %.6f of the light passes (1 - alpha %+.2e)' % (a, light, light - (1.0 - a)))
    print('worst |light - (1 - alpha)| %.2e (one stratum of R over the tile: 1/16384 = %.2e)' % (worst, 1.0 / 16384))
    a, other_draw, other_tri, back = caster_layer(11, 7, True), caster_layer(12, 7, True), caster_layer(11, 8, True), caster_layer(11, 7, False)
    pairs = (lit([(a, 0.5), (other_draw, 0.5)]), lit([(a, 0.5), (other_tri, 0.5)]), lit([(a, 0.5), (back, 0.5)]))
    three = lit([(a, 0.3), (other_draw, 0.3), (other_tri, 0.3)])
    print('0.5 + 0.5: two draws %.4f, two triangles %.4f, two sides %.4f; 3 x 0.3 %.4f (product 0.343)' % (pairs + (three,)))
    if not args.check:
        return 0
    # the C++ test pins these spellings, and bounds the coverage as checked here (test_caster)
    text = re.sub(r'\s+', '', CPP_TEST.read_text(encoding='utf-8'))
    for fragment in pinned:
        # a texel the rule keeps is spelled without the '!' (preceded by '(', '&' or ' ')
        found = fragment in text and not (not fragment.startswith('!') and '!' + fragment in text)
        if not found:
            print('FAIL: the C++ test does not pin', fragment)
            return 1
    bounds = [(worst < 1.0 / 16384 + 1e-6, 'one layer lets 1 - alpha through within one stratum'),
              (all(abs(v - 0.25) < 0.01 for v in pairs), 'two layers of 0.5 pass 0.25 of the light'),
              (abs(three - 0.343) < 0.01, 'three layers of 0.3 pass 0.343 of the light')]
    for ok, what in bounds:
        if not ok:
            print('FAIL:', what)
            return 1
    print('gen_stochastic_goldens: the C++ test pins all %d values, and the coverage is within its bounds' % len(pinned))
    return 0


if __name__ == '__main__':
    sys.exit(main())
