<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Analysis CI for the C++ cleanup

Tools, not agents, find the mechanical issues first (`doc/clean/plan.md`). The workflow
`.github/workflows/analysis.yaml` ("Analysis") runs static analysis and sanitizers over the library a cleanup branch
names, so that an agent reads code only after the tools have had their say.

## What a run analyses

| Trigger | Analyses |
|---|---|
| push to `clean/<lib>` | `indra/<lib>` and its own tests |
| push to `clean/newview-<area>` | clang-tidy over the newview translation units the branch's changed files affect |
| pull request into `release-clean` | the library named by the head branch, the same way |
| pull request from any other branch | `llcommon` as a self-test, **only** when the pull request changes `.github/workflows/analysis.yaml`, `.github/actions/analysis-setup/**`, `.github/analysis/**`, `scripts/ci_analysis.py`, `scripts/ci_triage.py`, `scripts/tests/ci_analysis_test.py`, `scripts/tests/ci_triage_test.py` or `scripts/tests/ci_analysis_setup_test.py` (the test fixtures are under `.github/analysis/fixtures/`); otherwise nothing runs |
| a `clean/...` branch that names no library | the **plan job fails** with `::error::` and the reason; nothing else runs |
| `workflow_dispatch` (input `lib`) | the named library; only possible once the file is on the default branch |

A library is a directory `indra/<lib>` whose `CMakeLists.txt` has `add_library(<lib>`, except `cmake`, `llkdu`,
`llphysicsextensionsos` and `llimagej2cneo` (the configure does not build them). `clean/newview-<area>` needs an area
(`clean/newview` alone fails).

A push and its pull request do not cancel each other: the concurrency group has the event name and the dispatch input
in it, a newer run of the same event replaces the older, and the plan job skips the push run when an open pull request
into `release-clean` has that branch as its head (the pull request run analyses it). Only the pull request event skips
documentation-only changes (`doc/**`, `**/*.md`); a push always runs.

## Jobs

| Job | Runs | Output |
|---|---|---|
| `plan` | the branch to library mapping, the test list, the TSan list, and why TSan has no job when it has none (always printed in the job summary) | outputs, job summary |
| `clang-tidy` (Linux) | clang-tidy (C++20) over the library's **sources** (not its tests) from a `compile_commands.json` written by a GCC configure, no build. Checks: `clang-analyzer-*`, `bugprone-*`, `performance-*`, `concurrency-*`, `modernize-*` (C++20 only), and a short low-noise `cppcoreguidelines-*` list; see `.github/analysis/clang-tidy.yaml`. The number of translation units processed is printed; a unit that makes clang-tidy exit non-zero is a finding | JSON, text, counts per checker and per family |
| `asan` (Linux) | the library and all its tests built with ASan + UBSan + LSan (`-O1`, no LTO), after a canary | same, per sanitizer kind; test counts |
| `tsan` (Linux) | the library's tests built with TSan, all of them except the deny list `.github/analysis/tsan/<lib>-deny.txt` (the tests that use coroutines or child processes), after a canary | same |
| `msvc` (Windows) | `/analyze` on the library target's **sources** (PREfast, `C6xxx`), logs as SARIF; build errors are findings | same, per rule |
| `triage` (Linux, report only) | explains the real bug findings of the run, see [Triage](#triage) | `analysis-triage-<lib>` artifact, job summary |
| `report` | merges the jobs, compares them with the committed baseline, decides red or green, writes the baseline proposal | job summary, `analysis-report-<lib>` artifact |

Every job uploads its artifact (`analysis-<job>`: `findings-<job>.json`, `summary-<job>.md`, the raw logs, the sanitizer
reports) and writes a job summary with the counts per checker (clang-tidy also per family: `bugprone`, `modernize`, ...).
The flags are in the workflow and in `.github/actions/analysis-setup`; no CMake file changes, and the viewer is never
built. There is no compile cache; the autobuild package cache is restored (never saved by this workflow).
`scripts/tests/ci_analysis_setup_test.py` keeps the configure options and the Windows `LL_BUILD` flags of
`analysis-setup` equal to those of `.github/actions/viewer-build`, except the listed differences (LTO, precompiled
headers, tests, packaging).

clang-tidy and MSVC `/analyze` cover the library's sources and headers, not its tests (`tests/` directories are skipped
by clang-tidy, and the MSVC job builds the library target only). The tests are covered by ASan, UBSan, LSan and TSan.

## Sanitizers

Why GCC: the Linux build is GCC only, and GCC 15 has all of ASan, UBSan, LSan and TSan, so no clang port is needed.

