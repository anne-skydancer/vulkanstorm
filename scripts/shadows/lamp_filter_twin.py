#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Lamp shadows L1: a CPU twin of the projector lookup (no GPU, no viewer), with its gates.

The lookup is shadowStochasticUtil.glsl's stSpotFilterTile as L1 ships it: a blocker search of 8 gathers on a Vogel disc plus one at the
receiver's own texels, the umbra and lit early-outs, then 32 bilinear gathers on a Vogel disc weighted 1 - 0.9 r^2 at the physical penumbra radius, at least
ST_SPOT_KERNEL_MIN (1.5) and at most ST_SPOT_KERNEL_TEXELS_MAX (12) tile texels (no edge reconstruction, no cap in metres). The numbers
are read from the shader, so the twin follows it. The emitter is the projector lamp's base, 4.5 cm, times the Shadow softness.

The scenes are the lamp-shadow study's (reports/lamp-shadow-method-2026-11-18, kept on the owner's machine): one caster plane (a
window cross with glass at alpha 0.5, an alpha-masked lace, an opaque diamond lattice) and one flat receiver parallel to the tile, the
window 3 m from the lamp and the floor 4.5 m (so the penumbra radius at the receiver is 0.5 x emitter x 0.5), the tile rasterised with
one sample at each texel centre (the casters as today: the hashed dither for the glass, the creator's cutoff on the mip-averaged mask),
receivers 5 mm apart. Truth: the caster's continuous transmittance convolved with the emitter's uniform disc at the receiver.

Gates (exit code 1 when one fails):
  hard steps  no neighbour pair of receivers differs by more than 0.5 where the truth is smooth (under 0.05): a texel square or a
              missed blocker (today's squares and the search's misses jump by 0.85 to 1), on glass, lace and the lattice, at tile
              texels of 8, 4, 2, 1 and 0.5 cm, at the default softness (1.035 cm) and the top (4.5 cm). The study's own count at 0.25 is
              printed beside it, not gated: the 1.5-texel floor's own ramp is steeper than 0.25 a sample on 0.5 cm texels
  widths      a straight opaque edge (2 to 45 degrees to the tile's axes): the 10-90 % width within 20 % of the uniform disc's (1.374
              times the penumbra radius) wherever the physical radius lies between the floor and the cap (the filter's weights draw it
              about 16 % narrower; at the floor it is wider by design, at the cap narrower: both reported)

Lamp shadows L3 (the light-through layer, lllightthrough.h), the same tile texels and emitters. The glass goes into the layer (tau =
(1 - a)(1 + a C) per channel, RGBA8, and the nearest caster's window depth in R32F, as it is), drawn only where it lies in front of the
tile's opaque depth (the depth test), the frame stays depth, and the lookup is stThroughFilter (a search of 8 gathers plus the
receiver's own texels for the nearest caster in front of the receiver, then 16 taps at its physical radius, weighted as the depth filter
and by the share of each tap the depth does not block; the reference lowered by the receiver plane, as the depth filter's) times the L1
depth lookup:
  grain       uniform glass between the bars: no grain (the largest neighbour step inside it is 0)
  frame       glass behind a window frame and glass with painted leading: closer to the truth than the dither, and no farther than the
              first L3 build (no depth test, no open weight; RMSE), at every tile texel; the largest step where the truth is smooth is
              printed beside it (L1's search misses a thin bar: reported, not gated)
  stack       two panes one behind the other: the product of the two, within the layer's 8 bits (2/255)
  in front    a receiver between the lamp and the window (an avatar): exactly 1 from the layer everywhere, no tinted rim
  hidden      a pane under the floor (glass in the room below), behind a pane or a glass globe in front of the floor: the floor gets the
              front one's light alone, within 2/255 (the first build multiplied the hidden pane in)
  tint edge   the edge of a red pane: the 10-90 % width of the green channel's step within 20 % of the uniform disc's where the
              physical radius lies between the floor and the cap
  tilted      a window flush in a wall lit at 30, 45 and 60 degrees: the wall next to the glass gets no tint (exactly 1; a flat
              reference tints a fringe, printed); a sheer 5 cm in front of such a wall: its light on the wall, printed (not gated)
  mask        not gated (L3.3 is not built): the lace's mean under the plan's sharpened-coverage rule and under the creator's cutoff,
              against its open share, printed as the evidence for the mask stage's open question

    python scripts/shadows/lamp_filter_twin.py [--quick] [--sheet DIR] [--l3 | --no-l3]

Needs numpy (and Pillow for --sheet). Deterministic: the hardware does not matter.
"""
import argparse
import math
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
UTIL = ROOT / 'indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl'
POOL = ROOT / 'indra/newview/llprojectorshadowpool.h'

ap = argparse.ArgumentParser()
ap.add_argument('--quick', action='store_true', help='two texel sizes and two angles')
ap.add_argument('--sheet', default=None, help='write a contact sheet per scene (PNG) into this folder')
ap.add_argument('--l3', action='store_true', help='only the L3 light-through gates')
ap.add_argument('--no-l3', action='store_true', help='only the L1 gates')
opts = ap.parse_args()


def define(name):
    return float(re.search(r'^#define %s\s+([0-9.]+)' % name, UTIL.read_text(encoding='utf-8'), re.M).group(1))


KERNEL_MIN = define('ST_SPOT_KERNEL_MIN')
KERNEL_MAX = define('ST_SPOT_KERNEL_TEXELS_MAX')
SEARCH = int(define('ST_SPOT_SEARCH'))
TAPS = int(define('ST_SPOT_TAPS'))
EMITTER_M = float(re.search(r'constexpr LampFilter kProjectorLamp\{\s*([0-9.]+)f', POOL.read_text(encoding='utf-8')).group(1))
GOLD = 2.39996323
GAP = 0.5             # (d_r - d_b) / d_b: a window 3 m from the lamp, a floor 4.5 m from it
SEARCH_RATIO = 88.0   # pen * c.z / (pen * (c.z - z_b)): near plane 0.1 m, window 3 m, floor 4.5 m (the search runs at its cap)
WINDOW_M = 0.96       # the receiver patch
STEP_M = 0.005        # receiver sample spacing (about a screen pixel at 3 to 4 m)
FINE_M = 0.001        # the truth's raster


def vogel(n):
    k = np.arange(n) + 0.5
    r = np.sqrt(k / n)
    phi = np.arange(n) * GOLD
    return np.stack([r * np.cos(phi), r * np.sin(phi)], -1)


DISC_S, DISC_F = vogel(SEARCH), vogel(TAPS)
W_F = 1.0 - 0.9 * (np.arange(TAPS) + 0.5) / TAPS


def quad(blocked, u, v):
    """stSpotQuadTile on a flat receiver: the four texels of the bilinear footprint of uv (blocked flags, clamped to the tile) and the
    bilinear weights, in gather order (0,1) (1,1) (1,0) (0,0)."""
    n = blocked.shape[0]
    tx, ty = u * n - 0.5, v * n - 0.5
    bx, by = np.floor(tx), np.floor(ty)
    fx, fy = tx - bx, ty - by
    bx, by = bx.astype(np.int64), by.astype(np.int64)
    def b(x, y):
        return blocked[np.clip(y, 0, n - 1), np.clip(x, 0, n - 1)]
    z = (b(bx, by + 1), b(bx + 1, by + 1), b(bx + 1, by), b(bx, by))
    w = ((1 - fx) * fy, fx * fy, fx * (1 - fy), (1 - fx) * (1 - fy))
    return z, w


def lookup(blocked, cx, cy, rphys):
    """The L1 lookup at receivers (cx, cy) in tile uv, for a physical penumbra radius rphys (texels; one caster plane, so a constant)."""
    n = blocked.shape[0]
    rs = min(max(rphys * SEARCH_RATIO, KERNEL_MIN), KERNEL_MAX)
    nblk = np.zeros(cx.shape)
    for dx, dy in list(DISC_S) + [(0.0, 0.0)]:  # the disc, then the receiver's own texels
        z, _ = quad(blocked, cx + dx * rs / n, cy + dy * rs / n)
        nblk += z[0].astype(float) + z[1] + z[2] + z[3]
    rf = min(max(rphys, KERNEL_MIN), KERNEL_MAX)
    lit = np.zeros(cx.shape)
    for k, (dx, dy) in enumerate(DISC_F):
        z, w = quad(blocked, cx + dx * rf / n, cy + dy * rf / n)
        lit += W_F[k] * sum((~zz) * ww for zz, ww in zip(z, w))
    out = lit / W_F.sum()
    out[nblk == 0] = 1.0
    out[nblk == 4 * (SEARCH + 1)] = 0.0
    return out


# ---- the study's scenes: opacity fields in receiver metres -> (opaque mask, alpha of the non-opaque part, kind)
def rot(x, y, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return c * x + s * y, -s * x + c * y


def frame(x, y):  # a window cross, bars 4 cm wide, 12 degrees off the tile axes
    u, v = rot(x - 0.5 * WINDOW_M, y - 0.5 * WINDOW_M, 12.0)
    return (np.abs(u) < 0.02) | (np.abs(v) < 0.02)


def scene_glass(x, y):
    return frame(x, y), np.full(np.shape(x), 0.5), 'blend'


def scene_lace(x, y):  # an alpha-mask lace: round holes, period 1.5 cm, open share about 0.44
    u, v = rot(x, y, 7.0)
    p = 0.015
    du = (u / p) - np.round(u / p)
    dv = (v / p) - np.round(v / p)
    hole = (du * du + dv * dv) < 0.375 ** 2
    return frame(x, y), np.where(hole, 0.0, 1.0), 'mask'


def scene_lattice(x, y):  # an opaque diamond lattice: bars 2 cm wide every 12 cm
    u, v = rot(x, y, 38.0)
    p = 0.12
    bar = (np.abs(u / p - np.round(u / p)) * p < 0.01) | (np.abs(v / p - np.round(v / p)) * p < 0.01)
    return bar, np.zeros(np.shape(x)), 'opaque'


SCENES = {'glass 0.5 + frame': scene_glass, 'lace mask + frame': scene_lace, 'opaque lattice': scene_lattice}


def ign_keep(n, alpha):  # today's hashed dither of a blended caster (shadowAlphaMaskF.glsl)
    y, x = np.mgrid[0:n, 0:n].astype(np.float64)
    ign = np.modf(52.9829189 * np.modf(0.06711056 * (x + 0.5) + 0.00583715 * (y + 0.5))[0])[0]
    return (ign < alpha) & (alpha >= 0.05) | (alpha >= 0.98)


def raster(scene, t):
    """The tile as the viewer draws it: one sample at each texel centre, the casters' rules of today (L3 changes them)."""
    n = int(round(WINDOW_M / t))
    c = (np.arange(n) + 0.5) * t
    x, y = np.meshgrid(c, c)
    opaque, alpha, kind = scene(x, y)
    if kind == 'opaque':
        return opaque
    if kind == 'mask':  # the creator's cutoff on the mip average of the mask over the texel (16 x 16 sub-samples)
        sub = 16
        cs = (np.arange(n * sub) + 0.5) * t / sub
        xs, ys = np.meshgrid(cs, cs)
        _, a_hi, _ = scene(xs, ys)
        a = a_hi.reshape(n, sub, n, sub).mean(axis=(1, 3))
        return opaque | (a >= 0.5)
    return opaque | ign_keep(n, alpha)


def fft_convolve(a, k):
    s = (a.shape[0] + k.shape[0] - 1, a.shape[1] + k.shape[1] - 1)
    out = np.fft.irfft2(np.fft.rfft2(a, s) * np.fft.rfft2(k, s), s)
    oy, ox = k.shape[0] // 2, k.shape[1] // 2
    return out[oy:oy + a.shape[0], ox:ox + a.shape[1]]


def truth(scene, rho):
    m = int(round(WINDOW_M / FINE_M))
    pad = int(math.ceil(rho / FINE_M)) + 2
    c = (np.arange(-pad, m + pad) + 0.5) * FINE_M
    x, y = np.meshgrid(c, c)
    opaque, alpha, _ = scene(x, y)
    trans = np.where(opaque, 0.0, 1.0 - alpha)
    r = max(rho / FINE_M, 0.5)
    k = int(math.ceil(r))
    ky, kx = np.mgrid[-k:k + 1, -k:k + 1]
    ker = ((kx * kx + ky * ky) <= r * r).astype(np.float64)
    ker /= ker.sum()
    out = fft_convolve(trans, ker)[pad:pad + m, pad:pad + m]
    s = int(round(STEP_M / FINE_M))
    return out.reshape(m // s, s, m // s, s).mean(axis=(1, 3))  # the receiver sample's own 5 mm footprint


def render(blocked, t, emitter):
    m = int(round(WINDOW_M / STEP_M))
    c = (np.arange(m) + 0.5) * STEP_M / WINDOW_M
    cx, cy = np.meshgrid(c, c)
    return lookup(blocked, cx, cy, 0.5 * emitter * GAP / t)


def metrics(img, ref):
    """RMSE, the study's share of neighbour pairs over 0.25 where the truth is smooth, the count over 0.5 (hard squares) and the largest
    neighbour jump where the truth is smooth"""
    crop = slice(8, -8)  # away from the patch border (the tile clamps there)
    e = (img - ref)[crop, crop]
    rmse = float(np.sqrt((e * e).mean()))
    study = squares = pairs = 0
    jump = 0.0
    for axis in (0, 1):
        gi = np.abs(np.diff(img[crop, crop], axis=axis))
        smooth = np.abs(np.diff(ref[crop, crop], axis=axis)) < 0.05
        study += int(((gi > 0.25) & smooth).sum())
        squares += int(((gi > 0.5) & smooth).sum())
        jump = max(jump, float(gi[smooth].max()) if smooth.any() else 0.0)
        pairs += gi.size
    return rmse, study / pairs, squares, jump


def edge_widths(angles):
    """A straight opaque edge on a 2.5 mm tile (96 x 96 texels): the 10-90 % width across it and the 50 % contour's peak-to-peak wander,
    in texels, for lamps of 1.035, 2.25 and 4.5 cm (the softness 0.23, 0.5 and 1.0) and two wider ones that reach the cap."""
    n = 96
    t = 0.0025
    cc = np.arange(n) + 0.5
    X, Y = np.meshgrid(cc, cc)
    rows = []
    for emitter in (0.01035, 0.0225, 0.045, 0.09, 0.15):
        rho = 0.5 * emitter * GAP / t  # the penumbra radius in texels
        for ang in angles:
            a = math.radians(ang)
            nx, ny = -math.sin(a), math.cos(a)
            blocked = ((X - 48.31) * nx + (Y - 48.17) * ny) < 0
            ds = np.arange(-20.0, 20.0, 0.05)
            widths, x50s = [], []
            for s in np.linspace(-15, 15, 60):
                px, py = 48.31 + s * math.cos(a), 48.17 + s * math.sin(a)
                vals = lookup(blocked, (px + ds * nx) / n, (py + ds * ny) / n, rho)
                def cross(level):
                    i = np.where((vals[:-1] < level) & (vals[1:] >= level))[0]
                    if len(i) == 0:
                        return None
                    i = i[0]
                    return ds[i] + 0.05 * (level - vals[i]) / (vals[i + 1] - vals[i])
                x10, x50, x90 = cross(0.1), cross(0.5), cross(0.9)
                if x10 is not None and x90 is not None:
                    widths.append(x90 - x10)
                if x50 is not None:
                    x50s.append(x50)
            physics = 1.3741 * rho  # a uniform disc's 10-90 % width across a straight edge
            regime = 'floor' if rho < KERNEL_MIN else ('cap' if rho > KERNEL_MAX else 'physical')
            rows.append((emitter, ang, rho, float(np.mean(widths)), physics, max(x50s) - min(x50s), regime))
    return rows


def save_sheet(rows, path):
    from PIL import Image
    pad = 4
    h = rows[0][0].shape[0]
    sheet = np.full((len(rows) * (h + pad) + pad, len(rows[0]) * (h + pad) + pad), 0.25)
    for r, row in enumerate(rows):
        for c, im in enumerate(row):
            sheet[pad + r * (h + pad):pad + r * (h + pad) + h, pad + c * (h + pad):pad + c * (h + pad) + h] = im
    g = np.clip(sheet, 0, 1) ** (1 / 2.2)
    Image.fromarray((g * 255 + 0.5).astype(np.uint8)).save(path)


# ---- lamp shadows L3: the light-through layer (lllightthrough.h, shadowLightThroughF.glsl, shadowStochasticUtil.glsl stThroughFilter)
HEADER = ROOT / 'indra/newview/lllightthrough.h'
FIT = ROOT / 'indra/newview/llshadowcascadefit.h'


def header_float(name):
    m = re.search(r'constexpr float %s = ([0-9.]+)f(?: / ([0-9.]+)f)?;' % name, HEADER.read_text(encoding='utf-8'))
    return float(m.group(1)) / (float(m.group(2)) if m.group(2) else 1.0)


SOLID = header_float('kSolidAlpha')
INVISIBLE = header_float('kInvisibleAlpha')
NO_LAYER = header_float('kNoLayer')
PLANE_SLOPE = define('ST_SPOT_PLANE_SLOPE')
PLANE_BIAS_MAX = float(re.search(r'SHADOW_PLANE_BIAS_MAX_TEXELS = ([0-9.]+)f;', FIT.read_text(encoding='utf-8')).group(1))
THROUGH_TAPS = int(define('ST_THROUGH_TAPS'))
DISC_T = vogel(THROUGH_TAPS)
W_T = 1.0 - 0.9 * (np.arange(THROUGH_TAPS) + 0.5) / THROUGH_TAPS
# the lamp's depth: a perspective map with its near plane 0.1 m and far plane 10 m, z = c0 - c1 / w
NEAR_M, FAR_M = 0.1, 10.0
C0 = FAR_M / (FAR_M - NEAR_M)
C1 = C0 * NEAR_M


def map_depth(w):
    return C0 - C1 / w


Z_WINDOW, Z_FLOOR = map_depth(3.0), map_depth(4.5)   # the scenes above: a window 3 m from the lamp, the floor 4.5 m
Z_FRAME = map_depth(2.95)                            # a window frame 5 cm in front of its pane
assert abs(SEARCH_RATIO - Z_FLOOR / (Z_FLOOR - Z_WINDOW)) < 1.0


def tau_of(a, c):
    """lllightthrough::transmittance: the light through one face of opacity a and linear colour c (per channel)"""
    a = np.clip(a, 0.0, 1.0)
    return (1.0 - a) * (1.0 + a * np.clip(c, 0.0, 1.0))


def q8(v):
    return np.round(np.clip(v, 0.0, 1.0) * 255.0) / 255.0


def f32(v):
    return np.asarray(v, np.float32).astype(np.float64)


class Layer:
    """the light-through layer of one tile as the casters draw it (shadowLightThroughF.glsl in LLLightThroughLayer::DrawScope): tau
    (n x n x 3, RGBA8, cleared to 1, multiplied by each caster) and zt (n x n, R32F, cleared to 1, the nearest caster's window depth as
    it is). A caster is drawn only where it lies in front of the tile's opaque depth (the depth atlas S, LEQUAL); depth_test False draws
    every caster, as the first L3 build did (kept for the comparison)."""

    def __init__(self, n):
        self.tau = np.ones((n, n, 3))
        self.zt = np.ones((n, n))

    def draw(self, cover, alpha, colour, z, opaque=None, depth_test=True):
        a = np.broadcast_to(np.asarray(alpha, float), cover.shape)
        z = np.broadcast_to(np.asarray(z, float), cover.shape)
        keep = cover & (a > INVISIBLE) & (a < SOLID)
        if opaque is not None and depth_test:
            keep &= z <= opaque
        t = tau_of(a[..., None], np.broadcast_to(np.asarray(colour, float), cover.shape + (3,)))
        self.tau = np.where(keep[..., None], q8(self.tau * t), self.tau)
        self.zt = np.where(keep, np.minimum(self.zt, f32(z)), self.zt)


def through_quad(n, u, v):
    """stThroughTap's footprint: the sample point clamped half a texel inside the tile (CLAMP_TO_EDGE of the tile, no read of a
    neighbour tile), its quad's lower left texel, its four texels and bilinear weights (gather order)"""
    px, py = np.clip(u * n, 0.5, n - 0.5), np.clip(v * n, 0.5, n - 0.5)
    tx, ty = px - 0.5, py - 0.5
    bx, by = np.minimum(np.floor(tx), n - 2), np.minimum(np.floor(ty), n - 2)
    fx, fy = tx - bx, ty - by
    ix, iy = bx.astype(np.int64), by.astype(np.int64)
    idx = ((ix, iy + 1), (ix + 1, iy + 1), (ix + 1, iy), (ix, iy))
    w = ((1 - fx) * fy, fx * fy, fx * (1 - fy), (1 - fx) * (1 - fy))
    return bx, by, idx, w


def through_ref(bx, by, n, cx, cy, cz, g, z_bias, rp_max, eps=0.0):
    """stThroughRef (stSpotQuadTile's reference): the receiver's depth less the bias, lowered (never raised) by the receiver plane g over
    each texel's offset, at most rp_max, at the quad's four texels in gather order (eps: a shift of every reference, for the GPU gate's
    test of a receiver on a decision's edge)"""
    ox, oy = (bx + 0.5) / n - cx, (by + 0.5) / n - cy
    e = 1.0 / n
    return [cz - z_bias + eps + np.clip(g[0] * dx + g[1] * dy, -rp_max, 0.0)
            for dx, dy in ((ox, oy + e), (ox + e, oy + e), (ox + e, oy), (ox, oy))]


def through_tap(layer, depth, u, v, c, g, z_bias, rp_max, eps=0.0):
    """stThroughTap: (lit per channel, open)"""
    n = layer.zt.shape[0]
    bx, by, idx, w = through_quad(n, u, v)
    z = [layer.zt[y, x] for x, y in idx]
    s = [depth[y, x] for x, y in idx]
    ref = through_ref(bx, by, n, c[0], c[1], c[2], g, z_bias, rp_max, eps)
    tb = sum(ww[..., None] * layer.tau[y, x] for (x, y), ww in zip(idx, w))   # the hardware's bilinear tau
    has = [zz < NO_LAYER for zz in z]
    opn = [ss >= rr for ss, rr in zip(s, ref)]
    w_all = sum(ww * hh for ww, hh in zip(w, has))
    w_open = sum(ww * oo for ww, oo in zip(w, opn))
    w_front = sum(ww * oo * hh * (zz < rr) for ww, oo, hh, zz, rr in zip(w, opn, has, z, ref))
    r = np.where(w_all > 0.0, w_front / np.maximum(w_all, 1e-30), 0.0)
    return w_open[..., None] + r[..., None] * (tb - 1.0), w_open


def through_filter(layer, depth, cx, cy, cz, pen, z_bias, z_tex, g=(0.0, 0.0), want_open=False, eps=0.0):
    """stThroughFilter: the nearest caster in front of the receiver over the search (the depth search's disc and radius, plus the
    receiver's own texels; none: exactly 1), then THROUGH_TAPS taps at the physical radius from it, weighted as the depth filter and by
    their open share: the light through the part of the disc the depth atlas does not block (1 where every tap is blocked). want_open:
    also the open share of the disc's weight (1 where no caster is in front: the value is exact there)"""
    n = layer.zt.shape[0]
    shape = np.shape(cx)
    rs = np.clip(pen * cz, KERNEL_MIN, KERNEL_MAX)
    rp_max = z_tex * np.maximum(PLANE_BIAS_MAX, PLANE_SLOPE * (rs + 1.0))
    z_near = np.full(shape, -1.0)
    for dx, dy in list(DISC_S) + [(0.0, 0.0)]:
        bx, by, idx, _ = through_quad(n, cx + dx * rs / n, cy + dy * rs / n)
        for (x, y), rr in zip(idx, through_ref(bx, by, n, cx, cy, cz, g, z_bias, rp_max, eps)):
            zz = layer.zt[y, x]
            z_near = np.maximum(z_near, np.where(zz < rr, zz, -1.0))
    rf = np.clip(pen * (cz - z_near), KERNEL_MIN, KERNEL_MAX)
    lit = np.zeros(shape + (3,))
    opn = np.zeros(shape)
    for k, (dx, dy) in enumerate(DISC_T):
        tl, to = through_tap(layer, depth, cx + dx * rf / n, cy + dy * rf / n, (cx, cy, cz), g, z_bias, rp_max, eps)
        lit += W_T[k] * tl
        opn += W_T[k] * to
    out = np.where(opn[..., None] > 0.0, np.clip(lit / np.maximum(opn, 1e-30)[..., None], 0.0, 1.0), 1.0)
    out[z_near < 0.0] = 1.0
    if want_open:
        return out, np.where(z_near < 0.0, 1.0, opn / W_T.sum())
    return out


def through_filter_first(layer, cx, cy, cz, pen, z_bias):
    """the first L3 build's lookup, for the comparison: a flat reference (no receiver plane) and every tap counted whole (no open
    weight), on a layer drawn without the depth test"""
    n = layer.zt.shape[0]
    ref = cz - z_bias
    rs = np.clip(pen * cz, KERNEL_MIN, KERNEL_MAX)
    z_near = np.full(np.shape(cx), -1.0)
    for dx, dy in list(DISC_S) + [(0.0, 0.0)]:
        _, _, idx, _ = through_quad(n, cx + dx * rs / n, cy + dy * rs / n)
        for x, y in idx:
            zz = layer.zt[y, x]
            z_near = np.maximum(z_near, np.where(zz < ref, zz, -1.0))
    rf = np.clip(pen * (cz - z_near), KERNEL_MIN, KERNEL_MAX)
    acc = np.zeros(np.shape(cx) + (3,))
    for k, (dx, dy) in enumerate(DISC_T):
        _, _, idx, w = through_quad(n, cx + dx * rf / n, cy + dy * rf / n)
        z = [layer.zt[y, x] for x, y in idx]
        tb = sum(ww[..., None] * layer.tau[y, x] for (x, y), ww in zip(idx, w))
        has = [zz < NO_LAYER for zz in z]
        w_all = sum(ww * hh for ww, hh in zip(w, has))
        w_front = sum(ww * hh * (zz < ref) for ww, hh, zz in zip(w, has, z))
        f = np.where(w_all > 0.0, w_front / np.maximum(w_all, 1e-30), 0.0)
        acc += W_T[k] * (1.0 + f[..., None] * (tb - 1.0))
    out = acc / W_T.sum()
    out[z_near < 0.0] = 1.0
    return out


def receivers(m=None):
    m = m or int(round(WINDOW_M / STEP_M))
    c = (np.arange(m) + 0.5) / m
    return np.meshgrid(c, c)


def z_tex_at(t, w=4.5):
    return C1 / (w * w) * t   # one texel of map depth at the receiver: dz/dw times the texel; SHADOW_BIAS_TEXELS (1) of them is the bias


def floor_depth(n):
    return np.full((n, n), Z_FLOOR)


def l3_glass(texels, emitters):
    """uniform glass at 0.5 (no grain: one value everywhere), then the window cross with glass at 0.5 in two builds: the pane behind its
    frame (5 cm behind the bars: the depth test keeps it out from under them) and painted leading (the bars are the pane's own solid
    texels: no pane under them). RMSE against the truth and the largest neighbour step where the truth is smooth: this build | the first
    L3 build (no depth test, no open weight) | the dither"""
    fails = 0
    cx, cy = receivers()
    print('\n== L3 uniform glass 0.5: the spread of the result over the patch (layer | dither)')
    for t in texels:
        n = int(round(WINDOW_M / t))
        layer = Layer(n)
        layer.draw(np.ones((n, n), bool), 0.5, (0.0, 0.0, 0.0), Z_WINDOW, floor_depth(n))
        line = ' texel %4.1f cm (%3d px):' % (t * 100, n)
        for label, emitter in emitters:
            rphys = 0.5 * emitter * GAP / t
            pen = rphys / (Z_FLOOR - Z_WINDOW)
            vis = through_filter(layer, floor_depth(n), cx, cy, np.full(cx.shape, Z_FLOOR), pen, z_tex_at(t), z_tex_at(t))[..., 1]
            old = lookup(ign_keep(n, np.full((n, n), 0.5)), cx, cy, rphys)
            bad = float(np.ptp(vis)) > 1e-9   # one value everywhere (to the float sum of the weights)
            fails += bad
            line += '  %s %.4f at %.4f | %.3f%s' % (label, float(np.ptp(vis)), float(vis.mean()), float(np.ptp(old)), '  FAIL' if bad else '')
        print(line)
    for build in ('pane behind its frame', 'painted leading'):
        print('\n== L3 glass 0.5 + frame, %s: RMSE against the truth (this build | first L3 build | dither), the largest step where the truth is smooth (same order)' % build)
        for t in texels:
            n = int(round(WINDOW_M / t))
            c = (np.arange(n) + 0.5) * t
            x, y = np.meshgrid(c, c)
            opaque, alpha, _ = scene_glass(x, y)
            # the depth atlas: the bars (in front of the pane, or the pane's own solid texels), the floor elsewhere
            depth = np.where(opaque, Z_FRAME if build == 'pane behind its frame' else Z_WINDOW, Z_FLOOR)
            cover = np.ones(opaque.shape, bool) if build == 'pane behind its frame' else ~opaque
            layer = Layer(n)
            layer.draw(cover, alpha, (0.0, 0.0, 0.0), Z_WINDOW, depth)
            first = Layer(n)
            first.draw(cover, alpha, (0.0, 0.0, 0.0), Z_WINDOW, depth, depth_test=False)
            dither = raster(scene_glass, t)
            line = ' texel %4.1f cm (%3d px):' % (t * 100, n)
            for label, emitter in emitters:
                rphys = 0.5 * emitter * GAP / t
                pen = rphys / (Z_FLOOR - Z_WINDOW)
                ref = truth(scene_glass, 0.5 * emitter * GAP)
                dep = lookup(opaque, cx, cy, rphys)
                cz = np.full(cx.shape, Z_FLOOR)
                vis = dep * through_filter(layer, depth, cx, cy, cz, pen, z_tex_at(t), z_tex_at(t))[..., 1]
                vis_f = dep * through_filter_first(first, cx, cy, cz, pen, z_tex_at(t))[..., 1]
                old = lookup(dither, cx, cy, rphys)
                (rm, _, _, jm), (rf_, _, _, jf), (ro, _, _, jo) = metrics(vis, ref), metrics(vis_f, ref), metrics(old, ref)
                bad = rm > ro or rm > rf_ + 1e-3
                fails += bad
                line += '  %s %.3f | %.3f | %.3f, %.2f | %.2f | %.2f%s' % (label, rm, rf_, ro, jm, jf, jo, '  FAIL' if bad else '')
            print(line)
    return fails


def l3_stack_and_front(texels):
    """two panes one behind the other (their product), and a receiver in front of the window (exactly 1)"""
    fails = 0
    print('\n== L3 two panes (0.5 grey at 2.5 m over the left two thirds, 0.6 red at 3 m over the right two thirds), and a receiver at 2 m')
    tA, tB = tau_of(0.5, np.array([0.5, 0.5, 0.5])), tau_of(0.6, np.array([1.0, 0.0, 0.0]))
    for t in texels:
        n = int(round(WINDOW_M / t))
        cc = (np.arange(n) + 0.5) / n
        x, _ = np.meshgrid(cc, cc)
        layer = Layer(n)
        layer.draw(x < 2 / 3, 0.5, (0.5, 0.5, 0.5), map_depth(2.5), floor_depth(n))
        layer.draw(x > 1 / 3, 0.6, (1.0, 0.0, 0.0), Z_WINDOW, floor_depth(n))
        cx, cy = receivers(96)
        rphys = 0.5 * EMITTER_M * GAP / t
        pen = rphys / (Z_FLOOR - Z_WINDOW)
        behind = through_filter(layer, floor_depth(n), cx, cy, np.full(cx.shape, Z_FLOOR), pen, z_tex_at(t), z_tex_at(t))
        # where both panes cover every tap: farther from the overlap's edges than the filter's reach (its radius and a texel)
        reach = (min(max(rphys, KERNEL_MIN), KERNEL_MAX) + 1.0) / n
        mid = (cx > 1 / 3 + reach) & (cx < 2 / 3 - reach)
        front = through_filter(layer, floor_depth(n), cx, cy, np.full(cx.shape, map_depth(2.0)), pen, z_tex_at(t, 2.0), z_tex_at(t, 2.0))
        exact = bool(np.all(front == 1.0))
        if not mid.any():
            fails += not exact
            print(' texel %4.1f cm: the overlap (%d texels) is narrower than the filter: no interior; in front of both: %s%s' % (
                t * 100, n // 3, 'exactly 1' if exact else 'NOT 1', '' if exact else '  FAIL'))
            continue
        err = float(np.abs(behind[mid] - tA * tB).max())
        bad = err > 2.0 / 255.0 or not exact
        fails += bad
        print(' texel %4.1f cm: both panes %s, product %s, largest error %.4f; in front of both: %s%s' % (
            t * 100, np.round(behind[mid].mean(axis=0), 3), np.round(tA * tB, 3), err, 'exactly 1' if exact else 'NOT 1', '  FAIL' if bad else ''))
    return fails


def l3_hidden(texels):
    """a see-through caster hidden behind the receiver (the auditor's 1): a pane in front of the floor (0.5 grey at 2.5 m) and a red pane
    under the floor (0.6 at 5 m, glass in the room below), and a glass globe round the lamp (0.3 at 0.3 m) over a red pane under the
    floor. The floor gets the front pane's light alone; the first L3 build (no depth test) multiplied the hidden pane in too"""
    fails = 0
    print('\n== L3 a pane hidden under the floor: the floor\'s light where a pane in front and the hidden one overlap (this build | first L3 build | truth)')
    tA, tB, tG = tau_of(0.5, np.array([0.5, 0.5, 0.5])), tau_of(0.6, np.array([1.0, 0.0, 0.0])), tau_of(0.3, np.array([0.9, 0.9, 0.9]))
    for t in texels:
        n = int(round(WINDOW_M / t))
        cc = (np.arange(n) + 0.5) / n
        x, _ = np.meshgrid(cc, cc)
        cx, cy = receivers(96)
        rphys = 0.5 * EMITTER_M * GAP / t
        pen = rphys / (Z_FLOOR - Z_WINDOW)
        reach = (min(max(rphys, KERNEL_MIN), KERNEL_MAX) + 1.0) / n
        mid = (cx > 1 / 3 + reach) & (cx < 2 / 3 - reach)
        if not mid.any():
            print(' texel %4.1f cm: the overlap (%d texels) is narrower than the filter: no interior' % (t * 100, n // 3))
            continue
        cz = np.full(cx.shape, Z_FLOOR)
        line = ' texel %4.1f cm:' % (t * 100)
        for label, front_cover, front_z, front_a, front_c, want in (
                ('pane', x < 2 / 3, map_depth(2.5), 0.5, (0.5, 0.5, 0.5), tA),
                ('globe', np.ones(x.shape, bool), map_depth(0.3), 0.3, (0.9, 0.9, 0.9), tG)):
            got = []
            for depth_test in (True, False):
                layer = Layer(n)
                layer.draw(front_cover, front_a, front_c, front_z, floor_depth(n), depth_test)
                layer.draw(x > 1 / 3, 0.6, (1.0, 0.0, 0.0), map_depth(5.0), floor_depth(n), depth_test)
                if depth_test:
                    v = through_filter(layer, floor_depth(n), cx, cy, cz, pen, z_tex_at(t), z_tex_at(t))
                else:
                    v = through_filter_first(layer, cx, cy, cz, pen, z_tex_at(t))
                got.append(v[mid].mean(axis=0))
            err = float(np.abs(got[0] - want).max())
            bad = err > 2.0 / 255.0
            fails += bad
            line += '  %s %s | %s | %s%s' % (label, np.round(got[0], 3), np.round(got[1], 3), np.round(want, 3), '  FAIL' if bad else '')
        print(line)
    return fails


def l3_tint_edge(angles):
    """the straight edge of a red pane (0.6) on a 2.5 mm tile: the green channel's 10-90 % width against a uniform disc's"""
    n, t = 96, 0.0025
    cc = np.arange(n) + 0.5
    X, Y = np.meshgrid(cc, cc)
    fails = 0
    print('\n== L3 the edge of a red pane, 2.5 mm tile: the green channel 10-90 % width against a uniform disc (texels)')
    print('| lamp | angle | penumbra radius | width | physics | ratio | regime |')
    print('|---|---|---|---|---|---|---|')
    g = float(tau_of(0.6, 0.0))
    for emitter in (0.01035, 0.0225, 0.045, 0.09, 0.15):
        rho = 0.5 * emitter * GAP / t
        pen = rho / (Z_FLOOR - Z_WINDOW)
        for ang in angles:
            a = math.radians(ang)
            nx, ny = -math.sin(a), math.cos(a)
            cover = ((X - 48.31) * nx + (Y - 48.17) * ny) < 0
            layer = Layer(n)
            layer.draw(cover, 0.6, (1.0, 0.0, 0.0), Z_WINDOW, floor_depth(n))
            ds = np.arange(-20.0, 20.0, 0.05)
            widths = []
            for s in np.linspace(-15, 15, 30):
                px, py = 48.31 + s * math.cos(a), 48.17 + s * math.sin(a)
                u, v = (px + ds * nx) / n, (py + ds * ny) / n
                vals = through_filter(layer, floor_depth(n), u, v, np.full(u.shape, Z_FLOOR), pen, z_tex_at(t), z_tex_at(t))[..., 1]
                lvl = (vals - g) / (1.0 - g)
                def cross(level):
                    i = np.where((lvl[:-1] < level) & (lvl[1:] >= level))[0]
                    if len(i) == 0:
                        return None
                    i = i[0]
                    return ds[i] + 0.05 * (level - lvl[i]) / (lvl[i + 1] - lvl[i])
                x10, x90 = cross(0.1), cross(0.9)
                if x10 is not None and x90 is not None:
                    widths.append(x90 - x10)
            physics = 1.3741 * rho
            regime = 'floor' if rho < KERNEL_MIN else ('cap' if rho > KERNEL_MAX else 'physical')
            w = float(np.mean(widths))
            bad = regime == 'physical' and abs(w / physics - 1.0) > 0.20
            fails += bad
            print('| %.2f cm | %2d deg | %.2f | %.2f | %.2f | %.2f | %s%s |' % (emitter * 100, ang, rho, w, physics, w / physics, regime, '  FAIL' if bad else ''))
    return fails


def tilted_wall(n, t, theta_deg, sign):
    """a wall tilted theta to the tile's plane, its depth affine in the tile's uv (a plane's window depth is): z(u) = z0 + gz (u - 0.5),
    gz = sign tan(theta) texels of depth per texel; returns the depth over the tile, gz and one texel of depth"""
    z_tex = z_tex_at(t)
    gz = sign * math.tan(math.radians(theta_deg)) * z_tex * n
    u = (np.arange(n) + 0.5) / n
    U, _ = np.meshgrid(u, u)
    return Z_FLOOR + gz * (U - 0.5), gz, z_tex


def l3_tilted(texels):
    """the receiver plane (the auditor's 6 and the review's Q1). A window flush in a wall lit obliquely: the glass lies in the wall's plane
    over u > 0.5 (the room far behind it), the wall's receivers next to it must get no tint (exactly 1); with a flat reference the glass
    texels nearer the lamp read as in front of them (a tinted fringe). And a sheer 5 cm in front of a wall lit obliquely, parallel to it:
    the wall behind it gets the sheer's light, 0.5 (the reference is lowered toward the lamp, never raised, as the depth filter's: on the
    far side of each receiver a sheer closer to the wall than the plane's rise reads as behind it, reported)"""
    fails = 0
    print('\n== L3 a window flush in a wall lit obliquely: the wall\'s largest tint (1 - light) within 6 texels of the glass (this build | flat reference)')
    print('== and a sheer 5 cm in front of the wall: the wall\'s mean light behind it and its largest error from 0.5 (this build | flat reference)')
    pen_t = 0.5 * EMITTER_M * 0.23 * GAP
    for t in texels:
        n = int(round(WINDOW_M / t))
        line = ' texel %4.1f cm:' % (t * 100)
        for theta in (30, 45, 60):
            worst_new = worst_flat = 0.0
            sheer = []
            for sign in (-1.0, 1.0):
                wall, gz, z_tex = tilted_wall(n, t, theta, sign)
                u = (np.arange(n) + 0.5) / n
                U, _ = np.meshgrid(u, u)
                glass = U > 0.5
                depth = np.where(glass, map_depth(8.0), wall)   # the room far behind the window
                layer = Layer(n)
                layer.draw(glass, 0.5, (0.0, 0.0, 0.0), wall, depth)
                # receivers on the wall within 6 texels of the glass, at sub-texel spacing
                m = 64
                cu = 0.5 - (np.arange(m) + 0.5) / m * 6.0 / n
                cv = np.linspace(0.3, 0.7, 16)
                CU, CV = np.meshgrid(cu, cv)
                CZ = Z_FLOOR + gz * (CU - 0.5)
                pen = pen_t / t / (Z_FLOOR - Z_WINDOW)
                new = through_filter(layer, depth, CU, CV, CZ, pen, z_tex, z_tex, (gz, 0.0))
                flat = through_filter(layer, depth, CU, CV, CZ, pen, z_tex, z_tex, (0.0, 0.0))
                worst_new = max(worst_new, float((1.0 - new).max()))
                worst_flat = max(worst_flat, float((1.0 - flat).max()))
                # the sheer: 5 cm in front of the wall along its normal, parallel to it, over the middle of the tile
                dz = C1 / (4.5 * 4.5) * 0.05 / math.cos(math.radians(theta))
                cover = (U > 0.2) & (U < 0.8)
                sh = Layer(n)
                sh.draw(cover, 0.5, (0.0, 0.0, 0.0), wall - dz, wall)
                reach = (KERNEL_MAX + 2.0) / n
                ru = np.linspace(0.2 + reach, 0.8 - reach, 32)
                RU, RV = np.meshgrid(ru, np.linspace(0.3, 0.7, 8))
                RZ = Z_FLOOR + gz * (RU - 0.5)
                for g in ((gz, 0.0), (0.0, 0.0)):
                    v = through_filter(sh, wall, RU, RV, RZ, pen, z_tex, z_tex, g)[..., 1]
                    sheer.append((float(v.mean()), float(np.abs(v - 0.5).max())))
            bad = worst_new > 1e-6
            fails += bad
            sn = max(sheer[0][1], sheer[2][1])
            sf = max(sheer[1][1], sheer[3][1])
            line += '  %d deg: flush %.4f | %.4f, sheer %.3f %.3f | %.3f %.3f%s' % (
                theta, worst_new, worst_flat, 0.5 * (sheer[0][0] + sheer[2][0]), sn, 0.5 * (sheer[1][0] + sheer[3][0]), sf, '  FAIL' if bad else '')
        print(line)
    return fails


def l3_mask_evidence(texels):
    """L3.3 is not built: the lace's mean visibility in its interior under the creator's cutoff on the mip average (today, and what
    this branch keeps) and under the plan's sharpened coverage a' = clamp((a - cutoff) / max(fwidth(a), 1e-4) + 0.5), depth where
    a' >= 0.98 and the layer times 1 - a' below, against the open share. The sampled alpha is the mip average over the tile texel
    (16 x 16 sub-samples: what trilinear sampling reads when the tile does not resolve the holes)."""
    print('\n== L3.3 evidence (not gated, mask faces keep the cutoff): lace mean visibility inside the lace, against its open share')
    sub = 16
    for t in texels:
        n = int(round(WINDOW_M / t))
        cs = (np.arange(n * sub) + 0.5) * t / sub
        xs, ys = np.meshgrid(cs, cs)
        _, a_hi, _ = scene_lace(xs, ys)
        a = a_hi.reshape(n, sub, n, sub).mean(axis=(1, 3))
        open_share = float(1.0 - a_hi.mean())
        fw = np.abs(np.diff(a, axis=1, append=a[:, -1:])) + np.abs(np.diff(a, axis=0, append=a[-1:, :]))
        ap = np.clip((a - 0.5) / np.maximum(fw, 1e-4) + 0.5, 0.0, 1.0)
        cutoff_mean = float(1.0 - (a >= 0.5).mean())
        plan_vis = np.where(ap >= SOLID, 0.0, 1.0 - ap)
        print(' texel %4.1f cm: open share %.3f | the creator\'s cutoff %.3f | the plan\'s sharpened coverage %.3f' % (
            t * 100, open_share, cutoff_mean, float(plan_vis.mean())))
    print(' (the mip average is smooth where the tile does not resolve the holes, so fwidth is small and a\' thresholds it like the cutoff)')


def l3_main():
    texels = [0.04, 0.01] if opts.quick else [0.08, 0.04, 0.02, 0.01, 0.005]
    emitters = [('default 0.23', EMITTER_M * 0.23), ('top 1.0', EMITTER_M * 1.0)]
    print('\nL3 light-through twin: solid from %.2f, invisible at or below %.5f, %d taps after a search of %d + 1 gathers; the depth as it is (R32F)' % (
        SOLID, INVISIBLE, THROUGH_TAPS, SEARCH))
    fails = l3_glass(texels, emitters)
    fails += l3_stack_and_front(texels)
    fails += l3_hidden(texels)
    fails += l3_tint_edge([2, 10] if opts.quick else [2, 10, 30, 45])
    fails += l3_tilted(texels)
    l3_mask_evidence(texels)
    print('\nL3 twin', 'PASS' if fails == 0 else 'FAIL (%d)' % fails)
    return fails


def main():
    if opts.l3:
        return 0 if l3_main() == 0 else 1
    texels = [0.04, 0.01] if opts.quick else [0.08, 0.04, 0.02, 0.01, 0.005]
    emitters = [('default 0.23', EMITTER_M * 0.23), ('top 1.0', EMITTER_M * 1.0)]
    print('L1 lookup twin: floor %.1f texels, cap %.0f texels, %d search + %d filter gathers; lamp base %.1f cm' % (
        KERNEL_MIN, KERNEL_MAX, SEARCH, TAPS, EMITTER_M * 100))
    print('receiver patch %.2f m, samples %.0f mm apart, gap ratio %.1f' % (WINDOW_M, STEP_M * 1000, GAP))
    print("columns: RMSE against the truth of the same emitter | pairs over 0.25 where the truth is smooth (the study's measure) | pairs over")
    print('0.5 there (hard squares, gated) | the largest jump there')
    failures = 0
    for name, scene in SCENES.items():
        print('\n== %s' % name)
        sheet = []
        for t in texels:
            blocked = raster(scene, t)
            line = ' texel %4.1f cm (%3d px):' % (t * 100, int(round(WINDOW_M / t)))
            row = []
            for label, emitter in emitters:
                ref = truth(scene, 0.5 * emitter * GAP)
                img = render(blocked, t, emitter)
                rmse, share, squares, jump = metrics(img, ref)
                failures += squares > 0
                line += '  %s (%.2f cm) %.3f | %5.2f %% | %d | %.2f%s' % (label, emitter * 100, rmse, 100 * share, squares, jump, '' if squares == 0 else '  FAIL')
                row += [img, ref]
            print(line)
            sheet.append(row)
            sys.stdout.flush()
        if opts.sheet:
            Path(opts.sheet).mkdir(parents=True, exist_ok=True)
            save_sheet(sheet, str(Path(opts.sheet) / ('l1_sheet_' + name.split()[0] + '.png')))
    print('\n== straight opaque edge, 2.5 mm tile: 10-90 % width against a uniform disc (texels), the 50 % contour wander (texels)')
    print('| lamp | angle | penumbra radius | width | physics | ratio | contour wander | regime |')
    print('|---|---|---|---|---|---|---|---|')
    for emitter, ang, rho, w, phys, wander, regime in edge_widths([2, 10] if opts.quick else [2, 5, 10, 20, 30, 45]):
        ratio = w / phys
        bad = regime == 'physical' and abs(ratio - 1.0) > 0.20
        failures += bad
        print('| %.2f cm | %2d deg | %.2f | %.2f | %.2f | %.2f | %.2f | %s%s |' % (emitter * 100, ang, rho, w, phys, ratio, wander, regime, '  FAIL' if bad else ''))
    print('\nL1 twin', 'PASS' if failures == 0 else 'FAIL (%d)' % failures)
    if not opts.no_l3:
        failures += l3_main()
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
