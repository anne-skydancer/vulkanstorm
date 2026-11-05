#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Stage S1 gate: the stable sun cascades against the old fit, offline.

The old fit is cascade_fit_model.py's line-by-line port of the stock
LLPipeline::generateSunShadow. The new fit mirrors indra/newview/llshadowcascadefit.h as
pipeline.cpp uses it: splits over [camera near, RenderShadowDistance] with the split
exponent's y, the bounding sphere of each frustum slice (quantised radius), the light view's up
locked to north, the orthographic box moved in whole texels of a world-anchored grid on square
maps of the same texel count (1456 x 1456 at 1920x1080), depth = sphere + one radius towards the
light.

Measured over camera sweeps in three scenes (a room on the ground, the same room as a skybox at
2500 m, open land), at shadow distances 64 / 128 / 192 / 256 m:
  * stability: split distances, texel size and the texel grid's phase at a fixed world point
    while walking and turning (new: must not change), cascade changes of a fixed floor point;
  * caster load: scene objects inside each cascade's cull volume, summed over the cascades,
    new / old, the median over all camera positions (the S1 performance gate is <= 1.15 at the
    default 128 m). Old: the receiver box x/y from the far plane up to the light. New: the stable
    box narrowed to the slice corners' light-space bounds plus 16 texels and 5 cm, never a set of
    visible objects; the far cascades 2-3 render on alternate frames (cache), so count half;
  * fixed casters (roof, tree, building, avatar) while the camera turns and tilts around their
    shadow: in every cascade whose slice holds the shadow, the caster is in the cull volume;
  * texel size of cascade 0 (density near the camera).
