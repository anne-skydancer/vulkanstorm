# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Tests of scripts/ci_triage.py: which findings are real bugs, the blame parser and the origin classes, the grouping
into cases, the upstream presence check and the whole triage over a fixture artifact tree.

The fixtures in .github/analysis/fixtures are excerpts of the real artifacts of the llcommon analysis run
(Actions run 45549930615) and a `git blame --line-porcelain` of lllivefile.cpp."""
import contextlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / ".github" / "analysis" / "fixtures"
sys.path.insert(0, str(ROOT / "scripts"))
import ci_analysis as ca  # noqa: E402
import ci_triage as ct  # noqa: E402


def fixture(name):
    return (FIXTURES / name).read_text(encoding="utf-8")


def tidy(check, line=10, file="indra/llcommon/a.cpp"):
    return ca.finding("clang-tidy", check, file, line, f"{check} at {line}")


class RealFindings(unittest.TestCase):
    def test_bugs_are_real_and_style_is_not(self):
        real = ["bugprone-use-after-move", "clang-analyzer-core.NullDereference", "concurrency-mt-unsafe",
                "clang-analyzer-cplusplus.NewDeleteLeaks", "clang-analyzer-optin.cplusplus.VirtualCall"]
        style = ["modernize-use-nullptr", "performance-unnecessary-value-param",
                 "cppcoreguidelines-pro-type-member-init", "readability-braces-around-statements",
                 "bugprone-easily-swappable-parameters", "clang-analyzer-deadcode.DeadStores",
                 "clang-analyzer-optin.performance.Padding", "clang-diagnostic-error"]
        self.assertTrue(all(ct.is_real(tidy(check)) for check in real))
        self.assertFalse(any(ct.is_real(tidy(check)) for check in style))

    def test_sanitizers_build_and_msvc(self):
        for tool in ("asan", "lsan", "ubsan", "tsan", "build"):
            self.assertTrue(ct.is_real({"tool": tool, "check": "x"}))
        for check, real in (("C6001", True), ("C28182", True), ("C6340", True), ("C4100", False), ("C26110", False)):
            self.assertEqual(ct.is_real({"tool": "msvc", "check": check}), real, check)


class Source(unittest.TestCase):
    CODE = """#include <x>

int free_function(int a)
{
    int b = a;
    return b;
}

void LLFoo::bar(const std::string& s,
                int c)
{
    if (c)
    {
        use(s);
    }
}

