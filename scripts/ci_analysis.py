#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The helper of .github/workflows/analysis.yaml (doc/clean/analysis.md).

  plan     which library a branch analyses, and what its tests are
  tidy     run clang-tidy over the library's translation units
  collect  turn one job's raw output into findings (JSON, SARIF, text, job summary)
  report   merge the jobs' findings, compare them with the committed baseline, decide red or green
  baseline write the baseline proposal of a run into .github/analysis/baseline/<lib>.json

Standard library only. A finding's fingerprint has no line numbers, so a finding survives edits above it.
"""
import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import subprocess
import sys
from collections import Counter

ROOT = pathlib.Path(__file__).resolve().parents[1]
BASELINE_DIR = ROOT / ".github" / "analysis" / "baseline"
TSAN_DIR = ROOT / ".github" / "analysis" / "tsan"
TIDY_CONFIG = ROOT / ".github" / "analysis" / "clang-tidy.yaml"
SELF_TEST_LIB = "llcommon"
NEWVIEW_BASE = "origin/release-clean"
JOBS = ("clang-tidy", "asan", "tsan", "msvc")


# ---------------------------------------------------------------- plan

def integration_tests(lib, root=ROOT):
    """The names of the library's active LL_ADD_INTEGRATION_TEST lines."""
    path = root / "indra" / lib / "CMakeLists.txt"
    names = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"\s*LL_ADD_INTEGRATION_TEST\(\s*(\w+)", line)
        if match:
            names.append(match.group(1))
    return names


def tsan_tests(lib, root=ROOT):
    """The tests of the library that run under TSan: .github/analysis/tsan/<lib>.txt, one name per line."""
    path = root / ".github" / "analysis" / "tsan" / f"{lib}.txt"
    if not path.is_file():
        return []
    wanted = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            wanted.append(line)
    return wanted


def resolve(head_ref, event, lib_input="", root=ROOT):
    """What a run analyses. Returns {run, lib, scope, reason, tests, tsan_tests}."""
    plan = {"run": False, "lib": "", "scope": "", "reason": "", "tests": [], "tsan_tests": []}
    if event == "workflow_dispatch":
        name = (lib_input or SELF_TEST_LIB).strip()
        head_ref = name if name.startswith("clean/") else f"clean/{name}"
    elif not head_ref.startswith("clean/"):
        # A pull request that is not a cleanup branch (such as this workflow's own): the self-test.
        head_ref = f"clean/{SELF_TEST_LIB}"
        plan["reason"] = f"not a clean/<lib> branch: self-test on {SELF_TEST_LIB}"
    name = head_ref[len("clean/"):]
    if name == "newview" or name.startswith("newview-"):
        plan.update(run=True, lib="newview", scope="newview",
                    reason=plan["reason"] or f"{head_ref}: static analysis of the changed newview sources")
        return plan
    if not re.fullmatch(r"[a-z0-9_]+", name) or not (root / "indra" / name / "CMakeLists.txt").is_file():
        plan["reason"] = (f"{head_ref}: indra/{name} is not a library of this repository; "
                          "the branch must be clean/<lib> or clean/newview-<area>; nothing ran")
        return plan
    plan.update(run=True, lib=name, scope="lib", reason=plan["reason"] or f"{head_ref}: library {name}")
    plan["tests"] = integration_tests(name, root)
    plan["tsan_tests"] = [t for t in tsan_tests(name, root) if t in plan["tests"]]
    return plan


def write_outputs(values):
    path = os.environ.get("GITHUB_OUTPUT")
    lines = [f"{key}={value}" for key, value in values.items()]
    if path:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")
    print("\n".join(lines))


def cmd_plan(args):
    plan = resolve(os.environ.get("HEAD_REF", ""), os.environ.get("EVENT", ""), os.environ.get("LIB_INPUT", ""))
    write_outputs({
        "run": str(plan["run"]).lower(),
        "lib": plan["lib"],
        "scope": plan["scope"],
        "reason": plan["reason"],
        "targets": " ".join(f"INTEGRATION_TEST_{t}" for t in plan["tests"]),
        "tsan_targets": " ".join(f"INTEGRATION_TEST_{t}" for t in plan["tsan_tests"]),
    })
    summary(f"### Analysis plan\n\n{plan['reason']}\n")
    return 0


# ---------------------------------------------------------------- findings

def summary(text):
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if path:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(text + "\n")


