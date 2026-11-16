# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Camera-orbit gates of the projector shadow pool over LEAP (development viewer, -DVKS_GL_STATS=ON).

SKELETON (stage S0 of doc/shadows/projector-pool-budget.md, spec section 7.3). It reads the LEAP API "ProjectorShadows"
(operation "stats", indra/newview/llprojectorshadowstatsapi.cpp). Before the pool exists (stages P2 and later) the API
reports "pool": {"pool_present": false, ...}; a gate then ends as NOT RUN, never as PASS.

  O-1 pure rotation   camera position fixed; yaw 360 deg and pitch -60..+60 deg; per step the pool's static renders, the
                      number of lights without a tile and the CRC32 of every static tile must not change.  (implemented; NOT RUN
                      with no projector tile in the baseline, or with no tile CRC: GL < 4.5 or "crc" off)
  O-2 orbit           24 steps around a centre (o2.centre: "agent" or [x, y, z] in region coordinates) at 3 m and 8 m, two laps each.
                      The first lap of each radius settles (it may re-render from the LOD pin, lod_up, and from a mesh or prim
                      that arrives late, edit: o2.first_lap_causes); the second changes nothing: 0 renders, no cause, the CRCs
                      of that radius's first lap. Tiles with an avatar in their light are left out of the comparison (they are
                      redrawn every frame until P4) and reported (avatar_tiles). Limit: at 8 m an object still loading into
                      view during the second lap fails the gate; run it again once the scene has loaded. Each step can save a
                      snapshot (o2.snapshots) and the last lap a contact sheet to look at.                        (stage P3)
  O-4 never vanish    walk a route, teleport in: lights without a tile is 0 after the first fill.           (stage P2)
  O-6 movers          three watch windows while you act in-world (o6.run): a fan with a spinning child under a lamp (no render),
                      a rotating spot (a dynamic render every frame; once it stops no tile is behind its lamp's pose: the final
                      pose is drawn as a dynamic item, so the static counter is not asked to rise), an object edited under a
                      second lamp (its tile waits at most 250 ms).                                              (stage P3)

The camera is moved with LLAgent.setCameraParams as light_check.py does; settings are restored at the end and on error.
stdout is the LEAP channel: this script only logs to stderr.

Modes: (default) run | --dry-run (read-only: the API and its reply shape) | --noop | --restore RUNDIR.
Exit codes: 0 every gate that ran passed (and at least one ran), 1 error, 2 stopped by a STOP file, 3 the pipe closed,
4 a gate failed, 5 protocol error, 6 nothing could run (no pool in this viewer, or no projector tile in the scene).
"""

import argparse
import datetime as dt
import json
import math
import os
import pathlib
import sys
import traceback

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from leap_client import ConnectionLost, LeapError, LeapSession, ProtocolError, ViewerGone  # noqa: E402
from light_check import Aborted, Viewer, default_capture_root, log, write_json_atomic  # noqa: E402

API = "ProjectorShadows"
POOL_KEYS = ["pool_present", "level", "static_renders", "dynamic_renders", "lights_without_tile", "tiles", "rerender_causes",
             "pinned_objects", "frame", "pending_tiles", "dirty_age_ms_max"]
SLOTS_KEYS = ["occupied", "targeted"]
LIGHTS_KEYS = ["nearby", "spotlights", "eligible", "reach_m", "eligible_within_16m", "eligible_within_32m",
               "eligible_within_64m", "eligible_within_reach"]
SETTINGS = ["RenderShadowMethod", "RenderShadowDetail"]


def pool_state(stats):
    """The comparable part of one stats reply: (static renders, lights without a tile, {tile key: crc})."""
    pool = stats.get("pool") or {}
    crcs = {str(t.get("key")): t.get("crc32") for t in (pool.get("tiles") or [])}
    return int(pool.get("static_renders", 0)), int(pool.get("lights_without_tile", 0)), crcs


def dry_run_rows(st):
    """The api, slots and lights rows of the dry run: each reply part is checked for its keys, like the pool's."""
    rows = []
    for name, part, keys in (("api", st.get("pool") or {}, POOL_KEYS), ("slots", st.get("slots") or {}, SLOTS_KEYS),
                             ("lights", st.get("lights") or {}, LIGHTS_KEYS)):
        missing = [k for k in keys if k not in part]
        if name == "api":
            detail = ("pool keys missing: %s" % missing if missing
                      else "ProjectorShadows.stats ok, pool_present=%s" % part.get("pool_present"))
        else:
            detail = "keys missing: %s" % missing if missing else json.dumps(part, sort_keys=True)
        rows.append((name, "FAIL" if missing else "PASS", detail))
    return rows


