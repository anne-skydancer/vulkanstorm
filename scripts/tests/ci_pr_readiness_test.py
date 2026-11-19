#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline tests of scripts/ci_pr_readiness.py: the verdict, the comment and the path filter."""
import os
import sys
import unittest
import urllib.error

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import ci_pr_readiness as pr_readiness  # noqa: E402


def run(name, status="completed", conclusion="success", id_=1, workflow=1):
    return {"name": name, "status": status, "conclusion": conclusion, "id": id_,
            "details_url": f"https://github.com/o/r/actions/runs/{workflow}/job/{id_}"}


GREEN = [run("windows", id_=1), run("linux", id_=2), run("linux-lts", id_=3), run("checks", id_=4)]
CODEX_P1 = {"nodes": [{"isResolved": False, "comments": {"nodes": [{
    "author": {"login": "chatgpt-codex-connector"}, "path": "indra/newview/a.cpp", "line": 42,
    "body": "**<sub><sub>![P1 Badge](https://img/p1.svg)</sub></sub>  Guard the null face**\n\nDetails here.",
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

    def test_own_job_is_ignored(self):
        v = pr_readiness.evaluate(pr(check_runs=GREEN + [run("readiness", conclusion="cancelled", id_=9)]))
        self.assertEqual(v["state"], "success")

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
        self.assertIn("[P1 `indra/newview/a.cpp:42`](https://github.com/o/r/pull/1#discussion_r1)", text)

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


class StaleAndRunning(unittest.TestCase):
    def test_failed_in_an_older_run_green_in_a_newer_one(self):
        runs = [run("check-description", conclusion="failure", id_=1, workflow=10),
                run("check-description", id_=2, workflow=20), run("windows", id_=3, workflow=11),
                run("linux", id_=4, workflow=12), run("linux-lts", id_=5, workflow=13)]
        v = pr_readiness.evaluate(pr(check_runs=runs, workflows={"10": 1, "20": 1}))
        self.assertEqual(v["state"], "success")

    def test_newer_run_that_fails_wins_over_an_older_green(self):
        runs = GREEN + [run("check-description", id_=10, workflow=10),
                        run("check-description", conclusion="failure", id_=11, workflow=20)]
        v = pr_readiness.evaluate(pr(check_runs=runs, workflows={"10": 1, "20": 1}))
        self.assertEqual(v["description"], "check-description failed")

    def test_cancelled_older_build_run_does_not_haunt(self):
        runs = [run("windows", conclusion="cancelled", id_=1, workflow=100), run("windows", id_=2, workflow=200),
                run("linux", id_=3, workflow=3), run("linux-lts", id_=4, workflow=4)]
        v = pr_readiness.evaluate(pr(check_runs=runs, workflows={"100": 7, "200": 7}))
        self.assertEqual(v["state"], "success")

    def test_other_check_in_progress_is_pending(self):
        runs = GREEN + [run("lint", "in_progress", None, id_=20, workflow=5)]
        v = pr_readiness.evaluate(pr(check_runs=runs))
        self.assertEqual((v["state"], v["description"]), ("pending", "waiting: checks running: lint"))
        self.assertIn("other checks running | `lint`", pr_readiness.render_comment(pr(check_runs=runs), v, "d" * 40))

    def test_hostile_check_name_and_path_are_inert_in_the_comment(self):
        name = "[x](https://evil.example) @someone | y"
        runs = GREEN + [run(name, "in_progress", None, id_=20, workflow=5)]
        thread = {"path": "a](https://evil.example) [b", "line": 1, "prio": "", "url": "u",
                  "author": "a", "title": "t"}
        p = pr(check_runs=runs, threads=[thread])
        text = pr_readiness.render_comment(p, pr_readiness.evaluate(p), "d" * 40)
        self.assertIn("`[x](https://evil.example) @someone / y`", text)
        self.assertIn("[`a](https://evil.example) [b:1`](u)", text)

    def test_later_failure_replaces_ready(self):
        runs = GREEN + [run("analyze", id_=20, workflow=5)]
        self.assertEqual(pr_readiness.evaluate(pr(check_runs=runs))["state"], "success")
        runs = GREEN + [run("analyze", conclusion="failure", id_=20, workflow=5)]
        self.assertEqual(pr_readiness.evaluate(pr(check_runs=runs))["state"], "failure")


class Comment(unittest.TestCase):
    def test_title_is_inert(self):
        t = {"author": "x", "path": "a.cpp", "line": 1, "prio": "", "url": "u",
             "title": "ping @owner ![i](http://e/x.png) <b>x</b> `y`"}
        v = pr_readiness.evaluate(pr(threads=[t]))
        text = pr_readiness.render_comment(pr(threads=[t]), v, "e" * 40)
        line = next(ln for ln in text.splitlines() if ln.startswith("- ["))
        self.assertTrue(line.endswith("`ping @owner ![i](http://e/x.png) <b>x</b> 'y'`"))


class FakeGithub:
    """A canned GitHub: GETs answer from `routes` by path prefix, writes are recorded."""
    repo = "o/r"

    def __init__(self, routes, comments=(), mergeable=(True,)):
        self.routes, self.comments, self.writes = routes, list(comments), []
        self.mergeable = list(mergeable)

    def call(self, method, path, body=None):
        if method != "GET":
            self.writes.append((method, path, body))
            return {}
        if path.startswith("/repos/o/r/pulls/1"):
            m = self.mergeable.pop(0) if len(self.mergeable) > 1 else self.mergeable[0]
            return dict(self.routes["pull"], mergeable=m)
        if "/statuses" in path:
            return self.routes.get("statuses", [])
        raise AssertionError(path)

    def pages(self, path, key=None):
        if "/issues/1/comments" in path:
            return self.comments
        for prefix, value in self.routes.items():
            if path.startswith(prefix):
                return value
        raise AssertionError(path)


def pull():
    return {"state": "open", "draft": False, "mergeable": True, "base": {"ref": "vkstorm-devel"},
            "head": {"sha": "f" * 40, "repo": {"full_name": "o/r"}}}


class Plumbing(unittest.TestCase):
    def test_workflow_run_maps_to_its_pull_requests(self):
        ev = {"workflow_run": {"pull_requests": [{"number": 7}, {"number": 9}], "head_sha": "s"}}
        self.assertEqual(pr_readiness.affected_prs(FakeGithub({}), "workflow_run", ev), [7, 9])

    def test_workflow_run_falls_back_to_the_head_sha(self):
        gh = FakeGithub({"/repos/o/r/pulls?state=open": [
            {"number": 3, "head": {"sha": "abc"}}, {"number": 4, "head": {"sha": "zzz"}}]})
        ev = {"workflow_run": {"pull_requests": [], "head_sha": "abc"}}
        self.assertEqual(pr_readiness.affected_prs(gh, "workflow_run", ev), [3])

    def test_fork_review_event_run_maps_by_head_sha_to_open_prs_only(self):
        # A review on a fork PR: the unprivileged "PR review event" run has no PR list.
        gh = FakeGithub({"/repos/o/r/pulls?state=open": [
            {"number": 3, "head": {"sha": "f" * 40}}, {"number": 4, "head": {"sha": "e" * 40}}]})
        ev = {"workflow_run": {"name": "PR review event", "event": "pull_request_review",
                               "pull_requests": [], "head_sha": "f" * 40}}
        self.assertEqual(pr_readiness.affected_prs(gh, "workflow_run", ev), [3])
        ev["workflow_run"]["head_sha"] = "d" * 40  # a closed or unknown PR maps to nothing
        self.assertEqual(pr_readiness.affected_prs(gh, "workflow_run", ev), [])

    def test_push_lists_the_prs_on_that_base(self):
        gh = FakeGithub({"/repos/o/r/pulls?state=open&base=vkstorm-devel": [{"number": 5, "head": {"sha": "x"}}]})
        self.assertEqual(pr_readiness.affected_prs(gh, "push", {"ref": "refs/heads/vkstorm-devel"}), [5])

    def test_mergeable_retries_while_null(self):
        gh = FakeGithub({"pull": pull()}, mergeable=(None, None, True))
        self.assertIs(pr_readiness.mergeable_of(gh, 1, tries=5, wait=0)[1], True)
        gh = FakeGithub({"pull": pull()}, mergeable=(None,))
        self.assertIsNone(pr_readiness.mergeable_of(gh, 1, tries=3, wait=0)[1])

    @staticmethod
    def verdict(state="success"):
        return {"state": state, "description": "d", "builds": {n: ("pass", "success") for n in pr_readiness.BUILDS},
                "failed": [], "threads": [], "conflict": False, "unknown": False, "running": []}

    def publish(self, comments, status=()):
        gh = FakeGithub({"statuses": list(status)}, comments=comments)
        pr_readiness.publish(gh, 1, pr(), self.verdict(), "f" * 40, "t")
        return gh

    def test_sticky_comment_created_once_patched_then_left_alone(self):
        self.assertEqual([w[0] for w in self.publish([]).writes], ["POST", "POST"])  # status and comment
        body = pr_readiness.render_comment(pr(), self.verdict(), "f" * 40)
        mine = {"id": 5, "body": body, "user": {"login": pr_readiness.BOT}}
        same = [{"context": pr_readiness.CONTEXT, "state": "success", "description": "d"}]
        self.assertEqual(self.publish([mine], same).writes, [])  # unchanged: no status, no comment write
        stale = dict(mine, body=body + "old")
        self.assertEqual([w[0] for w in self.publish([stale], same).writes], ["PATCH"])

    def test_comment_by_someone_else_with_the_marker_is_not_taken(self):
        other = {"id": 6, "body": pr_readiness.MARKER + " quoted", "user": {"login": "reviewer"}}
        self.assertEqual(self.publish([other]).writes[-1][0], "POST")

    def test_read_only_fork_token_is_skipped_other_errors_raise(self):
        class Failing(FakeGithub):
            code = 403

            def call(self, method, path, body=None):
                if method != "GET":
                    raise urllib.error.HTTPError(path, self.code, "x", {}, None)
                return super().call(method, path, body)
        for fork, code, raises in ((True, 403, False), (False, 403, True), (True, 502, True)):
            gh = Failing({"statuses": []})
            gh.code = code
            args = (gh, 1, pr(fork=fork), self.verdict(), "f" * 40, "t")
            if raises:
                with self.assertRaises(urllib.error.HTTPError):
                    pr_readiness.publish(*args)
            else:
                pr_readiness.publish(*args)

    def test_missing_filter_on_the_base_means_builds_required(self):
        class NoFile(FakeGithub):
            def raw(self, path):
                raise urllib.error.HTTPError(path, 404, "nf", {}, None)
        self.assertEqual(pr_readiness.base_patterns(NoFile({}), "vkstorm-x", {}), [])
        self.assertTrue(pr_readiness.builds_required(["doc/a.md"], []))


class PathFilter(unittest.TestCase):
    IGNORE = pr_readiness.ignore_patterns(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", pr_readiness.BUILD_WORKFLOW))

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