Exit status 0 when the gate and the stability checks pass.
"""

from __future__ import annotations

import math
import statistics
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cascade_fit_model as m  # noqa: E402

RES_SQUARE = (int(math.sqrt(1920 * 1080)) + 16) & ~0xF   # BlurHappySize(sqrt(resX * resY), 1)
SPLIT_EXP_Y = 3.0


def light_up(d):
    d = d / np.linalg.norm(d)
    ref = np.array([0.0, 1.0, 0.0]) if abs(d[1]) <= 0.9 else np.array([1.0, 0.0, 0.0])
    u = ref - d * (ref @ d)
    return u / np.linalg.norm(u)


def split_distances(near, far, e):
    d = [near] + [near + (far - near) * ((i / 4.0) ** max(e, 0.1)) for i in range(1, 5)]
    d[1] = min(d[1] * 1.25, d[2])
    return d


def slice_sphere(k, n, f):
    kk = 1.0 + k * k
    c = min(0.5 * (n + f) * kk, f)
    return c, math.sqrt((f - c) ** 2 + (f * k) ** 2)


def quantize(r):
    octave = 2.0 ** math.floor(math.log2(r))
    step = octave / 16.0
    return math.ceil(r / step) * step


def snapped(cx, cy, r, res, ax, ay):
    t = 2.0 * r / (res - 1.0)
    lo_x = ax + math.floor((cx - r - ax) / t) * t
    lo_y = ay + math.floor((cy - r - ay) / t) * t
    return lo_x, lo_y, lo_x + res * t, lo_y + res * t, t


GUARD = 1.1   # SUN_CASCADE_GUARD: box half-size / sphere radius of the cached cascades 2-3


def slice_corners(cam, dd, j):
    """The frustum section of cascade j as pipeline.cpp builds it (5 % widened corner rays)."""
    base = cam.corners(far=16.0)
    eye = cam.origin
    fr = base.copy()
    for i in range(4):
        delta = m.norm(base[i + 4] - eye + (base[i + 4] - base[(i + 2) % 4 + 4]) * 0.05)
        dp = delta @ cam.at
        fr[i] = eye + delta * dd[j] * 0.75 / dp
        fr[i + 4] = eye + delta * dd[j + 1] * 1.25 / dp
    return fr


def new_fit(cam, sun_dir, distance, cache=True):
    light_dir = -m.norm(np.array(sun_dir, float))
    up = light_up(light_dir)
    near = min(max(cam.near, 0.01), 4.0)
    far = max(min(distance, cam.far), near + 1.0)
    d = split_distances(near, far, SPLIT_EXP_Y)
    base = cam.corners(far=16.0)
    eye = cam.origin
    k = 0.0
    for i in range(4):
        delta = base[i + 4] - eye
        delta = delta + (base[i + 4] - base[(i + 2) % 4 + 4]) * 0.05
        delta = m.norm(delta)
        dp = delta @ cam.at
        k = max(k, np.linalg.norm(delta - cam.at * dp) / max(dp, 1e-3))
    view = m.look(eye, light_dir, -up)
    anchor = m.xform(view, np.zeros((1, 3)))[0]
    out = {"clip": d[1:], "dd": d, "cascades": [], "view": view}
    for j in range(4):
        dist, r = slice_sphere(k, d[j] * 0.75, d[j + 1] * 1.25)
        r = quantize(r)
        rb = r * GUARD if (cache and j >= 2) else r
        c = m.xform(view, (eye + cam.at * dist)[None, :])[0]
        lo_x, lo_y, hi_x, hi_y, t = snapped(c[0], c[1], rb, RES_SQUARE, anchor[0], anchor[1])
        out["cascades"].append({
            "empty": False, "mode": "ortho", "texel": t, "radius": r, "box_radius": rb,
            "sphere_centre": eye + cam.at * dist,
            "mn": np.array([lo_x, lo_y, c[2] - rb]), "mx": np.array([hi_x, hi_y, c[2] + 2 * rb]),
            "view": view, "width": hi_x - lo_x, "height": hi_y - lo_y,
            "slice": slice_corners(cam, d, j),
        })
    return out


def cull_volume(c, box_only=False):
    """pipeline.cpp's cull box of a cascade: the stable box narrowed to the slice corners'
    light-space bounds + 16 texels + 5 cm (+ the guard slack of a cached cascade), from the
    slice's far side up to the light. lo/hi in light-view space; hi z = towards the light."""
    lo, hi = c["mn"].copy(), c["mx"].copy()
    hi[2] = np.inf   # the cull near plane is 2 x draw distance towards the light
    if box_only:
        return lo, hi
    lp = m.xform(c["view"], c["slice"])
    margin = 16 * c["texel"] + 0.05 + (c["box_radius"] - c["radius"])
    lo[0] = max(lo[0], lp[:, 0].min() - margin)
    hi[0] = min(hi[0], lp[:, 0].max() + margin)
    lo[1] = max(lo[1], lp[:, 1].min() - margin)
    hi[1] = min(hi[1], lp[:, 1].max() + margin)
    lo[2] = max(lo[2], lp[:, 2].min() - margin)
    return lo, hi


def in_volume(view, o, lo, hi):
    corners = np.array([[x, y, z] for x in (o["lo"][0], o["hi"][0]) for y in (o["lo"][1], o["hi"][1]) for z in (o["lo"][2], o["hi"][2])])
    p = m.xform(view, corners)
    return not (p[:, 0].max() < lo[0] or p[:, 0].min() > hi[0] or p[:, 1].max() < lo[1] or p[:, 1].min() > hi[1]
                or p[:, 2].max() < lo[2])


def caster_count(fit, objects):
    """Objects inside the cull volume of every rendered cascade: x/y of the box, from the far
    plane up to the light (the near plane is moved 2 x draw distance towards the light)."""
    total = 0
    for c in fit["cascades"]:
        if c.get("empty"):
            continue
        for o in objects:
            corners = np.array([[x, y, z] for x in (o["lo"][0], o["hi"][0]) for y in (o["lo"][1], o["hi"][1]) for z in (o["lo"][2], o["hi"][2])])
            p = m.xform(c["view"], corners)
            if p[:, 0].max() < c["mn"][0] or p[:, 0].min() > c["mx"][0]:
                continue
            if p[:, 1].max() < c["mn"][1] or p[:, 1].min() > c["mx"][1]:
                continue
            if p[:, 2].max() < c["mn"][2]:
                continue
            total += 1
    return total