class K
{
public:
    void inlined(int x) const
    {
        call(x);
    }
};
""".splitlines()

    def test_enclosing_function(self):
        self.assertEqual(ct.enclosing_function(self.CODE, 5), "free_function")
        self.assertEqual(ct.enclosing_function(self.CODE, 13), "LLFoo::bar")
        self.assertEqual(ct.enclosing_function(self.CODE, 22), "inlined")
        self.assertIsNone(ct.enclosing_function(self.CODE, 8))

    def test_context_is_clipped(self):
        context = ct.source_context(self.CODE, 2)
        self.assertEqual(context[0][0], 1)
        self.assertEqual([n for n, _t, hit in context if hit], [2])
        self.assertEqual(ct.source_context(self.CODE, 500), [])


class Blame(unittest.TestCase):
    def test_parse_porcelain(self):
        blame = ct.parse_blame(fixture("blame-porcelain.txt"))
        self.assertEqual(sorted(blame), [74, 75, 76, 77, 78])
        entry = blame[76]
        self.assertEqual(len(entry["sha"]), 40)
        self.assertTrue(entry["author"] and entry["summary"] and entry["time"] > 0)
        self.assertIn("impl(", entry["text"])

    def entry(self, **kwargs):
        base = {"sha": "a" * 40, "author": "X", "email": "x@example.org", "time": 1_300_000_000, "summary": "s",
                "text": "int x;"}
        base.update(kwargs)
        return base

    def test_classes(self):
        after = ct.RESET + 86400
        ll, fs = {"b" * 40, "c" * 40}, {"c" * 40, "d" * 40}

        def cls(**kwargs):
            return ct.classify(self.entry(**kwargs), ll, fs)[0]

        self.assertEqual(cls(time=after, email=ct.OWNER_EMAIL), "Vulkanstorm")
        self.assertEqual(cls(email=ct.OWNER_EMAIL), "Linden Lab")  # before the reset the address proves nothing
        self.assertEqual(cls(text="x(); // <Vulkanstorm> fix"), "Vulkanstorm")
        self.assertEqual(cls(summary="<Vulkanstorm> fix"), "Vulkanstorm")
        self.assertEqual(cls(text="    // <FS:Ansariel> change"), "Firestorm")
        self.assertEqual(cls(text="int y; // FS: note"), "Firestorm")
        self.assertEqual(cls(sha="d" * 40), "Firestorm")  # only in Firestorm's history
        self.assertEqual(cls(sha="c" * 40), "Linden Lab")  # in both: Linden Lab's
        self.assertEqual(cls(sha="b" * 40), "Linden Lab")
        self.assertEqual(cls(time=after, sha="e" * 40), "Vulkanstorm")  # after the reset, in neither history
        self.assertEqual(cls(), "Linden Lab")

    def test_blamer_uses_the_porcelain(self):
        blamer = ct.Blamer(ROOT, run=lambda _path: fixture("blame-porcelain.txt"))
        origin = blamer.origin("indra/llcommon/lllivefile.cpp", 76)
        self.assertRegex(origin["date"], r"^\d{4}-\d\d-\d\d$")
        self.assertIn(origin["class"], ("Linden Lab", "Firestorm"))
        self.assertIsNone(blamer.origin("indra/llcommon/lllivefile.cpp", 999))


class Upstream(unittest.TestCase):
    def sources(self, pages):
        def fetch(url):
            return pages.get(url, (404, ""))
        return ct.Upstreams(fetch, (("fs", "https://fs/"), ("ll", "https://ll/")))

    def test_presence(self):
        up = self.sources({"https://fs/f.cpp": (200, "void A::go(int x)\n{\n    do_it(x);   \n}\n"),
                           "https://ll/f.cpp": (200, "void A::go()\n{\n}\n")})
        both = up.check("f.cpp", "do_it(x);", "A::go")
        self.assertEqual(both, {"fs": "line-present", "ll": "function-present"})
        self.assertEqual(ct.verdict(both), "inherited")
        function_only = up.check("f.cpp", "other_thing(y);", "A::go")
        self.assertEqual(ct.verdict(function_only), "function-only")
        ours = up.check("f.cpp", "other_thing(y);", "A::stop")
        self.assertEqual(ours, {"fs": "absent", "ll": "absent"})
        self.assertEqual(ct.verdict(ours), "ours")
        missing = up.check("g.cpp", "do_it(x);", None)
        self.assertEqual(missing, {"fs": "file-missing", "ll": "file-missing"})
        self.assertEqual(ct.verdict(missing), "ours")

    def test_unavailable_and_trivial_lines(self):
        up = self.sources({"https://fs/f.cpp": (None, ""), "https://ll/f.cpp": (200, "}\n}\n")})
        result = up.check("f.cpp", "}", None)
        self.assertEqual(result, {"fs": "unavailable", "ll": "absent"})  # a brace is never matched as text
        self.assertEqual(ct.verdict(result), "unknown")
        self.assertEqual(ct.verdict({}), "n/a")


def make_artifacts(root):
    """A downloaded artifact tree of a run: asan with a UBSan report in its log and an LSan report file, tsan with a
    lock-order report, clang-tidy with bug and style findings."""
    asan = root / "analysis-asan"
    (asan / "analysis-out").mkdir(parents=True)
    (asan / "san").mkdir()
    (asan / "analysis-out" / "build.log").write_text(fixture("asan-build-excerpt.log"), encoding="utf-8")
    (asan / "san" / "report.1").write_text(fixture("lsan-leak.txt"), encoding="utf-8")
    rule = "llcommon/CMakeFiles/INTEGRATION_TEST_llprocess.dir/build.make:1"
    failed = ca.build_finding("test-failed", f"failed: {rule}", rule)
    (asan / "analysis-out" / "findings-asan.json").write_text(json.dumps({"job": "asan", "findings": [failed]}))
    tsan = root / "analysis-tsan"
    (tsan / "san").mkdir(parents=True)
    (tsan / "san" / "report.2").write_text(fixture("tsan-lock-order.txt"), encoding="utf-8")
    (tsan / "findings-tsan.json").write_text(json.dumps({"job": "tsan", "findings": []}))
    tidy_dir = root / "analysis-clang-tidy"
    tidy_dir.mkdir()
    findings = [tidy("bugprone-use-after-move", 12), tidy("bugprone-use-after-move", 13),
                tidy("bugprone-use-after-move", 40), tidy("modernize-use-nullptr", 14),
                tidy("performance-avoid-endl", 15), tidy("concurrency-mt-unsafe", 16, "indra/llcommon/b.cpp")]
    (tidy_dir / "findings-clang-tidy.json").write_text(json.dumps({"job": "clang-tidy", "findings": findings}))


class Triage(unittest.TestCase):
    def test_sanitizer_blocks_keep_their_stacks(self):
        blocks = ct.parse_blocks(fixture("asan-build-excerpt.log"))
        self.assertTrue(blocks)
        found, block = blocks[0]
        self.assertEqual(found["tool"], "ubsan")
        self.assertEqual(ct.test_of(block), ("INTEGRATION_TEST_llprocess", "llprocess_data", 16))
        leaks = ct.parse_blocks(fixture("lsan-leak.txt"))
        self.assertTrue(all(f["tool"] == "lsan" for f, _b in leaks))
        stacks = ct.frames_of(leaks[0][1])
        self.assertEqual(stacks[0]["frames"][1]["file"], "indra/llcommon/lllivefile.cpp")
        self.assertEqual(stacks[0]["frames"][1]["line"], 76)

    def test_cases_are_grouped_by_root(self):
        with tempfile.TemporaryDirectory() as work:
            work = pathlib.Path(work)
            make_artifacts(work)
            items, _notes = ct.collect_items(work)
            self.assertFalse([i for i in items if i["finding"]["check"].startswith(("modernize", "performance"))])
            body = ["x"] * 11 + ["void f()", "{", "  g();", "}", ""] + ["void h()", "{", "  k();", "}", ""] * 10
            sources = {"indra/llcommon/a.cpp": body}
            cases = ct.group_cases(items, lambda path: sources.get(path, []))
            tidy_cases = [c for c in cases if c[0]["finding"]["tool"] == "clang-tidy"]
            sizes = sorted(len(c) for c in tidy_cases)
            self.assertEqual(sum(sizes), 4)
            self.assertIn(2, sizes)  # the two use-after-move lines of the same function are one case
            tools = {c[0]["finding"]["tool"] for c in cases}
            self.assertTrue({"ubsan", "lsan", "tsan", "build", "clang-tidy"} <= tools)

    def test_dedup_keeps_every_triggering_test(self):
        """One fingerprint hit by two test binaries keeps both triggering tests, so the case lists both."""
        with tempfile.TemporaryDirectory() as work:
            work = pathlib.Path(work)
            make_artifacts(work)
            san = work / "analysis-asan" / "san"
            text = fixture("lsan-leak.txt")
            first = ct.test_of(ct.parse_blocks(text)[0][1])[0] or "INTEGRATION_TEST_lllivefile"
            other = text.replace(first, "INTEGRATION_TEST_copy") if first in text else text
            (san / "report.9").write_text(other, encoding="utf-8")
            items, _notes = ct.collect_items(work)
            leaks = [i for i in items if i["finding"]["tool"] == "lsan"]
            self.assertTrue(leaks)
            self.assertTrue(all(i["count"] >= 2 for i in leaks))
            if first in text:
                binaries = {b for i in leaks for b, _g, _n in i["triggers"]}
                self.assertIn("INTEGRATION_TEST_copy", binaries)

    def test_whole_run(self):
        with tempfile.TemporaryDirectory() as work:
            work = pathlib.Path(work)
            artifacts, repo, out = work / "artifacts", work / "repo", work / "out"
            artifacts.mkdir()
            make_artifacts(artifacts)
            (repo / "indra" / "llcommon").mkdir(parents=True)
            (repo / "indra" / "llcommon" / "lllivefile.cpp").write_text(
                "\n".join(f"line {n};" for n in range(1, 200)), encoding="utf-8")
            env = dict(os.environ, GIT_AUTHOR_NAME="Anne", GIT_COMMITTER_NAME="Anne", GIT_AUTHOR_EMAIL=ct.OWNER_EMAIL,
                       GIT_COMMITTER_EMAIL=ct.OWNER_EMAIL, GIT_AUTHOR_DATE="2027-01-01T00:00:00Z",
                       GIT_COMMITTER_DATE="2027-01-01T00:00:00Z")
            for command in (["init", "-q"], ["add", "."], ["-c", "commit.gpgsign=false", "commit", "-q", "-m", "x"]):
                subprocess.run(["git", "-C", str(repo), *command], check=True, env=env, capture_output=True)
            original = ct.http_text
            ct.http_text = lambda url, timeout=30: (404, "")
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    code = ct.main(["triage", "--lib", "llcommon", "--dir", str(artifacts), "--out", str(out),
                                    "--repo", str(repo), "--no-fetch"])
            finally:
                ct.http_text = original
            self.assertEqual(code, 0)
            data = json.loads((out / "triage.json").read_text(encoding="utf-8"))
            cases = data["cases"]
            self.assertGreaterEqual(len(cases), 6)
            leak = next(c for c in cases if c["tool"] == "lsan" and c["line"] == 76)
            self.assertEqual(leak["origin"]["class"], "Vulkanstorm")
            self.assertEqual(leak["inherited"]["verdict"], "ours")
            self.assertTrue(leak["stacks"] and leak["stacks"][0]["frames"][1]["source"])
            ub = next(c for c in cases if c["tool"] == "ubsan")
            self.assertEqual(ub["tests"][0]["binary"], "INTEGRATION_TEST_llprocess")
            text = (out / "triage.md").read_text(encoding="utf-8")
            self.assertIn("## case-001", text)
            self.assertIn("Focused re-run", text)
            self.assertNotIn("modernize", text)

    def test_nothing_to_triage(self):
        with tempfile.TemporaryDirectory() as work:
            out = pathlib.Path(work) / "out"
            with contextlib.redirect_stdout(io.StringIO()):
                code = ct.main(["triage", "--lib", "llcommon", "--dir", work, "--out", str(out), "--no-fetch"])
            self.assertEqual(code, 0)
            self.assertEqual(json.loads((out / "triage.json").read_text(encoding="utf-8"))["cases"], [])
            self.assertIn("nothing to triage", (out / "triage.md").read_text(encoding="utf-8"))


class Rerun(unittest.TestCase):
    def test_environment_replaces_log_path_and_adds_detail(self):
        env = ct.rerun_env("asan", {"ASAN_OPTIONS": "detect_leaks=1:log_path=/w/san/report:exitcode=0"})
        self.assertNotIn("log_path", env["ASAN_OPTIONS"])
        self.assertIn("malloc_context_size=30", env["ASAN_OPTIONS"])
        self.assertIn("exitcode=0", env["ASAN_OPTIONS"])
        self.assertEqual(ct.rerun_env("asan", {})["UBSAN_OPTIONS"], "print_stacktrace=1")
        self.assertIn("history_size=7:second_deadlock_stack=1", ct.rerun_env("tsan", {})["TSAN_OPTIONS"])

    def test_binaries_to_rerun(self):
        names = ct.binaries_to_rerun(fixture("asan-build-excerpt.log"), [fixture("lsan-leak.txt")], [])
        self.assertEqual(names, ["INTEGRATION_TEST_apply", "INTEGRATION_TEST_llprocess", "INTEGRATION_TEST_llprocessstdin"])

    def test_rerun_uses_the_generated_test_command(self):
        """The re-run runs CMake's own POST_BUILD command (run_build_test.py with any wrapper), not the bare binary."""
        with tempfile.TemporaryDirectory() as work:
            build = pathlib.Path(work) / "build-asan"
            rules = build / "llcorehttp" / "CMakeFiles" / "INTEGRATION_TEST_llcorehttp.dir"
            rules.mkdir(parents=True)
            command = ("cd /b/llcorehttp && /usr/bin/python3 /s/cmake/run_build_test.py -l/b/sharedlibs/lib "
                       "-DPYTHON=/usr/bin/python3 /usr/bin/python3 /s/llcorehttp/tests/test_llcorehttp_peer.py "
                       "/b/sharedlibs/bin/INTEGRATION_TEST_llcorehttp")
            (rules / "build.make").write_text("all:\n\t$(CMAKE_COMMAND) -E echo link\n\t" + command + "\n",
                                              encoding="utf-8")
            self.assertEqual(ct.test_command(build, "INTEGRATION_TEST_llcorehttp"), command)
            self.assertEqual(ct.test_command(build, "INTEGRATION_TEST_other"), "")

    @unittest.skipUnless(os.name == "posix", "runs a shell script as the test binary")
    def test_rerun_runs_a_binary_alone_and_bounds_it(self):
        with tempfile.TemporaryDirectory() as work:
            work = pathlib.Path(work)
            bin_dir = work / "build-asan" / "sharedlibs" / "bin"
            bin_dir.mkdir(parents=True)
            (work / "build-asan" / "llcommon").mkdir()
            scripts = (("INTEGRATION_TEST_quick", 'echo "ASAN=$ASAN_OPTIONS"\nexit 3\n'),
                       ("INTEGRATION_TEST_slow", "sleep 30\n"))
            for name, body in scripts:
                path = bin_dir / name
                path.write_text("#!/bin/sh\n" + body, encoding="utf-8")
                path.chmod(0o755)
            failed = [ca.build_finding("test-failed", f"failed: {name}", name)
                      for name in ("INTEGRATION_TEST_quick", "INTEGRATION_TEST_slow")]
            findings = work / "findings-asan.json"
            findings.write_text(json.dumps({"findings": failed}), encoding="utf-8")
            out = work / "rerun"
            with contextlib.redirect_stdout(io.StringIO()):
                ct.main(["rerun", "--job", "asan", "--lib", "llcommon", "--dir", str(work / "build-asan"),
                         "--findings", str(findings), "--out", str(out), "--timeout", "2"])
            entries = {e["binary"]: e for e in json.loads((out / "rerun.json").read_text(encoding="utf-8"))}
            self.assertEqual(entries["INTEGRATION_TEST_quick"]["exit"], 3)
            self.assertIn("malloc_context_size=30", (out / "INTEGRATION_TEST_quick.rerun.txt").read_text(encoding="utf-8"))
            self.assertTrue(entries["INTEGRATION_TEST_slow"]["timed_out"])


if __name__ == "__main__":
    unittest.main()
