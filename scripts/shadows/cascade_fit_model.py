#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026 Vulkanstorm contributors
"""Offline model of the sun-shadow cascade fit in LLPipeline::generateSunShadow.

A line-by-line port (numpy) of the parts of indra/newview/pipeline.cpp that decide
what each sun cascade covers and how it is projected:

  * getVisiblePointCloud / getVisibleExtents  (receiver cloud = frustum x AABB of the
    objects whose box touches the frustum; occlusion is OFF in generateSunShadow)
  * the split distances (near_clip, far_clip, RenderShadowSplitExponent, x1.25 bump)
  * the per-split frustum section (dist[j]*0.75 .. dist[j+1]*1.25, 5% widening)
  * look(camera origin, lightDir, -up) with up = camera at-axis (or up-axis)
  * the ortho / perspective-warp ("LiSPSM-like") choice: mShadowError vs
    RenderShadowErrorCutoff, fovx vs RenderShadowFOVCutoff
  * the caster-cull frustum (ortho side planes, near plane replaced by the plane
    RenderFarClip*2 towards the light)
  * the constant depth bias of pcfShadow (shadow_bias*2 in [0,1] depth units) plus
    RenderShadowBiasError * |camera z| / 3000 (bindDeferredShader)

It prints the evidence tables used in doc/shadows/shadow-model-rework.md.
Run:  python scripts/shadows/cascade_fit_model.py [--quick]
Nothing here touches the viewer; it only needs numpy.
"""

from __future__ import annotations

import argparse
import math
import sys

import numpy as np

# ---------------------------------------------------------------- settings.xml defaults
RENDER_FAR_CLIP = 256.0                 # RenderFarClip (camera far = draw distance)
SPLIT_EXP = (3.0, 3.0, 2.0)            # RenderShadowSplitExponent
SHADOW_BIAS = -0.002                    # RenderShadowBias
SHADOW_BIAS_ERROR = -0.007              # RenderShadowBiasError
SHADOW_ERROR_CUTOFF = 5.0               # RenderShadowErrorCutoff
SHADOW_FOV_CUTOFF = 0.8                 # RenderShadowFOVCutoff
SPLITS = 3                              # RenderShadowSplits (j > 3 never true: 4 maps)
FOV_Y = math.radians(60.0)              # DEFAULT_FIELD_OF_VIEW
NEAR = 0.25                             # DEFAULT_NEAR_PLANE
SCREEN = (1920, 1080)                   # sun maps are screen sized x RenderShadowResolutionScale


def norm(v):
    n = np.linalg.norm(v)
    return v / n if n > 0 else v


class Camera:
    """LLCamera: origin, at/left/up axes, vertical fov, aspect, near, far."""

    def __init__(self, origin, yaw_deg, pitch_deg, far=RENDER_FAR_CLIP, aspect=SCREEN[0] / SCREEN[1]):
        self.origin = np.array(origin, float)
        yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
        self.at = np.array([math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), math.sin(pitch)])
        world_up = np.array([0.0, 0.0, 1.0])
        self.left = norm(np.cross(world_up, self.at))
        self.up = np.cross(self.at, self.left)
        self.aspect = aspect
        self.near = NEAR
        self.far = far

    def corners(self, near=None, far=None):
        """mAgentFrustum order: near 0-3, far 4-7 (ll, lr, ur, ul seen from the eye)."""
        near = self.near if near is None else near
        far = self.far if far is None else far
        out = []
        for d in (near, far):
            h = math.tan(FOV_Y / 2) * d
            w = h * self.aspect
            c = self.origin + self.at * d
            for sx, sy in ((1, -1), (-1, -1), (-1, 1), (1, 1)):  # left axis points left
                out.append(c + self.left * (sx * w) + self.up * (sy * h))
        return np.array(out)

    def view(self):
        """saved_view: camera space, looking down -z, x = -left (right), y = up."""
        r = np.stack([-self.left, self.up, -self.at])
        m = np.eye(4)
        m[:3, :3] = r
        m[:3, 3] = -r @ self.origin
        return m


