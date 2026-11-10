# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""In-world light check driven over LEAP (the viewer's --leap plugin interface).

Start it from the viewer with --leap (see README.md for the exact PowerShell lines). The script waits until you
have logged in and the scene has settled, then, for every sky of the scenario, selects the sky, points a fixed
camera at the sun's azimuth, takes a noon snapshot and sweeps the sun through the horizon with a snapshot per
step. It restores your environment and settings at the end and on error. Dev tooling only: nothing here is part
of the viewer.

Modes:  (default) run | --dry-run (read-only check of the APIs, settings and widget paths; --deep also opens
        Personal Lighting and the fixed sky editor and restores the environment afterwards) |
        --list-skies (print the settings items of your inventory) | --restore RUNDIR.

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
import subprocess
import sys
import time
import traceback
import uuid

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from leap_client import ConnectionLost, LeapError, LeapSession, LeapTimeout, ProtocolError, ViewerGone  # noqa: E402

REPO_ROOT = HERE.parents[1]

# Operations the script uses, per API. Checked by the dry run and at the start of every real run: an operation
# the viewer does not have is refused with a clear message instead of being sent.
REQUIRED_OPS = {
    "LLStartUp": ["postStartupState", "getStateTable"],
    "LLViewerControl": ["get", "set", "groups"],
    "LLFloaterReg": ["showInstance", "hideInstance", "instanceVisible", "clickButton"],
    "UI": ["getValue", "setSelectedByValue"],
    "LLWindow": ["getPaths", "getInfo", "keyDown", "pasteText", "mouseDown", "mouseUp"],
    "LLViewerWindow": ["saveSnapshot"],
    "LLAgent": ["getPosition", "setCameraParams", "setFollowCamActive", "removeCameraParams"],
    "LLInventory": ["getBasicFolderID", "collectDescendantsIf", "getItemsInfo"],
}
OPTIONAL_OPS = {"LLStats": ["getPerfData"]}      # needed only with record_fps

PHOTOTOOLS = "phototools"            # floater with the sky combo box (floater_phototools.xml)
PERSONAL = "env_adjust_snapshot"     # Personal Lighting (floater_adjust_environment.xml)
FIXED_SKY = "env_fixed_environmentent_sky"  # fixed sky editor (spelling as in the viewer)
ENV_TAB_BUTTON = "htab_EnvironmentTab"      # tab button of the Phototools Environment tab
FIXED_TAB_BUTTONS = ["htab_panel_settings_sky_hbodies", "htab_panel_fs_settings_sky_hbodies"]
REGION_BUTTONS = ["Revert to Region Default", "ResetToRegionDefault"]
REGION, DAYCYCLE, NOCHOICE = "__Regiondefault__", "__Day_Cycle__", "__None__"   # quickprefs.h
SIGNATURE_WIDGETS = ["sun_azimuth", "sun_elevation", "probe_ambiance", "cloud_coverage", "haze_horizon",
                     "haze_density", "scene_gamma"]
BASE_SIGNATURE = ["sun_azimuth", "sun_elevation", "cloud_coverage", "haze_horizon", "haze_density", "scene_gamma"]
UUID_RE = re.compile(r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}")
NULL_UUID = "00000000-0000-0000-0000-000000000000"


