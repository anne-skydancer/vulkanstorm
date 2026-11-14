# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline tests of the pure parts of projector_orbit_check.py (the live gates need the development viewer)."""
import json
import math
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))

import projector_orbit_check as poc  # noqa: E402

SCENARIO = HERE.parent / "scenarios" / "projector_orbit.json"


class OrbitTest(unittest.TestCase):
    def test_the_shipped_scenario_loads_and_has_the_o1_steps(self):
        sc = json.loads(SCENARIO.read_text(encoding="utf-8"))
        steps = poc.o1_steps(sc["o1"])
        self.assertEqual(steps[0], (0.0, 0.0))
        self.assertEqual(len([s for s in steps if s[1] == 0.0]), 24)
        self.assertEqual(len(steps), 24 + 8)

    def test_pool_state_reads_the_reply(self):
        st = {"pool": {"static_renders": 7, "lights_without_tile": 1, "tiles": [{"key": 3, "crc32": 9}]}}
        self.assertEqual(poc.pool_state(st), (7, 1, {"3": 9}))
        self.assertEqual(poc.pool_state({}), (0, 0, {}))

    def test_o1_passes_when_nothing_changes(self):
        base = (5, 0, {"1": 11, "2": 22})
        self.assertEqual(poc.judge_o1(base, [base, base]), [])

    def test_o1_names_a_render_a_missing_tile_and_a_changed_crc(self):
        base = (5, 0, {"1": 11, "2": 22})
        fails = poc.judge_o1(base, [(6, 0, {"1": 11, "2": 22}), (5, 2, {"1": 11, "2": 22}), (5, 0, {"1": 11, "2": 99})])
        self.assertEqual(len(fails), 3)
        self.assertIn("static renders 5 -> 6", fails[0])
        self.assertIn("2 lights without a tile", fails[1])
        self.assertIn("tile CRC changed for 2", fails[2])

    def test_o1_fails_when_the_baseline_has_a_light_without_a_tile(self):
        base = (5, 1, {"1": 11})
        fails = poc.judge_o1(base, [(5, 0, {"1": 11})])
        self.assertEqual(len(fails), 1)
        self.assertIn("step 0: 1 lights without a tile", fails[0])

    def test_dry_run_rows_check_slots_and_lights_like_the_pool(self):
        full = {"pool": {k: 0 for k in poc.POOL_KEYS}, "slots": {k: 0 for k in poc.SLOTS_KEYS},
                "lights": {k: 0 for k in poc.LIGHTS_KEYS}}
        self.assertEqual([r[1] for r in poc.dry_run_rows(full)], ["PASS", "PASS", "PASS"])
        rows = poc.dry_run_rows({"pool": full["pool"]})
        self.assertEqual([r[:2] for r in rows], [("api", "PASS"), ("slots", "FAIL"), ("lights", "FAIL")])
        self.assertIn("occupied", rows[1][2])
        partial = dict(full, lights={"nearby": 1})
        self.assertIn("eligible", poc.dry_run_rows(partial)[2][2])

    def test_o1_is_not_run_when_the_baseline_has_no_tile(self):
        check = object.__new__(poc.Check)
        check.sc = {"camera": {"height_m": 1.5, "look_distance_m": 10}, "settle": {"step_s": 0.0, "after_camera_s": 0.0},
                    "o1": {"yaw_step_deg": 90, "pitch_deg": []}}
        empty = {"pool": {"pool_present": True, "static_renders": 0, "lights_without_tile": 0, "tiles": []}}
        no_crc = {"pool": {"pool_present": True, "static_renders": 2, "lights_without_tile": 0, "tiles": [{"key": 1}]}}

        class V:
            def agent_pos(self):
                return [0.0, 0.0, 0.0]

            def check_abort(self):
                pass

            def sleep(self, _s):
                pass

        check.v = V()
        check.stats = lambda: empty
        check.look = lambda *_a: None
        check.warm_up = lambda: None
        g = check.gate_o1()
        self.assertEqual(g["status"], "NOT RUN")
        self.assertIn("no projector tile", g["why"])
        check.stats = lambda: no_crc
        g = check.gate_o1()
        self.assertEqual(g["status"], "NOT RUN")
        self.assertIn("no tile CRC", g["why"])


def sample(lap, tag, renders=5, without=0, crcs=None, causes=None):
    return {"lap": lap, "tag": tag, "renders": renders, "without": without, "crcs": crcs if crcs is not None else {"1": 11, "2": 22},
            "causes": causes if causes is not None else {"new": 5}}


