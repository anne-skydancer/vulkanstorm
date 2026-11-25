# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Tests of scripts/ci_analysis.py: the branch-to-library plan, the log parsers, the gate and the commands.

The fixtures in scripts/tests/fixtures/analysis are excerpts of the real artifacts of the first llcommon run
(Actions run 45545911963): an ASan build log with the UBSan errors and the failing llprocess test, a TSan
lock-order report and an LSan leak report."""
import argparse
import contextlib
import io
import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "scripts" / "tests" / "fixtures" / "analysis"
sys.path.insert(0, str(ROOT / "scripts"))
import ci_analysis as ca  # noqa: E402


def fixture(name):
    return (FIXTURES / name).read_text(encoding="utf-8")


def quiet(function, *args, **kwargs):
    with contextlib.redirect_stdout(io.StringIO()):
        return function(*args, **kwargs)


def make_repo(libs, tests=None, deny=None):
    """A fixture checkout: indra/<lib>/CMakeLists.txt with add_library and the named integration tests."""
    root = pathlib.Path(tempfile.mkdtemp())
    for lib in libs:
        (root / "indra" / lib).mkdir(parents=True)
        lines = [f"add_library ({lib} STATIC a.cpp)"]
        lines += [f'  LL_ADD_INTEGRATION_TEST({name} "" "${{test_libs}}")' for name in (tests or {}).get(lib, [])]
        lines.append('##LL_ADD_INTEGRATION_TEST(commented "" "")')
        (root / "indra" / lib / "CMakeLists.txt").write_text("\n".join(lines), encoding="utf-8")
    if deny is not None:
        (root / ".github" / "analysis" / "tsan").mkdir(parents=True)
        for lib, names in deny.items():
            (root / ".github" / "analysis" / "tsan" / f"{lib}-deny.txt").write_text("\n".join(names), encoding="utf-8")
    return root


def found(tool, check, file="indra/llcommon/x.cpp", line=1, message="m", key="", group=""):
    return ca.finding(tool, check, file, line, message, key, group)


class PlanTest(unittest.TestCase):
    def setUp(self):
        self.root = make_repo(["llcommon", "llmath", "cmake", "llkdu"],
                              {"llcommon": ["lltrace", "llcond", "llsingleton"], "llmath": []},
                              {"llcommon": ["llcond", "nosuchtest"]})

    def test_libraries_need_add_library_and_exclude_unbuilt(self):
        self.assertEqual(ca.libraries(self.root), ["llcommon", "llmath"])
        (self.root / "indra" / "plain").mkdir()
        (self.root / "indra" / "plain" / "CMakeLists.txt").write_text("project(x)", encoding="utf-8")
        self.assertNotIn("plain", ca.libraries(self.root))

    def test_integration_tests_skip_commented_lines(self):
        self.assertEqual(ca.integration_tests("llcommon", self.root), ["lltrace", "llcond", "llsingleton"])

    def test_library_branch_and_deny_list(self):
        plan = ca.resolve("clean/llcommon", "push", root=self.root)
        self.assertTrue(plan["run"])
        self.assertEqual((plan["lib"], plan["scope"]), ("llcommon", "lib"))
        self.assertEqual(plan["tsan_tests"], ["lltrace", "llsingleton"])
        self.assertEqual(plan["deny_unmatched"], ["nosuchtest"])
        self.assertIn("TSan runs 2 of 3 tests", plan["tsan_reason"])

    def test_no_deny_file_runs_every_test_and_says_so(self):
        (self.root / ".github" / "analysis" / "tsan" / "llcommon-deny.txt").unlink()
        plan = ca.resolve("clean/llcommon", "push", root=self.root)
        self.assertEqual(len(plan["tsan_tests"]), 3)
        self.assertIn("no deny list", plan["tsan_reason"])

    def test_library_without_tests_says_why_there_is_no_tsan_job(self):
        plan = ca.resolve("clean/llmath", "push", root=self.root)
        self.assertEqual(plan["tsan_tests"], [])
        self.assertIn("no integration tests", plan["tsan_reason"])

    def test_every_test_denied(self):
        (self.root / ".github" / "analysis" / "tsan" / "llcommon-deny.txt").write_text("lltrace\nllcond\nllsingleton")
        self.assertIn("deny list", ca.resolve("clean/llcommon", "push", root=self.root)["tsan_reason"])

    def test_newview_area(self):
        plan = ca.resolve("clean/newview-render-pipeline", "pull_request", root=self.root)
        self.assertEqual((plan["run"], plan["lib"], plan["scope"]), (True, "newview-render-pipeline", "newview"))

    def test_clean_branch_naming_nothing_fails(self):
        for head in ("clean/nosuchlib", "clean/newview", "clean/cmake", "clean/llkdu", "clean/"):
            plan = ca.resolve(head, "push", root=self.root)
            self.assertFalse(plan["run"], head)
            self.assertIn("names no analysable target", plan["error"], head)

    def test_other_pull_request_is_the_self_test_only_when_analysis_files_changed(self):
        plan = ca.resolve("codex/analysis-ci", "pull_request", root=self.root, changed=["scripts/ci_analysis.py"])
        self.assertEqual((plan["run"], plan["lib"]), (True, "llcommon"))
        plan = ca.resolve("codex/other", "pull_request", root=self.root, changed=["indra/newview/a.cpp"])
        self.assertFalse(plan["run"])
        self.assertEqual(plan["error"], "")
        plan = ca.resolve("codex/other", "pull_request", root=self.root, changed=None)
        self.assertTrue(plan["run"])

    def test_touches_analysis(self):
        self.assertTrue(ca.touches_analysis([".github/analysis/baseline/llcommon.json"]))
        self.assertTrue(ca.touches_analysis([".github/actions/analysis-setup/action.yml"]))
        self.assertFalse(ca.touches_analysis([".github/workflows/build-linux.yaml", "doc/clean/analysis.md"]))

    def test_push_with_an_open_pull_request_is_skipped(self):
        plan = ca.resolve("clean/llcommon", "push", root=self.root, has_pull_request=True)
        self.assertFalse(plan["run"])
        self.assertIn("pull request analyses", plan["reason"])
        self.assertTrue(ca.resolve("clean/llcommon", "pull_request", root=self.root, has_pull_request=True)["run"])

    def test_dispatch_input(self):
        self.assertEqual(ca.resolve("", "workflow_dispatch", "llmath", root=self.root)["lib"], "llmath")
        self.assertEqual(ca.resolve("", "workflow_dispatch", "", root=self.root)["lib"], "llcommon")

    def test_real_repository_branches(self):
        self.assertIn("llcommon", ca.libraries())
        plan = ca.resolve("clean/llcommon", "push")
        self.assertIn("lltrace", plan["tests"])
        self.assertNotIn("llleap", plan["tsan_tests"])


class PathTest(unittest.TestCase):
    def test_workspace_prefixes_are_removed(self):
        for path in (r"D:\a\v\v\indra\llcommon\x.cpp", "d:/a/v/v/indra/llcommon/x.cpp", "/__w/v/v/indra/llcommon/x.cpp",
                     "/home/runner/work/v/v/indra/llcommon/x.cpp", "file:///D:/a/v/v/indra/llcommon/x.cpp",
                     "indra/llcommon/x.cpp", "../../indra/llcommon/x.cpp", r"..\..\indra\llcommon\x.cpp"):
            self.assertEqual(ca.rel(path), "indra/llcommon/x.cpp", path)
            self.assertTrue(ca.project_file(path), path)
        self.assertFalse(ca.project_file("/usr/include/c++/15/string"))

    def test_messages_lose_the_workspace(self):
        text = r"see previous declaration at line '636' of 'd:\a\v\v\indra\llcommon\llerror.cpp'"
        self.assertEqual(ca.strip_workspace(text), "see previous declaration at line '636' of 'indra/llcommon/llerror.cpp'")


TIDY = """/home/runner/work/v/v/indra/llcommon/llstring.cpp:10:5: warning: use of a moved value [bugprone-use-after-move]
/home/runner/work/v/v/indra/llcommon/llstring.cpp:10:5: warning: use of a moved value [bugprone-use-after-move]
/home/runner/work/v/v/indra/llmath/llvector.cpp:3:1: warning: x [bugprone-sizeof-expression]
/usr/include/c++/15/string:1:1: warning: system [bugprone-sizeof-expression]
/home/runner/work/v/v/indra/llcommon/llx.cpp:3:1: error: unknown type [clang-diagnostic-error]
"""


class TidyTest(unittest.TestCase):
    def test_dedupes_and_scopes(self):
        got = ca.parse_tidy(TIDY, "llcommon")
        self.assertEqual([(f["check"], f["file"]) for f in got],
                         [("bugprone-use-after-move", "indra/llcommon/llstring.cpp"),
                          ("clang-diagnostic-error", "indra/llcommon/llx.cpp")])
        self.assertEqual(ca.family(got[0]), "bugprone")

    def test_markers(self):
        log = ("analysis: clang-tidy over 3 translation unit(s) of newview-x\nanalysis: only indra/newview/a.h\n"
               "analysis: tidy-exit 1 indra/newview/b.cpp\nanalysis: processed 3 of 3\n")
        self.assertEqual(ca.parse_tidy_markers(log), (3, 3, [(1, "indra/newview/b.cpp")], {"indra/newview/a.h"}))

    def test_tidy_files_skip_tests_and_other_libraries(self):
        root = pathlib.Path(tempfile.mkdtemp())
        database = root / "compile_commands.json"
        database.write_text(json.dumps([
            {"directory": "/w/build", "file": "/w/indra/llcommon/a.cpp"},
            {"directory": "/w/build", "file": "/w/indra/llcommon/tests/a_test.cpp"},
            {"directory": "/w/build", "file": "/w/indra/llmath/m.cpp"},
            {"directory": "/w/build", "file": "../indra/llcommon/b.cc"},
            {"directory": "/w/build", "file": "/w/indra/llcommon/c.h"}]), encoding="utf-8")
        self.assertEqual(ca.tidy_files(database, "llcommon"), ["indra/llcommon/a.cpp", "indra/llcommon/b.cc"])

    def test_header_maps_to_every_including_translation_unit(self):
        root = pathlib.Path(tempfile.mkdtemp())
        (root / "indra" / "newview").mkdir(parents=True)
        files = {"indra/newview/widget.h": "", "indra/newview/mid.h": '#include "widget.h"\n',
                 "indra/newview/a.cpp": '#include "mid.h"\n', "indra/newview/b.cpp": '#include "widget.h"\n',
                 "indra/newview/c.cpp": '#include "other.h"\n'}
        for name, text in files.items():
            (root / name).write_text(text, encoding="utf-8")
        units = {"indra/newview/a.cpp", "indra/newview/b.cpp", "indra/newview/c.cpp"}
        self.assertEqual(ca.affected_units(["indra/newview/widget.h"], units, root),
                         ["indra/newview/a.cpp", "indra/newview/b.cpp"])
        self.assertEqual(ca.affected_units(["indra/newview/c.cpp"], units, root), ["indra/newview/c.cpp"])
        self.assertEqual(ca.affected_units(["indra/newview/unused.h"], units, root), [])


class SanitizerTest(unittest.TestCase):
    def asan(self, line=42, extra=""):
        return (f"==1==ERROR: AddressSanitizer: heap-use-after-free on address 0x6020 at pc 0x55\nREAD of size 4\n"
                f"    #0 0x55 in foo(int) /home/runner/work/v/v/indra/llcommon/llfoo.cpp:{line}:7\n"
                f"    #1 0x56 in bar() /home/runner/work/v/v/indra/llcommon/llbar.cpp:9:1\n"
                f"    #2 0x57 in main /usr/lib/x.c:1:1\n\n{extra}SUMMARY: AddressSanitizer: heap-use-after-free\n")

    def test_asan_key_is_the_top_project_frame_and_survives_a_line_edit(self):
        first, second = ca.parse_sanitizer(self.asan(42))[0], ca.parse_sanitizer(self.asan(99))[0]
        self.assertEqual((first["tool"], first["check"], first["file"]), ("asan", "heap-use-after-free",
                                                                       "indra/llcommon/llfoo.cpp"))
        self.assertEqual(first["fp"], second["fp"])
        self.assertNotIn("llbar", first["fp"])
        self.assertEqual(len(ca.dedupe(ca.parse_sanitizer(self.asan(42) + self.asan(99)))), 1)

    def test_asan_uses_only_the_first_stack(self):
        extra = "freed by thread T0 here:\n    #0 0x1 in other() /home/runner/work/v/v/indra/llcommon/llfree.cpp:5:1\n\n"
        got = ca.parse_sanitizer(self.asan(extra=extra))[0]
        self.assertNotIn("llfree", got["fp"])

    def test_leak_report_of_the_real_run(self):
        got = ca.parse_sanitizer(fixture("lsan-leak.txt"))
        self.assertTrue(got and all(f["tool"] == "lsan" for f in got))
        self.assertEqual(got[0]["file"], "indra/llcommon/lllivefile.cpp")

    def test_tsan_key_is_the_sorted_set_of_stack_tops(self):
        got = ca.parse_sanitizer(fixture("tsan-lock-order.txt"))
        self.assertEqual(len(got), 1)
        self.assertEqual((got[0]["tool"], got[0]["check"], got[0]["file"]),
                         ("tsan", "lock-order-inversion", "indra/llcommon/lockstatic.h"))
        self.assertIn("LockStatic", got[0]["fp"])
        self.assertNotIn("pthread_mutex_lock", got[0]["fp"])

    def test_tsan_thread_creation_stack_is_not_part_of_the_key(self):
        text = ("WARNING: ThreadSanitizer: data race (pid=7)\n  Write of size 4 by thread T1:\n"
                "    #0 worker() /w/indra/llcommon/llq.cpp:20:3 (bin+0x1)\n\n  Previous read of size 4 by main thread:\n"
                "    #0 reader() /w/indra/llcommon/llr.cpp:8:3 (bin+0x1)\n\n"
                "  Thread T1 (tid=2, running) created by main thread at:\n"
                "    #0 start() /w/indra/llcommon/llstart.cpp:3:1 (bin+0x1)\n\nSUMMARY: ThreadSanitizer: data race\n")
        got = ca.parse_sanitizer(text)[0]
        self.assertEqual(got["fp"].split("|")[3], "reader()@indra/llcommon/llr.cpp>worker()@indra/llcommon/llq.cpp")
        self.assertNotIn("llstart", got["fp"])

    def test_reports_without_a_project_frame_are_not_collapsed(self):
        one = ("WARNING: ThreadSanitizer: data race (pid=7)\n  Write:\n    #0 alpha() /usr/include/a.h:1 (bin+0x1)\n\n")
        two = ("WARNING: ThreadSanitizer: data race (pid=7)\n  Write:\n    #0 beta() /usr/include/b.h:1 (bin+0x1)\n\n")
        got = ca.dedupe(ca.parse_sanitizer(one + two))
        self.assertEqual(len(got), 2)
        self.assertNotEqual(got[0]["group"], got[1]["group"])
        self.assertIn("no project frame", got[0]["fp"])

    def test_ubsan_errors_of_the_real_run_are_found_in_the_build_log(self):
        got = ca.parse_sanitizer(fixture("asan-build-excerpt.log"))
        ubsan = [f for f in got if f["tool"] == "ubsan"]
        self.assertEqual(len(ubsan), 4)
        self.assertEqual({(f["file"], f["line"]) for f in ubsan}, {("indra/llcommon/llprocess.cpp", 1261),
                                                                  ("indra/llcommon/llprocess.cpp", 1263)})
        self.assertEqual(ubsan[0]["check"], "load")
        self.assertEqual(len([f for f in ca.dedupe(got) if f["tool"] == "ubsan"]), 2)

    def test_the_same_report_in_two_sources_is_one_finding(self):
        text = fixture("asan-build-excerpt.log")
        self.assertEqual(len(ca.dedupe(ca.parse_sanitizer(text) + ca.parse_sanitizer(text))),
                         len(ca.dedupe(ca.parse_sanitizer(text))))

    def test_ubsan_outside_the_project_is_ignored(self):
        text = "/usr/include/c++/15/bits/x.h:5:3: runtime error: signed integer overflow: 1 + 2 cannot be represented\n"
        self.assertEqual(ca.parse_sanitizer(text), [])


class BuildTest(unittest.TestCase):
    MAKE = ("gmake[2]: *** [llcommon/CMakeFiles/llcommon.dir/build.make:75: llcommon/CMakeFiles/llcommon.dir/llx.cpp.o] Error 1\n"
            "make[1]: *** [CMakeFiles/Makefile2:900: llcommon/CMakeFiles/llcommon.dir/all] Error 2\n")

    def test_make_failures_are_leaf_rules_only(self):
        got = ca.parse_build_log(self.MAKE)
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0]["check"], "build-failed")

    def test_the_failing_test_of_the_real_run_is_a_test_failure(self):
        got = ca.parse_build_log(fixture("asan-build-excerpt.log"))
        self.assertEqual([f["check"] for f in got], ["test-failed"])
        self.assertEqual(ca.failed_test_targets(fixture("asan-build-excerpt.log")), ["INTEGRATION_TEST_llprocess"])

    def test_a_failed_step_is_never_silent(self):
        got = ca.parse_build_errors("nothing useful here\n", 2)
        self.assertEqual([f["check"] for f in got], ["step-failed"])
        self.assertEqual(ca.parse_build_errors("all fine\n", 0), [])
        self.assertEqual(ca.parse_build_errors("make: *** No rule to make target 'foo'.  Stop.\n", 2)[0]["check"], "no-rule")
        self.assertEqual(ca.parse_build_errors("CMake Error at CMakeLists.txt:3 (x):\n", 1)[0]["check"], "cmake-error")
        self.assertEqual(ca.parse_build_errors("indra/a.cpp:3:1: error: expected ';'\n", 1)[0]["check"], "compile-error")

    def test_msvc_errors(self):
        log = r"D:\a\v\v\indra\llcommon\x.cpp(3,1): error C2065: 'y': undeclared identifier [D:\a\v\v\b\llcommon.vcxproj]"
        self.assertEqual(ca.parse_build_errors_msvc(log, 1)[0]["check"], "msvc-error")
        self.assertEqual(ca.parse_build_errors_msvc("LINK : fatal error LNK1104: cannot open file", 1)[0]["check"],
                         "msvc-error")
        self.assertEqual(ca.parse_build_errors_msvc("nothing", 1)[0]["check"], "step-failed")
        self.assertEqual(ca.parse_build_errors_msvc("nothing", 0), [])

    def test_status_file(self):
        self.assertEqual(ca.parse_status("build=2\nother=0\njunk"), {"build": 2, "other": 0})

    def test_test_counts_of_the_real_run(self):
        text = fixture("asan-build-excerpt.log")
        runs = ca.parse_test_stats(text)
        binaries = ca.test_binaries(text)
        got, line = ca.test_findings(runs, binaries, len(binaries))
        self.assertEqual([f["check"] for f in got], ["test-failed"])
        self.assertIn("llprocess", got[0]["message"])
        self.assertIn("27 of 28", got[0]["message"])
        self.assertIn("test binaries run", line)
        self.assertEqual(runs[("LLHash", "LLMemoryStream", "LLSD", "U64_conversion")]["total"], 21)

    def test_no_tests_or_missing_binaries_are_findings(self):
        self.assertEqual([f["check"] for f in ca.test_findings({}, set(), 3)[0]], ["no-tests-run"])
        self.assertEqual([f["check"] for f in ca.test_findings({}, {"INTEGRATION_TEST_a"}, 3)[0]], ["tests-missing"])
        self.assertEqual(ca.test_findings({}, set(), 0)[0], [])

    def test_a_crashed_binary_has_no_summary(self):
        runs = ca.parse_test_stats("Unit test group_started name=boom\n")
        self.assertEqual([f["check"] for f in ca.test_findings(runs, {"INTEGRATION_TEST_boom"}, 1)[0]],
                         ["test-no-summary"])

    def test_retry_replaces_a_failed_run(self):
        first = "Unit test group_started name=g\nUnit test group_completed name=g\n\tTotal Tests:\t2\n\tPassed Tests:\t1\n"
        retry = "Unit test group_started name=g\nUnit test group_completed name=g\n\tTotal Tests:\t2\n\tPassed Tests:\t2\n"
        self.assertEqual(ca.test_findings(ca.parse_test_stats(first, retry), {"a"}, 1)[0], [])
        failing = [build_failed("INTEGRATION_TEST_g")]
        kept, flaky = ca.apply_retries(failing, "retry INTEGRATION_TEST_g exit 0\n")
        self.assertEqual((kept, flaky), ([], ["INTEGRATION_TEST_g"]))
        kept, flaky = ca.apply_retries(failing, "retry INTEGRATION_TEST_g exit 2\n")
        self.assertEqual((len(kept), flaky), (1, []))

    def test_skipped_known_failures_are_not_failures(self):
        text = ("Unit test group_started name=g\nUnit test group_completed name=g\n\tTotal Tests:\t2\n"
                "\tPassed Tests:\t1\n\tSkipped known failures:\t1\n")
        self.assertEqual(ca.test_findings(ca.parse_test_stats(text), {"a"}, 1)[0], [])


def build_failed(target):
    return ca.build_finding("test-failed", f"failed: llcommon/{target}", target)


SARIF = json.dumps({"runs": [{"results": [
    {"ruleId": "C6001", "message": {"text": "Using uninitialized memory"},
     "locations": [{"physicalLocation": {"artifactLocation": {"uri": "file:///D:/a/v/v/indra/llcommon/llx.cpp"},
                                         "region": {"startLine": 4, "startColumn": 2}}}]},
    {"ruleId": "C6001", "message": {"text": "dup"},
     "locations": [{"physicalLocation": {"artifactLocation": {"uri": "file:///D:/a/v/v/indra/llcommon/llx.cpp"},
                                         "region": {"startLine": 4, "startColumn": 2}}}]},
    {"ruleId": "C6011", "message": {"text": "relative"},
     "locations": [{"physicalLocation": {"artifactLocation": {"uri": "indra/llcommon/rel.cpp"},
                                         "region": {"startLine": 1, "startColumn": 1}}}]},
    {"ruleId": "C6011", "message": {"text": "elsewhere"},
     "locations": [{"physicalLocation": {"artifactLocation": {"uri": "file:///D:/a/v/v/indra/llmath/y.cpp"},
                                         "region": {"startLine": 1, "startColumn": 1}}}]},
]}]})


class MsvcTest(unittest.TestCase):
    def test_sarif_is_scoped_deduped_and_accepts_relative_uris(self):
        got = ca.parse_msvc([SARIF], "", "llcommon")
        self.assertEqual([(f["check"], f["file"], f["line"]) for f in got],
                         [("C6001", "indra/llcommon/llx.cpp", 4), ("C6011", "indra/llcommon/rel.cpp", 1)])

    def test_log_fallback_gives_the_same_key_as_sarif(self):
        message = ("Local declaration of 'tags' hides declaration of the same name in outer scope. For additional "
                   r"information, see previous declaration at line '636' of 'd:\a\v\v\indra\llcommon\llerror.cpp'.")
        sarif = json.dumps({"runs": [{"results": [{"ruleId": "C6246", "message": {"text": message + ": Lines: 636"},
                                                   "locations": [{"physicalLocation": {
                                                       "artifactLocation": {"uri": "file:///D:/a/v/v/indra/llcommon/llerror.cpp"},
                                                       "region": {"startLine": 678, "startColumn": 0}}}]}]}]})
        log = (r"D:\a\v\v\indra\llcommon\llerror.cpp(678): warning C6246: " + message +
               r": Lines: 636 [D:\a\v\v\build-msvc\llcommon\llcommon.vcxproj]")
        from_sarif = ca.parse_msvc([sarif], "", "llcommon")
        from_log = ca.parse_msvc([], log, "llcommon")
        self.assertEqual(len(from_log), 1)
        self.assertEqual(from_sarif[0]["fp"], from_log[0]["fp"])
        self.assertNotIn("d:", from_log[0]["message"])

    def test_library_built(self):
        self.assertTrue(ca.library_built("[100%] Built target llcommon\n", "llcommon"))
        self.assertTrue(ca.library_built(r"  llcommon.vcxproj -> D:\a\v\v\b\llcommon.lib", "llcommon"))
        self.assertFalse(ca.library_built("[100%] Built target llcommonextra\n", "llcommon"))


class GateTest(unittest.TestCase):
    def test_group_counts_and_compare(self):
        items = [found("tidy", "c1", "indra/llcommon/a.cpp", n) for n in (1, 2, 3)] + [found("tidy", "c2")]
        groups = ca.group_counts(items)
        self.assertEqual(groups, {"tidy|c1|indra/llcommon/a.cpp": 3, "tidy|c2|indra/llcommon/x.cpp": 1})
        self.assertEqual(ca.compare(groups, None), (None, None))
        self.assertEqual(ca.compare(groups, dict(groups)), (0, 0))
        self.assertEqual(ca.compare(groups, {"tidy|c1|indra/llcommon/a.cpp": 2, "tidy|c2|indra/llcommon/x.cpp": 1}), (1, 0))
        self.assertEqual(ca.compare(groups, {**groups, "gone|c|f": 4, "tidy|c1|indra/llcommon/a.cpp": 5}), (0, 2))

    def test_a_moved_message_is_not_new(self):
        before = found("clang-tidy", "bugprone-x", "indra/llcommon/a.cpp", 10, "message one")
        after = found("clang-tidy", "bugprone-x", "indra/llcommon/a.cpp", 99, "message reworded")
        self.assertEqual(ca.compare(ca.group_counts([after]), ca.group_counts([before])), (0, 0))

    def test_dedupe(self):
        a = found("clang-tidy", "c", line=1)
        self.assertEqual(len(ca.dedupe([a, dict(a), found("clang-tidy", "c", line=2)])), 2)
        asan = found("asan", "uaf", key="k")
        self.assertEqual(len(ca.dedupe([asan, dict(asan, line=9)])), 1)

    def test_baseline_state_wording(self):
        self.assertEqual(ca.baseline_state(None, None), "no baseline committed: report-only")
        self.assertEqual(ca.baseline_state(0, 0), "no new findings")
        self.assertIn("3 baseline entries are gone: shrink the baseline", ca.baseline_state(0, 3))

    def test_summary_lists_families_and_notes(self):
        text = ca.render_summary("clang-tidy", "llcommon", [found("clang-tidy", "modernize-use-auto"),
                                                             found("clang-tidy", "bugprone-x")], None, None, ["a note"])
        self.assertIn("| modernize | 1 |", text)
        self.assertIn("| bugprone | 1 |", text)
        self.assertIn("a note", text)
        self.assertIn("no baseline committed", text)


class CommandTest(unittest.TestCase):
    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp())
        self.saved = ca.BASELINE_DIR
        ca.BASELINE_DIR = self.dir / "baseline"
        ca.BASELINE_DIR.mkdir()

    def tearDown(self):
        ca.BASELINE_DIR = self.saved

    def write_baseline(self, lib, jobs):
        (ca.BASELINE_DIR / f"{lib}.json").write_text(json.dumps({"lib": lib, "jobs": jobs}), encoding="utf-8")

    def args(self, **values):
        base = dict(job="asan", lib="llcommon", out=str(self.dir / "out"), log="", status="", retry_log="",
                    canary="", expect_tests=0, san_dir="", dir=str(self.dir))
        base.update(values)
        return argparse.Namespace(**base)

    def collect_asan(self, log, status="build=0\n", canary=None, expect=0, san=None):
        (self.dir / "build.log").write_text(log, encoding="utf-8")
        (self.dir / "status.txt").write_text(status, encoding="utf-8")
        values = dict(log=str(self.dir / "build.log"), status=str(self.dir / "status.txt"), expect_tests=expect)
        if canary is not None:
            (self.dir / "canary.json").write_text(json.dumps(canary), encoding="utf-8")
            values["canary"] = str(self.dir / "canary.json")
        if san:
            (self.dir / "san").mkdir(exist_ok=True)
            for name, text in san.items():
                (self.dir / "san" / name).write_text(text, encoding="utf-8")
            values["san_dir"] = str(self.dir / "san")
        quiet(ca.cmd_collect, self.args(**values))
        return json.loads((self.dir / "out" / "findings-asan.json").read_text(encoding="utf-8"))

    def test_canary_reads_the_build_flags_from_the_cache(self):
        cache = self.dir / "CMakeCache.txt"
        cache.write_text("CMAKE_CXX_FLAGS:STRING=-fsanitize=address,undefined -O1" + chr(10) + "OTHER:STRING=x" + chr(10), encoding="utf-8")
        self.assertEqual(ca.cache_value(cache, "CMAKE_CXX_FLAGS"), "-fsanitize=address,undefined -O1")
        self.assertEqual(ca.cache_value(cache, "MISSING"), "")

    def test_canary_without_sanitizer_flags_reports_it(self):
        (self.dir / "CMakeCache.txt").write_text("CMAKE_CXX_FLAGS:STRING=-O1" + chr(10), encoding="utf-8")
        quiet(ca.cmd_canary, argparse.Namespace(mode="asan", dir=str(self.dir), out=str(self.dir / "out")))
        result = json.loads((self.dir / "out" / "canary-asan.json").read_text(encoding="utf-8"))
        self.assertFalse(result["ok"])
        self.assertIn("-fsanitize in the build's CMAKE_CXX_FLAGS", result["missing"])

    def test_failed_tests_command_prints_the_targets(self):
        (self.dir / "b.log").write_text(fixture("asan-build-excerpt.log"), encoding="utf-8")
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            ca.cmd_failed_tests(argparse.Namespace(log=str(self.dir / "b.log")))
        self.assertEqual(buffer.getvalue().split(), ["INTEGRATION_TEST_llprocess"])


    def test_load_baseline(self):
        self.assertIsNone(ca.load_baseline("llcommon", "asan"))
        self.write_baseline("llcommon", {"asan": {"g": 2}})
        self.assertEqual(ca.load_baseline("llcommon", "asan"), {"g": 2})
        self.assertIsNone(ca.load_baseline("llcommon", "tsan"))

    def test_collect_asan_reads_ubsan_from_the_build_log_and_the_canary_counts(self):
        record = self.collect_asan(fixture("asan-build-excerpt.log"), "build=2\n", {"ok": True, "missing": []},
                                   expect=len(ca.test_binaries(fixture("asan-build-excerpt.log"))),
                                   san={"report.1": fixture("lsan-leak.txt")})
        checks = {(f["tool"], f["check"]) for f in record["findings"]}
        self.assertIn(("ubsan", "load"), checks)
        self.assertIn(("lsan", "leak"), checks)
        self.assertIn(("build", "test-failed"), checks)
        self.assertEqual(len([f for f in record["findings"] if f["tool"] == "ubsan"]), 2)
        self.assertIsNone(record["new"])
        self.assertTrue(any("test binaries run" in note for note in record["notes"]))

    def test_a_silent_or_missing_canary_is_a_finding(self):
        record = self.collect_asan("[100%] Built target llcommon\n", canary={"ok": False, "missing": ["LeakSanitizer"]})
        self.assertIn("canary-silent", [f["check"] for f in record["findings"]])
        record = self.collect_asan("[100%] Built target llcommon\n")
        self.assertIn("canary-missing", [f["check"] for f in record["findings"]])

    def test_a_job_that_builds_or_runs_nothing_is_a_finding(self):
        record = self.collect_asan("", "build=0\n", {"ok": True, "missing": []}, expect=4)
        self.assertEqual({f["check"] for f in record["findings"]}, {"library-not-built", "no-tests-run"})

    def test_a_failed_build_with_no_rule_is_a_finding(self):
        record = self.collect_asan("[100%] Built target llcommon\n", "build=2\n", {"ok": True, "missing": []})
        self.assertEqual([f["check"] for f in record["findings"]], ["step-failed"])

    def test_collect_msvc(self):
        sarif_dir = self.dir / "b"
        sarif_dir.mkdir()
        (sarif_dir / "x.sarif").write_text(SARIF, encoding="utf-8")
        (self.dir / "m.log").write_text(r"  llcommon.vcxproj -> D:\a\v\v\b\llcommon.lib", encoding="utf-8")
        (self.dir / "ms.txt").write_text("build=0\n", encoding="utf-8")
        quiet(ca.cmd_collect, self.args(job="msvc", log=str(self.dir / "m.log"), status=str(self.dir / "ms.txt"),
                                        dir=str(sarif_dir)))
        record = json.loads((self.dir / "out" / "findings-msvc.json").read_text(encoding="utf-8"))
        self.assertEqual(len(record["findings"]), 2)
        (sarif_dir / "x.sarif").unlink()
        quiet(ca.cmd_collect, self.args(job="msvc", log=str(self.dir / "m.log"), status=str(self.dir / "ms.txt"),
                                        dir=str(sarif_dir)))
        record = json.loads((self.dir / "out" / "findings-msvc.json").read_text(encoding="utf-8"))
        self.assertEqual([f["check"] for f in record["findings"]], ["no-analysis-output"])

    def test_collect_tidy_reports_changed_files_only_and_counts_units(self):
        log = ("analysis: clang-tidy over 2 translation unit(s) of newview-x\nanalysis: only indra/newview/a.cpp\n"
               "/w/indra/newview/a.cpp:1:1: warning: m [bugprone-a]\n/w/indra/newview/other.cpp:1:1: warning: m [bugprone-a]\n"
               "analysis: tidy-exit 1 indra/newview/a.cpp\nanalysis: processed 2 of 2\n")
        (self.dir / "t.log").write_text(log, encoding="utf-8")
        quiet(ca.cmd_collect, self.args(job="clang-tidy", lib="newview-x", log=str(self.dir / "t.log")))
        record = json.loads((self.dir / "out" / "findings-clang-tidy.json").read_text(encoding="utf-8"))
        self.assertEqual(sorted((f["tool"], f["file"]) for f in record["findings"]),
                         [("build", ""), ("clang-tidy", "indra/newview/a.cpp")])
        self.assertTrue(any("2 of 2 translation unit" in note for note in record["notes"]))

    def test_tidy_that_processed_nothing_is_a_finding(self):
        (self.dir / "t.log").write_text("", encoding="utf-8")
        quiet(ca.cmd_collect, self.args(job="clang-tidy", log=str(self.dir / "t.log")))
        record = json.loads((self.dir / "out" / "findings-clang-tidy.json").read_text(encoding="utf-8"))
        self.assertEqual([f["check"] for f in record["findings"]], ["tidy-incomplete"])

    def test_baseline_is_per_library_and_job(self):
        self.write_baseline("newview-x", {"clang-tidy": {"clang-tidy|bugprone-a|indra/newview/a.cpp": 1}})
        log = ("analysis: clang-tidy over 1 translation unit(s) of newview-x\nanalysis: only indra/newview/a.cpp\n"
               "/w/indra/newview/a.cpp:1:1: warning: m [bugprone-a]\nanalysis: processed 1 of 1\n")
        (self.dir / "t.log").write_text(log, encoding="utf-8")
        quiet(ca.cmd_collect, self.args(job="clang-tidy", lib="newview-x", log=str(self.dir / "t.log")))
        record = json.loads((self.dir / "out" / "findings-clang-tidy.json").read_text(encoding="utf-8"))
        self.assertEqual((record["new"], record["gone"]), (0, 0))

    def record(self, job, groups, new, gone=0):
        out = self.dir / "collected" / f"analysis-{job}"
        out.mkdir(parents=True, exist_ok=True)
        findings = [found("t", "c", message=group) for group in groups]
        (out / f"findings-{job}.json").write_text(json.dumps(
            {"job": job, "lib": "llcommon", "new": new, "gone": gone, "groups": {g: 1 for g in groups},
             "findings": findings, "notes": []}), encoding="utf-8")

    def report(self, needs):
        args = argparse.Namespace(lib="llcommon", dir=str(self.dir / "collected"), out=str(self.dir / "report"),
                                  needs=json.dumps(needs))
        return quiet(ca.cmd_report, args)

    def test_report_without_a_baseline_is_green_and_says_report_only(self):
        self.record("asan", ["a"], None)
        self.assertEqual(self.report({"asan": "success", "tsan": "skipped"}), 0)
        text = (self.dir / "report" / "report.md").read_text(encoding="utf-8")
        self.assertIn("no baseline committed: report-only", text)
        self.assertNotIn("baseline created", text)
        proposal = json.loads((self.dir / "report" / "baseline-llcommon.json").read_text(encoding="utf-8"))
        self.assertEqual(proposal["jobs"], {"asan": {"a": 1}})

    def test_report_with_a_baseline_is_red_on_new_findings_and_notes_gone_entries(self):
        self.write_baseline("llcommon", {"asan": {"a": 1}, "tsan": {"gone": 1}})
        self.record("asan", ["a", "b"], 1)
        self.record("tsan", [], 0, gone=1)
        self.assertEqual(self.report({"asan": "success", "tsan": "success"}), 1)
        text = (self.dir / "report" / "report.md").read_text(encoding="utf-8")
        self.assertIn("1 baseline entries are gone: shrink the baseline", text)
        self.assertIn("1 new finding(s)", text)

    def test_report_is_green_when_nothing_is_new(self):
        self.write_baseline("llcommon", {"asan": {"a": 1}})
        self.record("asan", ["a"], 0)
        self.assertEqual(self.report({"asan": "success"}), 0)

    def test_failed_jobs_are_red_with_a_baseline_and_left_out_of_the_proposal(self):
        self.write_baseline("llcommon", {"asan": {"old": 1}, "tsan": {"old": 1}})
        self.record("asan", ["partial"], 0)
        self.assertEqual(self.report({"asan": "failure", "tsan": "cancelled"}), 1)
        proposal = json.loads((self.dir / "report" / "baseline-llcommon.json").read_text(encoding="utf-8"))
        self.assertEqual(proposal["jobs"], {"asan": {"old": 1}, "tsan": {"old": 1}})

    def test_a_successful_job_without_findings_is_a_missing_record(self):
        self.write_baseline("llcommon", {})
        self.assertEqual(self.report({"msvc": "success"}), 1)


if __name__ == "__main__":
    unittest.main()
