#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The helper of .github/workflows/analysis.yaml (doc/clean/analysis.md).

  plan          which library a branch analyses, what its tests are, which of them TSan runs
  tidy          run clang-tidy over the library's translation units
  canary        prove that a sanitizer build reports (a deliberately faulty program, compiled with the build's flags)
  failed-tests  the test targets whose make rule failed in a build log (the workflow retries each once)
  collect       turn one job's raw output into findings (JSON, text, job summary)
  report        merge the jobs' findings, compare them with the committed baseline, decide red or green

Standard library only. The gate compares counts per (tool, checker, file), never fingerprints with line numbers or
message text, so a rename, a move or an edit above a finding does not make it new. The detailed findings stay in the
report.
"""
import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile
from collections import Counter

ROOT = pathlib.Path(__file__).resolve().parents[1]
BASELINE_DIR = ROOT / ".github" / "analysis" / "baseline"
TSAN_DIR = ROOT / ".github" / "analysis" / "tsan"
TIDY_CONFIG = ROOT / ".github" / "analysis" / "clang-tidy.yaml"
SELF_TEST_LIB = "llcommon"
NEWVIEW_BASE = os.environ.get("NEWVIEW_BASE", "origin/release-clean")
JOBS = ("clang-tidy", "asan", "tsan", "msvc")
# A pull request from a branch that is not clean/<lib> runs the self-test only when it changes one of these.
ANALYSIS_FILES = (
    ".github/workflows/analysis.yaml",
    ".github/actions/analysis-setup/",
    ".github/analysis/",
    "scripts/ci_analysis.py",
    "scripts/ci_triage.py",
    "scripts/tests/ci_analysis_test.py",
    "scripts/tests/ci_triage_test.py",
    "scripts/tests/ci_analysis_setup_test.py",
)
CXX_SUFFIXES = (".cpp", ".cc", ".c", ".h", ".hpp", ".inl")


# ---------------------------------------------------------------- plan

# Directories with an add_library that the analysis configure does not build: the CMake helper project, the
# proprietary KDU and physics-extension libraries (not shipped), and neo-j2k (the configure turns it off).
NOT_BUILT = {"cmake", "llkdu", "llphysicsextensionsos", "llimagej2cneo"}


def libraries(root=ROOT):
    """The libraries a clean/<lib> branch may name: indra/<dir> whose CMakeLists.txt has add_library(<dir>)."""
    found = []
    for path in sorted((root / "indra").glob("*/CMakeLists.txt")):
        name = path.parent.name
        if name in NOT_BUILT:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if re.search(rf"^\s*add_library\s*\(\s*{re.escape(name)}\b", text, re.M | re.I):
            found.append(name)
    return found


def integration_tests(lib, root=ROOT):
    """The names of the library's active LL_ADD_INTEGRATION_TEST lines."""
    path = root / "indra" / lib / "CMakeLists.txt"
    names = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"\s*LL_ADD_INTEGRATION_TEST\(\s*(\w+)", line)
        if match:
            names.append(match.group(1))
    return names


def tsan_deny(lib, root=ROOT):
    """The tests TSan must not run (.github/analysis/tsan/<lib>-deny.txt, one name per line), or None without a file."""
    path = root / ".github" / "analysis" / "tsan" / f"{lib}-deny.txt"
    if not path.is_file():
        return None
    names = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            names.append(line)
    return names


def touches_analysis(changed):
    return any(name == prefix or (prefix.endswith("/") and name.startswith(prefix))
               for name in changed for prefix in ANALYSIS_FILES)


def resolve(head_ref, event, lib_input="", root=ROOT, changed=None, has_pull_request=False):
    """What a run analyses. Returns {run, error, lib, scope, reason, tests, tsan_tests, tsan_reason, deny_unmatched}.

    error is a message when a clean/ branch names nothing analysable (the plan job fails with it); changed is the
    list of files a pull request changes (None: unknown)."""
    plan = {"run": False, "error": "", "lib": "", "scope": "", "reason": "", "tests": [], "tsan_tests": [],
            "tsan_reason": "", "deny_unmatched": []}
    if event == "workflow_dispatch":
        name = (lib_input or SELF_TEST_LIB).strip()
        head_ref = name if name.startswith("clean/") else f"clean/{name}"
    elif not head_ref.startswith("clean/"):
        if changed is not None and not touches_analysis(changed):
            plan["reason"] = (f"{head_ref} is not a clean/<lib> branch and changes no analysis file: "
                              "nothing to analyse")
            return plan
        head_ref = f"clean/{SELF_TEST_LIB}"
        plan["reason"] = f"not a clean/<lib> branch; it changes analysis files: self-test on {SELF_TEST_LIB}"
    name = head_ref[len("clean/"):]
    known = libraries(root)
    if re.fullmatch(r"newview-[a-z0-9][a-z0-9-]*", name):
        plan.update(scope="newview", lib=name)
        plan["reason"] = plan["reason"] or f"{head_ref}: clang-tidy over the newview sources the branch changed"
    elif name in known:
        plan.update(scope="lib", lib=name)
        plan["reason"] = plan["reason"] or f"{head_ref}: library {name}"
    else:
        plan["error"] = (f"{head_ref} names no analysable target: a cleanup branch is clean/<lib> with <lib> one of "
                         f"{', '.join(known)}, or clean/newview-<area>")
        plan["reason"] = plan["error"]
        return plan
    if event == "push" and has_pull_request:
        plan["reason"] += "; skipped on push, an open pull request analyses this branch"
        return plan
    plan["run"] = True
    if plan["scope"] == "lib":
        plan["tests"] = integration_tests(name, root)
        deny = tsan_deny(name, root)
        if not plan["tests"]:
            plan["tsan_reason"] = f"no TSan job: {name} has no integration tests"
        else:
            denied = deny or []
            plan["deny_unmatched"] = [t for t in denied if t not in plan["tests"]]
            plan["tsan_tests"] = [t for t in plan["tests"] if t not in denied]
            if deny is None:
                plan["tsan_reason"] = (f"TSan runs all {len(plan['tests'])} tests: no deny list "
                                       f".github/analysis/tsan/{name}-deny.txt (add one if a test uses coroutines)")
            elif not plan["tsan_tests"]:
                plan["tsan_reason"] = f"no TSan job: all {len(plan['tests'])} tests are on the deny list"
            else:
                plan["tsan_reason"] = (f"TSan runs {len(plan['tsan_tests'])} of {len(plan['tests'])} tests; denied "
                                       f"({len(denied)}): {', '.join(denied)}")
    else:
        plan["tsan_reason"] = "no TSan job: newview analysis is clang-tidy only"
    return plan