def rel(path):
    """A path inside the checkout as indra/..., or the path unchanged."""
    path = path.replace("\\", "/")
    if path.startswith("file:///"):
        path = path[len("file:///"):]
    index = path.find("/indra/")
    return path[index + 1:] if index >= 0 else path


def project_file(path):
    return "/indra/" in path.replace("\\", "/")


def normalise(text):
    text = re.sub(r"0x[0-9a-fA-F]+", "N", text)
    return re.sub(r"\d+", "N", text).strip()


def finding(tool, check, file, line, message, fp_extra=""):
    file = rel(file) if file else ""
    fp = "|".join([tool, check, file, fp_extra or normalise(message)])
    return {"tool": tool, "check": check, "file": file, "line": int(line or 0), "message": message.strip(), "fp": fp}


TIDY_LINE = re.compile(r"^(\S.*?):(\d+):(\d+): (warning|error): (.*) \[([\w.,-]+)\]$")


def parse_tidy(text, lib=None):
    seen, found = set(), []
    for raw in text.splitlines():
        match = TIDY_LINE.match(raw.strip())
        if not match:
            continue
        path, line, col, _level, message, checks = match.groups()
        if not project_file(path):
            continue
        if lib and lib != "newview" and f"indra/{lib}/" not in rel(path):
            continue
        key = (rel(path), line, col, checks)
        if key in seen:
            continue
        seen.add(key)
        found.append(finding("clang-tidy", checks.split(",")[0], path, line, message))
    return found


FRAME = re.compile(r"^\s*#\d+ (?:0x[0-9a-fA-F]+ in )?(.+?) (\S*/\S+?):(\d+)(?::\d+)?(?: \(.*)?$")
BLOCK_START = re.compile(
    r"ERROR: AddressSanitizer: ([\w-]+)"
    r"|(Direct|Indirect) leak of"
    r"|WARNING: ThreadSanitizer: ([A-Za-z -]+?)\s*(?:\(|$)"
    r"|^(\S[^\n]*?):(\d+):(\d+): runtime error: ([^\n]*)", re.M)


def sanitizer_frames(block):
    frames = []
    for line in block.splitlines():
        match = FRAME.match(line)
        if match and project_file(match.group(2)):
            frames.append((match.group(1), match.group(2), match.group(3)))
    return frames


def parse_sanitizer(text):
    """Findings of ASan, LSan, UBSan and TSan log text. A finding has its first three project frames."""
    starts = list(BLOCK_START.finditer(text))
    found = []
    for index, match in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        block = text[match.start():end]
        asan, leak, tsan, ub_file, ub_line, _col, ub_msg = match.groups()
        if ub_file is not None:
            kind = re.split(r" (?:for|of|to|on|from|at|by|within|index|shift) ", normalise(ub_msg), maxsplit=1)[0]
            found.append(finding("ubsan", kind, ub_file, ub_line, ub_msg))
            continue
        if asan:
            tool, kind = "asan", asan
        elif leak:
            tool, kind = "lsan", "leak"
        else:
            tool, kind = "tsan", normalise(tsan).replace(" ", "-")
        frames = sanitizer_frames(block)[:3]
        names = ">".join(normalise(name) for name, _file, _line in frames) or "(no project frame)"
        first = frames[0] if frames else ("", "", "0")
        found.append(finding(tool, kind, first[1], first[2], f"{kind} in {first[0] or 'third-party code'}", names))
    return found


MAKE_FAIL = re.compile(r"^g?make(?:\[\d+\])?: \*\*\* \[[^\]]*build\.make:\d+: ([^\]]+)\] Error \d+")


def parse_build_log(text):
    """Leaf make rules that failed: a compile or link error, or a test that exited non-zero."""
    found, seen = [], set()
    for line in text.splitlines():
        match = MAKE_FAIL.match(line)
        if match and match.group(1) not in seen:
            seen.add(match.group(1))
            found.append(finding("build", "failure", "", 0, f"failed: {match.group(1)}", match.group(1)))
    return found


def sarif_results(document):
    for run in document.get("runs", []):
        for result in run.get("results", []):
            location = (result.get("locations") or [{}])[0].get("physicalLocation", {})
            uri = location.get("artifactLocation", {}).get("uri", "")
            region = location.get("region", {})
            yield (result.get("ruleId", "unknown"), result.get("message", {}).get("text", ""), uri,
                   region.get("startLine", 0), region.get("startColumn", 0))


MSVC_LOG = re.compile(r"^\s*(\S[^(]*)\((\d+)(?:,(\d+))?\): warning (C\d+): (.*?)(?: \[|$)")


