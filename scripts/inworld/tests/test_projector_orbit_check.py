# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline tests of the pure parts of projector_orbit_check.py (the live gates need the development viewer)."""
import json
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

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


if __name__ == "__main__":
    unittest.main()