def write_outputs(values):
    path = os.environ.get("GITHUB_OUTPUT")
    lines = [f"{key}={value}" for key, value in values.items()]
    if path:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")
    print("\n".join(lines))


def git_lines(*command):
    done = subprocess.run(["git", *command], cwd=ROOT, capture_output=True, text=True, check=False)
    return [line for line in done.stdout.splitlines() if line] if done.returncode == 0 else None


def open_pull_request(branch):
    """True when an open pull request into release-clean has this head branch (gh needs GH_TOKEN and GH_REPO)."""
    done = subprocess.run(["gh", "pr", "list", "--head", branch, "--base", "release-clean", "--state", "open",
                           "--json", "number", "--jq", "length"], capture_output=True, text=True, check=False)
    return done.returncode == 0 and done.stdout.strip() not in ("", "0")


def cmd_plan(args):
    head = os.environ.get("HEAD_REF", "")
    event = os.environ.get("EVENT", "")
    changed = git_lines("diff", "--name-only", "HEAD^1", "HEAD") if event == "pull_request" else None
    has_pr = event == "push" and open_pull_request(head)
    plan = resolve(head, event, os.environ.get("LIB_INPUT", ""), changed=changed, has_pull_request=has_pr)
    write_outputs({
        "run": str(plan["run"]).lower(),
        "lib": plan["lib"],
        "scope": plan["scope"],
        "targets": " ".join(f"INTEGRATION_TEST_{t}" for t in plan["tests"]),
        "tsan_targets": " ".join(f"INTEGRATION_TEST_{t}" for t in plan["tsan_tests"]),
    })
    lines = ["### Analysis plan", "", plan["reason"], ""]
    if plan["scope"] == "lib":
        lines += [plan["tsan_reason"], ""]
    for name in plan["deny_unmatched"]:
        print(f"::warning::the TSan deny list of {plan['lib']} names {name}, which is no test of the library")
        lines.append(f"- warning: the deny list names `{name}`, which is no test of the library")
    summary("\n".join(lines))
    if plan["error"]:
        print(f"::error::{plan['error']}")
        return 1
    return 0


# ---------------------------------------------------------------- findings

def summary(text):
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if path:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(text + "\n")


# A workspace prefix (D:\a\v\v\, /home/runner/work/v/v/, /__w/v/v/) in front of indra\ ; removed from messages.
WORKSPACE = re.compile(r"(?i)(?:file:///)?(?:[a-z]:)?[\\/](?:[^\s'\"\[\]()<>:*?|]*?[\\/])?(?=indra[\\/])")
INDRA = re.compile(r"(?:^|[\\/])(indra[\\/].*)$")


def strip_workspace(text):
    """The text with workspace prefixes removed and indra\\... paths written with slashes."""
    text = WORKSPACE.sub("", text)
    return re.sub(r"indra[\\/][\w.\\/+-]*", lambda m: m.group(0).replace("\\", "/"), text)


def rel(path):
    """A path inside the checkout as indra/..., or the path with forward slashes (relative URIs included)."""
    path = path.replace("\\", "/")
    if path.startswith("file:///"):
        path = path[len("file:///"):]
    match = INDRA.search(path)
    return match.group(1) if match else path


def project_file(path):
    return rel(path).startswith("indra/")


def normalise(text):
    text = strip_workspace(text)
    text = re.sub(r"0x[0-9a-fA-F]+", "N", text)
    return re.sub(r"\d+", "N", text).strip()


def finding(tool, check, file, line, message, key="", group=""):
    """key: the stable fingerprint part (default the normalised message); group: the gate's key."""
    file = rel(file) if file else ""
    message = strip_workspace(message).strip()
    return {"tool": tool, "check": check, "file": file, "line": int(line or 0), "message": message,
            "fp": "|".join([tool, check, file, key or normalise(message)]),
            "group": group or "|".join([tool, check, file])}


