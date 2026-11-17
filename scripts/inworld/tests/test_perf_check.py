# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline tests of perf_check.py and analyze_perf_check.py against a fake viewer with the GLSubmitStats API.

  python -m unittest discover -s scripts/inworld/tests -v

Not covered (needs the live development viewer): the real replies, the real GPU timers, the check on real targets.
"""
import csv
import json
import pathlib
import statistics
import sys
import threading
import time
import tempfile
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))

import analyze_perf_check as apc  # noqa: E402
import light_check as lc  # noqa: E402
import perf_check as pc  # noqa: E402
from fake_viewer import FakeViewer, PoolModel  # noqa: E402
from leap_client import LeapError, LeapSession, ViewerGone  # noqa: E402

SCENARIO = HERE.parent / "scenarios" / "gl_submit_g0.json"
ALPHA_CROWD = HERE.parent / "scenarios" / "alpha_crowd.json"
ALPHA_CROWD_LOW = HERE.parent / "scenarios" / "alpha_crowd_lowtier.json"


def session_for(fake):
    return LeapSession(fake.plugin_in, fake.plugin_out, default_timeout=10).start()


def scenario_file(tmp, **over):
    sc = json.loads(SCENARIO.read_text(encoding="utf-8"))
    sc["settle"] = {"post_login_s": 0.0, "after_camera_s": 0.0, "after_teleport_s": 0.0, "login_timeout_s": 20,
                    "window_timeout_s": 10.0, "compare_wait_s": 2.0, "after_setting_s": 0.0}
    sc["window"] = {"frames": 5, "gpu": True}
    sc["overhead"] = {"probe_s": 0.2, "probe_repeats": 3, "bench_iterations": 1000}
    sc.update(over)
    p = pathlib.Path(tmp) / "sc.json"
    p.write_text(json.dumps(sc), encoding="utf-8")
    return p


def run_main(fv, tmp, extra=(), **over):
    p = scenario_file(tmp, **over)
    out = pathlib.Path(tmp) / "run"
    args = pc.parse_args(["--scenario", str(p), "--out", str(out)] + list(extra))
    with mock.patch.object(lc, "viewer_window_state", return_value=None):
        code = pc.execute(session_for(fv), args, pc.load_scenario(p), out)
    return code, out


class ScenarioTest(unittest.TestCase):
    def test_the_shipped_scenario_loads(self):
        sc = pc.load_scenario(SCENARIO)
        self.assertEqual([p["id"] for p in sc["presets"]], ["current", "north", "east", "south"])
        self.assertTrue(sc["compare"]["prove_with_injection"])

    def test_duplicate_or_unsafe_preset_ids_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            for presets in ([{"id": "a"}, {"id": "a"}], [{"id": "a/b"}], []):
                p = pathlib.Path(tmp) / "s.json"
                p.write_text(json.dumps({"presets": presets}), encoding="utf-8")
                with self.assertRaises(ValueError):
                    pc.load_scenario(p)


class RunTest(unittest.TestCase):
    def test_a_run_writes_windows_and_compares_every_pass_and_restores_the_settings(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            orig = dict(fv.settings)
            code, run = run_main(fv, tmp)
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(sorted(index["presets"]), ["current", "east", "north", "south"])
            self.assertTrue(index["proof"]["ok"])
            self.assertEqual(index["compare_failures"], [])
            for pid in index["presets"]:
                w = json.loads((run / ("window_%s.json" % pid)).read_text(encoding="utf-8"))
                self.assertEqual(w["frames"], 5)
                self.assertTrue((run / ("compare_%s.json" % pid)).is_file())
                self.assertTrue((run / ("profile_%s.json" % pid)).is_file())
            with open(run / "manifest.csv", encoding="utf-8") as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(len(rows), 4)
            self.assertEqual(index["hook_bench"]["hooks_per_draw"], 20)
            self.assertIsNotNone(index["presets"]["north"]["fps_stats_off"])
            self.assertIn("overhead_pct", index["presets"]["north"])
            self.assertEqual(rows[0]["draws"], "4200.0")
            self.assertIsNone(fv.follow_cam)       # the camera was released
            self.assertEqual(fv.settings, orig)    # nothing left changed
            # the arm of the proof used the inject setting and put it back
            self.assertEqual(fv.settings["RenderGLSubmitCompareInject"][1], 0)

    def test_a_check_that_misses_the_injected_bit_fails_the_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, gl_stats_catches_injection=False, logs_dir=tmp)
            code, run = run_main(fv, tmp)
            self.assertEqual(code, 4)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertFalse(index["proof"]["ok"])

    def test_a_pass_that_renders_differently_fails_the_run_and_is_named(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, gl_stats_diff_pass="sun cascade 2", logs_dir=tmp)
            code, run = run_main(fv, tmp, presets=[{"id": "current", "camera": None}])
            self.assertEqual(code, 4)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(index["compare_failures"][0]["passes"], ["sun cascade 2"])

    def test_a_proof_hidden_by_an_unrelated_difference_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, gl_stats_diff_pass="G-buffer", logs_dir=tmp)
            code, run = run_main(fv, tmp, presets=[{"id": "current", "camera": None}])
            self.assertEqual(code, 4)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertFalse(index["proof"]["ok"])
            self.assertIn("other than the injection", index["proof"]["why"])

    def test_stale_results_are_an_error_not_a_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(
                gl_stats=True, gl_stale_results=True, logs_dir=tmp)
            code, run = run_main(fv, tmp, presets=[{"id": "current", "camera": None}])
            self.assertEqual(code, 4)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertFalse(index["proof"]["ok"])
            self.assertIn("never ran", index["proof"]["why"])
            self.assertTrue(index["compare_failures"] and "never ran" in index["compare_failures"][0]["error"])

    def test_all_passes_not_run_fails_the_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, gl_not_run=True, logs_dir=tmp)
            code, run = run_main(fv, tmp, presets=[{"id": "current", "camera": None}])
            self.assertEqual(code, 4)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertIn("did not run", index["proof"]["why"])
            self.assertIn("G-buffer did not run", index["compare_failures"][0]["error"])

    def test_a_lagging_window_warns_and_the_run_goes_on(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, gl_gpu_lag=True, logs_dir=tmp)
            code, run = run_main(fv, tmp, presets=[{"id": "current", "camera": None}],
                                 settle={"post_login_s": 0.0, "after_camera_s": 0.0, "after_teleport_s": 0.0, "login_timeout_s": 20,
                                         "window_timeout_s": 1.0, "compare_wait_s": 2.0})
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertTrue(any("did not finish" in w for w in index["warnings"]), index["warnings"])
            self.assertEqual(index["presets"]["current"]["gpu_frames"], 4)

    def test_a_window_that_stops_short_fails_the_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, gl_short_window=True, logs_dir=tmp)
            code, run = run_main(fv, tmp, presets=[{"id": "current", "camera": None}],
                                 settle={"post_login_s": 0.0, "after_camera_s": 0.0, "after_teleport_s": 0.0, "login_timeout_s": 20,
                                         "window_timeout_s": 1.0, "compare_wait_s": 2.0})
            self.assertNotEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(index["window_failures"][0]["preset"], "current")

    def test_an_empty_gbuffer_is_noted(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, gl_empty_gbuffer=True, logs_dir=tmp)
            code, run = run_main(fv, tmp, presets=[{"id": "current", "camera": None}])
            self.assertEqual(code, 0)    # the run does not fail on it; the log says so

    def test_a_viewer_without_the_api_is_refused_with_the_reason(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=False)
            code, run = run_main(fv, tmp)
            self.assertEqual(code, 1)
            self.assertFalse((run / "perf.json").exists())

    def test_dry_run_checks_the_api_and_the_settings_and_changes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True)
            orig = dict(fv.settings)
            code, run = run_main(fv, tmp, extra=["--dry-run"])
            self.assertEqual(code, 0)
            rows = json.loads((run / "dry_run.json").read_text(encoding="utf-8"))
            self.assertTrue(all(r["status"] == "PASS" for r in rows), rows)
            self.assertEqual(fv.settings, orig)
            self.assertNotIn(("GLSubmitStats", "windowBegin"), fv.log)

    def test_no_glstats_measures_the_frame_rate_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            code, run = run_main(fv, tmp, extra=["--no-glstats"])
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(sorted(index["presets"]), ["current", "east", "north", "south"])
            for info in index["presets"].values():
                self.assertIsNotNone(info["fps_stats_off"])
                self.assertNotIn("summary", info)
            for op in ("windowBegin", "dump", "compareArm", "selfBench", "profileFrame"):
                self.assertNotIn(("GLSubmitStats", op), fv.log)

    def test_no_glstats_without_variants_records_a_median_of_probes_with_the_spread(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            code, run = run_main(fv, tmp, extra=["--no-glstats"])
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            for info in index["presets"].values():
                probe = info["fps_probe"]
                self.assertEqual(len(probe["samples"]), 3)      # overhead.probe_repeats in scenario_file
                self.assertEqual(info["fps_stats_off"], round(statistics.median(probe["samples"]), 2))
                self.assertIn("spread_pct", probe)

    def test_projector_stats_gives_none_for_an_error_reply_but_a_lost_connection_propagates(self):
        run = object.__new__(pc.Run)

        class Viewer:
            def __init__(self, exc):
                self.exc = exc

            def req(self, *_a, **_k):
                raise self.exc

        run.v = Viewer(LeapError("no such API"))
        self.assertIsNone(run.projector_stats())
        run = object.__new__(pc.Run)        # the availability answer is cached per run
        run.v = Viewer(ViewerGone("pipe closed"))
        with self.assertRaises(ViewerGone):
            run.projector_stats()

    def test_replies_stay_small(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            run_main(fv, tmp)
            self.assertLess(fv.max_frame, 4000)

    def test_only_selects_presets(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            code, run = run_main(fv, tmp, extra=["--only", "north"])
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(list(index["presets"]), ["north"])


class ProofRulesTest(unittest.TestCase):
    GOOD = [{"label": "G-buffer", "ran": True, "attachments": [
        {"name": "color0", "injected": True, "mismatched_pixels": 1}, {"name": "depth", "mismatched_pixels": 0}]}]

    def test_exactly_the_injection(self):
        self.assertEqual(pc.injection_caught(self.GOOD), (True, "caught"))

    def test_not_run_wrong_count_or_other_differences(self):
        self.assertFalse(pc.injection_caught([])[0])
        self.assertFalse(pc.injection_caught([{"label": "G-buffer", "ran": False}])[0])
        bad = json.loads(json.dumps(self.GOOD))
        bad[0]["attachments"][0]["mismatched_pixels"] = 2
        self.assertFalse(pc.injection_caught(bad)[0])
        bad = json.loads(json.dumps(self.GOOD))
        bad[0]["attachments"][1]["mismatched_pixels"] = 3
        self.assertFalse(pc.injection_caught(bad)[0])
        bad = json.loads(json.dumps(self.GOOD))
        del bad[0]["attachments"][0]["injected"]
        self.assertFalse(pc.injection_caught(bad)[0])

    def test_thin_attachments(self):
        res = [{"label": "sun cascade 0", "ran": True, "identical": True, "attachments": [{"name": "depth", "distinct_values": 1}]},
               {"label": "G-buffer", "ran": True, "identical": True, "attachments": [{"name": "color0", "distinct_values": 40}]}]
        self.assertEqual(pc.thin_attachments(res), [("sun cascade 0", "depth")])


def window_with(cpu, samples=1.0e6):
    return {"totals": {"draws": 1000.0}, "views": [{"view": "main", "cpu_ms": 8.0, "gpu_ms": 5.0}],
            "passes": [{"view": "main", "pass": "deferred lighting/geom post-deferred/ALPHA_POST_WATER 0", "cpu_ms": cpu, "draws": 400.0,
                        "samples": samples},
                       {"view": "main", "pass": "geom post-deferred/ALPHA_MASK 0", "cpu_ms": 99.0}]}


def entry(method, fps, cpu):
    return {"window": window_with(cpu), "info": {"preset": "p", "variant": "m%d" % method, "fps_stats_off": fps,
                                                "variant_settings": {"RenderAlphaSortMethod": method}}}


NO_COMPARE = {"enabled": False, "passes": [-1], "prove_with_injection": False}


class VariantsTest(unittest.TestCase):
    def test_shipped_scenarios_load_and_differ_only_in_the_tier(self):
        a, b = pc.load_scenario(ALPHA_CROWD), pc.load_scenario(ALPHA_CROWD_LOW)
        for sc in (a, b):
            self.assertEqual([v["settings"]["RenderAlphaSortMethod"] for v in sc["variants"]], [0, 1])
            self.assertFalse(sc["compare"]["enabled"])
            self.assertEqual(len(sc["presets"]), 4)
        self.assertEqual(a["settings"], {})
        self.assertEqual(b["settings"]["RenderShadowDetail"], 0)
        self.assertEqual(b["settings"]["RenderAvatarMaxNonImpostors"], 3)
        self.assertNotIn("RenderQualityPerformance", b["settings"])

    def test_bad_variant_ids_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            for variants in ([{"id": "a"}, {"id": "a"}], [{"id": "a__b"}], [{"id": ""}], [{"id": "a", "settings": 3}], ["a"]):
                p = pathlib.Path(tmp) / "s.json"
                p.write_text(json.dumps({"variants": variants}), encoding="utf-8")
                with self.assertRaises(ValueError):
                    pc.load_scenario(p)

    def test_a_run_measures_each_preset_with_each_method_and_restores(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            orig = dict(fv.settings)
            sc = json.loads(ALPHA_CROWD.read_text(encoding="utf-8"))
            code, run = run_main(fv, tmp, presets=sc["presets"][:2], variants=sc["variants"], compare=NO_COMPARE)
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(list(index["presets"]), ["current__m0", "current__m1", "north__m1", "north__m0"])   # the order alternates
            self.assertEqual(index["presets"]["north__m1"]["variant"], "m1")
            self.assertEqual(index["presets"]["north__m1"]["preset"], "north")
            self.assertTrue((run / "window_north__m0.json").is_file())
            self.assertEqual(fv.settings, orig)
            self.assertIsNone(fv.follow_cam)
            with open(run / "manifest.csv", encoding="utf-8") as f:
                rows = {r["preset"]: r for r in csv.DictReader(f)}
            self.assertEqual({k: r["variant"] for k, r in rows.items()},
                             {"current__m0": "m0", "current__m1": "m1", "north__m1": "m1", "north__m0": "m0"})
            self.assertIn("frame_rate_caps", index)
            for key, cpu in (("current__m0", 1.0), ("current__m1", 1.5), ("north__m0", 1.0), ("north__m1", 1.5)):
                w = json.loads((run / ("window_%s.json" % key)).read_text(encoding="utf-8"))
                self.assertEqual(apc.alpha_sums(w)["cpu_ms"], cpu, key)
            self.assertEqual(apc.main([str(run)]), 0)
            text = (run / "report.txt").read_text(encoding="utf-8")
            self.assertIn("sorted alpha (method 0) against stochastic (method 1)", text)
            self.assertIn("verdict", text)
            self.assertIn("[not_slower]", text)
            self.assertIn("alpha samples", text)

    def _run_variants(self, tmp, fv, variants, **over):
        return run_main(fv, tmp, presets=[{"id": "current", "camera": None}, {"id": "north", "camera": None}], variants=variants,
                        compare=NO_COMPARE, **over)

    def test_a_missing_variant_control_fails_the_run_and_restores(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            orig = dict(fv.settings)
            code, run = self._run_variants(tmp, fv, [{"id": "a", "settings": {"RenderAlphaSortMethod": 0}},
                                                    {"id": "b", "settings": {"NoSuchSetting": 1}}])
            self.assertEqual(code, 1)
            self.assertEqual(fv.settings, orig)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertNotIn("current__b", index["presets"])    # no window recorded for an unapplied variant

    def test_an_abort_between_variants_restores_the_method(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            orig = dict(fv.settings)
            real = pc.Run.one_preset
            calls = []

            def spy(self, preset, variant=None, set_camera=True, fps_samples=None):
                calls.append(variant["id"])
                if len(calls) == 2:
                    raise lc.Aborted("test stop")
                real(self, preset, variant, set_camera, fps_samples)
            sc = json.loads(ALPHA_CROWD.read_text(encoding="utf-8"))
            with mock.patch.object(pc.Run, "one_preset", spy):
                code, run = self._run_variants(tmp, fv, sc["variants"])
            self.assertEqual(code, 2)
            self.assertEqual(fv.settings, orig)    # RenderAlphaSortMethod is back at 1
            self.assertIsNone(fv.follow_cam)

    def test_ctrl_c_restores(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            orig = dict(fv.settings)
            real = pc.Run.one_preset

            def spy(self, preset, variant=None, set_camera=True, fps_samples=None):
                if variant["id"] == "m1":
                    raise KeyboardInterrupt()
                real(self, preset, variant, set_camera, fps_samples)
            sc = json.loads(ALPHA_CROWD.read_text(encoding="utf-8"))
            with mock.patch.object(pc.Run, "one_preset", spy):
                code, _ = self._run_variants(tmp, fv, sc["variants"])
            self.assertEqual(code, 2)
            self.assertEqual(fv.settings, orig)

    def test_other_keys_go_back_to_the_scenario_value_not_the_original(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            fv.settings["RenderGlow"] = ("Boolean", True)
            variants = [{"id": "a", "settings": {"RenderGlow": True}}, {"id": "b", "settings": {}}]
            seen = []
            real = pc.Run.one_preset

            def spy(self, preset, variant=None, set_camera=True, fps_samples=None):
                real(self, preset, variant, set_camera, fps_samples)
                seen.append((variant["id"], fv.settings["RenderGlow"][1]))
            with mock.patch.object(pc.Run, "one_preset", spy):
                code, _ = run_main(fv, tmp, presets=[{"id": "current", "camera": None}], variants=variants, compare=NO_COMPARE,
                                   settings={"RenderGlow": False})
            self.assertEqual(code, 0)
            self.assertEqual(seen, [("a", True), ("b", False)])    # b is back at the scenario's False, not the original True
            self.assertEqual(fv.settings["RenderGlow"][1], True)

    def test_a_slower_stochastic_gives_the_slower_verdict_end_to_end(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp, gl_m1_fps_factor=0.9)
            sc = json.loads(ALPHA_CROWD.read_text(encoding="utf-8"))
            code, run = self._run_variants(tmp, fv, sc["variants"])
            self.assertEqual(code, 0)
            entries, _ = apc.load(str(run))
            text, facts = apc.verdict(apc.method_runs(entries))
            self.assertEqual(facts["result"], "slower_explained" if facts["alpha_cpu_delta_ms"] >= 0.5 * facts["frame_delta_ms"]
                             else "slower_not_explained", text)
            self.assertGreater(facts["frame_pct"], 3.0)
            self.assertEqual(facts["alpha_cpu_delta_ms"], 0.5)

    def test_a_u32_control_that_reads_back_as_a_string_is_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp, u32_as_string=True)
            orig = dict(fv.settings)
            sc = json.loads(ALPHA_CROWD.read_text(encoding="utf-8"))
            code, run = self._run_variants(tmp, fv, sc["variants"])
            self.assertEqual(code, 0)
            self.assertEqual(fv.settings["RenderAlphaSortMethod"], orig["RenderAlphaSortMethod"])

    def test_the_frame_rate_is_probed_three_times_per_variant_alternating(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            seen = []
            real = pc.Run.measure_fps_off

            def spy(self):
                seen.append(int(fv.settings["RenderAlphaSortMethod"][1]))
                return real(self)
            sc = json.loads(ALPHA_CROWD.read_text(encoding="utf-8"))
            with mock.patch.object(pc.Run, "measure_fps_off", spy):
                code, run = run_main(fv, tmp, presets=sc["presets"][:2], variants=sc["variants"], compare=NO_COMPARE)
            self.assertEqual(code, 0)
            self.assertEqual(seen[:6], [0, 1, 1, 0, 0, 1])      # preset 0: m0 m1, m1 m0, m0 m1
            self.assertEqual(seen[6:], [1, 0, 0, 1, 1, 0])      # preset 1 starts with the other method
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            for key in ("current__m0", "current__m1", "north__m0", "north__m1"):
                pr = index["presets"][key]["fps_probe"]
                self.assertEqual(len(pr["samples"]), 3)
                self.assertIn("spread_pct", pr)
                self.assertEqual(index["presets"][key]["fps_stats_off"], sorted(pr["samples"])[1])    # the median
            self.assertIn("window_geometry", index)

    def test_implausible_alpha_samples_are_dropped_with_a_warning(self):
        w = window_with(1.0, samples=2.07e17)
        notes = []
        info = {"preset": "p", "variant": "m0", "window_geometry": {"width": 1920, "height": 1080}}
        a = apc.alpha_sums(w, info, notes)
        self.assertNotIn("samples", a)
        self.assertEqual(a["cpu_ms"], 1.0)
        self.assertEqual(len(notes), 1)
        self.assertIn("ignored", notes[0])
        ok = apc.alpha_sums(window_with(1.0, samples=5.0e6), info, [])      # 5 M samples over 400 draws at 1080p: fine
        self.assertEqual(ok["samples"], 5.0e6)
        # and the report says so instead of printing the number
        e = {"window": w, "info": dict(info, fps_stats_off=60.0)}
        e1 = {"window": window_with(1.0), "info": dict(info, variant="m1", variant_settings={"RenderAlphaSortMethod": 1}, fps_stats_off=60.0)}
        e["info"]["variant_settings"] = {"RenderAlphaSortMethod": 0}
        text = "\n".join(apc.report_methods({"p__m0": e, "p__m1": e1}))
        self.assertIn("WARNING implausible alpha samples", text)
        self.assertNotIn("2.07e", text)

    def test_a_difference_inside_the_probe_noise_is_inconclusive(self):
        def noisy(method, fps, cpu, spread):
            e = entry(method, fps, cpu)
            e["info"]["fps_probe"] = {"samples": [fps * 0.9, fps, fps * 1.1], "spread_pct": spread}
            return e
        # 5 % slower, but the probes of the same window scatter by 20 %
        run = {"p": {0: noisy(0, 100.0, 1.0, 20.0), 1: noisy(1, 95.0, 3.0, 20.0)}}
        text, facts = apc.verdict(run)
        self.assertEqual(facts["result"], "inconclusive", text)
        self.assertIn("noise", text)
        # the same difference with quiet probes is a finding
        run = {"p": {0: noisy(0, 100.0, 1.0, 1.0), 1: noisy(1, 95.0, 3.0, 1.0)}}
        self.assertEqual(apc.verdict(run)[1]["result"], "slower_explained")

    def test_verdict_rule(self):
        # 4 % slower and the alpha CPU delta explains it (0.5 ms of a ~0.69 ms difference): slower
        slow = {"p": {0: entry(0, 60.0, 1.0), 1: entry(1, 57.6, 1.5)}}
        text, facts = apc.verdict(slow)
        self.assertEqual(facts["result"], "slower_explained", text)
        self.assertIn("SLOWER", text)
        # 4 % slower but the alpha CPU time is unchanged: slower for another reason
        other = {"p": {0: entry(0, 60.0, 1.0), 1: entry(1, 57.6, 1.0)}}
        self.assertEqual(apc.verdict(other)[1]["result"], "slower_not_explained")
        self.assertIn("NOT because", apc.verdict(other)[0])
        # 2 % slower: inside the limit
        fine = {"p": {0: entry(0, 60.0, 1.0), 1: entry(1, 58.8, 3.0)}}
        self.assertEqual(apc.verdict(fine)[1]["result"], "not_slower")
        # exactly 3 % is not "more than 3 %"
        edge = {"p": {0: entry(0, 100.0, 1.0), 1: entry(1, 100.0 / 1.03, 3.0)}}
        self.assertEqual(apc.verdict(edge)[1]["result"], "not_slower")
        # the median over presets, not the mean: one slow outlier preset does not decide
        runs = {"a": {0: entry(0, 60.0, 1.0), 1: entry(1, 60.0, 1.0)}, "b": {0: entry(0, 60.0, 1.0), 1: entry(1, 60.0, 1.0)},
                "c": {0: entry(0, 60.0, 1.0), 1: entry(1, 30.0, 9.0)}}
        self.assertEqual(apc.verdict(runs)[1]["result"], "not_slower")
        self.assertIn("no verdict", apc.verdict({"p": {0: entry(0, 60.0, 1.0)}})[0])
        # presets that disagree in sign: no finding, even when the median is above the limit
        mixed = {"a": {0: entry(0, 60.0, 1.0), 1: entry(1, 50.0, 3.0)}, "b": {0: entry(0, 60.0, 1.0), 1: entry(1, 50.0, 3.0)},
                 "c": {0: entry(0, 60.0, 1.0), 1: entry(1, 70.0, 0.5)}}
        text, facts = apc.verdict(mixed)
        self.assertEqual(facts["result"], "inconclusive", text)
        self.assertLess(facts["pct_min"], 0)
        self.assertGreater(facts["pct_max"], 0)
        # a GPU-side cost shows: main view GPU delta is reported
        self.assertIn("GPU", apc.verdict(slow)[0])

    def test_a_method_1_that_drew_nothing_is_warned_about(self):
        run = {"p": {0: entry(0, 60.0, 1.0), 1: entry(1, 60.0, 1.0)}}
        run["p"][1]["window"]["passes"][0]["draws"] = 0.0
        self.assertTrue(apc.verdict(run)[1]["warnings"])
        same = {"p": {0: entry(0, 60.0, 1.0), 1: entry(1, 60.0, 1.0)}}    # equal alpha draws: also suspicious
        self.assertTrue(apc.verdict(same)[1]["warnings"])
        ok = {"p": {0: entry(0, 60.0, 1.0), 1: entry(1, 60.0, 1.0)}}
        ok["p"][1]["window"]["passes"][0]["draws"] = 300.0
        self.assertEqual(apc.verdict(ok)[1]["warnings"], [])

    def test_absent_alpha_keys_are_dashes_not_zeros(self):
        w = {"passes": [{"view": "main", "pass": "geom post-deferred/ALPHA 0", "cpu_ms": 1.0}]}
        a = apc.alpha_sums(w)
        self.assertEqual(a, {"cpu_ms": 1.0})
        self.assertEqual(apc.fmt(a.get("samples")), "-")

    def test_alpha_sums_pick_only_the_alpha_pools(self):
        a = apc.alpha_sums(window_with(2.0))
        self.assertEqual(a["cpu_ms"], 2.0)       # ALPHA_MASK (99) is not an alpha pass
        self.assertEqual(a["samples"], 1.0e6)
        self.assertIsNone(apc.alpha_sums({"passes": [{"view": "main", "pass": "gbuffer"}]}))


class AnalysisTest(unittest.TestCase):
    def test_report_and_baseline_delta(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, logs_dir=tmp)
            code, run = run_main(fv, tmp)
            self.assertEqual(code, 0)
            self.assertEqual(apc.main([str(run)]), 0)
            text = (run / "report.txt").read_text(encoding="utf-8")
            self.assertIn("preset north", text)
            self.assertIn("mean mergeable run length", text)
            self.assertIn("proof (one bit flipped", text)
            self.assertIn("CAUGHT", text)
            self.assertIn("sun cascade 0", text)
            # against itself: all deltas zero
            self.assertEqual(apc.main([str(run), "--compare", str(run)]), 0)
            self.assertIn("against the baseline run", (run / "report.txt").read_text(encoding="utf-8"))

    def test_ratios(self):
        r = dict(apc.ratios({"draws": 100, "vbChanges": 90, "texBindFast": 50, "texBindFastRedundant": 10, "runs": 40,
                             "mergeableDraws": 60}))
        self.assertAlmostEqual(r["vertex buffer changes per draw"], 0.9)
        self.assertAlmostEqual(r["redundant share of bindFast"], 0.2)
        self.assertAlmostEqual(r["mean mergeable run length (draws per run)"], 2.5)


MC1_SL = HERE.parent / "scenarios" / "mc1_sl.json"
SETTINGS_XML = HERE.parents[2] / "indra" / "newview" / "app_settings" / "settings.xml"


class FakeCapture:
    """A stand-in for the tracy-capture process: it exits (code 0) after `run_s` seconds and has written its file by then."""
    instances = []

    def __init__(self, cmd, stdin=None, stdout=None, stderr=None, run_s=0.4, never_exits=False):
        self.cmd = cmd
        self.t0 = time.monotonic()
        self.run_s = run_s
        self.never_exits = never_exits
        self.terminated = False
        self.code = None
        FakeCapture.instances.append(self)

    def poll(self):
        if self.code is None and not self.never_exits and time.monotonic() - self.t0 >= self.run_s:
            pathlib.Path(self.cmd[self.cmd.index("-o") + 1]).write_bytes(b"x" * 10)
            self.code = 0
        return self.code

    def terminate(self):
        self.terminated = True
        self.code = 1

    def wait(self, timeout=None):
        return self.code

    def kill(self):
        self.code = 1


class SeriesCapture(FakeCapture):
    """The teleport series capture outlasts the series (as it must); the window captures are short."""
    def __init__(self, cmd, **kw):
        super().__init__(cmd, run_s=14.0 if "MC1-teleports" in " ".join(cmd) else 0.4, **kw)


def mc1_main(fv, tmp, extra=(), popen=SeriesCapture, log_text="Entering region [Start]\n", hand=True, fps_from_log=False, **over):
    """perf_check.execute on a viewer without GLSubmitStats, the way the MC1 launcher runs it: --no-glstats, a viewer log
    holding the region line, the Tracy capture faked, the frame rate read from the log (faked)."""
    sc = json.loads(MC1_SL.read_text(encoding="utf-8"))
    sc["settle"] = {"post_login_s": 0.0, "after_camera_s": 0.0, "after_setting_s": 0.0, "after_teleport_s": 0.0}
    sc["owner_teleport"].update({"poll_s": 0.2, "timeout_s": 15})
    sc["window"] = {"seconds": 1}
    sc["fps_probe"]["seconds"] = 1
    sc["teleports"].update({"pause_s": 0, "settle_s": 0.3, "capture_s": 3, "timeout_s": 10})
    sc.update(over)
    scp = pathlib.Path(tmp) / "mc1.json"
    scp.write_text(json.dumps(sc), encoding="utf-8")
    vlog = pathlib.Path(tmp) / "viewer.log"
    vlog.write_text(log_text, encoding="utf-8")
    out = pathlib.Path(tmp) / "run"
    argv = ["--scenario", str(scp), "--out", str(out), "--no-glstats", "--viewer-log", str(vlog),
            "--tracy-capture", "C:/tracy/tracy-capture.exe"] + list(extra)
    args = pc.parse_args(argv)
    args.fps_from_log = fps_from_log
    args.popen = popen
    FakeCapture.instances.clear()
    if hand:
        def teleport_by_hand():
            fv.agent_region, fv.agent_pos = "Busy", [100.0, 100.0, 30.0]
            try:
                with open(vlog, "a", encoding="utf-8") as f:
                    f.write("Entering region [Busy]\n")
            except OSError:
                pass        # the test is over
        threading.Timer(1.0, teleport_by_hand).start()
    with mock.patch.object(lc, "viewer_window_state", return_value=None), \
            mock.patch.object(pc, "fps_from_log", return_value=77.0):
        code = pc.execute(session_for(fv), args, pc.load_scenario(scp), out)
    return code, out


class MC1Test(unittest.TestCase):
    def test_the_scenario_loads_and_every_setting_it_names_exists(self):
        sc = pc.load_scenario(MC1_SL)
        self.assertEqual([p["id"] for p in sc["presets"]], ["MC1-still", "MC1-moving", "MC1-moving-2"])
        self.assertEqual(sc["settle"]["post_login_s"], 20.0)
        self.assertEqual(sc["window"]["seconds"], 60)
        self.assertEqual((sc["teleports"]["within"], sc["teleports"]["second"]), (5, 5))
        self.assertEqual(sc["fps_probe"]["seconds"], 60)
        xml = SETTINGS_XML.read_text(encoding="utf-8")
        for key in list(sc["settings"]) + pc.CAP_SETTINGS:
            self.assertIn("<key>%s</key>" % key, xml)

    def test_the_teleport_scenarios_load_and_an_empty_one_is_refused(self):
        for name in ("mc1_sl_teleports", "mc1_sl_teleports_manual"):
            sc = pc.load_scenario(HERE.parent / "scenarios" / (name + ".json"))
            self.assertTrue(sc["presets"] or sc["teleports"])
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "s.json"
            p.write_text(json.dumps({"presets": [], "teleports": None}), encoding="utf-8")
            with self.assertRaises(ValueError):
                pc.load_scenario(p)

    def test_a_motion_preset_needs_a_camera(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "s.json"
            p.write_text(json.dumps({"presets": [{"id": "a", "camera": None, "motion": {"orbit_deg_per_s": 5}}]}), encoding="utf-8")
            with self.assertRaises(ValueError):
                pc.load_scenario(p)

    def test_fps_from_log_and_region_from_log(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = pathlib.Path(tmp) / "v.log"
            f.write_text("junk\n", encoding="utf-8")
            off = f.stat().st_size
            with open(f, "a", encoding="utf-8") as fh:
                for n in (10, 100, 120, 140):
                    fh.write("INFO: FrameCounters: frames=%d skin_palette_calls=1\n" % n)
                fh.write("Entering region []\nEntering region [Ahern]\nEntering region [Bar Foo]\nEntering region []\n")
            self.assertEqual(pc.fps_from_log(f, off), 120.0)        # the first, partial second is dropped
            self.assertIsNone(pc.fps_from_log(f, f.stat().st_size))
            self.assertEqual(pc.region_from_log(f), "Bar Foo")
            self.assertEqual(pc.region_from_log(f, first=True), "Ahern")
            self.assertIsNone(pc.region_from_log(f, f.stat().st_size))

    def test_a_full_run_captures_each_window_moves_the_camera_and_times_the_teleports(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            code, run = mc1_main(fv, tmp)
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            # one capture per window, then the teleport series, one at a time, in this order
            self.assertEqual([c["id"] for c in index["captures"]], ["MC1-still", "MC1-moving", "MC1-moving-2", "MC1-teleports"])
            for c in index["captures"]:
                self.assertEqual((c["exit_code"], c["size_bytes"]), (0, 10))
                self.assertTrue(c["utc_start"].endswith("+00:00"))
            self.assertEqual(FakeCapture.instances[0].cmd,
                             ["C:/tracy/tracy-capture.exe", "-a", "127.0.0.1", "-o", str(run / "MC1-still.tracy"), "-s", "1", "-f"])
            self.assertEqual(FakeCapture.instances[3].cmd[6], "3")      # the series: capture_s
            for info in index["presets"].values():
                self.assertEqual(info["fps_log"], 77.0)
                self.assertTrue(info["tracy_connected"])
            self.assertEqual(index["fps_without_profiler"], {"fps": 77.0, "seconds": 1.0, "tracy_connected": False})
            # the camera turned during the moving windows (about 10 Hz)
            foci = {tuple(round(x, 1) for x in c["focus_pos"]) for c in fv.cameras}
            self.assertGreater(len(foci), 8)
            # the teleports: 5 within the region (+ the return), 5 to the second region (+ the return), all timed
            res = index["teleports"]["results"]
            self.assertEqual([r["kind"] for r in res], ["within"] * 5 + ["return"] + ["second_region"] * 5 + ["return"])
            # the owner's flow: the login region is recorded, a pop-up asks for the teleport, the teleports start in the busy region
            self.assertEqual(index["login"]["region"], "Start")
            self.assertEqual(index["busy"]["region"], "Busy")
            self.assertEqual(index["teleports"]["start_region"], "Busy")
            self.assertEqual(fv.notifications, [("SystemMessage", "Ready for the busy region: teleport there now."),
                                                ("SystemMessage", "Session done: you can quit.")])
            self.assertEqual([d["regionname"] for d in fv.tp_requests][4:6], ["Busy", "Busy"])
            self.assertEqual([d["regionname"] for d in fv.tp_requests][-2:], ["Start", "Busy"])
            self.assertEqual(fv.tp_requests[-2]["x"], 128.0)         # the login position
            for r in res:
                self.assertGreaterEqual(r["arrive_s"], 0.3)
                self.assertLess(r["arrive_s"], 3.0)
                self.assertGreaterEqual(r["last_move_s"], r["arrive_s"])
            self.assertEqual((fv.agent_region, fv.agent_pos), ("Busy", [100.0, 100.0, 30.0]))
            self.assertIsNone(fv.follow_cam)
            self.assertEqual(fv.confirm_dialogs, [])        # LLAgent.requestTeleport would open a dialog nobody clicks
            self.assertEqual(fv.settings["AFKTimeout"][1], 300)     # restored
            self.assertNotIn("teleport_failures", index)

    def test_neighbour_region_lines_do_not_name_the_regions(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            vlog = pathlib.Path(tmp) / "viewer.log"

            def arrive_with_neighbours():
                fv.agent_region, fv.agent_pos = "Busy", [100.0, 100.0, 30.0]
                with open(vlog, "a", encoding="utf-8") as f:
                    f.write("Entering region [Busy]\nEntering region [Neighbour]\n")
            threading.Timer(1.0, arrive_with_neighbours).start()
            code, run = mc1_main(fv, tmp, hand=False, log_text="Entering region [Start]\nEntering region [StartNeighbour]\n",
                                 owner_teleport={"poll_s": 0.2, "timeout_s": 15})
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual((index["login"]["region"], index["busy"]["region"]), ("Start", "Busy"))

    def test_a_failed_teleport_is_recorded_the_series_goes_on_and_the_owner_is_told(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            real = fv.api_LLTeleportHandler
            fv.api_LLTeleportHandler = lambda op, d: None if d["x"] == 128.0 and d["regionname"] == "Start" else real(op, d)
            code, run = mc1_main(fv, tmp, teleports={"within": 1, "second": 1, "pause_s": 0, "settle_s": 0.2, "capture_s": 14, "timeout_s": 1.0})
            self.assertEqual(code, 4)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            kinds = [(r["kind"], bool(r.get("failed"))) for r in index["teleports"]["results"]]
            self.assertIn(("second_region", True), kinds)
            self.assertIn(("return", False), kinds)
            self.assertEqual(fv.notifications[-1][1], "Session ended with problems: see perf.json (you can quit).")

    def test_auto_handoff_teleports_to_the_busy_pos_without_a_pop_up_asking(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            code, run = mc1_main(fv, tmp, hand=False, extra=["--auto-handoff", "--busy-region", "Busy", "--busy-pos", "100,100,30"])
            self.assertEqual(code, 0)
            self.assertEqual((fv.tp_requests[0]["regionname"], fv.tp_requests[0]["x"]), ("Busy", 100.0))
            self.assertNotIn("Ready for the busy region: teleport there now.", [n[1] for n in fv.notifications])
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual((index["login"]["region"], index["busy"]["region"]), ("Start", "Busy"))

    def test_auto_handoff_already_in_the_busy_region_ignores_case_and_does_not_teleport(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            code, run = mc1_main(fv, tmp, hand=False, extra=["--auto-handoff", "--busy-region", "sTART", "--start-region", "Start"],
                                 teleports=None, fps_probe=None, presets=[{"id": "MC1-still", "camera": None}])
            self.assertEqual(code, 0)
            self.assertEqual(fv.tp_requests, [])
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(index["busy"]["region"], "sTART")

    def test_auto_handoff_without_busy_pos_fails_cleanly_when_not_in_the_busy_region(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            code, _ = mc1_main(fv, tmp, hand=False, extra=["--auto-handoff", "--busy-region", "Busy"])
            self.assertEqual(code, 1)
            self.assertEqual(fv.tp_requests, [])
            self.assertEqual(fv.notifications[-1][1], "Session ended with problems: see perf.json (you can quit).")

    def test_auto_handoff_retries_once_then_ends_the_run_with_the_problem_pop_up(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            fv.api_LLTeleportHandler = lambda op, d: fv.tp_requests.append(d)      # refused: the agent never moves
            code, _ = mc1_main(fv, tmp, hand=False, extra=["--auto-handoff", "--busy-region", "Busy", "--busy-pos", "100,100,30"],
                               teleports={"within": 0, "second": 0, "timeout_s": 1.0, "settle_s": 0.2})
            self.assertEqual(code, 1)
            self.assertEqual(len(fv.tp_requests), 2)
            self.assertEqual(fv.notifications[-1][1], "Session ended with problems: see perf.json (you can quit).")

    def test_auto_handoff_accepts_a_late_first_arrival_on_the_retry_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())

            def late(run, kind, region, xyz, cfg):
                fv.agent_region, fv.agent_pos = region, list(xyz)       # it arrives, but after the wait gave up
                raise pc.LeapError("did not arrive")
            with mock.patch.object(pc.Run, "teleport_once", late):
                code, run = mc1_main(fv, tmp, hand=False, extra=["--auto-handoff", "--busy-region", "Busy", "--busy-pos", "100,100,30"],
                                     teleports=None, fps_probe=None, presets=[{"id": "MC1-still", "camera": None}])
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(index["busy"]["region"], "Busy")

    def test_auto_handoff_ends_the_run_when_the_landing_misses_by_more_than_a_few_metres(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            real = fv.api_LLTeleportHandler
            fv.api_LLTeleportHandler = lambda op, d: real(op, dict(d, x=d["x"] + 40.0))
            code, _ = mc1_main(fv, tmp, hand=False, extra=["--auto-handoff", "--busy-region", "Busy", "--busy-pos", "100,100,30"])
            self.assertEqual(code, 1)

    def test_auto_handoff_login_timeout_ends_cleanly(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            with mock.patch.object(pc.Viewer, "wait_for_login", side_effect=pc.LeapTimeout("login did not finish in 300 s")):
                code, _ = mc1_main(fv, tmp, hand=False, extra=["--auto-handoff", "--busy-region", "Busy", "--busy-pos", "1,1,1"])
            self.assertEqual(code, 1)
            self.assertEqual(fv.tp_requests, [])
            self.assertEqual(fv.notifications[-1][1], "Session ended with problems: see perf.json (you can quit).")

    def test_auto_handoff_is_inert_when_the_scenario_has_no_owner_teleport(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            code, _ = mc1_main(fv, tmp, hand=False, extra=["--auto-handoff", "--busy-region", "Busy"], owner_teleport=None,
                               teleports=None, fps_probe=None, presets=[{"id": "MC1-still", "camera": None}])
            self.assertEqual(code, 0)
            self.assertEqual(fv.tp_requests, [])

    def test_a_failed_run_tells_the_owner(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            code, run = mc1_main(fv, tmp, hand=False, owner_teleport={"poll_s": 0.2, "timeout_s": 1.0})
            self.assertEqual(code, 1)
            self.assertEqual(fv.notifications[-1][1], "Session ended with problems: see perf.json (you can quit).")

    def test_tp_region_overrides_the_login_region(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            code, run = mc1_main(fv, tmp, extra=["--tp-region", "Other", "--tp-pos", "10,20,30"],
                                 presets=[{"id": "MC1-still", "camera": None}], fps_probe=None)
            self.assertEqual(code, 0)
            second = [d for d in fv.tp_requests if d["regionname"] == "Other"]
            self.assertEqual((len(second), second[0]["x"], second[0]["y"]), (3, 10.0, 20.0))

    def test_no_arrival_within_the_timeout_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            code, run = mc1_main(fv, tmp, hand=False, owner_teleport={"poll_s": 0.2, "timeout_s": 1.0})
            self.assertEqual(code, 1)
            self.assertEqual(fv.tp_requests, [])
            self.assertNotIn("Session done: you can quit.", [n[1] for n in fv.notifications])      # the request, then the problem pop-up

    def test_an_unknown_start_region_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            code, _ = mc1_main(fv, tmp, log_text="nothing\n")
            self.assertEqual(code, 1)
            self.assertEqual(fv.tp_requests, [])
            self.assertEqual([n[1] for n in fv.notifications], ["Session ended with problems: see perf.json (you can quit)."])

    def test_a_teleport_that_never_arrives_fails_the_run_and_stops_the_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            fv.tp_delay = 60.0
            code, run = mc1_main(fv, tmp, popen=lambda cmd, **kw: FakeCapture(cmd, never_exits=True, **kw),
                                 presets=[{"id": "MC1-still", "camera": None}], fps_probe=None,
                                 teleports={"within": 1, "second": 0, "pause_s": 0, "settle_s": 0.2, "capture_s": 3, "timeout_s": 1.0})
            self.assertEqual(code, 4)       # recorded as a failed teleport; the run goes on to the end
            self.assertTrue(FakeCapture.instances[-1].terminated)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertTrue(index["teleports"]["results"][0]["failed"])

    def test_a_stop_during_a_capture_stops_the_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            (pathlib.Path(tmp) / "run").mkdir()
            threading.Timer(2.5, lambda: (pathlib.Path(tmp) / "run" / "STOP").write_text("x")).start()
            code, run = mc1_main(fv, tmp, popen=lambda cmd, **kw: FakeCapture(cmd, never_exits=True, **kw))
            self.assertEqual(code, 2)
            self.assertTrue(FakeCapture.instances[0].terminated)
            self.assertIsNone(fv.follow_cam)

    def test_a_capture_that_exits_with_an_error_fails_the_run(self):
        class Failing(FakeCapture):
            def poll(self):
                if self.code is None and time.monotonic() - self.t0 >= self.run_s:
                    self.code = 1
                return self.code
        with tempfile.TemporaryDirectory() as tmp:
            code, run = mc1_main(FakeViewer(), tmp, popen=Failing, teleports=None, fps_probe=None)
            self.assertEqual(code, 4)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertIn("MC1-still", index["capture_failures"])

    def test_one_capture_at_a_time(self):
        with tempfile.TemporaryDirectory() as tmp:
            cap = pc.TracyCapture("tc.exe", tmp, popen=FakeCapture)
            cap.start("a", 5)
            with self.assertRaises(LeapError):
                cap.start("b", 5)
            cap.stop()

    def test_dry_run_without_the_stats_api_checks_the_settings_and_the_capture_exe(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            code, run = mc1_main(fv, tmp, extra=["--dry-run", "--no-login-wait"])
            self.assertEqual(code, 1)       # C:/tracy/tracy-capture.exe does not exist here
            rows = json.loads((run / "dry_run.json").read_text(encoding="utf-8"))
            by = {r["check"]: r["status"] for r in rows}
            self.assertEqual(by["api"], "PASS")
            self.assertEqual(by["setting RenderVSyncEnable"], "PASS")
            self.assertEqual(by["tracy-capture"], "FAIL")
            self.assertEqual(by["viewer log"], "PASS")
            for api in ("LLAgent", "LLNotifications", "LLTeleportHandler"):
                self.assertEqual(by["api " + api], "PASS")
            self.assertEqual(fv.tp_requests, [])


class MovingVariantsTest(unittest.TestCase):
    """The camera turns for every variant of a moving preset, each variant waits for the frame rate to settle, and the
    projector stats are recorded (or said to be missing)."""

    VARIANTS = [{"id": "none", "settings": {"AFKTimeout": 1}}, {"id": "sun", "settings": {"AFKTimeout": 2}},
                {"id": "all", "settings": {"AFKTimeout": 3}}]

    def _run(self, tmp, fv):
        presets = [{"id": "RT-moving", "camera": {"az_deg": 0.0, "pitch_deg": 0.0}, "motion": {"orbit_deg_per_s": 10}}]
        settle = {"post_login_s": 0.0, "after_camera_s": 0.0, "after_setting_s": 0.0, "after_teleport_s": 0.0,
                  "warmup_min_s": 0.05, "warmup_max_s": 0.5, "warmup_poll_s": 0.0}
        calls = []

        def lines(path, pos):
            calls.append(1)
            return [100] * 6, pos
        with mock.patch.object(pc, "read_new_frames", side_effect=lines):
            return mc1_main(fv, tmp, presets=presets, variants=self.VARIANTS, teleports=None, fps_probe=None, settle=settle,
                            window={"seconds": 2.5}, overhead={"probe_s": 0.3, "probe_repeats": 1, "bench_iterations": 0}), calls

    def test_every_variant_orbits_for_its_whole_capture_and_logs_the_heading(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            (code, run), calls = self._run(tmp, fv)
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(sorted(index["presets"]), ["RT-moving__all", "RT-moving__none", "RT-moving__sun"])
            for key, info in index["presets"].items():
                log_ = info["camera_log"]
                self.assertGreaterEqual(len(log_), 2, key)
                azs = [e["az_deg"] for e in log_]
                self.assertGreater(len(set(azs)), 1, "the view was frozen in " + key)
                self.assertTrue(all(b != a for a, b in zip(azs, azs[1:])), key)
                self.assertEqual(set(log_[0]), {"t_s", "az_deg", "camera_pos", "focus_pos"})
                self.assertTrue(info["moving"])
                self.assertTrue(info["warmup"]["settled"])
            self.assertEqual(len(index["warmups"]), 3)      # the measurement of each variant: the log frame rate runs no probe

    def test_fps_log_mode_runs_no_probe_variants(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            with mock.patch.object(pc.Run, "probe_variants", side_effect=AssertionError("probe")) as pv:
                presets = [{"id": "RT-moving", "camera": {"az_deg": 0.0, "pitch_deg": 0.0}, "motion": {"orbit_deg_per_s": 10}}]
                settle = {"post_login_s": 0.0, "after_camera_s": 0.0, "after_setting_s": 0.0, "after_teleport_s": 0.0,
                          "warmup_min_s": 0.05, "warmup_max_s": 0.2, "warmup_poll_s": 0.0}
                with mock.patch.object(pc, "read_new_frames", side_effect=lambda p, pos: ([100] * 6, pos)):
                    code, run = mc1_main(fv, tmp, presets=presets, variants=self.VARIANTS, teleports=None, fps_probe=None,
                                         settle=settle, window={"seconds": 1.0}, fps_from_log=True,
                                         overhead={"probe_s": 0.3, "probe_repeats": 1, "bench_iterations": 0})
            pv.assert_not_called()
            self.assertEqual(code, 0)

    def test_a_preset_without_variants_stops_turning_after_its_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            presets = [{"id": "RT-moving", "camera": {"az_deg": 0.0, "pitch_deg": 0.0}, "motion": {"orbit_deg_per_s": 10}}]
            code, run = mc1_main(fv, tmp, presets=presets, variants=[], teleports=None, fps_probe=None, window={"seconds": 2.5},
                                 overhead={"probe_s": 0.3, "probe_repeats": 1, "bench_iterations": 0})
            self.assertEqual(code, 0)
            n = len(fv.cameras)
            time.sleep(0.5)
            self.assertEqual(len(fv.cameras), n)

    def test_projector_stats_are_recorded_with_the_counter_deltas_but_not_the_tiles(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            (code, run), _ = self._run(tmp, fv)
            info = json.loads((run / "perf.json").read_text(encoding="utf-8"))["presets"]["RT-moving__none"]
            self.assertIn("rerender_causes", info["projector_stats"]["pool"])
            self.assertNotIn("tiles", info["projector_stats"]["pool"])
            self.assertIn("projector_stats_end", info)
            self.assertIn("static_renders", info["projector_delta"])
            self.assertIn("new", info["projector_delta"]["rerender_causes"])

    def test_a_viewer_without_the_api_is_said_to_lack_it_once_and_is_not_waited_for(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer()
            t0 = time.monotonic()
            (code, run), _ = self._run(tmp, fv)
            self.assertEqual(code, 0)
            self.assertLess(time.monotonic() - t0, 25.0)      # no 30 s LEAP timeout per preset
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            for info in index["presets"].values():
                self.assertFalse(info["projector_stats"]["available"])
                self.assertIn("VKS_GL_STATS", info["projector_stats"]["reason"])

    def test_gpu_window_keeps_the_log_flow_and_adds_a_gpu_timed_window_after_each_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, projector=PoolModel())
            presets = [{"id": "RT-moving", "camera": {"az_deg": 0.0, "pitch_deg": 0.0}, "motion": {"orbit_deg_per_s": 10}}]
            settle = {"post_login_s": 0.0, "after_camera_s": 0.0, "after_setting_s": 0.0, "after_teleport_s": 0.0,
                      "warmup_min_s": 0.05, "warmup_max_s": 0.2, "warmup_poll_s": 0.0}
            order = []
            begin, finish = pc.Run.gpu_window, pc.Run.finish_capture

            def window(run_, key, info):
                order.append(("window", key, run_.tracy.running()))
                return begin(run_, key, info)

            def finished(run_, started):
                finish(run_, started)
                order.append(("capture", None, False))
            with mock.patch.object(pc.Run, "probe_variants", side_effect=AssertionError("probe")), \
                    mock.patch.object(pc.Run, "gpu_window", window), mock.patch.object(pc.Run, "finish_capture", finished), \
                    mock.patch.object(pc, "read_new_frames", side_effect=lambda p, pos: ([100] * 6, pos)):
                code, run = mc1_main(fv, tmp, extra=["--gpu-window", "--frames", "7"], presets=presets, variants=self.VARIANTS,
                                     teleports=None, fps_probe=None, settle=settle, window={"seconds": 1.0},
                                     overhead={"probe_s": 0.3, "probe_repeats": 1, "bench_iterations": 0})
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(len(index["captures"]), 3)
            self.assertEqual(len(index["warmups"]), 3)          # the log flow: a warm-up per variant, no probes
            self.assertEqual([kind for kind, _, _ in order], ["capture", "window"] * 3)     # each window after its capture
            self.assertFalse(any(running for kind, _, running in order if kind == "window"))
            for key, info in index["presets"].items():
                self.assertEqual(info["fps_log"], 77.0)
                self.assertTrue(info["tracy_connected"])
                self.assertEqual((info["frames"], info["gpu_frames"]), (7, 7))
                self.assertEqual(info["summary"]["totals"]["gpu_ms"], 7.25)
                self.assertIn("projector_delta", info)
                doc = json.loads((run / info["window_file"]).read_text(encoding="utf-8"))
                self.assertEqual(doc["gpu_frames"], 7)
            self.assertEqual(index["window_failures"], [])

    def test_a_gpu_window_that_stops_short_fails_the_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True, gl_short_window=True)
            with mock.patch.object(pc, "read_new_frames", side_effect=lambda p, pos: ([100] * 6, pos)):
                code, run = mc1_main(fv, tmp, extra=["--gpu-window", "--frames", "7"], presets=[{"id": "RT-still", "camera": None}],
                                     variants=[], teleports=None, fps_probe=None, window={"seconds": 1.0},
                                     settle={"post_login_s": 0.0, "after_camera_s": 0.0, "after_setting_s": 0.0,
                                             "after_teleport_s": 0.0, "window_timeout_s": 1.0})
            self.assertEqual(code, 4)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            self.assertEqual(index["window_failures"], [{"preset": "RT-still", "collected": 3, "target": 7}])

    def test_gpu_window_on_a_viewer_without_the_api_runs_the_plain_log_flow(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer()
            with mock.patch.object(pc.Run, "gpu_window", side_effect=AssertionError("window")):
                code, run = mc1_main(fv, tmp, extra=["--gpu-window"], presets=[{"id": "RT-still", "camera": None}], variants=[],
                                     teleports=None, fps_probe=None, window={"seconds": 1.0})
            self.assertEqual(code, 0)
            info = json.loads((run / "perf.json").read_text(encoding="utf-8"))["presets"]["RT-still"]
            self.assertNotIn("window_file", info)

    def test_dry_run_with_gpu_window_says_whether_the_viewer_can_collect_it(self):
        for gl_stats, want in ((True, "PASS"), (False, "FAIL")):
            with tempfile.TemporaryDirectory() as tmp:
                code, run = mc1_main(FakeViewer(gl_stats=gl_stats), tmp, extra=["--dry-run", "--no-login-wait", "--gpu-window"])
                by = {r["check"]: r for r in json.loads((run / "dry_run.json").read_text(encoding="utf-8"))}
                self.assertEqual(by["gpu window"]["status"], want)
                self.assertEqual("is there" in by["api"]["detail"], gl_stats)

    def test_the_stage_1b_scenario_loads_and_names_only_settings_and_variants_that_exist(self):
        sc = pc.load_scenario(HERE.parent / "scenarios" / "mc1b_rooftops.json")
        self.assertEqual([(p["id"], p["variants"]) for p in sc["presets"]],
                         [("RT-still", ["sun", "none", "all", "splits2"]), ("RT-moving", ["sun", "none", "all"]),
                          ("RT-fixed", ["sun", "night", "high"])])
        self.assertEqual(sc["settle"]["warmup_capture_s"], 10.0)
        by = {v["id"]: v for v in sc["variants"]}
        self.assertEqual(by["night"]["environment"], "midnight")
        self.assertEqual((sc["soft_settings"]["RenderShadowDistance"], by["high"]["soft_settings"]["RenderShadowDistance"]), (192.0, 128.0))
        xml = SETTINGS_XML.read_text(encoding="utf-8")
        keys = set(sc["settings"]) | set(sc["soft_settings"])
        for v in sc["variants"]:
            keys |= set(v["settings"]) | set(v.get("soft_settings", {}))
        for key in sorted(keys):
            self.assertIn("<key>%s</key>" % key, xml)

    def test_a_preset_naming_an_unknown_variant_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "s.json"
            p.write_text(json.dumps({"presets": [{"id": "a", "camera": None, "variants": ["nope"]}],
                                     "variants": [{"id": "sun", "settings": {}}]}), encoding="utf-8")
            with self.assertRaises(ValueError):
                pc.load_scenario(p)

    def test_presets_pick_their_variants_the_sky_is_set_and_put_back_and_tier_settings_are_soft(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(projector=PoolModel())
            presets = [{"id": "A", "camera": None, "variants": ["sun", "none"]},
                       {"id": "B", "camera": {"az_deg": 0.0, "pitch_deg": 0.0}, "variants": ["sun", "night", "high"]}]
            variants = [{"id": "sun", "settings": {"AFKTimeout": 1}}, {"id": "none", "settings": {"AFKTimeout": 2}},
                        {"id": "night", "settings": {"AFKTimeout": 1}, "environment": "midnight"},
                        {"id": "high", "settings": {"AFKTimeout": 1}, "soft_settings": {"FramePerSecondLimit": 7, "NoSuchTierSetting": 1}}]
            settle = {"post_login_s": 0.0, "after_camera_s": 0.0, "after_setting_s": 0.0, "after_teleport_s": 0.0,
                      "warmup_min_s": 0.05, "warmup_max_s": 0.2, "warmup_poll_s": 0.0, "warmup_capture_s": 0.3}
            with mock.patch.object(pc, "read_new_frames", side_effect=lambda p, pos: ([100] * 6, pos)):
                code, run = mc1_main(fv, tmp, presets=presets, variants=variants, teleports=None, fps_probe=None, settle=settle,
                                     window={"seconds": 1.0}, soft_settings={"AlsoMissing": 3},
                                     overhead={"probe_s": 0.3, "probe_repeats": 1, "bench_iterations": 0})
            self.assertEqual(code, 0)
            index = json.loads((run / "perf.json").read_text(encoding="utf-8"))
            # the second preset runs its variants in reverse (the alternation), each preset only its own
            self.assertEqual(list(index["presets"]), ["A__sun", "A__none", "B__high", "B__night", "B__sun"])
            self.assertEqual(fv.ui_calls, [("World.EnvSettings", "midnight"), ("World.EnvSettings", "region")])
            self.assertEqual([(e["where"], e["key"]) for e in index["soft_settings_not_taken"]],
                             [("scenario", "AlsoMissing"), ("variant high", "NoSuchTierSetting")])
            self.assertEqual(int(fv.settings["FramePerSecondLimit"][1]), 0)      # the tier value went back after the variant
            self.assertEqual(sorted(p.name for p in run.glob("*.tracy")),
                             ["A__none.tracy", "A__sun.tracy", "B__high.tracy", "B__night.tracy", "B__sun.tracy"])     # no warm-up file left
            for info in index["presets"].values():
                self.assertEqual(info["warmup_capture"], {"seconds": 1, "exit_code": 0})     # whole seconds for tracy-capture
                self.assertEqual(info["environment"], "night" in info["variant"] and "midnight" or "shared")
                self.assertIsNone(info["directional_shadows"])        # the fake has no DirectionalShadows API
            self.assertEqual(list(run.glob("*.warmup*")), [])         # neither the capture nor its log is left
            self.assertEqual(len(index["captures"]), 5)               # the throw-away captures are not listed

    QUICK = {"post_login_s": 0.0, "after_camera_s": 0.0, "after_setting_s": 0.0, "after_teleport_s": 0.0,
             "warmup_min_s": 0.05, "warmup_max_s": 0.2, "warmup_poll_s": 0.0}
    NO_PROBE = {"probe_s": 0.3, "probe_repeats": 1, "bench_iterations": 0}

    def _quick(self, tmp, fv, presets, variants, extra=(), popen=None, **over):
        kw = {"popen": popen} if popen else {}
        with mock.patch.object(pc, "read_new_frames", side_effect=lambda p, pos: ([100] * 6, pos)):
            code, run = mc1_main(fv, tmp, extra=list(extra), presets=presets, variants=variants, teleports=None, fps_probe=None,
                                 settle=dict(self.QUICK, **over.pop("settle", {})), window={"seconds": 1.0},
                                 overhead=self.NO_PROBE, **kw, **over)
        return code, run, json.loads((run / "perf.json").read_text(encoding="utf-8"))

    def test_a_view_that_starts_on_the_variant_the_last_one_ended_on_still_gets_its_settings_pass_and_warm_up(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer()
            presets = [{"id": "A", "camera": None}, {"id": "B", "camera": {"az_deg": 0.0, "pitch_deg": 0.0}}]
            variants = [{"id": "sun", "settings": {"AFKTimeout": 1}}, {"id": "none", "settings": {"AFKTimeout": 2}}]
            code, run, index = self._quick(tmp, fv, presets, variants)
            self.assertEqual(code, 0)
            # A runs sun, none; B (the alternation) none, sun: "none" is on both sides of the view change
            self.assertEqual(list(index["presets"]), ["A__sun", "A__none", "B__none", "B__sun"])
            self.assertEqual([w["variant"] for w in index["warmups"]], ["sun", "none", "none", "sun"])
            for info in index["presets"].values():
                self.assertIn("warmup", info)

    def test_the_sky_switch_is_verified_by_the_dark_gate_and_recorded_and_put_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer()
            fv.directional = {}
            fv.settings["FSRepeatedEnvTogglesShared"] = ("Boolean", True)
            seen = []
            fv.on_op = lambda op, d: seen.append(fv.settings["FSRepeatedEnvTogglesShared"][1]) if op == "call" else None
            variants = [{"id": "sun", "settings": {"AFKTimeout": 1}},
                        {"id": "night", "settings": {"AFKTimeout": 1}, "environment": "midnight"}]
            code, run, index = self._quick(tmp, fv, [{"id": "A", "camera": None}], variants)
            self.assertEqual(code, 0)
            self.assertEqual(fv.ui_calls, [("World.EnvSettings", "midnight"), ("World.EnvSettings", "region")])
            self.assertEqual(seen, [False, False])      # the toggle-back setting is off while the script switches
            self.assertTrue(fv.settings["FSRepeatedEnvTogglesShared"][1])       # and restored
            night, sun = index["presets"]["A__night"], index["presets"]["A__sun"]
            self.assertEqual((sun["environment"], night["environment"]), ("shared", "midnight"))
            self.assertEqual(night["environment_check"]["verified"], True)
            self.assertNotIn("environment_check", sun)
            self.assertNotIn("environment_failures", index)
            # the readout at both ends of a run, with the frames the cascades were rendered in between
            self.assertEqual(night["directional_shadows"]["decision_light"], 2.0e-5)
            self.assertGreater(night["directional_delta"]["frames_rendered"], 0)
            self.assertEqual(night["directional_delta"]["frames_skipped"], None)        # the fake reports none
            self.assertIsNone(json.loads((run / "restore.json").read_text(encoding="utf-8"))["environment"])

    def test_a_sky_that_did_not_change_fails_the_run_and_names_the_variant(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer()
            fv.directional = {}
            fv.sky_stuck = True
            variants = [{"id": "sun", "settings": {"AFKTimeout": 1}},
                        {"id": "night", "settings": {"AFKTimeout": 1}, "environment": "midnight"}]
            code, run, index = self._quick(tmp, fv, [{"id": "A", "camera": None}], variants)
            self.assertEqual(code, 4)
            self.assertEqual([(f["variant"], f["verified"]) for f in index["environment_failures"]], [("night", False)])
            self.assertEqual(index["presets"]["A__night"]["environment_check"]["verified"], False)
            self.assertEqual(len(index["captures"]), 2)       # both runs were still measured

    def test_the_dark_gate_api_is_asked_for_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer()
            variants = [{"id": "sun", "settings": {"AFKTimeout": 1}}, {"id": "none", "settings": {"AFKTimeout": 2}}]
            code, run, index = self._quick(tmp, fv, [{"id": "A", "camera": None}], variants)
            self.assertEqual(code, 0)
            self.assertEqual(sum(1 for pump, op in fv.log if pump == "DirectionalShadows"), 0)
            for info in index["presets"].values():
                self.assertIsNone(info["directional_shadows"])
                self.assertIsNone(info["directional_shadows_end"])

    def test_a_tier_value_the_viewer_clamps_is_recorded_and_still_put_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer()
            fv.clamps = {"FramePerSecondLimit": 5}
            variants = [{"id": "high", "settings": {"AFKTimeout": 1}, "soft_settings": {"FramePerSecondLimit": 7}},
                        {"id": "sun", "settings": {"AFKTimeout": 1}}]
            orig = pc.Run.apply_variant

            def unclamp_then_apply(run_, variant):
                if variant["id"] == "sun":
                    fv.clamps = {}          # the way back takes
                return orig(run_, variant)
            with mock.patch.object(pc.Run, "apply_variant", unclamp_then_apply):
                code, run, index = self._quick(tmp, fv, [{"id": "A", "camera": None}], variants)
            self.assertEqual(code, 0)
            self.assertEqual([(e["where"], e["key"]) for e in index["soft_settings_not_taken"]],
                             [("variant high", "FramePerSecondLimit")])
            # the viewer held 5 during "high"; the key was tracked although the 7 did not take, so "sun" put the 0 back
            sets = [d for d in fv.set_log if d[0] == "FramePerSecondLimit"]
            self.assertEqual([v for _, v in sets][:2], [7, 0])
            self.assertEqual(int(fv.settings["FramePerSecondLimit"][1]), 0)

    def test_a_failed_gpu_window_is_recorded_and_the_other_runs_go_on(self):
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer(gl_stats=True)
            variants = [{"id": "sun", "settings": {"AFKTimeout": 1}}, {"id": "none", "settings": {"AFKTimeout": 2}}]
            real = pc.Run.wait_window
            calls = []

            def flaky(run_, start):
                calls.append(1)
                if len(calls) == 1:
                    raise pc.LeapError("boom")
                return real(run_, start)
            with mock.patch.object(pc.Run, "wait_window", flaky):
                code, run, index = self._quick(tmp, fv, [{"id": "A", "camera": None}], variants, extra=["--gpu-window", "--frames", "7"])
            self.assertEqual(code, 4)
            self.assertEqual([(f["preset"], f.get("error")) for f in index["window_failures"]], [("A__sun", "boom")])
            self.assertNotIn("window_file", index["presets"]["A__sun"])
            self.assertEqual(index["presets"]["A__none"]["gpu_frames"], 7)
            self.assertEqual(len(index["captures"]), 2)

    def test_a_warm_up_capture_that_cannot_start_does_not_stop_the_run(self):
        class NoWarmup(FakeCapture):
            def __init__(self, cmd, **kw):
                if ".warmup" in " ".join(cmd):
                    raise OSError("cannot start")
                super().__init__(cmd, **kw)
        with tempfile.TemporaryDirectory() as tmp:
            fv = FakeViewer()
            code, run, index = self._quick(tmp, fv, [{"id": "A", "camera": None}], [{"id": "sun", "settings": {"AFKTimeout": 1}}],
                                           popen=NoWarmup, settle={"warmup_capture_s": 0.3})
            self.assertEqual(code, 0)
            info = index["presets"]["A__sun"]
            self.assertEqual(info["warmup_capture"]["error"], "cannot start")
            self.assertEqual(sorted(p.name for p in run.glob("A__sun*")), ["A__sun.capture.log", "A__sun.tracy"])
            self.assertTrue(info["tracy_connected"])

    def test_gpu_window_needs_the_no_glstats_flow(self):
        with self.assertRaises(SystemExit), mock.patch("sys.stderr"):
            pc.parse_args(["--gpu-window"])
        self.assertTrue(pc.parse_args(["--gpu-window", "--no-glstats"]).gpu_window)

    def test_an_empty_variant_list_and_an_unknown_environment_are_refused(self):
        bad = [{"presets": [{"id": "a", "camera": None, "variants": []}], "variants": [{"id": "sun", "settings": {}}]},
               {"presets": [{"id": "a", "camera": None}], "variants": [{"id": "n", "settings": {}, "environment": "region"}]},
               {"presets": [{"id": "a", "camera": None}], "variants": [{"id": "n", "settings": {}, "environment": "dusk"}]}]
        with tempfile.TemporaryDirectory() as tmp:
            for i, doc in enumerate(bad):
                p = pathlib.Path(tmp) / ("s%d.json" % i)
                p.write_text(json.dumps(doc), encoding="utf-8")
                with self.assertRaises(ValueError, msg=str(doc)):
                    pc.load_scenario(p)

    def test_the_rooftops_scenarios_load(self):
        sc = pc.load_scenario(HERE.parent / "scenarios" / "mc1_rooftops_moving.json")
        self.assertEqual([p["id"] for p in sc["presets"]], ["RT-moving"])
        self.assertEqual([v["id"] for v in sc["variants"]], ["none", "sun", "all"])
        self.assertEqual([v["settings"]["RenderShadowDetail"] for v in sc["variants"]], [0, 1, 2])
        full = pc.load_scenario(HERE.parent / "scenarios" / "mc1_rooftops.json")
        self.assertEqual([p["id"] for p in full["presets"]], ["RT-still", "RT-moving"])

    def test_settled_wants_the_last_windows_close_to_their_median(self):
        self.assertTrue(pc.settled([5, 100, 105, 98, 100, 102]))
        self.assertFalse(pc.settled([100, 100, 100, 100]))                  # fewer than 5 windows
        self.assertFalse(pc.settled([10, 100, 105, 60, 100, 100]))          # warming up: 60 is 40 % under the median
        self.assertFalse(pc.settled([0, 0, 0, 0, 0]))
        self.assertTrue(pc.settled([100, 86, 114, 100, 100]))               # a moving camera: within 15 % of the median
        self.assertFalse(pc.settled([100, 84, 100, 100, 100]))
        self.assertTrue(pc.settled([100, 91, 100], 3, 0.10))
        self.assertFalse(pc.settled([100, 89, 100], 3, 0.10))

    def test_read_new_frames_reads_only_new_whole_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = pathlib.Path(tmp) / "v.log"
            f.write_text("INFO: FrameCounters: frames=10\nINFO: FrameCounters: frames=20\nINFO: FrameCounters: fra", encoding="utf-8")
            vals, pos = pc.read_new_frames(f, 0)
            self.assertEqual(vals, [10, 20])
            self.assertEqual(f.read_bytes()[pos:], b"INFO: FrameCounters: fra")      # the unfinished line waits
            with open(f, "a", encoding="utf-8") as h:
                h.write("mes=30\n")
            vals, pos2 = pc.read_new_frames(f, pos)
            self.assertEqual(vals, [30])
            self.assertEqual(pc.read_new_frames(f, pos2), ([], pos2))
            self.assertEqual(pc.read_new_frames(None, 7), ([], 7))

    def test_warm_up_polls_the_log_incrementally_at_most_once_per_poll_period(self):
        run = object.__new__(pc.Run)
        run.sc = {"settle": {"warmup_min_s": 0.0, "warmup_max_s": 0.6, "warmup_poll_s": 0.25}}
        run.vlog = None
        run.index = {"warnings": []}
        calls = []

        def fake(path, pos):
            calls.append(pos)
            return [100] * 3, pos + 10
        run.tick = lambda _p: time.sleep(0.05)
        with mock.patch.object(pc, "read_new_frames", side_effect=fake):
            res = run.warm_up()
        self.assertLessEqual(len(calls), 4)             # not once per 0.05 s tick
        self.assertEqual(calls, sorted(calls))          # the offset only advances
        self.assertEqual(calls[1], 10)

    def test_warm_up_waits_for_the_minimum_then_until_settled_and_stops_at_the_maximum(self):
        with tempfile.TemporaryDirectory() as tmp:
            vlog = pathlib.Path(tmp) / "v.log"
            vlog.write_text("", encoding="utf-8")
            run = object.__new__(pc.Run)
            run.sc = {"settle": {"warmup_min_s": 0.2, "warmup_max_s": 1.0, "warmup_poll_s": 0.0}}
            run.vlog = vlog
            run.index = {"warnings": []}

            def make_tick(frames):
                it = iter(frames)

                def tick(_period):
                    time.sleep(0.05)
                    with open(vlog, "a", encoding="utf-8") as f:
                        f.write("INFO: FrameCounters: frames=%d\n" % next(it, frames[-1]))
                return tick
            run.tick = make_tick([5, 20, 60, 100, 101, 99, 100, 100])
            res = run.warm_up()
            self.assertTrue(res["settled"])
            self.assertGreaterEqual(res["waited_s"], 0.2)
            self.assertTrue(all(v >= 90 for v in res["last_fps"]))
            vlog.write_text("", encoding="utf-8")
            run.tick = make_tick([int(10 * 1.5 ** i) for i in range(40)])
            res = run.warm_up()
            self.assertFalse(res["settled"])
            self.assertGreaterEqual(res["waited_s"], 1.0)
            self.assertEqual(len(run.index["warnings"]), 1)


if __name__ == "__main__":
    unittest.main()
