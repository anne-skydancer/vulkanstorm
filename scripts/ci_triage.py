#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The findings triage of .github/workflows/analysis.yaml (doc/clean/analysis.md, "Triage").

  rerun   run each test binary that failed or reported, alone, with extra sanitizer detail (the sanitizer jobs)
  triage  turn the real bug findings of a run into cases: origin (git blame), inherited or ours (upstream Firestorm and
          Linden Lab), source and stack context, the triggering test, grouping by root cause, the re-run output

No AI and no API key: git, the public raw file URLs of the two upstream repositories, and text parsing. Report only:
`triage` exits 0 whatever happens and commits nothing. Standard library only; the parsers are those of ci_analysis.py.
"""
import argparse
import datetime
import json
import os
import pathlib
import re
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ci_analysis as ca  # noqa: E402

ROOT = ca.ROOT
JOBS = ca.JOBS
RESET = datetime.datetime(2026, 10, 2, tzinfo=datetime.timezone.utc).timestamp()
OWNER_EMAIL = "216790674+anne-skydancer@users.noreply.github.com"
UPSTREAM_FS = "https://github.com/FirestormViewer/phoenix-firestorm.git"
UPSTREAM_LL = "https://github.com/secondlife/viewer.git"
# (name, raw URL prefix up to and including the ref)
RAW_UPSTREAMS = (
    ("firestorm-master", "https://raw.githubusercontent.com/FirestormViewer/phoenix-firestorm/master/"),
    ("firestorm-7.2.5", "https://raw.githubusercontent.com/FirestormViewer/phoenix-firestorm/Firestorm_7.2.5/"),
    ("linden-lab", "https://raw.githubusercontent.com/secondlife/viewer/HEAD/"),
)
# Cases are listed in this order: what is most certainly a bug first.
TOOL_ORDER = {"build": 0, "asan": 1, "ubsan": 2, "lsan": 3, "tsan": 4, "clang-tidy": 5, "msvc": 6}
CONTEXT_LINES = 8
FRAMES_WITH_SOURCE = 4
STACKS_PER_CASE = 3
RERUN_OUTPUT_LIMIT = 200_000
RERUN_EXTRA = {
    "asan": {"ASAN_OPTIONS": "fast_unwind_on_malloc=0:malloc_context_size=30:detect_leaks=1",
             "LSAN_OPTIONS": "fast_unwind_on_malloc=0:malloc_context_size=30",
             "UBSAN_OPTIONS": "print_stacktrace=1"},
    "tsan": {"TSAN_OPTIONS": "history_size=7:second_deadlock_stack=1"},
}


# ---------------------------------------------------------------- what is a real bug finding

# bugprone-* checks that are style or API hygiene, not bugs.
NOT_BUGS = {"bugprone-easily-swappable-parameters", "bugprone-reserved-identifier"}
# clang-analyzer checks that are dead code or performance hints.
NOT_BUG_PREFIXES = ("clang-analyzer-deadcode.", "clang-analyzer-optin.performance.")


def is_real(item):
    """A bug finding: sanitizers, test failures and build findings, clang-analyzer-*, bugprone-*, concurrency-*,
    MSVC C6xxx and C28xxx. Never modernize-*, performance-*, cppcoreguidelines-* or other style items."""
    tool, check = item["tool"], item["check"]
    if tool in ("asan", "lsan", "ubsan", "tsan", "build"):
        return True
    if tool == "clang-tidy":
        if check in NOT_BUGS or check.startswith(NOT_BUG_PREFIXES):
            return False
        return check.startswith(("clang-analyzer-", "bugprone-", "concurrency-"))
    if tool == "msvc":
        return bool(re.fullmatch(r"C(?:6|28)\d{3}", check))
    return False


# ---------------------------------------------------------------- source: function and context

KEYWORDS = {"if", "for", "while", "switch", "catch", "return", "else", "do", "sizeof", "defined", "case", "new",
            "delete", "throw", "using", "typedef"}
SIGNATURE = re.compile(r"(~?[A-Za-z_]\w*(?:<[^<>()]*>)?(?:::~?[A-Za-z_]\w*(?:<[^<>()]*>)?)*|operator\s*[^\s(]+)\s*\(")


def signature_name(text):
    for match in SIGNATURE.finditer(text):
        name = match.group(1)
        if name.split("::")[-1] not in KEYWORDS:
            return name
    return None


def enclosing_function(lines, line):
    """The name of the function that contains a 1-based line, by a text heuristic (no parser): the nearest earlier
    line at column 0 that looks like a signature, else (headers) a lightly indented one that ends like a signature
    or a brace. None when the line is not inside a function (a closing brace at column 0 is met first)."""
    for index in range(min(line, len(lines)) - 1, -1, -1):
        text = lines[index].rstrip()
        stripped = text.lstrip()
        if not stripped or stripped.startswith(("//", "/*", "*", "#")):
            continue
        if text[0] == "}":
            break
        if text[0] != " " and text[0] != "\t":
            if text[0] == "{" or text.endswith(";") or text.startswith(("namespace", "class ", "struct ", "enum ")):
                continue
            name = signature_name(text)
            if name:
                return name
            # a signature split over lines: the name may be on the previous line
    for index in range(min(line, len(lines)) - 1, -1, -1):
        text = lines[index].rstrip()
        stripped = text.lstrip()
        indent = len(text) - len(stripped)
        if not stripped or stripped.startswith(("//", "/*", "*", "#")) or indent == 0 or indent > 8:
            continue
        if stripped.endswith((";", ",")) or "=" in stripped or stripped.startswith(("return", "else")):
            continue
        if stripped.endswith((")", ") const", "{", ") const {", ") {", ") override", ") noexcept")) or ") :" in stripped:
            name = signature_name(stripped)
            if name:
                return name
    return None


def read_source(path, root=ROOT):
    try:
        return (pathlib.Path(root) / path).read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return None


def source_context(lines, line, radius=CONTEXT_LINES):
    """[(number, text, is_the_line)] around a 1-based line."""
    if not lines or line < 1 or line > len(lines):
        return []
    first, last = max(1, line - radius), min(len(lines), line + radius)
    return [(n, lines[n - 1], n == line) for n in range(first, last + 1)]


# ---------------------------------------------------------------- git: blame and origin classes

def git(root, *args, check=False, timeout=300):
    return subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=timeout, check=check)


def parse_blame(text):
    """{line: {sha, author, email, time, summary, text}} from `git blame --line-porcelain`."""
    result, current = {}, None
    for raw in text.split("\n"):
        if current is None:
            match = re.match(r"^([0-9a-f]{40}) \d+ (\d+)(?: \d+)?$", raw)
            if match:
                current = {"sha": match.group(1), "line": int(match.group(2))}
            continue
        if raw.startswith("\t"):
            current["text"] = raw[1:]
            result[current.pop("line")] = current
            current = None
        elif raw.startswith("author "):
            current["author"] = raw[7:]
        elif raw.startswith("author-mail "):
            current["email"] = raw[12:].strip("<>")
        elif raw.startswith("author-time "):
            current["time"] = int(raw[12:])
        elif raw.startswith("summary "):
            current["summary"] = raw[8:]
    return result


def classify(entry, ll_commits=frozenset(), fs_commits=frozenset()):
    """(class, basis) of a blamed line. Vulkanstorm: a commit after the 2026-10-02 reset by the repository owner's
    noreply address, or one that carries a <Vulkanstorm> marker (line or commit subject); also, after the reset, a
    commit that neither upstream history holds. Firestorm: an FS: marker on the line, or a commit that is in
    Firestorm's history and not in Linden Lab's. Otherwise Linden Lab."""
    text = entry.get("text", "")
    summary = entry.get("summary", "")
    after_reset = entry.get("time", 0) >= RESET
    if after_reset and entry.get("email") == OWNER_EMAIL:
        return "Vulkanstorm", "commit after the 2026-10-02 reset by the owner's noreply address"
    if "<Vulkanstorm" in text or "<Vulkanstorm" in summary:
        return "Vulkanstorm", "<Vulkanstorm> marker"
    if re.search(r"<FS:|\bFS:", text):
        return "Firestorm", "FS: marker on the line"
    sha = entry.get("sha", "")
    if sha in fs_commits and sha not in ll_commits:
        return "Firestorm", "commit only in the upstream Firestorm history"
    if after_reset and sha not in ll_commits and sha not in fs_commits:
        return "Vulkanstorm", "commit after the 2026-10-02 reset, in neither upstream history"
    return "Linden Lab", "everything else (the line predates the fork or is in Linden Lab's history)"


def origin_of(entry, ll_commits, fs_commits):
    if not entry:
        return None
    cls, basis = classify(entry, ll_commits, fs_commits)
    date = datetime.datetime.fromtimestamp(entry.get("time", 0), datetime.timezone.utc).strftime("%Y-%m-%d")
    return {"commit": entry["sha"][:12], "date": date, "author": entry.get("author", ""),
            "summary": entry.get("summary", "")[:160], "class": cls, "basis": basis}


class Blamer:
    """git blame per file (whole file, ignoring whitespace and moved lines: the 2024 tab and CRLF clean-up of Linden
    Lab would otherwise own every line), cached."""

    def __init__(self, root, ll_commits=frozenset(), fs_commits=frozenset(), run=None):
        self.root, self.ll, self.fs, self.cache = root, ll_commits, fs_commits, {}
        self.run = run or self._git

    def _git(self, path):
        done = git(self.root, "blame", "-w", "-M", "--line-porcelain", "HEAD", "--", path, timeout=300)
        return done.stdout if done.returncode == 0 else ""

    def origin(self, path, line):
        if path not in self.cache:
            try:
                self.cache[path] = parse_blame(self.run(path))
            except (OSError, subprocess.SubprocessError):
                self.cache[path] = {}
        return origin_of(self.cache[path].get(line), self.ll, self.fs)


def fetch_upstream(root):
    """Fetch the commit graphs (no trees, no blobs) of upstream Firestorm and Linden Lab into refs/triage/*."""
    notes = []
    for url, refs in ((UPSTREAM_FS, ["master:refs/triage/fs-master", "Firestorm_7.2.5:refs/triage/fs-7.2.5"]),
                      (UPSTREAM_LL, ["HEAD:refs/triage/ll"])):
        try:
            done = git(root, "fetch", "--no-tags", "--quiet", "--filter=tree:0", url, *refs, timeout=600)
            if done.returncode:
                notes.append(f"fetch of {url} failed: {done.stderr.strip()[:200]}")
        except (OSError, subprocess.SubprocessError) as error:
            notes.append(f"fetch of {url} failed: {error}")
    return notes


def commit_sets(root):
    """(ll_commits, fs_commits, notes): the commit ids reachable from the fetched upstream refs."""
    def listing(*refs):
        done = git(root, "rev-list", *refs, timeout=300)
        return set(done.stdout.split()) if done.returncode == 0 else None
    ll = listing("refs/triage/ll")
    fs = listing("refs/triage/fs-master", "refs/triage/fs-7.2.5")
    notes = []
    if ll is None or fs is None:
        notes.append("the upstream histories are not available: classification uses markers and dates only")
    return ll or set(), fs or set(), notes


# ---------------------------------------------------------------- upstream presence

def http_text(url, timeout=30):
    """(status, text): 200 and the text, 404, or None when the network fails."""
    request = urllib.request.Request(url, headers={"User-Agent": "vulkanstorm-analysis-triage"})
    for attempt in range(2):
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:  # noqa: S310 (fixed https hosts)
                return 200, response.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return 404, ""
        except (urllib.error.URLError, OSError, TimeoutError):
            pass
        time.sleep(1 + attempt)
    return None, ""


def squash(text):
    return re.sub(r"\s+", " ", text).strip()


def significant(text):
    """A line worth matching exactly: not just braces, not a bare keyword or comment marker."""
    text = squash(text)
    return len(text) >= 8 and re.search(r"[A-Za-z_]\w{2,}", text) is not None


class Upstreams:
    """Is the finding's code in upstream Firestorm (master, 7.2.5) and in Linden Lab's viewer? Per upstream: the file
    is missing, the exact line text is present, or only the function is."""

    def __init__(self, fetch=None, sources=RAW_UPSTREAMS):
        self.fetch, self.sources, self.cache = fetch or http_text, sources, {}

    def text(self, prefix, path):
        key = (prefix, path)
        if key not in self.cache:
            self.cache[key] = self.fetch(prefix + path)
        return self.cache[key]

    def check(self, path, line_text, function):
        result = {}
        wanted = squash(line_text) if significant(line_text) else None
        bare = function.split("::")[-1] if function else None
        for name, prefix in self.sources:
            status, body = self.text(prefix, path)
            if status is None:
                result[name] = "unavailable"
            elif status == 404:
                result[name] = "file-missing"
            elif wanted and wanted in {squash(entry) for entry in body.splitlines()}:
                result[name] = "line-present"
            elif bare and re.search(rf"\b{re.escape(bare)}\s*\(", body):
                result[name] = "function-present"
            else:
                result[name] = "absent"
        return result


def verdict(upstream):
    """inherited (the exact line is upstream), function-only, ours (absent everywhere), unknown, or n/a."""
    if not upstream:
        return "n/a"
    values = set(upstream.values())
    if "line-present" in values:
        return "inherited"
    if "function-present" in values:
        return "function-only"
    if values <= {"unavailable"} or "unavailable" in values:
        return "unknown"
    return "ours"


# ---------------------------------------------------------------- findings of the run

TEST_NAME = re.compile(r"tut::test_object<tut::(\w+)>::test<(\d+)>")
BINARY = re.compile(r"INTEGRATION_TEST_\w+")


def parse_blocks(text):
    """[(finding, block)] for the sanitizer reports of a text (the counterpart of ci_analysis.parse_sanitizer, which
    drops the report text)."""
    starts = list(ca.BLOCK_START.finditer(text))
    found = []
    for index, match in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        asan, leak, tsan, ub_file, ub_line, _col, ub_msg = match.groups()
        block = text[match.start():end]
        if ub_file is not None:
            if ca.project_file(ub_file):
                kind = re.split(r" (?:for|of|to|on|from|at|by|within|index|shift) ", ca.normalise(ub_msg), maxsplit=1)[0]
                found.append((ca.finding("ubsan", kind, ub_file, ub_line, ub_msg), block))
        elif asan:
            found.append((ca.sanitizer_finding("asan", asan, block), block))
        elif leak:
            found.append((ca.sanitizer_finding("lsan", "leak", block), block))
        else:
            found.append((ca.sanitizer_finding("tsan", ca.normalise(tsan).replace(" ", "-"), block), block))
    return found


def read(path):
    try:
        return pathlib.Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def load_job(directory, job):
    """What one job's artifact holds: its findings record, build log, sanitizer reports and re-run output."""
    base = pathlib.Path(directory) / f"analysis-{job}"
    job_data = {"job": job, "record": {}, "log": "", "san": [], "rerun": [], "found": base.is_dir()}
    if not base.is_dir():
        return job_data
    for path in base.rglob(f"findings-{job}.json"):
        try:
            job_data["record"] = json.loads(read(path))
        except ValueError:
            pass
    for path in base.rglob("build.log"):
        job_data["log"] = read(path)
    for path in sorted(base.rglob("report.*")):
        if path.parent.name == "san":
            job_data["san"].append(read(path))
    for path in base.rglob("rerun.json"):
        try:
            for entry in json.loads(read(path)):
                entry["output"] = read(path.parent / entry["file"])
                job_data["rerun"].append(entry)
        except (ValueError, KeyError):
            pass
    return job_data


def test_of(block):
    """(binary, group, test number) named in a report's text."""
    binary = BINARY.search(block)
    test = TEST_NAME.search(block)
    return (binary.group(0) if binary else "", test.group(1) if test else "", int(test.group(2)) if test else 0)


def build_log_excerpt(log, target, limit=60):
    """The lines of the build log after a test binary was linked, up to the next make line."""
    lines = log.splitlines()
    for index, line in enumerate(lines):
        if re.search(rf"Linking CXX executable \S*{re.escape(target)}\s*$", line):
            excerpt = []
            for follow in lines[index + 1:index + 1 + limit]:
                if re.match(r"^g?make(\[\d+\])?:|^\[[ \d]+%\] ", follow):
                    break
                excerpt.append(follow)
            return "\n".join(excerpt)
    return ""


def collect_items(directory):
    """(items, notes): the real bug findings of every job, each {finding, job, block, binary, group, test, count}."""
    items, notes, by_fp = [], [], {}
    for job in JOBS:
        data = load_job(directory, job)
        if not data["found"]:
            notes.append(f"{job}: no artifact")
            continue
        findings = data["record"].get("findings", [])
        raw = []
        if job in ("asan", "tsan"):
            for text in [data["log"], *data["san"]]:
                raw += parse_blocks(text)
        sanitizer_json = [f for f in findings if f["tool"] in ("asan", "lsan", "ubsan", "tsan")]
        pairs = raw or [(f, "") for f in sanitizer_json]
        pairs += [(f, "") for f in findings if f["tool"] not in ("asan", "lsan", "ubsan", "tsan")]
        for found, block in pairs:
            if not is_real(found):
                continue
            key = (job, found["fp"], found["line"] if found["tool"] in ("ubsan", "clang-tidy", "msvc") else 0)
            if key in by_fp:
                by_fp[key]["count"] += 1
                continue
            binary, group, number = test_of(block)
            if found["tool"] == "build":
                target = BINARY.search(found["message"])
                binary = binary or (target.group(0) if target else "")
            item = {"finding": found, "job": job, "block": block, "binary": binary, "group": group,
                    "test": number, "count": 1, "log": data["log"]}
            by_fp[key] = item
            items.append(item)
    return items, notes


def case_key(item, lines_of):
    found = item["finding"]
    tool = found["tool"]
    if tool in ("asan", "lsan", "tsan"):
        return (tool, found["check"], found["fp"])
    if tool == "build":
        return (tool, found["group"])
    function = enclosing_function(lines_of(found["file"]), found["line"]) if found["file"] else None
    return (tool, found["check"], found["file"], function or f"line {found['line']}")


def group_cases(items, lines_of):
    """Collapse findings with a common root into one case: leaks and sanitizer reports by their stable stack key,
    UB, tidy and MSVC findings by checker, file and enclosing function, build findings by check and target."""
    cases = {}
    for item in items:
        cases.setdefault(case_key(item, lines_of), []).append(item)
    return list(cases.values())


# ---------------------------------------------------------------- cases

def frames_of(block):
    """[{header, frames: [{name, file, line}]}] of the first stacks of a report."""
    stacks = []
    for header, frames in ca.stacks_of(block)[:STACKS_PER_CASE]:
        stacks.append({"header": header[:200], "frames": [
            {"name": name[:200], "file": ca.rel(path) if path else "", "line": int(line or 0)}
            for name, path, line in frames[:40]]})
    return stacks


def rerun_blocks(rerun, case_items):
    """The re-run report text that matches a case (same stable key), or a note."""
    binaries = {item["binary"] for item in case_items if item["binary"]}
    wanted = {item["finding"]["fp"] for item in case_items}
    tool = case_items[0]["finding"]["tool"]
    matches, seen = [], False
    for entry in rerun:
        if entry["binary"] not in binaries:
            continue
        seen = True
        if tool in ("asan", "lsan", "ubsan", "tsan"):
            for found, block in parse_blocks(entry["output"]):
                if found["fp"] in wanted:
                    matches.append("\n".join(block.rstrip().splitlines()[:150]))
                    break
        else:
            tail = entry["output"].rstrip().splitlines()[-60:]
            matches.append("\n".join(tail))
    return {"ran": seen, "output": matches[:2]}


def make_case(number, case_items, blamer, upstreams, rerun, root=ROOT):
    first = case_items[0]
    found = first["finding"]
    path, line = found["file"], found["line"]
    lines = read_source(path, root) if path else None
    function = enclosing_function(lines, line) if lines and line else None
    stacks = frames_of(first["block"]) if first["block"] else []
    if stacks and found["tool"] in ("asan", "lsan", "tsan"):
        # a sanitizer names the function itself: its top project frame
        top = next((f for f in stacks[0]["frames"] if f["file"] == path and f["line"] == line), None)
        if top:
            function = top["name"].split("(")[0].strip() or function
    # project frames of the first stack: origin and source context
    project = []
    for stack in stacks[:1]:
        project = [f for f in stack["frames"] if f["file"] and ca.project_file(f["file"])][:FRAMES_WITH_SOURCE]
    for stack in stacks:
        for frame in stack["frames"]:
            frame["source"] = []
    for frame in project:
        frame["source"] = source_context(read_source(frame["file"], root), frame["line"])
        frame["origin"] = blamer.origin(frame["file"], frame["line"])
    primary = blamer.origin(path, line) if path and line else None
    if primary is None and project:
        primary = project[0].get("origin")
    line_text = (lines[line - 1] if lines and 0 < line <= len(lines) else "")
    upstream = upstreams.check(path, line_text, function) if path and line and path.startswith("indra/") else {}
    binaries = sorted({item["binary"] for item in case_items if item["binary"]})
    tests = sorted({(item["binary"], item["group"], item["test"]) for item in case_items if item["binary"]})
    excerpt = ""
    if found["tool"] == "build":
        target = BINARY.search(found["message"] + " " + found["fp"])
        excerpt = build_log_excerpt(first["log"], target.group(0)) if target else ""
    return {
        "id": f"case-{number:03d}",
        "tool": found["tool"],
        "check": found["check"],
        "job": first["job"],
        "title": found["message"][:240],
        "file": path,
        "line": line,
        "function": function or "",
        "origin": primary,
        "inherited": {"verdict": verdict(upstream), "upstream": upstream},
        "source": source_context(lines, line) if lines and line and not project else [],
        "stacks": stacks,
        "tests": [{"binary": b, "group": g, "test": n} for b, g, n in tests],
        "binaries": binaries,
        "build_log": excerpt,
        "rerun": rerun_blocks(rerun, case_items) if binaries else {"ran": False, "output": []},
        "members": [{"tool": i["finding"]["tool"], "check": i["finding"]["check"], "file": i["finding"]["file"],
                     "line": i["finding"]["line"], "message": i["finding"]["message"][:240], "count": i["count"]}
                    for i in case_items],
    }


# ---------------------------------------------------------------- output

def counts(cases):
    by_tool, by_origin, by_inherit = {}, {}, {}
    for case in cases:
        by_tool[case["tool"]] = by_tool.get(case["tool"], 0) + 1
        cls = (case["origin"] or {}).get("class", "unknown")
        by_origin[cls] = by_origin.get(cls, 0) + 1
        by_inherit[case["inherited"]["verdict"]] = by_inherit.get(case["inherited"]["verdict"], 0) + 1
    return {"cases": len(cases), "per_tool": dict(sorted(by_tool.items())),
            "per_origin": dict(sorted(by_origin.items())), "inherited": dict(sorted(by_inherit.items()))}


def fence(text):
    return "```text\n" + text.rstrip("\n").replace("```", "'''") + "\n```"


def render_source(source):
    return fence("\n".join(f"{'>' if hit else ' '} {n:5d}  {text}" for n, text, hit in source))


def render_case(case):
    where = f"{case['file']}:{case['line']}" if case["file"] else "(no file)"
    lines = [f"## {case['id']}: {case['tool']} {case['check']}", "",
             f"- **What:** {case['title']}", f"- **Where:** `{where}`"
             + (f" in `{case['function']}`" if case["function"] else "")
             + f" (found by the {case['job']} job)"]
    origin = case["origin"]
    if origin:
        lines.append(f"- **Origin:** {origin['class']}: commit `{origin['commit']}` of {origin['date']} by "
                     f"{origin['author']}: {origin['summary']} ({origin['basis']})")
    else:
        lines.append("- **Origin:** none (no source line)")
    inherited = case["inherited"]
    detail = ", ".join(f"{name}: {state}" for name, state in inherited["upstream"].items()) or "not applicable"
    lines.append(f"- **Inherited or ours:** {inherited['verdict']} ({detail})")
    if case["tests"]:
        lines.append("- **Test:** " + "; ".join(
            f"`{t['binary']}`" + (f" group `{t['group']}` test {t['test']}" if t["group"] else "") for t in case["tests"]))
    if len(case["members"]) > 1 or case["members"][0]["count"] > 1:
        lines += ["", f"**Members ({len(case['members'])} findings, one root):**", ""]
        lines += [f"- {m['tool']} {m['check']} `{m['file']}:{m['line']}` {m['message'][:140]}"
                  + (f" (x{m['count']})" if m["count"] > 1 else "") for m in case["members"][:40]]
    for stack in case["stacks"]:
        lines += ["", f"**Stack** {stack['header']}".rstrip(), ""]
        lines.append(fence("\n".join(
            f"#{i} {f['name']}" + (f" {f['file']}:{f['line']}" if f["file"] else "")
            for i, f in enumerate(stack["frames"][:30]))))
    seen = set()
    for stack in case["stacks"][:1]:
        for frame in stack["frames"]:
            if frame.get("source") and (frame["file"], frame["line"]) not in seen:
                seen.add((frame["file"], frame["line"]))
                o = frame.get("origin")
                lines += ["", f"Source of `{frame['file']}:{frame['line']}`"
                          + (f" ({o['class']}, `{o['commit']}`, {o['date']})" if o else ""), "", render_source(frame["source"])]
    if case["source"]:
        lines += ["", "Source:", "", render_source(case["source"])]
    if case["build_log"]:
        lines += ["", "Build log after the test binary was linked:", "", fence(case["build_log"])]
    rerun = case["rerun"]
    if case["binaries"]:
        lines += ["", "**Focused re-run:**", ""]
        if not rerun["ran"]:
            lines.append("not run (no re-run output for this binary).")
        elif not rerun["output"]:
            lines.append("the re-run ran; its output holds no matching report (flaky, or only a summary).")
        else:
            lines += [fence(text) for text in rerun["output"]]
    return "\n".join(lines) + "\n"


def render_summary(lib, stats, notes):
    lines = [f"### Triage of {lib}: {stats['cases']} case(s)", ""]
    if not stats["cases"]:
        lines.append("No real bug findings: nothing to triage.")
    else:
        def row(title, table):
            return f"| {title} | " + ", ".join(f"{k} {v}" for k, v in table.items()) + " |"
        lines += ["| | cases |", "|---|---|", row("per tool", stats["per_tool"]), row("per origin", stats["per_origin"]),
                  row("inherited or ours", stats["inherited"])]
    lines += [f"- {note}" for note in notes]
    return "\n".join(lines) + "\n"


def render_markdown(lib, cases, stats, notes):
    lines = [f"# Triage of {lib}", "", render_summary(lib, stats, notes), "---", ""]
    lines += [render_case(case) for case in cases]
    if not cases:
        lines.append("No real bug findings in this run.")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------- commands

def cmd_triage(args):
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    notes, cases = [], []
    try:
        items, load_notes = collect_items(args.dir)
        notes += load_notes
        cache = {}

        def lines_of(path):
            if path not in cache:
                cache[path] = read_source(path, args.repo) or []
            return cache[path]
        ll, fs = set(), set()
        if items:
            if not args.no_fetch:
                notes += fetch_upstream(args.repo)
            ll, fs, set_notes = commit_sets(args.repo)
            notes += set_notes
        blamer, upstreams = Blamer(args.repo, ll, fs), Upstreams()
        rerun = []
        for job in ("asan", "tsan"):
            rerun += load_job(args.dir, job)["rerun"]
        grouped = sorted(group_cases(items, lines_of), key=lambda c: TOOL_ORDER.get(c[0]["finding"]["tool"], 9))
        for number, case_items in enumerate(grouped, 1):
            cases.append(make_case(number, case_items, blamer, upstreams, rerun, args.repo))
    except Exception as error:  # report only: the triage never fails the run
        notes.append(f"the triage stopped early: {type(error).__name__}: {error}")
    stats = counts(cases)
    (out / "triage.json").write_text(json.dumps({"lib": args.lib, **stats, "notes": notes, "cases": cases}, indent=1),
                                     encoding="utf-8")
    (out / "triage.md").write_text(render_markdown(args.lib, cases, stats, notes), encoding="utf-8")
    text = render_summary(args.lib, stats, notes)
    ca.summary(text)
    print(text)
    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a", encoding="utf-8") as handle:
            handle.write(f"cases={stats['cases']}\n")
    return 0


def binaries_to_rerun(log, san_texts, findings):
    """The test binaries that failed or reported: named in sanitizer reports, failed make rules, build findings."""
    names = set()
    for text in [log, *san_texts]:
        for _found, block in parse_blocks(text):
            names |= set(BINARY.findall(block))
    names |= set(ca.failed_test_targets(log))
    for found in findings:
        if found["tool"] == "build":
            names |= set(BINARY.findall(found["message"] + " " + found["fp"]))
    return sorted(names)


def rerun_env(job, base):
    env = dict(base)
    for name, extra in RERUN_EXTRA[job].items():
        current = re.sub(r"(?:^|:)log_path=[^:]*", "", env.get(name, "")).strip(":")
        env[name] = f"{current}:{extra}" if current else extra
    return env


def run_bounded(command, env, cwd, timeout):
    """(exit code, output, timed out): run in its own process group and kill the group on a timeout."""
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, cwd=cwd,
                               text=True, encoding="utf-8", errors="replace", start_new_session=True)
    try:
        output, _ = process.communicate(timeout=timeout)
        return process.returncode, output, False
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        output, _ = process.communicate()
        return None, output, True


def cmd_rerun(args):
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    build = pathlib.Path(args.dir).resolve()
    log = ca.read_text(args.log)
    san_texts = [read(p) for p in sorted(pathlib.Path(args.san_dir).glob("*")) if p.is_file()] if args.san_dir else []
    findings = []
    record = ca.read_text(args.findings)
    if record:
        findings = json.loads(record).get("findings", [])
    names = binaries_to_rerun(log, san_texts, findings)
    entries = []
    if not names:
        print("triage re-run: no failing or reporting test binary")
    env_base = dict(os.environ)
    staging = build / "sharedlibs"
    env_base["LD_LIBRARY_PATH"] = ":".join(p for p in (str(staging), str(staging / "lib"),
                                                       env_base.get("LD_LIBRARY_PATH", "")) if p)
    started_all = time.time()
    for name in names:
        if time.time() - started_all > args.budget:
            entries.append({"binary": name, "file": "", "exit": None, "timed_out": False, "seconds": 0,
                            "note": f"not run: the {args.budget} s budget of the step is spent"})
            continue
        binary = staging / "bin" / name
        if not binary.is_file():
            entries.append({"binary": name, "file": "", "exit": None, "timed_out": False, "seconds": 0,
                            "note": "binary not found"})
            continue
        started = time.time()
        cwd = build / args.lib if (build / args.lib).is_dir() else build
        code, output, timed_out = run_bounded([str(binary)], rerun_env(args.job, env_base), cwd, args.timeout)
        text = output if len(output) <= RERUN_OUTPUT_LIMIT else output[:RERUN_OUTPUT_LIMIT] + "\n[output cut]\n"
        (out / f"{name}.rerun.txt").write_text(text, encoding="utf-8")
        entries.append({"binary": name, "file": f"{name}.rerun.txt", "exit": code, "timed_out": timed_out,
                        "seconds": round(time.time() - started, 1), "note": ""})
        print(f"triage re-run: {name} exit {code} in {entries[-1]['seconds']}s" + (" (timed out)" if timed_out else ""))
        (out / "rerun.json").write_text(json.dumps(entries, indent=1), encoding="utf-8")
    (out / "rerun.json").write_text(json.dumps(entries, indent=1), encoding="utf-8")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("rerun")
    p.add_argument("--job", required=True, choices=sorted(RERUN_EXTRA))
    p.add_argument("--lib", required=True)
    p.add_argument("--dir", required=True, help="the build directory")
    p.add_argument("--log", default="")
    p.add_argument("--san-dir", default="")
    p.add_argument("--findings", default="", help="the job's findings-<job>.json")
    p.add_argument("--out", required=True)
    p.add_argument("--timeout", type=int, default=300, help="seconds per test binary")
    p.add_argument("--budget", type=int, default=1500, help="seconds after which no further binary is started")
    p.set_defaults(func=cmd_rerun)
    p = sub.add_parser("triage")
    p.add_argument("--lib", required=True)
    p.add_argument("--dir", required=True, help="the downloaded analysis-* artifacts")
    p.add_argument("--out", required=True)
    p.add_argument("--repo", default=str(ROOT))
    p.add_argument("--no-fetch", action="store_true", help="do not fetch the upstream histories")
    p.set_defaults(func=cmd_triage)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
