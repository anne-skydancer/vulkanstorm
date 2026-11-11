# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline tests of perf_check.py and analyze_perf_check.py against a fake viewer with the GLSubmitStats API.

  python -m unittest discover -s scripts/inworld/tests -v

Not covered (needs the live development viewer): the real replies, the real GPU timers, the check on real targets.
"""
import csv
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))

import analyze_perf_check as apc  # noqa: E402
import light_check as lc  # noqa: E402
import perf_check as pc  # noqa: E402
from fake_viewer import FakeViewer  # noqa: E402
from leap_client import LeapSession  # noqa: E402

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


if __name__ == "__main__":
    unittest.main()
