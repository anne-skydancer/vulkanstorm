<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Analysis CI for the C++ cleanup

Tools, not agents, find the mechanical issues first (`doc/clean/plan.md`). The workflow
`.github/workflows/analysis.yaml` ("Analysis") runs static analysis and sanitizers over the library a cleanup branch
touches, so that an agent reads code only after the tools have had their say.

## What a run analyses

| Trigger | Analyses |
|---|---|
| push to `clean/<lib>` | `indra/<lib>` and its own tests |
| push to `clean/newview-<area>` | the newview sources the branch changed (clang-tidy only) |
| pull request into `release-clean` | the library named by the head branch, the same way |
| pull request from any other branch | `llcommon` as a self-test (this is how the workflow tests itself) |
| `clean/<name>` that is no library | nothing; the plan job says why in its summary |
| `workflow_dispatch` (input `lib`) | the named library; only possible once the file is on the default branch |

A push and its pull request share one run (the newer replaces the older). Documentation-only pull requests are skipped.

## Jobs

| Job | Runs | Output |
|---|---|---|
| `clang-tidy` (Linux) | clang-tidy over the library's translation units from a `compile_commands.json` written by a GCC configure (no build). Checks: `clang-analyzer-*`, `bugprone-*`, `performance-*`, `concurrency-*`, `modernize-*` (C++20 only), and a short low-noise `cppcoreguidelines-*` list; see `.github/analysis/clang-tidy.yaml` | SARIF, JSON, text, counts per checker |
| `asan` (Linux) | the library and its tests built with ASan + UBSan + LSan (`-O1`, no LTO) | same, per sanitizer kind |
| `tsan` (Linux) | the library's threaded tests that use no coroutines, built with TSan; the list is `.github/analysis/tsan/<lib>.txt`; no list, no job | same |
| `msvc` (Windows) | `/analyze` on the library target (PREfast, `C6xxx`), logs as SARIF | same, per rule |
| `report` | merges the jobs, compares them with the baseline, decides red or green, and writes the baseline proposal | job summary, `analysis-report-<lib>` artifact |

Every job uploads its artifact (`analysis-<job>`: `findings-<job>.sarif`, `findings-<job>.json`, `summary-<job>.md`, the
raw log) and writes a job summary with the counts per checker. The flags are in the workflow and in
`.github/actions/analysis-setup`; no CMake file changes, and the viewer is never built. Nothing is cached.

Why GCC for the sanitizers: the Linux build is GCC only, and GCC 15 has all of ASan, UBSan, LSan and TSan, so no clang
port is needed. TSan skips coroutine tests because Boost.Fiber is prebuilt without TSan fibre annotations and would
report races that are not there. Sanitizer reports never change a test's exit code (`exitcode=0`); they are read from the
log files, so one failing test does not hide the rest. A plain failed test or compile error is a finding of the
tool `build`.

## The baseline

`.github/analysis/baseline/<lib>.json` holds, per job, the findings that are known: a fingerprint (tool, checker, file and
message with the numbers removed; sanitizers use the first three project frames) and how many times it occurs.
Line numbers are not part of a fingerprint, so an edit above a finding does not make it new.

- **The first run on a library** (no baseline, or none for that job) is report-only and green. The `report` artifact
  holds `baseline-<lib>.json`, the baseline that run would create.
- **After that** the `report` job is red when a job finds more of any fingerprint than the baseline holds, and when a job
  did not finish. Fixed findings never turn it red.

To create or update the baseline: download the `analysis-report-<lib>` artifact of the run, copy `baseline-<lib>.json`
to `.github/analysis/baseline/<lib>.json` (or run `python scripts/ci_analysis.py baseline <file>`), and commit it in a
reviewed pull request. Shrink it when a cleanup fixes findings; add to it only for a finding the reviewers accept. A job
the run did not execute keeps its old entry.

## Files and the SPDX header

New source files carry `SPDX-License-Identifier: GPL-3.0-or-later`. The JSON baselines and the list files cannot carry
one and are data.

## Limits

- A pull request from a `clean/<lib>` branch analyses the library, but not what other libraries inherit from it.
- `clang-tidy` reads a GCC compile database; clang may report `clang-diagnostic-*` for GCC-only flags or code. They are
  findings like any other and go into the baseline.
- MSVC `/analyze` writes its logs next to the object files; if none is found the job falls back to the `warning C6xxx`
  lines of the build log.
