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

    def test_spot_is_drawn_every_frame_while_it_turns_and_its_final_pose_is_drawn_when_it_stops(self):
        before = {"frame": 100, "dynamic": 10, "static": 5, "without": 0}
        during = {"frame": 160, "dynamic": 70, "static": 5, "without": 0}
        after = {"frame": 190, "dynamic": 70, "static": 5, "without": 0, "pose_stale": 0, "lamp_moving": 0}   # the final pose was a dynamic render
        self.assertEqual(poc.judge_o6_spot(before, during, after), [])
        slow = dict(during, dynamic=20)
        self.assertIn("not redrawn every frame", poc.judge_o6_spot(before, slow, after)[0])
        self.assertIn("still behind their lamp's pose", poc.judge_o6_spot(before, during, dict(after, pose_stale=1))[0])
        self.assertIn("still read as moving", poc.judge_o6_spot(before, during, dict(after, lamp_moving=1))[0])
        self.assertIn("without a tile", poc.judge_o6_spot(before, dict(during, without=1), after)[0])
        self.assertIn("did not advance", poc.judge_o6_spot(before, dict(before), after)[0])
        self.assertIn("did not advance after", poc.judge_o6_spot(before, during, dict(after, frame=160))[0])

    def test_lamp_edit_waits_at_most_250_ms(self):
        before = {"causes": {"edit": 2}, "dirty_age_ms_max": 0.0}
        self.assertEqual(poc.judge_o6_lamp(before, dict(before, dirty_age_ms_max=16.0))[0], "NOT RUN")
        edited = {"causes": {"edit": 5}, "dirty_age_ms_max": 40.0}
        self.assertEqual(poc.judge_o6_lamp(before, edited)[0], "PASS")
        late = {"causes": {"edit": 5}, "dirty_age_ms_max": 400.0}
        status, why = poc.judge_o6_lamp(before, late)
        self.assertEqual(status, "FAIL")
        self.assertIn("400 ms", why)

    def test_o2_leaves_avatar_tiles_out_and_judges_each_radius_by_its_own_first_lap(self):
        base = sample(0, "baseline")
        # radius 0: lap 1 settles with an edit (allowed by the caller), lap 2 is the gate; tile 2 holds an avatar and changes its CRC
        r0a = dict(sample(1, "s0", 6, causes={"new": 5, "edit": 1}), settle=True, group=0, avatar_keys=["2"])
        r0b = dict(sample(2, "s0", 6, causes={"new": 5, "edit": 1, "avatar": 2}, crcs={"1": 11, "2": 77}), settle=False, group=0, avatar_keys=["2"])
        # radius 1: its own settling lap (a late arrival changes tile 1), then a gate lap equal to it
        r1a = dict(sample(3, "s0", 6, causes={"new": 5, "edit": 2, "avatar": 2}, crcs={"1": 55, "2": 77}), settle=True, group=1)
        r1b = dict(sample(4, "s0", 6, causes={"new": 5, "edit": 2, "avatar": 2}, crcs={"1": 55, "2": 77}), settle=False, group=1)
        self.assertEqual(poc.judge_o2(base, [r0a, r0b, r1a, r1b], ("lod_up", "edit")), [])
        # the same tile 1 change in a gate lap is a failure
        bad = dict(r1b, crcs={"1": 56, "2": 77})
        self.assertIn("tile CRC differs from the first lap for 1", poc.judge_o2(base, [r0a, r0b, r1a, bad], ("lod_up", "edit"))[0])

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
    check.fake_viewer = fv
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

    def test_o2_reports_avatar_tiles_and_honours_the_centre(self):
        model = fake_pool()
        model.avatar_tiles = {2}
        check = make_check(model, self.tmp, o2={"radii_m": [3.0], "steps": 4, "laps": 2, "snapshots": False, "centre": [10.0, 20.0, 30.0]})
        seen = []
        orig = check.aim
        check.aim = lambda cam, focus: (seen.append(focus), orig(cam, focus))[1]
        g = check.gate_o2()
        self.assertEqual(g["status"], "PASS", g)
        self.assertEqual(g["avatar_tiles"], ["2"])
        self.assertTrue(all(f[:2] == [10.0, 20.0] for f in seen))
        check.cleanup()

    def test_contact_sheet_keeps_each_picture_s_aspect(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow not installed")
        check = make_check(fake_pool(), self.tmp)
        files = []
        for i, size in enumerate([(640, 480), (1920, 1080)]):
            f = pathlib.Path(self.tmp) / ("s%d.png" % i)
            Image.new("RGB", size, (200, 10, 10)).save(f)
            files.append(f)
        out = check.contact_sheet(files, pathlib.Path(self.tmp) / "sheet.png")
        with Image.open(out) as sheet:
            self.assertEqual(sheet.size, (6 * 320, 180))
            self.assertEqual(sheet.getpixel((10, 90)), (0, 0, 0))   # the 4:3 picture is centred in its cell, the margin is black
            self.assertEqual(sheet.getpixel((160, 90)), (200, 10, 10))

    def test_o2_needs_two_laps(self):
        g = make_check(fake_pool(), self.tmp, o2={"radii_m": [3.0], "steps": 4, "laps": 1}).gate_o2()
        self.assertEqual(g["status"], "NOT RUN")

    def test_o2_fails_on_a_light_without_a_tile(self):
        g = make_check(fake_pool(without=1), self.tmp).gate_o2()
        self.assertEqual(g["status"], "FAIL")
        self.assertTrue(any("lights without a tile" in f for f in g["failures"]))

    def test_the_scenario_teleport_uses_the_handler_and_opens_no_dialog(self):
        check = make_check(fake_pool(), self.tmp)
        check.sc["teleport"] = {"region": "Busy", "x": 10, "y": 20, "z": 30}
        check.sc["settle"]["after_teleport_s"] = 0.0
        check.teleport()
        self.assertEqual(check.fake_viewer.confirm_dialogs, [])   # LLAgent.requestTeleport would open a dialog nobody clicks
        self.assertEqual([(d["regionname"], d["x"], d["y"], d["z"]) for d in check.fake_viewer.tp_requests], [("Busy", 10, 20, 30)])
        check.cleanup()

    def test_o6_is_not_run_unless_asked_for(self):
        self.assertEqual(make_check(fake_pool(), self.tmp).gate_o6()["status"], "NOT RUN")

    def test_o6_passes_when_the_three_windows_behave(self):
        check = make_check(ScriptedPool(), self.tmp, o6={"run": True, "windows": {"prompt_s": 0, "fan_s": 0, "spot_s": 0, "spot_stop_s": 0, "lamp_edit_s": 0}})
        g = check.gate_o6()
        self.assertEqual(g["status"], "PASS", g)
        self.assertEqual({k: p["status"] for k, p in g["parts"].items()}, {"fan": "PASS", "spot": "PASS", "lamp_edit": "PASS"})

    def test_o6_runs_its_three_windows_and_names_the_part_that_did_not_run(self):
        model = fake_pool()
        check = make_check(model, self.tmp, o6={"run": True, "windows": {"prompt_s": 0, "fan_s": 0, "spot_s": 0, "spot_stop_s": 0, "lamp_edit_s": 0}})
        g = check.gate_o6()
        # the fan saw nothing change (PASS); the spot never moved the frame or drew dynamically (FAIL: the model is static)
        self.assertEqual(g["parts"]["fan"]["status"], "PASS")
        self.assertEqual(g["parts"]["spot"]["status"], "FAIL")
        self.assertEqual(g["parts"]["lamp_edit"]["status"], "NOT RUN")
        self.assertEqual(g["status"], "FAIL")


class ScriptedPool:
    """A PoolModel whose reads advance a script: the O-6 windows pass (the fan sees nothing, the spot turns for 60 frames and its final
    pose is drawn as a dynamic item, an edited object under a lamp is drawn within 40 ms)."""

    def __new__(cls):
        import fake_viewer as fvm

        class Model(fvm.PoolModel):
            def __init__(self):
                super().__init__()
                self.reads = 0

            def stats(self, d):
                self.reads += 1
                if self.reads == 5:        # the spot's window ends: it turned for 60 frames, drawn in each
                    self.frame += 60
                    self.dynamic += 60
                elif self.reads == 6:      # it stopped; the frames go on and no tile is behind its pose
                    self.frame += 30
                elif self.reads == 8:      # an edit under a lamp
                    self.causes["edit"] += 2
                    self.dirty_age_ms_max = 40.0
                return super().stats(d)
        return Model()


def fake_pool(**kw):
    import fake_viewer as fvm
    return fvm.PoolModel(**kw)


if __name__ == "__main__":
    unittest.main()
