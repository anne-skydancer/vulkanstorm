# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Scenes and CPU reference for scripts/perf/bench_alpha_oit.py; no GL, no dependencies.

A scene is a background colour and a list of full-screen layers drawn in index order. Each
layer has a colour, an alpha, a window depth and a coverage pattern. SCENE_GLSL is the
fragment-side half of the scene (coverage and per-pixel alpha) that every bench
implementation calls; fragment_alpha() is its Python mirror, using the same integer
arithmetic, so exact_window() can composite the same fragments back to front on the CPU:
the analytic reference the bench measures its RMSE against.

Patterns:
  full     every pixel, alpha 0.5, a distinct colour per layer (layer 0 is the bench's
           original 0.8, 0.5, 0.2) over a mid-grey background
  sparse   as full on 25% of 64x64 blocks (a hair/particle clump proxy)
  bright   bright, low-alpha layers (alpha 0.22-0.37, off the k/S grid) over a black
           background: the highest relative variance case for stochastic transparency
  foliage  thin stripes 4 px wide with period 10 px in eight directions, 40% coverage per
           layer, alpha 0.95 in the stripe core and 0.15 on its two edge pixels (high alpha
           contrast); at 32 layers about 13 fragments per pixel, past the 8 slots of PPLL
  opaque   alpha 1 layers: the composite is the nearest layer, which both methods must
           reproduce
  deep     as full, but the window depths are those of a perspective projection (near 0.1 m,
           far 256 m) of layers 5 cm apart from 40 m: stacked glass or clothing seen from a
           distance, where a 16-bit depth cannot separate the layers (DEEP_* below)
  occl     as full, over an opaque depth of 0.537 on 8 px wide vertical bars (every other bar),
           so about half of the layers are hidden on the bars: opaque silhouettes, where a
           half-resolution stochastic depth must not borrow visibility across the edge

