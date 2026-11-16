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

Moving camera, Tracy captures, teleports (README.md, "SL session"): a preset may carry "motion": {"orbit_deg_per_s": N}
(the camera turns around the avatar during its window); --tracy-capture EXE records every window of "window.seconds" seconds
with tracy-capture; a scenario "teleports" section times teleports within the region and to --tp-region and back. Without the
GLSubmitStats API (a Tracy measuring build), with --no-glstats the frame rate comes from the viewer's "FrameCounters:" log lines.

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
import re
import statistics
import subprocess
import sys
import time
import traceback
import urllib.parse

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from leap_client import ConnectionLost, LeapError, LeapSession, LeapTimeout, ProtocolError, ViewerGone  # noqa: E402
from light_check import (Aborted, Viewer, camera_params, default_capture_root, find_viewer_log, log,  # noqa: E402
                         write_json_atomic)

API = "GLSubmitStats"
PROJECTOR_API = "ProjectorShadows"    # optional: the slot and light-set counters recorded per preset
REQUIRED_OPS = ["status", "windowBegin", "windowStop", "summary", "dump", "compareArm", "compareResults", "profileFrame"]
SETTINGS = ["RenderGLSubmitStats", "RenderGLSubmitStatsGPU", "RenderGLSubmitCompare", "RenderGLSubmitCompareInject",
            "RenderGLSubmitPath", "RenderAlphaSortMethod"]
# what can cap the frame rate: recorded in perf.json, so a capped run can be recognised
CAP_SETTINGS = ["RenderVSyncEnable", "FramePerSecondLimit", "FSLimitFramerate"]

DEFAULTS = {
    "name": "gl_submit",
    "description": "",
    "settle": {"post_login_s": 20.0, "after_camera_s": 4.0, "after_teleport_s": 30.0, "login_timeout_s": 3600,
               "window_timeout_s": 180.0, "compare_wait_s": 6.0, "after_setting_s": 3.0,
               "warmup_min_s": 10.0, "warmup_max_s": 45.0, "warmup_stable_windows": 5, "warmup_tolerance": 0.15},
    "window": {"frames": 120, "gpu": True, "seconds": 60.0},
    "teleport": None,
    "teleports": None,      # {"within": N, "offset": [x, y, z], "second": N, "pause_s", "timeout_s", "settle_s", "min_move_m", "capture_s"}
    "owner_teleport": None,  # {"poll_s", "stable_polls", "timeout_s", "ready_message", "done_message"}: the owner teleports by hand
    "fps_probe": None,      # {"seconds": 60, "camera": {...}, "motion": {...}}: the frame rate with no capture connected
    "settings": {},
    "variants": [],
    "camera": {"height_m": 2.5, "look_distance_m": 200.0},
    "presets": [{"id": "current", "camera": None}],
    "profile_frame": False,
    "overhead": {"probe_s": 5.0, "probe_repeats": 3, "bench_iterations": 2000000},
    "compare": {"enabled": True, "passes": [-1], "prove_with_injection": True},
}


def window_geometry(pid=None):
    """Size of the viewer's client area in pixels and whether the window is maximised, from the OS (Windows only; None
    elsewhere or when the window is not found). The viewer is this script's parent process."""
    if os.name != "nt":
        return None
    try:
        import ctypes  # noqa: PLC0415
        from ctypes import wintypes  # noqa: PLC0415
        user32 = ctypes.windll.user32
        pid = pid or os.getppid()
        found = []

        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def callback(hwnd, _lparam):
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid and user32.IsWindowVisible(hwnd) and not user32.GetWindow(hwnd, 4) and user32.GetWindowTextLengthW(hwnd) > 0:
                found.append(hwnd)
            return True

        user32.EnumWindows(callback, 0)
        if not found:
            return None
        rect = wintypes.RECT()
        user32.GetClientRect(found[0], ctypes.byref(rect))
        return {"width": int(rect.right - rect.left), "height": int(rect.bottom - rect.top),
                "maximized": bool(user32.IsZoomed(found[0])), "minimized": bool(user32.IsIconic(found[0]))}
    except (OSError, AttributeError, ValueError):
        return None


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
    if (not ids and not sc["teleports"]) or len(ids) != len(set(ids)):
        raise ValueError("the scenario needs at least one preset (or a teleport series), with distinct ids")
    for p in sc["presets"]:
        if not all(c.isalnum() or c in "_.-" for c in p["id"]):
            raise ValueError("preset id %r must be a file-name-safe token" % p["id"])
    for p in sc["presets"] + ([sc["fps_probe"]] if sc["fps_probe"] else []):
        if p.get("motion") and not p.get("camera"):
            raise ValueError("a preset with motion needs a camera")
    return sc


def fps_from_log(path, offset, end=None):
    """Mean frames per second over the "FrameCounters: frames=N" lines (one per second) the viewer wrote after byte `offset`
    of its log (up to byte `end` when given; the first, partial second is dropped when there are more than two), or None."""
    if not path:
        return None
    try:
        with open(path, "rb") as f:
            f.seek(offset)
            text = f.read(None if end is None else max(0, end - offset)).decode("utf-8", "replace")
    except OSError:
        return None
    vals = [int(m) for m in re.findall(r"FrameCounters: frames=(\d+)", text)]
    if len(vals) > 2:
        vals = vals[1:]
    return round(sum(vals) / len(vals), 2) if vals else None