class OrbitJudgeTest(unittest.TestCase):
    def test_orbit_points_are_a_circle_around_the_centre_looking_at_it(self):
        pts = poc.orbit_points([100.0, 50.0, 20.0], 3.0, 4, 1.6, 0.5)
        self.assertEqual(len(pts), 4)
        cam0, focus0 = pts[0]
        self.assertAlmostEqual(cam0[0], 103.0)
        self.assertAlmostEqual(cam0[1], 50.0)
        self.assertAlmostEqual(cam0[2], 21.6)
        self.assertEqual(focus0, [100.0, 50.0, 20.5])
        for cam, focus in pts:
            self.assertAlmostEqual(math.hypot(cam[0] - 100.0, cam[1] - 50.0), 3.0)
            self.assertEqual(focus, [100.0, 50.0, 20.5])

    def test_cause_delta_names_only_what_changed(self):
        self.assertEqual(poc.cause_delta({"a": 1, "b": 2}, {"a": 1, "b": 5, "c": 1}), {"b": 3, "c": 1})
        self.assertEqual(poc.cause_delta({"a": 1}, {"a": 1}), {})

    def test_o2_passes_when_only_the_lod_pin_draws_in_the_first_lap(self):
        base = sample(0, "baseline")
        first = [sample(1, "s0", 6, causes={"new": 5, "lod_up": 1}), sample(1, "s1", 6, causes={"new": 5, "lod_up": 1})]
        again = [sample(2, "s0", 6, causes={"new": 5, "lod_up": 1}), sample(2, "s1", 6, causes={"new": 5, "lod_up": 1})]
        for s in first + again:
            s["crcs"] = {"1": 11, "2": 99}
        self.assertEqual(poc.judge_o2(base, first + again), [])

    def test_o2_fails_on_a_first_lap_cause_the_pin_does_not_explain(self):
        base = sample(0, "baseline")
        s = sample(1, "s0", 6, causes={"new": 5, "edit": 1})
        fails = poc.judge_o2(base, [s])
        self.assertEqual(len(fails), 1)
        self.assertIn("edit", fails[0])
        self.assertIn("lod_up", fails[0])

    def test_o2_fails_when_the_second_lap_renders_changes_a_cause_or_a_crc(self):
        base = sample(0, "baseline")
        lap1 = [sample(1, "s0")]
        fails = poc.judge_o2(base, lap1 + [sample(2, "s0", renders=6, causes={"new": 5, "lod_up": 1}, crcs={"1": 11, "2": 23})])
        text = "\n".join(fails)
        self.assertIn("static renders 5 -> 6", text)
        self.assertIn("render causes changed: {'lod_up': 1}", text)
        self.assertIn("tile CRC differs from the first lap for 2", text)

    def test_o2_fails_on_a_light_without_a_tile_in_any_lap(self):
        base = sample(0, "baseline")
        fails = poc.judge_o2(base, [sample(1, "s0", without=2)])
        self.assertEqual(len(fails), 1)
        self.assertIn("2 lights without a tile", fails[0])

    def test_fan_must_cause_no_render_at_all(self):
        a = {"static": 5, "dynamic": 0, "causes": {"edit": 0}}
        self.assertEqual(poc.judge_o6_fan(a, dict(a)), [])
        fails = poc.judge_o6_fan(a, {"static": 6, "dynamic": 3, "causes": {"edit": 1}})
        self.assertEqual(len(fails), 3)

    def test_spot_is_drawn_every_frame_while_it_turns_and_once_more_when_it_stops(self):
        before = {"frame": 100, "dynamic": 10, "static": 5, "without": 0}
        during = {"frame": 160, "dynamic": 70, "static": 5, "without": 0}
        after = {"frame": 190, "dynamic": 70, "static": 6, "without": 0}
        self.assertEqual(poc.judge_o6_spot(before, during, after), [])
        slow = dict(during, dynamic=20)
        self.assertIn("not redrawn every frame", poc.judge_o6_spot(before, slow, after)[0])
        self.assertIn("no static render after the spot stopped", poc.judge_o6_spot(before, during, dict(after, static=5))[0])
        self.assertIn("without a tile", poc.judge_o6_spot(before, dict(during, without=1), after)[0])
        self.assertIn("did not advance", poc.judge_o6_spot(before, dict(before), after)[0])

    def test_lamp_edit_waits_at_most_250_ms(self):
        before = {"causes": {"edit": 2}, "dirty_age_ms_max": 0.0}
        self.assertEqual(poc.judge_o6_lamp(before, dict(before, dirty_age_ms_max=16.0))[0], "NOT RUN")
        edited = {"causes": {"edit": 5}, "dirty_age_ms_max": 40.0}
        self.assertEqual(poc.judge_o6_lamp(before, edited)[0], "PASS")
        late = {"causes": {"edit": 5}, "dirty_age_ms_max": 400.0}
        status, why = poc.judge_o6_lamp(before, late)
        self.assertEqual(status, "FAIL")
        self.assertIn("400 ms", why)

    def test_pool_extras_reads_the_p3_counters(self):
        st = {"pool": {"rerender_causes": {"edit": 2}, "dynamic_renders": 3, "static_renders": 4, "pinned_objects": 5, "frame": 6,
                       "pending_tiles": 1, "lights_without_tile": 2, "dirty_age_ms_max": 12.5, "tiles": [{"key": 7, "drawn_frame": 9}]}}
        ex = poc.pool_extras(st)
        self.assertEqual((ex["causes"], ex["dynamic"], ex["static"], ex["pinned"], ex["frame"], ex["pending"], ex["without"]),
                         ({"edit": 2}, 3, 4, 5, 6, 1, 2))
        self.assertEqual(ex["drawn"], {"7": 9})
        self.assertEqual(ex["dirty_age_ms_max"], 12.5)
        self.assertEqual(poc.pool_extras({})["causes"], {})


