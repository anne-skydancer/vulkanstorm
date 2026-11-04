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
"""
import math
import random
import struct
import zlib

PATTERNS = {'full': 0, 'sparse': 1, 'bright': 2, 'foliage': 3, 'opaque': 4}
STRIPE_PERIOD = 10
STRIPE_WIDTH = 4
# Keeps x*dx + y*dy + offset non-negative for |dx|, |dy| <= 2 below 100000 px, so the GLSL
# integer % (undefined for negative operands) agrees with Python's.
STRIPE_BIAS = STRIPE_PERIOD * 100000
STRIPE_DIRS = [(1, 0), (0, 1), (1, 1), (1, -1), (2, 1), (1, 2), (-1, 2), (2, -1)]
FOLIAGE_CORE_ALPHA = 0.95
FOLIAGE_EDGE_ALPHA = 0.15
BRIGHT_ALPHAS = (0.3, 0.22, 0.37, 0.27)

SCENE_GLSL = '''
uniform int pattern;
uniform vec4 layer_color;     // rgb, alpha (the core alpha for foliage)
uniform ivec4 stripe;         // foliage: dx, dy, offset (biased non-negative), width
uniform float stripe_edge_alpha;
#define STRIPE_PERIOD %d
vec4 scene_color()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
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


def _frac(x):
    return x - math.floor(x)


def scene(name, layers):
    """Return (background rgb, [Layer]) for a pattern name and a layer count."""
    out = []
    for i in range(layers):
        z = layer_depth(i, layers)
        if name in ('full', 'sparse', 'opaque'):
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
                  'bright': (0.0, 0.0, 0.0), 'foliage': (0.35, 0.5, 0.75)}[name]
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
    return 0


def exact_pixel(name, background, layers, x, y):
    """Exact back-to-front composite: (rgb, total alpha, fragment count) over the background."""
    frags = []
    for layer in layers:
        a = fragment_alpha(name, layer, x, y)
        if a is not None and a > 0.0:
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