def settled(vals, windows=5, tolerance=0.15):
    """The last `windows` values are all non-zero and each within `tolerance` of their median (a moving camera varies)."""
    if len(vals) < windows or windows < 2:
        return False
    tail = sorted(vals[-windows:])
    if tail[0] <= 0:
        return False
    mid = len(tail) // 2
    med = tail[mid] if len(tail) % 2 else (tail[mid - 1] + tail[mid]) / 2.0
    return all(abs(v - med) <= tolerance * med for v in tail)


def read_new_frames(path, pos):
    """(frames of each complete "FrameCounters: frames=N" line written after byte `pos`, the new position). Only whole lines
    are consumed, so a line being written is read next time."""
    if not path:
        return [], pos
    try:
        with open(path, "rb") as f:
            f.seek(pos)
            data = f.read()
    except OSError:
        return [], pos
    end = data.rfind(b"\n") + 1
    text = data[:end].decode("utf-8", "replace")
    return [int(m) for m in re.findall(r"FrameCounters: frames=(\d+)", text)], pos + end


def region_from_log(path, offset=0, first=False):
    """A region the viewer entered ("Entering region [NAME]" in its log after byte `offset`): the last one, or the first with
    first=True; None when there is none. The viewer logs the line for neighbour regions too, so the caller picks the line
    that is the agent's own: the first of the session (login) or the first after a teleport began."""
    try:
        with open(path, "rb") as f:
            f.seek(offset)
            text = f.read().decode("utf-8", "replace")
    except (OSError, TypeError):
        return None
    # the viewer logs "Entering region []" at login, before the name is known: skip empty names
    found = [n for n in re.findall(r"Entering region \[([^\]]*)\]", text) if n.strip()]
    return (found[0] if first else found[-1]) if found else None


def log_size(path):
    try:
        return os.path.getsize(path) if path else 0
    except OSError:
        return 0


