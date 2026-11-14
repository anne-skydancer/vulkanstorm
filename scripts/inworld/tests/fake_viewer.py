# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""A fake LEAP peer that plays the viewer: speaks the wire protocol and models just enough of the APIs, floaters
and widgets that light_check.py uses to run end to end offline. It models what the viewer's source says the
operations do (setSelectedByValue does not commit, sliders take typed text, snapshots are BMP, ...). It cannot
tell whether the real widget paths, key names or camera behave the same; the dry run does that in the viewer.
"""

import io
import math
import os
import pathlib
import sys
import threading
import time
import urllib.parse
import uuid

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from leap_client import binary_dumps, llsd_loads, read_frame, write_frame  # noqa: E402

FL = "/floater_view/"
SKIES = {   # name -> (asset id, sun azimuth, sun elevation, cloud coverage, haze, gamma)
    "Default": (uuid.UUID("11111111-0000-0000-0000-000000000001"), 90.0, 66.0, 0.2, 0.7, 1.0),
    "Sunset": (uuid.UUID("11111111-0000-0000-0000-000000000002"), 200.0, 3.0, 0.4, 0.7, 1.0),
    "[TOR] SUNSET - Bangko 1": (uuid.UUID("11111111-0000-0000-0000-000000000003"), 250.0, 1.0, 0.1, 0.1, 1.0),
}
DAYCYCLE_DEFAULT = uuid.UUID("22222222-0000-0000-0000-000000000009")  # same name as a sky, not in the combo
REGION_DEFAULT = "__Regiondefault__"
INV_ROOT = "33333333-0000-0000-0000-000000000002"
SETTINGS_FOLDER = "33333333-0000-0000-0000-000000000001"


class PoolModel:
    """A model of the projector shadow pool's ProjectorShadows.stats reply (stage P3), enough for the orbit gates: a few tiles with a CRC
    each, the render counters and their causes. The ideal pool changes nothing when the camera moves, except that the first moves
    lift objects to full detail (cause lod_up, once). camera_bug models the defect P3 removes: every camera move redraws a tile."""

    def __init__(self, tiles=3, crc=True, camera_bug=False, lod_ups=2, without=0, present=True):
        self.frame = 1000
        self.crc = crc
        self.camera_bug = camera_bug
        self.lod_ups = lod_ups
        self.without = without
        self.present = present
        self.static = tiles
        self.dynamic = 0
        self.causes = {"new": tiles, "lod_up": 0, "add_remove": 0, "edit": 0, "mover": 0, "lamp": 0, "avatar": 0, "lowres": 0, "other": 0}
        self.crcs = {k: 100 + k for k in range(1, tiles + 1)}
        self.moves = 0
        self.dirty_age_ms_max = 0.0

    def on_camera(self, pos, focus):
        self.moves += 1
        self.frame += 6
        if self.camera_bug:
            self.static += 1
            self.causes["edit"] += 1
            for k in list(self.crcs)[:1]:
                self.crcs[k] += 1
        elif self.lod_ups > 0:
            self.lod_ups -= 1
            self.static += 1
            self.causes["lod_up"] += 1
            for k in list(self.crcs)[-1:]:
                self.crcs[k] += 1000

    def stats(self, d):
        if d.get("reset_ages"):
            self.dirty_age_ms_max = 0.0
        tiles = []
        for k, c in sorted(self.crcs.items()):
            t = {"key": k, "size": 1024, "x": 0, "y": 0, "drawn_frame": self.frame - 3, "cam_dist": 5.0, "pending": False}
            if self.crc and d.get("crc", True):
                t["crc32"] = c
            tiles.append(t)
        pool = {"pool_present": self.present, "level": 0, "atlas_size": 4096, "lights": len(tiles), "static_renders": self.static,
                "dynamic_renders": self.dynamic, "repacks": 0, "lights_without_tile": self.without, "tile_cost_ms": 0.07,
                "last_frame_renders": 0, "max_stale_frames": 3, "last_frame_render_ms": 0.0, "pinned_objects": 4, "pending_tiles": 0,
                "avatar_tiles": 0, "dirty_boxes_last_frame": 0, "frame": self.frame, "dirty_age_ms_max": self.dirty_age_ms_max,
                "tiles": tiles, "rerender_causes": dict(self.causes)}
        return {"compiled": True, "pool": pool, "slots": {"occupied": 0, "targeted": 0}, "lights": {}}


class FakeViewer:
    def __init__(self, pop_in_sky=None, snapshot_fails_for=None, drop_ops=(), initial_combo=REGION_DEFAULT,
                 water_combo=REGION_DEFAULT, dc_combo=REGION_DEFAULT, on_op=None, personal_open=False, break_after_ops=0, crowd=0, fixed_open=False,
                 bad_takes=(), bad_kind="black", gl_stats=False, gl_stats_catches_injection=True, gl_stats_diff_pass=None,
                 logs_dir=None, gl_stale_results=False, gl_not_run=False, gl_gpu_lag=False, gl_empty_gbuffer=False, gl_short_window=False,
                 gl_m1_fps_factor=1.0, u32_as_string=False, projector=None):
        # gl_stats: the viewer has the GLSubmitStats API (a development build). gl_stats_catches_injection=False models a
        # broken identical-image check; gl_stats_diff_pass names a pass the fake renders differently the second time.
        self.projector = projector            # a model of the projector shadow pool (ProjectorShadows.stats), or None
        self.gl_stats = gl_stats
        self.gl_catches = gl_stats_catches_injection
        self.gl_diff_pass = gl_stats_diff_pass
        self.logs_dir = logs_dir
        self.gl_stale = gl_stale_results      # an arming no frame takes: the results (and the serial) never change
        self.gl_not_run = gl_not_run          # every pass reports "not run"
        self.gl_gpu_lag = gl_gpu_lag          # the GPU times of the last window frame never arrive
        self.gl_empty_gbuffer = gl_empty_gbuffer
        self.gl_short_window = gl_short_window  # the window never reaches its frame target
        self.gl_serial = 0
        self.gl_window = {"target": 0, "collected": 0, "gpu": 0}
        self.u32_as_string = u32_as_string          # LLSD has no unsigned type: the real viewer returns a U32 as a string
        self.gl_m1_fps_factor = gl_m1_fps_factor    # the frame rate while RenderAlphaSortMethod is 1 (a slower stochastic)
        self._fc = (time.monotonic(), 0.0)
        self.gl_compare_pending = 0
        self.gl_compare_results = []
        self.bad_takes = set(bad_takes)   # 1-based numbers of the saveSnapshot calls that read back the cleared buffer
        self.bad_kind = bad_kind          # 'black': the whole frame; 'band': the top 40 % of its rows
        self.takes = 0
        self.crowd = crowd             # inventory items named 'Default Day nnn' that come before the exact ones
        self.inv_queries = []
        self.break_after_ops = break_after_ops
        v2p_r, v2p_w = os.pipe()
        p2v_r, p2v_w = os.pipe()
        self.plugin_in = os.fdopen(v2p_r, "rb", buffering=0)
        self.plugin_out = os.fdopen(p2v_w, "wb", buffering=0)
        self._to_plugin = os.fdopen(v2p_w, "wb", buffering=0)
        self._from_plugin = os.fdopen(p2v_r, "rb", buffering=0)
        self.pop_in_sky = pop_in_sky
        self.snapshot_fails_for = snapshot_fails_for
        self.drop_ops = set(drop_ops)
        self.lock = threading.Lock()
        self.listeners = set()
        self.reply_pump = "reply-pump-1"
        self.command_pump = "cmd-pump-1"
        self.log = []
        # settings
        self.settings = {k: ("F32", v) for k, v in {
            "RenderLightHandoverSwing": 0.10, "RenderLightHandoverFade": 0.30, "RenderMoonlightCompensation": -3.0,
            "RenderMoonAdaptationDepth": 6.0, "RenderAtmosphereHazeSlab": 250.0, "CameraAngle": 1.047,
            "FSEnvironmentManualTransitionTime": 3.0}.items()}
        self.settings["RenderAtmospherePhysicalLight"] = ("Boolean", True)
        self.settings["RenderAlphaSortMethod"] = ("U32", 1)
        if gl_stats:
            self.settings.update({"RenderGLSubmitStats": ("Boolean", False), "RenderGLSubmitStatsGPU": ("Boolean", True),
                                  "RenderGLSubmitCompare": ("S32", 0), "RenderGLSubmitCompareInject": ("S32", 0),
                                  "RenderGLSubmitPath": ("S32", 0)})
        # environment
        self.current_sky = initial_combo
        self.combo = initial_combo
        self.water_combo = water_combo
        self.dc_combo = dc_combo
        self.on_op = on_op
        self.frozen = False          # Personal Lighting froze the sky into the local environment
        self.shown = []              # floaters shown, in order
        self.refuse_values = set()   # setSelectedByValue values the combo refuses (a bare false), to test a restore that fails
        self.clouds_paused = False
        self.floaters = {"phototools": False, "env_adjust_snapshot": False, "env_fixed_environmentent_sky": False}
        self.follow_cam = None
        self.sun_el = 66.0
        self.sliders = {}
        self.text = {}
        self.fixed_tab_sun = False
        self.sky_applied_count = 0
        if fixed_open:
            self.floaters["env_fixed_environmentent_sky"] = True
        if personal_open:
            self.floaters["env_adjust_snapshot"] = True
            self._capture_sky()
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    # -- wire -----------------------------------------------------------------------------------
    def _send(self, pump, data):
        payload = binary_dumps({"pump": pump, "data": data})
        self.max_frame = max(getattr(self, "max_frame", 0), len(payload))
        with self.lock:
            write_frame(self._to_plugin, payload)

    def _serve(self):
        self._send(self.reply_pump, {"command": self.command_pump, "features": {}})
        while True:
            try:
                frame = read_frame(self._from_plugin)
            except Exception:  # noqa: BLE001
                return
            if frame is None:
                return
            msg = llsd_loads(frame)
            self._dispatch(msg["pump"], msg["data"])

    def _reply(self, req, resp):
        if "reply" in req:
            if not isinstance(resp, dict):
                resp = {}
            if "reqid" in req:
                resp["reqid"] = req["reqid"]
            self._send(req["reply"], resp)

    def _dispatch(self, pump, d):
        op = d.get("op")
        if op in self.drop_ops:
            return
        self.log.append((pump, op))
        self.ops_seen = getattr(self, "ops_seen", 0) + 1
        if self.break_after_ops and self.ops_seen == self.break_after_ops:
            with self.lock:       # a frame that is not LLSD
                self._to_plugin.write(b"20:" + b"garbage garbage garbage"[:20])
            return
        if self.on_op:
            self.on_op(op, d)
        if pump == self.command_pump:
            return self._leap(op, d)
        handler = getattr(self, "api_" + pump, None)
        if handler is None:
            return self._reply(d, {"error": "no such pump %s" % pump})
        handler(op, d)

    # -- LEAP -----------------------------------------------------------------------------------
    def _leap(self, op, d):
        if op == "ping":
            self._reply(d, {})
        elif op == "listen":
            self.listeners.add(d["source"])
            self._reply(d, {"status": True})
        elif op == "getAPIs":
            self._reply(d, {n: {"desc": n} for n in ("LLStartUp", "LLViewerControl", "LLFloaterReg", "UI", "LLWindow",
                                                    "LLViewerWindow", "LLAgent", "LLInventory", "LLStats")
                            + (("GLSubmitStats",) if self.gl_stats else ()) + (("ProjectorShadows",) if self.projector else ())})
        elif op == "getAPI":
            if d["api"] == "GLSubmitStats":
                if not self.gl_stats:
                    return self._reply(d, {"error": "no such API GLSubmitStats"})
                return self._reply(d, {"name": "GLSubmitStats", "ops": [
                    {"name": o} for o in ("status", "windowBegin", "windowStop", "summary", "dump", "compareArm",
                                          "compareResults", "profileFrame", "selfBench") if o not in self.drop_ops]})
            ops = {"LLStartUp": ["postStartupState", "getStateTable"], "LLViewerControl": ["get", "set", "groups"],
                   "LLFloaterReg": ["showInstance", "hideInstance", "instanceVisible", "clickButton"],
                   "UI": ["getValue", "setSelectedByValue"],
                   "LLWindow": ["getPaths", "getInfo", "keyDown", "pasteText", "mouseDown", "mouseUp"],
                   "LLViewerWindow": ["saveSnapshot"],
                   "LLAgent": ["getPosition", "setCameraParams", "setFollowCamActive", "removeCameraParams"],
                   "LLInventory": ["getBasicFolderID", "collectDescendantsIf", "getItemsInfo"],
                   "LLStats": ["getPerfData"], "ProjectorShadows": ["stats"]}[d["api"]]
            ops = [o for o in ops if o not in self.drop_ops]
            self._reply(d, {"name": d["api"], "ops": [{"name": o} for o in ops]})

    # -- APIs -----------------------------------------------------------------------------------
    def api_LLStartUp(self, op, d):
        if op == "getStateTable":
            self._reply(d, {"table": ["STATE_FIRST", "STATE_LOGIN_WAIT", "STATE_STARTED"]})
        elif op == "postStartupState":
            if "StartupState" in self.listeners:
                self._send("StartupState", {"str": "STATE_LOGIN_WAIT", "enum": 1})
                self._send("StartupState", {"str": "STATE_STARTED", "enum": 2})

    def api_LLViewerControl(self, op, d):
        if op == "groups":
            return self._reply(d, {"groups": ["Global", "PerAccount"]})
        key = d.get("key")
        if key not in self.settings:
            return self._reply(d, {"error": "In group 'Global', unrecognized control key '%s'" % key})
        typ, val = self.settings[key]
        if op == "set":
            val = d["value"]
            self.settings[key] = (typ, val)
        if self.u32_as_string and typ == "U32":
            val = str(val)
        self._reply(d, {"name": key, "type": typ, "value": val, "comment": ""})

    def api_LLFloaterReg(self, op, d):
        name = d["name"]
        if op == "clickButton":
            if not self.floaters.get(name) or d["button"] not in {p.rsplit("/", 1)[-1] for p in self._paths()}:
                return self._reply(d, {"type": "LLButton", "error": "NULL"})
            self._click(d["button"])
            return self._reply(d, {})
        if op == "showInstance":
            self.floaters[name] = True
            self.shown.append(name)
            self.text.clear()
            if name == "env_adjust_snapshot":
                self.frozen = True
                self._capture_sky()
            if name == "env_fixed_environmentent_sky":
                self.sliders = {"sun_elevation": 0.0, "sun_azimuth": 0.0, "moon_brightness": 0.5}
                self.fixed_tab_sun = False
        elif op == "hideInstance":
            self.floaters[name] = False
        elif op == "instanceVisible":
            self._reply(d, {"visible": self.floaters.get(name, False)})

    def _capture_sky(self):
        name = next((n for n, s in SKIES.items() if s[0] == self.current_sky), "Default")
        _, az, el, cc, haze, gamma = SKIES[name]
        self.sliders = {"sun_azimuth": az, "sun_elevation": el, "moon_azimuth": 10.0, "moon_elevation": 20.0,
                        "probe_ambiance": 0.0, "cloud_coverage": cc, "haze_horizon": haze, "haze_density": 0.7,
                        "scene_gamma": gamma}
        self.sun_el = el

    def _paths(self):
        out = []
        if self.floaters["phototools"]:
            base = FL + "phototools/"
            out += [FL + "phototools"] + [base + n for n in (
                "WLPresetsCombo", "WLPrevPreset", "WLNextPreset", "WaterPresetsCombo", "DCPresetsCombo", "PauseClouds",
                "Sunrise", "Noon", "Sunset", "Midnight", "Revert to Region Default", "WWPrevPreset", "WWNextPreset",
                "DCPrevPreset", "DCNextPreset", "htab_EnvironmentTab")]
        if self.floaters["env_adjust_snapshot"]:
            base = FL + "env_adjust_snapshot/"
            out += [FL + "env_adjust_snapshot"]
            for n in ("sun_azimuth", "sun_elevation", "moon_azimuth", "moon_elevation", "probe_ambiance", "cloud_coverage",
                      "haze_horizon", "haze_density", "scene_gamma"):
                out += [base + n, base + n + "/slider_bar", base + n + "/value_editor"]
        if self.floaters["env_fixed_environmentent_sky"]:
            # the view is named by the floater XML ("Fixed Environment"), not by its LLFloaterReg name
            # once the water editor (same XML, same view name) has been built it is found first by name
            view = "Fixed Environment" if "env_fixed_environmentent_water" not in self.shown else "Fixed Environment (shadowed)"
            base = FL + view + "/"
            out += [FL + view, base + "htab_panel_settings_sky_hbodies"]
            if self.fixed_tab_sun:
                for n in ("sun_azimuth", "sun_elevation", "moon_brightness"):
                    out += [base + n, base + n + "/value_editor"]
        return out

    def api_LLWindow(self, op, d):
        paths = self._paths()
        path = d.get("path", "")
        if op == "getPaths":
            under = d.get("under", "")
            if under.startswith("//"):
                floater, _, widget = under[2:].partition("//")
                floater = urllib.parse.unquote(floater)      # LLUI::resolvePath unescapes each part
                if widget:
                    sel = [p for p in paths if (p == FL + floater or p.startswith(FL + floater + "/")) and (
                        p.rsplit("/", 1)[-1] == widget or ("/" + widget + "/") in p)]
                    sel = sorted(sel, key=len)
                    if not sel:
                        return self._reply(d, {"error": "getPaths request specified invalid \"under\" path: '%s'" % under})
                else:
                    sel = [p for p in paths if p == FL + floater or p.startswith(FL + floater + "/")]
            else:
                sel = [p for p in paths if p == under or p.startswith(under + "/")]
            return self._reply(d, {"paths": sel})
        if path and path not in paths:
            return self._reply(d, {"error": "%s request specified invalid \"path\": '%s'" % (op, path)})
        last = path.rsplit("/", 1)[-1]
        if op == "getInfo" and last.endswith("Combo"):
            # the real LLComboBox::addInfo lists every item: a big reply
            return self._reply(d, {"path": path, "class": "class LLComboBox", "available": True,
                                   "items": [{"label": "sky number %d with a longish name" % i, "value": str(uuid.uuid4())}
                                             for i in range(300)]})
        if op == "getInfo":
            cls = "class LLLineEditor" if last == "value_editor" else "class LLButton" if (
                last.startswith(("WL", "WW", "DC", "Paus", "Sun", "Noon", "Revert", "Mid", "htab_"))) else "class LLSliderCtrl"
            return self._reply(d, {"path": path, "class": cls, "available": True})
        if op == "keyDown":
            if path.endswith("value_editor"):
                key = path.split("/")[-2]
                t = self.text.setdefault(key, "%g" % self.sliders.get(key, 0.0))
                ks = d.get("keysym")
                if ks == "Backsp":
                    self.text[key] = t[:-1]
                elif ks == "Enter":
                    try:
                        val = float(self.text[key])
                    except ValueError:
                        return
                    lim = (-90, 90) if key in ("sun_elevation", "moon_elevation") else (0, 360) if "azimuth" in key else (0, 10)
                    if lim[0] <= val <= lim[1]:
                        self.sliders[key] = round(val, 2)
                        if key == "sun_elevation":
                            self.sun_el = self.sliders[key]
            return
        if op == "pasteText":
            key = path.split("/")[-2]
            self.text[key] = self.text.get(key, "") + d["text"]
            return
        if op in ("mouseDown", "mouseUp"):
            if op == "mouseUp":
                self._click(last)
            return self._reply(d, {"path": path, "handled": True})

    def _click(self, name):
        if name == "PauseClouds":
            self.clouds_paused = not self.clouds_paused
        elif name in ("WLNextPreset", "WLPrevPreset"):
            ids = [s[0] for s in SKIES.values()]
            i = ids.index(self.combo) if self.combo in ids else -1
            i = (i + (1 if name == "WLNextPreset" else -1)) % len(ids)
            self.combo = ids[i]
            self._apply(self.combo)
        elif name == "Revert to Region Default":
            self.current_sky = REGION_DEFAULT
            self.combo = REGION_DEFAULT
            self.water_combo = REGION_DEFAULT
            self.dc_combo = REGION_DEFAULT
            self.frozen = False
        elif name in ("Sunset", "Noon"):
            self._apply(SKIES["Sunset" if name == "Sunset" else "Default"][0])
        elif name == "htab_panel_settings_sky_hbodies":
            self.fixed_tab_sun = True

    def _apply(self, asset):
        self.current_sky = asset
        self.sky_applied_count += 1

    def api_UI(self, op, d):
        path = d["path"]
        last = path.rsplit("/", 1)[-1]
        if op == "getValue":
            if last == "WLPresetsCombo":
                return self._reply(d, {"value": self.combo})
            if last == "WaterPresetsCombo":
                return self._reply(d, {"value": self.water_combo})
            if last == "DCPresetsCombo":
                return self._reply(d, {"value": self.dc_combo})
            if last == "PauseClouds":
                return self._reply(d, {"value": self.clouds_paused})
            if last in self.sliders:
                return self._reply(d, {"value": float(self.sliders[last])})
            return self._reply(d, {})
        if op == "setSelectedByValue":
            ok = self._select_by_value(last, d)
            if "reply" in d:      # as the real viewer: a bare boolean, no reqid (LLReqID::stamp leaves a scalar alone)
                self._send(d["reply"], bool(ok))
            return

    def _select_by_value(self, last, d):
        if d["value"] in self.refuse_values:
            return False
        if last == "WaterPresetsCombo":
            self.water_combo = d["value"]
            return True
        if last == "DCPresetsCombo":
            self.dc_combo = d["value"]
            return True
        if last == "WLPresetsCombo" and d["value"] in [s[0] for s in SKIES.values()] + [REGION_DEFAULT]:
            self.combo = d["value"]       # no commit, as in the viewer
            return True
        return False

    def api_LLAgent(self, op, d):
        if op == "getPosition":
            self._reply(d, {"region": [128.0, 64.0, 30.0], "global": [1.0, 2.0, 3.0]})
        elif op == "setCameraParams":
            self.follow_cam = d
            if self.projector:
                self.projector.on_camera(d["camera_pos"], d["focus_pos"])
        elif op in ("setFollowCamActive", "removeCameraParams"):
            self.follow_cam = None

    def api_ProjectorShadows(self, op, d):
        if op == "stats" and self.projector:
            self._reply(d, self.projector.stats(d))

    def api_LLInventory(self, op, d):
        if op == "getBasicFolderID":
            self._reply(d, {"id": uuid.UUID(INV_ROOT if d["ft_name"] == "root_inv" else SETTINGS_FOLDER)})
        elif op == "collectDescendantsIf":
            # the settings folder holds none of the items; they are elsewhere under the inventory root. The filter is a
            # case-insensitive substring match with a limit, in traversal order, as in llinventorylistener.cpp
            self.inv_queries.append((str(d["folder_id"]), d.get("name"), d.get("limit")))
            items = {}
            if str(d["folder_id"]) == INV_ROOT:
                name = d.get("name", "").lower()
                limit = d.get("limit") or 10 ** 9
                entries = ([("Default Day %03d" % i, uuid.uuid4()) for i in range(self.crowd)]
                           + [(n, s[0]) for n, s in SKIES.items()] + [("Default", DAYCYCLE_DEFAULT)])
                for n, aid in entries:
                    if name in n.lower() and len(items) < limit:
                        items[str(uuid.uuid4())] = {"name": n, "asset_id": aid, "asset_type": "settings"}
            self._reply(d, {"items": items})

    # -- GLSubmitStats (a development build) -----------------------------------------------------
    GL_PASSES = ["G-buffer", "sun cascade 0", "sun cascade 1", "sun cascade 2", "sun cascade 3", "spot shadow 0",
                 "spot shadow 1", "post-deferred"]

    def _gl_totals(self):
        return {"draws": 4200.0, "drawTriangles": 1.5e6, "vbChanges": 3900.0, "attribRespecs": 3950.0, "iboBinds": 3900.0,
                "shaderBinds": 60.0, "texBindFast": 5200.0, "texBindFastRedundant": 900.0, "texGLBind": 5300.0,
                "uniformGL": 7400.0, "uniformBytes": 410000.0, "paletteUploads": 260.0, "paletteDistinct": 90.0,
                "mdvSyncs": 1800.0, "bufSubData": 12.0, "stateToggles": 300.0, "occBlocking": 0.0, "runs": 2900.0,
                "mergeableDraws": 1300.0, "cpu_ms": 9.5, "gpu_ms": 7.25}

    def _frame_count(self):
        """Frames rendered so far: 60 per second, times gl_m1_fps_factor while RenderAlphaSortMethod is 1."""
        now = time.monotonic()
        t0, n = self._fc
        rate = 60.0 * (self.gl_m1_fps_factor if int(self.settings["RenderAlphaSortMethod"][1]) == 1 else 1.0)
        n += (now - t0) * rate
        self._fc = (now, n)
        return n

    def api_GLSubmitStats(self, op, d):
        w = self.gl_window
        if op == "status":
            self._reply(d, {"compiled": True, "window_target": w["target"], "window_collected": w["collected"],
                            "window_gpu_frames": w["gpu"], "window_active": w["collected"] < w["target"],
                            "compare_armed": 0, "compare_results": len(self.gl_compare_results),
                            "frame_count": int(self._frame_count())})
        elif op == "windowBegin":
            n = int(d.get("frames", 60))
            if self.gl_short_window:
                self.gl_window = {"target": n, "collected": n // 2, "gpu": n // 2}
                self._reply(d, {"frames": n})
                return
            self.gl_window = {"target": n, "collected": n, "gpu": (n - 1 if self.gl_gpu_lag else n) if d.get("gpu", True) else 0}
            self._reply(d, {"frames": n})
        elif op == "windowStop":
            self._reply(d, {"collected": w["collected"]})
        elif op == "summary":
            self._reply(d, {"summary": {"totals": self._gl_totals(), "views": [{"view": "main", "draws": 3000.0}]},
                            "frames": w["collected"], "gpu_frames": w["gpu"]})
        elif op == "dump":
            import json  # noqa: PLC0415
            t = self._gl_totals()
            method = int(self.settings["RenderAlphaSortMethod"][1])
            doc = {"schema": 1, "kind": "window", "scale": "per_frame_mean", "frames": w["collected"], "gpu_frames": w["gpu"],
                   "totals": dict(t, run_hist=[1400.0, 700.0, 500.0, 200.0, 80.0, 20.0]),
                   "views": [{"view": "frame", "calls": 1.0, "cpu_ms": 9.5, "gpu_ms": 7.25, "draws": 0.0},
                             {"view": "main", "calls": 3.0, "cpu_ms": 6.0, "gpu_ms": 5.0, "draws": 3000.0, "vbChanges": 2800.0}],
                   "passes": [{"view": "main", "pass": "gbuffer", "calls": 1.0, "cpu_ms": 4.0, "gpu_ms": 3.5, "draws": 2500.0},
                              {"view": "main", "pass": "deferred lighting/geom post-deferred/ALPHA_POST_WATER 0", "calls": 1.0,
                               "cpu_ms": 1.0 if method == 0 else 1.5, "gpu_ms": 0.8, "draws": 400.0 if method == 0 else 300.0, "shaderBinds": 12.0,
                               "paletteUploads": 30.0, "uniformBytes": 5000.0, "samples": 1.0e6 if method == 0 else 2.0e6},
                              {"view": "sun cascade 0", "pass": "shadow opaque 0", "calls": 1.0, "cpu_ms": 1.0, "gpu_ms": 0.9,
                               "draws": 900.0}],
                   "compare": []}
            pathlib.Path(d["path"]).parent.mkdir(parents=True, exist_ok=True)
            pathlib.Path(d["path"]).write_text(json.dumps(doc), encoding="utf-8")
            self._reply(d, {"path": d["path"]})
        elif op == "compareArm":
            if self.gl_stale:
                return self._reply(d, {"armed": int(d.get("pass", -1))})
            self.gl_compare_pending = int(d.get("pass", -1))
            self.settings["RenderGLSubmitCompare"] = ("S32", self.gl_compare_pending)
            self._reply(d, {"armed": self.gl_compare_pending})
        elif op == "compareResults":
            if self.gl_compare_pending:
                # the next frame takes the arming; it compares, then the setting is back to 0
                inject = self.settings["RenderGLSubmitCompareInject"][1]
                results = []
                for i, label in enumerate(self.GL_PASSES, 1):
                    if self.gl_compare_pending not in (-1, i):
                        continue
                    if self.gl_not_run:
                        results.append({"pass": i, "label": label, "ran": False, "identical": False, "note": "not run", "attachments": []})
                        continue
                    injected = bool(inject and i == 1 and self.gl_catches)
                    other = self.gl_diff_pass == label
                    distinct = 1 if (self.gl_empty_gbuffer and i == 1) else 50
                    atts = [{"name": "color0", "pixels": 100, "mismatched_pixels": 1 if injected else 0, "distinct_values": distinct}]
                    if injected:
                        atts[0]["injected"] = True
                    if i == 1:
                        atts.append({"name": "depth", "pixels": 100, "mismatched_pixels": 5 if other else 0, "distinct_values": distinct})
                    elif other:
                        atts[0]["mismatched_pixels"] = 1
                    results.append({"pass": i, "label": label, "ran": True, "identical": not (injected or other), "attachments": atts})
                self.gl_compare_results = results
                self.gl_serial += 1
                self.gl_compare_pending = 0
                self.settings["RenderGLSubmitCompare"] = ("S32", 0)
            self._reply(d, {"armed": 0, "pending": 0, "frame": 1, "arm_serial": self.gl_serial, "results": self.gl_compare_results})
        elif op == "selfBench":
            self._reply(d, {"iterations": d.get("iterations"), "hooks_per_draw": 20, "ns_per_draw_not_collecting": 9.0,
                            "ns_per_draw_collecting": 80.0, "ns_per_hook_not_collecting": 0.45, "ns_per_hook_collecting": 4.0,
                            "ns_per_scope_cpu_only": 400.0, "ns_per_scope_with_gpu_timers": 900.0})
        elif op == "profileFrame":
            if self.logs_dir:
                p = pathlib.Path(self.logs_dir) / "profile.vfake.json"
                p.write_text("{\"submit\": {}}", encoding="utf-8")
            self._reply(d, {"logs_dir": str(self.logs_dir or "")})

    def api_LLStats(self, op, d):
        self._reply(d, {"stats": {"frametime": {"fps": [60.0] * 40, "frame_time_ms": [16.6] * 40}}})

    def api_LLViewerWindow(self, op, d):
        from PIL import Image  # noqa: PLC0415
        name = next((n for n, s in SKIES.items() if s[0] == self.current_sky), "")
        if self.snapshot_fails_for and name == self.snapshot_fails_for:
            return self._reply(d, {"ok": False})
        self.takes += 1
        el = self.sun_el
        glow = max(0.0, math.sin(math.radians(max(el + 4.0, 0.0)))) * 0.8 + 0.05
        ground = 0.1 + 0.5 * glow
        if self.pop_in_sky and name == self.pop_in_sky and el < -0.45:
            ground += 0.35
        im = Image.new("RGB", (320, 180))
        px = im.load()
        for y in range(180):
            for x in range(320):
                v = ground if y > 120 else (glow * 0.9 + 0.1 if y > 80 else 0.2 + 0.4 * glow)
                px[x, y] = tuple(int(255 * min(1.0, max(0.0, v))) for _ in range(3))
        if self.takes in self.bad_takes:
            for y in range(180 if self.bad_kind == "black" else 72):
                for x in range(320):
                    px[x, y] = (0, 0, 0)
        buf = io.BytesIO()
        im.save(buf, "BMP")
        pathlib.Path(d["filename"]).parent.mkdir(parents=True, exist_ok=True)
        pathlib.Path(d["filename"]).write_bytes(buf.getvalue())
        self._reply(d, {"ok": True})