def o1_steps(cfg):
    """(yaw, pitch) pairs of the pure-rotation gate: a full yaw turn at pitch 0, then every other pitch at yaw 0."""
    step = float(cfg.get("yaw_step_deg", 15))
    yaws = [i * step for i in range(int(round(360.0 / step)))]
    pitches = [float(p) for p in cfg.get("pitch_deg", [])]
    return [(y, 0.0) for y in yaws] + [(0.0, p) for p in pitches if p != 0.0]


def judge_o1(baseline, states):
    """Failures of O-1: any change of the static renders or of a tile CRC against the first step, any light without a tile."""
    base_renders, base_without, base_crcs = baseline
    fails = []
    if base_without:
        fails.append("step 0: %d lights without a tile" % base_without)
    for i, (renders, without, crcs) in enumerate(states, 1):
        if renders != base_renders:
            fails.append("step %d: static renders %d -> %d" % (i, base_renders, renders))
        if without:
            fails.append("step %d: %d lights without a tile" % (i, without))
        if crcs != base_crcs:
            changed = sorted(k for k in set(crcs) | set(base_crcs) if crcs.get(k) != base_crcs.get(k))
            fails.append("step %d: tile CRC changed for %s" % (i, ", ".join(changed[:8])))
    return fails


def pool_extras(stats):
    """The P3 counters of one stats reply: causes, dynamic renders, pinned objects, the frame, and each tile's drawn frame."""
    pool = stats.get("pool") or {}
    return {
        "causes": {str(k): int(v) for k, v in (pool.get("rerender_causes") or {}).items()},
        "dynamic": int(pool.get("dynamic_renders", 0)),
        "static": int(pool.get("static_renders", 0)),
        "pinned": int(pool.get("pinned_objects", 0)),
        "frame": int(pool.get("frame", 0)),
        "pending": int(pool.get("pending_tiles", 0)),
        "without": int(pool.get("lights_without_tile", 0)),
        "drawn": {str(t.get("key")): int(t.get("drawn_frame", 0)) for t in (pool.get("tiles") or [])},
        "dirty_age_ms_max": float(pool.get("dirty_age_ms_max", 0.0)),
        "pose_stale": sum(1 for t in (pool.get("tiles") or []) if t.get("pose_stale")),
        "lamp_moving": sum(1 for t in (pool.get("tiles") or []) if t.get("lamp_moving")),
        "avatar_keys": sorted(str(t.get("key")) for t in (pool.get("tiles") or []) if t.get("avatar")),
    }


def orbit_points(centre, radius, steps, height_m, target_height_m=0.0):
    """(camera, focus) for each step of a circle of radius_m around centre (region coordinates), the camera height_m above
    centre z, looking at the centre (target_height_m above it). The first point is east of the centre."""
    pts = []
    for i in range(int(steps)):
        a = 2.0 * math.pi * i / steps
        cam = [centre[0] + radius * math.cos(a), centre[1] + radius * math.sin(a), centre[2] + height_m]
        pts.append((cam, [centre[0], centre[1], centre[2] + target_height_m]))
    return pts