def family(item):
    """The checker family: bugprone, modernize, ... for clang-tidy; the tool for the rest."""
    return item["check"].split("-")[0] if item["tool"] == "clang-tidy" else item["tool"]


TIDY_LINE = re.compile(r"^(\S.*?):(\d+):(\d+): (warning|error): (.*) \[([\w.,-]+)\]$")


def parse_tidy(text, lib=None):
    """clang-tidy findings of the library's sources (lib: a directory name under indra/, or None for all)."""
    seen, found = set(), []
    for raw in text.splitlines():
        match = TIDY_LINE.match(raw.strip())
        if not match:
            continue
        path, line, col, _level, message, checks = match.groups()
        if not project_file(path):
            continue
        if lib and not rel(path).startswith(f"indra/{lib}/"):
            continue
        key = (rel(path), line, col, checks)
        if key in seen:
            continue
        seen.add(key)
        found.append(finding("clang-tidy", checks.split(",")[0], path, line, message))
    return found


def parse_tidy_markers(text):
    """The markers cmd_tidy writes: translation units planned and processed, non-zero exits, files to report."""
    planned = processed = None
    exits, only = [], set()
    for raw in text.splitlines():
        match = re.match(r"analysis: clang-tidy over (\d+) translation unit", raw)
        if match:
            planned = int(match.group(1))
        match = re.match(r"analysis: processed (\d+) of (\d+)", raw)
        if match:
            processed = int(match.group(1))
        match = re.match(r"analysis: tidy-exit (-?\d+) (\S+)", raw)
        if match:
            exits.append((int(match.group(1)), match.group(2)))
        match = re.match(r"analysis: only (\S+)", raw)
        if match:
            only.add(match.group(1))
    return planned, processed, exits, only


FRAME_FILE = re.compile(r"^\s*#\d+ (?:0x[0-9a-fA-F]+ in )?(.+?) (\S*[\\/]\S+?):(\d+)(?::\d+)?(?: \(.*)?$")
FRAME_NAME = re.compile(r"^\s*#\d+ (?:0x[0-9a-fA-F]+ in )?(.+?)(?: \([^()]*\+0x[0-9a-fA-F]+\).*)?$")
BLOCK_START = re.compile(
    r"ERROR: AddressSanitizer: ([\w-]+)"
    r"|(Direct|Indirect) leak of"
    r"|WARNING: ThreadSanitizer: ([A-Za-z -]+?)\s*(?:\(|$)"
    r"|^(\S[^\n]*?):(\d+):(\d+): runtime error: ([^\n]*)", re.M)
# TSan stacks that say where a thread or an allocation came from: not part of the race's identity.
CONTEXT_STACK = re.compile(r"created by|allocated by|Location is|SUMMARY")


def stacks_of(block):
    """[(header, [(name, file or '', line)])]: the stacks of a sanitizer report, in order."""
    stacks, header, current = [], "", None
    for raw in block.splitlines():
        match = FRAME_FILE.match(raw)
        if match:
            frame = (match.group(1), match.group(2), match.group(3))
        elif re.match(r"^\s*#\d+ ", raw):
            named = FRAME_NAME.match(raw)
            frame = (named.group(1) if named else raw.strip(), "", "0")
        else:
            frame = None
        if frame:
            if current is None:
                current = (header, [])
                stacks.append(current)
            current[1].append(frame)
        else:
            current = None
            if raw.strip():
                header = raw.strip()
    return stacks


def sanitizer_finding(tool, kind, block):
    """One finding. Its key is the sorted set of the top project frame of each access stack (ASan and LSan: the
    first stack only), so a report counts once however often it fires and a line edit does not change it. A report
    with no project frame keeps its kind and top frame."""
    stacks = stacks_of(block)
    picked = stacks[:1] if tool in ("asan", "lsan") else [s for s in stacks if not CONTEXT_STACK.search(s[0])]
    tops, first_line = set(), 0
    for _header, frames in picked:
        top = next((f for f in frames if f[1] and project_file(f[1])), None)
        if top:
            tops.add((normalise(top[0]), rel(top[1])))
            first_line = first_line or int(top[2])
    if tops:
        ordered = sorted(tops)
        key = ">".join(f"{name}@{file}" for name, file in ordered)
        file = ordered[0][1]
        message = f"{kind} in {ordered[0][0]}"
        return finding(tool, kind, file, first_line, message, key)
    top = next((f for _h, frames in stacks for f in frames), None)
    name = normalise(top[0]) if top else "(no stack)"
    return finding(tool, kind, "", 0, f"{kind}, no project frame, top frame {name}", f"(no project frame) {name}",
                   f"{tool}|{kind}|(no project frame) {name}")


def parse_sanitizer(text):
    """Findings of ASan, LSan, UBSan and TSan report text (a san/ file or a build or test log)."""
    starts = list(BLOCK_START.finditer(text))
    found = []
    for index, match in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        asan, leak, tsan, ub_file, ub_line, _col, ub_msg = match.groups()
        if ub_file is not None:
            if project_file(ub_file):
                kind = re.split(r" (?:for|of|to|on|from|at|by|within|index|shift) ", normalise(ub_msg), maxsplit=1)[0]
                found.append(finding("ubsan", kind, ub_file, ub_line, ub_msg))
            continue
        block = text[match.start():end]
        if asan:
            found.append(sanitizer_finding("asan", asan, block))
        elif leak:
            found.append(sanitizer_finding("lsan", "leak", block))
        else:
            found.append(sanitizer_finding("tsan", normalise(tsan).replace(" ", "-"), block))
    return found


