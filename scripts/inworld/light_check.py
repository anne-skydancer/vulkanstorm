# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""In-world light check driven over LEAP (the viewer's --leap plugin interface).

Start it from the viewer:  vulkanstorm-bin.exe --leap "<python> <this file> --scenario <json> --out <dir>"
(see README.md for the exact PowerShell line). The script waits until you have logged in and the scene
has settled, then, for every sky of the scenario, selects the sky, points a fixed camera at the sun's
azimuth, takes a noon snapshot and sweeps the sun through the horizon with a snapshot per step. It
restores your environment and settings at the end and on error. Dev tooling only: nothing here is part
of the viewer.

Modes:  (default) run | --dry-run (check APIs, controls and widget paths, change nothing) |
        --list-skies (print the sky/settings items of your inventory) | --restore RUNDIR.

stdout is the LEAP channel: this script only ever logs to stderr (the viewer copies it into its log).
"""

import argparse
import csv
import datetime as dt
import json
import math
import os
import pathlib
import re
import sys
import time
import uuid

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from leap_client import LeapError, LeapSession, LeapTimeout, ViewerGone  # noqa: E402

REPO_ROOT = HERE.parents[1]
OWNER_REPO = pathlib.Path("C:/Users/annes/source/repos/vulkanstorm")

# Operations the script uses, per API; the dry run checks that the viewer has every one of them.
REQUIRED_OPS = {
    "LLLeap": ["ping", "listen", "getAPIs", "getAPI"],
    "LLStartUp": ["postStartupState", "getStateTable"],
    "LLViewerControl": ["get", "set", "groups"],
    "LLFloaterReg": ["showInstance", "hideInstance", "instanceVisible"],
    "UI": ["getValue", "setSelectedByValue"],
    "LLWindow": ["getPaths", "getInfo", "keyDown", "pasteText", "mouseDown", "mouseUp"],
    "LLViewerWindow": ["saveSnapshot"],
    "LLAgent": ["getPosition", "setCameraParams", "setFollowCamActive", "removeCameraParams"],
    "LLInventory": ["getBasicFolderID", "collectDescendantsIf", "getItemsInfo"],
    "LLStats": ["getPerfData"],
}

PHOTOTOOLS = "phototools"            # floater with the sky combo box (floater_phototools.xml)
PERSONAL = "env_adjust_snapshot"     # Personal Lighting (floater_adjust_environment.xml)
FIXED_SKY = "env_fixed_environmentent_sky"  # fixed sky editor (spelling as in the viewer)
SIGNATURE_WIDGETS = ["sun_azimuth", "sun_elevation", "probe_ambiance", "cloud_coverage", "haze_horizon",
                     "haze_density", "scene_gamma"]


def default_capture_root():
    env = os.environ.get("VKSTORM_CAPTURES")
    if env:
        return pathlib.Path(env)
    if OWNER_REPO.is_dir():
        return OWNER_REPO / ".captures"
    return REPO_ROOT / ".captures"


def as_value(text):
    """A combo box value as the LLSD type the viewer stores: UUID when it parses as one, else the string."""
    try:
        return uuid.UUID(str(text))
    except ValueError:
        return str(text)


def log(msg):
    sys.stderr.write("LIGHTCHECK %s %s\n" % (dt.datetime.now().strftime("%H:%M:%S"), msg))
    sys.stderr.flush()


class Aborted(Exception):
    pass


# ---------------------------------------------------------------------------------------------
# Sweep geometry
# ---------------------------------------------------------------------------------------------

def sweep_elevations(start=10.0, stop=-10.0, step=0.5, fine_step=0.1, fine_half=1.5):
    """Elevations from start to stop (degrees): every `step` from start, plus every `fine_step` inside
    +-fine_half of the horizon. Integer arithmetic in thousandths of a degree: no float drift."""
    unit = 1000
    s0, s1 = round(start * unit), round(stop * unit)
    coarse, fine, half = round(step * unit), round(fine_step * unit), round(fine_half * unit)
    if coarse <= 0 or fine <= 0:
        raise ValueError("steps must be positive")
    direction = -1 if s1 < s0 else 1
    out = []
    cur = s0
    while (s1 - cur) * direction >= 0:
        if abs(cur) <= half or (cur - s0) % coarse == 0:
            out.append(cur / unit)
        cur += direction * fine
    return out


def sun_direction(az_deg, el_deg):
    """Region-coordinate sun direction for the Personal Lighting azimuth/elevation (llfloaterenvironmentadjust.cpp)."""
    az, el = math.radians(az_deg), math.radians(el_deg)
    return (math.cos(el) * math.cos(az), -math.cos(el) * math.sin(az), math.sin(el))


def camera_params(pos, az_deg, pitch_deg, height_m, look_m):
    az, pitch = math.radians(az_deg), math.radians(pitch_deg)
    cam = [pos[0], pos[1], pos[2] + height_m]
    focus = [cam[0] + look_m * math.cos(pitch) * math.cos(az),
             cam[1] - look_m * math.cos(pitch) * math.sin(az),
             cam[2] + look_m * math.sin(pitch)]
    return cam, focus


def horizon_fraction(pitch_deg, fov_deg):
    """Vertical position of the horizon in the frame (0 = top, 1 = bottom) for a camera pitched up by pitch_deg."""
    return 0.5 + 0.5 * math.tan(math.radians(pitch_deg)) / math.tan(math.radians(fov_deg) / 2.0)


def analysis_regions(pitch_deg, fov_deg):
    h = horizon_fraction(pitch_deg, fov_deg)
    clamp = lambda v: max(0.0, min(1.0, v))  # noqa: E731
    return {
        "ground": [0.05, clamp(h + 0.10), 0.95, clamp(h + 0.30)],
        "horizon_sky": [0.05, clamp(h - 0.12), 0.95, clamp(h - 0.02)],
        "cloud": [0.05, clamp(0.08), 0.95, clamp(min(0.35, h - 0.20))],
        "horizon_y": h,
    }  # regions are [x0, y0, x1, y1] fractions of the image


# ---------------------------------------------------------------------------------------------
# Viewer wrapper
# ---------------------------------------------------------------------------------------------

class Viewer:
    def __init__(self, session, abort_file=None):
        self.s = session
        self.abort_file = abort_file
        self._paths = {}

    # -- plumbing -------------------------------------------------------------------------------
    def check_abort(self):
        if self.abort_file and pathlib.Path(self.abort_file).exists():
            raise Aborted("stop file %s found" % self.abort_file)

    def sleep(self, seconds):
        end = time.monotonic() + seconds
        while True:
            self.check_abort()
            left = end - time.monotonic()
            if left <= 0:
                return
            if self.s.closed:
                raise ViewerGone("viewer closed")
            time.sleep(min(0.25, left))

    def req(self, api, op, /, timeout=None, **args):
        pump = self.s.command_pump if api == "LLLeap" else api
        return self.s.request(pump, op, timeout=timeout, **args)

    def fire(self, api, op, /, **args):
        pump = self.s.command_pump if api == "LLLeap" else api
        self.s.fire(pump, op, **args)

    def barrier(self):
        self.s.ping(timeout=60)

    # -- debug settings -------------------------------------------------------------------------
    def get_control(self, key, group="Global"):
        return self.req("LLViewerControl", "get", group=group, key=key)

    def try_control(self, key, group="Global"):
        try:
            return self.get_control(key, group)
        except LeapError:
            return None

    def set_control(self, key, value, group="Global"):
        """Set a control, coerced to its type, and read it back. Returns the value the viewer now holds."""
        cur = self.get_control(key, group)
        typ = str(cur.get("type", ""))
        if typ in ("F32", "F64"):
            value = float(value)
        elif typ in ("S32", "U32"):
            value = int(value)
        elif typ == "Boolean":
            value = bool(value)
        reply = self.req("LLViewerControl", "set", group=group, key=key, value=value)
        got = reply.get("value")
        if isinstance(value, float) and isinstance(got, (int, float)):
            ok = abs(got - value) <= 1e-4 * max(1.0, abs(value))
        else:
            ok = got == value
        if not ok:
            raise LeapError("%s did not take: wanted %r, viewer holds %r" % (key, value, got))
        return got

    # -- floaters and widgets -------------------------------------------------------------------
    def floater_visible(self, name):
        return bool(self.req("LLFloaterReg", "instanceVisible", name=name).get("visible"))

    def show_floater(self, name, key=None, wait=True):
        args = {"name": name}
        if key is not None:
            args["key"] = key
        self.fire("LLFloaterReg", "showInstance", **args)
        self.barrier()
        if wait:
            for _ in range(20):
                if self.floater_visible(name):
                    break
                self.sleep(0.25)
            else:
                raise LeapError("floater %s did not become visible" % name)
        self._paths.pop(name, None)

    def hide_floater(self, name):
        self.fire("LLFloaterReg", "hideInstance", name=name)
        self.barrier()
        self._paths.pop(name, None)

    def floater_paths(self, floater):
        """All view paths under a floater (cached until the floater is shown or hidden again)."""
        if floater not in self._paths:
            reply = self.req("LLWindow", "getPaths", under="//" + floater, timeout=60)
            self._paths[floater] = list(reply.get("paths") or [])
        return self._paths[floater]

    def find_widget(self, floater, name, required=True):
        for p in self.floater_paths(floater):
            if p.rsplit("/", 1)[-1] == name:
                return p
        if required:
            raise LeapError("widget %r not found under floater %r" % (name, floater))
        return None

    def widget_info(self, path):
        return self.req("LLWindow", "getInfo", path=path)

    def value_of(self, path):
        reply = self.req("UI", "getValue", path=path)
        if "value" not in reply:
            raise LeapError("no value at %s (not a control?)" % path)
        return reply["value"]

    def click(self, path):
        a = self.req("LLWindow", "mouseDown", button="LEFT", path=path)
        if a.get("error"):
            raise LeapError(str(a["error"]))
        b = self.req("LLWindow", "mouseUp", button="LEFT", path=path)
        if b.get("error"):
            raise LeapError(str(b["error"]))
        self.barrier()

    def slider_editor(self, slider_path):
        cached = self._paths.get("editor:" + slider_path)
        if cached:
            return cached
        reply = self.req("LLWindow", "getPaths", under=slider_path, timeout=30)
        for p in reply.get("paths") or []:
            if p != slider_path and p.startswith(slider_path + "/"):
                if "LineEditor" in str(self.widget_info(p).get("class", "")):
                    self._paths["editor:" + slider_path] = p
                    return p
        raise LeapError("no text editor child under slider %s" % slider_path)

    def set_slider(self, floater, name, value, tol=0.006):
        """Type a value into a slider's text field and commit it with Enter; verify by reading it back.
        (LEAP has no 'set value' operation, so this is the keyboard route.)"""
        slider = self.find_widget(floater, name)
        editor = self.slider_editor(slider)
        text = ("%.4f" % value).rstrip("0").rstrip(".") if value != int(value) else "%d" % int(value)
        for attempt in range(2):
            self.fire("LLWindow", "keyDown", path=editor, keysym="End")
            for _ in range(14):
                self.fire("LLWindow", "keyDown", path=editor, keysym="Backsp")
            if attempt:
                self.fire("LLWindow", "keyDown", path=editor, char="A", mask=["CTL"])
            self.fire("LLWindow", "pasteText", path=editor, text=text)
            self.fire("LLWindow", "keyDown", path=editor, keysym="Enter")
            self.barrier()
            self.sleep(0.15)
            got = self.value_of(slider)
            if isinstance(got, (int, float)) and abs(float(got) - value) <= tol:
                return float(got)
        raise LeapError("slider %s/%s did not take %s (reads %r)" % (floater, name, text, got))

    # -- scene ----------------------------------------------------------------------------------
    def agent_pos(self):
        r = self.req("LLAgent", "getPosition")
        return [float(x) for x in r["region"]]

    def point_camera(self, az_deg, cam_cfg):
        cam, focus = camera_params(self.agent_pos(), az_deg, cam_cfg["pitch_deg"], cam_cfg["height_m"],
                                   cam_cfg["look_distance_m"])
        self.fire("LLAgent", "setCameraParams", camera_pos=cam, focus_pos=focus, camera_locked=True,
                  focus_locked=True, camera_lag=0.0, focus_lag=0.0, camera_threshold=0.0, focus_threshold=0.0)
        self.barrier()
        return cam, focus

    def release_camera(self):
        self.fire("LLAgent", "setFollowCamActive", active=False)
        self.fire("LLAgent", "removeCameraParams")
        self.barrier()

    def perf(self):
        """(fps, frame_ms) from the viewer's frame recording, or (None, None)."""
        try:
            stats = self.req("LLStats", "getPerfData", timeout=15).get("stats") or {}
            ft = stats.get("frametime") or stats.get("frame_time") or stats
            fps = [x for x in (ft.get("fps") or [])[:30] if isinstance(x, (int, float))]
            ms = [x for x in (ft.get("frame_time_ms") or [])[:30] if isinstance(x, (int, float))]
            return (sum(fps) / len(fps) if fps else None, sum(ms) / len(ms) if ms else None)
        except LeapError:
            return (None, None)

    def snapshot(self, filename, width=None, height=None):
        args = {"filename": filename, "showui": False, "showhud": False, "rebuild": False}
        if width:
            args["width"] = int(width)
        if height:
            args["height"] = int(height)
        r = self.req("LLViewerWindow", "saveSnapshot", timeout=90, **args)
        if not r.get("ok"):
            raise LeapError("saveSnapshot failed for %s" % filename)

    # -- login ----------------------------------------------------------------------------------
    def wait_for_login(self, timeout):
        self.s.listen("StartupState")
        table = self.req("LLStartUp", "getStateTable").get("table") or []
        final = table[-1] if table else "STATE_STARTED"
        self.fire("LLStartUp", "postStartupState")
        last = None
        end = time.monotonic() + timeout
        while True:
            self.check_abort()
            left = end - time.monotonic()
            if left <= 0:
                raise LeapTimeout("login did not finish in %.0f s" % timeout)
            try:
                ev = self.s.wait_event("StartupState", timeout=min(5.0, left))
            except LeapTimeout:
                continue
            state = str(ev.get("str"))
            if state != last:
                log("startup state: %s" % state)
                last = state
            if state == final:
                return state