def caster_count_new(fit, cam, objects):
    """The new path per frame: casters in each cascade's cull volume (slice-bound, never a visible
    set); the far cascades 2-3 render on alternate frames (the cache), so they count half."""
    total = 0.0
    for j, c in enumerate(fit["cascades"]):
        lo, hi = cull_volume(c)
        n = sum(1 for o in objects if in_volume(c["view"], o, lo, hi))
        total += n * (0.5 if j >= 2 else 1.0)
    return total


def fixed_caster_test(failures):
    """A caster stays put, the camera turns and tilts around its shadow: whenever the shadowed
    point lies in a cascade's slice (and so is shaded from that cascade), the caster must be in
    that cascade's cull volume, and inside its box (x/y). Roof, tree, building and avatar."""
    s = m.sun(35.0, 200.0)
    light = -m.norm(s)
    room = m.room_scene(22.0, outdoor=True)
    land = m.outdoor_scene(22.0)
    cases = [("roof", room, [o for o in room if o["name"] == "roof"][0], 22.0),
             ("tree crown", land, [o for o in land if o["name"] == "crown 0"][0], 22.0),
             ("house", land, [o for o in land if o["name"] == "house S"][0], 22.0),
             ("avatar", land, [o for o in land if o["name"] == "avatar"][0], 22.0)]
    print("\n## fixed casters, camera turning and tilting around their shadow (sun 35 deg, 128 m)")
    for name, objects, caster, ground in cases:
        centre = (caster["lo"] + caster["hi"]) / 2
        t = (centre[2] - ground) / -light[2]
        point = centre + light * t                       # where the caster's centre shadows the ground
        checked = misses = box_misses = 0
        for dist in (3.0, 8.0, 20.0, 45.0):
            for yaw in range(0, 360, 15):
                for pitch in (-40.0, -20.0, 0.0, 10.0):
                    a = math.radians(yaw)
                    eye = point - np.array([math.cos(a), math.sin(a), 0.0]) * dist + np.array([0.0, 0.0, 1.7])
                    cam = m.Camera(eye, yaw, pitch)
                    fit = new_fit(cam, s, 128.0)
                    for j, c in enumerate(fit["cascades"]):
                        if not m.aabb_in_planes(point - 0.01, point + 0.01, m.planes_from_corners(c["slice"])):
                            continue
                        checked += 1
                        lo, hi = cull_volume(c)
                        if not in_volume(c["view"], caster, lo, hi):
                            misses += 1
                        p = m.xform(c["view"], point[None, :])[0]
                        if not (c["mn"][0] <= p[0] <= c["mx"][0] and c["mn"][1] <= p[1] <= c["mx"][1]):
                            box_misses += 1
        print(f"{name:10s}: {checked:5d} (camera, cascade) pairs where the shadow is in the slice; "
              f"caster culled in {misses}; shadow point outside the box in {box_misses}")
        if misses or box_misses or checked == 0:
            failures.append(f"fixed caster {name}: {misses} culled, {box_misses} outside the box")


BIAS_TEXELS = 3.0   # RenderShadowBiasTexels