def default_capture_root():
    """VKSTORM_CAPTURES, else <main checkout>/.captures (found through git, so a worktree uses the main checkout's),
    else <this checkout>/.captures. All are git-ignored."""
    env = os.environ.get("VKSTORM_CAPTURES")
    if env:
        return pathlib.Path(env)
    try:
        common = subprocess.run(["git", "-C", str(REPO_ROOT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                capture_output=True, text=True, timeout=10).stdout.strip()
        if common:
            return pathlib.Path(common).parent / ".captures"
    except (OSError, subprocess.SubprocessError):
        pass
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


def write_json_atomic(path, obj):
    path = pathlib.Path(path)
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2, default=str)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


class Aborted(Exception):
    pass


class NotRestorable(LeapError):
    pass


# ---------------------------------------------------------------------------------------------
# Geometry
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


def camera_params(pos, az_deg, pitch_deg, height_m, look_m):
    """Camera and focus points (region coordinates) for a camera looking along the Personal Lighting azimuth
    (azimuth turns clockwise from east: direction (cos az, -sin az), see llfloaterenvironmentadjust.cpp)."""
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
                raise self.s.gone("the next step")
            time.sleep(min(0.25, left))

    def req(self, api, op, /, timeout=None, **args):
        pump = self.s.command_pump if api == "LLLeap" else api
        return self.s.request(pump, op, timeout=timeout, **args)

    def fire(self, api, op, /, **args):
        pump = self.s.command_pump if api == "LLLeap" else api
        self.s.fire(pump, op, **args)

    def fire_scalar(self, api, op, /, **args):
        """Like fire() for an operation whose success reply is a bare non-map value (see LeapSession.fire)."""
        pump = self.s.command_pump if api == "LLLeap" else api
        self.s.fire(pump, op, scalar_reply=True, **args)

    def select_by_value(self, path, value):
        """UI.setSelectedByValue moves a combo highlight. The viewer answers with a bare boolean (false when the combo
        or the value is not found), which LLReqID::stamp cannot give a reqid, so request() would wait for a reply that
        never matches: fire it and wait for a ping instead. A refusal is therefore NOT reported here; callers read the
        combo back with value_of."""
        self.fire_scalar("UI", "setSelectedByValue", path=path, value=value)
        self.barrier()

    def barrier(self):
        """Everything sent before has been handled; raises if a fire-and-forget operation reported an error."""
        self.s.ping(timeout=60)
        self.s.settle()
        errs = self.s.take_errors()
        if errs:
            raise LeapError("viewer reported: " + "; ".join(errs[:3]))

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
        self.forget(name)

    def hide_floater(self, name):
        self.fire("LLFloaterReg", "hideInstance", name=name)
        self.barrier()
        self.forget(name)

    def forget(self, floater):
        """Drop the cached widget paths of a floater (they change when it is shown, hidden or switches tab)."""
        for k in [k for k in self._paths if (isinstance(k, tuple) and k[1] == floater)]:
            del self._paths[k]
        self._paths.pop("info", None)

    def find_widget(self, floater, name, required=True):
        """Absolute path of the first view called `name` under `floater`: LLWindow.getPaths on "//floater//name" lists
        only that widget's subtree (a few paths), unlike getPaths over a whole floater (100 KB or more) or getInfo on a
        combo box (LLComboBox::addInfo lists every item). Cached until the floater is shown or hidden."""
        key = ("widget", floater, name)
        if key in self._paths:
            return self._paths[key]
        try:
            paths = self.req("LLWindow", "getPaths", under="//%s//%s" % (floater, name)).get("paths") or []
        except LeapError:
            paths = []
        path = str(paths[0]) if paths else None
        if path:
            self._paths[key] = path
        elif required:
            raise LeapError("widget %r not found under floater %r" % (name, floater))
        return path

    def widget_info(self, path):
        """getInfo of a SMALL widget (button, slider, text field). Never for a combo box: its reply lists every item."""
        if path.rsplit("/", 1)[-1].endswith("Combo"):
            raise LeapError("getInfo on a combo box is a big reply; not done (%s)" % path)
        cache = self._paths.setdefault("info", {})
        if path not in cache:
            cache[path] = self.req("LLWindow", "getInfo", path=path)
        return cache[path]

    def value_of(self, path):
        reply = self.req("UI", "getValue", path=path)
        if "value" not in reply:
            raise LeapError("no value at %s (not a control?)" % path)
        return reply["value"]

    def mouse_click(self, path):
        for op in ("mouseDown", "mouseUp"):
            r = self.req("LLWindow", op, button="LEFT", path=path)
            if r.get("handled") is False:
                raise LeapError("%s on %s was not handled by the widget" % (op, path))
        self.barrier()

    def press(self, floater, name):
        """Click the button `name` of `floater`: LLFloaterReg.clickButton (calls the button's commit) first, mouse
        events on the button's path if that is refused."""
        path = self.find_widget(floater, name)
        try:
            self.req("LLFloaterReg", "clickButton", name=floater, button=name)
            self.barrier()
        except LeapError as exc:
            log("clickButton %s/%s refused (%s); using mouse events" % (floater, name, exc))
            self.mouse_click(path)

    def press_first(self, floater, names):
        for n in names:
            if self.find_widget(floater, n, required=False):
                self.press(floater, n)
                return n
        raise LeapError("none of %s found under %s" % (names, floater))

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
        got = None
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
        """(fps, frame_ms) from the viewer's frame recording, or (None, None). LLStats.getPerfData replies with
        about 200 values for each of a dozen statistics (tens of KB), which the viewer's Windows pipe writer can
        corrupt: it is only called when the scenario sets record_fps."""
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


def check_apis(v, need_stats=False):
    """[(api, ok, detail)] for every API and operation in REQUIRED_OPS."""
    out = []
    for api, ops in {**REQUIRED_OPS, **(OPTIONAL_OPS if need_stats else {})}.items():
        try:
            detail = v.req("LLLeap", "getAPI", api=api)
            blob = json.dumps(detail.get("ops"), default=str)
            missing = [o for o in ops if '"%s"' % o not in blob]
            out.append((api, not missing, ("missing ops: %s" % missing) if missing else "ops ok"))
        except LeapError as exc:
            out.append((api, False, str(exc)))
    return out


# ---------------------------------------------------------------------------------------------
# Sky resolution
# ---------------------------------------------------------------------------------------------

def inventory_roots(v, library_root_id):
    """(roots, user_roots): folders to search, and the user's own. The inventory root contains the settings folder and
    every other folder, so it is the one user root (searching the settings folder as well would return its items twice, and
    settings items may be anywhere); the settings folder is used only if the root cannot be found. The library comes last."""
    user = []
    for ft in ("root_inv", "settings"):
        try:
            rid = str(v.req("LLInventory", "getBasicFolderID", ft_name=ft).get("id"))
        except LeapError:
            continue
        if rid and rid != NULL_UUID:
            user.append(rid)
            break
    roots = list(user)
    if library_root_id:
        roots.append(library_root_id)
    return roots, user


ITEM_LIMITS = (25, 100, 400)    # replies of a few hundred bytes per item: grow only when the first one is full


def find_settings_items(v, roots, name):
    """Settings items whose name contains `name`: exact matches (case-sensitive, then case-insensitive) first, then the
    rest. The viewer's filter is a substring match with no exact option and no paging, and "Default" or "Sunset" are inside
    dozens of other names, so the exact item can fall outside a small limit: when a reply is full and has no exact match the
    query is repeated with a larger limit (ITEM_LIMITS). Folders are searched in order, stopping at the first with an exact
    match."""
    exact_cs, exact_ci, partial, seen = [], [], [], set()
    for root in roots:
        found_exact = False
        for limit in ITEM_LIMITS:
            try:
                reply = v.req("LLInventory", "collectDescendantsIf", folder_id=root, name=name, type="settings",
                              limit=limit, timeout=60)
            except LeapError:
                break
            items = list((reply.get("items") or {}).values())
            for item in items:
                aid = str(item.get("asset_id"))
                iname = str(item.get("name", ""))
                if aid in seen or aid == NULL_UUID:
                    continue
                seen.add(aid)
                if iname == name:
                    exact_cs.append((iname, aid))
                    found_exact = True
                elif iname.lower() == name.lower():
                    exact_ci.append((iname, aid))
                    found_exact = True
                else:
                    partial.append((iname, aid))
            if found_exact or len(items) < limit:
                break
            log("inventory: %d items contain %r and none is exact; asking again with a larger limit" % (len(items), name))
        if found_exact:
            break
    return exact_cs + exact_ci + partial


def list_settings(v, roots, name=""):
    out = {}
    for root in roots:
        try:
            reply = v.req("LLInventory", "collectDescendantsIf", folder_id=root, name=name, type="settings",
                          limit=ITEM_LIMITS[0], timeout=120)
        except LeapError:
            continue
        for item in (reply.get("items") or {}).values():
            out[str(item.get("asset_id"))] = str(item.get("name"))
    return out


def plan_restore(orig):
    """How to put the original environment back, from the Phototools combo values recorded at the start; None if
    the original selection cannot be restored faithfully (then the run refuses to start)."""
    sky, water, dc = orig.get("sky_combo"), orig.get("water_combo"), orig.get("daycycle_combo")
    is_id = lambda x: isinstance(x, str) and bool(UUID_RE.fullmatch(x))  # noqa: E731
    if orig.get("personal_open") or orig.get("fixed_open"):
        return None     # an editor is open: a local edit may be in progress, and it has no asset to select again
    # "Use Shared Environment" (llviewermenu.cpp) clears the local layer and selects it, so FloaterQuickPrefs::
    # setSelectedEnvironment finds no day cycle and no fixed sky: the combos read REGION/REGION/__None__. Phototools'
    # own button gives REGION/REGION/REGION. Both are the shared environment, restored with setSharedEnvironment (the
    # "Revert to Region Default" button). A local sky edited without a saved asset reads the same: the combos cannot tell.
    if sky == REGION and water == REGION and dc in (REGION, NOCHOICE):
        return {"kind": "region"}
    if is_id(sky) and (is_id(water) or water in (REGION, DAYCYCLE)) and dc == NOCHOICE:
        return {"kind": "sky", "sky": sky, "water": water if is_id(water) else None}
    if sky == DAYCYCLE and is_id(dc) and (is_id(water) or water == DAYCYCLE):
        return {"kind": "daycycle", "dc": dc, "water": water if is_id(water) else None}
    return None


# ---------------------------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------------------------

DEFAULTS = {
    "settle": {"post_login_s": 20.0, "after_sky_s": 3.0, "per_step_s": 0.6, "min_frames": 6, "login_timeout_s": 3600},
    "snapshot": {"width": None, "height": None},
    "camera": {"enabled": True, "height_m": 2.5, "pitch_deg": 10.0, "look_distance_m": 200.0},
    "sweep": {"start": 10.0, "stop": -10.0, "step": 0.5, "fine_step": 0.1, "fine_half_width": 1.5},
    "noon_elevation_deg": 66.0,
    "record_fps": False,
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
        self.roots = []
        self.user_roots = []
        self.orig_settings = {}
        self.floaters_opened = []
        self.clouds_toggled = False
        self.camera_set = False
        self.cleaned = False
        self.plan = None
        self.prev_base = None     # (names, base signature) of the previous sky, to verify that a sky took
        self.failed_required = []
        self.run_info = {"scenario": scenario, "skies": {}, "warnings": []}
        self.manifest_rows = []
        self.csv_file = None
        self.csv = None

    # -- bookkeeping ----------------------------------------------------------------------------
    def warn(self, msg):
        log("WARNING " + msg)
        self.run_info["warnings"].append(msg)

    def save_run_info(self):
        write_json_atomic(self.out / "run.json", self.run_info)

    def save_restore(self):
        # settings and camera_set are what --restore uses; the other fields are for the reader
        write_json_atomic(self.out / "restore.json",
                          {"settings": self.orig_settings, "camera_set": self.camera_set,
                           "clouds_toggled": self.clouds_toggled, "floaters_opened": self.floaters_opened,
                           "original_environment": self.run_info.get("original_environment")})

    def remember(self, key):
        if key not in self.orig_settings:
            cur = self.v.try_control(key)
            if cur is None:
                return False
            self.orig_settings[key] = cur.get("value")
            self.save_restore()
        return True

    def change(self, key, value):
        if not self.remember(key):
            self.warn("control %s does not exist in this viewer; skipped" % key)
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
            if cur is None:
                self.warn("knob %s does not exist in this viewer (a baseline viewer?); skipped" % key)
                continue
            want = sc["knobs"][key]
            if want is not None:
                self.change(key, want)
                knob_values[key] = want
        for k, val in {**sc["settings"], **self.args.set_values}.items():
            self.change(k, val)
        self.change("FSEnvironmentManualTransitionTime", 0.0)
        self.run_info["knobs"] = knob_values
        cam = self.v.try_control("CameraAngle")
        self.run_info["fov_deg"] = math.degrees(float(cam["value"])) if cam else 60.0

    def cleanup(self):
        """Restore everything we touched. Safe to call twice; never raises. A stop file no longer applies."""
        if self.cleaned:
            return
        self.cleaned = True
        self.v.abort_file = None
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
            attempt("clouds", self.unpause_clouds)
        if PHOTOTOOLS in self.floaters_opened:
            attempt("hide " + PHOTOTOOLS, lambda: v.hide_floater(PHOTOTOOLS))
        for key, val in self.orig_settings.items():
            attempt("setting " + key, lambda k=key, x=val: v.set_control(k, x))

    def unpause_clouds(self):
        p = self.v.find_widget(PHOTOTOOLS, "PauseClouds", required=False)
        if not p:
            raise LeapError("PauseClouds button not found")
        if bool(self.v.value_of(p)):       # only toggle it back if it is paused now
            self.v.press(PHOTOTOOLS, "PauseClouds")
        else:
            self.warn("clouds were toggled by the script but the button does not read as paused; left alone")

    # -- environment ----------------------------------------------------------------------------
    def ensure_floater(self, name):
        if not self.v.floater_visible(name):
            self.v.show_floater(name)
            if name not in self.floaters_opened:
                self.floaters_opened.append(name)
                self.save_restore()

    def select_combo(self, combo, prev_button, next_button, value):
        """Select `value` in a Phototools combo box and apply it. setSelectedByValue only moves the highlight (no
        commit), so step Next then Prev: the pair ends on the same preset and applies it."""
        v = self.v
        path = v.find_widget(PHOTOTOOLS, combo)
        v.select_by_value(path, as_value(value))
        if str(v.value_of(path)).lower() != str(value).lower():
            return False
        v.press(PHOTOTOOLS, next_button)
        v.press(PHOTOTOOLS, prev_button)
        v.sleep(0.5)
        return str(v.value_of(path)).lower() == str(value).lower()

    def select_sky_asset(self, asset_id):
        return self.select_combo("WLPresetsCombo", "WLPrevPreset", "WLNextPreset", asset_id)

    def read_combos(self):
        out = {}
        for key, widget in (("sky_combo", "WLPresetsCombo"), ("water_combo", "WaterPresetsCombo"),
                            ("daycycle_combo", "DCPresetsCombo")):
            w = self.v.find_widget(PHOTOTOOLS, widget, required=False)
            try:
                out[key] = str(self.v.value_of(w)) if w else None
            except LeapError:
                out[key] = None
        return out

    def restore_environment(self):
        if not self.run_info.get("environment_changed"):
            return
        plan = self.plan
        if plan is None:
            raise LeapError("no restore plan for the original environment")
        self.ensure_floater(PHOTOTOOLS)
        if plan["kind"] == "region":
            self.v.press_first(PHOTOTOOLS, REGION_BUTTONS)
        elif plan["kind"] == "sky":
            ok = self.select_combo("WLPresetsCombo", "WLPrevPreset", "WLNextPreset", plan["sky"])
            if plan["water"]:
                ok = self.select_combo("WaterPresetsCombo", "WWPrevPreset", "WWNextPreset", plan["water"]) and ok
            if not ok:
                self.warn("could not re-select the original sky/water")
        else:
            ok = self.select_combo("DCPresetsCombo", "DCPrevPreset", "DCNextPreset", plan["dc"])
            if plan["water"]:
                ok = self.select_combo("WaterPresetsCombo", "WWPrevPreset", "WWNextPreset", plan["water"]) and ok
            if not ok:
                self.warn("could not re-select the original day cycle")
        self.v.sleep(1.0)
        now = self.read_combos()
        orig = self.run_info.get("original_environment") or {}
        diff = {k: (orig.get(k), now.get(k)) for k in now if orig.get(k) != now.get(k)}
        if plan["kind"] == "sky" and not plan["water"]:
            diff.pop("water_combo", None)     # a sky selection leaves the water as it is
        if diff:
            self.warn("environment after the restore differs from the original: %s" % diff)
            log("environment restore differs: %s" % diff)

    def choose_sky(self, spec, mode):
        """Make the sky of `spec` the current local environment ("inventory": the combo trick; "button": the
        quick-environment button of the scenario). Returns a description dict."""
        v = self.v
        self.run_info["environment_changed"] = True
        if v.floater_visible(PERSONAL):
            v.hide_floater(PERSONAL)
        self.ensure_floater(PHOTOTOOLS)
        if mode == "inventory":
            tried = []
            for nm in spec.get("names", []):
                for name, aid in find_settings_items(v, self.roots, nm):
                    tried.append("%s=%s" % (name, aid))
                    if self.select_sky_asset(aid):
                        return {"how": "inventory", "item": name, "asset_id": aid}
            raise LeapError("no inventory sky from %s took (tried: %s)" % (spec.get("names"), tried))
        fb = spec.get("fallback_button")
        if not fb:
            raise LeapError("no fallback button for %s" % spec["id"])
        v.press(PHOTOTOOLS, fb)
        v.sleep(1.0)
        return {"how": "button", "item": fb}

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

    def signature_ok(self, spec, sig):
        """Did the sky take? Compare the signature (before any edit) with the previous sky's: it must be equal when
        both specs name the same sky and different otherwise. No check for the first sky."""
        base = {k: sig[k] for k in BASE_SIGNATURE if k in sig}
        names = spec.get("names", [])
        if self.prev_base is None or not base:
            return True, base
        pnames, pbase = self.prev_base
        keys = [k for k in base if k in pbase]
        same = all(abs(float(base[k]) - float(pbase[k])) < 1e-3 for k in keys)
        return (same if names == pnames else not same), base

    # -- editing a sky --------------------------------------------------------------------------
    def open_editor(self, spec):
        v = self.v
        if spec.get("editor", "personal") == "fixed":
            if v.floater_visible(FIXED_SKY):
                raise LeapError("the fixed sky editor is already open (closing it would discard your edit); "
                                "close it and run again")
            if v.floater_visible(PERSONAL):
                v.hide_floater(PERSONAL)
            v.show_floater(FIXED_SKY, key="new")
            if FIXED_SKY not in self.floaters_opened:
                self.floaters_opened.append(FIXED_SKY)
                self.save_restore()
            self.select_fixed_tab(spec.get("tab_buttons", FIXED_TAB_BUTTONS))
            return FIXED_SKY
        v.show_floater(PERSONAL)
        if PERSONAL not in self.floaters_opened:
            self.floaters_opened.append(PERSONAL)
            self.save_restore()
        return PERSONAL

    def select_fixed_tab(self, buttons):
        """Click the tab button of the fixed editor that holds the sun and moon sliders. The tab buttons are named
        htab_<panel name> (lltabcontainer.cpp); the panel is panel_settings_sky_hbodies, or the Firestorm compact
        editor's panel_fs_settings_sky_hbodies."""
        v = self.v
        tried = []
        for name in buttons:
            p = v.find_widget(FIXED_SKY, name, required=False)
            tried.append(name)
            if not p:
                continue
            v.mouse_click(p)
            v.forget(FIXED_SKY)
            sl = v.find_widget(FIXED_SKY, "sun_elevation", required=False)
            if sl is None or not v.widget_info(sl).get("available"):
                raise LeapError("clicked %s but the sun sliders are not available" % name)
            return name
        raise LeapError("none of the tab buttons %s exists in the fixed sky editor" % tried)

    def prepare_sky(self, spec):
        v = self.v
        info = {"selected": None, "signature": None, "controls": {}, "error": None}
        settle = self.sc["settle"]["after_sky_s"]
        if spec.get("editor", "personal") == "fixed":
            # the fixed editor starts from a clone of the current sky: select the base sky first
            modes = ["inventory", "button"] if spec.get("names") else ["button"]
            last = None
            for mode in modes:
                try:
                    info["selected"] = self.choose_sky(spec, mode)
                    break
                except LeapError as exc:
                    last = exc
            else:
                raise last
            v.sleep(settle)
            floater = self.open_editor(spec)
            v.sleep(settle)
            base = None
        else:
            floater, base = None, None
            modes = ["inventory", "button"] if spec.get("names") else ["button"]
            last = None
            for i, mode in enumerate(modes):
                try:
                    info["selected"] = self.choose_sky(spec, mode)
                except LeapError as exc:
                    last = exc
                    self.warn("sky %s: %s" % (spec["id"], exc))
                    continue
                v.sleep(settle)
                floater = self.open_editor(spec)
                v.sleep(settle)
                ok, base = self.signature_ok(spec, self.read_signature())
                info["signature_verified"] = ok
                if ok:
                    break
                self.warn("sky %s: the sky read from Personal Lighting does not match what was expected after "
                          "selecting by %s (%s)" % (spec["id"], mode, base))
                if i + 1 < len(modes):
                    v.hide_floater(PERSONAL)
            if floater is None:
                raise last
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
        if floater == PERSONAL:
            info["signature"] = self.read_signature()
            self.prev_base = (spec.get("names", []), base or {})
        return floater, az, info

    # -- capture --------------------------------------------------------------------------------
    def capture(self, spec, step, kind, elev, floater, az):
        v = self.v
        got = v.set_slider(floater, "sun_elevation", elev, tol=0.006)
        fps, ms = v.perf() if self.sc["record_fps"] else (None, None)
        wait = self.sc["settle"]["per_step_s"]
        if fps and fps > 0:
            wait = max(wait, self.sc["settle"]["min_frames"] / fps)
        v.sleep(wait)
        fps, ms = v.perf() if self.sc["record_fps"] else (None, None)
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
                              "args": vars(self.args)})
        self.save_run_info()
        try:
            bad = [(a, d) for a, ok, d in check_apis(v, sc['record_fps']) if not ok]
            if bad:
                raise LeapError("this viewer lacks LEAP operations the script needs: %s" % bad)
            self.roots, self.user_roots = inventory_roots(v, sc["library_root_id"])
            self.run_info["inventory_roots"] = self.roots
            self.wait_for_inventory()
            self.snapshot_original_state()          # may refuse before anything is changed
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

    def wait_for_inventory(self, timeout=120.0, interval=4.0):
        """Wait until the scenario's sky names resolve to the same set of settings items on two polls in a row
        (small replies: a listing of the whole inventory would be a big frame), or until the timeout."""
        names = sorted({n for sp in self.sc["skies"] for n in sp.get("names", [])})
        end = time.monotonic() + timeout
        last = None
        while True:
            if not self.user_roots:
                self.roots, self.user_roots = inventory_roots(self.v, self.sc["library_root_id"])
            found = None
            if self.user_roots:
                found = sorted((a for n in names for _, a in find_settings_items(self.v, self.roots, n)))
            if found is not None and found == last:
                if not found and names:
                    self.warn("none of the scenario's sky names is in your inventory; skies fall back to the "
                              "quick-environment buttons")
                return
            last = found
            if time.monotonic() > end:
                self.warn("the inventory did not settle in %.0f s (%s matches); continuing" % (
                    timeout, "no folder" if found is None else len(found)))
                return
            log("waiting for the inventory (%s matching items)" % ("no folder yet" if found is None else len(found)))
            self.v.sleep(interval)

    def snapshot_original_state(self):
        v = self.v
        orig = {}
        if v.floater_visible(PHOTOTOOLS):
            orig["phototools_open"] = True
        else:
            self.ensure_floater(PHOTOTOOLS)
        self.select_env_tab()
        orig.update(self.read_combos())
        orig["personal_open"] = v.floater_visible(PERSONAL)
        orig["fixed_open"] = v.floater_visible(FIXED_SKY)
        self.run_info["original_environment"] = orig
        log("original environment: %s" % orig)
        self.plan = plan_restore(orig)
        if self.plan is None:
            if orig.get("personal_open") or orig.get("fixed_open"):
                todo = ("close Personal Lighting and the fixed sky editor (an edit in progress has no saved asset and "
                        "would be lost), then relaunch")
            else:
                todo = ("in Phototools > Environment click 'Revert to Region Default' or select a sky from the list, "
                        "then relaunch")
            raise NotRestorable(
                "cannot restore your current environment safely (Phototools shows sky=%r, water=%r, day cycle=%r; "
                "Personal Lighting open: %s; fixed sky editor open: %s). What to do: %s. Nothing was changed."
                % (orig.get("sky_combo"), orig.get("water_combo"), orig.get("daycycle_combo"), orig.get("personal_open"),
                   orig.get("fixed_open"), todo))
        if self.plan["kind"] == "region":
            self.warn("If you had a locally edited sky (no saved asset), it will be replaced by the shared environment "
                      "at the end.")
        self.save_restore()

    def select_env_tab(self):
        p = self.v.find_widget(PHOTOTOOLS, ENV_TAB_BUTTON, required=False)
        if p:
            try:
                self.v.mouse_click(p)
            except LeapError as exc:
                self.warn("could not select the Phototools Environment tab: %s" % exc)
        else:
            self.warn("Phototools Environment tab button %s not found" % ENV_TAB_BUTTON)

    def pause_clouds(self):
        v = self.v
        p = v.find_widget(PHOTOTOOLS, "PauseClouds", required=False)
        if not p:
            self.warn("PauseClouds button not found; clouds keep drifting during the sweeps")
            return
        before = bool(v.value_of(p))
        if before:
            self.run_info["clouds_paused_by_script"] = False
            return
        v.press(PHOTOTOOLS, "PauseClouds")
        self.clouds_toggled = True
        self.save_restore()
        v.sleep(0.4)
        if not bool(v.value_of(p)):
            self.warn("the PauseClouds button does not read as paused after the click; clouds may still drift")
        self.run_info["clouds_paused_by_script"] = True

    def run_sky(self, spec, elevs):
        v, sc = self.v, self.sc
        sid = spec["id"]
        entry = {"label": spec.get("label"), "why": spec.get("why"), "status": "started"}
        self.run_info["skies"][sid] = entry
        log("sky %s: preparing" % sid)
        opened_fixed = False
        try:
            if spec.get("editor") == "fixed":
                opened_fixed = FIXED_SKY not in self.floaters_opened
            floater, az, info = self.prepare_sky(spec)
            entry.update(info)
            if sc["camera"]["enabled"]:
                self.camera_set = True            # set before the call: a failure half way is still released
                self.save_restore()
                cam, focus = v.point_camera(az, sc["camera"])
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
        except (Aborted, ConnectionLost):
            entry["status"] = "aborted"
            raise
        except Exception as exc:  # noqa: BLE001 - a failed sky must not lose the others
            entry["status"] = "failed"
            entry["error"] = str(exc)
            if spec.get("optional"):
                self.warn("optional sky %s failed: %s" % (sid, exc))
            else:
                self.warn("sky %s FAILED: %s" % (sid, exc))
                self.failed_required.append(sid)
        finally:
            if spec.get("editor") == "fixed" and opened_fixed and FIXED_SKY in self.floaters_opened:
                try:
                    v.hide_floater(FIXED_SKY)
                    self.floaters_opened.remove(FIXED_SKY)
                except LeapError:
                    pass