def parse_msvc(sarif_texts, log_text, lib):
    """MSVC /analyze results of the library: SARIF where it exists, else the build log's C-numbered warnings."""
    seen, found = set(), []

    def add(rule, message, uri, line, col):
        uri = uri.replace("\\", "/")
        if not project_file(uri) or f"indra/{lib}/" not in rel(uri) or (rule, rel(uri), line, col) in seen:
            return
        seen.add((rule, rel(uri), line, col))
        found.append(finding("msvc", rule, uri, line, message))

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


def to_sarif(findings, tool_name):
    rules = sorted({f["check"] for f in findings})
    return {
        "version": "2.1.0",
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "runs": [{
            "tool": {"driver": {"name": tool_name, "rules": [{"id": rule} for rule in rules]}},
            "results": [{
                "ruleId": f["check"],
                "level": "warning",
                "message": {"text": f["message"]},
                "locations": [{"physicalLocation": {
                    "artifactLocation": {"uri": f["file"] or "unknown"},
                    "region": {"startLine": max(f["line"], 1)}}}],
                "partialFingerprints": {"vkstorm/v1": f["fp"]},
            } for f in findings],
        }],
    }


# ---------------------------------------------------------------- baseline

def baseline_path(lib):
    return BASELINE_DIR / f"{lib}.json"


def load_baseline(lib, job):
    """{fingerprint: count} for the job, or None when the library has no baseline for it yet."""
    path = baseline_path(lib)
    if not path.is_file():
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    entry = data.get("jobs", {}).get(job)
    return None if entry is None else dict(entry)


def count_new(findings, baseline):
    """How many findings exceed the baseline, per fingerprint; None when there is no baseline."""
    if baseline is None:
        return None
    have = Counter(f["fp"] for f in findings)
    return sum(max(0, n - baseline.get(fp, 0)) for fp, n in have.items())


def per_check(findings):
    return sorted(Counter((f["tool"], f["check"]) for f in findings).items(), key=lambda item: (-item[1], item[0]))


def render_summary(job, lib, findings, new, extra=""):
    status = ("baseline created (report-only run)" if new is None
              else f"{new} new finding(s) against the baseline" if new else "no new findings")
    lines = [f"### {job} on {lib}: {len(findings)} finding(s), {status}", ""]
    if extra:
        lines += [extra, ""]
    if findings:
        lines += ["| tool | checker | count |", "|---|---|---|"]
        lines += [f"| {tool} | {check} | {count} |" for (tool, check), count in per_check(findings)]
    return "\n".join(lines) + "\n"


def cmd_collect(args):
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    def text(name):
        return pathlib.Path(name).read_text(encoding="utf-8", errors="replace") if name and pathlib.Path(name).is_file() else ""

    extra = ""
    if args.job == "clang-tidy":
        findings = parse_tidy(text(args.log), args.lib)
        tool = "clang-tidy"
    elif args.job in ("asan", "tsan"):
        findings = parse_build_log(text(args.log))
        for path in sorted(pathlib.Path(args.san_dir).glob("*")) if args.san_dir else []:
            if path.is_file():
                findings += parse_sanitizer(path.read_text(encoding="utf-8", errors="replace"))
        tool = args.job
    elif args.job == "msvc":
        sarifs = []
        for pattern in ("*.sarif", "*.nativecodeanalysis.*"):
            for path in pathlib.Path(args.dir).rglob(pattern):
                sarifs.append(path.read_text(encoding="utf-8", errors="replace"))
        findings = parse_msvc(sarifs, text(args.log), args.lib)
        extra = f"{len(sarifs)} analysis log file(s) read"
        tool = "msvc"
    else:
        raise SystemExit(f"unknown job {args.job}")
    findings = dedupe(findings)
    baseline = load_baseline(args.lib, args.job)
    new = count_new(findings, baseline)
    record = {"job": args.job, "lib": args.lib, "new": new, "findings": findings, "note": extra}
    (out / f"findings-{args.job}.json").write_text(json.dumps(record, indent=1), encoding="utf-8")
    (out / f"findings-{args.job}.sarif").write_text(json.dumps(to_sarif(findings, tool), indent=1), encoding="utf-8")
    text_summary = render_summary(args.job, args.lib, findings, new, extra)
    (out / f"summary-{args.job}.md").write_text(text_summary, encoding="utf-8")
    summary(text_summary)
    print(text_summary)
    return 0


