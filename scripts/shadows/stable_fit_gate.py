#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Stage S1 gate: the stable sun cascades against the old fit, offline.

The old fit is cascade_fit_model.py's line-by-line port of the stock
LLPipeline::generateSunShadow. The new fit mirrors indra/newview/llshadowcascadefit.h as
pipeline.cpp uses it: splits over [camera near, RenderShadowDistance] with the split
exponent's y, the bounding sphere of each frustum slice (quantised radius), the light view's up
locked to north, the orthographic box moved in whole texels of a world-anchored grid on square
maps of the same texel count (1456 x 1456 at 1920x1080), depth = sphere + one radius towards the
light. Every cascade renders every frame (there is no far-cascade cache), so the cost is the
plain per-frame caster count, with no discount for any cascade.

Measured over camera sweeps in three scenes (a room on the ground, the same room as a skybox at
2500 m, open land), at shadow distances 32 ... 256 m:
  * stability: split distances, texel size and the texel grid's phase at a fixed world point
    while walking and turning (new: must not change);
  * caster load: scene objects inside each cascade's cull volume, summed over the cascades,
    new / old, the median over all camera positions (the S1 performance gate is <= 1.15 at the
    default 128 m). Old: the receiver box x/y from the far plane up to the light. New: the stable
    box narrowed to the slice corners' light-space bounds plus 16 texels and 5 cm, never a set of
    visible objects;
  * fixed casters (roof, tree, building, avatar) while the camera turns and tilts around their
    shadow: in every cascade whose slice holds the shadow, the caster is in the cull volume;
  * missing shadows, frame by frame: ground points in every cascade are shadowed when a caster in
    that cascade's cull set blocks the sun; any difference from the truth (all casters, this
    frame's light and caster positions) is a wrong pixel, and any wrong pixel fails the gate.
    Cases: still camera, slow and fast turns, a walk, fast flicks (6 and 12 deg per frame), the
    sun turning (an environment switch) and casters moving through the scene;
  * sensitivity: the same check run with every cull volume halved in x and y must report wrong
    pixels, or the gate could not see a missing shadow and fails.