def planes_from_corners(c):
    """Outward planes (n, d) with n.p + d > 0 outside, from 8 corners (calcAgentFrustumPlanes)."""
    centre = c.mean(axis=0)
    faces = [(0, 1, 2), (4, 6, 5), (0, 4, 1), (1, 5, 2), (2, 6, 3), (3, 7, 0)]
    out = []
    for a, b, d in faces:
        n = norm(np.cross(c[b] - c[a], c[d] - c[a]))
        dd = -n @ c[a]
        if n @ centre + dd > 0:
            n, dd = -n, -dd
        out.append((n, dd))
    return out


def aabb_in_planes(lo, hi, planes):
    """AABBInFrustum: false only if the box is wholly outside one plane."""
    for n, d in planes:
        p = np.where(n > 0, lo, hi)  # most-inside corner
        if n @ p + d > 0:
            return False
    return True


def visible_extents(objects, planes):
    lo = np.full(3, 65536.0)
    hi = np.full(3, -65536.0)
    empty = True
    for o in objects:
        if aabb_in_planes(o["lo"], o["hi"], planes):
            lo = np.minimum(lo, o["lo"])
            hi = np.maximum(hi, o["hi"])
            empty = False
    return empty, lo, hi


def visible_point_cloud(corners, objects):
    """Port of LLPipeline::getVisiblePointCloud."""
    planes = planes_from_corners(corners)
    empty, lo, hi = visible_extents(objects, planes)
    if empty:
        return None, None, None
    pp = [np.array([x, y, z]) for z in (lo[2], hi[2]) for y in (lo[1], hi[1]) for x in (lo[0], hi[0])]
    pp += list(corners)
    bs = [0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 3, 7, 2, 6]
    for i in range(12):
        v1, v2 = pp[bs[2 * i]], pp[bs[2 * i + 1]]
        for n, d in planes:
            line = v1 - v2
            d1 = line @ n
            if abs(d1) < 1e-12:
                continue
            t = -(n @ v2 + d) / d1
            if 0 < t < 1:
                pp.append(v2 + line * t)
    bplanes = [(np.array(n, float), None) for n in ((-1, 0, 0), (0, -1, 0), (0, 0, -1), (1, 0, 0), (0, 1, 0), (0, 0, 1))]
    bpts = [lo, lo, lo, hi, hi, hi]
    fs = [0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7]
    for i in range(12):
        v1, v2 = pp[fs[2 * i] + 8], pp[fs[2 * i + 1] + 8]
        for (n, _), bp in zip(bplanes, bpts):
            d = -n @ bp
            line = v1 - v2
            d1 = line @ n
            if abs(d1) < 1e-12:
                continue
            t = -(n @ v2 + d) / d1
            if 0 < t < 1:
                pp.append(v2 + line * t)
    ext_lo, ext_hi = lo - 0.05, hi + 0.05
    fp = []
    for p in pp:
        if np.any(p < ext_lo) or np.any(p > ext_hi):
            continue
        if any(n @ p + d > 0.05 for n, d in planes):
            continue
        fp.append(p)
    if not fp:
        return None, None, None
    return np.array(fp), lo, hi


def look(pos, direction, up):
    lft = norm(np.cross(direction, up))
    upn = norm(np.cross(lft, direction))
    dirn = norm(direction)
    r = np.stack([lft, upn, -dirn])
    m = np.eye(4)
    m[:3, :3] = r
    m[:3, 3] = -r @ pos
    return m


def xform(m, pts):
    return (m[:3, :3] @ pts.T).T + m[:3, 3]


