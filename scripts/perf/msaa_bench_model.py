# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""CPU-side logic of the deferred MSAA bench (scripts/perf/bench_deferred_msaa.py).

Pure Python, no GL: the viewer's G-buffer layout and per-format sizes, the memory table
the study quotes, the synthetic scenes, the edge-classification rule the bench's GLSL
mirrors, and the reference resolves (box and tonemap-weighted) the bench checks its GPU
output against. scripts/tests/test_msaa_bench_model.py tests this module."""
import math
import random
import statistics

# GL internal format -> bytes per sample. RGB16F and DEPTH_COMPONENT24 are padded by the
# driver to 8 and 4 bytes (assumed; the bench prints the measured video-memory drop where
# GL_NVX_gpu_memory_info exists).
FORMAT_BYTES = {
    'RGBA8': 4, 'RGB10_A2': 4, 'RGBA16': 8, 'RGB16F': 8, 'RGBA16F': 8, 'R11F_G11F_B10F': 4,
    'RG16F': 4, 'R8': 1, 'R16F': 2, 'R32UI': 4, 'DEPTH_COMPONENT24': 4, 'DEPTH24_STENCIL8': 4,
    'DEPTH_COMPONENT32F': 4,
}

GL_FORMATS = {
    'RGBA8': 0x8058, 'RGB10_A2': 0x8059, 'RGBA16': 0x805B, 'RGB16F': 0x881B,
    'RGBA16F': 0x881A, 'R11F_G11F_B10F': 0x8C3A, 'RG16F': 0x822F, 'R8': 0x8229,
    'R16F': 0x822D, 'R32UI': 0x8236, 'DEPTH_COMPONENT24': 0x81A6,
    'DEPTH24_STENCIL8': 0x88F0, 'DEPTH_COMPONENT32F': 0x8CAC,
}

RESOLUTIONS = {'1080p': (1920, 1080), '1440p': (2560, 1440)}


def gbuffer_layout(hdr=True, emissive=True, stencil=False):
    """mRT->deferredScreen as pipeline.cpp allocates it (allocateScreenBufferInternal and
    addDeferredAttachments): diffuse RGBA8, specular/ORM RGBA8, normal RGBA16 (HDR) or
    RGB10_A2, optional emissive RGB16F (HDR) or RGB8 (counted as RGBA8), and the D24 depth
    texture. stencil=True is the design's D24S8 (same size) for edge classification."""
    layout = [('diffuse', 'RGBA8'), ('specular', 'RGBA8'),
              ('normal', 'RGBA16' if hdr else 'RGB10_A2')]
    if emissive:
        layout.append(('emissive', 'RGB16F' if hdr else 'RGBA8'))
    layout.append(('depth', 'DEPTH24_STENCIL8' if stencil else 'DEPTH_COMPONENT24'))
    return layout


def bytes_per_sample(layout):
    return sum(FORMAT_BYTES[f] for _, f in layout)


def msaa_memory(width, height, samples, hdr=True, emissive=True):
    """Bytes of the scene targets under the recommended design (design A of the study):
    the G-buffer and depth at `samples`, the lit scene colour mRT->screen (RGBA16F) at
    `samples`, plus the single-sample resolve targets that design adds: a resolved RGBA16F
    colour (the existing single-sample mRT->screen is reused, so it is counted once in the
    baseline) and a resolved D24 depth for the post chain. samples=1 is today's frame
    (G-buffer + depth + screen, no resolve targets)."""
    px = width * height
    gbuf = bytes_per_sample(gbuffer_layout(hdr, emissive, stencil=samples > 1)) * px * samples
    screen = FORMAT_BYTES['RGBA16F'] * px * samples
    resolve = 0
    if samples > 1:
        resolve = (FORMAT_BYTES['RGBA16F'] + FORMAT_BYTES['DEPTH_COMPONENT24']) * px
    return {'gbuffer': gbuf, 'screen': screen, 'resolve': resolve,
            'total': gbuf + screen + resolve}


def memory_table(samples_list=(1, 2, 4, 8), resolutions=RESOLUTIONS, hdr=True, emissive=True):
    """Rows (name, samples, MiB G-buffer+depth, MiB screen, MiB resolve, MiB total, MiB
    added over samples=1)."""
    rows = []
    for name, (w, h) in resolutions.items():
        base = msaa_memory(w, h, 1, hdr, emissive)['total']
        for n in samples_list:
            m = msaa_memory(w, h, n, hdr, emissive)
            rows.append((name, n, mib(m['gbuffer']), mib(m['screen']), mib(m['resolve']),
                         mib(m['total']), mib(m['total'] - base)))
    return rows


def mib(b):
    return b / (1024.0 * 1024.0)


# --- synthetic scenes ---
# name -> (triangle count, triangle size in NDC, thin factor). The ground plane is added
# by the bench. The counts were tuned so the measured edge fractions bracket what a
# scene in Second Life shows: an open landscape, a built-up region, and foliage/hair.
SCENES = {
    'open': (300, 0.35, 1.0),
    'dense': (6000, 0.08, 1.0),
    'foliage': (40000, 0.05, 0.12),
}