# ---------------------------------------------------------------------------------------------
# Dry run
# ---------------------------------------------------------------------------------------------

class Report:
    def __init__(self):
        self.rows = []

    def add(self, name, ok, detail="", warn=False):
        self.rows.append((name, bool(ok), ("WARN: " if warn and ok else "") + str(detail)))
        log("%-4s %s %s" % ("FAIL" if not ok else "WARN" if warn else "PASS", name, detail))

    @property
    def failed(self):
        return [r for r in self.rows if not r[1]]


def check_widgets(v, rep, floater, widgets):
    for w in widgets:
        p = v.find_widget(floater, w, required=False)
        avail = None
        if p and not w.endswith("Combo"):      # getInfo of a combo box lists all its items
            try:
                avail = bool(v.widget_info(p).get("available"))
            except LeapError:
                avail = None
        rep.add("widget %s/%s" % (floater, w), p is not None, p or "not found",
                warn=p is not None and avail is False)


def dry_run(v, sc, args, outdir):
    """Read-only unless args.deep. Opening Personal Lighting or the fixed sky editor changes the environment
    (Personal Lighting freezes the current sky into the local environment), so those are only checked when they are
    already open, or with --deep, which records the environment and restores it afterwards like a run."""
    rep = Report()
    for api, ok, detail in check_apis(v, sc['record_fps']):
        rep.add("api %s" % api, ok, detail)
    groups = v.req("LLViewerControl", "groups").get("groups") or []
    rep.add("control group Global", "Global" in groups, str(groups))
    for key in list(sc["knobs"]) + ["CameraAngle", "FSEnvironmentManualTransitionTime"]:
        cur = v.try_control(key)
        is_knob = key in sc["knobs"]
        rep.add("control %s" % key, cur is not None or is_knob,
                "missing (a baseline viewer?); skipped in a real run" if cur is None else
                "%s = %r" % (cur.get("type"), cur.get("value")), warn=cur is None)
    run = None
    if args.no_login_wait:
        log("dry run before login: floaters, inventory and agent checks skipped")
    else:
        run = Run(v, sc, outdir, args)
        try:
            dry_run_ui(v, sc, rep, run, args)
        except ConnectionLost:
            out = pathlib.Path(outdir)
            out.mkdir(parents=True, exist_ok=True)
            write_json_atomic(out / "dry_run.json", [{"check": n, "ok": ok, "detail": d} for n, ok, d in rep.rows]
                              + [{"check": "connection", "ok": None, "detail": "lost: the checks above are all there is"}])
            raise
        finally:
            if args.deep:
                run.cleanup()
            else:
                for fl in list(run.floaters_opened):
                    try:
                        v.hide_floater(fl)
                    except LeapError:
                        pass
    out = pathlib.Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    write_json_atomic(out / "dry_run.json", [{"check": n, "ok": ok, "detail": d} for n, ok, d in rep.rows])
    log("dry run: %d checks, %d failed; details in %s" % (len(rep.rows), len(rep.failed), out / "dry_run.json"))
    return 1 if rep.failed else 0


