#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline tests of scripts/ci_pr_readiness.py: the verdict, the comment and the path filter."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import ci_pr_readiness as pr_readiness  # noqa: E402


def run(name, status="completed", conclusion="success", id_=1, workflow=1):
    return {"name": name, "status": status, "conclusion": conclusion, "id": id_,
            "details_url": f"https://github.com/o/r/actions/runs/{workflow}/job/{id_}"}


GREEN = [run("windows", id_=1), run("linux", id_=2), run("linux-lts", id_=3), run("checks", id_=4)]
CODEX_P1 = {"nodes": [{"isResolved": False, "comments": {"nodes": [{
    "author": {"login": "chatgpt-codex-connector"}, "path": "indra/newview/a.cpp", "line": 42,
    "body": "![P1 Badge](https://img/p1.svg) **Guard the null face**\n\nDetails here.",
    "url": "https://github.com/o/r/pull/1#discussion_r1"}]}}]}


def pr(**kw):
    base = {"base": "vkstorm-devel", "draft": False, "mergeable": True, "builds_required": True,
            "check_runs": GREEN, "threads": []}
    base.update(kw)
    return base


class Verdict(unittest.TestCase):
    def test_green(self):
        v = pr_readiness.evaluate(pr())
        self.assertEqual((v["state"], v["description"]), ("success", "ready: builds green, no open threads"))

    def test_building(self):
        runs = [run("windows"), run("linux", "in_progress", None), run("linux-lts", "queued", None)]
        v = pr_readiness.evaluate(pr(check_runs=runs))
        self.assertEqual(v["state"], "pending")
        self.assertEqual(v["description"], "waiting: builds running: linux, linux-lts")

    def test_builds_not_started(self):
        v = pr_readiness.evaluate(pr(check_runs=[]))
        self.assertEqual(v["state"], "pending")
        self.assertIn("windows, linux, linux-lts", v["description"])

    def test_failed_build(self):
        runs = [run("windows", conclusion="failure"), run("linux"), run("linux-lts", "in_progress", None)]
        v = pr_readiness.evaluate(pr(check_runs=runs))
        self.assertEqual((v["state"], v["description"]), ("failure", "windows failed"))

    def test_failed_other_check_and_rerun_wins(self):
        runs = GREEN + [run("ci-lint", conclusion="failure")]
        self.assertEqual(pr_readiness.evaluate(pr(check_runs=runs))["description"], "ci-lint failed")
        rerun = [run("windows", conclusion="failure", id_=1), run("windows", id_=2), run("linux", id_=3), run("linux-lts", id_=4)]
        self.assertEqual(pr_readiness.evaluate(pr(check_runs=rerun))["state"], "success")

    def test_skipped_viewer_job_is_not_green(self):
        # Windows' prerequisite failed, so its viewer job was skipped; another workflow's later,
        # successful `checks` job must not hide that.
        runs = [run("checks", conclusion="failure", id_=1, workflow=10), run("windows", conclusion="skipped", id_=2, workflow=10),
                run("checks", id_=3, workflow=11), run("linux", id_=4, workflow=11), run("linux-lts", id_=5, workflow=12)]
        v = pr_readiness.evaluate(pr(check_runs=runs))
        self.assertEqual((v["state"], v["description"]), ("failure", "checks, windows failed"))
        lone = pr_readiness.evaluate(pr(check_runs=[run("windows", conclusion="skipped"), run("linux", id_=2), run("linux-lts", id_=3)]))
        self.assertEqual((lone["state"], lone["description"]), ("failure", "windows failed"))

    def test_open_codex_thread(self):
        threads = [pr_readiness.thread_info(t) for t in CODEX_P1["nodes"]]
        self.assertEqual(threads[0]["prio"], "P1")
        self.assertEqual(threads[0]["title"], "Guard the null face")
        v = pr_readiness.evaluate(pr(threads=threads))
        self.assertEqual((v["state"], v["description"]), ("pending", "waiting: 1 open thread"))
        text = pr_readiness.render_comment(pr(threads=threads), v, "a" * 40)
        self.assertIn("[P1 indra/newview/a.cpp:42](https://github.com/o/r/pull/1#discussion_r1)", text)

    def test_resolved_thread_is_not_counted(self):
        # open_threads() drops resolved nodes before thread_info; the verdict sees an empty list.
        self.assertEqual(pr_readiness.evaluate(pr(threads=[]))["state"], "success")

    def test_conflict_beats_everything(self):
        v = pr_readiness.evaluate(pr(mergeable=False, check_runs=[run("windows", conclusion="failure")]))
        self.assertEqual((v["state"], v["description"]), ("failure", "conflicts with vkstorm-devel"))

    def test_unknown_mergeable(self):
        v = pr_readiness.evaluate(pr(mergeable=None))
        self.assertEqual((v["state"], v["description"]), ("pending", "waiting: mergeability unknown"))

    def test_draft_gets_a_verdict_and_a_note(self):
        p = pr(draft=True)
        v = pr_readiness.evaluate(p)
        self.assertEqual(v["state"], "success")
        self.assertIn("Draft pull request.", pr_readiness.render_comment(p, v, "b" * 40))

    def test_no_builds_needed(self):
        p = pr(builds_required=False, check_runs=[run("ci-lint")])
        v = pr_readiness.evaluate(p)
        self.assertEqual((v["state"], v["description"]), ("success", "ready: no viewer builds needed, no open threads"))
        text = pr_readiness.render_comment(p, v, "c" * 40)
        self.assertIn("viewer builds | not needed", text)
        self.assertTrue(text.startswith(pr_readiness.MARKER))

    def test_description_fits_a_status(self):
        runs = [run(f"check-{i}-with-a-long-name", conclusion="failure") for i in range(20)]
        self.assertLessEqual(len(pr_readiness.evaluate(pr(check_runs=runs))["description"]), 140)


class PathFilter(unittest.TestCase):
    IGNORE = [".github/**", ".gitignore", "doc/**", "**/*.md", "scripts/ci_*", "scripts/inworld/**",
              "scripts/tests/ci_*", "scripts/tests/test_ci_files.py"]

    def required(self, *files):
        return pr_readiness.builds_required(files, self.IGNORE)

    def test_ci_and_docs_only(self):
        self.assertFalse(self.required(".github/workflows/x.yaml", "doc/a/b.md", "README.md",
                                       "scripts/ci_pr_readiness.py", "scripts/tests/ci_pr_readiness_test.py"))

    def test_viewer_change(self):
        self.assertTrue(self.required("doc/a.md", "indra/newview/a.cpp"))
        self.assertTrue(self.required("scripts/other.py"))
        self.assertTrue(self.required("scripts/ci/sub.py"))  # `scripts/ci_*` is a name prefix, not a directory

    def test_an_empty_change_needs_no_builds(self):
        self.assertFalse(self.required())


if __name__ == "__main__":
    unittest.main()