def cause_delta(prev, cur):
    """{cause: count change} between two cause maps, only the causes that changed."""
    return {k: cur.get(k, 0) - prev.get(k, 0) for k in set(cur) | set(prev) if cur.get(k, 0) != prev.get(k, 0)}


def judge_o2(baseline, samples, first_lap_causes=("lod_up",)):
    """Failures of O-2. baseline: the sample before the orbit; samples: dicts with lap (1-based, over the whole run), tag, renders,
    without, crcs, causes, and optionally settle (the first lap of a radius; default: lap 1), group (the radius' index, default 0)
    and avatar_keys (tiles with an avatar in their light). A settling lap may re-render only from the causes in first_lap_causes
    (the LOD pin lifting an object once, a late mesh); every other lap must change nothing: no render, no cause, no CRC against the
    last step of its radius's settling lap. Tiles that held an avatar in any sample are left out of the CRC comparison (the avatars
    are drawn into the tiles every frame until P4), and the cause "avatar" and as many renders are tolerated. A light without a
    tile fails at any step."""
    fails = []
    skip = set(str(k) for k in baseline.get("avatar_keys", []))
    for s in samples:
        skip |= set(str(k) for k in s.get("avatar_keys", []))
    prev = baseline
    refs = {}
    for s in samples:
        if s.get("settle", s["lap"] == 1):
            refs[s.get("group", 0)] = s["crcs"]   # the last step of the settling lap
    for s in samples:
        tag = "lap %d %s" % (s["lap"], s["tag"])
        if s["without"]:
            fails.append("%s: %d lights without a tile" % (tag, s["without"]))
        delta = cause_delta(prev["causes"], s["causes"])
        avatar_renders = max(0, delta.pop("avatar", 0))
        if s.get("settle", s["lap"] == 1):
            bad = {k: v for k, v in delta.items() if k not in first_lap_causes}
            if bad:
                fails.append("%s: re-render for %s (only %s allowed in a settling lap)"
                             % (tag, dict(sorted(bad.items())), ", ".join(first_lap_causes)))
        else:
            if s["renders"] - prev["renders"] > avatar_renders:
                fails.append("%s: static renders %d -> %d" % (tag, prev["renders"], s["renders"]))
            if delta:
                fails.append("%s: render causes changed: %s" % (tag, dict(sorted(delta.items()))))
            ref = refs.get(s.get("group", 0))
            if ref is not None:
                changed = sorted(k for k in set(s["crcs"]) | set(ref) if k not in skip and s["crcs"].get(k) != ref.get(k))
                if changed:
                    fails.append("%s: tile CRC differs from the first lap for %s" % (tag, ", ".join(changed[:8])))
        prev = s
    return fails


def judge_o6_fan(before, after):
    """The fan and its spinning child under a lamp (a client spin: a known limit): no static render, no dynamic render, no cause."""
    fails = []
    if after["static"] != before["static"]:
        fails.append("static renders %d -> %d while the fan spun" % (before["static"], after["static"]))
    if after["dynamic"] != before["dynamic"]:
        fails.append("dynamic renders %d -> %d while the fan spun" % (before["dynamic"], after["dynamic"]))
    delta = cause_delta(before["causes"], after["causes"])
    if delta:
        fails.append("render causes changed: %s" % dict(sorted(delta.items())))
    return fails