def dry_run_ui(v, sc, rep, run, args):
    try:
        pos = v.agent_pos()
        rep.add("agent position", len(pos) == 3 and any(pos), str(pos))
    except LeapError as exc:
        rep.add("agent position", False, str(exc))
    run.roots, run.user_roots = inventory_roots(v, sc["library_root_id"])
    rep.add("inventory roots", bool(run.roots), str(run.roots))
    try:
        if not v.floater_visible(PHOTOTOOLS):
            v.show_floater(PHOTOTOOLS)
            run.floaters_opened.append(PHOTOTOOLS)
    except LeapError as exc:
        rep.add("floater %s opens" % PHOTOTOOLS, False, str(exc))
        return
    run.select_env_tab()
    v.forget(PHOTOTOOLS)
    check_widgets(v, rep, PHOTOTOOLS, ["WLPresetsCombo", "WLPrevPreset", "WLNextPreset", "WaterPresetsCombo", "WWPrevPreset",
                                       "WWNextPreset", "DCPresetsCombo", "DCPrevPreset", "DCNextPreset", "PauseClouds",
                                       "Noon", "Sunset", ENV_TAB_BUTTON])
    rg = [n for n in REGION_BUTTONS if v.find_widget(PHOTOTOOLS, n, required=False)]
    rep.add("region-default button", bool(rg), str(rg))
    orig = run.read_combos()
    orig = dict(orig, personal_open=v.floater_visible(PERSONAL), fixed_open=v.floater_visible(FIXED_SKY))
    run.run_info["original_environment"] = orig
    plan = plan_restore(orig)
    rep.add("original environment restorable", plan is not None,
            "%s -> %s" % (orig, plan) if plan else "a real run would refuse to start: %s" % orig)
    # Personal Lighting and the fixed sky editor
    editor_widgets = ["sun_azimuth", "sun_elevation", "moon_azimuth", "moon_elevation", "probe_ambiance", "cloud_coverage"]
    if v.floater_visible(PERSONAL):
        check_widgets(v, rep, PERSONAL, editor_widgets)
        slider_checks(v, rep)
    elif args.deep:
        run.plan = plan
        if plan is None:
            rep.add("deep checks", False, "skipped: the environment could not be restored afterwards")
        else:
            run.run_info["environment_changed"] = True
            run.ensure_floater(PERSONAL)
            check_widgets(v, rep, PERSONAL, editor_widgets)
            slider_checks(v, rep)
            v.hide_floater(PERSONAL)
            run.floaters_opened.remove(PERSONAL)
            if v.floater_visible(FIXED_SKY):
                rep.add("fixed sky editor", True, "already open: not touched", warn=True)
            else:
                try:
                    v.show_floater(FIXED_SKY, key="new")
                    run.floaters_opened.append(FIXED_SKY)
                    tab = run.select_fixed_tab(FIXED_TAB_BUTTONS)
                    rep.add("fixed editor tab", True, tab)
                    check_widgets(v, rep, FIXED_SKY, ["sun_azimuth", "sun_elevation", "moon_brightness"])
                except LeapError as exc:
                    rep.add("fixed editor tab", False, str(exc))
                finally:
                    v.hide_floater(FIXED_SKY)
                    if FIXED_SKY in run.floaters_opened:
                        run.floaters_opened.remove(FIXED_SKY)
    else:
        rep.add("floater %s widgets" % PERSONAL, True,
                "not checked: opening Personal Lighting freezes the sky into the local environment (use --deep)", warn=True)
        rep.add("fixed sky editor tab", True, "not checked (use --deep)", warn=True)
    # skies: highlight only, put back afterwards
    combo = v.find_widget(PHOTOTOOLS, "WLPresetsCombo", required=False)
    before = orig.get("sky_combo")
    for spec in sc["skies"]:
        found = []
        for nm in spec.get("names", []):
            for name, aid in find_settings_items(v, run.roots, nm):
                if not combo:
                    break
                v.select_by_value(combo, uuid.UUID(aid))
                in_combo = str(v.value_of(combo)).lower() == aid.lower()
                found.append((name, aid, in_combo))
                if in_combo:
                    break
            if any(f[2] for f in found):
                break
        good = any(f[2] for f in found)
        fb = spec.get("fallback_button")
        if good:
            rep.add("sky %s" % spec["id"], True, "; ".join("%s [%s]%s" % (n, a, "" if ok else " (not a sky of the combo)")
                                                          for n, a, ok in found if ok))
        elif spec.get("editor") == "fixed" and fb:
            rep.add("sky %s" % spec["id"], True, "base sky by button %s (no inventory sky)" % fb, warn=True)
        elif spec.get("optional"):
            rep.add("sky %s" % spec["id"], True, "no inventory sky from %s: would be skipped or use button %s" % (
                spec.get("names"), fb), warn=True)
        else:
            close = []
            for nm in spec.get("names", []):
                close += [n for n, _ in find_settings_items(v, run.roots, nm)][:5]
            rep.add("sky %s" % spec["id"], False, "no inventory sky from %s (closest names: %s); a real run would use the "
                    "fallback button %s and the data would be of a different sky" % (
                        spec.get("names"), close[:8] or "none", fb))
    if combo and before not in (None, "None"):
        try:
            v.select_by_value(combo, as_value(before))
            now = v.value_of(combo)
            if str(now).lower() != str(before).lower():
                log("WARNING the original sky combo value %r was not restored (it reads %r)" % (before, now))
        except LeapError as exc:
            log("WARNING the original sky combo value %r was not restored: %s" % (before, exc))


