#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""PR readiness: one commit status and one sticky comment per open pull request.

A PR is ready when its viewer builds (windows, linux, linux-lts) are green and no review
thread is open. A PR whose changed files are all ignored by the build workflows builds no
viewer, so it needs no builds. The verdict (`evaluate`) and the comment (`render_comment`)
are pure functions, tested offline by scripts/tests/ci_pr_readiness_test.py.

Run by .github/workflows/pr-readiness.yaml, which gives GITHUB_TOKEN, GITHUB_REPOSITORY,
GITHUB_EVENT_NAME, GITHUB_EVENT_PATH and optionally GITHUB_SERVER_URL / GITHUB_RUN_ID.
Standard library only (PyYAML for the build workflow's path filter). The PR's code is
never run: the PR is read through the API.
"""
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

CONTEXT = "PR readiness"
MARKER = "<!-- pr-readiness -->"
BUILDS = ("windows", "linux", "linux-lts")
BUILD_WORKFLOW = ".github/workflows/build-windows.yaml"  # test_ci_files.py keeps all three equal
BAD_CONCLUSIONS = {"failure", "timed_out", "cancelled", "action_required", "startup_failure", "stale"}
OK_CONCLUSIONS = {"success", "skipped", "neutral"}
STATUS_MAX = 140


# ---------------------------------------------------------------- pure logic

def glob_to_regex(pattern):
    """A GitHub path filter glob as a regex: `*` stays inside a directory, `**` crosses them."""
    out, i = [], 0
    while i < len(pattern):
        if pattern.startswith("**/", i):
            out.append("(?:.*/)?")
            i += 3
        elif pattern.startswith("**", i):
            out.append(".*")
            i += 2
        elif pattern[i] == "*":
            out.append("[^/]*")
            i += 1
        elif pattern[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(pattern[i]))
            i += 1
    return re.compile("".join(out) + r"\Z")


def builds_required(changed_files, ignore_patterns):
    """True when any changed file escapes the build workflows' paths-ignore (so they run)."""
    regexes = [glob_to_regex(p) for p in ignore_patterns]
    return any(not any(r.match(f) for r in regexes) for f in changed_files)


def thread_info(thread):
    """A review thread (GraphQL node) as {author, path, line, prio, title, url}."""
    comment = ((thread.get("comments") or {}).get("nodes") or [{}])[0] or {}
    body = " ".join((comment.get("body") or "").split())
    title = body.split("**")[1] if body.count("**") >= 2 else body[:120]
    badge = re.search(r"\bP([0-3]) Badge", body)
    return {
        "author": (comment.get("author") or {}).get("login", "?"),
        "path": comment.get("path") or "",
        "line": comment.get("line") or comment.get("originalLine") or 0,
        "prio": f"P{badge.group(1)}" if badge else "",
        "title": title.strip()[:120],
        "url": comment.get("url") or "",
    }


def latest_runs(check_runs):
    """The latest check run of each name."""
    latest = {}
    for run in check_runs:
        if run["name"] not in latest or run.get("id", 0) > latest[run["name"]].get("id", 0):
            latest[run["name"]] = run
    return latest


def build_states(check_runs):
    """{build: (state, detail)} from the head's check runs."""
    latest = latest_runs(check_runs)
    states = {}
    for name in BUILDS:
        run = latest.get(name)
        if run is None:
            states[name] = ("missing", "not started")
        elif run.get("status") != "completed":
            states[name] = ("running", run.get("status") or "running")
        elif run.get("conclusion") in OK_CONCLUSIONS:
            states[name] = ("pass", run.get("conclusion"))
        else:
            states[name] = ("fail", run.get("conclusion") or "unknown")
    return states


def failed_checks(check_runs):
    """Names of the completed check runs, latest per name, that failed (builds or any other)."""
    return sorted(n for n, r in latest_runs(check_runs).items()
                  if r.get("status") == "completed" and r.get("conclusion") in BAD_CONCLUSIONS)


def evaluate(pr):
    """Verdict for one PR.

    pr: {base, draft, mergeable (True/False/None), builds_required, check_runs, threads}
    Returns {state, description, builds, failed, threads, conflict, unknown}.
    """
    runs = pr.get("check_runs") or []
    builds = build_states(runs) if pr["builds_required"] else {}
    failed = failed_checks(runs)
    threads = pr.get("threads") or []
    conflict = pr.get("mergeable") is False
    unknown = pr.get("mergeable") is None
    waiting_builds = [n for n, (s, _) in builds.items() if s in ("running", "missing")]
    if conflict:
        state, text = "failure", f"conflicts with {pr['base']}"
    elif failed:
        state, text = "failure", f"{', '.join(failed)} failed"
    elif waiting_builds or threads or unknown:
        parts = []
        if waiting_builds:
            parts.append(f"builds running: {', '.join(waiting_builds)}")
        if threads:
            parts.append(f"{len(threads)} open thread{'s' if len(threads) != 1 else ''}")
        if unknown:
            parts.append("mergeability unknown")
        state, text = "pending", "waiting: " + " / ".join(parts)
    elif pr["builds_required"]:
        state, text = "success", "ready: builds green, no open threads"
    else:
        state, text = "success", "ready: no viewer builds needed, no open threads"
    return {"state": state, "description": text[:STATUS_MAX], "builds": builds, "failed": failed,
            "threads": threads, "conflict": conflict, "unknown": unknown}


def render_comment(pr, verdict, head_sha):
    """The sticky comment body. No timestamps, so an unchanged verdict is an unchanged body."""
    label = {"success": "ready", "pending": "waiting", "failure": "not ready"}[verdict["state"]]
    lines = [MARKER, f"### PR readiness: {label}", "", f"{verdict['description']} (`{head_sha[:10]}`)", ""]
    if pr.get("draft"):
        lines += ["Draft pull request.", ""]
    lines += ["| Check | Result |", "| --- | --- |"]
    if pr["builds_required"]:
        for name in BUILDS:
            state, detail = verdict["builds"][name]
            lines.append(f"| build {name} | {state}{'' if state == 'pass' else ' (' + detail + ')'} |")
    else:
        lines.append("| viewer builds | not needed: only CI files and documentation changed |")
    other = [n for n in verdict["failed"] if n not in BUILDS]
    if other:
        lines.append(f"| other failed checks | {', '.join(other)} |")
    if verdict["conflict"]:
        lines.append(f"| merge | conflicts with {pr['base']} |")
    elif verdict["unknown"]:
        lines.append("| merge | not computed yet |")
    else:
        lines.append("| merge | no conflicts |")
    lines.append(f"| open review threads | {len(verdict['threads'])} |")
    if verdict["threads"]:
        lines += ["", "Open threads:", ""]
        for t in verdict["threads"]:
            where = f"{t['path']}:{t['line']}" if t["path"] else "(conversation)"
            badge = f"{t['prio']} " if t["prio"] else ""
            lines.append(f"- [{badge}{where}]({t['url']}) {t['author']}: {t['title']}")
    lines += ["", "Ready means CI is green and no review thread is open "
                  "(doc/release_channels.md, PR readiness)."]
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------- GitHub access

class Github:
    def __init__(self, repo, token, server="https://api.github.com"):
        self.repo, self.token, self.server = repo, token, server

    def call(self, method, path, body=None):
        url = path if path.startswith("http") else f"{self.server}{path}"
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method, headers={
            "Authorization": f"Bearer {self.token}", "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "pr-readiness"})
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read()
            return json.loads(raw) if raw else None

    def pages(self, path, key=None):
        """All pages of a list endpoint (`key` for the {key: [...]} shape)."""
        out, page = [], 1
        sep = "&" if "?" in path else "?"
        while True:
            chunk = self.call("GET", f"{path}{sep}per_page=100&page={page}")
            items = chunk[key] if key else chunk
            out += items
            if len(items) < 100:
                return out
            page += 1

    def graphql(self, query, variables):
        res = self.call("POST", "/graphql", {"query": query, "variables": variables})
        if res.get("errors"):
            raise RuntimeError(f"graphql: {res['errors']}")
        return res["data"]


THREADS_QUERY = """query($o:String!,$n:String!,$num:Int!,$after:String){repository(owner:$o,name:$n){
pullRequest(number:$num){reviewThreads(first:100,after:$after){pageInfo{hasNextPage endCursor}
nodes{isResolved comments(first:1){nodes{author{login} path line originalLine body url}}}}}}}"""


def open_threads(gh, number):
    owner, name = gh.repo.split("/")
    after, found = None, []
    while True:
        data = gh.graphql(THREADS_QUERY, {"o": owner, "n": name, "num": number, "after": after})
        conn = data["repository"]["pullRequest"]["reviewThreads"]
        found += [thread_info(t) for t in conn["nodes"] if not t["isResolved"]]
        if not conn["pageInfo"]["hasNextPage"]:
            return found
        after = conn["pageInfo"]["endCursor"]


def ignore_patterns(path=BUILD_WORKFLOW):
    import yaml  # the workflow installs it, as ci-lint does
    with open(path, encoding="utf-8") as f:
        doc = yaml.safe_load(f)
    triggers = doc.get("on", doc.get(True))  # YAML reads a bare `on` as True
    return list(triggers["pull_request"]["paths-ignore"])


def mergeable_of(gh, number, tries=6, wait=5):
    """(pull, mergeable): GitHub computes mergeability lazily, so retry while it says null."""
    for attempt in range(tries):
        pull = gh.call("GET", f"/repos/{gh.repo}/pulls/{number}")
        if pull["mergeable"] is not None or pull["state"] != "open":
            return pull, pull["mergeable"]
        if attempt + 1 < tries:
            time.sleep(wait)
    return pull, None


def evaluate_pr(gh, number, patterns):
    pull, mergeable = mergeable_of(gh, number)
    if pull["state"] != "open":
        return None
    head = pull["head"]["sha"]
    changed = gh.pages(f"/repos/{gh.repo}/pulls/{number}/files")
    # A renamed file also leaves its old path: count both, as the path filter does.
    files = [f["filename"] for f in changed] + [f["previous_filename"] for f in changed if f.get("previous_filename")]
    runs = gh.pages(f"/repos/{gh.repo}/commits/{head}/check-runs", key="check_runs")
    pr = {"base": pull["base"]["ref"], "draft": pull["draft"], "mergeable": mergeable,
          "builds_required": builds_required(files, patterns), "check_runs": runs,
          "threads": open_threads(gh, number)}
    return pr, evaluate(pr), head


def publish(gh, number, pr, verdict, head, target_url):
    try:
        gh.call("POST", f"/repos/{gh.repo}/statuses/{head}", {
            "state": verdict["state"], "description": verdict["description"],
            "context": CONTEXT, "target_url": target_url})
    except urllib.error.HTTPError as err:  # a fork PR's token is read-only
        print(f"#{number}: status not written (HTTP {err.code})")
    body = render_comment(pr, verdict, head)
    try:
        comments = gh.pages(f"/repos/{gh.repo}/issues/{number}/comments")
        mine = next((c for c in comments if MARKER in (c.get("body") or "")), None)
        if mine is None:
            gh.call("POST", f"/repos/{gh.repo}/issues/{number}/comments", {"body": body})
        elif mine["body"] != body:
            gh.call("PATCH", f"/repos/{gh.repo}/issues/comments/{mine['id']}", {"body": body})
    except urllib.error.HTTPError as err:
        print(f"#{number}: comment not written (HTTP {err.code})")


def affected_prs(gh, event_name, event):
    """Open PR numbers an event can change the readiness of."""
    if event_name.startswith("pull_request"):
        return [event["pull_request"]["number"]]
    if event_name == "workflow_run":
        run = event["workflow_run"]
        numbers = [p["number"] for p in run.get("pull_requests") or []]
        if numbers:
            return numbers
        # Fork and some same-repo runs carry no PR list: match the head commit.
        return [p["number"] for p in gh.pages(f"/repos/{gh.repo}/pulls?state=open")
                if p["head"]["sha"] == run["head_sha"]]
    if event_name == "push":
        base = event["ref"].removeprefix("refs/heads/")
        return [p["number"] for p in gh.pages(f"/repos/{gh.repo}/pulls?state=open&base={base}")]
    return []


def main():
    repo, token = os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_TOKEN"]
    gh = Github(repo, token, os.environ.get("GITHUB_API_URL", "https://api.github.com"))
    with open(os.environ["GITHUB_EVENT_PATH"], encoding="utf-8") as f:
        event = json.load(f)
    event_name = os.environ["GITHUB_EVENT_NAME"]
    server, run_id = os.environ.get("GITHUB_SERVER_URL", "https://github.com"), os.environ.get("GITHUB_RUN_ID", "")
    target = f"{server}/{repo}/actions/runs/{run_id}" if run_id else f"{server}/{repo}"
    patterns = ignore_patterns()
    numbers = sorted(set(affected_prs(gh, event_name, event)))
    print(f"{event_name}: pull requests {numbers or 'none'}")
    for number in numbers:
        result = evaluate_pr(gh, number, patterns)
        if result is None:
            continue
        pr, verdict, head = result
        print(f"#{number} @{head[:10]}: {verdict['state']}: {verdict['description']}")
        publish(gh, number, pr, verdict, head, target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
