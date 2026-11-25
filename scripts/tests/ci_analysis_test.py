# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Tests of scripts/ci_analysis.py: the branch-to-library plan, the log parsers and the baseline comparison."""
import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import ci_analysis as ca  # noqa: E402

TIDY = """/home/runner/work/v/v/indra/llcommon/llstring.cpp:10:5: warning: use of a moved value [bugprone-use-after-move]
/home/runner/work/v/v/indra/llcommon/llstring.cpp:10:5: warning: use of a moved value [bugprone-use-after-move]
/home/runner/work/v/v/indra/llmath/llvector.cpp:3:1: warning: x [bugprone-sizeof-expression]
/usr/include/c++/15/string:1:1: warning: system [bugprone-sizeof-expression]
"""

ASAN = """==1==ERROR: AddressSanitizer: heap-use-after-free on address 0x6020 at pc 0x55
READ of size 4 at 0x6020 thread T0
    #0 0x55 in foo(int) /home/runner/work/v/v/indra/llcommon/llfoo.cpp:42:7
    #1 0x56 in bar() /home/runner/work/v/v/indra/llcommon/llbar.cpp:9:1
    #2 0x57 in main /usr/lib/x.c:1:1

SUMMARY: AddressSanitizer: heap-use-after-free
"""

LSAN = """==2==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 8 byte(s) in 1 object(s) allocated from:
    #0 0x1 in operator new(unsigned long) (/lib/libasan.so+0x1)
    #1 0x2 in leaky() /home/runner/work/v/v/indra/llcommon/llleak.cpp:5:3

SUMMARY: AddressSanitizer: 8 byte(s) leaked in 1 allocation(s).
"""

UBSAN = "/home/runner/work/v/v/indra/llcommon/llmath.h:77:12: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'\n"

TSAN = """WARNING: ThreadSanitizer: data race (pid=7)
  Write of size 4 at 0x1 by thread T1:
    #0 worker() /home/runner/work/v/v/indra/llcommon/llq.cpp:20:3 (bin+0x1)
    #1 run /usr/include/c++/15/thread:1:1 (bin+0x2)

SUMMARY: ThreadSanitizer: data race
"""

MAKE = """make[2]: *** [llcommon/CMakeFiles/llcommon.dir/build.make:75: llcommon/CMakeFiles/llcommon.dir/llx.cpp.o] Error 1
make[1]: *** [CMakeFiles/Makefile2:900: llcommon/CMakeFiles/llcommon.dir/all] Error 2
"""

SARIF = json.dumps({"runs": [{"results": [
    {"ruleId": "C6001", "message": {"text": "Using uninitialized memory"},
     "locations": [{"physicalLocation": {"artifactLocation": {"uri": "file:///D:/a/v/v/indra/llcommon/llx.cpp"},
                                         "region": {"startLine": 4, "startColumn": 2}}}]},
    {"ruleId": "C6001", "message": {"text": "dup"},
     "locations": [{"physicalLocation": {"artifactLocation": {"uri": "file:///D:/a/v/v/indra/llcommon/llx.cpp"},
                                         "region": {"startLine": 4, "startColumn": 2}}}]},
    {"ruleId": "C6011", "message": {"text": "elsewhere"},
     "locations": [{"physicalLocation": {"artifactLocation": {"uri": "file:///D:/a/v/v/indra/llmath/y.cpp"},
                                         "region": {"startLine": 1, "startColumn": 1}}}]},
]}]})