# ---------------------------------------------------------------------------------------------
# Sky resolution
# ---------------------------------------------------------------------------------------------

def inventory_roots(v, library_root_id):
    roots = []
    for ft in ("settings", "root_inv"):
        try:
            rid = str(v.req("LLInventory", "getBasicFolderID", ft_name=ft).get("id"))
        except LeapError:
            continue
        if rid and rid != "00000000-0000-0000-0000-000000000000":
            roots.append(rid)
    if library_root_id:
        roots.append(library_root_id)
    return roots


def find_settings_items(v, roots, name):
    """Settings items whose name matches `name` (exact, case-insensitive first, then by substring)."""
    exact, partial, seen = [], [], set()
    for root in roots:
        try:
            reply = v.req("LLInventory", "collectDescendantsIf", folder_id=root, name=name, type="settings",
                          timeout=60)
        except LeapError:
            continue
        for item in (reply.get("items") or {}).values():
            aid = str(item.get("asset_id"))
            if aid in seen or aid == "00000000-0000-0000-0000-000000000000":
                continue
            seen.add(aid)
            (exact if str(item.get("name", "")).lower() == name.lower() else partial).append(
                (str(item.get("name")), aid))
    return exact + partial


def list_settings(v, roots):
    out = {}
    for root in roots:
        try:
            reply = v.req("LLInventory", "collectDescendantsIf", folder_id=root, name="", type="settings",
                          timeout=120)
        except LeapError:
            continue
        for item in (reply.get("items") or {}).values():
            out[str(item.get("asset_id"))] = str(item.get("name"))
    return out