def fit(cam: Camera, objects, sun_dir, res=SCREEN, frozen_up=None):
    """generateSunShadow's fit for one frame. Returns dict with per-cascade data."""
    caster_dir = norm(np.array(sun_dir, float))
    light_dir = -caster_dir
    up = cam.at.copy() if frozen_up is None else np.array(frozen_up, float)
    if frozen_up is None and abs(up @ light_dir) > 0.75:
        up = cam.up.copy()
    up = norm(up)

    fp, _, _ = visible_point_cloud(cam.corners(), objects)
    if fp is None:
        return None
    v = xform(cam.view(), fp)
    lo, hi = v.min(axis=0), v.max(axis=0)
    near_clip = float(np.clip(-hi[2], 0.01, 4.0))
    far_clip = float(np.clip(-lo[2] * 2.0, 16.0, 512.0))
    far_clip = min(far_clip, cam.far)
    rng = far_clip - near_clip
    da = 1.0 - max(abs(light_dir @ up), abs(light_dir @ cam.left))
    da = da ** SPLIT_EXP[2]
    sxp = SPLIT_EXP[1] + (SPLIT_EXP[0] - SPLIT_EXP[1]) * da
    clip = [near_clip + rng * ((i + 1) / 4.0) ** sxp for i in range(4)]
    clip[0] *= 1.25
    dist = [near_clip] + clip

    bias = SHADOW_BIAS + SHADOW_BIAS_ERROR * abs(cam.origin[2]) / 3000.0
    out = {"near": near_clip, "far": far_clip, "clip": clip, "up": up, "cascades": [], "bias": bias}

    # split sections: shadow_cam = camera, far 16, corners pushed out to dist
    base = cam.corners(far=16.0)
    eye = cam.origin
    for j in range(4):
        fr = base.copy()
        for i in range(4):
            delta = base[i + 4] - eye
            delta = delta + (base[i + 4] - base[(i + 2) % 4 + 4]) * 0.05
            delta = norm(delta)
            dp = delta @ cam.at
            fr[i] = eye + delta * dist[j] * 0.75 / dp
            fr[i + 4] = eye + delta * dist[j + 1] * 1.25 / dp
        pts, _, _ = visible_point_cloud(fr, objects)
        if pts is None:
            out["cascades"].append({"empty": True})
            continue
        view = look(eye, light_dir, -up)
        wpf = xform(view, pts)
        mn, mx = wpf.min(axis=0), wpf.max(axis=0)
        center = (mn + mx) * 0.5
        size = (mx - mn) * 0.5
        w = wpf - center
        w[:, 0] = np.abs(w[:, 0])
        w[:, 2] = np.abs(w[:, 2])
        n = len(w)
        sx, sx2, sy, sxy = w[:, 0].sum(), (w[:, 0] ** 2).sum(), w[:, 1].sum(), (w[:, 0] * w[:, 1]).sum()
        with np.errstate(all="ignore"):
            bfm = (sy * sx - n * sxy) / (sx * sx - n * sx2)
            bfb = (sx * sxy - sy * sx2) / (sx * sx - bfm * sx2)
            lx = w[:, 0] - (w[:, 1] - bfb) / bfm
            k = int(np.nanargmax(np.where(np.isfinite(lx), lx, -np.inf))) if np.any(np.isfinite(lx)) else 0
            lp = w[k]
            bfb = lp[1] - bfm * lp[0]
            err = np.abs(w[:, 0] - (w[:, 1] - bfb) / bfm).sum() / n / size[0] if size[0] > 0 else np.inf
        mode = "ortho"
        fovx = None
        if not (err > SHADOW_ERROR_CUTOFF) and np.isfinite(err):
            origin = np.array([0.0, bfb, 0.0])
            fz = fx = 1.0
            for p in w:
                atz = p - origin
                atz[0] = 0
                atz = norm(atz)
                fz = min(fz, -atz[1]) if fz > -atz[1] else fz
                atx = p - origin
                atx[2] = 0
                atx = norm(atx)
                fx = min(fx, -atx[1]) if fx > -atx[1] else fx
            fovx = math.acos(max(-1.0, min(1.0, fx)))
            cutoff = min(SHADOW_FOV_CUTOFF, 1.4)
            if not (fovx > cutoff):
                mode = "warped"
        width = mx[0] - mn[0]
        height = mx[1] - mn[1]
        depth = mx[2] - mn[2]
        out["cascades"].append({
            "empty": False, "mode": mode, "err": float(err), "fovx": fovx,
            "width": float(width), "height": float(height), "depth": float(depth),
            "texel": float(max(width / res[0], height / res[1])),
            "bias_m": float(2.0 * abs(bias) * depth),
            "origin_light": (view[:3, :3] @ np.zeros(3) + view[:3, 3]),
            "mn": mn, "mx": mx, "view": view,
        })
    return out