# ---------------------------------------------------------------- build log, tests

MAKE_FAIL = re.compile(r"^g?make(?:\[\d+\])?: \*\*\* \[[^\]]*build\.make:\d+: ([^\]]+)\] Error \d+")
GENERIC_ERRORS = (
    (re.compile(r"No rule to make target '?([^'\n]+)"), "no-rule"),
    (re.compile(r"^CMake Error[^\n]*", re.M), "cmake-error"),
    (re.compile(r"^\S[^\n]*?: (?:fatal )?error: [^\n]*", re.M), "compile-error"),
    (re.compile(r"^[^\n]*?\b(?:fatal )?error (?:C|LNK|MSB|D)\d+[^\n]*", re.M), "msvc-error"),
)


def build_finding(check, message, name):
    return finding("build", check, "", 0, message, name, f"build|{check}|{normalise(name)[:120]}")


def parse_build_log(text):
    """Leaf make rules that failed: a compile or link error, or a test that exited non-zero."""
    found, seen = [], set()
    for line in text.splitlines():
        match = MAKE_FAIL.match(line)
        if match and match.group(1) not in seen:
            seen.add(match.group(1))
            rule = match.group(1)
            found.append(build_finding("test-failed" if "INTEGRATION_TEST_" in rule else "build-failed",
                                       f"failed: {rule}", rule))
    return found


def parse_build_errors(text, status):
    """Why a step failed: the leaf rules, else the log's error lines, else (status non-zero) the bare status."""
    found = parse_build_log(text)
    if found or not status:
        return found
    for pattern, check in GENERIC_ERRORS:
        for match in pattern.finditer(text):
            line = match.group(0).strip()
            found.append(build_finding(check, line[:300], line))
            if len(found) >= 20:
                return found
        if found:
            return found
    return [build_finding("step-failed", f"the build step exited with status {status} and the log names no failed rule",
                          f"status {status}")]


def parse_status(text):
    """{step: exit status} from the lines 'name=N' the workflow writes."""
    status = {}
    for line in text.splitlines():
        match = re.match(r"^(\w+)=(-?\d+)\s*$", line)
        if match:
            status[match.group(1)] = int(match.group(2))
    return status


GROUP_START = re.compile(r"^Unit test group_started name=(\S+)")
GROUP_FIELDS = (("Total Tests:", "total"), ("Passed Tests:", "passed"), ("Skipped known failures:", "skipped"),
                ("Failed Tests:", "failed"))


def parse_test_stats(*texts):
    """{(group, ...): {total, passed, skipped, failed} or None} from the test runner's output. A test binary runs
    several groups and prints one summary after them; groups that started and got no summary (a crash or hang) map
    to None. A later text (a retry) replaces the entry of the same groups."""
    runs = {}
    for text in texts:
        pending, current = [], None
        for line in text.splitlines():
            match = GROUP_START.match(line)
            if match:
                pending.append(match.group(1))
                current = None
                continue
            line = line.strip()
            if line.startswith("Total Tests:") and pending:
                current = {"total": 0, "passed": 0, "skipped": 0, "failed": 0}
                runs[tuple(pending)] = current
                pending = []
            for prefix, name in GROUP_FIELDS:
                if current is not None and line.startswith(prefix):
                    number = re.match(r"\s*(\d+)", line[len(prefix):])
                    if number:
                        current[name] = int(number.group(1))
        if pending:
            runs.setdefault(tuple(pending), None)
    return runs


def test_binaries(*texts):
    """The test binaries that ran: those whose target built (its test passed) or whose rule failed."""
    names = set()
    for text in texts:
        names |= set(re.findall(r"^\[[ \d]+%\] Built target (INTEGRATION_TEST_\w+)", text, re.M))
        names |= set(failed_test_targets(text))
    return names


def test_findings(runs, binaries, expected):
    """(findings, summary line): no test run, a crashed or failing binary, or fewer binaries than planned."""
    found = []
    run = len(binaries)
    summaries = [r for r in runs.values() if r]
    total = sum(r["total"] for r in summaries)
    passed = sum(r["passed"] for r in summaries)
    skipped = sum(r["skipped"] for r in summaries)
    if expected and run == 0:
        found.append(build_finding("no-tests-run", f"none of the {expected} planned test binaries ran", "no tests"))
    elif run < expected:
        found.append(build_finding("tests-missing", f"{run} of {expected} planned test binaries ran", "missing tests"))
    for names, result in sorted(runs.items()):
        label = "+".join(names)
        if result is None:
            found.append(build_finding("test-no-summary", f"test group(s) {label} started and printed no summary "
                                       "(a crash or hang)", f"nosummary {label}"))
        elif result["total"] == 0 or result["passed"] + result["skipped"] < result["total"]:
            found.append(build_finding("test-failed", f"test group(s) {label}: {result['passed']} of {result['total']} "
                                       f"passed, {result['failed']} failed", f"group {label}"))
    line = (f"{run} test binaries run (planned {expected}); {sum(len(k) for k in runs)} test groups, {total} tests, "
            f"{passed} passed, {skipped} skipped known failures")
    return found, line


