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


def session_for(fake):
    return LeapSession(fake.plugin_in, fake.plugin_out, default_timeout=10).start()


def scenario_file(tmp, **over):
    sc = json.loads(SCENARIO.read_text(encoding="utf-8"))
    sc["settle"] = {"post_login_s": 0.0, "after_camera_s": 0.0, "after_teleport_s": 0.0, "login_timeout_s": 20,
                    "window_timeout_s": 10.0, "compare_wait_s": 2.0}
    sc["window"] = {"frames": 5, "gpu": True}
    sc["overhead"] = {"probe_s": 0.2, "bench_iterations": 1000}
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