def make_check(model, tmp, o2=None, o6=None):
    from leap_client import LeapSession
    from light_check import Viewer
    import fake_viewer as fvm
    fv = fvm.FakeViewer(projector=model)
    session = LeapSession(fv.plugin_in, fv.plugin_out, default_timeout=10).start()
    sc = {"name": "t", "settle": {"after_camera_s": 0.0, "step_s": 0.0}, "camera": {"height_m": 1.6, "look_distance_m": 10.0},
          "o1": {"yaw_step_deg": 90, "pitch_deg": []}, "o2": o2 or {"radii_m": [3.0, 8.0], "steps": 4, "laps": 2, "snapshots": False},
          "o6": o6 or {}}
    check = poc.Check(Viewer(session), sc, tmp)
    check.warm_up = lambda: None
    return check


class OrbitGateAgainstTheFakeViewerTest(unittest.TestCase):
    def setUp(self):
        import tempfile
        self.tmp = tempfile.mkdtemp()

    def test_o2_passes_against_a_pool_that_ignores_the_camera(self):
        check = make_check(fake_pool(lod_ups=3), self.tmp)   # one lift before the baseline, two in the first lap
        g = check.gate_o2()
        self.assertEqual(g["status"], "PASS", g)
        self.assertEqual(g["steps"], 16)                      # 2 radii x 2 laps x 4 steps
        self.assertEqual(g["causes"], {"lod_up": 2})          # the pin's lifts, in the first lap
        check.cleanup()

    def test_o2_fails_against_a_pool_that_redraws_with_the_camera(self):
        check = make_check(fake_pool(camera_bug=True), self.tmp)
        g = check.gate_o2()
        self.assertEqual(g["status"], "FAIL", g)
        self.assertTrue(any("edit" in f for f in g["failures"]))
        self.assertTrue(any("lap 2" in f and "static renders" in f for f in g["failures"]))
        check.cleanup()

    def test_o2_is_not_run_without_a_pool_tiles_or_crcs(self):
        self.assertEqual(make_check(fake_pool(present=False), self.tmp).gate_o2()["status"], "NOT RUN")
        g = make_check(fake_pool(tiles=0), self.tmp).gate_o2()
        self.assertEqual(g["status"], "NOT RUN")
        self.assertIn("no projector tile", g["why"])
        g = make_check(fake_pool(crc=False), self.tmp).gate_o2()
        self.assertEqual(g["status"], "NOT RUN")
        self.assertIn("no tile CRC", g["why"])

    def test_o2_needs_two_laps(self):
        g = make_check(fake_pool(), self.tmp, o2={"radii_m": [3.0], "steps": 4, "laps": 1}).gate_o2()
        self.assertEqual(g["status"], "NOT RUN")

    def test_o2_fails_on_a_light_without_a_tile(self):
        g = make_check(fake_pool(without=1), self.tmp).gate_o2()
        self.assertEqual(g["status"], "FAIL")
        self.assertTrue(any("lights without a tile" in f for f in g["failures"]))

    def test_o6_is_not_run_unless_asked_for(self):
        self.assertEqual(make_check(fake_pool(), self.tmp).gate_o6()["status"], "NOT RUN")

    def test_o6_runs_its_three_windows_and_names_the_part_that_did_not_run(self):
        model = fake_pool()
        check = make_check(model, self.tmp, o6={"run": True, "windows": {"prompt_s": 0, "fan_s": 0, "spot_s": 0, "spot_stop_s": 0, "lamp_edit_s": 0}})
        g = check.gate_o6()
        # the fan saw nothing change (PASS); the spot never moved the frame or drew dynamically (FAIL: the model is static)
        self.assertEqual(g["parts"]["fan"]["status"], "PASS")
        self.assertEqual(g["parts"]["spot"]["status"], "FAIL")
        self.assertEqual(g["parts"]["lamp_edit"]["status"], "NOT RUN")
        self.assertEqual(g["status"], "FAIL")


def fake_pool(**kw):
    import fake_viewer as fvm
    return fvm.PoolModel(**kw)


if __name__ == "__main__":
    unittest.main()