def altitude_test(failures):
    """The owner's skybox case: a skybox room at 1000-3000 m with the ground far below. The bias
    in metres of each cascade (the gap below which a caster loses its shadow on a receiver):
    old fit (receiver depth range x the uniform with its altitude term), S1 as first built (the
    sphere depth range 3 r x the same uniform), S1 now (RenderShadowBiasTexels texels, world units)."""
    sdir = m.sun(35.0, 200.0)
    def uniform(z):
        return abs(-0.002 - 0.007 * z / 3000.0) * 2.0
    print("\n## skybox at altitude: bias in metres per cascade 0 / 1 / 2 / 3 (cm)")
    for z in (25.0, 1000.0, 2000.0, 3000.0):
        room = m.room_scene(z, outdoor=False)
        room.append(m.box((-128, -128, 18.0), (128, 128, 22.0), "ground far below"))
        depths = []
        for yaw in range(0, 360, 30):
            f = m.fit(m.Camera((-3.0, 0.0, z + 1.7), yaw, -10.0), room, sdir)
            if f:
                depths.append([0.0 if c["empty"] else c["depth"] for c in f["cascades"]])
        od = np.median(np.array(depths), axis=0)
        print(f"z {z:5.0f} m  old fit                         " + " ".join(f"{100 * uniform(z) * d:7.1f}" for d in od))
        for dist in (64.0, 128.0, 256.0):
            f = new_fit(m.Camera((-3.0, 0.0, z + 1.7), 0.0, -10.0), sdir, dist, cache=False)
            first = [100 * uniform(z) * 3 * c["radius"] for c in f["cascades"]]
            now = [100 * BIAS_TEXELS * c["texel"] for c in f["cascades"]]
            print(f"           S1 first build, {dist:3.0f} m          " + " ".join(f"{x:7.1f}" for x in first))
            print(f"           S1 now (3 texels), {dist:3.0f} m       " + " ".join(f"{x:7.1f}" for x in now))
            if z == 3000.0 and dist == 128.0:
                ground = new_fit(m.Camera((-3.0, 0.0, 23.7), 0.0, -10.0), sdir, dist, cache=False)
                same = all(abs(a["texel"] - b["texel"]) < 1e-9 for a, b in zip(f["cascades"], ground["cascades"]))
                if not same:
                    failures.append("bias depends on altitude")
                if now[0] > 3.0 or now[1] > 12.0:
                    failures.append(f"near cascades' bias too large: {now[:2]}")


def ray_hits_box(p, d, lo, hi):
    """Does the ray p + t d (t > 1 cm) hit the box [lo, hi]? (vectorised over boxes)"""
    with np.errstate(divide="ignore", invalid="ignore"):
        inv = 1.0 / d
        t0 = (lo - p) * inv
        t1 = (hi - p) * inv
    tn = np.nanmax(np.minimum(t0, t1), axis=1)
    tf = np.nanmin(np.maximum(t0, t1), axis=1)
    return (tn <= tf) & (tf > 0.01)


class CascadeCacheSim:
    """The far-cascade cache of pipeline.cpp, frame by frame: cascades 2-3 render on alternate
    frames and are reused in between when the reuse test passes. A reused map holds the casters
    of the frame it was rendered on (that frame's cull set). check_footprint adds the fix: reuse
    also needs this frame's slice footprint (+ margin) inside the cull box the map was rendered
    with."""

    def __init__(self, caching=True, check_footprint=False):
        self.caching, self.check_footprint = caching, check_footprint
        self.frame = 0
        self.cache = {}

    def frame_cull_sets(self, cam, objects, sdir, distance):
        self.frame += 1
        fit = new_fit(cam, sdir, distance, cache=self.caching)
        sets = []
        for j, c in enumerate(fit["cascades"]):
            lo, hi = cull_volume(c)
            reuse = False
            if self.caching and j >= 2 and j in self.cache:
                cached = self.cache[j]
                off = ((self.frame + j) & 1) != 0
                if off and cached["frame"] + 1 == self.frame:
                    # the C++ test: this frame's sphere inside the cached box (in the cached light view)
                    centre = cam.origin + cam.at * 0  # placeholder, replaced below
                    cc = m.xform(cached["view"], c["sphere_centre"][None, :])[0]
                    r = c["radius"]
                    reuse = (cc[0] - r >= cached["mn"][0] and cc[0] + r <= cached["mx"][0] and
                             cc[1] - r >= cached["mn"][1] and cc[1] + r <= cached["mx"][1] and
                             cc[2] - r >= cached["mn"][2] and cc[2] + r <= cached["mx"][2])
                    if reuse and self.check_footprint:
                        lp = m.xform(cached["view"], c["slice"])
                        margin = 16 * c["texel"] + 0.05
                        clo, chi = cached["cull_lo"], cached["cull_hi"]
                        reuse = (lp[:, 0].min() - margin >= clo[0] and lp[:, 0].max() + margin <= chi[0] and
                                 lp[:, 1].min() - margin >= clo[1] and lp[:, 1].max() + margin <= chi[1] and
                                 lp[:, 2].min() - margin >= clo[2])
            if reuse:
                self.reused = getattr(self, "reused", 0) + 1
                sets.append(self.cache[j]["set"])
                self.cache[j]["frame"] = self.frame
                continue
            cull = [o for o in objects if in_volume(c["view"], o, lo, hi)]
            sets.append(cull)
            if self.caching and j >= 2:
                self.cache[j] = {"frame": self.frame, "view": c["view"], "mn": c["mn"], "mx": c["mx"],
                                 "cull_lo": lo, "cull_hi": hi, "set": cull}
        return fit, sets