def slider_checks(v, rep):
    for w in ("sun_azimuth", "sun_elevation", "probe_ambiance"):
        p = v.find_widget(PERSONAL, w, required=False)
        if not p:
            continue
        try:
            ed = v.slider_editor(p)
            rep.add("slider editor %s" % w, True, ed)
            rep.add("slider value %s" % w, True, repr(v.value_of(p)))
        except LeapError as exc:
            rep.add("slider editor %s" % w, False, str(exc))


def restore_run(v, args):
    rj = pathlib.Path(args.restore) / "restore.json"
    data = json.loads(rj.read_text(encoding="utf-8"))
    for key, val in data.get("settings", {}).items():
        try:
            v.set_control(key, val)
            log("restored %s = %r" % (key, val))
        except LeapError as exc:
            log("could not restore %s: %s" % (key, exc))
    if data.get("camera_set"):
        try:
            v.release_camera()
            log("camera released")
        except LeapError as exc:
            log("camera: %s" % exc)
    orig = data.get("original_environment")
    log("settings%s restored. The environment is not touched by --restore: original was %s; "
        "use the Phototools Environment tab (Revert to Region Default or the combo boxes) to set it." % (
            " and camera" if data.get("camera_set") else "", orig))
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
    ap.add_argument("--out", default=None, help="run folder (default: .captures/<date-time>); resolved to an absolute path")
    ap.add_argument("--dry-run", action="store_true",
                    help="read-only check of the APIs, controls and Phototools widgets; Personal Lighting and the fixed "
                         "editor are checked only if already open (or with --deep)")
    ap.add_argument("--deep", action="store_true",
                    help="with --dry-run: also open Personal Lighting and the fixed sky editor (this changes the environment; "
                         "it is restored afterwards, like a run)")
    ap.add_argument("--list-skies", action="store_true", help="print the settings items (skies, water, day cycles) of the inventory")
    ap.add_argument("--sky-filter", default="", help="with --list-skies: only items whose name contains this text "
                    "(a listing of a big inventory is one big reply, which the viewer's pipe writer can corrupt)")
    ap.add_argument("--restore", default=None, metavar="RUNDIR", help="re-apply the settings saved in RUNDIR/restore.json")
    ap.add_argument("--noop", action="store_true",
                    help="do nothing: take the LEAP handshake and exit 0 (the second, throw-away --leap entry, see README)")
    ap.add_argument("--no-login-wait", action="store_true", help="do not wait for login (dry run: API checks only)")
    ap.add_argument("--only", default=None, help="comma-separated sky ids to run")
    ap.add_argument("--step-limit", type=int, default=0, help="only the first N sweep steps (quick test)")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                    help="set a debug setting for the run (restored afterwards), e.g. RenderAtmospherePhysicalLight=false")
    ap.add_argument("--viewer-log", default=None, help="path of the viewer log, for the excerpt (default: newest log touched in the last 10 minutes)")
    ap.add_argument("--max-wait-s", type=float, default=None, help="seconds to wait for login (default: the scenario's login_timeout_s, 3600)")
    args = ap.parse_args(argv)
    args.only = [s for s in (args.only or "").split(",") if s]
    vals = {}
    for item in args.set:
        k, sep, val = item.partition("=")
        if not k or not sep:
            ap.error("--set needs KEY=VALUE")
        low = val.lower()
        vals[k] = True if low == "true" else False if low == "false" else (
            int(val) if re.fullmatch(r"-?\d+", val) else float(val) if re.fullmatch(r"-?\d+\.\d*([eE][-+]?\d+)?", val) else val)
    args.set_values = vals
    if args.deep and not args.dry_run:
        ap.error("--deep only goes with --dry-run")
    return args


