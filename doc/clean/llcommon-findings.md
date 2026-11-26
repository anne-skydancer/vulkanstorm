<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# llcommon: root causes of the analysis findings

Analysis run 45682009355 on `clean/llcommon` (release-clean 6853ccaa45, neo-j2k static), triage artifact
`analysis-triage-llcommon` (148 cases). Read and checked against the code on 2026-11-25. Every case below is Linden Lab
code that is also in upstream Firestorm (master, 7.2.5) and Linden Lab's viewer, unless it says otherwise.

## Sanitizers and failing tests (24 cases)

| Cases | What | Root cause | Verdict and fix |
|---|---|---|---|
| build 001, 002 | `llprocess` test 27 "WritePipe drops its queue when the child exits" fails (983,040 of 1,048,576 bytes still queued), also on the retry | Test 27 came with Vulkanstorm's LEAP pipe hotfix 90e8478af6 (2026-11-17), which fixed the **Windows** `WritePipe` only ("Non-Windows code is textually unchanged"). On Linux the old LL behaviour remains: after the child exits, the queued data is kept and the write retried every tick, forever. Normal CI never runs llcommon's tests, so only the analysis saw it. | **Real bug (Linux).** Port the Windows fix's rule to the POSIX `WritePipe`: drop the queue on a dead-pipe error (EPIPE) and warn once. Test 27 then passes on Linux. |
| ubsan 005 | load of value 4, not a valid `FILESLOT`, at `llprocess.cpp:1261` (`getPipePtr`) | The test passes `LLProcess::FILESLOT(4)` (`llprocess_test.cpp:1011,1014`) to exercise the "has no slot" error. `FILESLOT` has no fixed underlying type, so its valid values are 0-3: converting 4 is undefined before `slot >= NSLOTS` can catch it. | **Real UB, harmless today.** Give the enum a fixed type, `enum FILESLOT : int { ... }`: every int is then a valid value and the existing check works as intended. No behaviour change. |
| ubsan 003, 004 | downcast of an object under construction: `static_cast<T*>(this)` in `LLInstanceTracker`'s constructors (`llinstancetracker.h:253`, `:489`) and `LLEventDispatcher::addMethod` (`lleventdispatcher.h:528`) | A base class converts `this` to the derived type while the derived part is not constructed yet (CRTP registration). The pointer is only used later, but the cast itself is undefined. | **Real UB, harmless in practice.** Tracker: store the base pointer at construction and convert when looked up (the object is complete then), e.g. an aliasing `shared_ptr` made at lookup; same API. Dispatcher: capture `this` as the base and cast inside the callable. |
| lsan 006, 007, 012, 013, 016-023 (12) | "leaks" at `llinstancetracker.h:253/:489` | **Not our code**: the allocation stacks run through `__ubsan::Diag` and `__cxa_demangle`, i.e. UBSan's own reporter allocated these while reporting the downcast above. | **Artifact.** Disappears with the downcast fix. |
| lsan 008-011 | `LogControlFile` chain (`llerror.cpp:451` -> `LLLiveFile`, its `Impl`, its event timer) | `LogControlFile::fromDirectory` allocates the logging-config watcher with `new` and never frees it, by design (comment at `llerror.cpp:451`): logging runs during shutdown, so freeing it brings back destruction-order crashes. | **Deliberate (load-bearing).** LSan suppression with this reason; no free. |
| lsan 014 | per-coroutine "initializing" list, `llsingleton.cpp:153` | `get_initializing_()` allocates one list per coroutine and stores it in an `LLCoros::local_ptr`; the main thread's default coroutine never resets its own before the leak check. One small list per thread. | **Plausible, bounded.** Prove with a test (count after a singleton build on the main coroutine); if so, reset it in `cleanup_initializing_()` on the main coroutine too or suppress with the reason. |
| lsan 015 | `lazyeventapi` test's listener (`lleventdispatcher.h:528`) | A test object registered lazily that lives to process exit by design. | **Test-only.** Suppress for that test, with the reason. |
| tsan 024 | lock-order inversion, `lockstatic.h:31` (`LLSingleton<A>` / `<B>` locks), test `singleton` 5 | Two singletons that build each other nest their locks A->B in one test and B->A in another. LLSingleton nests locks **only on the main thread**: another thread takes a singleton's lock briefly, releases it and hands the construction to the main thread (`llsingleton.h:550-580`). A deadlock needs two threads in opposite orders; only one ever nests. | **False positive.** TSan suppression for this lock pair with this reason, limited to `LLSingleton<A>` and `LLSingleton<B>` (the two test classes of `llsingleton_test.cpp`): a lock-order inversion of any other singleton's lock is still reported. |

