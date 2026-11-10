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


class FakeViewer:
    def __init__(self, pop_in_sky=None, snapshot_fails_for=None, drop_ops=(), initial_combo=REGION_DEFAULT,
                 water_combo=REGION_DEFAULT, dc_combo=REGION_DEFAULT, on_op=None, personal_open=False, break_after_ops=0, crowd=0, fixed_open=False):
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
                                                    "LLViewerWindow", "LLAgent", "LLInventory", "LLStats")})
        elif op == "getAPI":
            ops = {"LLStartUp": ["postStartupState", "getStateTable"], "LLViewerControl": ["get", "set", "groups"],
                   "LLFloaterReg": ["showInstance", "hideInstance", "instanceVisible", "clickButton"],
                   "UI": ["getValue", "setSelectedByValue"],
                   "LLWindow": ["getPaths", "getInfo", "keyDown", "pasteText", "mouseDown", "mouseUp"],
                   "LLViewerWindow": ["saveSnapshot"],
                   "LLAgent": ["getPosition", "setCameraParams", "setFollowCamActive", "removeCameraParams"],
                   "LLInventory": ["getBasicFolderID", "collectDescendantsIf", "getItemsInfo"],
                   "LLStats": ["getPerfData"]}[d["api"]]
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
        elif op in ("setFollowCamActive", "removeCameraParams"):
            self.follow_cam = None

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

    def api_LLStats(self, op, d):
        self._reply(d, {"stats": {"frametime": {"fps": [60.0] * 40, "frame_time_ms": [16.6] * 40}}})

    def api_LLViewerWindow(self, op, d):
        from PIL import Image  # noqa: PLC0415
        name = next((n for n, s in SKIES.items() if s[0] == self.current_sky), "")
        if self.snapshot_fails_for and name == self.snapshot_fails_for:
            return self._reply(d, {"ok": False})
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
        buf = io.BytesIO()
        im.save(buf, "BMP")
        pathlib.Path(d["filename"]).parent.mkdir(parents=True, exist_ok=True)
        pathlib.Path(d["filename"]).write_bytes(buf.getvalue())
        self._reply(d, {"ok": True})
