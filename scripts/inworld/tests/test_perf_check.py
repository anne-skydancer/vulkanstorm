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
