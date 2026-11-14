# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Camera-orbit gates of the projector shadow pool over LEAP (development viewer, -DVKS_GL_STATS=ON).

SKELETON (stage S0 of doc/shadows/projector-pool-budget.md, spec section 7.3). It reads the LEAP API "ProjectorShadows"
(operation "stats", indra/newview/llprojectorshadowstatsapi.cpp). Before the pool exists (stages P2 and later) the API
reports "pool": {"pool_present": false, ...}; a gate then ends as NOT RUN, never as PASS.

  O-1 pure rotation   camera position fixed; yaw 360 deg and pitch -60..+60 deg; per step the pool's static renders, the
                      number of lights without a tile and the CRC32 of every static tile must not change.  (implemented)
  O-2 orbit           24 steps around a centre at 3 m and 8 m, two laps; lap 2 must show 0 renders and identical CRCs,
                      every re-render with its cause.                                                      (stage P3)
  O-4 never vanish    walk a route, teleport in: lights without a tile is 0 after the first fill.           (stage P2)
  O-6 movers          a fan with a spinning child, a rotating spot, a second lamp with a dirty object.        (stage P3)

The camera is moved with LLAgent.setCameraParams as light_check.py does; settings are restored at the end and on error.
stdout is the LEAP channel: this script only logs to stderr.

Modes: (default) run | --dry-run (read-only: the API and its reply shape) | --noop | --restore RUNDIR.
Exit codes: 0 every gate that ran passed (and at least one ran), 1 error, 2 stopped by a STOP file, 3 the pipe closed,
4 a gate failed, 5 protocol error, 6 nothing could run (no pool in this viewer).
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
POOL_KEYS = ["pool_present", "level", "static_renders", "dynamic_renders", "lights_without_tile", "tiles", "rerender_causes"]
SETTINGS = ["RenderShadowMethod", "RenderShadowDetail"]


def pool_state(stats):
    """The comparable part of one stats reply: (static renders, lights without a tile, {tile key: crc})."""
    pool = stats.get("pool") or {}
    crcs = {str(t.get("key")): t.get("crc32") for t in (pool.get("tiles") or [])}
    return int(pool.get("static_renders", 0)), int(pool.get("lights_without_tile", 0)), crcs


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
        self.v.fire("LLAgent", "setCameraParams", camera_pos=cam, focus_pos=focus, camera_locked=True, focus_locked=True,
                    camera_lag=0.0, focus_lag=0.0, camera_threshold=0.0, focus_threshold=0.0)
        self.v.barrier()
        self.camera_set = True
        self.save_restore()

    def warm_up(self):
        """Before the baseline: let the first fill finish. Waits after_camera_s, then polls until static_renders and
        lights_without_tile are unchanged for 3 reads (at most 15 s), so step 0 is not read mid-fill."""
        self.v.sleep(float(self.sc["settle"].get("after_camera_s", 0.5)))
        last, same = None, 0
        for _ in range(30):
            self.v.check_abort()
            st = pool_state(self.stats())[:2]
            same = same + 1 if st == last else 0
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
        fails = judge_o1(states[0], states[1:])
        return {"status": "FAIL" if fails else "PASS", "steps": len(states), "failures": fails[:50]}

    @staticmethod
    def not_yet(name, stage):
        return {"status": "NOT RUN", "why": "%s is implemented in %s" % (name, stage)}

    def go(self):
        self.save_restore()
        for k, val in self.sc.get("settings", {}).items():
            self.change(k, val)
        t = self.sc.get("teleport")
        if t:
            self.v.fire("LLAgent", "requestTeleport", regionname=t["region"], x=t.get("x", 128), y=t.get("y", 128),
                        z=t.get("z", 30), skip_confirmation=True)
            self.v.barrier()
            self.v.sleep(float(self.sc["settle"]["after_teleport_s"]))
        gates = self.report["gates"]
        gates["O-1"] = self.gate_o1()
        gates["O-2"] = self.not_yet("O-2", "stage P3")
        gates["O-4"] = self.not_yet("O-4", "stage P2")
        gates["O-6"] = self.not_yet("O-6", "stage P3")
        for name, g in gates.items():
            log("ORBIT %s %s %s" % (g["status"], name, g.get("why") or "; ".join(g.get("failures", [])[:3])))
        self.cleanup()
        ran = [g["status"] for g in gates.values() if g["status"] != "NOT RUN"]
        if "FAIL" in ran:
            return 4
        return 0 if ran else 6


def dry_run(v, outdir):
    rows = []
    try:
        st = v.req(API, "stats")
        pool = st.get("pool") or {}
        missing = [k for k in POOL_KEYS if k not in pool]
        rows.append(("api", "FAIL" if missing else "PASS",
                     "pool keys missing: %s" % missing if missing else "ProjectorShadows.stats ok, pool_present=%s" % pool.get("pool_present")))
        rows.append(("slots", "PASS", json.dumps(st.get("slots"), sort_keys=True)))
        rows.append(("lights", "PASS", json.dumps(st.get("lights"), sort_keys=True)))
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
