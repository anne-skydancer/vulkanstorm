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
| tsan 024 | lock-order inversion, `lockstatic.h:31` (`LLSingleton<A>` / `<B>` locks), test `singleton` 5 | Two singletons that build each other nest their locks A->B in one test and B->A in another. LLSingleton nests locks **only on the main thread**: another thread takes a singleton's lock briefly, releases it and hands the construction to the main thread (`llsingleton.h:550-580`). A deadlock needs two threads in opposite orders; only one ever nests. | **False positive.** TSan suppression for this lock pair with this reason. |

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
