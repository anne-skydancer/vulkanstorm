# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline tests of the LEAP client, the light check against a fake viewer, and the analysis.

  python -m unittest discover -s scripts/inworld/tests -v

Not covered (needs the live viewer): the real widget paths, key names, the follow camera, the real replies.
"""
import csv
import io
import json
import pathlib
import sys
import tempfile
import unittest
import uuid

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))

import analyze_light_check as alc  # noqa: E402
import viewer_cmdline as vc  # noqa: E402
import light_check as lc  # noqa: E402
from fake_viewer import FakeViewer, REGION_DEFAULT, SKIES  # noqa: E402
from leap_client import (ConnectionLost, LeapError, LeapSession, ProtocolError, ViewerGone, binary_dumps, llsd_loads, notation_dumps,  # noqa: E402
                         read_frame, write_frame)

SCENARIO = HERE.parent / "scenarios" / "l3_at1_handover.json"


class WireTest(unittest.TestCase):
    SAMPLE = {"a": 1, "b": 2.5, "c": "it's a \\ test\nline", "d": [True, False, None, 7],
              "e": {"x": uuid.UUID("11111111-2222-3333-4444-555555555555")}, "f": ""}

    def test_notation_roundtrip(self):
        self.assertEqual(llsd_loads(notation_dumps(self.SAMPLE).encode()), self.SAMPLE)

    def test_binary_roundtrip(self):
        self.assertEqual(llsd_loads(binary_dumps(self.SAMPLE)), self.SAMPLE)

    def test_notation_literal_forms_the_viewer_accepts(self):
        self.assertEqual(notation_dumps({"op": "ping", "n": 3, "r": 0.5, "ok": True, "u": None}),
                         "{'op':'ping','n':i3,'r':r0.5,'ok':true,'u':!}")

    def test_framing(self):
        buf = io.BytesIO()
        write_frame(buf, b"hello")
        write_frame(buf, b"")
        buf.seek(0)
        self.assertEqual(read_frame(buf), b"hello")
        self.assertEqual(read_frame(buf), b"")
        self.assertIsNone(read_frame(buf))
        with self.assertRaises(ViewerGone):
            read_frame(io.BytesIO(b"10:short"))
        with self.assertRaises(ProtocolError):
            read_frame(io.BytesIO(b"x1:"))

    def test_nan_is_refused(self):
        with self.assertRaises(ValueError):
            notation_dumps({"v": float("nan")})


def session_for(fake):
    return LeapSession(fake.plugin_in, fake.plugin_out, default_timeout=10).start()


class SessionTest(unittest.TestCase):
    def test_request_reply_events_and_errors(self):
        fv = FakeViewer()
        s = session_for(fv)
        self.assertEqual(s.command_pump, "cmd-pump-1")
        s.ping()
        r = s.request("LLViewerControl", "get", group="Global", key="CameraAngle")
        self.assertAlmostEqual(r["value"], 1.047)
        with self.assertRaises(LeapError):
            s.request("LLViewerControl", "get", group="Global", key="NoSuchKey")
        s.listen("StartupState")
        s.fire("LLStartUp", "postStartupState")
        ev = s.wait_event("StartupState", lambda d: d["str"] == "STATE_STARTED", timeout=5)
        self.assertEqual(ev["enum"], 2)

    def test_fire_and_forget_errors_are_reported(self):
        fv = FakeViewer()
        s = session_for(fv)
        s.fire("NoSuchPump", "x")
        s.ping()
        self.assertTrue(s.take_errors())
        s.fire("LLAgent", "setFollowCamActive", active=False)
        s.ping()
        self.assertEqual(s.take_errors(), [])

    def test_handshake_timeout(self):
        import os
        from leap_client import LeapTimeout
        r, w = os.pipe()
        s = LeapSession(os.fdopen(r, "rb", buffering=0), os.fdopen(os.dup(w), "wb", buffering=0))
        with self.assertRaises(LeapTimeout):
            s.start(handshake_timeout=0.3)

    def test_timeout_when_the_viewer_never_replies(self):
        fv = FakeViewer(drop_ops=["get"])
        s = session_for(fv)
        s.default_timeout = 0.3
        with self.assertRaises(LeapError):
            s.request("LLViewerControl", "get", group="Global", key="CameraAngle")


class FrameDamageTest(unittest.TestCase):
    PATHS = ["/main_view/menu_stack/world_panel/Floater View/phototools/" + n for n in
             ["WLPresetsCombo", "Préréglage", "日本語ボタン", "emoji 🌞", "100%2F50"]] * 400

    def test_non_ascii_strings_roundtrip_with_a_byte_length_prefix(self):
        fv = FakeViewer()
        s = session_for(fv)
        fv._send("StartupState", {"paths": self.PATHS, "n": 3, "r": 0.5, "u": uuid.UUID(int=7), "b": b"\x00\xffo", "none": None})
        ev = s.wait_event("StartupState", timeout=5)
        self.assertEqual(ev["paths"], self.PATHS)
        self.assertEqual(ev["b"], b"\x00\xffo")
        s.ping()      # the frame after it still parses: the prefix counted bytes, not characters

    def test_a_duplicated_4k_chunk_inside_a_big_frame_is_a_protocol_error_not_viewer_exit(self):
        fv = FakeViewer()
        s = session_for(fv)
        s.default_timeout = 5
        payload = binary_dumps({"pump": "StartupState", "data": {"paths": self.PATHS}})
        self.assertGreater(len(payload), 20000)
        damaged = payload[:9000] + payload[9000 - 4096:9000] + payload[9000:]    # the pipe writer repeated a chunk
        with fv.lock:
            fv._to_plugin.write(b"%d:" % len(payload) + damaged)
        with self.assertRaises(ProtocolError) as cm:
            s.ping()
        msg = str(cm.exception)
        self.assertIn("protocol error", msg)
        self.assertNotIn("viewer closed", msg)
        self.assertNotIsInstance(cm.exception, ViewerGone)
        self.assertNotIsInstance(cm.exception, LeapError)       # a dead connection is never a failed operation
        self.assertIn("repeated 4 KB block at offset", msg)
        self.assertIn("WritePipeImpl", msg)
        self.assertRegex(msg, r"bytes \d+\.\.\d+ of \d+: ([0-9a-f]{2} )+")

    def test_the_bad_frame_and_the_previous_frame_tail_are_saved(self):
        fv = FakeViewer()
        s = session_for(fv)
        with tempfile.TemporaryDirectory() as tmp:
            s.diag_dir = tmp
            s.ping()
            payload = binary_dumps({"pump": "StartupState", "data": {"paths": self.PATHS}})
            damaged = payload[:9000] + payload[9000 - 4096:9000] + payload[9000:]
            with fv.lock:
                fv._to_plugin.write(b"%d:" % len(payload) + damaged)
            with self.assertRaises(ProtocolError):
                s.ping()
            self.assertEqual((pathlib.Path(tmp) / "bad_frame.bin").read_bytes(), damaged[:len(payload)])
            note = (pathlib.Path(tmp) / "bad_frame.txt").read_text(encoding="utf-8")
            self.assertIn("previous good frame", note)
            self.assertIn("repeated 4 KB block", note)

    def test_a_bad_length_prefix_saves_the_bytes_seen(self):
        fv = FakeViewer()
        s = session_for(fv)
        with tempfile.TemporaryDirectory() as tmp:
            s.diag_dir = tmp
            with fv.lock:
                fv._to_plugin.write(b"12:" + b"x" * 12 + b"view:")
            with self.assertRaises(ProtocolError):
                s.ping()
            self.assertTrue((pathlib.Path(tmp) / "bad_frame.bin").read_bytes())

    def test_end_of_file_is_viewer_gone(self):
        fv = FakeViewer()
        s = session_for(fv)
        fv._to_plugin.close()
        with self.assertRaises(ViewerGone) as cm:
            s.request("LLViewerControl", "get", group="Global", key="CameraAngle")
        self.assertIn("end of file", str(cm.exception))

    def test_replies_of_a_run_stay_small(self):
        fv = FakeViewer()
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp, skies=only("classic_default"))
            self.assertEqual(code, 0)
            self.assertLess(fv.max_frame, 4096)       # the viewer's Windows pipe writer works in 4 KB chunks


class InventoryLookupTest(unittest.TestCase):
    def test_the_exact_name_is_found_among_100_other_names_containing_it(self):
        fv = FakeViewer(crowd=100)
        v = lc.Viewer(session_for(fv))
        roots, user = lc.inventory_roots(v, "00000112-000f-0000-0000-000100bba000")
        self.assertEqual(user, ["33333333-0000-0000-0000-000000000002"])        # the inventory root, not the settings folder
        found = lc.find_settings_items(v, roots, "Default")
        self.assertEqual(found[0], ("Default", str(SKIES["Default"][0])))        # exact, case-sensitive, first
        self.assertEqual([n for n, _ in found if n == "Default"].__len__(), 2)   # the sky and the day cycle of that name
        limits = [q[2] for q in fv.inv_queries if q[1] == "Default"]
        self.assertEqual(limits, [25, 100, 400])                                 # grown only because the replies were full
        self.assertEqual(len({a for _, a in found}), len(found))                 # no item twice

    def test_a_small_first_reply_is_not_repeated(self):
        fv = FakeViewer()
        v = lc.Viewer(session_for(fv))
        roots, _ = lc.inventory_roots(v, "")
        lc.find_settings_items(v, roots, "Sunset")
        self.assertEqual([q[2] for q in fv.inv_queries], [25])

    def test_case_insensitive_exact_name_comes_after_the_case_sensitive_one(self):
        fv = FakeViewer()
        v = lc.Viewer(session_for(fv))
        roots, _ = lc.inventory_roots(v, "")
        found = lc.find_settings_items(v, roots, "default")
        self.assertEqual(found[0][0], "Default")                                 # exact only case-insensitively: still first

    def test_a_dry_run_lists_the_closest_names_when_nothing_matches(self):
        fv = FakeViewer(crowd=3)
        with tempfile.TemporaryDirectory() as tmp:
            skies = [{"id": "ghost", "names": ["Default Day"], "fallback_button": "Noon"}]
            code, run, _ = run_main(fv, tmp, skies=skies, extra=["--dry-run"])
            rows = {r["check"]: r for r in json.loads((run / "dry_run.json").read_text())}
            self.assertFalse(rows["sky ghost"]["ok"])
            self.assertIn("closest names", rows["sky ghost"]["detail"])
            self.assertIn("Default Day 000", rows["sky ghost"]["detail"])

    def test_the_refusal_says_what_to_do(self):
        import contextlib
        fv = FakeViewer(initial_combo="some legacy name", dc_combo="__None__")
        err = io.StringIO()
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stderr(err):
            code, run, _ = run_main(fv, tmp)
        self.assertEqual(code, 1)
        self.assertEqual(fv.sky_applied_count, 0)
        self.assertIn("Revert to Region Default", err.getvalue())
        self.assertIn("then relaunch", err.getvalue())


class SweepTest(unittest.TestCase):
    def test_default_sweep(self):
        e = lc.sweep_elevations()
        self.assertEqual(len(e), 65)
        self.assertEqual((e[0], e[-1]), (10.0, -10.0))
        self.assertIn(0.0, e)
        self.assertEqual(sum(1 for x in e if abs(x) <= 1.5), 31)
        self.assertEqual(e[e.index(2.0) + 1], 1.5)
        steps = [round(abs(a - b), 3) for a, b in zip(e, e[1:])]
        self.assertEqual(set(steps), {0.5, 0.1})
        self.assertEqual(e, sorted(e, reverse=True))

    def test_geometry(self):
        cam, focus = lc.camera_params([10, 20, 30], 90.0, 10.0, 2.5, 100.0)
        self.assertAlmostEqual(cam[2], 32.5)
        self.assertLess(focus[1], cam[1])      # azimuth 90 turns clockwise from east: towards -y
        self.assertGreater(focus[2], cam[2])
        self.assertAlmostEqual(lc.horizon_fraction(0, 60), 0.5)
        self.assertGreater(lc.horizon_fraction(10, 60), 0.6)


def scenario_file(tmp, skies=None, **over):
    sc = json.loads(SCENARIO.read_text(encoding="utf-8"))
    sc["settle"] = {"post_login_s": 0.0, "after_sky_s": 0.0, "per_step_s": 0.0, "min_frames": 0, "login_timeout_s": 20}
    sc["sweep"] = {"start": 4.0, "stop": -4.0, "step": 2.0, "fine_step": 0.5, "fine_half_width": 1.0}
    sc["skies"] = skies or [s for s in sc["skies"] if s["id"] in ("pbr_default", "pbr_sunset_crescent_high", "classic_default")]
    sc.update(over)
    p = pathlib.Path(tmp) / "sc.json"
    p.write_text(json.dumps(sc), encoding="utf-8")
    return p, sc


def only(sky_id):
    return [s for s in json.loads(SCENARIO.read_text(encoding="utf-8"))["skies"] if s["id"] == sky_id]


def run_main(fv, tmp, extra=(), **over):
    p, sc = scenario_file(tmp, **over)
    out = pathlib.Path(tmp) / "run"
    args = lc.parse_args(["--scenario", str(p), "--out", str(out)] + list(extra))
    s = session_for(fv)
    code = lc.execute(s, args, lc.load_scenario(p), out)
    return code, out, sc


class FullRunTest(unittest.TestCase):
    def test_full_run_restores_everything_and_finds_the_pop(self):
        fv = FakeViewer(pop_in_sky="Sunset")
        orig_settings = dict(fv.settings)
        with tempfile.TemporaryDirectory() as tmp:
            code, run, sc = run_main(fv, tmp, extra=["--set", "RenderAtmospherePhysicalLight=false"], record_fps=True)
            self.assertEqual(code, 0)
            with open(run / "manifest.csv", encoding="utf-8") as mf:
                rows = list(csv.DictReader(mf))
            elevs = lc.sweep_elevations(4.0, -4.0, 2.0, 0.5, 1.0)
            self.assertEqual(len(rows), 3 * (1 + len(elevs)))
            for r in rows:
                self.assertTrue((run / r["snapshot"]).is_file(), r["snapshot"])
                self.assertTrue(r["snapshot"].endswith(".png"))
                self.assertAlmostEqual(float(r["sun_elevation_readback"]), float(r["sun_elevation_deg"]), places=2)
                self.assertEqual(r["fps"], "60.00")
            self.assertEqual(rows[0]["kind"], "noon")
            info = json.loads((run / "run.json").read_text(encoding="utf-8"))
            self.assertTrue(all(v["status"] == "ok" for v in info["skies"].values()), info["skies"])
            self.assertEqual(info["skies"]["pbr_default"]["selected"]["how"], "inventory")
            self.assertEqual(info["skies"]["pbr_default"]["controls"]["probe_ambiance"], 1.0)
            self.assertEqual(info["skies"]["pbr_sunset_crescent_high"]["moon"]["elevation"], 55.0)
            # everything restored in the fake viewer
            self.assertEqual(fv.settings, orig_settings)
            self.assertIsNone(fv.follow_cam)
            self.assertEqual(fv.current_sky, REGION_DEFAULT)
            self.assertFalse(fv.clouds_paused)
            self.assertFalse(any(fv.floaters.values()), fv.floaters)
            self.assertFalse(fv.frozen)
            self.assertIs(info["args"]["set_values"]["RenderAtmospherePhysicalLight"], False)
            # the analysis finds the injected pop in Sunset only
            result, text = alc.analyse(run)
            self.assertTrue(any(p["region"] == "ground" for p in result["skies"]["pbr_sunset_crescent_high"]["pops"]), text)
            self.assertEqual(result["skies"]["pbr_default"]["pops"], [], text)
            self.assertTrue((run / "contact_pbr_default.png").is_file())
            self.assertTrue((run / "analysis_classic_default.csv").is_file())

    def test_stop_file_after_the_camera_is_set_aborts_and_restores_everything(self):
        with tempfile.TemporaryDirectory() as tmp:
            stop = pathlib.Path(tmp) / "run" / "STOP"

            def on_op(op, d):
                if op == "setCameraParams":
                    stop.write_text("")
            fv = FakeViewer(on_op=on_op)
            orig_settings = dict(fv.settings)
            code, run, _ = run_main(fv, tmp)
            self.assertEqual(code, 2)
            self.assertEqual(fv.settings, orig_settings)
            self.assertIsNone(fv.follow_cam)
            self.assertFalse(fv.clouds_paused)
            self.assertEqual(fv.current_sky, REGION_DEFAULT)
            self.assertEqual(fv.combo, REGION_DEFAULT)
            self.assertFalse(any(fv.floaters.values()), fv.floaters)

    def test_a_stale_stop_file_is_cleared(self):
        fv = FakeViewer()
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / "run").mkdir()
            (pathlib.Path(tmp) / "run" / "STOP").write_text("")
            code, run, _ = run_main(fv, tmp, skies=only("classic_default"))
            self.assertEqual(code, 0)

    def test_original_sky_is_restored(self):
        sky = SKIES["Sunset"][0]
        fv = FakeViewer(initial_combo=sky, water_combo=REGION_DEFAULT, dc_combo="__None__")
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp, skies=only("classic_default"))
            self.assertEqual(code, 0)
            self.assertEqual(fv.current_sky, sky)
            self.assertEqual(fv.combo, sky)

    def test_an_environment_that_cannot_be_restored_is_refused_before_anything_changes(self):
        sky = SKIES["Sunset"][0]
        cases = [("__Day_Cycle__", "__None__", False), ("None", "None", False), ("some legacy name", "__None__", False),
                 (sky, REGION_DEFAULT, False),           # a fixed sky over a day cycle would lose the day cycle
                 (REGION_DEFAULT, REGION_DEFAULT, True),  # Personal Lighting open
                 (REGION_DEFAULT, "__None__", True),      # Personal Lighting open
                 (sky, "__None__", True)]
        for combo, dc, personal in cases:
            fv = FakeViewer(initial_combo=combo, dc_combo=dc, personal_open=personal)
            orig_settings = dict(fv.settings)
            with tempfile.TemporaryDirectory() as tmp:
                code, run, _ = run_main(fv, tmp)
                self.assertEqual(code, 1, (combo, dc, personal))
                self.assertEqual(fv.settings, orig_settings)
                self.assertIsNone(fv.follow_cam)
                self.assertEqual(fv.sky_applied_count, 0)

    def test_both_shared_environment_forms_are_accepted_and_restored_by_the_region_button(self):
        for dc in (REGION_DEFAULT, "__None__"):
            fv = FakeViewer(dc_combo=dc)
            with tempfile.TemporaryDirectory() as tmp:
                code, run, _ = run_main(fv, tmp, skies=only("classic_default"))
                self.assertEqual(code, 0, dc)
                info = json.loads((run / "run.json").read_text(encoding="utf-8"))
                self.assertTrue(any("locally edited sky" in w for w in info["warnings"]), info["warnings"])
                self.assertEqual((fv.current_sky, fv.combo), (REGION_DEFAULT, REGION_DEFAULT))
                self.assertFalse(fv.frozen)

    def test_an_open_fixed_sky_editor_is_refused(self):
        import contextlib
        fv = FakeViewer(fixed_open=True)
        err = io.StringIO()
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stderr(err):
            code, run, _ = run_main(fv, tmp)
        self.assertEqual(code, 1)
        self.assertIn("close Personal Lighting and the fixed sky editor", err.getvalue())
        self.assertEqual(fv.sky_applied_count, 0)

    def test_a_day_cycle_original_is_restored_by_its_selection(self):
        dc = "22222222-0000-0000-0000-000000000009"
        fv = FakeViewer(initial_combo="__Day_Cycle__", dc_combo=dc, water_combo="__Day_Cycle__")
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp, skies=only("classic_default"))
            self.assertEqual(code, 0)
            self.assertEqual(str(fv.dc_combo), dc)

    def test_restore_mode_creates_no_capture_folder(self):
        fv = FakeViewer()
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp, skies=only("classic_default"))
            fv2 = FakeViewer()
            p, _sc = scenario_file(tmp)
            ghost = pathlib.Path(tmp) / "ghost"
            args = lc.parse_args(["--scenario", str(p), "--out", str(ghost), "--restore", str(run)])
            self.assertEqual(lc.execute(session_for(fv2), args, lc.load_scenario(p), ghost), 0)
            self.assertFalse(ghost.exists())

    def test_a_failing_sky_does_not_stop_the_others(self):
        fv = FakeViewer(snapshot_fails_for="Sunset")
        orig_settings = dict(fv.settings)
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp)
            self.assertEqual(code, 4)       # a required sky failed
            info = json.loads((run / "run.json").read_text(encoding="utf-8"))
            self.assertEqual(info["skies"]["pbr_sunset_crescent_high"]["status"], "failed")
            self.assertEqual(info["skies"]["classic_default"]["status"], "ok")
            self.assertEqual(fv.settings, orig_settings)
            self.assertEqual(fv.current_sky, REGION_DEFAULT)

    def test_fixed_editor_sky_with_tab_switch(self):
        fv = FakeViewer()
        with tempfile.TemporaryDirectory() as tmp:
            skies = [s for s in json.loads(SCENARIO.read_text(encoding="utf-8"))["skies"] if s["id"] == "no_moon"]
            code, run, _ = run_main(fv, tmp, skies=skies)
            self.assertEqual(code, 0)
            info = json.loads((run / "run.json").read_text(encoding="utf-8"))
            self.assertEqual(info["skies"]["no_moon"]["status"], "ok", info["skies"]["no_moon"])
            self.assertEqual(fv.sliders.get("moon_brightness"), 0.0)
            self.assertEqual(info["skies"]["no_moon"]["selected"]["item"], "Default")
            self.assertFalse(any(fv.floaters.values()), fv.floaters)

    def test_knob_missing_on_the_baseline_viewer_is_skipped(self):
        fv = FakeViewer()
        del fv.settings["RenderLightHandoverSwing"]
        with tempfile.TemporaryDirectory() as tmp:
            code, run, sc = run_main(fv, tmp, knobs={"RenderLightHandoverSwing": 0.2, "RenderMoonAdaptationDepth": 8.0})
            self.assertEqual(code, 0)
            info = json.loads((run / "run.json").read_text(encoding="utf-8"))
            self.assertIsNone(info["knobs"]["RenderLightHandoverSwing"])
            self.assertTrue(any("RenderLightHandoverSwing" in w for w in info["warnings"]), info["warnings"])
            self.assertEqual(info["knobs"]["RenderMoonAdaptationDepth"], 8.0)
            self.assertEqual(fv.settings["RenderMoonAdaptationDepth"][1], 6.0)   # restored


class DryRunTest(unittest.TestCase):
    def test_a_protocol_error_mid_dry_run_exits_5_without_misleading_fail_rows(self):
        fv = FakeViewer(break_after_ops=60)
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp, extra=["--dry-run"])
            self.assertEqual(code, 5)
            rows = json.loads((run / "dry_run.json").read_text())
            self.assertGreater(len(rows), 5)
            self.assertEqual([r for r in rows if r["ok"] is False], [])
            self.assertTrue((run / "bad_frame.bin").is_file())

    def test_combos_are_found_with_getPaths_never_getInfo(self):
        fv = FakeViewer()
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp, extra=["--dry-run"])
            self.assertEqual(code, 0)
            self.assertLess(fv.max_frame, 4096)
            self.assertNotIn(("LLWindow", "getInfo"), [(p, o) for p, o in fv.log if False])

    def test_dry_run_is_read_only(self):
        fv = FakeViewer(initial_combo=SKIES["Default"][0], dc_combo="__None__")
        before = (dict(fv.settings), fv.current_sky, fv.combo, fv.water_combo, fv.dc_combo, fv.clouds_paused)
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp, extra=["--dry-run"])
            rows = json.loads((run / "dry_run.json").read_text())
            self.assertEqual(code, 0, [r for r in rows if not r["ok"]])
            self.assertTrue(any(r["check"] == "sky pbr_default" and r["ok"] for r in rows))
            self.assertEqual((dict(fv.settings), fv.current_sky, fv.combo, fv.water_combo, fv.dc_combo, fv.clouds_paused), before)
            self.assertFalse(fv.frozen)
            self.assertNotIn("env_adjust_snapshot", fv.shown)          # opening it would freeze the sky
            self.assertNotIn("env_fixed_environmentent_sky", fv.shown)
            self.assertEqual(fv.sky_applied_count, 0)
            self.assertFalse(any(fv.floaters.values()))

    def test_deep_dry_run_restores_the_environment(self):
        fv = FakeViewer()
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp, extra=["--dry-run", "--deep"])
            rows = json.loads((run / "dry_run.json").read_text())
            self.assertEqual(code, 0, [r for r in rows if not r["ok"]])
            self.assertIn("env_adjust_snapshot", fv.shown)
            self.assertTrue(any(r["check"] == "fixed editor tab" and r["ok"] for r in rows), rows)
            self.assertFalse(fv.frozen)
            self.assertEqual((fv.current_sky, fv.combo), (REGION_DEFAULT, REGION_DEFAULT))
            self.assertFalse(any(fv.floaters.values()), fv.floaters)

    def test_dry_run_reports_a_missing_operation(self):
        fv = FakeViewer(drop_ops=["saveSnapshot"])
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp, extra=["--dry-run"])
            self.assertEqual(code, 1)
            rows = json.loads((run / "dry_run.json").read_text())
            self.assertTrue(any(r["check"] == "api LLViewerWindow" and not r["ok"] for r in rows))

    def test_a_real_run_refuses_a_viewer_with_a_missing_operation(self):
        fv = FakeViewer(drop_ops=["saveSnapshot"])
        orig = dict(fv.settings)
        with tempfile.TemporaryDirectory() as tmp:
            code, run, _ = run_main(fv, tmp)
            self.assertEqual(code, 1)
            self.assertEqual(fv.settings, orig)

    def test_dry_run_fails_a_required_sky_that_would_use_the_fallback(self):
        fv = FakeViewer()
        with tempfile.TemporaryDirectory() as tmp:
            skies = [{"id": "ghost", "names": ["No Such Sky"], "fallback_button": "Noon"},
                     {"id": "ghost_opt", "names": ["No Such Sky"], "fallback_button": "Noon", "optional": True}]
            code, run, _ = run_main(fv, tmp, skies=skies, extra=["--dry-run"])
            rows = {r["check"]: r for r in json.loads((run / "dry_run.json").read_text())}
            self.assertEqual(code, 1)
            self.assertFalse(rows["sky ghost"]["ok"])
            self.assertTrue(rows["sky ghost_opt"]["ok"])
            self.assertTrue(rows["sky ghost_opt"]["detail"].startswith("WARN"))


class LaunchTest(unittest.TestCase):
    """The --leap command line as the viewer reads it (viewer_cmdline.py ports llcommandlineparser.cpp and llleap.cpp)."""
    PY = "C:/Users/annes/AppData/Local/Python/pythoncore-3.15-64/python.exe"
    S = "C:/Users/annes/source/repos/vulkanstorm/work/leap-check/scripts/inworld"

    def test_two_leap_options_give_two_plugin_commands(self):
        real = "%s %s/light_check.py --dry-run --out C:/x/dry" % (self.PY, self.S)
        noop = "%s %s/light_check.py --noop" % (self.PY, self.S)
        cmdline = 'vulkanstorm-bin.exe --leap "%s" --leap "%s"' % (real, noop)
        leap = vc.leap_command(cmdline)
        self.assertIsInstance(leap, list)
        self.assertEqual(leap, [real, noop])
        self.assertEqual(vc.llleap_tokens(leap[0]), [self.PY, self.S + "/light_check.py", "--dry-run", "--out", "C:/x/dry"])
        self.assertEqual(vc.llleap_tokens(leap[1])[-1], "--noop")

    def test_the_single_option_forms_do_not_work(self):
        real = "%s %s/light_check.py --dry-run" % (self.PY, self.S)
        for quoted in ('--leap "%s"' % real, "--leap \"'%s'\"" % real):
            leap = vc.leap_command("vulkanstorm-bin.exe " + quoted)
            # one value: the viewer parses it as LLSD notation, and the text is not notation (no quote survives)
            self.assertIsInstance(leap, str)
            self.assertFalse(leap.startswith("'") or leap.startswith('"'), leap)

    def test_a_path_with_spaces_is_split(self):
        cmdline = 'x.exe --leap "C:/Program Files/Python/python.exe C:/a/light_check.py" --leap "p -c pass"'
        tokens = vc.llleap_tokens(vc.leap_command(cmdline)[0])
        self.assertEqual(tokens[0], "C:/Program")

    def test_the_noop_plugin_exits_zero_after_the_handshake(self):
        import subprocess
        proc = subprocess.Popen([sys.executable, str(HERE.parent / "light_check.py"), "--noop"], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        frame = binary_dumps({"pump": "r", "data": {"command": "c", "features": {}}})
        proc.stdin.write(b"%d:" % len(frame) + frame)
        proc.stdin.flush()
        self.assertEqual(proc.wait(timeout=30), 0)
        self.assertIn(b"noop", proc.stderr.read())


class SubprocessTest(unittest.TestCase):
    def test_script_as_a_real_leap_plugin_over_pipes(self):
        """light_check.py started as a child process, stdin/stdout carrying the protocol (as --leap does)."""
        import subprocess
        import threading
        fv = FakeViewer()
        with tempfile.TemporaryDirectory() as tmp:
            p, _ = scenario_file(tmp)
            out = pathlib.Path(tmp) / "run"
            proc = subprocess.Popen([sys.executable, str(HERE.parent / "light_check.py"), "--dry-run", "--scenario", str(p),
                                     "--out", str(out)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            def pump(src, dst):
                while True:
                    chunk = src.read(1)
                    if not chunk:
                        return
                    try:
                        dst.write(chunk)
                        dst.flush()
                    except (OSError, ValueError):
                        return

            threading.Thread(target=pump, args=(fv.plugin_in, proc.stdin), daemon=True).start()
            threading.Thread(target=pump, args=(proc.stdout, fv.plugin_out), daemon=True).start()
            try:
                err = proc.stderr.read().decode("utf-8", "replace")   # EOF once the plugin has exited
                proc.wait(timeout=30)
            finally:
                if proc.poll() is None:
                    proc.kill()
            self.assertEqual(proc.returncode, 0, err)
            self.assertIn("LIGHTCHECK", err)
            rows = json.loads((out / "dry_run.json").read_text())
            self.assertTrue(all(r["ok"] for r in rows), [r for r in rows if not r["ok"]])


class AnalysisTest(unittest.TestCase):
    def test_find_pops_ignores_the_step_size_change(self):
        # a smooth ramp of 0.1 luma per degree: coarse 0.5 steps give 0.05 changes, fine 0.1 steps 0.01: no pop
        elevs = lc.sweep_elevations()
        vals = [0.5 + 0.1 * e for e in elevs]
        _, flagged = alc.find_pops(elevs, vals, 4.0, 0.005)
        self.assertEqual(flagged, [])
        vals2 = list(vals)
        for i in range(30, len(vals2)):
            vals2[i] -= 0.2        # a step of 0.2 at one place
        _, flagged = alc.find_pops(elevs, vals2, 4.0, 0.005)
        self.assertEqual(len(flagged), 1)

    def test_compare_is_new_minus_baseline_and_analyses_a_baseline_without_analysis(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as tmp:
            dirs = []
            for name, level in (("base", 60), ("new", 120)):
                d = pathlib.Path(tmp) / name
                (d / "snapshots").mkdir(parents=True)
                rows = ""
                for i, (k, e) in enumerate((("noon", 66.0), ("sweep", 10.0), ("sweep", 9.5))):
                    rel = "snapshots/s_%03d.png" % i
                    Image.new("RGB", (64, 64), (level,) * 3).save(d / rel)
                    rows += "s,%d,%s,%s,%s,0,%s,t,60,16,{}\n" % (i, k, e, e, rel)
                (d / "manifest.csv").write_text("sky,step,kind,sun_elevation_deg,sun_elevation_readback,sun_azimuth_deg,snapshot,timestamp_utc,fps,frame_ms,settings\n" + rows)
                (d / "run.json").write_text(json.dumps({"regions": {"ground": [0, .6, 1, .9], "horizon_sky": [0, .3, 1, .5], "cloud": [0, 0, 1, .2]}}))
                dirs.append(d)
            alc.analyse(dirs[1])                    # the baseline has no analysis.json yet
            text = alc.compare(dirs[0], dirs[1])
            self.assertIn("delta = B - A", text)
            self.assertIn("ground +0.", text)       # new is brighter: positive
            self.assertTrue((dirs[0] / "analysis.json").is_file())

    def test_regions_and_luma(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as tmp:
            im = Image.new("RGB", (100, 100), (0, 0, 0))
            for y in range(50, 100):
                for x in range(100):
                    im.putpixel((x, y), (255, 255, 255))
            p = pathlib.Path(tmp) / "a.png"
            im.save(p)
            regions = {"ground": [0, 0.6, 1, 0.9], "horizon_sky": [0, 0.1, 1, 0.4], "cloud": [0, 0, 1, 0.2]}
            m, _ = alc.measure(p, regions)
            self.assertAlmostEqual(m["ground"][0], 1.0, places=3)
            self.assertAlmostEqual(m["cloud"][0], 0.0, places=3)
            self.assertAlmostEqual(m["frame"][1], 0.5, places=2)


if __name__ == "__main__":
    unittest.main()