Also here, the CPU side of the round 2 variants: blue_noise() (the threshold map of the
blue-noise masks), upsample_weights() (the depth-aware upsample of a half-resolution
stochastic depth) and error_stats_blurred() (the error after a 3x3 binomial filter, a proxy
for the noise the eye sees at a normal viewing distance).
"""
import math
import random
import struct
import zlib

PATTERNS = {'full': 0, 'sparse': 1, 'bright': 2, 'foliage': 3, 'opaque': 4, 'deep': 5, 'occl': 6}
DEEP_NEAR = 0.1       # m, the camera near plane
DEEP_FAR = 256.0      # m, the far plane
DEEP_DISTANCE = 40.0  # m, the nearest layer
DEEP_SPACING = 0.05   # m between layers
OCCL_BAR = 8          # px, width of the opaque bars and of the gaps between them
OCCL_DEPTH = 0.537    # window depth of the opaque bars, off the layer depth grid (no LEQUAL ties)
STRIPE_PERIOD = 10
STRIPE_WIDTH = 4
# Keeps x*dx + y*dy + offset non-negative for |dx|, |dy| <= 2 below 100000 px, so the GLSL
# integer % (undefined for negative operands) agrees with Python's.
STRIPE_BIAS = STRIPE_PERIOD * 100000
STRIPE_DIRS = [(1, 0), (0, 1), (1, 1), (1, -1), (2, 1), (1, 2), (-1, 2), (2, -1)]
FOLIAGE_CORE_ALPHA = 0.95
FOLIAGE_EDGE_ALPHA = 0.15
BRIGHT_ALPHAS = (0.3, 0.22, 0.37, 0.27)
# Screen-wide fraction of pixels a layer covers, where it is not 1.
COVERAGE = {'sparse': 0.25, 'foliage': STRIPE_WIDTH / STRIPE_PERIOD}

SCENE_GLSL = '''
uniform int pattern;
uniform vec4 layer_color;     // rgb, alpha (the core alpha for foliage)
uniform ivec4 stripe;         // foliage: dx, dy, offset (biased non-negative), width
uniform float stripe_edge_alpha;
#define STRIPE_PERIOD %d
// The pixel the coverage pattern is evaluated at; a half-resolution pass redefines it.
#ifndef SCENE_PIXEL
#define SCENE_PIXEL ivec2(gl_FragCoord.xy)
#endif
vec4 scene_color()
{
    ivec2 p = SCENE_PIXEL;
    if (pattern == 1 && (((p.x >> 6) ^ (p.y >> 6)) & 3) != 0) discard;
    vec4 c = layer_color;
    if (pattern == 3)
    {
        int m = (p.x * stripe.x + p.y * stripe.y + stripe.z) %% STRIPE_PERIOD;
        if (m >= stripe.w) discard;
        if (m == 0 || m == stripe.w - 1) c.a = stripe_edge_alpha;
    }
    return c;
}
''' % STRIPE_PERIOD


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


class Layer:
    def __init__(self, color, alpha, depth, stripe=(0, 0, 0, 0), edge_alpha=0.0):
        self.color = tuple(f32(c) for c in color)
        self.alpha = f32(alpha)
        self.depth = f32(depth)
        self.stripe = stripe
        self.edge_alpha = f32(edge_alpha)


def layer_depth(i, layers):
    # The bench's original depth permutation: draw order i, distinct depths, unsorted.
    return 0.05 + 0.9 * ((i * 7919) % layers) / max(layers, 1)


def perspective_depth(d, near=DEEP_NEAR, far=DEEP_FAR):
    """Window depth (glDepthRange 0..1) of eye distance d under a standard perspective
    projection: far (d - near) / ((far - near) d)."""
    return far * (d - near) / ((far - near) * d)


def opaque_depth(name, x, y):
    """The scene's opaque depth at pixel (x, y): 1.0 (nothing opaque) except on the occl bars."""
    if name == 'occl' and (x // OCCL_BAR) & 1:
        return OCCL_DEPTH
    return 1.0


def _frac(x):
    return x - math.floor(x)


def scene(name, layers):
    """Return (background rgb, [Layer]) for a pattern name and a layer count."""
    out = []
    for i in range(layers):
        z = layer_depth(i, layers)
        if name == 'deep':
            z = perspective_depth(DEEP_DISTANCE + DEEP_SPACING * ((i * 7919) % layers))
        if name in ('full', 'sparse', 'opaque', 'deep', 'occl'):
            if i == 0:
                rgb = (0.8, 0.5, 0.2)
            else:
                rgb = (0.15 + 0.8 * _frac(i * 0.618034), 0.15 + 0.8 * _frac(i * 0.414214 + 0.3),
                       0.15 + 0.8 * _frac(i * 0.732051 + 0.6))
            out.append(Layer(rgb, 1.0 if name == 'opaque' else 0.5, z))
        elif name == 'bright':
            rgb = (1.0, 0.85 + 0.15 * _frac(i * 0.618034), 0.7 + 0.3 * _frac(i * 0.414214))
            out.append(Layer(rgb, BRIGHT_ALPHAS[i % len(BRIGHT_ALPHAS)], z))
        elif name == 'foliage':
            rgb = (0.08 + 0.2 * _frac(i * 0.618034), 0.3 + 0.45 * _frac(i * 0.414214 + 0.5),
                   0.04 + 0.16 * _frac(i * 0.732051))
            dx, dy = STRIPE_DIRS[i % len(STRIPE_DIRS)]
            offset = STRIPE_BIAS + (i * 3 + i // len(STRIPE_DIRS)) % STRIPE_PERIOD
            out.append(Layer(rgb, FOLIAGE_CORE_ALPHA, z, (dx, dy, offset, STRIPE_WIDTH), FOLIAGE_EDGE_ALPHA))
        else:
            raise ValueError(f'unknown scene pattern {name!r}')
    background = {'full': (0.25, 0.25, 0.25), 'sparse': (0.25, 0.25, 0.25), 'opaque': (0.25, 0.25, 0.25),
                  'deep': (0.25, 0.25, 0.25), 'occl': (0.25, 0.25, 0.25), 'bright': (0.0, 0.0, 0.0), 'foliage': (0.35, 0.5, 0.75)}[name]
    return background, out


def fragment_alpha(name, layer, x, y):
    """Alpha of the layer's fragment at pixel (x, y), or None where it discards (SCENE_GLSL)."""
    if name == 'sparse' and ((x >> 6) ^ (y >> 6)) & 3:
        return None
    if name == 'foliage':
        dx, dy, offset, width = layer.stripe
        m = (x * dx + y * dy + offset) % STRIPE_PERIOD
        if m >= width:
            return None
        return layer.edge_alpha if m in (0, width - 1) else layer.alpha
    return layer.alpha


def pixel_key(name, x, y):
    # Pixels with equal keys receive the same fragments, so share one exact composite.
    if name == 'sparse':
        return ((x >> 6) ^ (y >> 6)) & 3 == 0
    if name == 'foliage':
        return (x % STRIPE_PERIOD, y % STRIPE_PERIOD)
    if name == 'occl':
        return (x // OCCL_BAR) & 1
    return 0


def exact_pixel(name, background, layers, x, y):
    """Exact back-to-front composite: (rgb, total alpha, fragment count) over the background.
    Fragments behind the opaque depth are hidden (the capture's LEQUAL reject)."""
    frags = []
    opaque = opaque_depth(name, x, y)
    for layer in layers:
        a = fragment_alpha(name, layer, x, y)
        if a is not None and a > 0.0 and layer.depth <= opaque:
            frags.append((layer.depth, layer.color, a))
    frags.sort(key=lambda f: -f[0])   # far to near
    rgb = list(background)
    trans = 1.0
    for _, c, a in frags:
        rgb = [c[k] * a + rgb[k] * (1.0 - a) for k in range(3)]
        trans *= 1.0 - a
    return tuple(rgb), 1.0 - trans, len(frags)


def exact_window(name, background, layers, x0, y0, w, h):
    """Row-major list of exact_pixel() results for the window, bottom row first (GL order)."""
    cache = {}
    out = []
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            k = pixel_key(name, x, y)
            if k not in cache:
                cache[k] = exact_pixel(name, background, layers, x, y)
            out.append(cache[k])
    return out


def mask_table(samples, entries=64, seed=0x5EED):
    """Stratified coverage masks: row r holds `entries` S-bit masks with exactly r bits set,
    each drawn uniformly from all C(S, r) such masks (a uniformly random r-subset of the
    samples). Rows 0..S, so (S + 1) * entries words."""
    rng = random.Random(seed + samples)
    rows = []
    for r in range(samples + 1):
        row = []
        for _ in range(entries):
            mask = 0
            for bit in rng.sample(range(samples), r):
                mask |= 1 << bit
            row.append(mask)
        rows.append(row)
    return rows


def error_stats(gpu_rgb, exact):
    """RMSE and max absolute error in 8-bit units over the alpha-covered pixels.

    gpu_rgb: list of (r, g, b) floats; exact: exact_window() results in the same order."""
    total = 0.0
    worst = 0.0
    n = 0
    for got, (want, _, count) in zip(gpu_rgb, exact):
        if count == 0:
            continue
        for k in range(3):
            e = 255.0 * (got[k] - want[k])
            total += e * e
            worst = max(worst, abs(e))
        n += 3
    return (math.sqrt(total / n) if n else 0.0), worst


def error_stats_blurred(gpu_rgb, exact, w, h):
    """RMSE in 8-bit units after a 3x3 binomial filter (1 2 1 / 2 4 2 / 1 2 1, / 16) of both the
    image and the exact composite, over the alpha-covered pixels whose 3x3 neighbourhood lies
    in the window: high-frequency noise (dither, blue noise) drops, a bias or a low-frequency
    blotch does not; a proxy for the noise seen at a normal viewing distance. gpu_rgb and exact
    are row-major windows of w x h."""
    kernel = ((-1, -1, 1), (0, -1, 2), (1, -1, 1), (-1, 0, 2), (0, 0, 4), (1, 0, 2),
              (-1, 1, 1), (0, 1, 2), (1, 1, 1))
    total = 0.0
    n = 0
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            if exact[y * w + x][2] == 0:
                continue
            for k in range(3):
                e = 0.0
                for dx, dy, wt in kernel:
                    i = (y + dy) * w + x + dx
                    e += wt * (gpu_rgb[i][k] - exact[i][0][k])
                e *= 255.0 / 16.0
                total += e * e
                n += 1
    return math.sqrt(total / n) if n else 0.0


def blue_noise(size=32, sigma=1.5, seed=0xB1E):
    """A size x size blue-noise threshold map by void-and-cluster (Ulichney, 1993) with a
    toroidal Gaussian energy. Returns row-major thresholds (rank + 0.5) / size^2: a permutation
    of size^2 equally spaced values in (0, 1). Phase 3 is simplified: the voids past half are
    filled by the phase 2 rule."""
    n = size * size
    radius = min(size // 2, int(math.ceil(3 * sigma)))
    kern = [(dx, dy, math.exp(-(dx * dx + dy * dy) / (2 * sigma * sigma)))
            for dy in range(-radius, radius + 1) for dx in range(-radius, radius + 1)]

    def spread(e, i, sign):
        x0, y0 = i % size, i // size
        for dx, dy, wt in kern:
            e[((y0 + dy) % size) * size + (x0 + dx) % size] += sign * wt

    def energy_of(points):
        e = [0.0] * n
        for i in points:
            spread(e, i, 1.0)
        return e

    rng = random.Random(seed)
    # Initial binary pattern: 10% random points, relaxed until the tightest cluster is also the
    # largest void.
    ones = set(rng.sample(range(n), n // 10))
    e = energy_of(ones)
    while True:
        cluster = max(ones, key=lambda i: e[i])
        ones.discard(cluster)
        spread(e, cluster, -1.0)
        void = min((i for i in range(n) if i not in ones), key=lambda i: e[i])
        ones.add(void)
        spread(e, void, 1.0)
        if void == cluster:
            break
    rank = [0] * n
    # Phase 1: remove the tightest clusters of the initial pattern, ranking downwards.
    pts = set(ones)
    e = energy_of(pts)
    for r in range(len(ones) - 1, -1, -1):
        cluster = max(pts, key=lambda i: e[i])
        pts.discard(cluster)
        spread(e, cluster, -1.0)
        rank[cluster] = r
    # Phase 2: fill the largest voids, ranking upwards.
    pts = set(ones)
    e = energy_of(pts)
    for r in range(len(ones), n):
        void = min((i for i in range(n) if i not in pts), key=lambda i: e[i])
        pts.add(void)
        spread(e, void, 1.0)
        rank[void] = r
    return [(r + 0.5) / n for r in rank]


def upsample_weights(fx, fy, z, seeds):
    """Depth-aware upsample of a half-resolution stochastic depth for one full-resolution
    fragment at depth z: the bilinear weights of the four half-resolution neighbours (order 00,
    10, 01, 11; fx, fy the fractional position) with every neighbour whose opaque seed (the
    farthest opaque depth of its 2x2 footprint) is nearer than z dropped, normalised to sum 1.
    The neighbour that contains the fragment's pixel has bilinear weight >= 9/16 and a seed >=
    the pixel's opaque depth, so a fragment that passed its own opaque test keeps a non-zero
    sum; all four are zero only for a fragment the capture rejects. Mirrored in the bench's
    STOCH_HALF accumulation shader."""
    w = [(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy]
    w = [wk if z <= sk else 0.0 for wk, sk in zip(w, seeds)]
    total = sum(w)
    return [wk / total for wk in w] if total > 0.0 else [0.0] * 4


def srgb8(v):
    v = min(max(v, 0.0), 1.0)
    s = 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1.0 / 2.4) - 0.055
    return int(round(255.0 * s))


def write_png(path, w, h, rgb, encode=srgb8):
    """Write an 8-bit RGB PNG. rgb: row-major (r, g, b) floats, bottom row first (GL order);
    `encode` maps a linear value to 0..255 (sRGB by default)."""
    raw = bytearray()
    for y in range(h - 1, -1, -1):
        raw.append(0)   # filter: none
        for x in range(w):
            for v in rgb[y * w + x]:
                raw.append(encode(v))

    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)