def temporal_test(failures):
    """Consecutive frames, nothing in the scene moving: still camera, slow and fast turns, a slow
    walk. Ground points in view (cascades 2-3: 16 m and more at 128 m) are shadowed when a caster
    in the cull set of the map they are sampled from blocks the sun; any frame where that differs
    from the truth (all casters) is a wrong pixel, and a change from the previous frame is a
    flicker."""
    sdir = m.sun(35.0, 200.0)
    to_sun = m.norm(sdir)
    land = m.outdoor_scene(22.0)
    # more casters out in the far cascades: a grove of trees between 15 and 120 m
    rng = np.random.default_rng(7)
    for k in range(60):
        a = rng.uniform(0, 2 * np.pi); dist = rng.uniform(15, 120)
        x, y = 0.0 + dist * np.cos(a), 0.0 + dist * np.sin(a)
        land.append(m.box((x - 0.4, y - 0.4, 22.0), (x + 0.4, y + 0.4, 26.0), f"grove trunk {k}"))
        land.append(m.box((x - 2.5, y - 2.5, 25.0), (x + 2.5, y + 2.5, 29.0), f"grove crown {k}"))
    casters = [o for o in land if not o["name"].startswith("terrain")]
    lo_all = np.array([o["lo"] for o in casters]); hi_all = np.array([o["hi"] for o in casters])
    names = [o["name"] for o in casters]
    # ground sample points: a grid out to 130 m
    g = np.linspace(-130, 130, 61)
    pts = np.array([[x, y, 22.0] for x in g for y in g])
    truth = np.array([ray_hits_box(p, to_sun, lo_all, hi_all).any() for p in pts])
    print("\n## consecutive frames, nothing moving (open land with a grove; 128 m; cascades 2-3 cached)")

    def sequences():
        eye = np.array([0.0, 0.0, 23.7])
        yield "still camera", [m.Camera(eye, 30.0, -12.0) for _ in range(12)]
        yield "turning 10 deg/s", [m.Camera(eye, 30.0 + 10.0 * f / 60.0, -12.0) for f in range(24)]
        yield "turning 90 deg/s", [m.Camera(eye, 30.0 + 90.0 * f / 60.0, -12.0) for f in range(24)]
        yield "walking 1.5 m/s", [m.Camera(eye + np.array([1.5 * f / 60.0, 0.0, 0.0]), 30.0, -12.0) for f in range(24)]
        yield "flick 6 deg/frame", [m.Camera(eye, 30.0 + 6.0 * f, -12.0) for f in range(16)]
        yield "flick 12 deg/frame", [m.Camera(eye, 30.0 + 12.0 * f, -12.0) for f in range(12)]

    for label, cams in sequences():
        for mode, kwargs in (("cache as built", {"caching": True}),
                             ("cache + footprint test", {"caching": True, "check_footprint": True}),
                             ("no cache", {"caching": False})):
            sim = CascadeCacheSim(**kwargs)
            prev = None; wrong_total = 0; flicker_total = 0; checked = 0
            for cam in cams:
                fit, sets = sim.frame_cull_sets(cam, land, sdir, 128.0)
                planes = m.planes_from_corners(cam.corners())
                vd = (pts - cam.origin) @ cam.at
                state = np.zeros(len(pts), dtype=np.int8) - 1   # -1: not in cascades 2-3 / not visible
                for j in (2, 3):
                    c = fit["cascades"][j]
                    near = fit["dd"][j] * 0.75 if j > 0 else 0
                    far = fit["dd"][j + 1] * 1.25
                    sel = (vd >= fit["dd"][j]) & (vd < fit["dd"][j + 1])
                    if not sel.any():
                        continue
                    allowed = np.array([n in {o["name"] for o in sets[j]} for n in names])
                    if not allowed.any():
                        lo_c = hi_c = None
                    for i in np.nonzero(sel)[0]:
                        if not m.aabb_in_planes(pts[i] - 0.01, pts[i] + 0.01, planes):
                            continue
                        hit = ray_hits_box(pts[i], to_sun, lo_all[allowed], hi_all[allowed]).any() if allowed.any() else False
                        state[i] = 1 if hit else 0
                vis = state >= 0
                checked += int(vis.sum())
                wrong_total += int(np.sum(vis & (state != truth.astype(np.int8))))
                if prev is not None:
                    both = vis & (prev >= 0)
                    flicker_total += int(np.sum(both & (state != prev)))
                prev = state
            print(f"{label:18s} {mode:24s}: wrong (shadow missing) {wrong_total:5d} of {checked} point-frames; "
                  f"frame-to-frame flips {flicker_total}")
            if mode != "cache as built" and (wrong_total or flicker_total):
                failures.append(f"{label} / {mode}: {wrong_total} wrong, {flicker_total} flips")


