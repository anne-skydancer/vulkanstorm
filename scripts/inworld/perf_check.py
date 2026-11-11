# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""In-world OpenGL submission capture over LEAP (the viewer's --leap plugin interface).

Needs a development viewer built with -DVKS_GL_STATS=ON (it has the LEAP API "GLSubmitStats"). Start it from the
viewer with --leap (see README.md for the PowerShell lines). The script waits until you have logged in and the
scene has settled, then:

  1. proves the identical-image check: it compares the G-buffer with one bit flipped in the second readback and
     requires the check to say DIFFERENT (without that proof a later IDENTICAL means nothing);
  2. for each camera preset of the scenario: points the camera, collects a window of N frames (counters per view
     and pass, CPU ms, GPU ms from timestamp queries), writes the window as JSON, optionally writes a Frame
     Profile, and runs the identical-image check on every pass it supports (the pass is rendered twice in one
     frame and compared bit for bit; today both renders use the same path, so any difference is a harness bug).

A scenario may list "variants" (each with its own settings, e.g. RenderAlphaSortMethod 0 and 1): every preset is
measured once per variant, back to back, the order alternating from preset to preset (no order bias), and the other
variants' settings are put back to the scenario's values in between.

It restores your settings and camera at the end and on error (also on Ctrl-C; after a crash use --restore RUNDIR). Dev tooling only: nothing here is part of the viewer.
stdout is the LEAP channel: this script only ever logs to stderr (the viewer copies it into its log).