def cell_of(glob):
    """The region cell (256 m) of a global position: it changes when the agent changes region."""
    return (int(glob[0] // 256), int(glob[1] // 256))


def dist3(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


class TracyCapture:
    """One tracy-capture process at a time: `tracy-capture -a 127.0.0.1 -o <id>.tracy -s <seconds> -f`. Its stdout is not the
    LEAP channel (it goes to a file with stderr)."""

    def __init__(self, exe, outdir, popen=subprocess.Popen):
        self.exe, self.out, self.popen = str(exe), pathlib.Path(outdir), popen
        self.proc = None
        self.cur = None

    def start(self, cid, seconds):
        if self.proc is not None:
            raise LeapError("a Tracy capture is already running (%s)" % self.cur["id"])
        path = self.out / (cid + ".tracy")
        logf = open(self.out / (cid + ".capture.log"), "wb")
        try:
            self.proc = self.popen([self.exe, "-a", "127.0.0.1", "-o", str(path), "-s", str(int(seconds)), "-f"],
                                   stdin=subprocess.DEVNULL, stdout=logf, stderr=subprocess.STDOUT)
        finally:
            logf.close()
        self.cur = {"id": cid, "file": path.name, "seconds": int(seconds),
                    "utc_start": dt.datetime.now(dt.timezone.utc).isoformat()}
        log("Tracy capture %s started (%d s)" % (cid, seconds))

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def finish(self, tick, timeout_s):
        """Wait for the capture to exit (calling tick() while waiting); the record: exit code and file size."""
        end = time.monotonic() + timeout_s
        while self.running():
            if time.monotonic() > end:
                log("WARNING Tracy capture %s did not exit in %.0f s: stopped" % (self.cur["id"], timeout_s))
                self.stop()
                break
            tick()
        rec, path = dict(self.cur), self.out / self.cur["file"]
        rec["exit_code"] = self.proc.poll()
        rec["size_bytes"] = path.stat().st_size if path.is_file() else 0
        self.proc = self.cur = None
        log("Tracy capture %s: exit %s, %d bytes" % (rec["id"], rec["exit_code"], rec["size_bytes"]))
        return rec

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()


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
        self.current_variant = None
        self.base = {}          # the values the scenario's settings and --set gave (what a variant's other keys go back to)
        self.cleaned = False
        self.rows = []
        self.motion = None
        self.cam_now = None             # the camera last commanded: az_deg, camera_pos, focus_pos
        self.heading_log = None         # the open per-second log of cam_now (one capture), or None
        self.heading_t0 = self.heading_last = 0.0
        self.last_warmup = None
        self.login = None           # {"region", "pos", "global"} where the owner logged in
        self.busy_region = None     # the region the owner teleported to
        self.tracy = TracyCapture(args.tracy_capture, outdir, getattr(args, "popen", subprocess.Popen)) if args.tracy_capture else None
        self.vlog = getattr(args, "vlog", None)
        self.fps_log = args.fps_from_log
        self.index = {"scenario": scenario, "presets": {}, "warnings": [], "proof": None, "compare_failures": [],
                      "window_failures": [], "captures": [], "capture_failures": [], "teleports": None,
                      "fps_without_profiler": None}

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
        if self.tracy:
            self.tracy.stop()
        try:
            self.save_index()       # what was measured before the stop or the error
        except OSError as exc:
            log("could not write perf.json: %s" % exc)
        log("restoring the viewer")
        if not self.fps_log:
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
            self.tick(0.5)

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

    def tick(self, period):
        """Wait `period` s; with a moving camera instead turn it on (about 10 Hz) by point_camera calls."""
        m = self.motion
        if not m:
            self.v.sleep(period)
            self.sample_heading()
            return
        az = (m["az0"] + m["rate"] * (time.monotonic() - m["t0"])) % 360.0
        cam, focus = self.v.point_camera(az, m["cfg"])
        self.cam_now = {"az_deg": round(az, 2), "camera_pos": [round(float(c), 2) for c in cam],
                        "focus_pos": [round(float(c), 2) for c in focus]}
        self.v.sleep(0.1)
        self.sample_heading()

    def sample_heading(self):
        """Once a second while a heading log is open: the camera the script last commanded (the viewer has no read-back)."""
        if self.heading_log is None or not self.cam_now:
            return
        now = time.monotonic()
        if now - self.heading_last >= 1.0:
            self.heading_last = now
            self.heading_log.append({"t_s": round(now - self.heading_t0, 1), **self.cam_now})

    def open_heading_log(self):
        self.heading_log = []
        self.heading_t0 = self.heading_last = time.monotonic()
        self.heading_last -= 1.0        # the first sample at once
        self.sample_heading()

    def close_heading_log(self):
        out, self.heading_log = self.heading_log, None
        return out

    def start_capture(self, cid, seconds):
        if not self.tracy:
            return False
        self.tracy.start(cid, seconds)
        return True

    def finish_capture(self, started):
        if not started:
            return
        rec = self.tracy.finish(lambda: self.tick(0.5), self.tracy.cur["seconds"] + 60.0)
        self.index["captures"].append(rec)
        if rec["exit_code"] != 0 or rec["size_bytes"] <= 0:
            self.index["capture_failures"].append(rec["id"])

    def set_preset_camera(self, preset):
        self.motion = None
        self.cam_now = None
        cam = preset.get("camera")
        if not cam:
            return
        cfg = {"pitch_deg": float(cam.get("pitch_deg", 0.0)),
               "height_m": float(cam.get("height_m", self.sc["camera"]["height_m"])),
               "look_distance_m": float(cam.get("look_m", self.sc["camera"]["look_distance_m"]))}
        cam_pos, focus = self.v.point_camera(float(cam.get("az_deg", 0.0)), cfg)
        self.cam_now = {"az_deg": float(cam.get("az_deg", 0.0)), "camera_pos": [round(float(c), 2) for c in cam_pos],
                        "focus_pos": [round(float(c), 2) for c in focus]}
        rate = float((preset.get("motion") or {}).get("orbit_deg_per_s", 0.0))
        if rate:
            self.motion = {"az0": float(cam.get("az_deg", 0.0)), "rate": rate, "cfg": cfg, "t0": time.monotonic()}
        self.camera_set = True
        self.save_restore()

    def measure_fps_off(self):
        """Frames per second of the viewer over probe_s seconds with nothing collecting (the status reports the frame count)."""
        probe = float(self.sc["overhead"]["probe_s"])
        if probe <= 0:
            return None
        if self.fps_log:
            return self.log_fps_for(probe)
        a = self.v.req(API, "status")
        t0 = time.monotonic()
        self.v.sleep(probe)
        b = self.v.req(API, "status")
        dt_s = time.monotonic() - t0
        if "frame_count" not in a or "frame_count" not in b or dt_s <= 0:
            return None
        return round((int(b["frame_count"]) - int(a["frame_count"])) / dt_s, 2)

    def log_fps_for(self, seconds):
        """Frame rate over `seconds` s, from the FrameCounters log lines (the camera moves meanwhile when a preset says so)."""
        off = self.vlog.stat().st_size if self.vlog else 0
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.tick(0.5)
        # only the lines written up to the deadline: the capture has ended by then, later lines are not "profiler connected"
        return fps_from_log(self.vlog, off, log_size(self.vlog))

    def log_window(self, key, preset):
        """The measurement window without the GLSubmitStats API: window.seconds s (moving camera, Tracy capture when given)."""
        seconds = float(self.sc["window"]["seconds"])
        info = self.index["presets"].setdefault(key, {})
        info["projector_stats"] = self.projector_record()
        started = self.start_capture(key, seconds)
        t0 = time.monotonic()
        self.open_heading_log()
        fps = self.log_fps_for(seconds)
        self.finish_capture(started)
        # the orbit goes on for the next variant of this preset (one_preset ends it for a preset without variants)
        info["camera_log"] = self.close_heading_log()
        self.projector_end(info)
        info.update({"seconds": seconds, "fps_log": fps, "tracy_connected": started, "wall_s": round(time.monotonic() - t0, 3),
                     "moving": bool(preset.get("motion"))})
        self.rows.append({"preset": key, "frames": 0, "gpu_frames": 0, "fps_wall": None, "fps_stats_off": None, "fps_log": fps,
                          "variant": ""})
        log("preset %s: %s fps over %.0f s%s" % (key, fps, seconds, " with Tracy connected" if started else ""))

    _projector_api = None       # None unknown, True/False once checked

    def projector_available(self):
        """Whether the viewer has the ProjectorShadows API, asked once. The API is compiled only with -DVKS_GL_STATS=ON, like
        GLSubmitStats: the Tracy measuring build has neither, and a request to a pump nobody listens on would wait the whole
        LEAP timeout for nothing (it did: projector_stats was null for every preset)."""
        if self._projector_api is None:
            try:
                reply = self.v.req("LLLeap", "getAPI", api=PROJECTOR_API, timeout=30)
                ops = {str(o.get("name")) for o in (reply.get("ops") or []) if isinstance(o, dict)}
                self._projector_api = "stats" in ops
            except LeapTimeout:
                self._projector_api = False
                log("no reply from %s getAPI (timeout): projector_stats is recorded as unavailable" % PROJECTOR_API)
                return False
            except LeapError:   # a lost connection (ConnectionLost) is deliberately not a LeapError: it propagates
                self._projector_api = False
            if self._projector_api:
                log("the %s API is there: its counters are recorded per preset (projector_stats)" % PROJECTOR_API)
            else:
                log("no %s API in this viewer (it needs a build with -DVKS_GL_STATS=ON): projector_stats is recorded as unavailable" % PROJECTOR_API)
        return self._projector_api

    def projector_stats(self):
        """The ProjectorShadows.stats reply (slots, light set, pool without the tile list), or None when the viewer has no such
        API or the call fails. crc=false: the tile read-back would stall the frame."""
        if not self.projector_available():
            return None
        try:
            r = self.v.req(PROJECTOR_API, "stats", crc=False)
        except LeapError:
            return None
        out = {k: r.get(k) for k in ("shadow_method", "shadow_detail", "slots", "lights")}
        pool = dict(r.get("pool") or {})
        pool.pop("tiles", None)
        out["pool"] = pool
        return out

    def projector_record(self):
        stats = self.projector_stats()
        if stats is None:
            return {"available": False, "reason": "no %s API in this viewer (built without VKS_GL_STATS=ON) or the call failed" % PROJECTOR_API}
        return stats

    # the cumulative pool counters whose change over a capture says what the pool did meanwhile
    PROJECTOR_DELTAS = ("static_renders", "dynamic_renders", "layer_renders", "dirty_renders", "repacks")

    def projector_end(self, info):
        """Stats again at the end of the capture, and the change of the cumulative counters and render causes over it."""
        start = info.get("projector_stats")
        if not isinstance(start, dict) or start.get("available") is False:
            return
        end = self.projector_stats()
        if end is None:
            return
        info["projector_stats_end"] = end
        a, b = start.get("pool") or {}, end.get("pool") or {}
        delta = {k: round(float(b[k]) - float(a[k]), 3) for k in self.PROJECTOR_DELTAS if k in a and k in b}
        ca, cb = a.get("rerender_causes") or {}, b.get("rerender_causes") or {}
        delta["rerender_causes"] = {k: round(float(cb[k]) - float(ca.get(k, 0)), 3) for k in cb}
        info["projector_delta"] = delta

    def wait(self, seconds):
        """Wait `seconds` s with the camera turning on (tick) when the preset moves."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.tick(min(0.5, max(0.0, end - time.monotonic())))

    def warm_up(self):
        """After a setting change: wait until the frame rate has settled (the first seconds after a change run at a fraction of
        the steady rate), judged from the FrameCounters log lines: warmup_stable_windows last 1 s windows each within
        warmup_tolerance of their median, after at least warmup_min_s and at most warmup_max_s. Returns what happened."""
        st = self.sc["settle"]
        lo, hi = float(st.get("warmup_min_s", 10.0)), float(st.get("warmup_max_s", 45.0))
        n, tol = int(st.get("warmup_stable_windows", 5)), float(st.get("warmup_tolerance", 0.15))
        poll = float(st.get("warmup_poll_s", 1.0))
        pos = self.vlog.stat().st_size if self.vlog else 0
        t0 = time.monotonic()
        stable, vals, first, last_poll = False, [], True, None
        while True:
            self.tick(0.5)
            now = time.monotonic()
            waited = now - t0
            if last_poll is None or now - last_poll >= poll:     # the log is read about once a second, only its new text
                last_poll = now
                new, pos = read_new_frames(self.vlog, pos)
                if first and new:
                    new, first = new[1:], False         # the first line is a partial window
                vals += new
                stable = settled(vals, n, tol)
            if waited >= hi or (waited >= lo and stable):
                break
        res = {"waited_s": round(time.monotonic() - t0, 1), "settled": stable, "last_fps": vals[-n:]}
        log("warm-up after the setting change: %.1f s, %s (last frame rates %s)" % (
            res["waited_s"], "settled" if stable else "NOT settled at the maximum wait", res["last_fps"]))
        if not stable:
            self.warn("the frame rate had not settled after %.0f s of warm-up (last %s)" % (hi, res["last_fps"]))
        return res

    def probe_variants(self, variants, preset_no):
        """probe_repeats probes of the frame rate with nothing collecting per variant, the variants alternating in order between
        repeats (no drift bias). Returns {variant id: [fps, ...]}. One 5 s probe was far too noisy at 130 to 420 fps."""
        reps = max(1, int(self.sc["overhead"].get("probe_repeats", 1)))
        out = {v["id"]: [] for v in variants}
        for rep in range(reps):
            order = variants if (rep + preset_no) % 2 == 0 else list(reversed(variants))
            for variant in order:
                self.v.check_abort()
                self.apply_variant(variant)
                self.v.wait_window_visible("the frame rate probe")
                fps = self.measure_fps_off()
                if fps is not None:
                    out[variant["id"]].append(fps)
        return out

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
        if self.current_variant == variant["id"]:
            return
        wanted = variant.get("settings", {})
        for key in sorted(self.variant_keys):
            if key not in wanted and key in self.orig:
                self.v.set_control(key, self.base.get(key, self.orig[key]))
        for key, val in wanted.items():
            if not self.change(key, val):
                raise LeapError("variant %s: the viewer has no setting %s" % (variant["id"], key))
            self.variant_keys.add(key)
        self.current_variant = variant["id"]
        if self.fps_log and self.vlog:
            self.last_warmup = self.warm_up()
            self.index.setdefault("warmups", []).append({"variant": variant["id"], **self.last_warmup})
        else:
            self.wait(float(self.sc["settle"].get("after_setting_s", 3.0)))

    def one_preset(self, preset, variant=None, set_camera=True, fps_samples=None):
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
            if self.last_warmup:
                info["warmup"] = self.last_warmup
                self.last_warmup = None
        if set_camera:
            self.set_preset_camera(preset)
            self.v.sleep(sc["settle"]["after_camera_s"])
        self.v.wait_window_visible("the window of preset " + key)
        if self.fps_log:
            self.log_window(key, preset)
            if variant is None:
                self.motion = None
            return

        if fps_samples is None and self.args.no_glstats:
            # no window to follow: the clean frame rate is the whole measurement, so it is a median of probe_repeats probes
            # (one 5 s probe is far too noisy), with the spread recorded like the variants' probes
            reps = max(1, int(sc["overhead"].get("probe_repeats", 1)))
            fps_samples = []
            for _ in range(reps):
                self.v.check_abort()
                one = self.measure_fps_off()
                if one is not None:
                    fps_samples.append(one)
        if fps_samples:
            fps_off = round(statistics.median(fps_samples), 2)
            info["fps_probe"] = {"samples": fps_samples, "min": min(fps_samples), "max": max(fps_samples),
                                 "spread_pct": round(100.0 * (max(fps_samples) - min(fps_samples)) / fps_off, 2) if fps_off else None}
        else:
            fps_off = self.measure_fps_off()
        info["projector_stats"] = self.projector_record()
        if self.args.no_glstats:
            # the clean frame rate only: no window, so the GPU timers and the hooks cost nothing
            info["fps_stats_off"] = fps_off
            self.rows.append({"preset": key, "frames": 0, "gpu_frames": 0, "fps_wall": None, "fps_stats_off": fps_off,
                              "variant": variant["id"] if variant else ""})
            log("preset %s: %s fps with the statistics off (no window collected)" % (key, fps_off))
            return
        started = self.start_capture(key, float(sc["window"]["seconds"]))
        start = time.monotonic()
        self.open_heading_log()
        self.v.req(API, "windowBegin", frames=int(sc["window"]["frames"]), gpu=bool(sc["window"]["gpu"]))
        collected, gpu_frames, wall = self.wait_window(start)
        self.finish_capture(started)
        info["camera_log"] = self.close_heading_log()
        self.projector_end(info)
        if variant is None:
            self.motion = None
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
        self.index["window_geometry"] = window_geometry()
        log("viewer window: %s" % (self.index["window_geometry"] or "unknown (not Windows, or not found)"))
        if caps.get("RenderVSyncEnable") or caps.get("FSLimitFramerate"):
            self.warn("vsync or the frame limiter is on (%s): the frame rates of this run are capped; turn them off for a measurement" % caps)
        if not self.fps_log:
            self.index["status_at_start"] = v.req(API, "status")
        if sc["teleport"]:
            t = sc["teleport"]
            self.request_teleport(t["region"], t.get("x", 128), t.get("y", 128), t.get("z", 30))
            log("teleporting to %s; waiting %.0f s" % (t["region"], sc["settle"]["after_teleport_s"]))
            v.sleep(sc["settle"]["after_teleport_s"])
        if sc["owner_teleport"]:
            self.wait_for_owner_teleport()
        if sc["overhead"]["bench_iterations"] and not self.args.no_glstats:
            try:
                b = v.req(API, "selfBench", iterations=int(sc["overhead"]["bench_iterations"]), timeout=120)
                self.index["hook_bench"] = {k: b.get(k) for k in b if k.startswith(("ns_", "hooks_", "iterations"))}
                log("hook cost: %.1f ns per draw's hooks not collecting, %.1f collecting; a pass scope %.0f ns, with GPU timers %.0f ns" % (
                    b.get("ns_per_draw_not_collecting", 0), b.get("ns_per_draw_collecting", 0),
                    b.get("ns_per_scope_cpu_only", 0), b.get("ns_per_scope_with_gpu_timers", 0)))
            except LeapError as exc:
                self.warn("selfBench not available: %s" % exc)
        proof_ok = True
        if sc["compare"]["enabled"] and sc["compare"]["prove_with_injection"] and not self.args.no_glstats:
            proof_ok = self.prove_injection()
        for self.preset_no, preset in enumerate(sc["presets"]):
            v.check_abort()
            if not sc["variants"]:
                self.one_preset(preset)
            else:
                # the same view measured once per variant, back to back (the camera is placed once)
                self.set_preset_camera(preset)
                v.sleep(sc["settle"]["after_camera_s"])
                if self.fps_log:    # the probe samples are not used with the log frame rate: no 10-45 s warm-up per variant for nothing
                    samples = {var["id"]: [] for var in sc["variants"]}
                else:
                    samples = self.probe_variants(sc["variants"], self.preset_no)
                order = sc["variants"] if self.preset_no % 2 == 0 else list(reversed(sc["variants"]))
                for variant in order:
                    v.check_abort()
                    self.one_preset(preset, variant, set_camera=False, fps_samples=samples[variant["id"]])
                self.motion = None      # the orbit ran for every variant; the next preset sets its own camera
            self.save_index()
        if sc["teleports"]:
            self.teleport_series()
        if sc["fps_probe"]:
            self.fps_probe()
        end_geo = window_geometry()
        self.index["window_geometry_end"] = end_geo
        start_geo = self.index.get("window_geometry")
        if start_geo and end_geo and (start_geo["width"], start_geo["height"]) != (end_geo["width"], end_geo["height"]):
            self.warn("the viewer window changed size during the run (%s -> %s): the windows are not comparable" % (start_geo, end_geo))
        self.save_index()
        self.cleanup()
        clean = (proof_ok and not self.index["compare_failures"] and not self.index["window_failures"]
                 and not self.index["capture_failures"] and not self.index.get("teleport_failures"))
        if sc["owner_teleport"]:
            self.notify(self.handoff_cfg()["done_message"] if clean else self.handoff_cfg()["problem_message"])
        return clean

    def handoff_cfg(self):
        return {"poll_s": 1.0, "stable_polls": 3, "timeout_s": 1800.0, "renotify_s": 180.0,
                "problem_message": "Session ended with problems: see perf.json (you can quit).",
                "ready_message": "Ready for the busy region: teleport there now.",
                "done_message": "Session done: you can quit.", **self.sc["owner_teleport"]}

    def notify(self, message):
        """A pop-up for the owner only (a local notification, not nearby chat)."""
        try:
            self.v.fire("LLNotifications", "requestAdd", name="SystemMessage", substitutions={"MESSAGE": message})
            self.v.barrier()
        except LeapError as exc:
            self.warn("the pop-up %r could not be shown: %s" % (message, exc))
        log("pop-up: " + message)

    def notify_problem(self):
        """The owner is told to stay away from the viewer: on a failed or aborted run a pop-up says it is over."""
        if self.sc["owner_teleport"]:
            try:
                self.notify(self.handoff_cfg()["problem_message"])
            except Exception as exc:  # noqa: BLE001  (the connection may be gone)
                log("the pop-up could not be shown: %s" % exc)

    def wait_for_owner_teleport(self):
        """Record where the owner logged in, ask for a teleport to the busy region, and wait until the region cell has changed
        and the position holds still for stable_polls polls; then settle (after_teleport_s)."""
        cfg = self.handoff_cfg()
        pos, glob = self.agent_state()
        name = self.args.start_region or region_from_log(self.vlog, first=True)     # the first line of the session is the home region
        self.login = {"region": name, "pos": pos, "global": glob}
        self.index["login"] = self.login
        log("logged in at %s %s (cell %s)" % (name, pos, cell_of(glob)))
        if not name and int((self.sc["teleports"] or {}).get("second", 0)) and not self.args.tp_region:
            raise LeapError("the login region is unknown (the viewer logs no region name at login): pass --start-region NAME (the launcher's -StartRegion NAME)")
        self.notify(cfg["ready_message"])
        end = time.monotonic() + float(cfg["timeout_s"])
        stable, last = 0, None
        mark, renotify = log_size(self.vlog), time.monotonic() + float(cfg["renotify_s"])
        while True:
            self.v.sleep(float(cfg["poll_s"]))
            reg, g = self.agent_state()
            if time.monotonic() > renotify:
                self.notify(cfg["ready_message"])       # a toast fades: ask again
                renotify = time.monotonic() + float(cfg["renotify_s"])
            if time.monotonic() > end:
                raise LeapError("no teleport to another region within %.0f s" % float(cfg["timeout_s"]))
            if cell_of(g) == cell_of(glob):
                stable, last = 0, None
                mark = log_size(self.vlog)      # the teleport's own log lines come after this
                continue
            stable = stable + 1 if last is not None and dist3(g, last) <= 1.0 else 1
            last = g
            if stable >= int(cfg["stable_polls"]):
                break
        busy = self.args.busy_region
        for _ in range(0 if busy else 10):
            busy = region_from_log(self.vlog, mark, first=True)     # the destination is logged before its neighbours
            if busy and busy != name:
                break
            busy = None
            self.v.sleep(1.0)       # the log line may lag
        if not busy:
            self.warn("the busy region's name could not be read from the viewer log: pass --busy-region NAME to time the teleports")
        self.busy_region = busy
        self.index["busy"] = {"region": busy, "pos": reg, "global": g}
        log("arrived in %s %s (cell %s); settling %.0f s" % (busy, reg, cell_of(g), self.sc["settle"]["after_teleport_s"]))
        self.v.sleep(float(self.sc["settle"]["after_teleport_s"]))

    def request_teleport(self, region, x, y, z):
        # LLAgent.requestTeleport still opens the "TeleportViaSLAPP" confirmation; this operation does not
        self.v.fire("LLTeleportHandler", "teleport", regionname=region, x=x, y=y, z=z)
        self.v.barrier()

    def agent_state(self):
        r = self.v.req("LLAgent", "getPosition")
        return [float(c) for c in r["region"]], [float(c) for c in r["global"]]

    def teleport_once(self, kind, region, xyz, cfg):
        """Time one teleport: from the request until the agent's global position has moved min_move_m (the position only
        changes on arrival), then until it holds still for settle_s. Raises when it has not arrived in timeout_s."""
        _, before = self.agent_state()
        t0 = time.monotonic()
        utc = dt.datetime.now(dt.timezone.utc).isoformat()
        self.request_teleport(region, xyz[0], xyz[1], xyz[2])
        while True:
            self.v.sleep(0.1)
            reg, glob = self.agent_state()
            if dist3(glob, before) > float(cfg["min_move_m"]):
                arrive = time.monotonic() - t0
                break
            if time.monotonic() - t0 > float(cfg["timeout_s"]):
                raise LeapError("teleport %s to %s did not arrive in %.0f s" % (kind, region, float(cfg["timeout_s"])))
        last, still = glob, time.monotonic()
        while time.monotonic() - still < float(cfg["settle_s"]) and time.monotonic() - t0 < float(cfg["timeout_s"]):
            self.v.sleep(0.25)
            reg, glob = self.agent_state()
            if dist3(glob, last) > 1.0:
                last, still = glob, time.monotonic()
        rec = {"kind": kind, "region": region, "target": list(xyz), "request_utc": utc, "arrive_s": round(arrive, 3),
               "last_move_s": round(still - t0, 3), "landed_region_pos": reg,
               "miss_m": round(((reg[0] - xyz[0]) ** 2 + (reg[1] - xyz[1]) ** 2) ** 0.5, 2)}
        log("teleport %s to %s: arrived after %.2f s, still since %.2f s (landed %.1f m from the target)" % (
            kind, region, arrive, rec["last_move_s"], rec["miss_m"]))
        return rec

    def teleport_series(self):
        """Timed teleports: N within the region (start offset, start, ...) and N to --tp-region and back to the start, each
        timed from the request to the arrival; one continuous Tracy capture covers the series."""
        cfg = {"within": 5, "offset": [20.0, 0.0, 0.0], "second": 5, "pause_s": 8.0, "timeout_s": 120.0, "settle_s": 3.0,
               "min_move_m": 5.0, "capture_s": 360.0, **self.sc["teleports"]}
        second = int(cfg["second"])
        if self.login and self.login.get("region") and not self.args.tp_region:
            other = (self.login["region"], list(self.login["pos"]))      # between the busy region and the login region
        elif self.args.tp_region:
            other = (self.args.tp_region, list(self.args.tp_pos))
        else:
            other = None
        if second and not other:
            raise LeapError("the scenario has teleports to a second region: pass --tp-region NAME (or log in first, with a log line)")
        self.motion = None
        if self.camera_set:
            self.v.release_camera()
            self.camera_set = False
            self.save_restore()
        start_region = self.busy_region or self.args.busy_region
        if not start_region and not self.sc["owner_teleport"]:      # no owner flow: the region the agent is in
            start_region = self.args.start_region or region_from_log(self.vlog)
        if not start_region:
            raise LeapError("the region of the teleports is unknown (not read from the viewer log): pass --busy-region NAME")
        pos, glob = self.agent_state()
        off = [float(c) for c in cfg["offset"]]
        for i in (0, 1):
            if pos[i] + off[i] > 250.0 or pos[i] + off[i] < 5.0:
                off[i] = -off[i]
        near = [pos[i] + off[i] for i in range(3)]
        info = {"start_region": start_region, "start_pos": pos, "start_global": glob, "offset": off, "results": []}
        self.index["teleports"] = info
        log("teleport series: start %s %s; %d within the region, %d to a second region" % (start_region, pos, int(cfg["within"]), second))
        plan = [("within", start_region, near if i % 2 == 0 else pos) for i in range(int(cfg["within"]))]
        if int(cfg["within"]) % 2:
            plan.append(("return", start_region, pos))
        plan += [("second_region", *(other if i % 2 == 0 else (start_region, pos))) for i in range(second)]
        if second % 2:
            plan.append(("return", start_region, pos))
        started = self.start_capture("MC1-teleports", float(cfg["capture_s"]))
        for kind, region, xyz in plan:
            self.v.check_abort()
            try:
                info["results"].append(self.teleport_once(kind, region, xyz, cfg))
            except Aborted:
                raise
            except LeapError as exc:
                self.warn("teleport %s to %s failed: %s" % (kind, region, exc))
                info["results"].append({"kind": kind, "region": region, "target": list(xyz), "failed": True, "reason": str(exc)})
                self.index.setdefault("teleport_failures", []).append(kind)
            self.save_index()
            self.v.sleep(float(cfg["pause_s"]))
        if started and not self.tracy.running():
            self.warn("the teleport series ran longer than the capture (%.0f s): its end is not in MC1-teleports" % float(cfg["capture_s"]))
            self.index["capture_failures"].append("MC1-teleports (ended before the series)")
        self.finish_capture(started)

    def fps_probe(self):
        """The frame rate with no capture connected (the viewer's own, for the doc's "fps without the profiler")."""
        p = self.sc["fps_probe"]
        secs = float(p.get("seconds", 60.0))
        self.set_preset_camera(p)
        self.v.sleep(self.sc["settle"]["after_camera_s"])
        fps = self.log_fps_for(secs)
        self.motion = None
        self.index["fps_without_profiler"] = {"fps": fps, "seconds": secs, "tracy_connected": False}
        log("fps without the profiler: %s over %.0f s" % (fps, secs))

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
    if args.fps_from_log:
        problems = []
        rows = [("api", "PASS", "no %s API: frame rates come from the FrameCounters log lines" % API)]
        for key in sorted(set(sc["settings"]) | set(CAP_SETTINGS)):
            cur = v.try_control(key)
            rows.append(("setting " + key, "PASS" if cur is not None else "FAIL", "" if cur is None else repr(cur.get("value"))))
        if args.tracy_capture:
            rows.append(("tracy-capture", "PASS" if os.path.isfile(args.tracy_capture) else "FAIL", args.tracy_capture))
        rows.append(("viewer log", "PASS" if args.vlog else "FAIL", str(args.vlog or "not found (--viewer-log): no frame rate without it")))
        need = {"LLAgent": ["getPosition"], "LLNotifications": ["requestAdd"]}
        if sc["owner_teleport"] or sc["teleports"] or sc["teleport"]:
            need["LLTeleportHandler"] = ["teleport"]
        if any(p.get("camera") for p in sc["presets"]):
            need["LLAgent"] += ["setCameraParams", "setFollowCamActive", "removeCameraParams"]
        for api, ops in need.items():
            try:
                blob = json.dumps(v.req("LLLeap", "getAPI", api=api).get("ops"), default=str)
                missing = [o for o in ops if '"%s"' % o not in blob]
                rows.append(("api " + api, "FAIL" if missing else "PASS", ("missing ops: %s" % missing) if missing else "ops ok"))
            except LeapError as exc:
                rows.append(("api " + api, "FAIL", str(exc)))
    else:
        problems = check_api(v)
        rows = [("api", "FAIL" if problems else "PASS", "; ".join(problems) or "%s has %d operations" % (API, len(REQUIRED_OPS)))]
    if not problems and not args.fps_from_log:
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
    ap.add_argument("--no-glstats", action="store_true",
                    help="frame rate only: nothing collects (no window, no GPU timers, no identical-image check, no hook bench); "
                         "per preset the probed fps (median of probe_repeats probes) and the ProjectorShadows.stats snapshot are recorded")
    ap.add_argument("--tracy-capture", default=None, metavar="EXE", help="path of tracy-capture.exe: one capture per measurement window "
                    "(window.seconds) and one for the teleport series, into the run folder")
    ap.add_argument("--tp-region", default=None, metavar="NAME", help="override: the second region for the teleports instead of the login region (URL-escaped: %%20 is a space; "
                    "--leap arguments cannot hold spaces)")
    ap.add_argument("--tp-pos", default="128,128,30", metavar="X,Y,Z", help="position in --tp-region (default 128,128,30)")
    ap.add_argument("--start-region", default=None, metavar="NAME", help="the region you log in to (default: the first "
                    "'Entering region' line of the viewer log; URL-escaped like --tp-region)")
    ap.add_argument("--busy-region", default=None, metavar="NAME", help="the region you teleport to, when its name cannot be read from "
                    "the viewer log (URL-escaped like --tp-region)")
    args = ap.parse_args(argv)
    args.fps_from_log = False
    args.start_region = urllib.parse.unquote(args.start_region) if args.start_region else None
    args.busy_region = urllib.parse.unquote(args.busy_region) if args.busy_region else None
    args.tp_region = urllib.parse.unquote(args.tp_region) if args.tp_region else None
    try:
        args.tp_pos = [float(c) for c in args.tp_pos.split(",")]
        if len(args.tp_pos) != 3:
            raise ValueError
    except ValueError:
        ap.error("--tp-pos needs X,Y,Z")
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
        if args.no_glstats and check_api(v):
            args.fps_from_log = True        # a Tracy measuring build: no GLSubmitStats; the frame rate comes from the log
            log("no %s API: the frame rate comes from the FrameCounters lines of the viewer log" % API)
            # variants work here too: each variant gets its own measured window (log_window), its own Tracy capture and
            # its own frame rate from the log; only the clean-probe comparison of the GLSubmitStats path is absent
        args.vlog = vlog
        if args.dry_run:
            return dry_run(v, sc, args, outdir)
        problems = [] if args.fps_from_log else check_api(v)
        if problems:
            log("FAILED: " + "; ".join(problems))
            return 1
        if args.fps_from_log and not vlog:
            log("FAILED: the viewer log was not found (--viewer-log): no frame rate without it")
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
            log("PERFCHECK FAILED: the proof, a compared pass, a capture or a teleport failed (see perf.json, compare_*.json)")
            code = 4
    except Aborted as exc:
        log("aborted: %s" % exc)
        code = 2
        if run is not None:
            run.cleanup()
            run.notify_problem()
    except KeyboardInterrupt:
        log("interrupted: restoring the viewer")
        code = 2
        if run is not None:
            run.cleanup()
            run.notify_problem()
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
            run.notify_problem()
    finally:
        if run is not None and run.tracy:
            run.tracy.stop()
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