Exit status 0 when every check passes; the caster ratio is reported against its limit and is
not adjusted.
"""

from __future__ import annotations

import math
import statistics
import sys
import warnings
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


def new_fit(cam, sun_dir, distance):
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
        rb = r
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


def cull_volume(c, shrink=1.0):
    """pipeline.cpp's cull box of a cascade: the stable box narrowed to the slice corners'
    light-space bounds + 16 texels + 5 cm, from the
    slice's far side up to the light. lo/hi in light-view space; hi z = towards the light."""
    lo, hi = c["mn"].copy(), c["mx"].copy()
    hi[2] = np.inf   # the cull near plane is 2 x draw distance towards the light
    lp = m.xform(c["view"], c["slice"])
    margin = 16 * c["texel"] + 0.05
    lo[0] = max(lo[0], lp[:, 0].min() - margin)
    hi[0] = min(hi[0], lp[:, 0].max() + margin)
    lo[1] = max(lo[1], lp[:, 1].min() - margin)
    hi[1] = min(hi[1], lp[:, 1].max() + margin)
    lo[2] = max(lo[2], lp[:, 2].min() - margin)
    if shrink != 1.0:   # test-only: a deliberately too small volume (sensitivity check)
        for a in (0, 1):
            mid, half = 0.5 * (lo[a] + hi[a]), 0.5 * (hi[a] - lo[a]) * shrink
            lo[a], hi[a] = mid - half, mid + half
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
    set). All four cascades render every frame, so every one counts in full."""
    total = 0
    for c in fit["cascades"]:
        lo, hi = cull_volume(c)
        total += sum(1 for o in objects if in_volume(c["view"], o, lo, hi))
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


def ray_hits_boxes(points, d, lo, hi):
    """For each point: does the ray p + t d (t > 1 cm) hit any box [lo, hi]? points (N, 3), d (3,)."""
    if len(points) == 0 or len(lo) == 0:
        return np.zeros(len(points), dtype=bool)
    with np.errstate(divide="ignore", invalid="ignore"), warnings.catch_warnings():
        warnings.simplefilter("ignore", RuntimeWarning)
        inv = 1.0 / np.asarray(d, float)
        t0 = (lo[None, :, :] - points[:, None, :]) * inv
        t1 = (hi[None, :, :] - points[:, None, :]) * inv
        tn = np.nanmax(np.minimum(t0, t1), axis=2)
        tf = np.nanmin(np.maximum(t0, t1), axis=2)
    return ((tn <= tf) & (tf > 0.01)).any(axis=1)


def ground_points():
    """Ground samples: 0.5 m out to 10 m, 2 m out to 40 m, 4.3 m out to 130 m around the origin."""
    pts = []
    for half, n in ((10.0, 41), (40.0, 41), (130.0, 61)):
        g = np.linspace(-half, half, n)
        pts += [[x, y, 22.0] for x in g for y in g]
    return np.unique(np.array(pts), axis=0)


def visible_mask(cam, pts):
    ok = np.ones(len(pts), dtype=bool)
    for n, d in m.planes_from_corners(cam.corners()):
        ok &= (pts @ n + d) <= 0.01 * np.abs(n).sum()
    return ok


def frame_check(cam, sdir, casters, distance, pts, shrink=1.0):
    """One frame of the new path. Returns (cull sets, wrong, checked): a ground point is shaded
    from every cascade whose view-depth range holds it (cascade j from 0.75 x its split, where the
    shader starts to blend it in, to its far split) and is shadowed when a caster in that
    cascade's cull set blocks the sun and the point lies inside the cascade's box. Wrong = differs
    from the truth (all casters, this frame's light and positions)."""
    fit = new_fit(cam, sdir, distance)
    lo_all = np.array([o["lo"] for o in casters])
    hi_all = np.array([o["hi"] for o in casters])
    sets = []
    for c in fit["cascades"]:
        lo, hi = cull_volume(c, shrink)
        sets.append(np.array([in_volume(c["view"], o, lo, hi) for o in casters]))
    used = sets
    to_sun = m.norm(np.array(sdir, float))
    vis = visible_mask(cam, pts)
    truth = ray_hits_boxes(pts, to_sun, lo_all, hi_all)
    vd = (pts - cam.origin) @ cam.at
    dd = fit["dd"]
    wrong = checked = 0
    for j, c in enumerate(fit["cascades"]):
        sel = vis & (vd >= (dd[j] * 0.75 if j else dd[0])) & (vd < dd[j + 1])
        if not sel.any():
            continue
        p = m.xform(c["view"], pts[sel])
        inside = (p[:, 0] >= c["mn"][0]) & (p[:, 0] <= c["mx"][0]) & (p[:, 1] >= c["mn"][1]) & (p[:, 1] <= c["mx"][1])
        got = ray_hits_boxes(pts[sel], to_sun, lo_all[used[j]], hi_all[used[j]]) & inside
        checked += int(sel.sum())
        wrong += int(np.sum(got != truth[sel]))
    return sets, wrong, checked


def run_frames(frames, distance, pts, shrink=1.0):
    wrong = checked = 0
    for f in frames:
        _, w, n = frame_check(f["cam"], f["sun"], f["casters"], distance, pts, shrink)
        wrong += w
        checked += n
    return wrong, checked


def land_scene():
    """Open land with a grove of 60 trees between 15 and 120 m (casters out in the far cascades)."""
    land = m.outdoor_scene(22.0)
    rng = np.random.default_rng(7)
    for k in range(60):
        a = rng.uniform(0, 2 * np.pi)
        dist = rng.uniform(15, 120)
        x, y = dist * np.cos(a), dist * np.sin(a)
        land.append(m.box((x - 0.4, y - 0.4, 22.0), (x + 0.4, y + 0.4, 26.0), f"grove trunk {k}"))
        land.append(m.box((x - 2.5, y - 2.5, 25.0), (x + 2.5, y + 2.5, 29.0), f"grove crown {k}"))
    return [o for o in land if not o["name"].startswith("terrain")]


def frame_cases(casters):
    sun0 = m.sun(35.0, 200.0)
    eye = np.array([0.0, 0.0, 23.7])

    def still(cams):
        return [{"cam": c, "sun": sun0, "casters": casters} for c in cams]

    yield "still camera", still([m.Camera(eye, 30.0, -12.0)] * 6)
    yield "turning 10 deg/s", still([m.Camera(eye, 30.0 + 10.0 * f / 60.0, -12.0) for f in range(24)])
    yield "turning 90 deg/s", still([m.Camera(eye, 30.0 + 90.0 * f / 60.0, -12.0) for f in range(24)])
    yield "walking 1.5 m/s", still([m.Camera(eye + np.array([1.5 * f / 60.0, 0.0, 0.0]), 30.0, -12.0) for f in range(24)])
    yield "flick 6 deg/frame", still([m.Camera(eye, 30.0 + 6.0 * f, -12.0) for f in range(16)])
    yield "flick 12 deg/frame", still([m.Camera(eye, 30.0 + 12.0 * f, -12.0) for f in range(12)])
    yield "flick 12 deg/frame, tilting", still([m.Camera(eye, 30.0 + 12.0 * f, -40.0 + 6.0 * f) for f in range(12)])
    cam = m.Camera(eye, 30.0, -12.0)
    mid, region = m.sun(70.0, 180.0), sun0
    for n in (4, 60, 240):
        yield f"sun turning over {n} frames", [
            {"cam": cam, "sun": m.norm(mid * (1 - min(f / n, 1.0)) + region * min(f / n, 1.0)), "casters": casters}
            for f in range(n + 4)]
    yield "sun jumps (sun to moon)", [{"cam": cam, "sun": s, "casters": casters}
                                      for s in (m.sun(60.0, 100.0), m.sun(35.0, 280.0), m.sun(20.0, 30.0), m.sun(60.0, 100.0))]
    # moving casters: a runner (8 m/s) and a vehicle (30 m/s) cross the grove while the camera turns 6 deg/frame
    frames = []
    for f in range(24):
        runner = m.box((-50 + 0.13 * f - 0.3, -30 - 0.3, 22.0), (-50 + 0.13 * f + 0.3, -30 + 0.3, 23.8), "runner")
        car = m.box((60 - 0.5 * f - 1.0, 40 - 0.5 * f - 0.7, 22.0), (60 - 0.5 * f + 1.0, 40 - 0.5 * f + 0.7, 23.5), "vehicle")
        frames.append({"cam": m.Camera(eye, 30.0 + 6.0 * f, -12.0), "sun": sun0, "casters": casters + [runner, car]})
    yield "moving casters, turning 6 deg/frame", frames
    frames = []
    for f in range(24):  # a large mover crossing the near field, camera still
        slab = m.box((-12 + 1.0 * f, 6, 22.0), (-8 + 1.0 * f, 8, 30.0), "flying wall")
        frames.append({"cam": cam, "sun": sun0, "casters": casters + [slab]})
    yield "moving casters, still camera", frames


def missing_shadow_test(failures):
    """Every frame of every case must show no wrong pixel (no shadow missing, none spurious)."""
    casters = land_scene()
    pts = ground_points()
    print("\n## missing shadows, frame by frame (open land with a grove; 128 m; every cascade renders every frame)")
    flick = None
    for label, frames in frame_cases(casters):
        wrong, checked = run_frames(frames, 128.0, pts)
        print(f"{label:38s}: wrong point-cascades {wrong:5d} of {checked}")
        if wrong:
            failures.append(f"{label}: {wrong} wrong")
        if label == "flick 12 deg/frame":
            flick = frames
    # the gate must be able to see a missing shadow: the same frames with every cull volume
    # shrunk to half its size in x and y (casters at the volume's edge are not drawn)
    wrong, checked = run_frames(flick, 128.0, pts, shrink=0.5)
    print(f"{'sensitivity: cull volumes halved':38s}: wrong point-cascades {wrong:5d} of {checked} (must be > 0)")
    if wrong == 0:
        failures.append("sensitivity: halved cull volumes were not detected, the gate cannot see missing shadows")


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
    print(f"# S1 stable cascades (slice-bound culling, uncached) vs the old fit; sun 35 deg; old maps 1920x1080, new {RES_SQUARE}x{RES_SQUARE} (same texel count)")
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
    missing_shadow_test(failures)

    print(f"\nS1 gate: median caster ratio at the default 128 m = {gate[128.0]:.2f} (limit 1.15)")
    if gate[128.0] > 1.15:
        failures.append(f"caster ratio {gate[128.0]:.2f} > 1.15")
    print("all checks passed" if not failures else "FAILED: " + "; ".join(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