def failed_test_targets(text):
    """The INTEGRATION_TEST_<name> targets whose rule failed in a log."""
    names = []
    for finding_ in parse_build_log(text):
        match = re.search(r"INTEGRATION_TEST_(\w+)", finding_["message"])
        if match and f"INTEGRATION_TEST_{match.group(1)}" not in names:
            names.append(f"INTEGRATION_TEST_{match.group(1)}")
    return names


def apply_retries(findings, retry_text):
    """Drop the failures of tests that passed on the retry ('retry <target> exit <N>' lines); returns the names."""
    passed = {m.group(1) for m in re.finditer(r"^retry (INTEGRATION_TEST_\w+) exit 0\s*$", retry_text, re.M)}
    kept = [f for f in findings
            if not (f["tool"] == "build" and any(f"{name}" in f["message"] for name in passed))]
    return kept, sorted(passed)


# ---------------------------------------------------------------- MSVC

def sarif_results(document):
    for run in document.get("runs", []):
        for result in run.get("results", []):
            location = (result.get("locations") or [{}])[0].get("physicalLocation", {})
            uri = location.get("artifactLocation", {}).get("uri", "")
            region = location.get("region", {})
            yield (result.get("ruleId", "unknown"), result.get("message", {}).get("text", ""), uri,
                   region.get("startLine", 0), region.get("startColumn", 0))


MSVC_LOG = re.compile(r"^\s*(\S[^(]*)\((\d+)(?:,(\d+))?\): warning (C\d+): (.*?)(?: \[[^\]]*\])?$")


def msvc_key(message):
    """The message of a MSVC finding without the parts that differ between SARIF and the build log."""
    message = strip_workspace(message)
    message = re.split(r"\.? For additional information|: Lines:", message, maxsplit=1)[0]
    return normalise(message.rstrip("."))


def parse_msvc(sarif_texts, log_text, lib_dir):
    """MSVC /analyze results under indra/<lib_dir>: SARIF where it exists, else the build log's C-numbered warnings."""
    seen, found = set(), []

    def add(rule, message, uri, line, col):
        if not project_file(uri) or not rel(uri).startswith(f"indra/{lib_dir}/") or (rule, rel(uri), line, col) in seen:
            return
        seen.add((rule, rel(uri), line, col))
        found.append(finding("msvc", rule, uri, line, message, msvc_key(message)))

    for text in sarif_texts:
        try:
            document = json.loads(text)
        except ValueError:
            continue
        for rule, message, uri, line, col in sarif_results(document):
            add(rule, message, uri, line, col)
    if not found:
        for raw in log_text.splitlines():
            match = MSVC_LOG.match(raw)
            if match:
                add(match.group(4), match.group(5), match.group(1), match.group(2), match.group(3) or 0)
    return found


# ---------------------------------------------------------------- baseline and gate

def baseline_path(lib):
    return BASELINE_DIR / f"{lib}.json"


def load_baseline(lib, job, directory=None):
    """{group: count} for the job, or None when the library has no baseline for it yet."""
    path = (directory or BASELINE_DIR) / f"{lib}.json"
    if not path.is_file():
        return None
    entry = json.loads(path.read_text(encoding="utf-8")).get("jobs", {}).get(job)
    return None if entry is None else dict(entry)


def group_counts(findings):
    return dict(sorted(Counter(f["group"] for f in findings).items()))


def compare(groups, baseline):
    """(new, gone): findings over the baseline per group, and baseline entries now smaller or absent; (None, None)
    without a baseline."""
    if baseline is None:
        return None, None
    new = sum(max(0, n - baseline.get(group, 0)) for group, n in groups.items())
    gone = sum(1 for group, n in baseline.items() if groups.get(group, 0) < n)
    return new, gone


def dedupe(findings):
    """One finding per fingerprint (sanitizer reports); per fingerprint and line for the tools that report lines."""
    seen, result = set(), []
    for item in findings:
        key = (item["fp"], item["line"]) if item["tool"] in ("ubsan", "clang-tidy", "msvc") else (item["fp"],)
        if key not in seen:
            seen.add(key)
            result.append(item)
    return result


def per_check(findings):
    return sorted(Counter((f["tool"], f["check"]) for f in findings).items(), key=lambda item: (-item[1], item[0]))


def baseline_state(new, gone):
    if new is None:
        return "no baseline committed: report-only"
    text = f"{new} new finding(s) against the baseline" if new else "no new findings"
    if gone:
        text += f"; {gone} baseline entries are gone: shrink the baseline"
    return text


def render_summary(job, lib, findings, new, gone, notes):
    lines = [f"### {job} on {lib}: {len(findings)} finding(s), {baseline_state(new, gone)}", ""]
    lines += [f"- {note}" for note in notes]
    families = Counter(family(f) for f in findings)
    if findings:
        lines += ["", "| family | count |", "|---|---|"]
        lines += [f"| {name} | {count} |" for name, count in sorted(families.items())]
        lines += ["", "| tool | checker | count |", "|---|---|---|"]
        lines += [f"| {tool} | {check} | {count} |" for (tool, check), count in per_check(findings)]
    return "\n".join(lines) + "\n"


