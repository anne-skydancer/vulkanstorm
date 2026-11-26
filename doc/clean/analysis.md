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
| pull request into `release-clean` | the library named by the head branch, the same way; the `codegen` job also checks the pull request's `(style)` commits |
| pull request from any other branch | `llcommon` as a self-test, **only** when the pull request changes `.github/workflows/analysis.yaml`, `.github/actions/analysis-setup/**`, `.github/analysis/**`, `scripts/ci_analysis.py`, `scripts/ci_triage.py`, `scripts/tests/ci_analysis_test.py`, `scripts/tests/ci_triage_test.py`, `scripts/tests/ci_analysis_setup_test.py`, `.github/actions/codegen-check/**`, `scripts/ci_codegen.py` or `scripts/tests/ci_codegen_test.py` (the test fixtures are under `.github/analysis/fixtures/`); otherwise nothing runs |
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
| `tsan` (Linux) | the library's tests built with TSan, all of them except the deny list `.github/analysis/tsan/<lib>-deny.txt` (the tests that use coroutines or child processes; **TSan has no say on those tests**, see below), after a canary | same |
| `msvc` (Windows) | `/analyze` on the library target's **sources** (PREfast, `C6xxx`), logs as SARIF; build errors are findings | same, per rule |
| `triage` (Linux, report only) | explains the real bug findings of the run, see [Triage](#triage) | `analysis-triage-<lib>` artifact, job summary |
| `codegen` (Linux) | the **identical compiled code** check of the pull request's `(style)` commits, see [Identical compiled code](#identical-compiled-code); red when a style commit is not proven identical. It is its own check: it is not part of `report`'s verdict or of the triage | `analysis-codegen-<lib>` artifact, job summary |
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
job has a `build` finding `canary-silent` that carries the tail of the canary's output, because the build would
otherwise look clean while uninstrumented. The canary runs once, with a timeout, and is never retried: the TSan canary
is deterministic (the worker thread writes, the main thread sleeps 20 ms, then writes), so a silent canary means the
sanitizer stopped working, which a retry would hide. A `FATAL: ThreadSanitizer` line (the runtime stopped the program)
is reported as its own cause.

**What the TSan deny list leaves unchecked.** Boost.Fiber is prebuilt without TSan fibre annotations, and TSan and
`fork` do not mix, so `.github/analysis/tsan/llcommon-deny.txt` skips the llcommon tests `lleventcoro`, `llcond`,
`workqueue`, `llleap`, `llprocess`, `llprocessstdin` and `llsdserialize`. Those include the tests of the threading
primitives that part 2 of the cleanup changed (`LLCond`, `WorkQueue`, `LLThreadSafeQueue`): **TSan did not look at them.**
A green TSan job says nothing about races in that code; ASan/UBSan/LSan do run those tests.

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

## Identical compiled code

`doc/clean/plan.md` splits a subsystem's cleanup into a bugs-and-safety pull request and a style pull request. The `codegen`
job proves the second: every commit of the pull request whose subject **ends with the tag `(style)`** (for example
`llmath: modernize-use-auto (style)`) must compile to the same machine code as its parent. A commit without the tag at the end is
listed as skipped and not checked; the job summary lists both. A style commit that differs turns the job red and moves to
the bugs-and-safety pull request.

It runs on a pull request into `release-clean` whose head names a library (`clean/<lib>`, or the `llcommon` self-test),
over the commits of `base..head`, and does nothing when none has the tag (the plan then prints a `::warning::` if there
were commits but none was tagged). A `clean/newview-<area>` pull request has no `codegen` job: it runs for a library
only (`scope == lib`). `workflow_dispatch` takes `codegen_range` (a range
`A..B` or a list of commits) and `codegen_all_style` (check every commit of it, tagged or not; this is how the check is
proven on history), and runs the job for the library of the `lib` input.

**How.** The job configures once (`analysis-setup`, mode `codegen`: the shipped options, RelWithDebInfo with `-O2 -g0 -DNDEBUG`:
the viewer's own flags add `-g` and the shipped build is `-O3` with LTO, so a pass is a proof at `-O2` without LTO, not of
the shipped output; `-g0` only saves build time), then for each style commit checks out its parent and the commit and builds the library target
(`cmake --build --target <lib>`). Only the translation units whose sources changed recompile, so a commit that edits two
files costs two compiles; a changed header recompiles what includes it. `scripts/ci_codegen.py` then reads the library's
object files (those its link line names) and compares each with the same object at the parent. A commit's parent that is the
previous checked commit is not built twice. `-ffile-prefix-map` and `-frandom-seed` are belt and braces: the objects carry
no debug info and no per-build names. The `extra_targets` input of the action adds other CMake targets (for instance a
library that consumes a changed header) to the build and the comparison.

**What the comparison covers.** The objects are ELF files, read by the script itself (no `objdump`): per section the type,
flags, alignment, entry size, size, bytes and relocations (offset, type, target symbol name, addend), and per symbol its
binding, type, visibility, section and size. The build puts every function and variable in its own section
(`-ffunction-sections -fdata-sections`), so a section is a symbol, its name is its mangled name and its offsets do not move
when a neighbour changes. A changed, added or removed section or symbol, or an added or removed object, is a difference; the
summary names the symbols (demangled) per object.

**What the build compiles, and what it does not.** Equal objects prove nothing about code the build never compiled, so
the verdict has more states than the objects alone:

| Verdict | Meaning |
|---|---|
| `identical` | the objects are equal, every changed line is compiled by at least one translation unit of the target, and every changed code file is compiled into them. Worded "identical in the compiled configuration (Linux, GCC, `LL_RELEASE_FOR_DOWNLOAD`)" |
| `differs` | an object, section or symbol differs; the symbols are listed |
| `unproven` | a changed line is not compiled by any translation unit of this build (an inactive branch: Windows, macOS, debug-only, `LL_RELEASE_FOR_DOWNLOAD`), or it cannot be probed (below). The summary lists `file:line (old or new text)` and the reason |
| `not-covered` | a changed `.c .cc .cpp .cxx .h .hpp .inl .mm` file is not in the union of the objects' depfiles (`CMakeFiles/<target>.dir/*.o.d`): tests (`LL_TESTS=OFF`), Windows-only sources, files of another library. Listed |
| `build-failed` | the library did not build at the commit or at its parent |
| `error` | the script itself failed on that commit (no link line, no depfile, an LTO or REL object, ...); the message is shown and the other commits still run |

Every state but `identical` turns the job red. When a header changed, the summary adds "headers changed: other libraries'
objects are not compared". Results are written to the job summary and to `codegen.json` after each commit, so a timeout
still leaves the earlier ones. `parse_elf` refuses what it cannot read soundly: `.gnu.lto*` sections (LTO objects hold
intermediate code), `SHT_REL` relocations, machines other than x86-64 and aarch64, and a set of objects with no `.text`
at all. The link line must name object files (a `@response` file is read); a build whose depfiles are missing fails.

**How a changed line is known to be compiled.** By asking the preprocessor, not by reading `#if`s. For each side of the commit (the parent's text for the old lines, the commit's text for the new lines) the script copies the repository files the affected translation units read into a scratch directory (never the working tree the build uses, so the incremental build and the `__LINE__` handling are untouched), puts a unique token line `__codegen_probe_<n>__` just before each changed line, and runs the real compile command of each translation unit that reads a changed file (`compile_commands.json`, paths moved to the scratch copy, `-E -P -w`, no output or dependency file; a `.cpp` is its own unit, a header goes through every unit whose depfile lists it). A token that appears in the output of at least one unit sits in an active region: the line is compiled, and the object comparison covers it (`#ifdef __GNUC__` on GCC and the `#else` of `#ifdef LL_WINDOWS` are compiled on this build). A token that appears in no unit is in an inactive branch and the line is `unproven`. This needs no knowledge of include guards. Lines that hold only blanks and comments are not probed (they cannot change the code). **Two kinds of line cannot take a probe and are reported `unproven`, chosen for safety over a guess about the macro's uses:** a continuation line (the line before ends with a backslash: a multi-line macro or statement, where a probe line would change the macro) and an `#else`, `#elif` or `#endif` line (a probe before it sits in the branch before it). A changed token inside a macro argument that the macro discards is also reported, since its probe vanishes; that is the conservative direction. The preprocessing runs on the translation units that read a changed file only.

**What it ignores, and why that is safe.**

| Ignored | Why |
|---|---|
| debug info (`.debug*`), notes, `.comment`, the symbol table order | they describe the code, they are not code; the build has no `-g` anyway |
| the unwind tables (`.eh_frame`) | generated from the function's instructions: they differ only if the code does |
| the build path | `-ffile-prefix-map=<workspace>=.` turns `__FILE__` and the debug paths relative |
| random names and seeds | `-frandom-seed` is fixed |
| `__LINE__` in the project's files | a style edit adds or removes lines, and every logging (`LL_INFOS`...) and assert call site bakes `__LINE__` into the code, so a whole file would differ. At each commit it builds, the script replaces the token `__LINE__` by `0` in the files under `indra/` that contain it (and puts the originals back before moving on; the commit under test is not changed). Only the files a commit changes are rewritten, so the build stays incremental. A line number only appears in what the code prints or asserts, which is not what a style commit is about. Not reached: `__LINE__` inside third-party headers (`BOOST_THROW_EXCEPTION`, `std::source_location`, `__builtin_LINE`): a style edit that shifts a line that uses one shows as a difference, never as a false "identical". A macro that builds a unique name from `__LINE__` and is used twice in one scope breaks the build with `0` (`LLREGISTER_STATIC`): the job reports `build-failed` and the commit gets a full read |

What it does not prove: the code of other libraries that include a changed header (only the library's own objects are
compared), a build with another compiler or flags (a different optimiser may order code differently; a difference found
there is a reason to read the commit, not proof of a bug), and anything at link time (no LTO here). A commit that differs only
because the compiler's choices moved (for instance an inlining decision after a signature change) is reported as differing
and is read; that is the safe direction.

A commit that cannot be proven says why in the summary: `build-failed` when the library does not build at the commit or at its parent on Linux (the
GCC build; a commit made from a Windows-only fix-it pass may not compile there), or `differs` with the symbols. Two
things in a `differs` list are not noise: a changed string literal (for example `llassert_always(p != NULL)` becomes
`p != nullptr`, and the assertion prints its expression) and an `auto` that deduces another type than the one it replaced
(`iterator` for `const_iterator`). The compiler's numbered local labels (`.LC27`) are compared by the place they point to,
and the clone suffixes (`.constprop.0`, `.cold`, `.isra.0`) appear in the demangled names.

The artifact `analysis-codegen-<lib>` holds `codegen.json` (the verdict per commit and every differing symbol), `codegen.md`
(the job summary) and one build log per commit. The script's tests (`scripts/tests/ci_codegen_test.py`) run without a
compiler.

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