def judge_o6_spot(before, during, after, min_fraction=0.5):
    """A rotating spot: while it turns, its tile is a dynamic item (drawn in at least min_fraction of the frames), no light is
    without a tile, and once it stands still no tile is behind its lamp's pose and none reads as moving. The final pose is drawn as
    a dynamic item (the lamp was moving in the frame it arrived), so the static counter is not asked to rise."""
    fails = []
    frames = during["frame"] - before["frame"]
    if frames <= 0:
        return ["the frame counter did not advance (%d -> %d)" % (before["frame"], during["frame"])]
    dyn = during["dynamic"] - before["dynamic"]
    if dyn < min_fraction * frames:
        fails.append("dynamic renders %d in %d frames: the rotating spot is not redrawn every frame" % (dyn, frames))
    if before["without"] or during["without"] or after["without"]:
        fails.append("a light was without a tile")
    if after["frame"] <= during["frame"]:
        fails.append("the frame counter did not advance after the spot stopped (%d -> %d)" % (during["frame"], after["frame"]))
    if after.get("pose_stale", 0):
        fails.append("%d tile(s) still behind their lamp's pose after the spot stopped" % after["pose_stale"])
    if after.get("lamp_moving", 0):
        fails.append("%d lamp(s) still read as moving after the spot stopped: stop it" % after["lamp_moving"])
    return fails


def judge_o6_lamp(before, after, limit_ms=250.0):
    """An object edited under a second lamp: the renders the change caused (the static renders of the window that were not first fills,
    not a lamp or an avatar) were drawn within limit_ms of the change reaching the pool (the pool reports the longest wait since the
    last reset). Returns (status, why). NOT RUN when no change was seen: edit an object under a lamp while the window runs."""
    changed = sum(max(0, after["causes"].get(k, 0) - before["causes"].get(k, 0)) for k in ("add_remove", "edit", "mover", "lod_up"))
    if not changed:
        return "NOT RUN", "no tile was drawn again for a changed object in the window: edit an object under a lamp while it runs"
    wait = after.get("dirty_age_ms_max", 0.0)
    if wait > limit_ms:
        return "FAIL", "a changed tile waited %.0f ms (limit %.0f ms), %d renders" % (wait, limit_ms, changed)
    return "PASS", "%d renders for changes, the longest wait %.0f ms" % (changed, wait)