def cull_contains(cascade, light_dir, cam_origin, box_lo, box_hi):
    """Is a caster box inside the shadow cull frustum of an ORTHO cascade?

    Side planes = ortho x/y bounds (parallel to the light), far plane = -min.z of the
    receivers, near plane = the plane RenderFarClip*2 towards the light (shadow_near_clip).
    """
    view = cascade["view"]
    corners = np.array([[x, y, z] for x in (box_lo[0], box_hi[0]) for y in (box_lo[1], box_hi[1]) for z in (box_lo[2], box_hi[2])])
    c = xform(view, corners)
    mn, mx = cascade["mn"], cascade["mx"]
    if c[:, 0].max() < mn[0] or c[:, 0].min() > mx[0] or c[:, 1].max() < mn[1] or c[:, 1].min() > mx[1]:
        return False
    if c[:, 2].max() < mn[2]:      # wholly beyond the far plane (away from the light)
        return False
    sun_plane_z = RENDER_FAR_CLIP * 2.0  # light-view z of shadow_near_clip relative to the eye
    if c[:, 2].min() > sun_plane_z:
        return False
    return True


def box(lo, hi, name):
    return {"lo": np.array(lo, float), "hi": np.array(hi, float), "name": name}


def room_scene(z0=22.0, outdoor=True):
    """10 x 10 m room, 3 m inside height, 0.2 m walls/floor/roof; optional outdoor scene."""
    o = []
    o.append(box((-5, -5, z0 - 0.2), (5, 5, z0), "floor"))
    o.append(box((-5.2, -5.2, z0 + 3.0), (5.2, 5.2, z0 + 3.2), "roof"))
    o.append(box((-5.2, -5.2, z0), (-5.0, 5.2, z0 + 3.0), "wall W"))
    o.append(box((5.0, -5.2, z0), (5.2, 5.2, z0 + 3.0), "wall E"))
    o.append(box((-5.0, -5.2, z0), (5.0, -5.0, z0 + 3.0), "wall S"))
    o.append(box((-5.0, 5.0, z0), (5.0, 5.2, z0 + 3.0), "wall N"))
    o.append(box((-1.0, -1.0, z0), (1.0, 1.0, z0 + 0.75), "table"))
    if outdoor:
        # terrain patches and a few buildings out to ~200 m east, i.e. behind wall E
        o.append(box((-128, -128, z0 - 2.0), (128, 128, z0 - 0.3), "terrain"))
        o.append(box((40, -20, z0 - 0.3), (60, 0, z0 + 15), "house 50 m E"))
        o.append(box((150, 30, z0 - 0.3), (190, 70, z0 + 30), "tower 170 m E"))
        o.append(box((-90, 60, z0 - 0.3), (-60, 90, z0 + 12), "house 100 m NW"))
    return o


def sun(elev_deg, az_deg):
    e, a = math.radians(elev_deg), math.radians(az_deg)
    return np.array([math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)])


def fmt(c):
    if c["empty"]:
        return "   (cleared: no receivers)"
    return (f"{c['mode']:6s} {c['width']:7.1f}x{c['height']:6.1f} m  texel {100 * c['texel']:6.2f} cm  "
            f"depth {c['depth']:7.1f} m  bias {100 * c['bias_m']:6.1f} cm")


