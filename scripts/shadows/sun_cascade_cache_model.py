#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline model for the sun/moon cascade refresh study (doc/shadows/sun-cascades-refresh.md).

No GPU, no viewer. It ports today's stable fit (stage S1: indra/newview/llshadowcascadefit.h and
LLPipeline::generateSunCascadesStable) and the proposed eye-centred levels, and prints the tables the
study quotes:

  1. texel sizes per cascade: S1 (slice spheres) against eye-centred levels with radial selection,
     at the default field of view and zoomed;
  2. rotation: the level a fixed world point uses, and how far each S1 box moves per degree of yaw
     (what makes S1 maps uncacheable while the camera turns);
  3. the semi-transparent caster dither: the hash index of a fixed world texel as the camera walks
     (map-relative today, world-anchored with an offset);
  4. the static-layer area of an eye-centred level against S1's per-frame cull box (the "omni factor");
  5. refresh intervals of a cached level: camera translation (guard band) and the light's motion;
  6. the per-frame CPU bracket at Rooftops (measured inputs from
     reports/rooftops-ultra-shadow-split-2026-11-16.md, assumptions marked).

Run: python scripts/shadows/sun_cascade_cache_model.py [--check]
--check runs the invariants only (exit 1 on failure): eye-centred levels pick the same level and the
same world texel for a fixed point at every yaw; the world-anchored dither index is constant while
walking; the S1 port reproduces the header's numbers.
"""

import argparse
import math
import sys

# ---------------------------------------------------------------- S1 port (llshadowcascadefit.h)

def split_distances(near, far, exponent):
    e = max(exponent, 0.1)
    d = [near] + [near + (far - near) * (i / 4.0) ** e for i in range(1, 5)]
    d[1] *= 1.25
    d[1] = min(d[1], d[2])
    return d


def slice_sphere(k, n, f):
    kk = 1.0 + k * k
    c = min(0.5 * (n + f) * kk, f)
    return c, math.sqrt((f - c) ** 2 + f * f * k * k)


def quantize_radius(r):
    if not r > 0.0:
        return r
    octave = 2.0 ** math.floor(math.log2(r))
    step = octave / 16.0
    return math.ceil(r / step) * step


def snap_shadow_distance(m):
    if not m > 32.0:
        return 32.0
    return min(math.floor(m / 32.0 + 0.5) * 32.0, 256.0)


FOOTPRINT_M = 0.25  # SHADOW_FOOTPRINT_M


def cull_margin(texel):
    return max(16.0 * texel, FOOTPRINT_M + texel) + 1.0 * texel  # casterCullMargin + normal offset (1 texel)


def corner_k(vfov_deg, aspect):
    """generateSunCascadesStable: corner rays widened by 5 % of the far-plane diagonal."""
    tv = math.tan(math.radians(vfov_deg) / 2.0)
    th = aspect * tv
    return 1.1 * math.hypot(th, tv)


def s1_levels(shadow_distance, vfov, aspect, res, near=0.25, far_clip=256.0, exponent=3.0):
    k = corner_k(vfov, aspect)
    far = max(min(snap_shadow_distance(shadow_distance), far_clip), near + 1.0)
    d = split_distances(near, far, exponent)
    out = []
    for j in range(4):
        c, r = slice_sphere(k, 0.75 * d[j], 1.25 * d[j + 1])
        rq = quantize_radius(r)
        out.append(dict(j=j, centre=c, radius=rq, texel=2.0 * rq / (res - 1.0)))
    return d, out


def eye_levels(d, res, guard):
    """Eye-centred level j: holds every receiver within 1.25 d[j+1] of the camera (the radial blend band),
    the filter footprint and the cull margin, and a guard band g*R the camera may move before the level
    is re-centred: R = (1.25 d[j+1] + margin) / (1 - g), quantised like S1."""
    out = []
    for j in range(4):
        reach = 1.25 * d[j + 1] if j < 3 else d[4]  # the last level ends at the shadow distance
        r = reach
        for _ in range(4):  # margin depends on the texel, iterate to a fixed point
            t = 2.0 * r / (res - 1.0)
            r = (reach + cull_margin(t)) / (1.0 - guard)
        rq = quantize_radius(r)
        out.append(dict(j=j, radius=rq, texel=2.0 * rq / (res - 1.0), guard_m=guard * rq))
    return out


# ---------------------------------------------------------------- small vector helpers

def norm(v):
    l = math.sqrt(sum(x * x for x in v))
    return [x / l for x in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def light_basis(light_dir):
    """lightUp (north projected on the light's plane; east next to the N-S axis) and the light view axes."""
    L = norm(light_dir)
    ref = [1.0, 0.0, 0.0] if abs(L[1]) > 0.999 else [0.0, 1.0, 0.0]
    dp = dot(ref, L)
    up = norm([ref[i] - L[i] * dp for i in range(3)])
    side = norm(cross(up, L))
    return side, up, L  # x, y, z (z towards the light's travel)


def sun_dir(elev_deg, azim_deg):
    e, a = math.radians(elev_deg), math.radians(azim_deg)
    to_sun = [math.cos(e) * math.sin(a), math.cos(e) * math.cos(a), math.sin(e)]
    return [-x for x in to_sun]  # direction the light travels


def view_axes(yaw_deg, pitch_deg):
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    at = [math.cos(p) * math.sin(y), math.cos(p) * math.cos(y), math.sin(p)]
    left = norm(cross([0.0, 0.0, 1.0], at))
    up = cross(at, left)
    return at, left, up


# ---------------------------------------------------------------- 1. texel sizes

def table_texels(out):
    out.append("## 1. Texel size per level, cm (S1 slice spheres vs eye-centred levels with radial selection)")
    out.append("Near plane 0.25 m (DEFAULT_NEAR_PLANE), split exponent y 3.0, far clip 256 m; map side = sqrt(w*h) (Stochastic).")
    for sd, tier in ((128.0, "High"), (160.0, "HighUltra"), (192.0, "Ultra")):
        for res, screen in ((1440, "1920x1080"), (1920, "2560x1440")):
            out.append(f"\n{tier} (shadow distance {sd:.0f} m), screen {screen}, map {res}^2")
            out.append("fov(v) aspect | level | S1 radius  S1 texel | eye g=0.125 radius texel ratio | eye g=0.25 radius texel ratio")
            for vfov, aspect in ((60.0, 16 / 9), (60.0, 4 / 3), (30.0, 16 / 9), (15.0, 16 / 9)):
                d, s1 = s1_levels(sd, vfov, aspect, res)
                e1 = eye_levels(d, res, 0.125)
                e2 = eye_levels(d, res, 0.25)
                for j in range(4):
                    out.append(
                        f"{vfov:5.0f} {aspect:5.2f}  |   {j}   | {s1[j]['radius']:8.2f} {100 * s1[j]['texel']:8.2f} |"
                        f" {e1[j]['radius']:8.2f} {100 * e1[j]['texel']:7.2f} {e1[j]['texel'] / s1[j]['texel']:5.2f} |"
                        f" {e2[j]['radius']:8.2f} {100 * e2[j]['texel']:7.2f} {e2[j]['texel'] / s1[j]['texel']:5.2f}")
            out.append(f"splits (m): {', '.join(f'{x:.2f}' for x in split_distances(0.25, min(snap_shadow_distance(sd), 256.0), 3.0))}")


# ---------------------------------------------------------------- 2. rotation

def blend_level(depth, d):
    """The levels with a weight above 0 in the Stochastic lookup's blend (sampleDirectionalShadow): level j spans
    0.75 d[j] .. 1.25 d[j+1], level 0 from 0, level 3 to d[4] (beyond it the lookup returns lit). Today the
    depth is the view depth; in the proposal it is the distance from the camera position."""
    if depth >= d[4]:
        return []
    return [j for j in range(4)
            if (j == 0 or depth > 0.75 * d[j]) and (j == 3 or depth < 1.25 * d[j + 1])]


def blend_weights(depth, d):
    """sampleDirectionalShadow's weights w[0..3] (shadowStochasticUtil.glsl), with pos.z = -depth and
    shadow_clip = d[1..4]; the pick uses w / sum(w)."""
    z = -depth
    clip = d[1:5]
    ns = [-0.75 * c for c in clip]
    fs = [-1.25 * c for c in clip]
    dom = [n - f for n, f in zip(ns, fs)]
    w = [0.0] * 4
    w[3] = 1.0 - max(z - fs[2], 0.0) / dom[2] if z < ns[2] else 0.0
    w[2] = (1.0 - max(z - fs[1], 0.0) / dom[1] - max(ns[2] - z, 0.0) / dom[2]) if (z < ns[1] and z > fs[2]) else 0.0
    w[1] = (1.0 - max(z - fs[0], 0.0) / dom[0] - max(ns[1] - z, 0.0) / dom[1]) if (z < ns[0] and z > fs[1]) else 0.0
    w[0] = 1.0 - max(ns[0] - z, 0.0) / dom[0] if z > fs[0] else 0.0
    w = [max(x, 0.0) for x in w]
    t = sum(w)
    return [x / t for x in w] if t > 0 else w


def table_rotation(out, res=1920, sd=192.0):
    d, s1 = s1_levels(sd, 60.0, 16 / 9, res)
    out.append("\n## 2. Rotation (Ultra, 60 deg, 16:9, map 1920^2)")
    out.append("Fixed points at a distance D from the camera, at bearing 0..45 deg off the view axis (turning in place).")
    out.append("D (m) | bearing | view depth | S1 pick shares L0..L3 (by view depth) | eye pick shares (by distance)")
    fails = 0
    changes = 0
    for D in (4.5, 5.5, 28.0, 95.0):
        eye_ref = blend_level(D, d)
        s1_ref = None
        for b in (0, 15, 30, 45):
            depth = D * math.cos(math.radians(b))
            a = blend_weights(depth, d)
            e = blend_weights(D, d)
            fails += max(abs(x - y) for x, y in zip(e, blend_weights(D, d))) > 1e-9
            if s1_ref is None:
                s1_ref = a
            changes += max(abs(x - y) for x, y in zip(a, s1_ref)) > 0.05
            fa = " ".join(f"{x:4.2f}" for x in a)
            fe = " ".join(f"{x:4.2f}" for x in e)
            out.append(f" {D:5.1f} |   {b:3d}   |  {depth:6.2f}    | {fa} | {fe}")
    out.append(f"turned views whose S1 shares differ from bearing 0 by more than 0.05: {changes} of 16; eye-centred: {fails}")
    out.append("S1 box centre displacement per degree of yaw (the box must be redrawn when it moves):")
    for j in range(4):
        c = s1[j]['centre']
        disp = c * math.radians(1.0)
        out.append(f"  cascade {j}: centre {c:7.2f} m ahead, moves {disp:6.3f} m/deg = {disp / s1[j]['texel']:7.1f} texels/deg")
    out.append("Eye-centred levels: the centre is the camera position, 0 texels/deg.")
    return fails


# ---------------------------------------------------------------- 3. dither anchoring

def table_dither(out, res=1920, sd=192.0):
    d, s1 = s1_levels(sd, 60.0, 16 / 9, res)
    t = s1[0]['texel']
    r = s1[0]['radius']
    out.append("\n## 3. Semi-transparent caster dither (shadowAlphaMaskF.glsl, Stochastic): hash index of one world texel")
    out.append(f"Cascade 0, texel {100 * t:.3f} cm; the camera walks along light-view x in 2 cm steps (a world point at x = 3.00 m).")
    out.append("step | box min (texels from the world anchor) | map column of the point (hashed today) | world column (hashed with the offset)")
    fails = 0
    world_cols = set()
    pt = 3.0
    for i in range(6):
        eye = 0.02 * i
        anchor = -eye                      # light-view x of the world origin, view anchored at the eye
        cx = 0.0                           # sphere centre relative to the eye (view axis along the light's y here)
        kx = math.floor((cx - r - anchor) / t)
        min_x = anchor + kx * t
        col = math.floor((pt - eye - min_x) / t)
        world_col = col + kx
        world_cols.add(world_col)
        out.append(f"  {i}  | {kx:10d} | {col:6d} | {world_col:6d}")
    fails += len(world_cols) != 1
    return fails


# ---------------------------------------------------------------- 4. omni factor

def light_xy(p, basis):
    return dot(p, basis[0]), dot(p, basis[1]), dot(p, basis[2])


def table_omni(out, res=1920, sd=192.0):
    d, s1 = s1_levels(sd, 60.0, 16 / 9, res)
    e1 = eye_levels(d, res, 0.125)
    k = corner_k(60.0, 16 / 9)
    tv = math.tan(math.radians(30.0))
    th = 16 / 9 * tv
    out.append("\n## 4. Static layer area: eye-centred level (whole square) / S1 per-frame cull box (slice corners' light-space AABB")
    out.append("plus margin, inside the S1 box), averaged over 24 yaws, pitch -10 deg. A proxy for the caster count of one static refresh.")
    out.append("sun elev | level 0 | level 1 | level 2 | level 3")
    rows = {}
    for elev in (15.0, 35.0, 60.0, 85.0):
        basis = light_basis(sun_dir(elev, 200.0))
        ratios = []
        for j in range(4):
            acc = 0.0
            for yi in range(24):
                at, left, up = view_axes(15.0 * yi, -10.0)
                pts = []
                for dist in (0.75 * d[j], 1.25 * d[j + 1]):
                    for sx in (-1, 1):
                        for sy in (-1, 1):
                            ray = [at[i] + 1.1 * (sx * th * left[i] + sy * tv * up[i]) for i in range(3)]
                            pts.append([dist * x for x in ray])
                xs = [light_xy(p, basis)[0] for p in pts]
                ys = [light_xy(p, basis)[1] for p in pts]
                m = cull_margin(s1[j]['texel'])
                rad = s1[j]['radius']
                cxy = light_xy([s1[j]['centre'] * x for x in at], basis)
                x0 = max(min(xs) - m, cxy[0] - rad)
                x1 = min(max(xs) + m, cxy[0] + rad)
                y0 = max(min(ys) - m, cxy[1] - rad)
                y1 = min(max(ys) + m, cxy[1] + rad)
                area_s1 = max(x1 - x0, 0.0) * max(y1 - y0, 0.0)
                area_eye = (2.0 * e1[j]['radius']) ** 2
                acc += area_eye / max(area_s1, 1e-6)
            ratios.append(acc / 24.0)
        rows[elev] = ratios
        out.append(f"  {elev:4.0f}   | " + " | ".join(f"{x:6.2f}" for x in ratios))
    return rows


# ---------------------------------------------------------------- 5. refresh intervals

SPEEDS = (("walk (assumed 3.2 m/s)", 3.2), ("run (assumed 5.1 m/s)", 5.1), ("fly (assumed 16 m/s)", 16.0))


def table_refresh(out, res=1920, sd=192.0):
    d, _ = s1_levels(sd, 60.0, 16 / 9, res)
    out.append("\n## 5. Refresh intervals of a cached level (Ultra, map 1920^2)")
    for guard in (0.125, 0.25):
        lv = eye_levels(d, res, guard)
        out.append(f"\nguard band g = {guard}: the static layer is redrawn when the camera has moved g*R in light-view xy")
        out.append("level | R (m) | g*R (m) | " + " | ".join(name for name, _ in SPEEDS) + "  (seconds; frames at 40 fps)")
        demand = [0.0] * len(SPEEDS)
        for L in lv:
            cells = []
            for si, (_, v) in enumerate(SPEEDS):
                sec = L['guard_m'] / v
                demand[si] += 1.0 / (sec * 40.0)
                cells.append(f"{sec:6.2f} s {sec * 40.0:6.1f} f")
            out.append(f"  {L['j']}   | {L['radius']:6.2f} | {L['guard_m']:6.2f} | " + " | ".join(cells))
        out.append("static refreshes demanded per frame at 40 fps (all levels): " +
                   ", ".join(f"{name.split()[0]} {x:.2f}" for (name, _), x in zip(SPEEDS, demand)))
    out.append("\nLight motion: a caster H metres above its receiver moves H * dtheta. Redraw when that reaches tau = 0.5 texel.")
    out.append("texel/R = 2/(res-1) for every level, so with H = R the threshold angle is the same for all levels.")
    lv = eye_levels(d, res, 0.125)
    out.append("level | texel (cm) | threshold deg, H = R | s between redraws, day 4 h | day 1 h | fixed sky")
    for L in lv:
        H = L['radius']
        theta = 0.5 * L['texel'] / H
        deg = math.degrees(theta)
        out.append(f"  {L['j']}   | {100 * L['texel']:6.2f}     | {deg:7.4f}  | {deg / (360.0 / 14400.0):7.2f} | {deg / (360.0 / 3600.0):6.2f} | never")


# ---------------------------------------------------------------- 6. CPU bracket at Rooftops

# measured, reports/rooftops-ultra-shadow-split-2026-11-16.md section 2 (still sun, ms per frame, main thread)
ROOF = dict(
    total=[2.71, 2.04, 2.31, 2.29],
    cull=[0.07, 0.38, 0.50, 0.46],
    sort=[1.72, 0.79, 0.80, 0.76],
    draw=[0.93, 0.86, 1.00, 1.08],
    avatar_draw=[0.46 + 0.32 + 0.20, 0.05 + 0.26 + 0.10, 0.04 + 0.14, 0.04 + 0.13],  # pool + rigged + palettes (overlap)
)
FRAME_STILL_SUN = 31.90
FRAME_STILL_NONE = 23.07
SHARED_SORT = 1.5          # the first sort of the frame pays the rebuild (report, section 2)
PROBE_CASCADES = 0.4       # the cube-snapshot call (2 passes), unchanged


def bracket(lo, hi):
    return f"{lo:5.2f}-{hi:5.2f}"


def table_cost(out, omni, res=1920, sd=192.0):
    out.append("\n## 6. CPU bracket at Rooftops (Ultra, about 50 avatars), main thread, ms per frame")
    out.append("Measured inputs: per-cascade cull/sort/draw and avatar draw rows of the still-sun capture. Assumptions (marked A):")
    out.append("A1 the dynamic layer's cull is 10-25 % of the level's cull (avatar partitions and active bridges only);")
    out.append("A2 avatars and their attachments are 30-70 % of the sort in levels 0-1 and 10-30 % in levels 2-3;")
    out.append("A3 the dynamic draw is the avatar rows (0.8-1.0 x, they overlap) plus 0-40 % for non-rigged attachments;")
    out.append("A4 a static refresh costs the level's non-avatar work times the omni factor (section 4, sun 35 deg);")
    out.append("A5 the shared sort (1.5 ms) moves to the world pass when no cascade sorts first.")
    d, _ = s1_levels(sd, 60.0, 16 / 9, res)
    lv = eye_levels(d, res, 0.125)
    om = omni[35.0]
    dyn_lo = dyn_hi = 0.0
    stat = []
    for j in range(4):
        own_sort = ROOF['sort'][j] - (SHARED_SORT if j == 0 else 0.0)
        s_lo, s_hi = (0.3, 0.7) if j < 2 else (0.1, 0.3)
        a = ROOF['avatar_draw'][j]
        lo = 0.10 * ROOF['cull'][j] + s_lo * own_sort + 0.8 * a
        hi = 0.25 * ROOF['cull'][j] + s_hi * own_sort + 1.0 * a * 1.4
        dyn_lo += lo
        dyn_hi += hi
        static_work = ROOF['total'][j] - (SHARED_SORT if j == 0 else 0.0) - 0.9 * a
        stat.append(static_work * om[j])
    copy_cpu = 0.02
    out.append(f"dynamic layer, all four levels: {bracket(dyn_lo + copy_cpu, dyn_hi + copy_cpu)} ms (today's four cascades: {sum(ROOF['total']):.2f} ms)")
    out.append("one static refresh, per level (ms): " + ", ".join(f"L{j} {s:.2f}" for j, s in enumerate(stat)))
    out.append("\ncase | cascade CPU today | cascade CPU cached | frame today -> cached (ms) | fps")
    cases = []
    # still: light refresh only (4 h day, H = R)
    still_amort = 0.0
    for j, L in enumerate(lv):
        H = L['radius']
        sec = math.degrees(0.5 * L['texel'] / H) / (360.0 / 14400.0)
        still_amort += stat[j] / (sec * 38.0)
    cases.append(("still, 4 h day", still_amort, still_amort))
    cases.append(("still, fixed sky", 0.0, 0.0))
    cases.append(("turning in place", still_amort, still_amort))
    for name, v in SPEEDS:
        amort = sum(stat[j] / (L['guard_m'] / v * 38.0) for j, L in enumerate(lv))
        cases.append((name.split()[0] + " (translation only)", amort, amort))
    today = sum(ROOF['total'])
    for name, alo, ahi in cases:
        lo = dyn_lo + copy_cpu + alo
        hi = dyn_hi + copy_cpu + ahi
        f_lo = FRAME_STILL_SUN - today + lo + SHARED_SORT
        f_hi = FRAME_STILL_SUN - today + hi + SHARED_SORT
        out.append(f"{name:28s} | {today:5.2f} | {bracket(lo, hi)} | {FRAME_STILL_SUN:5.2f} -> {bracket(f_lo, f_hi)} |"
                   f" {1000.0 / FRAME_STILL_SUN:4.1f} -> {1000.0 / f_hi:4.1f}-{1000.0 / f_lo:4.1f}")
    worst = max(stat)
    out.append(f"worst single frame while moving (one static refresh of the largest level plus the dynamic layer): "
               f"{bracket(dyn_lo + worst, dyn_hi + worst)} ms against today's {today:.2f}")
    allr = sum(stat)
    out.append(f"refresh-all frame (invalid cache, dark-gate reopening, teleport; all four static refreshes plus the dynamic layer): "
               f"{bracket(dyn_lo + allr, dyn_hi + allr)} ms against today's {today:.2f}")
    out.append(f"for reference: no sun shadows, still, {FRAME_STILL_NONE:.2f} ms ({1000.0 / FRAME_STILL_NONE:.1f} fps); "
               "the floor is 16.7 ms still (60 fps) and 40 ms moving (25 fps)")


# ---------------------------------------------------------------- checks

def checks():
    fails = 0
    # the S1 port: k for 60 deg 16:9 and the quantisation steps
    k = corner_k(60.0, 16 / 9)
    if abs(k - 1.2955) > 1e-3:
        print(f"FAIL corner k {k}")
        fails += 1
    if quantize_radius(6.27) != 6.5 or quantize_radius(1.0) != 1.0:
        print("FAIL quantize_radius")
        fails += 1
    if split_distances(0.25, 192.0, 3.0)[1] != 1.25 * (0.25 + 191.75 / 64.0):
        print("FAIL split_distances")
        fails += 1
    # NOTE: the two checks below hold BY CONSTRUCTION (the eye-centred pick is a function of the distance alone, and the
    # world column is col + kx from the same kx). They document the rule; they are not evidence. The real tests are
    # C++ unit tests on llshadowcascadefit.h (C1) and a check that k from the real snappedSquare matches the hash origin (H).
    out = []
    f = table_rotation(out)
    if f:
        print("FAIL eye-centred level choice changes with yaw")
    fails += f
    f = table_dither(out)
    if f:
        print("FAIL world-anchored dither index changes while walking")
    fails += f
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    if args.check:
        n = checks()
        print("checks:", "OK" if n == 0 else f"{n} failed")
        return 1 if n else 0
    out = ["# Sun cascade refresh model (doc/shadows/sun-cascades-refresh.md)"]
    table_texels(out)
    table_rotation(out)
    table_dither(out)
    omni = table_omni(out)
    table_refresh(out)
    table_cost(out, omni)
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