## Static analysis: the findings that can be real defects

| Where | Tool | Verdict |
|---|---|---|
| `llformat.cpp:44` `va_format` | MSVC C6053 | **Real bug (Windows).** `_vsnprintf` does not NUL-terminate when the output reaches 1,024 bytes, and `out.assign(tstr)` then reads past the buffer. Use standard `vsnprintf` (terminates, returns the full length) and format into the string at its real length. |
| `llcond.h:151` | bugprone-use-after-move | **Latent.** `std::forward<Pred>(pred)` inside a loop forwards an rvalue predicate again on every iteration. Call `pred(const_data())` without forwarding; same behaviour for every current caller. |
| `llsdserialize.cpp:148` | clang ArrayBound + MSVC C6385 | **False positive.** `get(hdr_buf, MAX_HDR_LEN)` reads at most `MAX_HDR_LEN-1`, so `hdr_buf[inbuf]` stays inside `hdr_buf[MAX_HDR_LEN+1]`; the tools cannot see the `gcount()` bound. Rewrite the bound so it is visible (or annotate), no behaviour change. |
| `llfasttimer.h:315` | StackAddressEscape | **By design.** `BlockTimer` links itself into a thread-local timer stack and unlinks in its destructor. |

The rest of the 124 static cases are lower risk and are fixed in the pass by class: macro parentheses (18), `switch` without
`default` (8), CRTP constructor access (8), virtual calls in constructors/destructors (5), empty `catch` (5), self-assignment
(4), stack-size warnings C6262 (6; one is Vulkanstorm's own, `unzip_llsdNavMesh` at `llsdserialize.cpp:2400`), and single
items. Each still gets checked before it is changed.

## Summary

- **Real bugs to fix:** Linux `WritePipe` never drops its queue (ours to finish; LL bug); `_vsnprintf` overrun in
  `llformat` on Windows (LL); `FILESLOT` and constructor-downcast UB (LL); forwarded predicate in `llcond` (latent).
- **Not bugs, suppressed with their reason:** the deliberate logging watcher, the singleton lock order, a test-only object.
- **Artifacts:** 12 of the 18 leak cases are UBSan's own reporter; they go with the downcast fix.
- **Origin:** of 148 cases, 140 trace to Linden Lab code still upstream, 5 to Firestorm, 1 to Vulkanstorm
  (`unzip_llsdNavMesh`, 2026-11-17), 2 have no source line (the failing test).

## Part 2: the findings left after the first pass

Pull request "llcommon cleanup pass, part 2" (clean/llcommon on release-clean). The analysis run of #612 ended at
clang-tidy 497, MSVC 24, ASan/UBSan/LSan 1, TSan 1 (start of the whole pass: 2340 / 38 / 6 / 1). The rest was handled
by family, one commit each, with the tool's fix-its read and compile-checked (clang-tidy on the Windows and the Linux
translation units) before they were kept.

**Fixed, with a real defect behind them**

- `LLFile::copy` reported success when `fread` failed with a read error (a short read looks like end of file): a
  truncated copy counted as done. Now `ferror` is checked, and the 16 KB buffer is on the heap (MSVC C6262, clang
  `unix.Stream`).
- `LLStringTable` rounded its size with `1u << (i+1)` at `i == 31`, a shift by 32 (clang `BitwiseShift`), and compared a
  signed with an unsigned value (MSVC C4018, which is an error in the Windows build). Done in unsigned arithmetic below
  bit 31.
- `LLTempRedirect` ignored the result of `dup2` (MSVC C6031); a failure is now logged.
- `StackWalker` printed `DWORD` values with `%d` (C6340) and passed an uninitialised-looking `dwHandle` to
  `GetFileVersionInfoA` (C6388; the parameter is ignored by the API, now `0`).