| Flag or option | Why |
|---|---|
| `-fsanitize=address,undefined` | ASan, UBSan (and LSan, with ASan) |
| `-fsanitize-recover=address` and `halt_on_error=0` | an ASan error does not abort the test: one run lists every error. UBSan recovers by default |
| `exitcode=0` | sanitizer reports never change a test's exit code; the report, not the first failing test, says what is wrong |
| `log_path=san/report` | one report file per process |
| `detect_stack_use_after_return=0` | Boost.Fiber and ucontext switch stacks, which this check reports as false use-after-return |
| `-U_FORTIFY_SOURCE` | fortified libc calls and ASan's own checks of the same calls do not mix |
| `-fno-omit-frame-pointer`, `-O1` | readable stacks at a bearable cost |
| TSan: only tests off the deny list | Boost.Fiber is prebuilt without TSan fibre annotations; a coroutine test would report races that are not there. A deny-list entry that is no test is a warning in the plan summary |

UBSan prints to the test's stderr, so it is in `build.log`, not in `san/`: the collector parses sanitizer text from both
and counts one report once. A **canary** (a deliberately faulty program compiled with the build's own `CMAKE_CXX_FLAGS`
and run with the job's options) must produce a UBSan and an LSan report (ASan job) or a TSan report; if it does not, the
job has a `build` finding `canary-silent`, because the build would otherwise look clean while uninstrumented.

A test target that fails is run once more; it is reported only if it fails again (a pass is listed as flaky in the
summary). The TSan job runs two build jobs at a time because TSan makes timing-based tests flaky under load. Build and
test steps time out after 90 minutes, so the Findings step still runs; jobs time out after 120.

## Nothing fails silently

A job that builds or runs nothing is a finding, not green. Each build step writes its exit status (`status.txt`); a
non-zero status with no failed make rule becomes a `build` finding from the first error line (`error:`, `No rule to make
target`, `CMake Error`, MSBuild `error C...`/`LNK...`), or `step-failed`. Also `build` findings: the library target was
not built, no test binary ran (or fewer than planned), a test binary crashed without a summary, a test group passed
fewer tests than it ran (skipped known failures count as skipped), clang-tidy processed fewer units than planned or exited
non-zero on one, the MSVC job produced no analysis output. The job summary shows the test binaries run, the tests, the
passed and skipped counts, and the exit status.

## The baseline and the gate

`.github/analysis/baseline/<lib>.json` holds, per job, the known findings as counts per `tool|checker|file` (for `build`
findings the failing target or test). The gate compares **counts per (tool, checker, file)**, never message text,
fingerprints or line numbers: a rename inside a file, a reworded message or an edit above a finding does not make it new
(a cleanup branch edits and moves code all the time). A file rename does show up as new in the new file and gone in the
old one until the baseline is updated. The detailed findings (message, line, the sanitizer stack key) stay in the
`findings-<job>.json` report. `modernize-*` stays in the gate: the cleanup ratchets modernisation too; the summary shows
the counts per checker family.

Sanitizer and MSVC findings use a stable key so that one issue counts once: the sorted set of the top project frame of
each stack (ASan and LSan: the first, access stack only; TSan: the access stacks, not thread-creation stacks), with the
workspace prefix (`D:\a\...`, `/home/runner/work/...`, `/__w/...`) removed from every message; MSVC messages are cut
before `For additional information` so the SARIF and the build-log fallback give the same key. A report with no project
frame keeps its kind and top frame, so unrelated third-party reports are not merged.

- **No baseline committed** (the first runs on a library): the report says "no baseline committed: report-only" and is
  green. The `report` artifact holds `baseline-<lib>.json`, the proposal of that run (jobs that failed or were cancelled
  are left out; a job the run did not execute keeps its old entry).
- **A baseline exists**: the `report` job is red when a job finds more of any group than the baseline holds, or did not
  finish. Fixed findings never turn it red; the summary says "N baseline entries are gone: shrink the baseline" (a note,
  not a failure).

To create or update a baseline: download the `analysis-report-<lib>` artifact of the run and copy `baseline-<lib>.json`
to `.github/analysis/baseline/<lib>.json`. **Each cleanup pull request commits its own baseline update**, so the baseline
lives in the pull request and the owner reviews it with the code: shrink it when the cleanup fixes findings; add to it
only for a finding the owner accepts. newview has one baseline per area (`baseline/newview-<area>.json`).

## Suppressions
A finding that is proven not to be a bug is suppressed, with the reason written beside the entry, never in a list nobody
reads:
- Sanitizers: `.github/analysis/suppressions/<lib>-lsan.txt` (LeakSanitizer) and `<lib>-tsan.txt` (ThreadSanitizer). The
  `asan` and `tsan` jobs add `suppressions=<file>` to `LSAN_OPTIONS` / `TSAN_OPTIONS` when the file exists and say so in
  the job summary. Every entry has a comment with its reason; a suppression without one is not accepted in review.
- clang-tidy: `// NOLINTNEXTLINE(check): reason` (or `NOLINTBEGIN`/`NOLINTEND` around a macro) in the source, next to the
  code. A check that does not fit the code base is disabled in `.github/analysis/clang-tidy.yaml` with the reason there.
- MSVC `/analyze`: `#pragma warning(suppress : Cnnnn)` or `push`/`disable`/`pop` with the reason in the comment above.
## Triage

The `triage` job runs after the analysis jobs and turns their findings into cases a person can act on. No AI and no API
key: git, the public raw file URLs of the two upstream repositories, and text parsing (`scripts/ci_triage.py`). It is
report only: it never fails the run (`continue-on-error`), commits nothing, and has `contents: read` only. It writes the
artifact `analysis-triage-<lib>` (`triage.json`, machine readable; `triage.md`, one section per case) and a job summary
with the cases per tool, per origin and inherited or ours. With no real finding it writes an empty triage and uploads no
artifact.

**Which findings.** Real bug findings only: ASan, UBSan, LSan and TSan reports, test failures and every `build` finding,
`clang-analyzer-*` (not the dead-code and performance groups), `bugprone-*` (not `easily-swappable-parameters` and
`reserved-identifier`), `concurrency-*`, and MSVC `C6xxx` and `C28xxx`. Never `modernize-*`, `performance-*`,
`cppcoreguidelines-*` or other style items.

**Per case:**

1. *Origin.* `git blame -w -M` of the finding's line and of the first project frames of its stack (whitespace changes
   ignored: the 2024 tab and line-ending clean-up of Linden Lab would otherwise own every line): commit, date, author,
   class. Vulkanstorm: a commit after the 2026-10-02 reset by the owner's noreply address, or one that carries a
   `<Vulkanstorm>` marker (line or commit subject), or (after the reset) one that is in neither upstream history.
   Firestorm: an `FS:` marker on the line, or a commit in upstream Firestorm's history and not in Linden Lab's. Everything
   else is Linden Lab. The job fetches the commit graphs (no file contents) of `FirestormViewer/phoenix-firestorm`
   (`master`, `Firestorm_7.2.5`) and `secondlife/viewer` (default branch) into the checkout and tells which commits are
   in which; if that fails, the classes use the markers and the dates only, and the job summary says so.
