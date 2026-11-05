#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline model of which projectors get the two projector shadow maps (stage S7).

Old (pipeline.cpp, Firestorm 7.2.5): every frame the targets are re-picked from the projectors
drawn this frame (their light box, half-size 1.5 x radius, inside the camera frustum), ranked by
LLVOVolume::updateSpotLightPriority: the screen-space pixel area (LLPipeline::calcPixelArea) of a
sphere of radius r placed at light + cone axis * r - camera at-axis * r, so the ranking depends on
the camera's orientation as well as its position. Two shadow slots fade towards the targets at
gFrameIntervalSeconds * max(camera speed in m/s, 1) per frame.

New (LLShadowProjectorSelect, llshadowprojectorselect.h): candidates are the same drawn projectors
(nothing is shadowed that cannot be seen), scored from the scene and the camera's position only
(the light's power: luminance x radius^2 x cone solid angle, over the squared distance from the
camera to the middle of the lit cone); a slot keeps its projector while it stays a candidate or for
a grace time after it stops being one, and is only taken over by a candidate scoring 1.5 x more;
fades take a fixed time, not the camera speed.

The owner's case: projectors around a room; the camera turns in place (and walks) while one
projector keeps lighting the floor in view. Reports how often the projector lighting the centre of
the view loses its shadow (old vs new), and slot changes per turn.
Exit status 0 when the new selection never drops a projector that lights a visible surface.
"""
from __future__ import annotations

import math
import sys

import numpy as np

FOV_Y = math.radians(60.0)
ASPECT = 16 / 9
PIXEL_ANGLE = 1080 / FOV_Y      # LLDrawable::sCurPixelAngle: pixels per radian


def norm(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


class Projector:
    def __init__(self, name, pos, target, radius, fov_deg, lum=1.0):
        self.name, self.pos, self.radius, self.fov, self.lum = name, np.array(pos, float), radius, math.radians(fov_deg), lum
        self.dir = norm(np.array(target, float) - self.pos)

    def footprint(self):
        """Centre of the lit area on the floor (z = 0) where the cone axis lands, within range."""
        if self.dir[2] >= -1e-3:
            return self.pos + self.dir * self.radius
        t = min(-self.pos[2] / self.dir[2], self.radius)
        return self.pos + self.dir * t


def camera(eye, yaw_deg, pitch_deg):
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    at = np.array([math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p)])
    left = norm(np.cross([0, 0, 1], at))
    up = np.cross(at, left)
    return {"eye": np.array(eye, float), "at": at, "left": left, "up": up}


def in_frustum(cam, p, radius=0.0):
    d = p - cam["eye"]
    z = d @ cam["at"]
    if z < -radius:
        return False
    th = math.tan(FOV_Y / 2)
    x, y = d @ cam["left"], d @ cam["up"]
    # sphere vs side planes (conservative)
    ny = math.sqrt(1 + th * th)
    nx = math.sqrt(1 + (th * ASPECT) ** 2)
    return abs(y) - z * th <= radius * ny and abs(x) - z * th * ASPECT <= radius * nx


def box_in_frustum(cam, centre, half):
    """AABBInFrustumNoFarClip, approximated by the box's bounding sphere."""
    return in_frustum(cam, centre, half * math.sqrt(3))


def old_priority(pr, cam):
    pos = pr.pos + pr.dir * pr.radius - cam["at"] * pr.radius
    dist = np.linalg.norm(pos - cam["eye"])
    if dist < 16:
        dist = (dist / 16) ** 2 * 16
    ang = math.atan(math.sqrt(3) * pr.radius / max(dist, 1e-6))
    r = ang * PIXEL_ANGLE
    return r * r * math.pi


def new_score(pr, cam):
    centre = pr.pos + pr.dir * (pr.radius * 0.5)
    d2 = max(float(np.sum((centre - cam["eye"]) ** 2)), 1.0)
    solid = 2 * math.pi * (1 - math.cos(pr.fov / 2))
    return pr.lum * pr.radius ** 2 * solid / d2


def candidates(projectors, cam):
    return [p for p in projectors if box_in_frustum(cam, p.pos, p.radius * 1.5)]


class OldSelector:
    def __init__(self):
        self.slots = [None, None]
        self.fade = [0.0, 0.0]

    def step(self, projectors, cam, dt, speed):
        cands = candidates(projectors, cam)
        targets = [None, None]
        pri = [0.0, 0.0]
        for p in cands:                      # setupSpotLight's insertion
            m = old_priority(p, cam); pot = p
            for i in range(2):
                if m > pri[i]:
                    targets[i], pot = pot, targets[i]
                    pri[i], m = m, pri[i]
        fade_amt = dt * max(speed, 1.0)
        for i in range(2):
            if self.slots[i] is not None and self.slots[i] in targets:
                self.fade[i] = min(self.fade[i] + fade_amt, 1.0)
            else:
                self.fade[i] = max(self.fade[i] - fade_amt, 0.0)
                if self.fade[i] == 0.0 or self.slots[i] is None:
                    other = self.slots[(i + 1) % 2]
                    self.slots[i] = targets[0] if targets[0] is not other else targets[1]
        return {p.name: f for p, f in zip(self.slots, self.fade) if p is not None}