def read_text(name):
    path = pathlib.Path(name) if name else None
    return path.read_text(encoding="utf-8", errors="replace") if path and path.is_file() else ""


def library_built(text, lib):
    return bool(re.search(rf"Built target {re.escape(lib)}\s*$", text, re.M)
                or re.search(rf"{re.escape(lib)}\.vcxproj -> ", text))


def cmd_collect(args):
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    log = read_text(args.log)
    status = parse_status(read_text(args.status)).get("build", 0)
    lib_dir = "newview" if args.lib.startswith("newview-") else args.lib
    notes, findings = [], []
    if args.job == "clang-tidy":
        findings = parse_tidy(log, lib_dir)
        planned, processed, exits, only = parse_tidy_markers(log)
        notes.append(f"{processed if processed is not None else 0} of {planned if planned is not None else 0} "
                     "translation unit(s) processed")
        if not planned or processed != planned:
            findings.append(build_finding("tidy-incomplete", f"clang-tidy processed {processed} of {planned} "
                                          "translation units", "tidy units"))
        for code, path in exits:
            findings.append(build_finding("tidy-exit", f"clang-tidy exited with status {code} on {path}", path))
        if exits:
            notes.append(f"{len(exits)} translation unit(s) made clang-tidy exit non-zero")
        if only:
            findings = [f for f in findings if f["tool"] == "build" or f["file"] in only]
            notes.append(f"findings reported for the {len(only)} changed file(s) only")
    elif args.job in ("asan", "tsan"):
        retry = read_text(args.retry_log)
        findings = parse_build_errors(log, status)
        findings, flaky = apply_retries(findings, retry)
        if flaky:
            notes.append(f"flaky, passed on retry: {', '.join(flaky)}")
        if status:
            notes.append(f"build step exit status {status}")
        if not library_built(log, args.lib) and not any(f["tool"] == "build" for f in findings):
            findings.append(build_finding("library-not-built", f"{args.lib} was not built", "library"))
        # UBSan (and any sanitizer that prints) writes to the test's stderr, which is in the build log; the rest
        # goes to the report files. The same report seen in both places is one finding (dedupe).
        sources = [log]
        if args.san_dir:
            sources += [read_text(p) for p in sorted(pathlib.Path(args.san_dir).glob("*")) if p.is_file()]
        for source in sources:
            findings += parse_sanitizer(source)
        stats, line = test_findings(parse_test_stats(log, retry), test_binaries(log, retry), args.expect_tests)
        findings += stats
        notes.append(line)
        canary = read_text(args.canary)
        if not canary:
            findings.append(build_finding("canary-missing", "the sanitizer canary left no result", "canary"))
        else:
            result = json.loads(canary)
            notes.append("canary: " + ("the sanitizer reports" if result["ok"] else f"MISSING {result['missing']}"))
            if not result["ok"]:
                findings.append(build_finding("canary-silent", f"the sanitizer did not report {result['missing']} in "
                                              "a deliberately faulty program: the build is not instrumented",
                                              "canary"))
    elif args.job == "msvc":
        sarifs = [p.read_text(encoding="utf-8", errors="replace")
                  for pattern in ("*.sarif", "*.nativecodeanalysis.*") for p in pathlib.Path(args.dir).rglob(pattern)]
        findings = parse_build_errors_msvc(log, status)
        findings += parse_msvc(sarifs, log, lib_dir)
        notes.append(f"{len(sarifs)} analysis log file(s) read; build step exit status {status}")
        if not library_built(log, args.lib) and not any(f["tool"] == "build" for f in findings):
            findings.append(build_finding("library-not-built", f"{args.lib} was not built", "library"))
        if not sarifs and not any(f["tool"] == "msvc" for f in findings):
            findings.append(build_finding("no-analysis-output", "no /analyze log and no C-numbered warning: the "
                                          "analysis did not run", "no analysis output"))
    else:
        raise SystemExit(f"unknown job {args.job}")
    findings = dedupe(findings)
    groups = group_counts(findings)
    new, gone = compare(groups, load_baseline(args.lib, args.job))
    record = {"job": args.job, "lib": args.lib, "new": new, "gone": gone, "groups": groups, "findings": findings,
              "notes": notes}
    (out / f"findings-{args.job}.json").write_text(json.dumps(record, indent=1), encoding="utf-8")
    text_summary = render_summary(args.job, args.lib, findings, new, gone, notes)
    (out / f"summary-{args.job}.md").write_text(text_summary, encoding="utf-8")
    summary(text_summary)
    print(text_summary)
    return 0


def parse_build_errors_msvc(text, status):
    """Build failures of an MSBuild log: the error lines, else (status non-zero) the bare status."""
    found, seen = [], set()
    for pattern, check in GENERIC_ERRORS:
        if check == "compile-error":
            continue
        for match in pattern.finditer(text):
            line = match.group(0).strip()
            if line not in seen:
                seen.add(line)
                found.append(build_finding(check, line[:300], line))
    if status and not found:
        found.append(build_finding("step-failed", f"the build step exited with status {status} and the log shows "
                                   "no error line", f"status {status}"))
    return found[:20]


