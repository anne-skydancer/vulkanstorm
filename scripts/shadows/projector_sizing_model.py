#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Lamp shadows L2: a CPU model of the projector pool's tile sizes while the camera walks (no GPU, no viewer), with its gate.

The rules are llprojectorshadowpool.h's, written out here (the unit test checks the C++ side):
  size      a tile's size from its lamp alone: tileBaseSize = clamp(pow2_round(tan_w * d_ref / 4 mm + 2 G), 128, 2048), d_ref =
            min(1.5 r, 4 m), tan_w the tile's wider axis as a span of tangents, G the guard of a wide lamp's cell (16 texels, else 0)
  valve     while the tiles fit (demand <= 0.60 of the atlas) they keep that size whatever the camera does; over the budget a pressure P
            shifts the tiles of distance band b (octaves of the camera position's distance to the tile's volume: < 8 m, 8-16, 16-32,
            32-64, >= 64 m, half an octave of hysteresis) down by clamp(P - (4 - b), 0, 5) octaves, never below 64; P rises at once (as far as it
            takes, never past the point where every tile is at its floor) and falls one step after 3 s at or below 0.45, past the
            steps that change nothing
  resize    every change of a tile's size is one render (draw-then-swap); the model counts them
  texel     the floor texel under the camera from the tile's INTERIOR (its size less two guards: the audit's correction of the
            earlier model, which took the whole tile as the interior)

Scenes (the earlier model's, reports/projector-edges-2026-11-16/sizing_model.py, kept on the owner's machine): a room with four wide
ceiling lamps (fov 2.5 rad, 3 x 3 cells, radius 8 m) and four wall spots (fov 1.0); a club of 20 lamps (fov 1.2 to 3.0); and a room
whose lamps fit the atlas (four ceiling lamps of fov 1.2, the four wall spots). The camera walks a loop at 1.4 m/s, eye 1.7 m, 60 s at
60 fps, on the 4096 (default) and 8192 (from 11 GB) atlases.

Gate (exit code 1): no tile changes its size on a frame at pressure 0 (the atlas has room), in every scene and tier.

    python scripts/shadows/projector_sizing_model.py [--seconds 60]