def table_yaw(objects, cam_pos, pitch, sun_dir, title, steps=12):
    print(f"\n## {title}")
    print("yaw  near   far    splits (m)                         | cascade 0                                                   | modes")
    rows = []
    for k in range(steps):
        yaw = 360.0 * k / steps
        cam = Camera(cam_pos, yaw, pitch)
        r = fit(cam, objects, sun_dir)
        if r is None:
            print(f"{yaw:5.0f}  no receivers")
            continue
        c0 = r["cascades"][0]
        modes = "".join("-" if c["empty"] else c["mode"][0] for c in r["cascades"])
        print(f"{yaw:5.0f} {r['near']:5.2f} {r['far']:6.1f}  {', '.join(f'{x:6.1f}' for x in r['clip'])} | {fmt(c0)} | {modes}")
        rows.append((yaw, r))
    return rows


def roof_test(objects, sun_dir):
    print("\n## H1/H2: is the off-screen roof inside the caster-cull frustum of the cascades whose receivers it shades?")
    cam = Camera((-3.0, 0.0, 22.0 + 1.7), 0.0, -35.0)  # looking down at the floor, roof off-screen
    roof = [o for o in objects if o["name"] == "roof"][0]
    main_planes = planes_from_corners(cam.corners())
    print(f"roof in MAIN camera frustum: {aabb_in_planes(roof['lo'], roof['hi'], main_planes)}")
    r = fit(cam, objects, sun_dir)
    light_dir = -norm(sun_dir)
    for j, c in enumerate(r["cascades"]):
        if c["empty"]:
            print(f"cascade {j}: cleared")
            continue
        inside = cull_contains(c, light_dir, cam.origin, roof["lo"], roof["hi"])
        # roof depth relative to the cascade's near plane: > 0 means in front of near -> depth-clamped to 0
        corners = np.array([[x, y, z] for x in (roof['lo'][0], roof['hi'][0]) for y in (roof['lo'][1], roof['hi'][1]) for z in (roof['lo'][2], roof['hi'][2])])
        rz = xform(c["view"], corners)[:, 2]
        clamp = rz.max() > c["mx"][2]
        print(f"cascade {j} ({c['mode']}): roof inside cull frustum: {inside}; roof nearer the light than the near plane"
              f" (rasterised with GL_DEPTH_CLAMP at depth 0): {clamp}")


def swim_test(objects, sun_dir):
    print("\n## H3: sub-texel drift of the cascade-0 grid while walking 0..20 cm (no texel snapping)")
    print("step(cm)  ortho min x (m)   min x / texel (fractional part)  texel(cm)")
    for k in range(0, 21, 4):
        cam = Camera((-3.0 + k / 100.0, 0.0, 23.7), 20.0, -10.0)
        r = fit(cam, objects, sun_dir)
        c = r["cascades"][0]
        # light-space x of a fixed world point (0,0,z0): the texel grid origin moves with mn
        p = xform(c["view"], np.array([[0.0, 0.0, 22.0]]))[0]
        u = (p[0] - c["mn"][0]) / c["texel"]
        print(f"{k:6d}   {c['mn'][0]:12.4f}   world point at texel {u:10.3f} (frac {u % 1:5.3f})   {100 * c['texel']:6.2f}")


def rotate_test(objects, sun_dir):
    print("\n## H3: light-view 'up' follows the camera: rotation of the texel grid about the light axis")
    print("yaw   grid rotation about light axis (deg)")
    ref = None
    for yaw in (0, 10, 20, 45, 90, 180):
        cam = Camera((-3.0, 0.0, 23.7), yaw, -10.0)
        r = fit(cam, objects, sun_dir)
        c = r["cascades"][0]
        x_axis = c["view"][0, :3]
        if ref is None:
            ref = x_axis
        ang = math.degrees(math.atan2(np.cross(ref, x_axis) @ norm(-sun_dir), ref @ x_axis))
        print(f"{yaw:4d}  {ang:7.1f}")