# ---------------------------------------------------------------- tidy

def tidy_include_map(root, subdir):
    """{included basename: set of files (relative to root) that include it} for the sources under indra/<subdir>."""
    includers = {}
    for path in (root / "indra" / subdir).rglob("*"):
        if path.suffix not in CXX_SUFFIXES or not path.is_file():
            continue
        name = path.relative_to(root).as_posix()
        for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', path.read_text(encoding="utf-8", errors="replace"),
                                 re.M):
            includers.setdefault(pathlib.PurePosixPath(match.group(1)).name, set()).add(name)
    return includers


def affected_units(changed, units, root=ROOT, subdir="newview"):
    """The translation units to analyse for the changed files: each changed source that is a unit, and every unit that
    includes a changed header, directly or through other headers (found by name, so it may include more than needed)."""
    wanted, queue = set(), []
    includers = tidy_include_map(root, subdir)
    seen = set()
    for name in changed:
        if name in units:
            wanted.add(name)
        if name.endswith(CXX_SUFFIXES[3:]):
            queue.append(name)
    while queue:
        header = queue.pop()
        for user in includers.get(pathlib.PurePosixPath(header).name, ()):
            if user in seen:
                continue
            seen.add(user)
            if user in units:
                wanted.add(user)
            else:
                queue.append(user)
    return sorted(wanted)


def tidy_files(database, lib_dir, changed=None, root=ROOT):
    """The translation units of indra/<lib_dir> in the compile database (no tests), as paths relative to root; with
    changed (newview), only those the changed files affect."""
    entries = json.loads(pathlib.Path(database).read_text(encoding="utf-8"))
    units = set()
    for entry in entries:
        path = entry["file"] if os.path.isabs(entry["file"]) else os.path.join(entry["directory"], entry["file"])
        path = rel(path)
        if path.endswith((".cpp", ".cc", ".c")) and "/tests/" not in path and path.startswith(f"indra/{lib_dir}/"):
            units.add(path)
    if changed is None:
        return sorted(units)
    return affected_units(changed, units, root, lib_dir)


def changed_files(subdir):
    out = git_lines("diff", "--name-only", f"{NEWVIEW_BASE}...HEAD", "--", f"indra/{subdir}")
    return [name for name in out or [] if name.endswith(CXX_SUFFIXES)]


def cmd_tidy(args):
    lib_dir = "newview" if args.lib.startswith("newview-") else args.lib
    changed = changed_files(lib_dir) if args.scope == "newview" else None
    files = tidy_files(pathlib.Path(args.dir) / "compile_commands.json", lib_dir, changed)
    lines = [f"analysis: clang-tidy over {len(files)} translation unit(s) of {args.lib}"]
    if changed is not None:
        lines += [f"analysis: only {name}" for name in changed]
        lines.append(f"analysis: {len(changed)} changed C++ file(s) map to {len(files)} translation unit(s)")
    print("\n".join(lines), flush=True)
    base = ["clang-tidy", "-p", args.dir, f"--config-file={TIDY_CONFIG}", f"--header-filter=.*/indra/{lib_dir}/.*",
            "--quiet", "--extra-arg=-Wno-unknown-warning-option", "--extra-arg=-Wno-unknown-argument",
            "--extra-arg=-Qunused-arguments"]

    def one(path):
        done = subprocess.run(base + [str(ROOT / path)], capture_output=True, text=True, errors="replace", check=False)
        return path, done.returncode, done.stdout + done.stderr

    processed = 0
    with open(args.out, "w", encoding="utf-8") as log, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        log.write("\n".join(lines) + "\n")
        for path, code, output in pool.map(one, files):
            processed += 1
            log.write(output)
            if code != 0:
                log.write(f"\nanalysis: tidy-exit {code} {path}\n")
        log.write(f"analysis: processed {processed} of {len(files)}\n")
    if not files:
        print("::error::0 translation units to analyse: " + ("no changed C++ file under indra/" + lib_dir
                                                           if changed is not None else "empty compile database"))
        return 1
    return 0


# ---------------------------------------------------------------- canary

CANARY = {
    "asan": ("""#include <climits>
static int *volatile sink;
int main(int argc, char **) {
    volatile int big = INT_MAX;
    volatile int sum = big + argc;   // signed overflow: UBSan
    sink = new int[16];              // never freed: LSan
    sink = nullptr;
    return sum == 0;
}
""", ("runtime error: signed integer overflow", "LeakSanitizer")),
    "tsan": ("""#include <thread>
int shared_value;
int main() {
    std::thread worker([] { shared_value = 1; });
    shared_value = 2;                // unsynchronised: a data race
    worker.join();
    return 0;
}
""", ("ThreadSanitizer: data race",)),
}


def cache_value(cache, name):
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(f"{name}:"):
            return line.split("=", 1)[1]
    return ""