Needs numpy. Deterministic.
"""
import argparse
import math
import sys

import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument('--seconds', type=float, default=60.0)
opts = ap.parse_args()

MIN_TILE, MIN_BASE, MAX_BASE, MAX_LEVEL = 64, 128, 2048, 5
SHRINK_ABOVE, GROW_AT_MOST, GROW_HOLD = 0.60, 0.45, 3.0
LAMP_TEXEL_M, REF_MAX_M, GUARD = 0.004, 4.0, 16
MAX_BAND, BAND_NEAR_M, BAND_HYST = 4, 8.0, 2.0 ** 0.5
FPS = 60


def pow2_round(v):
    return 1 if not v > 1 else 1 << int(min(math.floor(math.log2(v) + 0.5), 31))


def tile_base_size(tan_w, radius, guard):
    d_ref = min(1.5 * max(radius, 0.0), REF_MAX_M)
    return min(max(pow2_round(tan_w * d_ref / LAMP_TEXEL_M + 2.0 * guard), MIN_BASE), MAX_BASE)


def band_of(d):
    if not d < 1e6:
        return MAX_BAND
    if not d >= BAND_NEAR_M:
        return 0
    return min(int(math.floor(math.log2(d / BAND_NEAR_M))) + 1, MAX_BAND)


def next_band(band, d):
    up, down = band_of(d / BAND_HYST), band_of(d * BAND_HYST)
    if up > band:
        return up
    return down if down < band else band


def level_size(base, shift):
    return max(MIN_TILE, base >> min(max(shift, 0), 31))


def valve_shift(pressure, band):
    return min(max(pressure - (MAX_BAND - band), 0), MAX_LEVEL)


def demand(bases, bands, pressure, atlas):
    return sum(level_size(b, valve_shift(pressure, d)) ** 2 for b, d in zip(bases, bands)) / atlas ** 2


def ceiling(bases, bands):
    c = 0
    for b, d in zip(bases, bands):
        octaves = 0
        while octaves < MAX_LEVEL and level_size(b, octaves) > MIN_TILE:
            octaves += 1
        if octaves:
            c = max(c, octaves + MAX_BAND - d)
    return c


class Valve:
    """LevelController::update with ValveTiles (no fixed tiles in the model)."""
    def __init__(self):
        self.level, self.hold = 0, 0.0

    def update(self, bases, bands, atlas, dt):
        top = ceiling(bases, bands)
        self.level = min(self.level, top)
        before = self.level
        while self.level < top and demand(bases, bands, self.level, atlas) > SHRINK_ABOVE:
            self.level += 1
            self.hold = 0.0
        if self.level == before and self.level > 0:
            down = self.level - 1
            at = demand(bases, bands, down, atlas)
            while down > 0 and demand(bases, bands, down - 1, atlas) == at:
                down -= 1
            if at <= GROW_AT_MOST:
                self.hold += dt
                if self.hold >= GROW_HOLD:
                    self.level, self.hold = down, 0.0
            else:
                self.hold = 0.0
        elif self.level == 0:
            self.hold = 0.0


class Lamp:
    def __init__(self, pos, fov, radius, down=True):
        self.pos = np.array(pos, float)
        self.fov, self.r = fov, radius
        self.t = math.tan(0.5 * fov)
        self.g = 1 if self.t <= 1.05 else min(int(math.ceil(self.t - 0.05)), 4)  # gridAxis
        self.far = 1.5 * radius
        self.fwd = np.array([0, 0, -1.0]) if down else np.array([-1.0, 0, 0])
        self.u = np.array([1.0, 0, 0]) if down else np.array([0, 1.0, 0])
        self.v = np.array([0, 1.0, 0]) if down else np.array([0, 0, 1.0])
        self.guard = GUARD if self.g > 1 else 0
        self.base = tile_base_size(2 * self.t / self.g, radius, self.guard)
        self.cells = []
        for j in range(self.g):
            for i in range(self.g):
                a0, a1 = -self.t + 2 * self.t * i / self.g, -self.t + 2 * self.t * (i + 1) / self.g
                b0, b1 = -self.t + 2 * self.t * j / self.g, -self.t + 2 * self.t * (j + 1) / self.g
                self.cells.append(self.cell_points(a0, a1, b0, b1))

    def cell_points(self, a0, a1, b0, b1, n=7):
        """points filling the cell's volume: rays through the cell, from the apex to the floor (z = 0) or the far range"""
        pts = []
        for a in np.linspace(a0, a1, n):
            for b in np.linspace(b0, b1, n):
                d = self.fwd + a * self.u + b * self.v
                d = d / np.linalg.norm(d)
                t_end = self.far
                if d[2] < -1e-6:
                    t_end = min(t_end, self.pos[2] / -d[2])
                for f in np.linspace(0.05, 1.0, n):
                    pts.append(self.pos + d * t_end * f)
        return np.array(pts)


def room():
    lamps = [Lamp((x, y, 2.5), 2.5, 8.0) for x in (-3, 3) for y in (-3, 3)]
    lamps += [Lamp((5.8, y, 2.0), 1.0, 6.0, down=False) for y in (-4.5, -1.5, 1.5, 4.5)]
    return 'room, 4 wide lamps', lamps, [(-4, -4), (4, -4), (4, 4), (-4, 4)]


def room_fits():
    lamps = [Lamp((x, y, 2.5), 1.2, 4.0) for x in (-3, 3) for y in (-3, 3)]
    lamps += [Lamp((5.8, y, 2.0), 1.0, 2.5, down=False) for y in (-4.5, -1.5, 1.5, 4.5)]
    return 'room, lamps that fit', lamps, [(-4, -4), (4, -4), (4, 4), (-4, 4)]


def club():
    rng = np.random.default_rng(3)
    lamps = [Lamp((rng.uniform(-9, 9), rng.uniform(-9, 9), rng.uniform(3, 5)), rng.uniform(1.2, 3.0), rng.uniform(5, 12)) for _ in range(20)]
    return 'club, 20 lamps', lamps, [(-7, -7), (7, -7), (7, 7), (-7, 7)]