def env_transition_test(failures):
    """The owner's repro: a still camera while the environment switches from fixed Midday to the
    region's sky, the sun turning over N frames (an EEP transition). Far cascades 2-3 render on
    alternate frames; a reused map was rendered with the previous frame's light. Ground points in
    cascades 2-3 are shadowed when a caster in the map's cull set blocks the map's light; any frame
    where that differs from the truth (all casters, this frame's light) is a wrong pixel. Old reuse
    test: the light within 0.2 degrees of the map's. New: the light's turn moves no caster in the
    cascade's depth range by more than a quarter texel (sin(angle) x range <= texel / 4)."""
    land = m.outdoor_scene(22.0)
    rng = np.random.default_rng(11)
    for k in range(60):
        a = rng.uniform(0, 2 * np.pi); dist = rng.uniform(15, 120)
        x, y = dist * np.cos(a), dist * np.sin(a)
        land.append(m.box((x - 0.4, y - 0.4, 22.0), (x + 0.4, y + 0.4, 30.0), f"grove trunk {k}"))
        land.append(m.box((x - 2.5, y - 2.5, 28.0), (x + 2.5, y + 2.5, 34.0), f"grove crown {k}"))
    casters = [o for o in land if not o["name"].startswith("terrain")]
    lo_all = np.array([o["lo"] for o in casters]); hi_all = np.array([o["hi"] for o in casters])
    names = [o["name"] for o in casters]
    g = np.linspace(-130, 130, 61)
    pts = np.array([[x, y, 22.0] for x in g for y in g])
    cam = m.Camera(np.array([0.0, 0.0, 23.7]), 30.0, -12.0)
    planes = m.planes_from_corners(cam.corners())
    vd = (pts - cam.origin) @ cam.at
    in_view = np.array([m.aabb_in_planes(p - 0.01, p + 0.01, planes) for p in pts])
    midday = np.array(m.sun(70.0, 180.0)); region = np.array(m.sun(35.0, 200.0))
    print("\n## environment switch, still camera: Midday -> region sky over N frames (far cascades cached)")
    for n_frames in (10, 60, 240, 2400):
        for mode in ("old 0.2 deg", "new quarter texel", "no cache"):
            cache = {}
            wrong = 0; checked = 0; renders = 0
            for f in range(n_frames + 4):
                t = min(f / n_frames, 1.0)
                sdir = m.norm(midday * (1 - t) + region * t)
                fit = new_fit(cam, sdir, 128.0, cache=(mode != "no cache"))
                for j in (2, 3):
                    c = fit["cascades"][j]
                    use_light, use_set = sdir, None
                    cached = cache.get(j)
                    off = ((f + j) & 1) != 0
                    reuse = False
                    if mode != "no cache" and cached and off and cached["frame"] + 1 == f:
                        cc = m.xform(cached["view"], c["sphere_centre"][None, :])[0]
                        r = c["radius"]
                        inside = (cc[0] - r >= cached["mn"][0] and cc[0] + r <= cached["mx"][0] and
                                  cc[1] - r >= cached["mn"][1] and cc[1] + r <= cached["mx"][1])
                        if mode == "old 0.2 deg":
                            light_ok = float(cached["light"] @ sdir) > 0.999994
                        else:
                            sin_turn = float(np.linalg.norm(np.cross(cached["light"], sdir)))
                            light_ok = sin_turn * cached["range"] <= 0.25 * cached["texel"]
                        reuse = inside and light_ok
                    if reuse:
                        use_light, use_set = cached["light"], cached["set"]
                        cached["frame"] = f
                    else:
                        lo, hi = cull_volume(c)
                        use_set = {o["name"] for o in land if in_volume(c["view"], o, lo, hi)}
                        renders += 1
                        if mode != "no cache":
                            cache[j] = {"frame": f, "view": c["view"], "mn": c["mn"], "mx": c["mx"], "light": sdir,
                                        "set": use_set, "range": c["mx"][2] - c["mn"][2], "texel": c["texel"]}
                    allowed = np.array([nm in use_set for nm in names])
                    sel = in_view & (vd >= fit["dd"][j]) & (vd < fit["dd"][j + 1])
                    for i in np.nonzero(sel)[0]:
                        truth = ray_hits_box(pts[i], sdir, lo_all, hi_all).any()
                        got = ray_hits_box(pts[i], use_light, lo_all[allowed], hi_all[allowed]).any() if allowed.any() else False
                        checked += 1
                        wrong += int(truth != got)
            print(f"N = {n_frames:4d} frames ({n_frames / 60:5.1f} s at 60 fps)  {mode:18s}: wrong point-frames {wrong:5d} of {checked}; "
                  f"far-cascade renders {renders} of {2 * (n_frames + 4)}")
            if mode == "new quarter texel" and wrong:
                failures.append(f"environment switch over {n_frames} frames: {wrong} wrong")