def cmd_canary(args):
    """Compile a faulty program with the build's own CXX flags, run it with the job's sanitizer options and check
    that the reports appear; the result goes to <out>/canary-<mode>.json for collect."""
    source, wanted = CANARY[args.mode]
    flags = shlex.split(cache_value(pathlib.Path(args.dir) / "CMakeCache.txt", "CMAKE_CXX_FLAGS"))
    missing = []
    if not any(flag.startswith("-fsanitize") for flag in flags):
        missing.append("-fsanitize in the build's CMAKE_CXX_FLAGS")
    with tempfile.TemporaryDirectory() as work:
        work = pathlib.Path(work)
        (work / "canary.cpp").write_text(source, encoding="utf-8")
        compiler = os.environ.get("CXX", "g++")
        built = subprocess.run([compiler, *flags, "-O1", "-g", "-pthread", str(work / "canary.cpp"), "-o",
                                str(work / "canary")], capture_output=True, text=True, check=False)
        text = built.stdout + built.stderr
        if built.returncode == 0:
            env = dict(os.environ)
            for name in ("ASAN_OPTIONS", "LSAN_OPTIONS", "UBSAN_OPTIONS", "TSAN_OPTIONS"):
                env[name] = re.sub(r"log_path=\S+", lambda _m: f"log_path={work}/report", env.get(name, ""))
            ran = subprocess.run([str(work / "canary")], capture_output=True, text=True, env=env, check=False)
            text = ran.stdout + ran.stderr + "".join(read_text(p) for p in sorted(work.glob("report*")))
        for marker in wanted:
            if marker not in text:
                missing.append(marker)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / f"canary-{args.mode}.json").write_text(json.dumps({"ok": not missing, "missing": missing}), encoding="utf-8")
    print("canary:", "ok" if not missing else f"missing {missing}")
    return 0


def cmd_failed_tests(args):
    print(" ".join(failed_test_targets(read_text(args.log))))
    return 0


# ---------------------------------------------------------------- report

def cmd_report(args):
    records = [json.loads(path.read_text(encoding="utf-8"))
               for path in sorted(pathlib.Path(args.dir).rglob("findings-*.json"))]
    results = json.loads(args.needs or "{}")
    finished = {record["job"] for record in records}
    bad = sorted(name for name, result in results.items()
                 if result in ("failure", "cancelled") or (result == "success" and name not in finished))
    has_baseline = baseline_path(args.lib).is_file()
    existing = {}
    if has_baseline:
        existing = json.loads(baseline_path(args.lib).read_text(encoding="utf-8")).get("jobs", {})
    proposal = {"lib": args.lib, "jobs": dict(existing)}
    lines = [f"## Analysis of {args.lib}", ""]
    total_new = 0
    for record in records:
        if record["job"] in bad:
            continue
        proposal["jobs"][record["job"]] = record["groups"]
    for record in records:
        total_new += record["new"] or 0
        lines.append(f"- {record['job']}: {len(record['findings'])} finding(s), "
                     f"{baseline_state(record['new'], record.get('gone'))}")
    if bad:
        lines.append(f"- jobs that did not finish or left no findings: {', '.join(bad)} "
                     "(left out of the baseline proposal)")
    lines += ["", "The gate is red only on new findings, and only once a baseline is committed; a cleanup pull request "
              "commits its own baseline update (`baseline-<lib>.json` of this report's artifact, reviewed by the owner)."]
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / f"baseline-{args.lib}.json").write_text(json.dumps(proposal, indent=1) + "\n", encoding="utf-8")
    if total_new:
        newest = [f for r in records if r["new"] for f in r["findings"]][:30]
        lines += ["", "The first findings (the baseline decides which are new):"]
        lines += [f"- {f['tool']} {f['check']} {f['file']}:{f['line']} {f['message'][:140]}" for f in newest]
    text = "\n".join(lines) + "\n"
    (out / "report.md").write_text(text, encoding="utf-8")
    summary(text)
    print(text)
    return 1 if has_baseline and (total_new or bad) else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("plan").set_defaults(func=cmd_plan)
    p = sub.add_parser("tidy")
    p.add_argument("--dir", required=True)
    p.add_argument("--lib", required=True)
    p.add_argument("--scope", default="lib")
    p.add_argument("--out", required=True)
    p.add_argument("--jobs", type=int, default=4)
    p.set_defaults(func=cmd_tidy)
    p = sub.add_parser("canary")
    p.add_argument("--mode", required=True, choices=sorted(CANARY))
    p.add_argument("--dir", required=True)
    p.add_argument("--out", required=True)
    p.set_defaults(func=cmd_canary)
    p = sub.add_parser("failed-tests")
    p.add_argument("--log", required=True)
    p.set_defaults(func=cmd_failed_tests)
    p = sub.add_parser("collect")
    p.add_argument("--job", required=True, choices=JOBS)
    p.add_argument("--lib", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--log", default="")
    p.add_argument("--status", default="", help="file with the step exit statuses (build=N)")
    p.add_argument("--retry-log", default="")
    p.add_argument("--canary", default="")
    p.add_argument("--expect-tests", type=int, default=0, help="test binaries the job should have run")
    p.add_argument("--san-dir", default="")
    p.add_argument("--dir", default=".", help="msvc: where the per-object SARIF logs are searched")
    p.set_defaults(func=cmd_collect)
    p = sub.add_parser("report")
    p.add_argument("--lib", required=True)
    p.add_argument("--dir", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--needs", default="", help="JSON of job name -> result")
    p.set_defaults(func=cmd_report)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