# ---------------------------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------------------------

DEFAULTS = {
    "settle": {"post_login_s": 20.0, "after_sky_s": 3.0, "per_step_s": 0.6, "min_frames": 6, "login_timeout_s": 3600},
    "snapshot": {"width": None, "height": None},
    "camera": {"enabled": True, "height_m": 2.5, "pitch_deg": 10.0, "look_distance_m": 200.0},
    "sweep": {"start": 10.0, "stop": -10.0, "step": 0.5, "fine_step": 0.1, "fine_half_width": 1.5},
    "noon_elevation_deg": 66.0,
    "knobs": {},
    "settings": {},
    "pause_clouds": True,
    "library_root_id": "00000112-000f-0000-0000-000100bba000",
    "skies": [],
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
    ids = [s["id"] for s in sc["skies"]]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate sky ids in the scenario")
    for s in sc["skies"]:
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", s["id"]):
            raise ValueError("sky id %r must be a file-name-safe token" % s["id"])
    return sc


def fmt_elev(e):
    return "e%+06.2f" % e


class Run:
    def __init__(self, viewer, scenario, outdir, args):
        self.v = viewer
        self.sc = scenario
        self.out = pathlib.Path(outdir)
        self.args = args
        self.orig_settings = {}
        self.floaters_opened = []
        self.clouds_toggled = False
        self.camera_set = False
        self.cleaned = False
        self.run_info = {"scenario": scenario, "skies": {}, "warnings": []}
        self.manifest_rows = []
        self.csv_file = None
        self.csv = None

    # -- bookkeeping ----------------------------------------------------------------------------
    def warn(self, msg):
        log("WARNING " + msg)
        self.run_info["warnings"].append(msg)

    def save_run_info(self):
        with open(self.out / "run.json", "w", encoding="utf-8") as f:
            json.dump(self.run_info, f, indent=2, default=str)

    def save_restore(self):
        with open(self.out / "restore.json", "w", encoding="utf-8") as f:
            json.dump({"settings": self.orig_settings, "clouds_toggled": self.clouds_toggled,
                       "floaters_opened": self.floaters_opened, "camera_set": self.camera_set}, f, indent=2)

    def remember(self, key):
        if key not in self.orig_settings:
            cur = self.v.try_control(key)
            if cur is None:
                return False
            self.orig_settings[key] = cur.get("value")
            self.save_restore()
        return True

    def change(self, key, value, required=False):
        if not self.remember(key):
            msg = "control %s does not exist in this viewer; skipped" % key
            if required:
                raise LeapError(msg)
            self.warn(msg)
            return False
        self.v.set_control(key, value)
        return True

    # -- setup / teardown -----------------------------------------------------------------------
    def apply_settings(self):
        sc = self.sc
        knob_values = {}
        for key in sorted(sc["knobs"]):
            cur = self.v.try_control(key)
            knob_values[key] = None if cur is None else cur.get("value")
            want = sc["knobs"][key]
            if cur is not None and want is not None:
                self.change(key, want)
                knob_values[key] = want
        for k, val in {**sc["settings"], **self.args.set_values}.items():
            self.change(k, val)
        self.change("FSEnvironmentManualTransitionTime", 0.0)
        self.run_info["knobs"] = knob_values
        cam = self.v.try_control("CameraAngle")
        self.run_info["fov_deg"] = math.degrees(float(cam["value"])) if cam else 60.0

    def cleanup(self):
        """Restore everything we touched. Safe to call twice; never raises."""
        if self.cleaned:
            return
        self.cleaned = True
        log("restoring the viewer")
        v = self.v

        def attempt(what, fn):
            try:
                fn()
            except Exception as exc:  # noqa: BLE001
                log("restore: %s failed: %s" % (what, exc))

        if self.camera_set:
            attempt("camera", v.release_camera)
        for name in (FIXED_SKY, PERSONAL):
            if name in self.floaters_opened:
                attempt("hide " + name, lambda n=name: v.hide_floater(n))
        attempt("environment", self.restore_environment)
        if self.clouds_toggled:
            attempt("clouds", lambda: self.click_named(PHOTOTOOLS, ["PauseClouds"]))
        if PHOTOTOOLS in self.floaters_opened:
            attempt("hide " + PHOTOTOOLS, lambda: v.hide_floater(PHOTOTOOLS))
        for key, val in self.orig_settings.items():
            attempt("setting " + key, lambda k=key, x=val: v.set_control(k, x))

    def click_named(self, floater, names):
        for n in names:
            p = self.v.find_widget(floater, n, required=False)
            if p:
                self.v.click(p)
                return p
        raise LeapError("none of %s found under %s" % (names, floater))

    def restore_environment(self):
        orig = self.run_info.get("original_environment") or {}
        if not self.run_info.get("environment_changed"):
            return
        self.ensure_floater(PHOTOTOOLS)
        sky = orig.get("sky_combo")
        if isinstance(sky, str) and re.fullmatch(r"[0-9a-fA-F-]{36}", sky):
            self.select_sky_asset(sky)
        else:
            self.click_named(PHOTOTOOLS, ["Revert to Region Default", "ResetToRegionDefault"])
        self.v.sleep(1.0)

    def ensure_floater(self, name):
        if not self.v.floater_visible(name):
            self.v.show_floater(name)
            if name not in self.floaters_opened:
                self.floaters_opened.append(name)
                self.save_restore()

    # -- sky selection --------------------------------------------------------------------------
    def combo_path(self):
        return self.v.find_widget(PHOTOTOOLS, "WLPresetsCombo")

    def select_sky_asset(self, asset_id):
        """Select a sky of the Phototools combo box and apply it. setSelectedByValue only moves the highlight (no
        commit), so step Next then Prev: the pair ends on the same preset and applies it."""
        v = self.v
        combo = self.combo_path()
        v.req("UI", "setSelectedByValue", path=combo, value=uuid.UUID(asset_id))
        got = str(v.value_of(combo))
        if got.lower() != asset_id.lower():
            return False
        self.click_named(PHOTOTOOLS, ["WLNextPreset"])
        self.click_named(PHOTOTOOLS, ["WLPrevPreset"])
        v.sleep(0.5)
        return str(v.value_of(combo)).lower() == asset_id.lower()

    def read_signature(self):
        sig = {}
        for n in SIGNATURE_WIDGETS:
            p = self.v.find_widget(PERSONAL, n, required=False)
            if p:
                try:
                    sig[n] = self.v.value_of(p)
                except LeapError:
                    pass
        return sig

    def choose_sky(self, spec):
        """Make the sky of `spec` the current local environment. Returns a description dict."""
        v = self.v
        self.run_info["environment_changed"] = True
        if v.floater_visible(PERSONAL):
            v.hide_floater(PERSONAL)
        self.ensure_floater(PHOTOTOOLS)
        tried = []
        for nm in spec.get("names", []):
            for name, aid in find_settings_items(v, self.roots, nm):
                tried.append("%s=%s" % (name, aid))
                if self.select_sky_asset(aid):
                    return {"how": "inventory", "item": name, "asset_id": aid}
        fb = spec.get("fallback_button")
        if fb:
            self.warn("sky %s: no inventory sky from %s took; using the %s quick-environment button (tried: %s)"
                      % (spec["id"], spec.get("names"), fb, tried))
            self.click_named(PHOTOTOOLS, [fb])
            v.sleep(1.0)
            return {"how": "button", "item": fb}
        raise LeapError("no sky found for %s (names %s, tried %s)" % (spec["id"], spec.get("names"), tried))

    # -- editing a sky --------------------------------------------------------------------------
    def open_editor(self, spec):
        v = self.v
        editor = spec.get("editor", "personal")
        if editor == "fixed":
            if v.floater_visible(PERSONAL):
                v.hide_floater(PERSONAL)
            v.show_floater(FIXED_SKY, key="new")
            if FIXED_SKY not in self.floaters_opened:
                self.floaters_opened.append(FIXED_SKY)
                self.save_restore()
            tab_re = re.compile(spec.get("tab_regex", "sun"), re.I)
            for p in v.floater_paths(FIXED_SKY):
                if tab_re.search(p.rsplit("/", 1)[-1]) and "button" in str(v.widget_info(p).get("class", "")).lower():
                    v.click(p)
                    v._paths.pop(FIXED_SKY, None)
                    break
            return FIXED_SKY
        v.show_floater(PERSONAL)
        if PERSONAL not in self.floaters_opened:
            self.floaters_opened.append(PERSONAL)
            self.save_restore()
        return PERSONAL

    def prepare_sky(self, spec):
        v = self.v
        info = {"selected": None, "signature": None, "controls": {}, "error": None}
        if spec.get("editor", "personal") == "personal":
            info["selected"] = self.choose_sky(spec)
            v.sleep(self.sc["settle"]["after_sky_s"])
        floater = self.open_editor(spec)
        v.sleep(self.sc["settle"]["after_sky_s"])
        info["floater"] = floater
        for name, val in (spec.get("controls") or {}).items():
            info["controls"][name] = v.set_slider(floater, name, float(val))
        if spec.get("azimuth_deg") is not None:
            v.set_slider(floater, "sun_azimuth", float(spec["azimuth_deg"]))
        az = float(v.value_of(v.find_widget(floater, "sun_azimuth")))
        moon = spec.get("moon")
        if moon:
            maz = (az + float(moon.get("azimuth_offset_deg", 180.0))) % 360.0
            v.set_slider(floater, "moon_azimuth", round(maz, 2), tol=0.02)
            v.set_slider(floater, "moon_elevation", float(moon.get("elevation_deg", 30.0)))
            info["moon"] = {"azimuth": maz, "elevation": float(moon.get("elevation_deg", 30.0))}
        info["azimuth_deg"] = az
        info["signature"] = self.read_signature() if floater == PERSONAL else {}
        return floater, az, info

    # -- capture --------------------------------------------------------------------------------
    def capture(self, spec, step, kind, elev, floater, az):
        v = self.v
        got = v.set_slider(floater, "sun_elevation", elev, tol=0.006)
        fps, ms = v.perf()
        wait = self.sc["settle"]["per_step_s"]
        if fps and fps > 0:
            wait = max(wait, self.sc["settle"]["min_frames"] / fps)
        v.sleep(wait)
        fps, ms = v.perf()
        base = "%s_%03d_%s" % (spec["id"], step, fmt_elev(elev))
        bmp = self.out / "snapshots" / (base + ".bmp")
        v.snapshot(str(bmp).replace("\\", "/"), self.sc["snapshot"].get("width"), self.sc["snapshot"].get("height"))
        final = self.finish_image(bmp)
        row = {"sky": spec["id"], "step": step, "kind": kind, "sun_elevation_deg": elev,
               "sun_elevation_readback": got, "sun_azimuth_deg": az,
               "snapshot": final.relative_to(self.out).as_posix(),
               "timestamp_utc": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z",
               "fps": "" if fps is None else "%.2f" % fps, "frame_ms": "" if ms is None else "%.3f" % ms,
               "settings": json.dumps({"knobs": self.run_info.get("knobs", {}),
                                       "controls": (spec.get("controls") or {})}, sort_keys=True)}
        self.csv.writerow(row)
        self.csv_file.flush()
        self.manifest_rows.append(row)
        return row

    def finish_image(self, bmp):
        """The viewer's LEAP saveSnapshot writes BMP data whatever the name; convert to PNG when Pillow is there."""
        if not bmp.exists():
            raise LeapError("snapshot file missing: %s" % bmp)
        try:
            from PIL import Image  # noqa: PLC0415
        except ImportError:
            self.warn("Pillow not installed: snapshots stay .bmp")
            return bmp
        png = bmp.with_suffix(".png")
        with Image.open(bmp) as im:
            im.convert("RGB").save(png, optimize=False)
        bmp.unlink()
        return png

    # -- main -----------------------------------------------------------------------------------
    def go(self):
        v, sc = self.v, self.sc
        (self.out / "snapshots").mkdir(parents=True, exist_ok=True)
        self.csv_file = open(self.out / "manifest.csv", "w", newline="", encoding="utf-8")
        cols = ["sky", "step", "kind", "sun_elevation_deg", "sun_elevation_readback", "sun_azimuth_deg", "snapshot",
                "timestamp_utc", "fps", "frame_ms", "settings"]
        self.csv = csv.DictWriter(self.csv_file, fieldnames=cols)
        self.csv.writeheader()
        started = dt.datetime.now(dt.timezone.utc)
        self.run_info.update({"started_utc": started.isoformat(), "script": str(pathlib.Path(__file__).resolve()),
                              "args": vars(self.args) | {"set_values": self.args.set_values}})
        self.save_run_info()
        try:
            self.roots = inventory_roots(v, sc["library_root_id"])
            self.run_info["inventory_roots"] = self.roots
            self.wait_for_inventory()
            self.snapshot_original_state()
            self.apply_settings()
            self.save_run_info()
            if sc["pause_clouds"]:
                self.pause_clouds()
            skies = [s for s in sc["skies"] if not self.args.only or s["id"] in self.args.only]
            elevs = sweep_elevations(sc["sweep"]["start"], sc["sweep"]["stop"], sc["sweep"]["step"],
                                     sc["sweep"]["fine_step"], sc["sweep"]["fine_half_width"])
            if self.args.step_limit:
                elevs = elevs[:self.args.step_limit]
            self.run_info["sweep_elevations"] = elevs
            for spec in skies:
                self.run_sky(spec, elevs)
                self.save_run_info()
        finally:
            try:
                self.run_info["finished_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
                self.csv_file.close()
                self.save_run_info()
            finally:
                self.cleanup()

    def wait_for_inventory(self):
        for _ in range(12):
            if list_settings(self.v, self.roots):
                return
            log("no settings items in the inventory yet; waiting for the inventory to load")
            self.v.sleep(10.0)
        self.warn("the inventory shows no settings items; skies fall back to the quick-environment buttons")

    def snapshot_original_state(self):
        v = self.v
        orig = {}
        if v.floater_visible(PHOTOTOOLS):
            orig["phototools_open"] = True
        else:
            self.ensure_floater(PHOTOTOOLS)
        combo = self.combo_path()
        orig["sky_combo"] = str(v.value_of(combo))
        for key, widget in (("water_combo", "WaterPresetsCombo"), ("daycycle_combo", "DCPresetsCombo")):
            w = v.find_widget(PHOTOTOOLS, widget, required=False)
            try:
                orig[key] = str(v.value_of(w)) if w else None
            except LeapError:
                orig[key] = None
        orig["personal_open"] = v.floater_visible(PERSONAL)
        self.run_info["original_environment"] = orig
        log("original environment: %s" % orig)

    def pause_clouds(self):
        p = self.v.find_widget(PHOTOTOOLS, "PauseClouds", required=False)
        if not p:
            self.warn("PauseClouds button not found; clouds keep drifting during the sweeps")
            return
        try:
            already = bool(self.v.value_of(p))
        except LeapError:
            already = False
        if not already:
            self.v.click(p)
            self.clouds_toggled = True
            self.save_restore()
        self.run_info["clouds_paused_by_script"] = not already

    def run_sky(self, spec, elevs):
        v, sc = self.v, self.sc
        sid = spec["id"]
        entry = {"label": spec.get("label"), "why": spec.get("why"), "status": "started"}
        self.run_info["skies"][sid] = entry
        log("sky %s: preparing" % sid)
        try:
            floater, az, info = self.prepare_sky(spec)
            entry.update(info)
            if sc["camera"]["enabled"]:
                cam, focus = v.point_camera(az, sc["camera"])
                self.camera_set = True
                self.save_restore()
                entry["camera"] = {"position": cam, "focus": focus}
                self.run_info["regions"] = analysis_regions(sc["camera"]["pitch_deg"], self.run_info.get("fov_deg", 60.0))
            v.sleep(sc["settle"]["after_sky_s"])
            step = 0
            if spec.get("noon", True):
                self.capture(spec, step, "noon", float(sc["noon_elevation_deg"]), floater, az)
            for e in elevs if spec.get("sweep", True) else []:
                step += 1
                try:
                    self.capture(spec, step, "sweep", e, floater, az)
                except LeapTimeout:
                    log("timeout at %s %+.2f; retrying once" % (sid, e))
                    self.capture(spec, step, "sweep", e, floater, az)
            entry["status"] = "ok"
            entry["steps"] = step
        except (Aborted, ViewerGone):
            entry["status"] = "aborted"
            raise
        except Exception as exc:  # noqa: BLE001 - a failed sky must not lose the others
            entry["status"] = "failed"
            entry["error"] = str(exc)
            if spec.get("optional"):
                self.warn("optional sky %s failed: %s" % (sid, exc))
            else:
                self.warn("sky %s FAILED: %s" % (sid, exc))
        finally:
            for name in (FIXED_SKY,):
                if name in self.floaters_opened and spec.get("editor") == "fixed":
                    try:
                        v.hide_floater(name)
                    except LeapError:
                        pass


# ---------------------------------------------------------------------------------------------
# Dry run
# ---------------------------------------------------------------------------------------------

class Report:
    def __init__(self):
        self.rows = []

    def add(self, name, ok, detail=""):
        self.rows.append((name, bool(ok), detail))
        log("%-4s %s %s" % ("PASS" if ok else "FAIL", name, detail))

    @property
    def failed(self):
        return [r for r in self.rows if not r[1]]


def dry_run(v, sc, args, outdir):
    rep = Report()
    # 1. APIs and operations
    apis = v.req("LLLeap", "getAPIs")
    for api, ops in REQUIRED_OPS.items():
        if api == "LLLeap":
            continue
        if api not in apis:
            rep.add("api %s" % api, False, "not registered")
            continue
        try:
            detail = v.req("LLLeap", "getAPI", api=api)
            blob = json.dumps(detail.get("ops"), default=str)
            missing = [o for o in ops if '"%s"' % o not in blob]
            rep.add("api %s" % api, not missing, ("missing ops: %s" % missing) if missing else "ops ok")
        except LeapError as exc:
            rep.add("api %s" % api, False, str(exc))
    # 2. controls
    groups = v.req("LLViewerControl", "groups").get("groups") or []
    rep.add("control group Global", "Global" in groups, str(groups))
    for key in list(sc["knobs"]) + ["CameraAngle", "FSEnvironmentManualTransitionTime"]:
        cur = v.try_control(key)
        rep.add("control %s" % key, cur is not None, "" if cur is None else "%s = %r" % (cur.get("type"), cur.get("value")))
    # 3. login-dependent parts
    if args.no_login_wait:
        log("dry run before login: floaters, inventory and agent checks skipped")
    else:
        try:
            pos = v.agent_pos()
            rep.add("agent position", len(pos) == 3 and any(pos), str(pos))
        except Exception as exc:  # noqa: BLE001
            rep.add("agent position", False, str(exc))
        roots = inventory_roots(v, sc["library_root_id"])
        rep.add("inventory roots", bool(roots), str(roots))
        opened = []
        try:
            for fl, widgets in ((PHOTOTOOLS, ["WLPresetsCombo", "WLPrevPreset", "WLNextPreset", "PauseClouds"]),
                                (PERSONAL, ["sun_azimuth", "sun_elevation", "moon_azimuth", "moon_elevation", "probe_ambiance",
                                            "cloud_coverage"])):
                was = v.floater_visible(fl)
                try:
                    v.show_floater(fl)
                    if not was:
                        opened.append(fl)
                    rep.add("floater %s opens" % fl, True)
                except LeapError as exc:
                    rep.add("floater %s opens" % fl, False, str(exc))
                    continue
                for w in widgets:
                    p = v.find_widget(fl, w, required=False)
                    rep.add("widget %s/%s" % (fl, w), p is not None, p or "")
                    if p and fl == PERSONAL and w in ("sun_azimuth", "sun_elevation", "probe_ambiance"):
                        try:
                            rep.add("slider editor %s" % w, bool(v.slider_editor(p)), v.slider_editor(p))
                            rep.add("slider value %s" % w, True, repr(v.value_of(p)))
                        except LeapError as exc:
                            rep.add("slider editor %s" % w, False, str(exc))
            ph = v.find_widget(PHOTOTOOLS, "Sunset", required=False)
            rep.add("quick-environment button Sunset", ph is not None, ph or "")
            rg = [n for n in ("Revert to Region Default", "ResetToRegionDefault") if v.find_widget(PHOTOTOOLS, n, False)]
            rep.add("region-default button", bool(rg), str(rg))
            # skies
            combo = v.find_widget(PHOTOTOOLS, "WLPresetsCombo", required=False)
            before = str(v.value_of(combo)) if combo else None
            for spec in sc["skies"]:
                found = []
                for nm in spec.get("names", []):
                    for name, aid in find_settings_items(v, roots, nm):
                        v.req("UI", "setSelectedByValue", path=combo, value=uuid.UUID(aid))
                        in_combo = str(v.value_of(combo)).lower() == aid.lower()
                        found.append("%s [%s]%s" % (name, aid, "" if in_combo else " (not a sky of the combo)"))
                        if in_combo:
                            break
                good = any("not a sky" not in f for f in found)
                rep.add("sky %s" % spec["id"], good or bool(spec.get("fallback_button")) or bool(spec.get("optional")),
                        "; ".join(found) if found else "no inventory item from %s; fallback button %s" % (
                            spec.get("names"), spec.get("fallback_button")))
            if combo and before:
                try:
                    v.req("UI", "setSelectedByValue", path=combo, value=as_value(before))
                except LeapError:
                    pass
        finally:
            for fl in opened:
                try:
                    v.hide_floater(fl)
                except LeapError:
                    pass
    out = pathlib.Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    with open(out / "dry_run.json", "w", encoding="utf-8") as f:
        json.dump([{"check": n, "ok": ok, "detail": d} for n, ok, d in rep.rows], f, indent=2, default=str)
    log("dry run: %d checks, %d failed; details in %s" % (len(rep.rows), len(rep.failed), out / "dry_run.json"))
    return 1 if rep.failed else 0


def restore_run(v, args):
    rj = pathlib.Path(args.restore) / "restore.json"
    data = json.loads(rj.read_text(encoding="utf-8"))
    for key, val in data.get("settings", {}).items():
        try:
            v.set_control(key, val)
            log("restored %s = %r" % (key, val))
        except LeapError as exc:
            log("could not restore %s: %s" % (key, exc))
    try:
        v.release_camera()
    except LeapError as exc:
        log("camera: %s" % exc)
    log("settings and camera restored. If the sky is still not yours, use Phototools > Revert to Region Default.")
    return 0


# ---------------------------------------------------------------------------------------------
# Viewer log excerpt (best effort)
# ---------------------------------------------------------------------------------------------

def find_viewer_log(hint=None):
    if hint:
        p = pathlib.Path(hint)
        return p if p.is_file() else None
    best = None
    for base in (os.environ.get("APPDATA"), os.environ.get("LOCALAPPDATA")):
        if not base:
            continue
        for p in pathlib.Path(base).glob("*/logs/*.log"):
            try:
                m = p.stat().st_mtime
            except OSError:
                continue
            if time.time() - m < 600 and (best is None or m > best[0]):
                best = (m, p)
    return best[1] if best else None


def parse_args(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scenario", default=str(HERE / "scenarios" / "l3_at1_handover.json"))
    ap.add_argument("--out", default=None, help="run folder (default: .captures/<date-time>)")
    ap.add_argument("--dry-run", action="store_true", help="check APIs, controls and widget paths; change nothing")
    ap.add_argument("--list-skies", action="store_true", help="print the settings items (skies, water, day cycles) of the inventory")
    ap.add_argument("--restore", default=None, metavar="RUNDIR", help="re-apply the settings saved in RUNDIR/restore.json")
    ap.add_argument("--no-login-wait", action="store_true", help="do not wait for login (dry run: API checks only)")
    ap.add_argument("--only", default=None, help="comma-separated sky ids to run")
    ap.add_argument("--step-limit", type=int, default=0, help="only the first N sweep steps (quick test)")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                    help="set a debug setting for the run (restored afterwards), e.g. RenderAtmospherePhysicalLight=false")
    ap.add_argument("--viewer-log", default=None, help="path of the viewer log, for the excerpt")
    ap.add_argument("--max-wait-s", type=float, default=None, help="override the login timeout")
    args = ap.parse_args(argv)
    args.only = [s for s in (args.only or "").split(",") if s]
    vals = {}
    for item in args.set:
        k, _, val = item.partition("=")
        if not k or not _:
            ap.error("--set needs KEY=VALUE")
        low = val.lower()
        vals[k] = True if low == "true" else False if low == "false" else (
            int(val) if re.fullmatch(r"-?\d+", val) else float(val) if re.fullmatch(r"-?\d+\.\d*([eE][-+]?\d+)?", val) else val)
    args.set_values = vals
    return args


def execute(session, args, sc, outdir):
    """Everything after the LEAP handshake. Returns the process exit code."""
    outdir = pathlib.Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    v = Viewer(session, abort_file=str(outdir / "STOP"))
    log("connected; command pump %s; run folder %s" % (session.command_pump, outdir))
    code = 0
    log_start = None
    vlog = find_viewer_log(args.viewer_log)
    if vlog:
        log_start = vlog.stat().st_size
    run = None
    try:
        session.ping()
        if args.restore:
            return restore_run(v, args)
        if not args.no_login_wait:
            log("waiting for login (log in by hand; the script starts when the scene is up)")
            v.wait_for_login(args.max_wait_s or sc["settle"]["login_timeout_s"])
            log("logged in; settling %.0f s" % sc["settle"]["post_login_s"])
            v.sleep(sc["settle"]["post_login_s"])
        if args.list_skies:
            roots = inventory_roots(v, sc["library_root_id"])
            items = list_settings(v, roots)
            for aid, name in sorted(items.items(), key=lambda kv: kv[1].lower()):
                log("settings item: %-48s %s" % (name, aid))
            (outdir / "settings_items.json").write_text(json.dumps(items, indent=2), encoding="utf-8")
            return 0
        if args.dry_run:
            return dry_run(v, sc, args, outdir)
        (outdir / "scenario.json").write_text(json.dumps(sc, indent=2), encoding="utf-8")
        run = Run(v, sc, outdir, args)
        run.go()
        log("done: %d snapshots in %s" % (len(run.manifest_rows), outdir))
        log("next: python %s %s" % (HERE / "analyze_light_check.py", outdir))
    except Aborted as exc:
        log("aborted: %s" % exc)
        code = 2
    except ViewerGone as exc:
        log("viewer connection lost: %s (settings could not be restored by the script; see restore.json)" % exc)
        code = 3
    except Exception as exc:  # noqa: BLE001
        log("FAILED: %s" % exc)
        code = 1
        if run is not None:
            run.cleanup()
    finally:
        if vlog and log_start is not None:
            try:
                with open(vlog, "rb") as f:
                    f.seek(log_start)
                    (outdir / "viewer_log_excerpt.txt").write_bytes(f.read())
            except OSError as exc:
                log("log excerpt not written: %s" % exc)
    return code


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    sc = load_scenario(args.scenario)
    outdir = pathlib.Path(args.out) if args.out else default_capture_root() / dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    outdir.mkdir(parents=True, exist_ok=True)
    session = LeapSession(sys.stdin.buffer, sys.stdout.buffer, log=log).start()
    return execute(session, args, sc, outdir)


if __name__ == "__main__":
    code = main()
    sys.stderr.flush()
    sys.stdout.flush()
    # Leave without interpreter finalisation: the reader thread is blocked in a read of stdin (daemon thread,
    # holding the buffer lock), which makes a normal exit abort with a fatal error on Windows.
    os._exit(code)