class NewSelector:
    GRACE = 0.5      # seconds a projector keeps its slot after it stops being a candidate
    HYSTERESIS = 1.5
    FADE_TIME = 0.4  # seconds

    def __init__(self):
        self.slots = [None, None]
        self.fade = [0.0, 0.0]
        self.missing = [0.0, 0.0]

    def step(self, projectors, cam, dt, speed):
        cands = {p: new_score(p, cam) for p in candidates(projectors, cam)}
        for i in range(2):
            p = self.slots[i]
            if p is None:
                continue
            self.missing[i] = 0.0 if p in cands else self.missing[i] + dt
        keep = [self.slots[i] is not None and self.missing[i] <= self.GRACE for i in range(2)]
        # the desired pair: kept slots, unless a free candidate beats the weaker kept one by 1.5 x
        ranked = sorted((p for p in cands if p not in self.slots), key=lambda p: -cands[p])
        desired = [self.slots[i] if keep[i] else None for i in range(2)]
        for p in ranked:
            free = [i for i in range(2) if desired[i] is None]
            if free:
                desired[free[0]] = p
                continue
            weak = min(range(2), key=lambda i: cands.get(desired[i], 0.0))
            if cands[p] > self.HYSTERESIS * cands.get(desired[weak], 0.0):
                desired[weak] = p
            break
        step = dt / self.FADE_TIME
        for i in range(2):
            if self.slots[i] is not None and self.slots[i] is desired[i]:
                self.fade[i] = min(self.fade[i] + step, 1.0)
            else:
                self.fade[i] = max(self.fade[i] - step, 0.0)
                if self.fade[i] == 0.0:
                    self.slots[i] = desired[i]
                    self.missing[i] = 0.0
        return {p.name: f for p, f in zip(self.slots, self.fade) if p is not None}


def lights_visible_floor(pr, cam):
    """Does the projector light a floor point inside the view? (the footprint, or any point of
    the lit disc around it)"""
    f = pr.footprint()
    reach = math.tan(pr.fov / 2) * np.linalg.norm(f - pr.pos)
    return in_frustum(cam, f, reach * 0.5)


def scenario(sel_cls, projectors, path, dt=1 / 60, warmup=0):
    """Drop events: a projector whose shadow was full loses it while it still lights a visible
    floor and no candidate scoring 1.5 x more needed its slot (a legitimate takeover)."""
    sel = sel_cls()
    drops = 0; changes = 0; prev = None; full = set(); frames_without = 0; frames = 0
    for k, (cam, speed) in enumerate(path):
        shadowed = sel.step(projectors, cam, dt, speed)
        if k < warmup:
            full = {n for n, f in shadowed.items() if f > 0.99}
            prev = tuple(sorted(full))
            continue
        frames += 1
        now = {n for n, f in shadowed.items() if f > 0.99}
        names = tuple(sorted(now))
        if names != prev:
            changes += 1
        prev = names
        lit = {p.name: p for p in projectors if lights_visible_floor(p, cam)}
        for n in full - now:
            if n in lit:
                p = lit[n]
                stronger = [q for q in lit.values() if q.name in now and new_score(q, cam) > 1.5 * new_score(p, cam)]
                if not stronger:
                    drops += 1
        # frames where the strongest projector lighting the view has no full shadow
        if lit:
            best = max(lit.values(), key=lambda q: new_score(q, cam))
            if best.name not in now:
                frames_without += 1
        full = now
    return drops, changes, frames_without, frames


def room():
    return [
        Projector("ceiling A", (2.0, 0.0, 3.0), (2.0, 0.0, 0.0), 6.0, 60),
        Projector("ceiling B", (-2.0, 1.5, 3.0), (-2.0, 1.5, 0.0), 6.0, 60),
        Projector("wall wash", (0.0, -4.5, 2.5), (0.0, -1.0, 0.0), 8.0, 90),
        Projector("big flood", (6.0, 6.0, 5.0), (0.0, 0.0, 0.0), 14.0, 70, lum=1.5),
        Projector("desk lamp", (-3.5, -3.0, 1.0), (-3.5, -3.0, 0.0), 2.5, 50, lum=0.6),
    ]


def turn_path(eye, seconds=6.0, dt=1 / 60, deg_per_s=60.0, pitch=-20.0):
    t = 0.0
    while t < seconds:
        yield camera(eye, t * deg_per_s, pitch), 0.0
        t += dt


def walk_path(seconds=6.0, dt=1 / 60, speed=2.5):
    t = 0.0
    while t < seconds:
        x = -4.0 + speed * t
        yield camera((x, 0.0, 1.7), 10.0 * math.sin(t), -20.0), speed
        t += dt


def warm(path, seconds=2.0, dt=1 / 60):
    """Let the selection settle on the first view before measuring."""
    first = next(iter(path))
    return [first] * int(seconds / dt)


def main():
    projectors = room()
    failures = []
    print("# Projector shadow selection, 5 projectors in a room, 2 shadow slots (after a 2 s settle)")
    cases = (("turning in place 60 deg/s", list(turn_path((0.0, 0.0, 1.7))), projectors),
             ("turning slowly 20 deg/s", list(turn_path((0.0, 0.0, 1.7), seconds=18, deg_per_s=20)), projectors),
             ("walking across the room 2.5 m/s", list(walk_path()), projectors),
             ("two projectors, slow turn", list(turn_path((0.0, -1.0, 1.7), seconds=12, deg_per_s=30)), [projectors[0], projectors[3]]))
    for label, path, prj in cases:
        w = warm(path)
        for name, cls in (("old", OldSelector), ("new", NewSelector)):
            drops, changes, without, frames = scenario(cls, prj, w + path, warmup=len(w))
            print(f"{label:34s} {name}: drop events {drops:3d}; slot changes {changes:2d}; "
                  f"strongest visible projector unshadowed in {100 * without / max(frames, 1):5.1f} % of frames")
            if name == "new" and drops:
                failures.append(f"{label}: {drops} drop events")
    print("all checks passed" if not failures else "FAILED: " + "; ".join(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