- `LLError::CallSite`'s constructor parameter `tags` was hidden by the logging macros' own `tags` array (C6246); renamed.
- `is_approx_equal_fraction` shifted a `U32` by a runtime count (C6297); now a `U64`.
- `map_` in llsdutil.h copied every value into the `LLSD` instead of forwarding it.
- Modernisation commits, generated by clang-tidy and reviewed: `modernize-use-nullptr`, `performance-avoid-endl`,
  `-return-braced-init-list` (two narrowing errors fixed by hand), `-use-ranges`, `-avoid-bind`, `-use-constraints`,
  `-use-using`, `-use-auto`, `-deprecated-headers`, `-use-emplace`, `-use-override`, `-use-designated-initializers`,
  `-loop-convert` (names and the `unsigned long` the tool wrote for a `size_t` fixed by hand),
  `cppcoreguidelines-pro-type-member-init`, `performance-unnecessary-value-param`, `-trivially-destructible`,
  `-faster-string-find`, `modernize-use-equals-delete` (deleted members made public), `-use-equals-default`.

**Small behaviour changes**, beyond the real defects above (the first commit that carried some of them,
`1d30cfa4e2`, says "No behaviour change", which is inaccurate: it also carries the `LLStringTable`, `memcpy` and
`LLPointer` changes below):

- `std::endl` became a plain newline (performance-avoid-endl): a caller's stream is no longer flushed on each line in
  `LLCPUInfo::stream`, `LLMemoryInfo::stream` and `dumpStats`. Output order and content are the same; a stream the caller
  expects flushed after the call must be flushed by the caller.
- `LLThreadSafeQueue::tryPush` and `tryPushUntil` forward their argument instead of moving it (bugprone-move-forwarding-
  reference): an lvalue argument is now copied, not moved from.
- `unzip_llsd` returns `ZR_MEM_ERROR` when `inflateInit` fails; the result used to be stored unread.
- `LLStringTable` rounds its size in unsigned arithmetic: the result for a requested size of 2^30 entries or more differs
  from the old undefined shift (the table is far smaller in practice).
- `LLFile::copy` returns false on a read error (`ferror`), where a truncated copy used to count as done.
- `LLPointer`'s copy assignment is copy-and-swap with no self-assignment guard (it handles self-assignment), and
  `LLSD::operator=` has none either (`Impl::reset` takes the new reference first).
- `LLInstanceTracker::setKey` and `LLMetricPerformanceTesterBasic::compareTestResults` take `const&`
  (no override of any of them exists anywhere in `indra/`).
- `LLError::CallSite`'s members are declared widest first (no padding); its constructor is the only way the logging macros
  build one, so only the initializer list had to follow the new order.
- `LLFile::copy` allocates its buffer before opening the files, so an allocation failure cannot leave them open.

**Rejected after trying the fix-it**

| Check | What the fix-it did | Decision |
|---|---|---|
| `modernize-use-integer-sign-comparison` (23 in the Windows .cpp files, more in headers) | `mPredicateFlags == ~0` (a `U32` against all ones, true today) became `std::cmp_equal(flags, ~0)`, false always; the `(S32)` casts it removes are the code's intent | check left out of `.github/analysis/clang-tidy.yaml` |
| `cppcoreguidelines-prefer-member-initializer` (41 in the Windows translation units) | moves assignments into the initializer list, which runs in declaration order, not in the order of the assignments; the fix-it's effect on `LLThread` (two allocations in declaration order and a `nullptr`) is harmless, so the earlier claim that it swapped allocations was wrong | check left out of the config because of the count and the per-site order check, not a known bug |
| `modernize-use-default-member-init` | left an orphan `:` in three constructors (compile error) | written by hand |
| `modernize-use-ranges` | `,,` garbage in 15 places; `std::ranges::sort`/`lower_bound` do not compile for `LLUUID` and `CompareTimeConstants` | simple cases by script, the rest keep a `NOLINT` with the reason |
| `performance-avoid-endl` | removed the flush that `getAlwaysFlush()` exists for in the log file | that one line keeps `std::endl` and a `NOLINT` |
| `performance-unnecessary-value-param` | `LLCoros::toplevel` would have bound references into the launcher's stack | kept by value, `NOLINT` with the reason |
| `performance-enum-size` (41) | no fix-it; narrower enums change struct layouts across libraries | check left out of the config |
| `cppcoreguidelines-rvalue-reference-param-not-moved` (7) | would replace `std::forward` by `std::move` in templates also instantiated with lvalue references | check left out of the config |