def dedupe(findings):
    """Same fingerprint and line are one finding (one issue seen by several translation units)."""
    seen, result = set(), []
    for item in findings:
        key = (item["fp"], item["line"])
        if key not in seen:
            seen.add(key)
            result.append(item)
    return result


# ---------------------------------------------------------------- tidy

def tidy_files(database, lib, scope, changed=None):
    entries = json.loads(pathlib.Path(database).read_text(encoding="utf-8"))
    files = []
    for entry in entries:
        path = pathlib.Path(entry["directory"], entry["file"]).as_posix() if not os.path.isabs(entry["file"]) \
            else entry["file"].replace("\\", "/")
        if path.endswith((".cpp", ".cc", ".c")) and "/tests/" not in path and f"/indra/{lib}/" in path:
            files.append(path)
    if scope == "newview":
        wanted = set()
        for name in changed or []:
            stem = pathlib.PurePosixPath(name)
            wanted.add(str(stem.with_suffix("")))
        files = [f for f in files if str(pathlib.PurePosixPath(rel(f)).with_suffix("")) in wanted]
    return sorted(set(files))


def changed_newview_files():
    out = subprocess.run(["git", "diff", "--name-only", f"{NEWVIEW_BASE}...HEAD", "--", "indra/newview"],
                         cwd=ROOT, capture_output=True, text=True, check=False).stdout
    return [line for line in out.splitlines() if line]


def cmd_tidy(args):
    database = pathlib.Path(args.dir) / "compile_commands.json"
    files = tidy_files(database, args.lib, args.scope, changed_newview_files() if args.scope == "newview" else None)
    print(f"clang-tidy over {len(files)} translation unit(s) of {args.lib}", flush=True)
    header_filter = f".*/indra/{args.lib}/.*"
    base = ["clang-tidy", "-p", args.dir, f"--config-file={TIDY_CONFIG}", f"--header-filter={header_filter}",
            "--quiet", "--extra-arg=-Wno-unknown-warning-option", "--extra-arg=-Wno-unknown-argument",
            "--extra-arg=-Qunused-arguments"]

    def one(path):
        done = subprocess.run(base + [path], capture_output=True, text=True, errors="replace", check=False)
        return path, done.stdout + done.stderr

    with open(args.out, "w", encoding="utf-8") as log, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for path, output in pool.map(one, files):
            log.write(output)
    return 0


# ---------------------------------------------------------------- report

def cmd_report(args):
    records = []
    for path in sorted(pathlib.Path(args.dir).rglob("findings-*.json")):
        records.append(json.loads(path.read_text(encoding="utf-8")))
    bad = [name for name, result in json.loads(args.needs or "{}").items() if result in ("failure", "cancelled")]
    existing = {}
    if baseline_path(args.lib).is_file():
        existing = json.loads(baseline_path(args.lib).read_text(encoding="utf-8")).get("jobs", {})
    proposal = {"lib": args.lib, "jobs": dict(existing)}
    lines = [f"## Analysis of {args.lib}", ""]
    total_new = 0
    for record in records:
        proposal["jobs"][record["job"]] = dict(sorted(Counter(f["fp"] for f in record["findings"]).items()))
        if record["new"]:
            total_new += record["new"]
        state = "baseline created" if record["new"] is None else f"{record['new']} new"
        lines.append(f"- {record['job']}: {len(record['findings'])} finding(s), {state}")
    if bad:
        lines.append(f"- jobs that did not finish: {', '.join(sorted(bad))}")
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
    if total_new or bad:
        return 1
    return 0


def cmd_baseline(args):
    proposal = json.loads(pathlib.Path(args.proposal).read_text(encoding="utf-8"))
    BASELINE_DIR.mkdir(parents=True, exist_ok=True)
    baseline_path(proposal["lib"]).write_text(json.dumps(proposal, indent=1) + "\n", encoding="utf-8")
    print(f"wrote {baseline_path(proposal['lib'])}")
    return 0


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
    p = sub.add_parser("collect")
    p.add_argument("--job", required=True, choices=JOBS)
    p.add_argument("--lib", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--log", default="")
    p.add_argument("--san-dir", default="")
    p.add_argument("--dir", default=".", help="msvc: where the per-object SARIF logs are searched")
    p.set_defaults(func=cmd_collect)
    p = sub.add_parser("report")
    p.add_argument("--lib", required=True)
    p.add_argument("--dir", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--needs", default="", help="JSON of job name -> result")
    p.set_defaults(func=cmd_report)
    p = sub.add_parser("baseline")
    p.add_argument("proposal")
    p.set_defaults(func=cmd_baseline)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