class Check:
    def __init__(self, viewer, scenario, outdir):
        self.v, self.sc, self.out = viewer, scenario, pathlib.Path(outdir)
        self.orig = {}
        self.camera_set = False
        self.cleaned = False
        self.report = {"scenario": scenario.get("name"), "gates": {}, "warnings": []}

    def save_restore(self):
        write_json_atomic(self.out / "restore.json", {"settings": self.orig, "camera_set": self.camera_set})

    def change(self, key, value):
        if key not in self.orig:
            cur = self.v.try_control(key)
            if cur is None:
                self.report["warnings"].append("control %s does not exist in this viewer" % key)
                return
            self.orig[key] = cur.get("value")
            self.save_restore()
        self.v.set_control(key, value)

    def cleanup(self):
        if self.cleaned:
            return
        self.cleaned = True
        write_json_atomic(self.out / "orbit.json", self.report)
        if self.camera_set:
            try:
                self.v.release_camera()
            except (LeapError, ConnectionLost) as exc:
                log("camera not released: %s" % exc)
        for key, val in self.orig.items():
            try:
                self.v.set_control(key, val)
            except (LeapError, ConnectionLost) as exc:
                log("could not restore %s=%r: %s" % (key, val, exc))

    def stats(self):
        return self.v.req(API, "stats")

    def look(self, cam, yaw, pitch):
        """Point the camera, its position fixed at cam, along yaw / pitch (degrees)."""
        a, p = math.radians(yaw), math.radians(pitch)
        d = float(self.sc["camera"]["look_distance_m"])
        focus = [cam[0] + d * math.cos(p) * math.cos(a), cam[1] - d * math.cos(p) * math.sin(a), cam[2] + d * math.sin(p)]
        self.aim(cam, focus)

    def warm_up(self):
        """Before the baseline: let the first fill finish. Waits after_camera_s, then polls until static_renders and
        lights_without_tile are unchanged and no tile is pending for 3 reads (at most 15 s), so step 0 is not read mid-fill or
        with a late mesh still waiting to be drawn."""
        self.v.sleep(float(self.sc["settle"].get("after_camera_s", 0.5)))
        last, same = None, 0
        for _ in range(30):
            self.v.check_abort()
            reply = self.stats()
            st = pool_state(reply)[:2] + (pool_extras(reply)["pending"],)
            same = same + 1 if st == last and not st[2] else 0
            if same >= 3:
                return
            last = st
            self.v.sleep(0.5)
        log("warm-up: pool counters still changing after 15 s; the baseline may be taken mid-fill")

    def gate_o1(self):
        first = self.stats()
        if not (first.get("pool") or {}).get("pool_present"):
            return {"status": "NOT RUN", "why": "this viewer has no projector shadow pool (pool_present is false)"}
        pos = self.v.agent_pos()
        cam = [pos[0], pos[1], pos[2] + float(self.sc["camera"]["height_m"])]
        states = []
        for n, (yaw, pitch) in enumerate(o1_steps(self.sc.get("o1", {}))):
            self.v.check_abort()
            self.look(cam, yaw, pitch)
            if n == 0:
                self.warm_up()
            self.v.sleep(float(self.sc["settle"].get("step_s", 0.3)))
            states.append(pool_state(self.stats()))
            if n == 0 and not states[0][2]:
                # a rotation test over an empty pool proves nothing: not a pass
                return {"status": "NOT RUN", "why": "no projector tile in the scene (the baseline has no tiles); "
                                                    "go where a projector is in reach"}
            if n == 0 and all(c is None for c in states[0][2].values()):
                # no CRC to compare (the API leaves crc32 out with "crc": false or for an empty or undrawn tile): not a pass either
                return {"status": "NOT RUN", "why": "no tile CRC in the reply (crc off or no drawn tile); "
                                                    "the rotation gate cannot compare tile contents"}
        fails = judge_o1(states[0], states[1:])
        return {"status": "FAIL" if fails else "PASS", "steps": len(states), "failures": fails[:50]}

    def aim(self, cam, focus):
        """Put the camera at cam looking at focus (region coordinates), locked, with no lag."""
        self.v.fire("LLAgent", "setCameraParams", camera_pos=cam, focus_pos=focus, camera_locked=True, focus_locked=True,
                    camera_lag=0.0, focus_lag=0.0, camera_threshold=0.0, focus_threshold=0.0)
        self.v.barrier()
        self.camera_set = True
        self.save_restore()

    def sample(self, tag, lap):
        """One comparable reading of the pool: static renders, lights without a tile, tile CRCs, the render causes."""
        st = self.stats()
        renders, without, crcs = pool_state(st)
        ex = pool_extras(st)
        return {"lap": lap, "tag": tag, "renders": renders, "without": without, "crcs": crcs, "causes": ex["causes"],
                "avatar_keys": ex["avatar_keys"]}

    def extras(self, reset_ages=False):
        """The P3 counters without the tile read-back (cheap): the O-6 windows poll these."""
        args = {"crc": False}
        if reset_ages:
            args["reset_ages"] = True
        return pool_extras(self.v.req(API, "stats", **args))

    def contact_sheet(self, files, out):
        """A grid of the last lap's snapshots to look at (Pillow, when installed): the same lamp shadows at every step is the pass.
        Each picture keeps its aspect, fitted in a 320 x 180 cell."""
        try:
            from PIL import Image  # noqa: PLC0415
        except ImportError:
            self.report["warnings"].append("Pillow not installed: no contact sheet, the snapshots stay .bmp")
            return None
        cw, ch = 320, 180
        thumbs = []
        for f in files:
            with Image.open(f) as im:
                t = im.convert("RGB")
                t.thumbnail((cw, ch))
                thumbs.append(t)
        if not thumbs:
            return None
        cols = 6
        rows = (len(thumbs) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cw, rows * ch))
        for i, t in enumerate(thumbs):
            sheet.paste(t, ((i % cols) * cw + (cw - t.width) // 2, (i // cols) * ch + (ch - t.height) // 2))
        sheet.save(out)
        return str(out)

    def gate_o2(self):
        cfg = self.sc.get("o2") or {}
        first = self.stats()
        if not (first.get("pool") or {}).get("pool_present"):
            return {"status": "NOT RUN", "why": "this viewer has no projector shadow pool (pool_present is false)"}
        centre_cfg = cfg.get("centre", "agent")
        if isinstance(centre_cfg, (list, tuple)) and len(centre_cfg) == 3:
            centre = [float(c) for c in centre_cfg]
        else:
            pos = self.v.agent_pos()
            centre = [pos[0], pos[1], pos[2]]
        height = float(self.sc["camera"]["height_m"])
        radii = [float(r) for r in cfg.get("radii_m", [3.0, 8.0])]
        steps = int(cfg.get("steps", 24))
        laps = int(cfg.get("laps", 2))
        if laps < 2:
            return {"status": "NOT RUN", "why": "o2.laps must be at least 2: the second lap is the gate"}
        sheet_dir = self.out / "o2"
        take_snaps = bool(cfg.get("snapshots", False))
        if take_snaps:
            sheet_dir.mkdir(parents=True, exist_ok=True)
        pts0 = orbit_points(centre, radii[0], steps, height, float(cfg.get("target_height_m", 0.5)))
        self.aim(*pts0[0])
        self.warm_up()
        baseline = self.sample("baseline", 0)
        if not baseline["crcs"]:
            return {"status": "NOT RUN", "why": "no projector tile in the scene (the baseline has no tiles); go where a projector is in reach"}
        if all(c is None for c in baseline["crcs"].values()):
            return {"status": "NOT RUN", "why": "no tile CRC in the reply (GL < 4.5 or crc off); the orbit gate cannot compare tile contents"}
        samples, lap_no, last_lap_files = [], 0, []
        for group, radius in enumerate(radii):
            pts = orbit_points(centre, radius, steps, height, float(cfg.get("target_height_m", 0.5)))
            for lap in range(laps):
                lap_no += 1
                files = []
                for i, (cam, focus) in enumerate(pts):
                    self.v.check_abort()
                    self.aim(cam, focus)
                    self.v.sleep(float(self.sc["settle"].get("step_s", 0.3)))
                    smp = self.sample("r%gm s%02d" % (radius, i), lap_no)
                    smp["settle"], smp["group"] = lap == 0, group   # the first lap of each radius settles; the others are the gate
                    samples.append(smp)
                    if take_snaps:
                        name = sheet_dir / ("lap%d_r%g_s%02d.bmp" % (lap_no, radius, i))
                        self.v.snapshot(str(name))
                        files.append(name)
                last_lap_files = files or last_lap_files
        fails = judge_o2(baseline, samples, tuple(cfg.get("first_lap_causes", ["lod_up", "edit"])))
        avatar_keys = sorted(set(str(k) for smp in samples + [baseline] for k in smp.get("avatar_keys", [])))
        out = {"status": "FAIL" if fails else "PASS", "steps": len(samples), "failures": fails[:50],
               "avatar_tiles": avatar_keys,   # left out of the comparison: an avatar in the light redraws the tile every frame until P4
               "causes": cause_delta(baseline["causes"], samples[-1]["causes"]),
               "first_lap_renders": samples[steps - 1]["renders"] - baseline["renders"] if len(samples) >= steps else None}
        if last_lap_files:
            out["contact_sheet"] = self.contact_sheet(last_lap_files, sheet_dir / "lap_last.png")
        return out

    def o6_window(self, what, seconds, prompt_s):
        """Tell the operator what to do, give them prompt_s to do it, then return the readings at the window's start."""
        log("ORBIT O-6: %s (window %.0f s, starts in %.0f s)" % (what, seconds, prompt_s))
        self.v.sleep(prompt_s)
        return self.extras()

    def gate_o6(self):
        cfg = self.sc.get("o6") or {}
        if not cfg.get("run"):
            return {"status": "NOT RUN", "why": "O-6 needs scripted objects and you in-world: set o6.run in the scenario"}
        first = self.stats()
        if not (first.get("pool") or {}).get("pool_present"):
            return {"status": "NOT RUN", "why": "this viewer has no projector shadow pool (pool_present is false)"}
        win = cfg.get("windows") or {}
        prompt = float(win.get("prompt_s", 10))
        parts = {}
        # 1. the fan: a spinning child in a still linkset casts frozen and never invalidates
        t = float(win.get("fan_s", 10))
        before = self.o6_window("stand still, the fan with the spinning child turns under a lamp", t, prompt)
        self.v.sleep(t)
        fails = judge_o6_fan(before, self.extras())
        parts["fan"] = {"status": "FAIL" if fails else "PASS", "failures": fails}
        # 2. the rotating spot: a moving lamp is a dynamic item every frame; the tile catches up when it stops
        t = float(win.get("spot_s", 12))
        stop_s = float(win.get("spot_stop_s", 5))
        before = self.o6_window("the rotating spot turns, then stop it after the window", t, prompt)
        self.v.sleep(t)
        during = self.extras()
        log("ORBIT O-6: stop the rotating spot now (%.0f s)" % stop_s)
        self.v.sleep(stop_s)
        fails = judge_o6_spot(before, during, self.extras())
        parts["spot"] = {"status": "FAIL" if fails else "PASS", "failures": fails}
        # 3. an object edited under a second lamp: its tile is drawn again within 250 ms
        t = float(win.get("lamp_edit_s", 20))
        log("ORBIT O-6: edit an object under a lamp during the next %.0f s (starts in %.0f s)" % (t, prompt))
        self.v.sleep(prompt)
        before = self.extras(reset_ages=True)
        self.v.sleep(t)
        status, why = judge_o6_lamp(before, self.extras(), float(win.get("limit_ms", 250)))
        parts["lamp_edit"] = {"status": status, "why": why}
        statuses = [p["status"] for p in parts.values()]
        if "FAIL" in statuses:
            status = "FAIL"
        elif all(s == "PASS" for s in statuses):
            status = "PASS"
        else:
            status = "NOT RUN"
        out = {"status": status, "parts": parts}
        if status == "NOT RUN":
            out["why"] = "parts not run: " + ", ".join(k for k, p in parts.items() if p["status"] == "NOT RUN")
        return out

    @staticmethod
    def not_yet(name, stage):
        return {"status": "NOT RUN", "why": "%s is implemented in %s" % (name, stage)}

    def teleport(self):
        t = self.sc.get("teleport")
        if t:
            # LLAgent.requestTeleport opens the modal "TeleportViaSLAPP" confirmation, which stalls a scripted run: the
            # LLTeleportHandler "teleport" operation (as perf_check.py uses) does not
            self.v.fire("LLTeleportHandler", "teleport", regionname=t["region"], x=t.get("x", 128), y=t.get("y", 128), z=t.get("z", 30))
            self.v.barrier()
            self.v.sleep(float(self.sc["settle"]["after_teleport_s"]))

    def go(self):
        self.save_restore()
        for k, val in self.sc.get("settings", {}).items():
            self.change(k, val)
        self.teleport()
        gates = self.report["gates"]
        gates["O-1"] = self.gate_o1()
        gates["O-2"] = self.gate_o2()
        gates["O-4"] = self.not_yet("O-4", "stage P2")
        gates["O-6"] = self.gate_o6()
        for name, g in gates.items():
            log("ORBIT %s %s %s" % (g["status"], name, g.get("why") or "; ".join(g.get("failures", [])[:3]) or "; ".join(
                "%s %s" % (k, p["status"]) for k, p in (g.get("parts") or {}).items())))
        self.cleanup()
        ran = [g["status"] for g in gates.values() if g["status"] != "NOT RUN"]
        if "FAIL" in ran:
            return 4
        return 0 if ran else 6


def dry_run(v, outdir):
    rows = []
    try:
        st = v.req(API, "stats")
        rows.extend(dry_run_rows(st))
    except LeapError as exc:
        rows.append(("api", "FAIL", "the viewer has no ProjectorShadows API (not a VKS_GL_STATS build?): %s" % exc))
    for key in SETTINGS:
        cur = v.try_control(key)
        rows.append(("setting " + key, "PASS" if cur is not None else "FAIL", "" if cur is None else repr(cur.get("value"))))
    ok = all(r[1] == "PASS" for r in rows)
    for name, status, detail in rows:
        log("ORBIT %s %s %s" % (status, name, detail))
    write_json_atomic(pathlib.Path(outdir) / "dry_run.json", [dict(zip(("check", "status", "detail"), r)) for r in rows])
    return 0 if ok else 1


def restore_run(v, args):
    data = json.loads((pathlib.Path(args.restore) / "restore.json").read_text(encoding="utf-8"))
    for key, val in (data.get("settings") or {}).items():
        try:
            v.set_control(key, val)
            log("restored %s = %r" % (key, val))
        except LeapError as exc:
            log("could not restore %s: %s" % (key, exc))
    if data.get("camera_set"):
        v.release_camera()
    return 0


def parse_args(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scenario", default=str(HERE / "scenarios" / "projector_orbit.json"))
    ap.add_argument("--out", default=None, help="run folder (default: .captures/<date-time>)")
    ap.add_argument("--dry-run", action="store_true", help="read-only check of the API and its reply shape")
    ap.add_argument("--noop", action="store_true", help="take the LEAP handshake and exit 0 (the second, throw-away --leap entry)")
    ap.add_argument("--no-login-wait", action="store_true")
    ap.add_argument("--restore", default=None, metavar="RUNDIR")
    ap.add_argument("--max-wait-s", type=float, default=None)
    return ap.parse_args(argv)


def execute(session, args, sc, outdir):
    outdir = pathlib.Path(outdir).resolve()
    if not args.restore:
        outdir.mkdir(parents=True, exist_ok=True)
    v = Viewer(session, abort_file=None if args.restore else str(outdir / "STOP"))
    code, run = 0, None
    try:
        session.ping()
        if args.restore:
            return restore_run(v, args)
        if not args.no_login_wait:
            log("waiting for login (log in by hand)")
            v.wait_for_login(args.max_wait_s or sc["settle"]["login_timeout_s"])
            v.sleep(sc["settle"]["post_login_s"])
        if args.dry_run:
            return dry_run(v, outdir)
        run = Check(v, sc, outdir)
        code = run.go()
    except Aborted as exc:
        log("aborted: %s" % exc)
        code = 2
        if run:
            run.cleanup()
    except KeyboardInterrupt:
        code = 2
        if run:
            run.cleanup()
    except ViewerGone as exc:
        log("the pipe from the viewer closed: %s" % exc)
        code = 3
    except ProtocolError as exc:
        log("PROTOCOL ERROR: %s" % exc)
        code = 5
    except Exception as exc:  # noqa: BLE001
        log("FAILED: %s" % exc)
        code = 1
        if run:
            run.cleanup()
    return code


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    sc = None if args.noop else json.loads(pathlib.Path(args.scenario).read_text(encoding="utf-8"))
    outdir = pathlib.Path(args.out) if args.out else default_capture_root() / ("orbit-" + dt.datetime.now().strftime("%Y%m%d-%H%M%S"))
    session = LeapSession(sys.stdin.buffer, sys.stdout.buffer, log=log).start()
    if args.noop:
        return 0
    return execute(session, args, sc, outdir)


if __name__ == "__main__":
    code = 1
    try:
        code = main()
    except BaseException:  # noqa: BLE001
        traceback.print_exc()
    finally:
        sys.stderr.flush()
        sys.stdout.flush()
        os._exit(code)