def walk(path, speed=1.4):
    pts = [np.array(p, float) for p in path] + [np.array(path[0], float)]
    seg = [(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
    total = sum(np.linalg.norm(b - a) for a, b in seg)
    out = []
    for f in range(int(opts.seconds * FPS)):
        s = (f / FPS * speed) % total
        for a, b in seg:
            l = np.linalg.norm(b - a)
            if s <= l:
                xy = a + (b - a) * s / l
                break
            s -= l
        out.append(np.array([xy[0], xy[1], 1.7]))
    return out


def run(name, lamps, path, atlas):
    cams = walk(path)
    tiles = [(li, ci) for li, l in enumerate(lamps) for ci in range(len(l.cells))]
    bases = [lamps[li].base for li, _ in tiles]
    bands = [MAX_BAND] * len(tiles)
    valve = Valve()
    sizes = None
    changes = changes_at_room = 0
    pressures, demands, texels = [], [], []
    for cam in cams:
        for k, (li, ci) in enumerate(tiles):
            d = float(np.min(np.linalg.norm(lamps[li].cells[ci] - cam, axis=1)))
            bands[k] = next_band(bands[k], d)
        valve.update(bases, bands, atlas, 1.0 / FPS)
        want = [level_size(b, valve_shift(valve.level, d)) for b, d in zip(bases, bands)]
        if sizes is not None:
            n = sum(1 for a, b in zip(sizes, want) if a != b)
            changes += n
            if valve.level == 0:
                changes_at_room += n
        sizes = want
        pressures.append(valve.level)
        demands.append(sum(s * s for s in want) / atlas ** 2)
        # the floor texel under the camera: the downward lamps whose footprint holds the camera's (x, y), from the cell's interior
        k = 0
        for l in lamps:
            if l.fwd[2] > -0.5:
                k += len(l.cells)
                continue
            h = l.pos[2]
            du, dv = (cam[0] - l.pos[0]) / h, (cam[1] - l.pos[1]) / h
            if abs(du) <= l.t and abs(dv) <= l.t:
                ci = min(int((du + l.t) / (2 * l.t) * l.g), l.g - 1)
                cj = min(int((dv + l.t) / (2 * l.t) * l.g), l.g - 1)
                interior = sizes[k + cj * l.g + ci] - 2 * l.guard
                texels.append(1000 * (2 * l.t / l.g) * h / interior)
            k += len(l.cells)
    seconds = len(cams) / FPS
    at_zero = 100.0 * pressures.count(0) / len(pressures)
    return dict(scene=name, atlas=atlas, tiles=len(tiles), demand_at_lamp_sizes=demand(bases, [MAX_BAND] * len(tiles), 0, atlas),
                changes_per_s=changes / seconds, changes_at_room=changes_at_room, pressure_min=min(pressures), pressure_max=max(pressures),
                share_at_zero=at_zero, demand_max=max(demands),
                texel_median=float(np.median(texels)) if texels else None, texel_p90=float(np.percentile(texels, 90)) if texels else None)


def main():
    failures = 0
    print('tile sizes from the lamp alone (4 mm texels at d_ref = min(1.5 r, 4 m), guard 16 for cells), the valve over the budget')
    print('| scene | atlas | tiles | demand at the lamps\' sizes | pressure (min-max, % of frames at 0) | demand max | size changes /s | '
          'of them at pressure 0 | floor texel under the camera, median / p90 (mm) |')
    print('|---|---|---|---|---|---|---|---|---|')
    for scene in (room(), room_fits(), club()):
        for atlas in (4096, 8192):
            r = run(*scene, atlas)
            failures += r['changes_at_room'] > 0
            tex = '%.1f / %.1f' % (r['texel_median'], r['texel_p90']) if r['texel_median'] is not None else 'n/a'
            print('| %s | %d | %d | %.2f | %d-%d (%.0f %%) | %.2f | %.2f | %d%s | %s |' % (
                r['scene'], atlas, r['tiles'], r['demand_at_lamp_sizes'], r['pressure_min'], r['pressure_max'], r['share_at_zero'],
                r['demand_max'], r['changes_per_s'], r['changes_at_room'], '  FAIL' if r['changes_at_room'] else '', tex))
            sys.stdout.flush()
    print('\nsizing model', 'PASS' if failures == 0 else 'FAIL (%d)' % failures)
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