def altitude_test():
    print("\n## bias: RenderShadowBias + RenderShadowBiasError*|z|/3000, times 2, in [0,1] depth units of each cascade")
    print("camera z   bias (depth units x2)   metres for a 20 m / 100 m / 300 m deep cascade")
    for z in (25, 500, 1000, 2000, 3000, 4000):
        b = 2 * abs(SHADOW_BIAS + SHADOW_BIAS_ERROR * z / 3000.0)
        print(f"{z:7d}   {b:8.4f}              {100 * b * 20:6.1f} cm / {100 * b * 100:6.1f} cm / {100 * b * 300:6.1f} cm")


def cascade_at(r, cam, p):
    """The cascade sampleDirectionalShadow picks for world point p (view-space depth vs splits)."""
    z = (cam.view()[:3, :3] @ p + cam.view()[:3, 3])[2]
    d = -z
    for j, c in enumerate(r["clip"]):
        if d < c:
            return j
    return None


def fixed_point_test(objects, cam_pos, z_floor, title, yaws=(0, 45, 90, 135, 180, 225, 270, 315)):
    """Resolution and bias at ONE fixed floor point (1.5 m from a wall) as the camera turns in place."""
    p = np.array([3.5, 1.0, z_floor])
    print(f"\n## {title}: floor point {tuple(float(x) for x in p)} seen from {cam_pos}, camera turning in place (pitch -20)")
    print("yaw  in view  cascade  mode    texel(cm)  bias(cm)  splits")
    for yaw in yaws:
        cam = Camera(cam_pos, yaw, -20.0)
        r = fit(cam, objects, sun(35.0, 200.0))
        in_view = aabb_in_planes(p - 0.01, p + 0.01, planes_from_corners(cam.corners()))
        j = cascade_at(r, cam, p)
        if j is None or r["cascades"][j]["empty"]:
            print(f"{yaw:4d}  {str(in_view):5s}    -")
            continue
        c = r["cascades"][j]
        print(f"{yaw:4d}  {str(in_view):5s}    {j}      {c['mode']:6s}  {100 * c['texel']:7.2f}   {100 * c['bias_m']:6.1f}   {', '.join(f'{x:.1f}' for x in r['clip'])}")