def scene_triangles(name, seed=1):
    """Deterministic triangle list: [(x, y, z, nx, ny, nz, id) * 3] per triangle, NDC xy,
    window z in [0.05, 0.95], a per-triangle id (material) and per-vertex normals that
    vary a little across a triangle (smooth shading), all facing the viewer (nz > 0)."""
    count, size, thin = SCENES[name]
    rng = random.Random(seed)
    tris = []
    for i in range(count):
        cx, cy = rng.uniform(-1.1, 1.1), rng.uniform(-1.1, 1.1)
        z = rng.uniform(0.05, 0.95)
        slope = rng.uniform(-0.02, 0.02)
        ang = rng.uniform(0.0, 2.0 * math.pi)
        base = normalize((rng.uniform(-0.6, 0.6), rng.uniform(-0.6, 0.6), 1.0))
        verts = []
        for k in range(3):
            a = ang + k * 2.0 * math.pi / 3.0
            r = size * (thin if k else 1.0)
            x, y = cx + r * math.cos(a), cy + r * math.sin(a)
            n = normalize((base[0] + rng.uniform(-0.05, 0.05), base[1] + rng.uniform(-0.05, 0.05), base[2]))
            verts.append((x, y, min(max(z + slope * k, 0.0), 1.0), n[0], n[1], n[2], float(i % 4096)))
        tris.append(verts)
    return tris


def normalize(v):
    l = math.sqrt(sum(c * c for c in v))
    return tuple(c / l for c in v)


def encode_normal(n):
    """The viewer's encodeNormal xy (globalF.glsl): n.xy / sqrt(8 n.z + 8) + 0.5."""
    f = math.sqrt(8.0 * n[2] + 8.0)
    return (n[0] / f + 0.5, n[1] / f + 0.5)


def decode_normal(e):
    """The viewer's decodeNormal."""
    fx, fy = e[0] * 4.0 - 2.0, e[1] * 4.0 - 2.0
    f = fx * fx + fy * fy
    g = math.sqrt(max(1.0 - f / 4.0, 0.0))
    return (fx * g, fy * g, 1.0 - f / 2.0)


# --- edge classification (mirrored by EDGE_GLSL in the bench) ---
EDGE_DEPTH_EPS = 2.0e-3     # window-z difference that marks a depth discontinuity
EDGE_NORMAL_EPS = 0.02      # encoded-normal difference (about 4.5 degrees)
EDGE_ALBEDO_EPS = 4.0 / 255.0


def is_edge(samples):
    """samples: list of (depth, (enc_nx, enc_ny), flag, (r, g, b)) per sample of one pixel.
    A pixel is complex when any sample differs from sample 0 beyond the thresholds; a
    pixel whose samples all come from one triangle differs only in depth (MSAA evaluates
    depth per sample, the other attributes once per pixel) and within the tolerance."""
    z0, n0, f0, a0 = samples[0]
    for z, n, f, a in samples[1:]:
        if abs(z - z0) > EDGE_DEPTH_EPS:
            return True
        if abs(n[0] - n0[0]) > EDGE_NORMAL_EPS or abs(n[1] - n0[1]) > EDGE_NORMAL_EPS:
            return True
        if f != f0:
            return True
        if max(abs(a[i] - a0[i]) for i in range(3)) > EDGE_ALBEDO_EPS:
            return True
    return False


# --- resolves ---
def box_resolve(colors):
    """glBlitFramebuffer's resolve of an RGBA16F MS target: the sample mean."""
    n = len(colors)
    return tuple(sum(c[i] for c in colors) / n for i in range(len(colors[0])))


def luma_max(c):
    return max(c[0], c[1], c[2])


def tonemap_weighted_resolve(colors, exposure=1.0):
    """Resolve "after tonemapping" without tonemapping the output: each sample is weighted
    by 1 / (1 + exposure * max(r, g, b)), the weights renormalised (Karis 2014). For the
    Reinhard curve on max(rgb) the weighted average equals tonemap -> average -> inverse
    tonemap exactly when every sample's largest channel is the same channel, and is close
    otherwise. A bright sample at an edge then contributes its coverage share of the
    displayed brightness instead of dominating the pixel. The alpha channel (glow) is
    resolved with the same weights."""
    ws = [1.0 / (1.0 + exposure * luma_max(c)) for c in colors]
    t = sum(ws)
    return tuple(sum(w * c[i] for w, c in zip(ws, colors)) / t for i in range(len(colors[0])))


def reinhard(c, exposure=1.0):
    return tuple(exposure * x / (1.0 + exposure * luma_max(c)) for x in c[:3])


def inverse_reinhard(c, exposure=1.0):
    m = luma_max(c)
    return tuple(x / (exposure * (1.0 - m)) for x in c[:3])


def resolve_sample_color(px, py, s, samples):
    """The pattern the bench writes per sample for its resolve check (also in GLSL):
    an HDR edge where one sample in `samples` is very bright."""
    v = ((px * 7 + py * 13 + s * 5) % 17) / 16.0
    bright = 40.0 if s == (px + py) % samples else 0.0
    return (v + bright, 0.5 * v, 0.25 + 0.1 * s, (s + 1) / float(samples))


# --- timing summaries ---
def median(values):
    return statistics.median(values)


def frame_estimate(timings, samples, variant):
    """Scene-pass GPU ms of one configuration: G-buffer fill + classification (edge
    variants) + lighting + resolve (design A; the resolve timed after that variant) + the
    depth resolve. `timings` maps pass -> ms."""
    total = timings['gfill'] + timings[variant]
    if variant.endswith('edge') and samples > 1:
        total += timings['classify']
    if variant.startswith('a_') and samples > 1:
        tone = timings['resolve_tone_all'] if variant == 'a_all' else timings['resolve_tone']
        total += tone + timings['resolve_depth']
    return total