2. *Inherited or ours.* The same file is fetched from the raw URLs of upstream Firestorm `master`, `Firestorm_7.2.5` and
   Linden Lab's default branch, nothing is cloned. Per upstream: `line-present` (the exact line text, whitespace
   squeezed; a line of only braces or a bare keyword is never matched), `function-present` (only the function name
   followed by `(`), `absent`, `file-missing` or `unavailable`. Verdict: `inherited` (the exact line is upstream),
   `function-only`, `ours` (absent everywhere), `unknown` (an upstream could not be read), `n/a` (no source line). The
   function of a finding is found by a text heuristic, not a parser; a sanitizer names its own function.
3. *Context.* The symbolised stacks with the source around each of the first four project frames (8 lines before and
   after), and the triggering test (binary, test group, test number) read from the stack; for a failed test, the build log
   after the binary was linked.
4. *Grouping.* One case per root: sanitizer reports by their stable key (the top project frame of each access stack: the
   allocation site of a leak, the lock pair of a lock-order report), UBSan, clang-tidy and MSVC findings by checker, file
   and enclosing function, `build` findings by check and target. The members of a case are listed in it.
5. *Focused re-run* (sanitizer jobs only). The `asan` and `tsan` jobs end with a step that runs each test binary that
   failed or reported once more, alone, with `ASAN_OPTIONS` `fast_unwind_on_malloc=0:malloc_context_size=30:detect_leaks=1`,
   `UBSAN_OPTIONS` `print_stacktrace=1`, `TSAN_OPTIONS` `history_size=7:second_deadlock_stack=1` added to the job's own
   options, at most 5 minutes per binary and 25 minutes in all (the process group is killed on a timeout). The output is
   in the job's artifact (`analysis-out/rerun/`); the triage attaches the matching report to the case, or says that the
   re-run reported nothing like it.

The triage is a pointer, not a verdict: a `function-only` or `unknown` result and the heuristic function names need a
person's eye.

## newview

`clean/newview-<area>` runs clang-tidy over the translation units the branch's changed C++ files affect: a changed `.cpp`
is itself a unit; a changed header maps to every unit that includes it, directly or through other headers (found by the
`#include` names in `indra/newview`, so the set may be larger than strictly needed). Only findings in the changed files
are reported. If the changed files map to **zero** translation units (or no C++ file changed), the job says so and
fails. The generated headers of newview come from `configure_file` at configure time, so the compile database needs no
build; a header that is missing anyway shows up as a `clang-diagnostic-error` finding and a clang-tidy non-zero exit.
The baseline is per area.

## Files and the SPDX header

New source files carry `SPDX-License-Identifier: GPL-3.0-or-later`. The JSON baselines and the list files cannot carry
one and are data.

## Limits

- A pull request from a `clean/<lib>` branch analyses the library, but not what other libraries inherit from it.
- `clang-tidy` reads a GCC compile database; clang may report `clang-diagnostic-*` for GCC-only flags or code. They are
  findings like any other and go into the baseline.
- MSVC `/analyze` writes its logs next to the object files; if none is found the job falls back to the `warning C6xxx`
  lines of the build log.