def walk_test(objects, z_floor, title):
    """Walking towards wall E (facing it): the fixed floor point's cascade and texel."""
    p = np.array([3.5, 1.0, z_floor])
    print(f"\n## {title}: walking towards wall E, facing it; floor point {tuple(float(x) for x in p)}")
    print("eye x  near   far    cascade  texel(cm)  bias(cm)")
    for x in (-4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0):
        cam = Camera((x, 0.0, z_floor + 1.7), 0.0, -20.0)
        r = fit(cam, objects, sun(35.0, 200.0))
        j = cascade_at(r, cam, p)
        if j is None or r["cascades"][j]["empty"]:
            print(f"{x:5.1f}  {r['near']:5.2f} {r['far']:6.1f}   -")
            continue
        c = r["cascades"][j]
        print(f"{x:5.1f}  {r['near']:5.2f} {r['far']:6.1f}   {j}        {100 * c['texel']:6.2f}   {100 * c['bias_m']:6.1f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()
    steps = 8 if args.quick else 12
    s = sun(35.0, 200.0)
    print("# Cascade-fit model: sun elevation 35 deg, azimuth 200 deg; maps 1920x1080 (scale 1)")
    ground = room_scene(22.0, outdoor=True)
    sky = room_scene(2500.0, outdoor=False)
    table_yaw(ground, (-3.0, 0.0, 23.7), -10.0, s, "Room on the ground with outdoor scene (camera inside, 1.7 m eye, pitch -10)", steps)
    table_yaw(sky, (-3.0, 0.0, 2501.7), -10.0, s, "Same room as a skybox at 2500 m (nothing outside)", steps)
    fixed_point_test(ground, (-3.0, 0.0, 23.7), 22.0, 'Ground room')
    fixed_point_test(sky, (-3.0, 0.0, 2501.7), 2500.0, 'Skybox room')
    walk_test(ground, 22.0, 'Ground room')
    walk_test(sky, 2500.0, 'Skybox room')
    outdoor = outdoor_scene(22.0)
    table_yaw(outdoor, (0.0, 0.0, 23.7), -10.0, s, "Open outdoor scene (terrain, trees, houses; avatar eye 1.7 m, pitch -10)", steps)
    pitch_test(outdoor, s)
    outdoor_point_test(outdoor, s)
    roof_test(ground, s)
    swim_test(ground, s)
    rotate_test(ground, s)
    altitude_test()
    return 0


def outdoor_scene(z0=22.0):
    """Open land: terrain patches (the viewer's terrain groups are ~16-32 m), trees, houses."""
    o = []
    for x in range(-128, 128, 32):
        for y in range(-128, 128, 32):
            o.append(box((x, y, z0 - 1.5), (x + 32, y + 32, z0 + 0.5), f"terrain {x},{y}"))
    for k, (x, y) in enumerate([(6, 3), (-9, 12), (15, -10), (30, 25), (-40, -20), (60, 5), (-70, 50), (100, -60), (120, 90)]):
        o.append(box((x - 0.4, y - 0.4, z0), (x + 0.4, y + 0.4, z0 + 4.0), f"trunk {k}"))
        o.append(box((x - 3.0, y - 3.0, z0 + 3.5), (x + 3.0, y + 3.0, z0 + 8.0), f"crown {k}"))
    o.append(box((20, -30, z0), (32, -18, z0 + 9), "house S"))
    o.append(box((-35, 40, z0), (-20, 55, z0 + 12), "house NW"))
    o.append(box((0.6, -0.3, z0), (1.2, 0.3, z0 + 1.8), "avatar"))
    return o


def pitch_test(objects, sun_dir):
    print("\n## Outdoors: looking down at the ground vs up at the horizon (yaw 30)")
    print("pitch  near   far    splits (m)                          cascade 0 | modes")
    for pitch in (-45, -30, -20, -10, 0, 10, 20):
        cam = Camera((0.0, 0.0, 23.7), 30.0, pitch)
        r = fit(cam, objects, sun_dir)
        if r is None:
            print(f"{pitch:5d}  no receivers")
            continue
        modes = "".join("-" if c["empty"] else c["mode"][0] for c in r["cascades"])
        print(f"{pitch:5d} {r['near']:5.2f} {r['far']:6.1f}  {', '.join(f'{x:6.1f}' for x in r['clip'])} | {fmt(r['cascades'][0])} | {modes}")


def outdoor_point_test(objects, sun_dir):
    """The avatar's own shadow on the ground (point 2 m from the avatar) as the camera orbits."""
    p = np.array([2.5, 1.0, 22.0])
    print(f"\n## Outdoors: the ground 2 m beside the avatar, camera orbiting at 3 m (third person)")
    print("orbit  cascade  mode    texel(cm)  bias(cm)  splits")
    for k in range(0, 360, 45):
        a = math.radians(k)
        eye = (1.0 - 3.0 * math.cos(a), -3.0 * math.sin(a), 24.0)
        cam = Camera(eye, k, -15.0)
        r = fit(cam, objects, sun_dir)
        j = cascade_at(r, cam, p)
        if j is None or r["cascades"][j]["empty"]:
            print(f"{k:5d}    -")
            continue
        c = r["cascades"][j]
        print(f"{k:5d}    {j}      {c['mode']:6s}  {100 * c['texel']:7.2f}   {100 * c['bias_m']:6.1f}   {', '.join(f'{x:.1f}' for x in r['clip'])}")


if __name__ == "__main__":
    sys.exit(main())