class PlanTest(unittest.TestCase):
    def test_library_branch(self):
        plan = ca.resolve("clean/llcommon", "push")
        self.assertTrue(plan["run"])
        self.assertEqual((plan["lib"], plan["scope"]), ("llcommon", "lib"))
        self.assertIn("lltrace", plan["tests"])
        self.assertIn("lltrace", plan["tsan_tests"])
        self.assertNotIn("lleventcoro", plan["tsan_tests"])

    def test_newview_area(self):
        plan = ca.resolve("clean/newview-render-pipeline", "pull_request")
        self.assertEqual((plan["run"], plan["lib"], plan["scope"]), (True, "newview", "newview"))

    def test_other_pull_request_is_the_self_test(self):
        plan = ca.resolve("codex/analysis-ci", "pull_request")
        self.assertEqual((plan["run"], plan["lib"]), (True, "llcommon"))

    def test_unknown_library_runs_nothing(self):
        plan = ca.resolve("clean/nosuchlib", "push")
        self.assertFalse(plan["run"])
        self.assertIn("nothing ran", plan["reason"])

    def test_dispatch_input(self):
        self.assertEqual(ca.resolve("", "workflow_dispatch", "llmath")["lib"], "llmath")
        self.assertEqual(ca.resolve("", "workflow_dispatch", "")["lib"], "llcommon")


class ParserTest(unittest.TestCase):
    def test_tidy_dedupes_and_scopes(self):
        found = ca.parse_tidy(TIDY, "llcommon")
        self.assertEqual([(f["check"], f["file"]) for f in found], [("bugprone-use-after-move", "indra/llcommon/llstring.cpp")])

    def test_sanitizers(self):
        asan = ca.parse_sanitizer(ASAN)
        self.assertEqual((asan[0]["tool"], asan[0]["check"], asan[0]["file"]),
                         ("asan", "heap-use-after-free", "indra/llcommon/llfoo.cpp"))
        self.assertIn("foo(int)>bar()", asan[0]["fp"])
        self.assertNotIn("42", asan[0]["fp"])
        lsan = ca.parse_sanitizer(LSAN)
        self.assertEqual((lsan[0]["tool"], lsan[0]["check"]), ("lsan", "leak"))
        ub = ca.parse_sanitizer(UBSAN)
        self.assertEqual(ub[0]["tool"], "ubsan")
        self.assertTrue(ub[0]["check"].startswith("signed integer overflow"))
        tsan = ca.parse_sanitizer(TSAN)
        self.assertEqual((tsan[0]["tool"], tsan[0]["check"], tsan[0]["file"]),
                         ("tsan", "data-race", "indra/llcommon/llq.cpp"))

    def test_make_failures_are_leaf_rules_only(self):
        found = ca.parse_build_log(MAKE)
        self.assertEqual(len(found), 1)
        self.assertIn("llx.cpp.o", found[0]["message"])

    def test_msvc_sarif_is_scoped_and_deduped(self):
        found = ca.parse_msvc([SARIF], "", "llcommon")
        self.assertEqual([(f["check"], f["file"], f["line"]) for f in found], [("C6001", "indra/llcommon/llx.cpp", 4)])

    def test_msvc_log_fallback(self):
        log = r"D:\a\v\v\indra\llcommon\llx.cpp(12,3): warning C6385: reading invalid data [D:\a\v\v\b\llcommon.vcxproj]"
        found = ca.parse_msvc([], log, "llcommon")
        self.assertEqual([(f["check"], f["line"]) for f in found], [("C6385", 12)])


class BaselineTest(unittest.TestCase):
    def test_new_is_over_baseline_per_fingerprint(self):
        found = ca.parse_sanitizer(ASAN + ASAN)
        self.assertEqual(len(found), 2)
        fp = found[0]["fp"]
        self.assertIsNone(ca.count_new(found, None))
        self.assertEqual(ca.count_new(found, {fp: 2}), 0)
        self.assertEqual(ca.count_new(found, {fp: 1}), 1)
        self.assertEqual(ca.count_new(found, {}), 2)

    def test_sarif_output(self):
        found = ca.parse_tidy(TIDY, "llcommon")
        document = ca.to_sarif(found, "clang-tidy")
        self.assertEqual(document["version"], "2.1.0")
        self.assertEqual(document["runs"][0]["results"][0]["ruleId"], "bugprone-use-after-move")


if __name__ == "__main__":
    unittest.main()