def camera_paths(scene_z):
    """Turning in place, tilting, and walking: (label, Camera) pairs."""
    eye = np.array([-3.0, 0.0, scene_z + 1.7])
    for yaw in range(0, 360, 30):
        yield "turn", m.Camera(eye, yaw, -10.0)
    for pitch in (-45, -30, -20, -10, 0, 10):
        yield "tilt", m.Camera(eye, 30.0, pitch)
    for step in range(12):
        yield "walk", m.Camera(eye + np.array([0.5 * step, 0.0, 0.0]), 0.0, -15.0)


def phase_at(fit, point):
    c = fit["cascades"][0]
    p = m.xform(c["view"], point[None, :])[0]
    u = (p[0] - c["mn"][0]) / c["texel"]
    return u - math.floor(u), c["texel"]


def main():
    s = m.sun(35.0, 200.0)
    scenes = {
        "room on the ground": (m.room_scene(22.0, outdoor=True), 22.0),
        "skybox room at 2500 m": (m.room_scene(2500.0, outdoor=False), 2500.0),
        "open land": (m.outdoor_scene(22.0), 22.0),
    }
    failures = []
    print(f"# S1 stable cascades (slice-bound culling, far-cascade cache) vs the old fit; sun 35 deg; old maps 1920x1080, new {RES_SQUARE}x{RES_SQUARE} (same texel count)")
    gate = {}
    for distance in (32.0, 64.0, 96.0, 128.0, 160.0, 192.0, 224.0, 256.0):   # the slider's steps
        print(f"\n## shadow distance {distance:.0f} m")
        print("scene                    casters new/old (median, max)   cascade-0 texel old (min-max) / new   splits new")
        ratios_all = []
        for name, (objects, z) in scenes.items():
            ratios, t_old, t_new = [], [], []
            for _, cam in camera_paths(z):
                old = m.fit(cam, objects, s)
                if old is None:
                    continue
                new = new_fit(cam, s, distance)
                co, cn = caster_count(old, objects), caster_count_new(new, cam, objects)
                if co > 0:
                    ratios.append(cn / co)
                if not old["cascades"][0]["empty"]:
                    t_old.append(old["cascades"][0]["texel"])
                t_new.append(new["cascades"][0]["texel"])
            ratios_all += ratios
            print(f"{name:24s} {statistics.median(ratios):5.2f}, {max(ratios):5.2f}"
                  f"                    {100 * min(t_old):5.2f}-{100 * max(t_old):5.2f} cm / {100 * min(t_new):5.2f} cm"
                  f"      {', '.join(f'{x:.1f}' for x in new['clip'])}")
        gate[distance] = statistics.median(ratios_all)
        print(f"all scenes: median caster ratio {gate[distance]:.2f}")

    print("\n## stability while moving and turning (room on the ground, 128 m)")
    objects, z = scenes["room on the ground"]
    point = np.array([3.5, 1.0, z])
    for label in ("turn", "walk", "tilt"):
        cams = [c for l, c in camera_paths(z) if l == label]
        new_fits = [new_fit(c, s, 128.0) for c in cams]
        old_fits = [m.fit(c, objects, s) for c in cams]
        splits_new = {tuple(round(x, 4) for x in f["clip"]) for f in new_fits}
        splits_old = {tuple(round(x, 2) for x in f["clip"]) for f in old_fits if f}
        tex_new = {round(f["cascades"][0]["texel"], 7) for f in new_fits}
        tex_old = {round(f["cascades"][0]["texel"], 5) for f in old_fits if f and not f["cascades"][0]["empty"]}
        phases = [phase_at(f, point)[0] for f in new_fits]
        drift = max(min(abs(p - phases[0]), 1 - abs(p - phases[0])) for p in phases)
        print(f"{label:5s}: distinct split sets old {len(splits_old):2d} / new {len(splits_new)};"
              f" distinct cascade-0 texel sizes old {len(tex_old):2d} / new {len(tex_new)}; new grid phase drift {drift:.3f} texel")
        if len(splits_new) != 1:
            failures.append(f"{label}: new splits change")
        if len(tex_new) != 1:
            failures.append(f"{label}: new texel size changes")
        if drift > 0.01:
            failures.append(f"{label}: new grid phase drifts {drift:.3f}")

    fixed_caster_test(failures)
    altitude_test(failures)
    temporal_test(failures)
    env_transition_test(failures)

    print(f"\nS1 gate: median caster ratio at the default 128 m = {gate[128.0]:.2f} (limit 1.15)")
    if gate[128.0] > 1.15:
        failures.append(f"caster ratio {gate[128.0]:.2f} > 1.15")
    print("all checks passed" if not failures else "FAILED: " + "; ".join(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