def execute(session, args, sc, outdir):
    """Everything after the LEAP handshake. Returns the process exit code."""
    outdir = pathlib.Path(outdir).resolve()
    if not args.restore:
        outdir.mkdir(parents=True, exist_ok=True)
        stale = outdir / "STOP"
        if stale.exists():
            stale.unlink()
            log("removed a stale STOP file from the run folder")
    v = Viewer(session, abort_file=None if args.restore else str(outdir / "STOP"))
    session.diag_dir = None if args.restore else outdir
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
            roots, _ = inventory_roots(v, sc["library_root_id"])
            items = list_settings(v, roots, args.sky_filter)
            for aid, name in sorted(items.items(), key=lambda kv: kv[1].lower()):
                log("settings item: %-48s %s" % (name, aid))
            write_json_atomic(outdir / "settings_items.json", items)
            return 0
        if args.dry_run:
            return dry_run(v, sc, args, outdir)
        write_json_atomic(outdir / "scenario.json", sc)
        run = Run(v, sc, outdir, args)
        run.go()
        log("done: %d snapshots in %s" % (len(run.manifest_rows), outdir))
        log("next: python %s %s" % (HERE / "analyze_light_check.py", outdir))
        if run.failed_required:
            log("FAILED SKIES %s (see run.json and the warnings above)" % ", ".join(run.failed_required))
            code = 4
    except Aborted as exc:
        log("aborted: %s" % exc)
        code = 2
    except ViewerGone as exc:
        log("the pipe from the viewer reached end of file: %s (the viewer exited or closed the plugin pipe; settings could "
            "not be restored by the script; see restore.json)" % exc)
        code = 3
    except ProtocolError as exc:
        log("PROTOCOL ERROR, the connection to the viewer is lost (the viewer may still be running): %s. Settings could "
            "not be restored by the script: use --restore on restore.json in a new viewer, and check the environment." % exc)
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
    outdir = pathlib.Path(args.out) if args.out else default_capture_root() / dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    session = LeapSession(sys.stdin.buffer, sys.stdout.buffer, log=log).start()
    if args.noop:
        log("noop plugin: handshake taken, exiting")
        return 0
    return execute(session, args, sc, outdir)


if __name__ == "__main__":
    code = 1
    try:
        code = main()
    except BaseException:  # noqa: BLE001 - report, then leave with a code
        traceback.print_exc()
    finally:
        sys.stderr.flush()
        sys.stdout.flush()
        # Leave without interpreter finalisation: the reader thread is blocked in a read of stdin (daemon thread
        # holding the buffer lock), which makes a normal exit abort with a fatal error on Windows.
        os._exit(code)