Modes: (default) run | --dry-run (read-only: the API, its operations, the settings) | --noop | --restore RUNDIR.
Exit codes: 0 ok, 1 error, 2 stopped by a STOP file, 3 the pipe closed, 4 the check failed (the proof, or a
compared pass differed), 5 protocol error.
"""

import argparse
import csv
import datetime as dt
import json
import os
import pathlib
import sys
import time
import traceback

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from leap_client import ConnectionLost, LeapError, LeapSession, LeapTimeout, ProtocolError, ViewerGone  # noqa: E402
from light_check import (Aborted, Viewer, camera_params, default_capture_root, find_viewer_log, log,  # noqa: E402
                         write_json_atomic)

API = "GLSubmitStats"
REQUIRED_OPS = ["status", "windowBegin", "windowStop", "summary", "dump", "compareArm", "compareResults", "profileFrame"]
SETTINGS = ["RenderGLSubmitStats", "RenderGLSubmitStatsGPU", "RenderGLSubmitCompare", "RenderGLSubmitCompareInject",
            "RenderGLSubmitPath", "RenderAlphaSortMethod"]
# what can cap the frame rate: recorded in perf.json, so a capped run can be recognised
CAP_SETTINGS = ["RenderVSyncEnable", "FramePerSecondLimit", "FSLimitFramerate"]

DEFAULTS = {
    "name": "gl_submit",
    "description": "",
    "settle": {"post_login_s": 20.0, "after_camera_s": 4.0, "after_teleport_s": 30.0, "login_timeout_s": 3600,
               "window_timeout_s": 180.0, "compare_wait_s": 6.0, "after_setting_s": 3.0},
    "window": {"frames": 120, "gpu": True},
    "teleport": None,
    "settings": {},
    "variants": [],
    "camera": {"height_m": 2.5, "look_distance_m": 200.0},
    "presets": [{"id": "current", "camera": None}],
    "profile_frame": False,
    "overhead": {"probe_s": 5.0, "bench_iterations": 2000000},
    "compare": {"enabled": True, "passes": [-1], "prove_with_injection": True},
}


def load_scenario(path):
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)
    sc = json.loads(json.dumps(DEFAULTS))
    for k, v in raw.items():
        if isinstance(v, dict) and isinstance(sc.get(k), dict):
            sc[k].update(v)
        else:
            sc[k] = v
    for v in sc["variants"]:
        if not isinstance(v, dict) or not isinstance(v.get("settings", {}), dict):
            raise ValueError("each variant must be an object with an id and a settings object")
    vids = [v.get("id") for v in sc["variants"]]
    if len(vids) != len(set(vids)) or not all(isinstance(i, str) and i and all(c.isalnum() or c in ".-" for c in i) for i in vids):
        raise ValueError("variant ids must be distinct tokens of letters, digits, '.' and '-' (they are joined to the preset id with '__')")
    ids = [p["id"] for p in sc["presets"]]
    if not ids or len(ids) != len(set(ids)):
        raise ValueError("the scenario needs at least one preset, with distinct ids")
    for p in sc["presets"]:
        if not all(c.isalnum() or c in "_.-" for c in p["id"]):
            raise ValueError("preset id %r must be a file-name-safe token" % p["id"])
    return sc


def check_api(v):
    """The GLSubmitStats API exists and has every operation. Returns the list of what is missing."""
    try:
        reply = v.req("LLLeap", "getAPI", api=API, timeout=30)
    except LeapError as exc:
        return ["the API %s is not there (%s): this viewer was not built with -DVKS_GL_STATS=ON" % (API, exc)]
    ops = {str(o.get("name")) for o in (reply.get("ops") or []) if isinstance(o, dict)}
    if not ops:
        return ["the API %s is not there: this viewer was not built with -DVKS_GL_STATS=ON" % API]
    return ["operation %s.%s is missing" % (API, op) for op in REQUIRED_OPS if op not in ops]


def thin_attachments(results):
    """Attachments the pass left (nearly) empty: one distinct value, so an IDENTICAL says little. [(pass label, attachment)]."""
    out = []
    for r in results or []:
        if r.get("ran") and r.get("identical"):
            for a in r.get("attachments") or []:
                if int(a.get("distinct_values", 2)) <= 1:
                    out.append((str(r.get("label")), str(a.get("name"))))
    return out


def injection_caught(results):
    """(ok, why): the G-buffer ran and exactly the injection differs: color0, flagged injected, one pixel, nothing else."""
    g = next((r for r in results or [] if r.get("label") == "G-buffer"), None)
    if g is None or not g.get("ran"):
        return False, "the G-buffer did not run"
    atts = g.get("attachments") or []
    injected = [a for a in atts if a.get("injected")]
    if len(injected) != 1 or injected[0].get("name") != "color0":
        return False, "no injection into color0 was reported"
    if int(injected[0].get("mismatched_pixels", 0)) != 1:
        return False, "the injected bit changed %s pixels, not 1" % injected[0].get("mismatched_pixels")
    others = [a.get("name") for a in atts if not a.get("injected") and int(a.get("mismatched_pixels", 1)) != 0]
    if others:
        return False, "attachments other than the injection differ: %s (a real difference hides the proof)" % others
    return True, "caught"


def passes_failed(results):
    """Compare results of one frame -> (identical passes, differing passes, passes that did not run)."""
    same, diff, skipped = [], [], []
    for r in results or []:
        label = str(r.get("label", r.get("pass")))
        if not r.get("ran"):
            skipped.append("%s (%s)" % (label, r.get("note", "not run")))
        elif r.get("identical"):
            same.append(label)
        else:
            diff.append(label)
    return same, diff, skipped


class Run:
    def __init__(self, viewer, scenario, outdir, args):
        self.v = viewer
        self.sc = scenario
        self.out = pathlib.Path(outdir)
        self.args = args
        self.orig = {}
        self.camera_set = False
        self.variant_keys = set()
        self.preset_no = 0
        self.base = {}          # the values the scenario's settings and --set gave (what a variant's other keys go back to)
        self.cleaned = False
        self.rows = []
        self.index = {"scenario": scenario, "presets": {}, "warnings": [], "proof": None, "compare_failures": [],
                      "window_failures": []}

    def warn(self, msg):
        log("WARNING " + msg)
        self.index["warnings"].append(msg)

    def save_restore(self):
        write_json_atomic(self.out / "restore.json", {"settings": self.orig, "camera_set": self.camera_set})

    def change(self, key, value):
        if key not in self.orig:
            cur = self.v.try_control(key)
            if cur is None:
                self.warn("control %s does not exist in this viewer; skipped" % key)
                return False
            self.orig[key] = cur.get("value")
            self.save_restore()
        self.v.set_control(key, value)
        return True

    def cleanup(self):
        if self.cleaned:
            return
        self.cleaned = True
        self.v.abort_file = None
        try:
            self.save_index()       # what was measured before the stop or the error
        except OSError as exc:
            log("could not write perf.json: %s" % exc)
        log("restoring the viewer")
        try:
            self.v.req(API, "windowStop")
        except (LeapError, ConnectionLost):
            pass
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

    # -- steps -----------------------------------------------------------------------------------
    def wait_window(self, start_wall):
        sc = self.sc
        end = time.monotonic() + sc["settle"]["window_timeout_s"]
        while True:
            st = self.v.req(API, "status")
            collected, target = int(st.get("window_collected", 0)), int(st.get("window_target", 0))
            gpu = int(st.get("window_gpu_frames", 0))
            done = not st.get("window_active") and collected >= target > 0
            if done and (not sc["window"]["gpu"] or gpu >= collected):
                return collected, gpu, time.monotonic() - start_wall
            if time.monotonic() > end:
                self.warn("the window did not finish in %.0f s (collected %d of %d, gpu %d)"
                          % (sc["settle"]["window_timeout_s"], collected, target, gpu))
                return collected, gpu, time.monotonic() - start_wall
            self.v.sleep(0.5)

    def compare_once(self, pass_id, inject=0):
        """Arm the check for the next frame and read its results."""
        if inject:
            self.change("RenderGLSubmitCompareInject", inject)
        before = int(self.v.req(API, "compareResults").get("arm_serial", 0))
        self.v.req(API, "compareArm", **{"pass": pass_id})
        end = time.monotonic() + self.sc["settle"]["compare_wait_s"] + 20.0
        # the setting stays set until a frame takes it; the frame disarms itself when it is over
        finished = False
        while time.monotonic() < end:
            r = self.v.req(API, "compareResults")
            if int(r.get("pending", 1)) == 0 and int(r.get("armed", 1)) == 0:
                finished = True
                break
            self.v.sleep(0.3)
        r = self.v.req(API, "compareResults")
        if not finished and not (int(r.get("pending", 1)) == 0 and int(r.get("armed", 1)) == 0):
            if inject:
                self.change("RenderGLSubmitCompareInject", 0)
            raise LeapError("the check did not finish before the timeout (still armed or pending): no result to trust")
        if inject:
            self.change("RenderGLSubmitCompareInject", 0)
        if int(r.get("arm_serial", before)) == before:
            raise LeapError("the armed pass never ran: no frame took the arming (is the viewer rendering, not minimized?); "
                            "the results are the previous ones")
        return r.get("results") or []

    def prove_injection(self):
        try:
            results = self.compare_once(1, inject=1)
            ok, why = injection_caught(results)
        except LeapError as exc:
            results, ok, why = [], False, str(exc)
        self.index["proof"] = {"ok": ok, "why": why, "results": results}
        log("PROOF %s: one flipped bit in the G-buffer readback: %s" % ("PASS" if ok else "FAIL", why))
        for label, name in thin_attachments(results):
            log("note: %s %s holds one distinct value" % (label, name))
        return ok

    def set_preset_camera(self, preset):
        cam = preset.get("camera")
        if not cam:
            return
        cfg = {"pitch_deg": float(cam.get("pitch_deg", 0.0)),
               "height_m": float(cam.get("height_m", self.sc["camera"]["height_m"])),
               "look_distance_m": float(cam.get("look_m", self.sc["camera"]["look_distance_m"]))}
        self.v.point_camera(float(cam.get("az_deg", 0.0)), cfg)
        self.camera_set = True
        self.save_restore()

    def measure_fps_off(self):
        """Frames per second of the viewer over probe_s seconds with nothing collecting (the status reports the frame count)."""
        probe = float(self.sc["overhead"]["probe_s"])
        if probe <= 0:
            return None
        a = self.v.req(API, "status")
        t0 = time.monotonic()
        self.v.sleep(probe)
        b = self.v.req(API, "status")
        dt_s = time.monotonic() - t0
        if "frame_count" not in a or "frame_count" not in b or dt_s <= 0:
            return None
        return round((int(b["frame_count"]) - int(a["frame_count"])) / dt_s, 2)

    def fetch_profile(self, logs_dir, asked, pid):
        """The Frame Profile the next frame writes (profile.v*.json in the viewer's log folder): copied into the run folder."""
        if not logs_dir:
            return None
        end = time.monotonic() + 30.0
        while time.monotonic() < end:
            files = [p for p in pathlib.Path(logs_dir).glob("profile.v*.json") if p.stat().st_mtime >= asked]
            if files:
                newest = max(files, key=lambda p: p.stat().st_mtime)
                dest = self.out / ("profile_%s.json" % pid)
                dest.write_bytes(newest.read_bytes())
                return dest.name
            self.v.sleep(0.5)
        return None

    def apply_variant(self, variant):
        """Set the settings of this variant; a setting an earlier variant changed and this one does not name goes back to
        the value the scenario (and --set) gave it, else its original value, so each variant is measured against the same
        baseline. A variant setting the viewer does not have is an error: the window would not measure the variant."""
        wanted = variant.get("settings", {})
        for key in sorted(self.variant_keys):
            if key not in wanted and key in self.orig:
                self.v.set_control(key, self.base.get(key, self.orig[key]))
        for key, val in wanted.items():
            if not self.change(key, val):
                raise LeapError("variant %s: the viewer has no setting %s" % (variant["id"], key))
            self.variant_keys.add(key)
        self.v.sleep(float(self.sc["settle"].get("after_setting_s", 3.0)))

    def one_preset(self, preset, variant=None, set_camera=True):
        sc = self.sc
        pid = preset["id"]
        if variant is None:
            key = pid
        else:
            key = "%s__%s" % (pid, variant["id"])
        if variant is not None:
            self.apply_variant(variant)     # raises before anything is recorded when a setting cannot be applied
        info = self.index["presets"].setdefault(key, {})
        if variant is not None:
            info["preset"] = pid
            info["variant"] = variant["id"]
            info["variant_settings"] = variant.get("settings", {})
        if set_camera:
            self.set_preset_camera(preset)
            self.v.sleep(sc["settle"]["after_camera_s"])
        self.v.wait_window_visible("the window of preset " + key)

        fps_off = self.measure_fps_off()
        start = time.monotonic()
        self.v.req(API, "windowBegin", frames=int(sc["window"]["frames"]), gpu=bool(sc["window"]["gpu"]))
        collected, gpu_frames, wall = self.wait_window(start)
        target = int(sc["window"]["frames"])
        if collected < target:
            self.index["window_failures"].append({"preset": key, "collected": collected, "target": target})
        info.update({"frames": collected, "gpu_frames": gpu_frames, "wall_s": round(wall, 3),
                     "fps_wall": round(collected / wall, 2) if wall > 0 else None, "fps_stats_off": fps_off})
        if fps_off and info["fps_wall"]:
            info["overhead_pct"] = round(100.0 * (fps_off / info["fps_wall"] - 1.0), 2)
            log("preset %s: %.1f fps with the statistics off, %.1f collecting (%+.1f%%)" % (key, fps_off, info["fps_wall"], info["overhead_pct"]))
        path = self.out / ("window_%s.json" % key)
        self.v.req(API, "dump", kind="window", path=str(path).replace("\\", "/"))
        summary = self.v.req(API, "summary", kind="window")
        info["summary"] = summary.get("summary")
        info["window_file"] = path.name
        totals = (summary.get("summary") or {}).get("totals") or {}
        self.rows.append({"preset": key, "frames": collected, "gpu_frames": gpu_frames, "fps_wall": info["fps_wall"],
                          "fps_stats_off": fps_off, "variant": variant["id"] if variant else "",
                          **{k: totals.get(k) for k in ("draws", "drawTriangles", "vbChanges", "attribRespecs", "iboBinds",
                                                         "shaderBinds", "texBindFast", "texBindFastRedundant", "texGLBind",
                                                         "uniformGL", "uniformBytes", "paletteUploads", "paletteDistinct",
                                                         "mdvSyncs", "bufSubData", "stateToggles", "occBlocking", "runs",
                                                         "mergeableDraws", "cpu_ms", "gpu_ms")}})
        log("preset %s: %d frames (%d with GPU time), %.1f fps wall, %s draws/frame, cpu %s ms, gpu %s ms" %
            (key, collected, gpu_frames, info["fps_wall"] or 0, totals.get("draws"), totals.get("cpu_ms"), totals.get("gpu_ms")))

        if sc["profile_frame"]:
            try:
                asked = time.time() - 1.0
                r = self.v.req(API, "profileFrame")
                found = self.fetch_profile(r.get("logs_dir"), asked, key)
                info["profile_file"] = found
                log("Frame Profile for %s: %s" % (key, found or "not found"))
            except LeapError as exc:
                self.warn("Frame Profile not available: %s" % exc)

        if sc["compare"]["enabled"]:
            allres = {}
            for p in sc["compare"]["passes"]:
                try:
                    res = self.compare_once(int(p))
                except LeapError as exc:
                    self.index["compare_failures"].append({"preset": key, "pass": p, "passes": [], "error": str(exc)})
                    log("compare %s pass %s FAILED: %s" % (key, p, exc))
                    continue
                same, diff, skipped = passes_failed(res)
                allres[str(p)] = res
                log("compare %s pass %s: identical %s; DIFFERENT %s; not run %s" % (key, p, same or "-", diff or "-", skipped or "-"))
                for label, name in thin_attachments(res):
                    log("note: %s %s holds one distinct value (an empty attachment: IDENTICAL says little)" % (label, name))
                if diff:
                    self.index["compare_failures"].append({"preset": key, "pass": p, "passes": diff})
                if int(p) in (-1, 1) and "G-buffer" not in same + diff:
                    self.index["compare_failures"].append({"preset": key, "pass": p, "passes": [],
                                                           "error": "the G-buffer did not run (%s)" % (skipped or "no result")})
                    log("compare %s: the G-buffer did not run" % key)
            info["compare"] = allres
            write_json_atomic(self.out / ("compare_%s.json" % key), allres)

    def go(self):
        sc = self.sc
        v = self.v
        self.save_restore()
        # the collection itself needs no setting (a window collects by itself); these keep the GPU timers on
        # for the whole run and make the run's settings explicit
        for k, val in {**sc["settings"], **self.args.set_values}.items():
            if self.change(k, val):
                self.base[k] = val
        caps = {}
        for k in CAP_SETTINGS:
            cur = v.try_control(k)
            if cur is not None:
                caps[k] = cur.get("value")
        self.index["frame_rate_caps"] = caps
        if caps.get("RenderVSyncEnable") or caps.get("FSLimitFramerate"):
            self.warn("vsync or the frame limiter is on (%s): the frame rates of this run are capped; turn them off for a measurement" % caps)
        st = v.req(API, "status")
        self.index["status_at_start"] = st
        if sc["teleport"]:
            t = sc["teleport"]
            v.fire("LLAgent", "requestTeleport", regionname=t["region"], x=t.get("x", 128), y=t.get("y", 128), z=t.get("z", 30),
                   skip_confirmation=True)
            v.barrier()
            log("teleporting to %s; waiting %.0f s" % (t["region"], sc["settle"]["after_teleport_s"]))
            v.sleep(sc["settle"]["after_teleport_s"])
        if sc["overhead"]["bench_iterations"]:
            try:
                b = v.req(API, "selfBench", iterations=int(sc["overhead"]["bench_iterations"]), timeout=120)
                self.index["hook_bench"] = {k: b.get(k) for k in b if k.startswith(("ns_", "hooks_", "iterations"))}
                log("hook cost: %.1f ns per draw's hooks not collecting, %.1f collecting; a pass scope %.0f ns, with GPU timers %.0f ns" % (
                    b.get("ns_per_draw_not_collecting", 0), b.get("ns_per_draw_collecting", 0),
                    b.get("ns_per_scope_cpu_only", 0), b.get("ns_per_scope_with_gpu_timers", 0)))
            except LeapError as exc:
                self.warn("selfBench not available: %s" % exc)
        proof_ok = True
        if sc["compare"]["enabled"] and sc["compare"]["prove_with_injection"]:
            proof_ok = self.prove_injection()
        for self.preset_no, preset in enumerate(sc["presets"]):
            v.check_abort()
            if not sc["variants"]:
                self.one_preset(preset)
            else:
                # the same view measured once per variant, back to back (the camera is placed once)
                self.set_preset_camera(preset)
                v.sleep(sc["settle"]["after_camera_s"])
                order = sc["variants"] if self.preset_no % 2 == 0 else list(reversed(sc["variants"]))
                for variant in order:
                    v.check_abort()
                    self.one_preset(preset, variant, set_camera=False)
            self.save_index()
        self.save_index()
        self.cleanup()
        return proof_ok and not self.index["compare_failures"] and not self.index["window_failures"]

    def save_index(self):
        write_json_atomic(self.out / "perf.json", self.index)
        if self.rows:
            cols = list(self.rows[0].keys())
            tmp = self.out / "manifest.csv.tmp"
            with open(tmp, "w", newline="", encoding="utf-8") as f:
                w = csv.DictWriter(f, fieldnames=cols)
                w.writeheader()
                w.writerows(self.rows)
            os.replace(tmp, self.out / "manifest.csv")


def dry_run(v, sc, args, outdir):
    problems = check_api(v)
    rows = [("api", "FAIL" if problems else "PASS", "; ".join(problems) or "%s has %d operations" % (API, len(REQUIRED_OPS)))]
    if not problems:
        st = v.req(API, "status")
        rows.append(("status", "PASS", json.dumps(st, sort_keys=True)))
        for key in SETTINGS:
            cur = v.try_control(key)
            rows.append(("setting " + key, "PASS" if cur is not None else "FAIL", "" if cur is None else repr(cur.get("value"))))
    ok = all(r[1] == "PASS" for r in rows)
    for name, status, detail in rows:
        log("PERFCHECK %s %s %s" % (status, name, detail))
    write_json_atomic(pathlib.Path(outdir) / "dry_run.json", [dict(zip(("check", "status", "detail"), r)) for r in rows])
    log("PERFCHECK dry run %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def restore_run(v, args):
    path = pathlib.Path(args.restore) / "restore.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    for key, val in (data.get("settings") or {}).items():
        try:
            v.set_control(key, val)
            log("restored %s = %r" % (key, val))
        except LeapError as exc:
            log("could not restore %s: %s" % (key, exc))
    if data.get("camera_set"):
        v.release_camera()
        log("camera released")
    return 0


def parse_args(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scenario", default=str(HERE / "scenarios" / "gl_submit_g0.json"))
    ap.add_argument("--out", default=None, help="run folder (default: .captures/<date-time>); resolved to an absolute path")
    ap.add_argument("--dry-run", action="store_true", help="read-only check of the API and the settings")
    ap.add_argument("--noop", action="store_true", help="do nothing: take the LEAP handshake and exit 0 (the second, throw-away --leap entry)")
    ap.add_argument("--no-login-wait", action="store_true", help="do not wait for login (dry run at the login screen)")
    ap.add_argument("--restore", default=None, metavar="RUNDIR", help="re-apply the settings saved in RUNDIR/restore.json")
    ap.add_argument("--frames", type=int, default=0, help="frames per window (default: the scenario's)")
    ap.add_argument("--only", default=None, help="comma-separated preset ids to run")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE", help="set a debug setting for the run (restored afterwards)")
    ap.add_argument("--viewer-log", default=None)
    ap.add_argument("--max-wait-s", type=float, default=None, help="seconds to wait for login (default: the scenario's login_timeout_s)")
    args = ap.parse_args(argv)
    args.only = [s for s in (args.only or "").split(",") if s]
    vals = {}
    for item in args.set:
        k, sep, val = item.partition("=")
        if not k or not sep:
            ap.error("--set needs KEY=VALUE")
        low = val.lower()
        vals[k] = True if low == "true" else False if low == "false" else (int(val) if val.lstrip("-").isdigit() else val)
    args.set_values = vals
    return args


def execute(session, args, sc, outdir):
    outdir = pathlib.Path(outdir).resolve()
    if not args.restore:
        outdir.mkdir(parents=True, exist_ok=True)
        stale = outdir / "STOP"
        if stale.exists():
            stale.unlink()
    v = Viewer(session, abort_file=None if args.restore else str(outdir / "STOP"))
    session.diag_dir = None if args.restore else outdir
    log("connected; command pump %s; run folder %s" % (session.command_pump, outdir))
    code = 0
    run = None
    vlog = find_viewer_log(args.viewer_log)
    log_start = vlog.stat().st_size if vlog else None
    try:
        session.ping()
        if args.restore:
            return restore_run(v, args)
        if not args.no_login_wait:
            log("waiting for login (log in by hand; the script starts when the scene is up)")
            v.wait_for_login(args.max_wait_s or sc["settle"]["login_timeout_s"])
            log("logged in; settling %.0f s" % sc["settle"]["post_login_s"])
            v.sleep(sc["settle"]["post_login_s"])
        if args.dry_run:
            return dry_run(v, sc, args, outdir)
        problems = check_api(v)
        if problems:
            log("FAILED: " + "; ".join(problems))
            return 1
        if args.frames:
            sc["window"]["frames"] = args.frames
        if args.only:
            sc["presets"] = [p for p in sc["presets"] if p["id"] in args.only]
            if not sc["presets"]:
                raise LeapError("--only matches no preset")
        if ".captures" not in outdir.parts:
            log("WARNING the run folder is not under a .captures folder: the viewer refuses to write its dump files anywhere else "
                "(GLSubmitStats.dump takes only the viewer log folder or a .captures folder)")
        write_json_atomic(outdir / "scenario.json", sc)
        run = Run(v, sc, outdir, args)
        clean = run.go()
        log("done: %d presets in %s" % (len(run.rows), outdir))
        log("next: python %s %s" % (HERE / "analyze_perf_check.py", outdir))
        if not clean:
            log("PERFCHECK FAILED: the proof or a compared pass failed (see perf.json, compare_*.json)")
            code = 4
    except Aborted as exc:
        log("aborted: %s" % exc)
        code = 2
        if run is not None:
            run.cleanup()
    except KeyboardInterrupt:
        log("interrupted: restoring the viewer")
        code = 2
        if run is not None:
            run.cleanup()
    except ViewerGone as exc:
        log("the pipe from the viewer reached end of file: %s (settings could not be restored; see restore.json)" % exc)
        code = 3
    except ProtocolError as exc:
        log("PROTOCOL ERROR, the connection to the viewer is lost: %s. Use --restore on restore.json in a new viewer." % exc)
        code = 5
    except Exception as exc:  # noqa: BLE001
        log("FAILED: %s" % exc)
        code = 1
        if run is not None:
            run.cleanup()
    finally:
        if vlog and log_start is not None and not args.restore:
            try:
                with open(vlog, "rb") as f:
                    f.seek(log_start)
                    (outdir / "viewer_log_excerpt.txt").write_bytes(f.read())
            except OSError as exc:
                log("log excerpt not written: %s" % exc)
    return code


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    sc = load_scenario(args.scenario) if not args.noop else None
    outdir = pathlib.Path(args.out) if args.out else default_capture_root() / ("perf-" + dt.datetime.now().strftime("%Y%m%d-%H%M%S"))
    session = LeapSession(sys.stdin.buffer, sys.stdout.buffer, log=log).start()
    if args.noop:
        log("noop plugin: handshake taken, exiting")
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