**Kept, with the reason beside each**

- `modernize-macro-to-enum`: `U8_MIN`, `U16_MIN`, `U32_MIN`, `LL_NEWLINE`, `LL_REFCOUNT_FREE` are used in `#if`s and in
  headers of every library, and an enum constant would be an `int`, not the unsigned type of the name (`NOLINT` at each).
- `bugprone-macro-parentheses` in `ll_convert_alias` (types in template argument lists), `performance-no-int-to-ptr` in
  the two aligned-address helpers, `bugprone-signed-char-misuse` in three UTF-32 helpers (`wchar_t` taken for a char),
  `performance-move-const-arg` in the LLSD rvalue overload set, `modernize-use-override` on a template-dependent
  `assign`, `clang-analyzer-unix.Stream` on the `fread` loop of `LLFile::copy`, `modernize-return-braced-init-list` on a
  `Param<std::string>` conversion: each has a `NOLINT` with its reason.
- MSVC: `C6326` (template constants), `C28182` (iterator never yields null), `C6001` (VirtualAlloc hint), `C6340` in
  `LLMD5` (`%i` of an unsigned is what existing digests are made from), `C28159`/`C6387` (`GetVersionEx`, kernel32), `C6262`
  in `StackWalker::ShowCallstack` (the crash path must not allocate): `#pragma warning(suppress ...)` with the reason
  above it.
- LSan: the logging watcher chain (`LogControlFile`, `LiveFileEventTimer`) stays suppressed, with its reason. The
  `get_initializing_` suppression of the first version of this pass is gone: it matched the whole allocation path, so the
  200-coroutine test (`llsingleton_test.cpp` test 16) could not fail and a per-thread list leak would have been hidden.
  The shared test `main()` (`indra/test/test.cpp`) now calls `LLSingletonBase::deleteAll()` at the end of the run, as the
  viewer does on exit, which destroys the `MasterList` and frees the running coroutine's list. Test 16 itself has no
  assertion of its own: its proof is LeakSanitizer, so outside a LeakSanitizer build it only shows that the coroutines run.
  (Commit 93211d4398's title says "narrow LSan suppression", but the entry it added covered that whole path.) Dropping
  `leak:LLError::commonInit` too: the `LogControlFile` and `LiveFileEventTimer` frames cover the chain; if the analysis
  run shows a leak that needs it, it comes back with its stack.
- Boost.Signals2 tracking: `LLEventPump` is a `signals2::trackable`. A `boost::bind` that holds a pointer or reference
  to a trackable disconnects the slot automatically when the object is destroyed; a lambda does not. `modernize-avoid-bind`
  converted three such sites (`LLLeapListener::listen`, the two `LLListenerOrPumpName` constructors) to lambdas, which
  would post to a destroyed pump (use after free). They are `boost::bind` again, with a `NOLINT` and this reason. The
  other converted sites were checked and are safe. Trade-off: those three stay on `boost::bind`, which Boost.Signals2
  tracks and clang-tidy would otherwise keep reporting.
- TSan: the deny list (`.github/analysis/tsan/llcommon-deny.txt`) skips the llcommon tests `lleventcoro`, `llcond`,
  `workqueue`, `llleap`, `llprocess`, `llprocessstdin` and `llsdserialize`, Boost.Fiber being prebuilt without TSan
  annotations. They include the tests of the threading primitives changed in this pass (`LLCond`, `WorkQueue`,
  `LLThreadSafeQueue`): **TSan has no say on those tests**; only ASan/UBSan/LSan ran them.
- The findings that are in the llmath headers (`llquaternion.h`, `llvector4a.*`, `llmatrix3a.*`, `m3math.h`, `llmath.h`,
  ...) reach llcommon through its includes. They belong to the llmath cleanup and stay in the llcommon baseline until
  then; nothing in llmath was edited here.
