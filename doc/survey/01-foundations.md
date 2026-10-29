# 01 — Foundation libraries

This chapter covers the five libraries at the bottom of the viewer's link
graph: `indra/llcommon` (values, strings, logging, threads, coroutines,
events, singletons, memory, profiling), `indra/llmath` (vectors, matrices,
SIMD, volumes and the prim mesher, octrees), `indra/llxml` (expat-based XML
trees and the settings control groups), `indra/llfilesystem` (directories,
the asset disk cache, asset files and the local file thread) and the small
`indra/llmeshoptimizer` wrapper. Together they hold about 120,000 lines of
non-test source, and nearly every other file in `indra/` uses them: `LLSD`
appears in 1,573 files and `LLUUID` in 1,170 of the tree's 3,132 source files.
Four earlier rungs already put Rust under parts of these libraries: LLSD
serialization (rung two), the prim mesher and mesh unpacking in `llvolume.cpp`
(the mesher rung and rung five), and the disk cache, asset file I/O and LFS
reads and writes in `llfilesystem` (rung five). The value types and the
runtime machinery (the `LLSD` class, `LLUUID`, strings, logging, threads,
coroutines, event pumps, singletons, `LLVector4a`) are still C++. That
machinery decides the shape of every later boundary.

Paths are relative to the repository root. Counts come from `grep`/`find`
over the checkout on 2026-10-27 (branch `codex/rust-inventory-model`). No
build, test run or measurement was made for this chapter, so every runtime or
performance statement below is either cited from `doc/rust_port.md` or marked
"not measured".

---

## Scope and size

Command (run from `indra/`; `<d>` is the directory):

```sh
# source (excluding tests/)
find <d> -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.inl' -o -name '*.c' -o -name '*.mm' \) \
     -not -path "<d>/tests/*" | wc -l                                    # files
find <d> -type f \( ...same... \) -not -path "<d>/tests/*" -print0 | xargs -0 cat | wc -l   # lines
# tests
find <d>/tests -type f \( -name '*.cpp' -o -name '*.h' \) | wc -l      # and the same | xargs cat | wc -l
```

| Directory | Source files | Source lines | Test files | Test lines | Main subareas |
|---|---:|---:|---:|---:|---|
| `indra/llcommon` | 266 | 79,109 | 52 | 22,613 | `LLSD` and its serialization, `llsdutil`, `llsdparam`, `llsdjson`; strings (`llstring`, `llformat`, `stringize`); `LLUUID`, `LLDate`, `LLURI`, base64/base32/MD5/CRC/xxhash; logging (`llerror*`); exceptions (`llexception`, `nd/ndexceptions`); `LLApp` and crash hooks; memory (`llmemory`, `llpointer`, `llrefcount`, `llalignedarray`); threads (`llthread`, `llmutex`, `llcond`, `llqueuedthread`, `llworkerthread`, `workqueue`, `threadpool`, `llthreadsafequeue`, `threadsafeschedule`); coroutines (`llcoros`, `lleventcoro`); events (`llevents`, `lleventfilter`, `lleventdispatcher`, `lleventapi`, `lleventtimer`, `llcallbacklist`, `llmainthreadtask`); `LLSingleton`, `LLInstanceTracker`, `LLInitParam`; profiling (`lltrace*`, `llfasttimer`, `llprofiler` for Tracy); `LLProcess`/`LLLeap`; `LLFile`; system info (`llsys`, `llprocessor`). |
| `indra/llmath` | 83 | 28,726 | 17 (+2 fixtures) | 7,503 | Scalar vectors and matrices (`v2math`…`v4coloru`, `m3math`, `m4math`, `llquaternion`); SIMD types (`llvector4a`, `llmatrix3a`, `llmatrix4a`, `llquaternion2`, `llsimdtypes`); geometry (`llbbox`, `llcamera`, `llplane`, `llsphere`, `raytrace`); `lloctree`/`llvolumeoctree`; `llvolume` (5,511 lines: profiles, paths, volume faces, mesh unpack); `llcalc` (boost.spirit expression parser); `llrigginginfo`; `nd/ndoctreelog`. |
| `indra/llxml` | 8 | 7,480 | 1 | 154 | `LLXMLNode` (DOM-like tree, 3,495-line `.cpp`), `LLXmlTree`/`LLXmlParser` (the older SAX-style tree), `LLControlGroup`/`LLControlVariable`/`LLCachedControl` (the settings system). |
| `indra/llfilesystem` | 19 | 4,686 | 2 | 785 | `LLDir` and its per-OS subclasses (path resolution, skin search), `LLDirIterator` (boost.filesystem and boost.regex), `LLDiskCache`, `LLFileSystem` (asset files), `LLLFSThread`. |
| `indra/llmeshoptimizer` | 2 | 497 | 0 | 0 | `LLMeshOptimizer` static wrappers over the meshoptimizer 3p library. |

Notes on the table:

- `.mm` and `.c` are included. No `.c` files exist in these directories;
  `llsys_objc.mm` and `lldir_utils_objc.mm` are the macOS sources.
- `llmath` is mostly math, but its largest file, `llvolume.cpp`, is the
  volume/mesh representation used by the renderer, the object store and
  physics. Chapter-wise it belongs here only because of where it lives.
- Upstream versus fork. Firestorm markers (`// <FS:...>`, counted with
  `grep -rE '<FS(:|>)'`) appear on 86 lines in 34 llcommon files, 20 lines in
  9 llmath files, 23 lines in 3 llxml files and 50 lines in 7 llfilesystem
  files; llmeshoptimizer has none. The `llcommon/nd/` directory (ETW tracing,
  `ndexceptions`, `ndlogthrottle`) and `llcommon/tea.cpp` (OpenSim currency
  support, `<FS:ND>`) are Firestorm-only. Vulkanstorm additions are the
  `vks_*`/`vkstorm_ffi.h` call sites (`llsdserialize.cpp`, `llvolume.cpp`,
  `lldiskcache.cpp/.h`, `llfilesystem.cpp`, `lllfsthread.cpp`), the
  `APP_NAME = "Vulkanstorm"` constant (`indra/llcommon/indra_constants.h:35`),
  and the test oracles and corpora listed in
  [Existing tests and oracles](#existing-tests-and-oracles). Everything else
  is Linden Lab upstream code with small Firestorm patches.

---

## Components

### 1. `LLSD`: the universal value

**Responsibility.** A dynamically typed value: Undefined, Boolean, Integer
(`S32`), Real (`F64`), String, UUID, Date, URI, Binary (`std::vector<U8>`),
Map (string → LLSD) or Array (`indra/llcommon/llsd.h:150`, scalar typedefs
`:179–185`, `enum Type` `:392`). It is the currency of capabilities and HTTP
bodies, login, settings, LEAP, the event system, notifications, UI parameters,
inventory and AIS updates, and the message system's LLSD mode.

**Key files.**
- `indra/llcommon/llsd.h` (545 lines), `llsd.cpp` (1,176 lines): the class.
  It is a pimpl (`class Impl; Impl* impl;`, `llsd.h:447–449`). The `Impl`
  hierarchy has one virtual subclass per type (`llsd.cpp:82`).
- `llsdutil.h/.cpp` (771 / 1,121 lines): `llsd_matches`, `llsd_equals`
  (`llsdutil.h:149,154`), `LLSDMap` builder (`:252`), `llsd_clone`/
  `llsd_shallow` (`:541,546`), pretty printing, and LLSD↔`LLSDParam`
  conversions.
- `llsdserialize.h/.cpp`: notation, binary and XML parsers and formatters.
  Since rung two these are the Rust crate (see section 5).
  `llsdserialize_xml.h` survives as a 35-line shim that only includes
  `llsdserialize.h`; one includer remains (`indra/llmessage/lliohttpserver.cpp:45`).
- `llsdjson.h/.cpp`: LLSD↔JSON through `boost::json` (C++, not ported).
- `llsdparam.h/.cpp`: `LLParamSDParser`, which binds `LLInitParam` blocks to LLSD
  (`llsdparam.h:44`).

**Data flow and ownership.** Copies share the `Impl` (copy-on-write).
`Impl::mUseCount` is a plain `U32` (`llsd.cpp:103`). `Impl::reset` increments
and decrements it without atomics (`llsd.cpp:766–781`), and `shared()`
(`:101`) triggers a copy in `makeMap`/`makeArray` before mutation. Static
singleton Impls (undefined, empty map/array) use a sentinel
`STATIC_USAGE_COUNT` so that they are never freed. Maps are
`std::map<LLSD::String, LLSD, std::less<>>` (`llsd.cpp:434`); arrays are
`std::vector<LLSD>` (`:593`). The public iterator typedefs expose those
standard containers (`llsd.h:369–379`), so the container types are part of
the API.

**Threading.** The header states plainly that LLSD is *less* thread-safe than
STL containers and that sharing a tree across threads is impractical
(`llsd.h:83–141`). That follows from the non-atomic refcount. Code that hands
LLSD to another thread must transfer a deep copy.

**3p dependencies.** None for the class itself. Serialization uses zlib-ng
for `zip_llsd`/`unzip_llsd` (`llsdserialize.h:632–654`) and the Rust crate;
JSON uses boost.json.

### 2. Strings, UUID, time, URIs

- **Strings.** `indra/llcommon/llstring.h` (2,075 lines) and `llstring.cpp`
  (1,974). `LLStringUtilBase<T>` (`llstring.h:259`) holds the case, trim,
  replace, compare and format-map operations, templated over `char` and
  `llwchar`. `LLWString = std::basic_string<llwchar>` (`:464`) is UTF-32 on
  all platforms (Linux has a `char_traits<U16>` specialization at `:64`). The
  `ll_convert` family (`:532–690`) converts between UTF-8, UTF-16 and UTF-32.
  `llformat()` (`llformat.h`) is the printf-style formatter (1,538 call
  sites). `stringize.h` holds the `STRINGIZE` stream macro. `LLStringTable`
  (`llstringtable.h:62`, global `gStringTable` `:96`) interns strings and is
  used by `LLXMLNode` for element and attribute names.
- **UUID.** `LLUUID` is a 16-byte POD (`U8 mData[UUID_BYTES]`,
  `indra/llcommon/lluuid.h:188`) with `generate()` (time and node based),
  `generate(stream)` (MD5 of the input, `lluuid.cpp:394`), per-OS
  `getNodeID` (`:456,514,600`) and a `std::hash` specialization (`lluuid.h:229`).
  Because it is POD, the FFI already passes it as `id.mData`
  (`indra/llfilesystem/lldiskcache.cpp`, `llsdserialize.cpp:738`).
- **Time.** `LLDate` stores `F64 mSecondsSinceEpoch` (`lldate.h:150`) and
  parses and formats ISO 8601. `LLTimer` (`lltimer.h:51`) is the monotonic
  high-resolution timer and `LLFrameTimer` (`llframetimer.h:39`) is the
  per-frame clock (415 uses in 189 files). `llunits.h`/`llunittype.h` give
  typed units (`F32Seconds`, `U32Kilobytes`, …) that run through the
  statistics API.
- **URI.** `LLURI` (`lluri.h`), with `lluriparser` (uriparser 3p).

### 3. Logging: `LL_INFOS` and friends

**Mechanism.** Each `LL_DEBUGS/INFOS/WARNS/ERRS(tags...)` expands to
`lllog()` (`indra/llcommon/llerror.h:397–455`): a function-local
`static LLError::CallSite` caches whether the site is enabled, a
`std::ostringstream _out` collects the message, and `LL_ENDL` calls
`LLError::Log::flush`. If the level is `LEVEL_ERROR`, it then runs
`LLERROR_CRASH`, which writes to `0xDEADBEEFDEADBEEF`
(`llerror.h:432–447`). **`LL_ERRS` is therefore a deliberate crash, not an
exception.** Each macro also opens a Tracy zone (`LL_PROFILE_ZONE_NAMED("lllog")`).

**Sinks.** `LLError::Recorder` (`llerrorcontrol.h:141`), registered with
`addRecorder` (`:203`). Built-in recorders write to a file, stderr, syslog, a
fixed buffer (for the crash report) and the Windows debugger
(`llerror.cpp:88–329`). Settings (levels per tag, class, function or file)
are loaded from `logcontrol.xml` as LLSD.

**Threading.** Log flushing is serialized by function-static
`std::recursive_mutex`es (`llerror.cpp:1298–1309`), so any thread may log.

**Scale.** 11,792 macro uses in 903 files (see [Coupling](#coupling)).

**Existing Rust bridge.** The asset crate logs through a callback
`vks_assets_set_log`. The viewer's sink `vks_assets_viewer_log` maps levels
0/1/≥2 to `LL_DEBUGS/INFOS/WARNS("VksAssets")`
(`indra/llfilesystem/lldiskcache.cpp:55–80`), and the `LLDiskCache` constructor
installs it (`:93`). The LLSD crate has no logging hook; it reports
failure only through return values.

### 4. Errors, exceptions and crash paths

- `LLException` (`indra/llcommon/llexception.h:42`) derives from
  `std::runtime_error` and `boost::exception`. `LLContinueError` (`:58`) marks
  "log and carry on". `LLTHROW(x)` (`:70`) throws with file and line through
  `boost::throw_exception`. `CRASH_ON_UNHANDLED_EXCEPTION` and
  `LOG_UNHANDLED_EXCEPTION` (`:95,101`) are the catch-site helpers; they have
  38 uses in 29 files. `LLTHROW` has 83 uses in 28 files.
- Windows structured exceptions: `LLThread::sehHandle` wraps every
  `LLThread::run()` in `__try/__except` (`llthread.cpp:68,259`).
  `LLCoros::toplevel` does the same for coroutines through `sehandle`
  (`llcoros.cpp:313–371`), which asks BugSplat
  (`LLApp::reportCrashToBugsplat`) first and otherwise converts the SEH into
  `std::exception`.
- **Platform asymmetry (fact, upstream code).** On non-Windows,
  `LLCoros::toplevel` catches every other exception and saves it for
  `LLCoros::rethrow()` on the main fiber (`llcoros.cpp:406–413`). On
  Windows, `cpphandle` (`:334–353`) catches only `Stop` and
  `LLContinueError`, so other exceptions propagate through the fiber.
- `LLApp` (`indra/llcommon/llapp.h:53`) holds the application status
  (`EAppStatus`, `:58–62`; `sStatus` is an `LLScalarCond`, `:295`, so threads
  can wait on a quit), the error handler (`setErrorHandler`, `:254`), the
  crash-reporter hooks (BugSplat on Windows, `bugsplat` CMake include in
  `llcommon/CMakeLists.txt:7`) and `sApplication` (`:336`).
- `llassert`/`llassert_always` (2,011 uses in 366 files) map to `LL_ERRS`;
  `llassert_always_msg` also calls `std::abort()` (`llerror.h:92`, an `<FS:ND>`
  change).

### 5. Memory and allocators

- `ll_aligned_malloc_16`/`ll_aligned_free_16` (`indra/llcommon/llmemory.h:179–200`)
  call `_aligned_malloc` on Windows, `malloc` on macOS and `posix_memalign`
  elsewhere, with Tracy alloc events. `ll_aligned_malloc(size, align)` is at
  `:285`. `LL_ALIGN_NEW` (`:108`) gives a class aligned `operator new`;
  `LLVector4a` uses it (`llvector4a.h:55`).
- **Global `operator new`/`delete` replacement**
  (`indra/llcommon/llcommon.cpp:36–100`) is active only when the build
  enables Tracy memory profiling (`LL_PROFILER_CONFIGURATION >=
  LL_PROFILER_CONFIG_TRACY && TRACY_ENABLE`). It wraps `malloc` with
  `LL_PROFILE_ALLOC`. The default configuration is `LL_PROFILER_CONFIG_FAST_TIMER`
  (`llprofiler.h:74`). No tcmalloc or mimalloc is wired in these
  libraries (grep found none).
- Reference counting: `LLRefCount` (`llrefcount.h:43`, non-atomic `mutable S32
  mRef`) and `LLThreadSafeRefCount` (`:89`, `LLAtomicS32`). `LLPointer<T>`
  (`llpointer.h:57`) is the intrusive smart pointer, with 2,521 uses in 522
  files. `LLAtomicBase` (`llatomic.h:34`) wraps `std::atomic`.
- `LLMemory` (`llmemory.h:444–449`) holds the available and physical memory
  figures that the texture and mesh budgets read. `LLMemoryInfo`/`gSysMemory`
  are in `llsys.h:117`.
- **Boundary rule already in use.** Rung five relies on
  `ll_aligned_malloc_16` returning null instead of throwing, so callbacks
  from Rust are exception-free by construction (`doc/rust_port.md`, rung
  five, "The boundary").

### 6. Threading primitives

| Facility | File | Model |
|---|---|---|
| `LLThread` | `indra/llcommon/llthread.h:40`, `.cpp:157` | Owns a `std::thread*` (`:112`); `threadRun` names the thread (Win32 and Tracy), creates a per-thread `LLTrace::ThreadRecorder` attached to the master recorder, runs `run()` under SEH. Status is a plain flag polled by joiners ("we really need memory barriers here", `llthread.cpp:207`). 29 `public LLThread` subclasses across indra. |
| `on_main_thread()`/`assert_main_thread()` | `llthread.cpp:134–139` | The main thread id is recorded by `LLCommon::initClass()`. |
| `LLMutex` | `llmutex.h:47` | `std::mutex` plus owner id and a count, making it **recursive** (`llmutex.cpp:62,83`). `LLSharedMutex` (`:72`), `LLCondition` (`:140`), `LLMutexLock` (`:156`), `LLMutexTrylock` (`:225`), `LLScopedLock` (`:251`). 116 `LLMutex` and 204 `LLMutexLock` uses. |
| `LLCond`, `LLScalarCond` | `llcond.h` | Condition variable plus value; used by `LLApp::sStatus`. |
| `LLQueuedThread`, `LLWorkerThread`/`LLWorkerClass` | `llqueuedthread.h:45`, `llworkerthread.h:49,119` | Priority request queue on one thread; the texture cache, texture fetch, LFS and image decode threads are built on these (rung five kept them in C++ on purpose). |
| `LL::WorkQueue`, `LL::WorkSchedule` | `workqueue.h:216,280` (base `:38`) | Named, `LLInstanceTracker`-registered queues of `std::function` work; `postTo(target, work, callback)` (`:121,500`) posts and routes the reply back to the origin queue; `waitForResult` (`:134,637`) suspends the calling *coroutine*, so a thread's default coroutine may not call it (`checkCoroutine`). `postToMainLoop` (`:267`). The viewer creates `gMainloopWork("mainloop", 1024*1024)` (`indra/newview/llappviewer.cpp:413`). |
| `LL::ThreadPool` | `threadpool.h:27` | Named pool of `std::thread`s draining one `WorkQueue`; the viewer's `"General"` pool has 3 threads (`llappviewer.cpp:2593`); sizes come from the `ThreadPoolSizes` setting. |
| `LLThreadSafeQueue`, `ThreadSafeSchedule` | `llthreadsafequeue.h:78`, `threadsafeschedule.h` | Bounded MPMC queue with close/interrupt semantics; timed variant. `concurrentqueue.h`/`blockingconcurrentqueue.h` (moodycamel) are vendored. |
| `LLMainThreadTask::dispatch` | `llmainthreadtask.h:44` | From a worker, wraps the callable in an `LLEventTimer` that fires on the next main-loop tick and blocks on a `std::future`; on the main thread it calls directly. Only 5 call sites in 2 files. |

### 7. Coroutines (`LLCoros`) and event pumps

- **`LLCoros`** (`indra/llcommon/llcoros.h:91`) is an `LLSingleton` over
  **Boost.Fiber** (`typedef boost::fibers::fiber coro`, `:113`; fiber
  mutex/condvar headers, `:33–47`). `launch(prefix, callable)` (`:154`)
  starts a named fiber whose body runs in `toplevel` (`llcoros.cpp:378`).
  `LLCoros::Stop` (`:268`, an `LLContinueError`) is thrown by
  `checkStop()` at shutdown. Fibers are cooperative and run on the thread
  that launched them, so in practice almost all of them run on the main
  thread. `launch` is called 171 times (65 files in `newview`, 6 in
  `llmessage`, 5 in `llcommon`, 1 each in `llcorehttp` and
  `viewer_components`). There are 184 suspension calls
  (`suspendUntilEventOn|postAndSuspend|llcoro::suspend|suspendUntilTimeout`).
  The HTTP coroutine adapter (`HttpCoroutineAdapter`, llmessage) suspends a
  fiber on a reply pump. Rung four kept it C++ (`doc/rust_port.md`, rung five
  table, `LLAssetStorage` row).
- **`LLEventPumps`** (`llevents.h:226`, an `LLSingleton`) is a registry of
  named `LLEventPump`s (`:422`). Kinds include `LLEventStream` (`:629`),
  `LLEventMailDrop` (`:660`, which keeps the last event for late listeners)
  and the filters in `lleventfilter.h`. Listeners are `boost::signals2`
  slots that take `const LLSD&` and return `bool` (stop propagation). The
  main loop posts each frame to the `"mainloop"` pump
  (`llappviewer.cpp:1587,1685`). `lleventcoro.h` bridges pumps and fibers
  (`suspendUntilEventOn`, `LLCoroEventPump` `:190`).
- **`LLEventDispatcher`/`LLEventAPI`** (`lleventdispatcher.h:58`,
  `lleventapi.h:48`) map LLSD requests to C++ functions by name. They are
  the LEAP and Lua-style scripting surface. **`LLLeap`** (`llleap.h:33`)
  runs a child process that speaks LLSD over its pipes through `LLProcess`
  (APR based).
- **`LLEventTimer`** (`lleventtimer.h:36`, `LLInstanceTracker`) and
  **`LLCallbackList`/`gIdleCallbacks`** (`llcallbacklist.h:35,78`, 51
  includers) are the per-frame idle hooks that the main loop drives.

**Threading model summary (inference from the above).** The viewer runs a
single-threaded cooperative core: a main thread that drives event pumps,
idle callbacks, event timers and Boost fibers. Around it are worker threads
(`LLThread` subclasses, `LLQueuedThread` instances, the `"General"` pool)
that exchange work through `WorkQueue` and locked queues and return results
to the main loop through `postTo` or `LLMainThreadTask`.

### 8. Singletons, instance tracking and initialization order

- **`LLSingleton<T>`** (`indra/llcommon/llsingleton.h:294`, base `:45`) is
  constructed lazily on first `getInstance()`. A per-class
  `std::recursive_mutex` (`:302–305`) guards construction. The base keeps a
  stack of singletons under construction (`:99–120`); while A's constructor
  or `initSingleton()` is running, any `B::getInstance()` records "A depends
  on B". `LLSingletonBase::deleteAll()` (`:175`) then calls
  `cleanupSingleton()` and deletes in reverse dependency order. The viewer
  calls it once at shutdown (`indra/newview/llappviewer.cpp:2574`).
  Variants: `LLParamSingleton` (`:637`, constructed with arguments by
  `initParamSingleton`; `LLDiskCache` is one, `lldiskcache.h:75`) and
  `LLSimpleton` (`:842`, explicitly created and destroyed, no dependency
  tracking). Usage: 245 `LLSingleton<` occurrences in 198 files, 16
  `LLParamSingleton<`, 14 `LLSimpleton<`.
- **`LLInstanceTracker<T, KEY>`** (`llinstancetracker.h:76`) keeps a
  per-class registry of live instances, optionally keyed by string.
  `WorkQueue`, `ThreadPool`, `LLControlGroup`, `LLControlCache`,
  `LLEventTimer` and `LLLeap` use it.
- `LLInitClass`/`LLDestroyClass` (`llinitdestroyclass.h:133,153`) register
  static init/cleanup callbacks. `llcleanup.h` holds `SUBSYSTEM_CLEANUP`.
- **Explicit start-up.** `LLCommon::initClass()` (`llcommon.cpp`) initializes
  APR, `LLTimer`, the main-thread id and the master `LLTrace::ThreadRecorder`.
  `cleanupClass()` undoes them in reverse. The viewer's own order
  (directories, logging, settings, then the singletons) is in
  `LLAppViewer::init`. That belongs to the application chapter.
- **Global state** is listed in [Coupling](#coupling).

### 9. Settings / control groups (`llxml/llcontrol`)

- `LLControlVariable` (`indra/llxml/llcontrol.h:80`, an `LLRefCount`) holds
  a typed value (`eControlType`: U32, S32, F32, Boolean, String, Vec3, Vec3d,
  Quat, Rect, Col4, Col3, LLSD; `:51–64`) stored as LLSD. It has
  `boost::signals2` validate, commit and sanity signals (`:87–89`), sanity
  rules (`:68–77`) and persistence flags. Firestorm added "Backup Settings"
  (`mCanBackup`, `<FS:Zi>`, `:105–162`).
- `LLControlGroup` (`:200`) is an `LLInstanceTracker` keyed by group name,
  with `std::map<std::string, LLControlVariablePtr, std::less<>> mNameTable`
  (`:205`). `loadFromFile`/`saveToFile` (`:323–325`) go through
  `LLSDSerialize::fromXML`/`toPrettyXML` (`llcontrol.cpp:1083,1107`), so
  settings files are parsed by the Rust XML LLSD parser. The legacy format is
  read through `LLXmlTree` (`:865`).
- `LLCachedControl<T>` (`:424`) / `LLControlCache<T>` (`:339`) give
  hot-path readers a cached copy that a commit signal keeps current.
- The viewer's groups are defined in `newview`: `gSavedSettings`
  (`indra/newview/llviewercontrol.cpp:135`, `"Global"`), plus per-account
  and color groups. `gSavedSettings` alone has 5,252 references in 477
  files.
- **Threading.** No lock in `llcontrol.h` (grep for `mutex|LLMutex` finds
  none). Inference: settings are meant for the main thread. Rung five already
  snapshots the two settings it needs into `VksTexFetchEnv` instead of
  reading them from Rust.

### 10. XML and XUI parsing foundations

- **`LLXMLNode`** (`indra/llxml/llxmlnode.h:94`) is an
  `LLThreadSafeRefCount` DOM node. Children and attributes are multimaps
  keyed by interned `LLStringTableEntry*` (`:74–76`), and children form a
  doubly linked list (`:89–90`). `parseFile`/`parseBuffer`/`parseStream`
  (`:134–145`) drive **expat** directly (`XML_ParserCreate` at
  `llxmlnode.cpp:681,741`). `getLayeredXMLNode` (`:151`) overlays the
  skin-search results (see `doc/rust_port_ui.md` §1.1). It is used for skins,
  XUI and notifications: 293 `LLXMLNodePtr` uses in 51 files.
- **`LLXmlTree`/`LLXmlTreeNode`/`LLXmlTreeParser`** (`llxmltree.h:49,85,197`)
  over **`LLXmlParser`** (`llxmlparser.h:39`, expat SAX, `llxmlparser.cpp:46`)
  form the older read-only tree used for avatar definitions
  (`avatar_lad.xml`) and legacy settings: 190 uses in 22 files.
- **`LLInitParam`** (`indra/llcommon/llinitparam.h`, 2,859 lines,
  plus `.cpp` 467) is the typed parameter-block system behind every XUI
  widget's `Params`. `BaseBlock` (`:784`) and `TypedParam`
  (`:1021,1192,1377,1608`) back `Mandatory<>`/`Optional<>`/`Multiple<>`.
  `ChoiceBlock` is at `:1863`, the abstract `Parser` (`:488`) is
  implemented by `LLXUIParser`/`LLSimpleXUIParser`
  (`indra/llui/llxuiparser.h:96,197`; expat again at `llxuiparser.cpp:1389`)
  and by `LLParamSDParser` (LLSD). It is template-heavy and the parser
  reaches into blocks through `BlockDescriptor`/`ParamDescriptor` offset
  tables (`:613,653`). Uses: 475 `LLInitParam::` occurrences in 197 files
  (`newview` 101 files, `llui` 89).
- **Expat status.** llcommon no longer *uses* expat, but still links
  `ll::expat` for its include directory (comment at
  `indra/llcommon/CMakeLists.txt:312–316`). `llxml` and `llui` use expat
  directly, and the LLSD parity oracle needs it (`CMakeLists.txt:374,379`).

### 11. SIMD math

- `LLQuad` is `__m128` (`indra/llmath/llsimdtypes.h:34`). `llsimdmath.h`
  includes `sse2neon.h` on ARM and `<xmmintrin.h>/<emmintrin.h>` otherwise
  (`:45–52`), then pulls in every SIMD type.
- `LLVector4a` (`llvector4a.h:53`, `alignas(16)`, `LL_ALIGN_NEW`) and its
  inline bodies (`llvector4a.inl`, 114 `_mm_` intrinsics). `LLMatrix4a`
  (`llmatrix4a.h:34`, four `LLVector4a` rows, 18 intrinsics),
  `LLMatrix3a`, `LLQuaternion2`, `LLVector4Logical`, `LLSimdScalar`. SSE2
  only: no `_mm256_`/`__m256` in llcommon, llmath, llrender or newview
  (grep). Uses: 2,030 `LLVector4a` occurrences in 120 files, 153
  `LLMatrix4a` in 34 files.
- Scalar types (`LLVector3` with 6,753 occurrences in 491 files, `LLColor4`
  with 2,463 in 331 files, `LLMatrix4`, `LLQuaternion`) are the API types of
  the message system, the object store and the UI. `LLVector4a` is the
  render and volume hot-path type.
- `LLOctreeNode<T, T_PTR>` (`lloctree.h:89`, `alignas(16)`) is the spatial
  partition used by the renderer and `LLVolumeOctree`. `gOctreeMinSize` is
  a global (`:44`).
- `LLVolume`/`LLVolumeFace`/`LLProfile`/`LLPath` (`llvolume.h:997,823,689,750`)
  are the in-memory volume representation. The Rust mesher writes into their
  16-byte-aligned buffers (section 5).

### 12. Filesystem and cache primitives (`llfilesystem`)

- **`LLDir`** (`indra/llfilesystem/lldir.h`, global `gDirUtilp` `:361`;
  1,211 uses in 193 files) resolves `ELLPath` locations (`:31–46`: user and
  app settings, per-account, cache, logs, skins, chat logs …) and implements
  the skin search path (`setSkinFolder`, `findSkinnedFilenames`, documented in
  `doc/rust_port_ui.md` §1.1). Per-OS subclasses are `lldir_win32/linux/mac`.
  It uses `boost::filesystem` (`lldir.cpp:46`).
- **`LLDirIterator`**: `boost::filesystem` with `boost::regex` glob filters
  (`lldiriterator.cpp:31–46`).
- **`LLDiskCache`** (`lldiskcache.h:75`, an `LLParamSingleton`) is the asset
  cache in `<cache>/<first hex>/sl_cache_<id>_0.asset` (the Firestorm
  subfolder layout, `<FS:Ansariel>`), with Firestorm high/low water marks
  (`<FS:Beq>`) and static-asset prepopulation. Since rung five it is a thin
  wrapper over an opaque `VksDiskCache` (`lldiskcache.cpp:84–104`). The purge
  runs on `LLPurgeDiskCacheThread` against the crate's own lock (comment at
  `:106–112`).
- **`LLFileSystem`** (`llfilesystem.h:37`) reads and writes asset files by
  `(LLUUID, LLAssetType)`. Position bookkeeping stays C++; exists, size, read,
  write, seek, rename, remove and touch are `vks_asset_file_*`
  (`llfilesystem.cpp:84–242`).
- **`LLLFSThread`** (`lllfsthread.h:42`, an `LLQueuedThread`, global
  `sLocal` `:134`): request queue in C++; the read and write bodies are
  `vks_lfs_read`/`vks_lfs_write` (`lllfsthread.cpp:190,195`).
- `LLFile` / `llifstream` / `llofstream` (`indra/llcommon/llfile.h:74,244,289`)
  are the UTF-8-path wrappers over the CRT (504 `LLFile::` uses in 116 files).
  They live in llcommon and are not part of rung five.

### 13. `llmeshoptimizer`

`LLMeshOptimizer` (`indra/llmeshoptimizer/llmeshoptimizer.h:34`) is a set of
static functions over **meshoptimizer**: shadow index buffers, vertex cache
optimization, remap and simplify. Its callers are `llvolume.cpp` (5
references, `cacheOptimize`/remap) and `newview/llmodelpreview.cpp` (6, LOD
generation in the uploader). CMake links `llmath → llmeshoptimizer` and
`llmeshoptimizer → llmath` (`indra/llmath/CMakeLists.txt:108`,
`indra/llmeshoptimizer/CMakeLists.txt:28–31`), a static-library cycle that
CMake tolerates. Rung five explicitly left `cacheOptimize` in C++.

### 14. Profiling and statistics

`LLTrace` (`lltrace.h`, `lltracerecording.h`, `lltracethreadrecorder.h`)
provides typed stat handles (`CountStatHandle`, `SampleStatHandle`,
`EventStatHandle`, `lltrace.h:121–174`) recorded per thread and merged into
the master recorder. `llfasttimer.h` holds the legacy block timers
(`LL_RECORD_BLOCK_TIME`, 119 uses). `llprofiler.h` holds the Tracy macros
(`LL_PROFILE_ZONE_*`, 1,894 uses in 216 files), with a Firestorm `active`
flag for deferred profiling (`<FS:Beq>`, `llprofiler.h:76–82`).
Firestorm's `nd/ndetw` adds Windows ETW events. CI compiles a statistics
type-coverage check (`lltrace_compile_check`, `llcommon/CMakeLists.txt:328–335`).

### 3p dependencies, summarized

| Library | Links (CMake) | Notes |
|---|---|---|
| llcommon | `ll::apr`, `ll::expat` (include directory only), `ll::zlib-ng`, `ll::boost`, `ll::oslibraries`, `ll::tracy`, `ll::sse2neon`, `vkstorm_ffi` (`llcommon/CMakeLists.txt:310–323`) | APR is used by `llapr`, `LLProcess` and APR pools; 11 llcommon files and about 20 files elsewhere (llmessage, llplugin, llimage, llcharacter) call `apr_*`. Most-included boost headers in these four libraries: bind (13), fiber (10), iterator (8), range (7), algorithm (7), spirit (6, llcalc), signals2 (4). |
| llmath | `llcommon`, `llmeshoptimizer`, `ll::sse2neon`, `vkstorm_ffi` | |
| llxml | `llfilesystem`, `llmath`, `llcommon`, `ll::expat` | |
| llfilesystem | `llcommon`, `vkstorm_ffi` | boost.filesystem and boost.regex reach it through `ll::boost`. |
| llmeshoptimizer | `llcommon`, `llmath`, `ll::meshoptimizer` | |

---

## Coupling

### Include sites of key headers

Command (from `indra/`; `newview` column restricts the path to `newview`):

```sh
grep -rlE "#\s*include\s*[<\"]$h[\">]" . --include=*.cpp --include=*.h --include=*.mm --include=*.inl | wc -l
```

These are direct includes only. `linden_common.h` includes `llerror.h`,
`llfile.h` and `llprofiler.h` (`llcommon/linden_common.h:30–63`), and the
viewer's precompiled header `newview/llviewerprecompiledheaders.h:36–81`
pulls in `llsd.h`, `lluuid.h`, `llstring.h`, `llsingleton.h`,
`llinitparam.h`, `llpointer.h`, `llfasttimer.h`, `llmath.h` and more. In
`newview` the *effective* reach of those headers is therefore every
translation unit.

| Header | Files including (all indra) | of which `newview` |
|---|---:|---:|
| `linden_common.h` | 669 | 40 |
| `lluuid.h` | 199 | 125 |
| `llsdserialize.h` | 185 | 114 |
| `llsd.h` | 181 | 58 |
| `llstring.h` | 181 | 75 |
| `llerror.h` | 168 | 51 |
| `llsingleton.h` | 136 | 86 |
| `v3math.h` | 128 | 56 |
| `llsdutil.h` | 123 | 74 |
| `lldir.h` | 117 | 58 |
| `llmath.h` | 115 | 31 |
| `llframetimer.h` | 103 | 58 |
| `v4color.h` | 103 | 43 |
| `lltimer.h` | 99 | 34 |
| `llpointer.h` | 94 | 45 |
| `llcoros.h` | 67 | 46 |
| `llcontrol.h` | 64 | 32 |
| `llfasttimer.h` | 56 | 18 |
| `lleventcoro.h` | 51 | 42 |
| `llcallbacklist.h` | 51 | 47 |
| `llevents.h` | 49 | 22 |
| `llfilesystem.h` | 47 | 36 |
| `llfile.h` | 44 | 24 |
| `llmemory.h` | 41 | 12 |
| `llexception.h` | 41 | 8 |
| `lltrace.h` | 38 | 13 |
| `llrefcount.h` | 35 | 7 |
| `llthread.h` | 31 | 8 |
| `llvolume.h` | 31 | 20 |
| `llinitparam.h` | 26 | 7 |
| `llapp.h` | 24 | 4 |
| `lldate.h` | 24 | 11 |
| `llvector4a.h` | 22 | 10 |
| `llmatrix4a.h` | 21 | 11 |
| `llxmlnode.h` | 20 | 7 |
| `llmutex.h` | 19 | 0 |
| `llhandle.h` | 19 | 14 |
| `workqueue.h` | 17 | 4 |
| `llinstancetracker.h` | 16 | 2 |
| `llxmltree.h` | 15 | 8 |
| `llsdparam.h` | 12 | 7 |
| `lldiskcache.h` | 8 | 4 |
| `llworkerthread.h` | 8 | 5 |
| `llprofiler.h` | 8 | 1 |
| `threadpool.h` | 7 | 1 |
| `lloctree.h` | 6 | 4 |
| `lllfsthread.h` | 5 | 2 |
| `llmainthreadtask.h` | 3 | 0 |
| `llqueuedthread.h` | 3 | 0 |
| `llmeshoptimizer.h` | 3 | 1 |

### Symbol usage (occurrences and files)

Command: `grep -rEo "<pattern>" . --include=*.cpp --include=*.h --include=*.mm --include=*.inl | wc -l` for occurrences,
`grep -rlE` for files, from `indra/` (3,132 source files in total by the
same globs).

| Pattern | Occurrences | Files |
|---|---:|---:|
| `\bLLSD\b` | 19,908 | 1,573 |
| `\bLLUUID\b` | 16,792 | 1,170 |
| `\bLL_(INFOS\|WARNS\|DEBUGS\|ERRS)\b` | 11,792 | 903 |
| `\bLLVector3\b` | 6,753 | 491 |
| `\bgSavedSettings\b` | 5,252 | 477 |
| `\bLLPointer<` | 2,521 | 522 |
| `\bLLColor4\b` | 2,463 | 331 |
| `\bLLVector4a\b` | 2,030 | 120 |
| `\bllassert(_always)?\(` | 2,011 | 366 |
| `\bLL_RECORD_BLOCK_TIME\b\|\bLL_PROFILE_ZONE_` | 2,013 | 236 |
| `\bLLStringUtil::` | 1,897 | 442 |
| `\bllformat\(` | 1,538 | 311 |
| `\bgDirUtilp\b` | 1,211 | 193 |
| `\bLLFile::` | 504 | 116 |
| `\bLLInitParam::` | 475 | 197 |
| `\bLLTrace::` | 447 | 92 |
| `\bLLHandle<` | 421 | 170 |
| `\bLLFrameTimer\b` | 415 | 189 |
| `\bLLCoros\b` | 375 | 108 |
| `\bLLDate\b` | 365 | 102 |
| `\bLLControlGroup\b` | 296 | 69 |
| `\bLLXMLNodePtr\b` | 293 | 51 |
| `\bLLSingleton<` | 245 | 198 |
| `\bLLMutexLock\b` | 204 | 28 |
| `\bLLFileSystem\b` | 194 | 47 |
| `\bLLEventPumps\b` | 192 | 61 |
| `\bLLXmlTree\b` | 190 | 22 |
| `\bLLMatrix4a\b` | 153 | 34 |
| `\bLLMutex\b` | 116 | 50 |
| `\bLL::WorkQueue\b` | 96 | 29 |
| `\bLLTHROW\(` | 83 | 28 |
| `\bLLInstanceTracker<` | 64 | 42 |

Distribution of files that mention `LLSD` by top-level directory (`grep
-rlE '\bLLSD\b' ... | awk -F/ '{print $2}' | sort | uniq -c`): newview
1,117; llui 143; llcommon 84; llmessage 68; llmath 29; llinventory 23;
llprimitive 22; llwindow 11; llcorehttp 8; llplugin 7; llaudio 6. The same
for `LLInitParam::|Mandatory<|Optional<|Multiple<`: newview 101, llui 89,
llcommon 7, llrender 1. For `LLCoros|llcoro::`: newview 84, llcommon 18,
llmessage 7, plus single digits elsewhere.

### Link-level dependents

Approximate method: CMake files whose `target_link_libraries` or
`test_libs` lines name the library (`grep -rlE` over `CMakeLists.txt`,
excluding the library's own). Multi-line link lists make this a lower
bound.

- `llcommon`: every library and executable (llappearance, llaudio,
  llcharacter, llcorehttp, llcrashlogger, llfilesystem, llimage,
  llimagej2coj, llinventory, llkdu, llmath, llmeshoptimizer, llmessage,
  llphysicsextensionsos, llplugin, llprimitive, llrender, llui, llwindow,
  llxml, newview, viewer_components, test, integration_tests,
  linux_crash_logger).
- `llmath`: the same set minus llimagej2coj, llkdu, llcrashlogger.
- `llxml`: llappearance, llcharacter, llcrashlogger, llinventory, llplugin,
  llprimitive, llrender, llui, llwindow, newview, viewer_components.
- `llfilesystem`: llappearance, llaudio, llcharacter, llcrashlogger,
  llimage, llinventory, llmessage, llrender, llui, llwindow, llxml, newview.
- `llmeshoptimizer`: llmath, newview.

### Hot cross-library dependencies

- `llxml → llfilesystem → llcommon` and `llxml → llmath`: the settings
  system (`llxml`) sits *above* the filesystem library, so anything that
  reads settings pulls in `LLDir`.
- `llmath ↔ llmeshoptimizer` cycle (above).
- `llcommon` is the lowest library that links `vkstorm_ffi`, so every
  binary in the tree links the Rust static library (as `rust_port.md`
  rung two states).
- `llsdserialize.h` has more includers than `llsd.h` (185 against 181).
  Inference: much of the tree serializes LLSD (HTTP bodies, caches,
  settings), which makes the rung-two boundary one of the most exercised in
  the viewer.

### Global state in these libraries

| Global | Declared | Notes |
|---|---|---|
| `gDirUtilp` (`LLDir*`) | `indra/llfilesystem/lldir.h:361` | Process-wide paths and skin search; 1,211 uses. |
| `gStringTable` | `indra/llcommon/llstringtable.h:96` | Interned names for `LLXMLNode`. |
| `gIdleCallbacks` | `indra/llcommon/llcallbacklist.h:78` | Per-frame idle callbacks. |
| `gSysCPU`, `gSysMemory` | `indra/llcommon/llsys.h` | CPU and memory info. |
| `gOctreeMinSize`, `gDebugGL` | `indra/llmath/lloctree.h:44`, `llvolume.h:209` | Render tuning flags living in llmath. |
| `LLApp::sApplication`, `LLApp::sStatus` | `indra/llcommon/llapp.h:336,295` | Application object and quit state. |
| Main-thread id | `indra/llcommon/llthread.cpp:134–139` | Set by `LLCommon::initClass`. |
| `LLLFSThread::sLocal` | `indra/llfilesystem/lllfsthread.h:134` | The LFS thread. |
| `LLDiskCache::sCacheDir` | `indra/llfilesystem/lldiskcache.cpp:50` | |
| `LLError` settings and recorders | `indra/llcommon/llerror.cpp` | Behind function-static recursive mutexes. |
| Every `LLSingleton`, `LLInstanceTracker` registry, `LLEventPumps` | — | Lazily constructed process globals. |
| `gSavedSettings` and the other control groups | `indra/newview/llviewercontrol.cpp:135` | Defined in newview but typed here. |

---

## Rust port status

### What is Rust today in these libraries

FFI call sites found with
`grep -rnoE '\bvks_[a-z0-9_]+' llcommon llmath llxml llfilesystem llmeshoptimizer`:

| File | `vks_*` functions called | Rung (per `doc/rust_port.md`) |
|---|---|---|
| `indra/llcommon/llsdserialize.cpp` | `vks_llsd_parse`, `vks_llsd_doc_nodes`, `vks_llsd_doc_bytes`, `vks_llsd_doc_free`, `vks_llsd_formatter_new`, `vks_llsd_formatter_finish`, `vks_llsd_fmt_{map_begin,map_end,array_begin,array_end,key,undef,boolean,integer,real,real_text,uuid,string,date,uri,binary}`, `vks_llsd_escape` (22 distinct) | Rung two, LLSD serialization |
| `indra/llmath/llvolume.cpp` | `vks_profile_params`, `vks_path_params`, `vks_volume_profile_generate`, `vks_volume_profile_num_points`, `vks_volume_path_generate`, `vks_volume_path_num_points`, `vks_volume_sweep`, `vks_volume_face_sizes`, `vks_volume_face_build`, `vks_volume_tangents` (mesher rung); `vks_mesh_unpack`, `vks_mesh_faces_count`, `vks_mesh_face`, `vks_mesh_faces_free` (rung five) | Prim mesher; asset pipeline (mesh LOD unpack) |
| `indra/llfilesystem/lldiskcache.cpp/.h` | `vks_diskcache_{new,purge,clear,info,metadata_to_filepath,remove_old_vfs_files,add_static,skip_list_clear,set_max_size,set_high_water,set_low_water}`, `vks_assets_set_log`, `vks_ffi_string` (C++ helper in the header) | Rung five |
| `indra/llfilesystem/llfilesystem.cpp` | `vks_asset_file_{exists,size,read,write,seek,rename,remove,touch}` | Rung five |
| `indra/llfilesystem/lllfsthread.cpp` | `vks_lfs_read`, `vks_lfs_write` | Rung five |
| `indra/llxml`, `indra/llmeshoptimizer` | none | — |

What each rung reports as ported (from `doc/rust_port.md`):

- **Rung two (LLSD serialization)**: the notation, binary and XML parsers and
  formatters and both `escapeString`s. Kept C++: the `LLSD` class itself
  ("Out of scope by design; everything in the viewer holds `LLSD`"), the
  stream adapters and the `LLSD` builder, `deserialize` header sniffing,
  `realFormat` pre-formatting through `llformat`, and the zlib helpers
  (`rust_port.md`, "Keep-or-port judgement" table, lines 88–100). It reports
  parse speedups of 1.0–6.5× and format speedups of 1.9–27×, measured with
  `llsdserialize_tool bench 20` (`rust_port.md:130–178`). Those are the
  doc's measurements; this chapter did not repeat them.
- **Mesher rung**: profile and path generation, the sweep, the face
  builders and the tangents; `LLVolume`, `LLProfile`, `LLPath` and
  `LLVolumeFace` stay the in-memory types (`rust_port.md:261–300`).
- **Rung five**: `LLDiskCache` naming, purge, scans and clear;
  `LLFileSystem` operations and `LLLFSThread` reads and writes; mesh LOD
  unpacking in `LLVolume::unpackVolumeFaces` (`rust_port.md:1229–1294`).
  Kept C++: the singleton, the API, the static-asset prepopulation, the
  position bookkeeping and the request thread.

### How it is wired

- **Build.** `indra/cmake/Rust.cmake` imports the prebuilt static library
  `vkstorm_ffi` from the `vkstorm-ffi` autobuild package. The pinned
  archives in `autobuild.xml` are tag `ffi-v0.1.0`, version string
  `0.1.0.1b183c77e7`, `full` feature set, windows64 and linux64. Its
  interface carries the header include directory, the native system
  libraries from `vkstorm_ffi.native-libs.txt`, and `ll::libcurl` for the
  HTTP rung (`Rust.cmake:58–102`). The package path refuses OpenSim and
  non-AVX2 configurations (`:60–65`); `-DVKS_CORE_RUST_DIR=…` builds from a
  checkout through corrosion v0.6.1 (`:18–56`). `llcommon`, `llmath` and
  `llfilesystem` each link `vkstorm_ffi` directly
  (`llcommon/CMakeLists.txt:322`, `llmath/CMakeLists.txt:108`,
  `llfilesystem/CMakeLists.txt:58`).
- **Header location (doc drift).** `vkstorm_ffi.h` is no longer in the
  viewer tree. Neither `indra/llcommon/vkstorm_ffi.h` nor `indra/rust/`
  exists. The header comes from the package
  (`build-*/packages/include/vkstorm_ffi.h`, 1,310 lines in the build
  directory inspected read-only). `rust_port.md` rung one and two text
  still says it is "mirrored by `indra/llcommon/vkstorm_ffi.h`", and the
  header's own banner still names `indra/rust/vkstorm-ffi/src/*.rs`. The
  "Build integration" section notes the October 2026 move, so only the rung
  text is stale.
- **Boundary patterns already established here:**
  1. *Pull source via C callbacks, flat result buffer.*
     `LLSDParser` hands Rust a `VksLlsdSource` (two callbacks over the
     `std::streambuf`, `llsdserialize.cpp:287–330`) in BULK, LINE or EXACT
     mode (`:285`). Rust returns a `VksLlsdDoc` of 16-byte tagged nodes plus
     a byte arena (`VksLlsdNode {u8 kind; u8 pad[3]; u32 len; u64 payload}`
     in the package header), which `buildLLSD` turns into an `LLSD` tree
     (`:415`).
  2. *Push formatter.* C++ walks the `LLSD` and makes one `vks_llsd_fmt_*`
     call per node (`walkLLSD`, `:689`); Rust writes back to the `ostream`
     through a sink callback in 64 KiB chunks (`format`, `:925–950`).
  3. *C++ exceptions are fenced.* An exception thrown by the stream inside a
     callback is caught, stored in `std::exception_ptr` and rethrown after
     the Rust call returns (`llsdserialize.cpp:292–315`, `:945–948`).
     Rust panics are caught by `catch_unwind` and reported as failure
     (`rust_port.md`, rungs two and five).
  4. *Opaque handles plus plain functions over byte slices.* For example,
     `VksDiskCache*` and `vks_asset_file_*(path, len, …)`. Strings come back
     through `vks_ffi_string(fill)`, a C++ inline in the header that retries
     with a larger buffer.
  5. *Rust-to-C++ logging* through an installed function pointer
     (`vks_assets_set_log`), for the asset crate only.
  6. *POD sharing.* `LLUUID::mData` (16 bytes) passes by pointer;
     `LLPath::PathPt`/`LLProfile::Face` layouts are shared and
     `static_assert`ed (`rust_port.md:287–288`).

### What remains C++ in these libraries

Everything else: the `LLSD` class and `llsdutil`, `llsdjson`, `llsdparam`;
strings, `LLUUID` generation, `LLDate`, `LLURI`; logging; exceptions and
crash handling; memory and refcounting; all threading, coroutine, event and
singleton machinery; `LLInitParam`; `LLTrace`/profiling; `LLProcess`/
`LLLeap`; `LLFile`; all of `llxml` (expat-based trees, settings); `LLDir` and
`LLDirIterator`; SIMD and scalar math; octrees; the parts of `llvolume.cpp`
listed as kept (sculpt map vertices, `cacheOptimize`, silhouettes,
intersection, `LLVolumeFace` memory management); `llmeshoptimizer`.

A note on recursion: `rust_port.md` says the parsers, formatters and the
`LLSD` builder are iterative, so depth cannot overflow the stack. That
holds for the Rust side and the builder. The C++ formatting walk
`walkLLSD` is recursive (`llsdserialize.cpp:689–716`), so formatting a very
deep `LLSD` still recurses on the C++ stack, as the original formatters did.
Not measured. Inference: in practice only a pathological in-memory tree
would be deep enough to matter, because parsed input is depth-limited.

---

## Port assessment

The judgement criteria are those `rust_port.md` applies: port where the work
is self-contained and measurable, and keep C++ where the code *is* the
boundary or is the viewer's scheduling and lifetime glue. "Defer" means it
should move together with a later rung's owner, or only once that rung needs
it.

| Component | Verdict | Reasons |
|---|---|---|
| `LLSD` value class | **Keep C++ now; plan a Rust-side mirror type, not a replacement** | 19,908 occurrences in 1,573 files. The API exposes `std::map`/`std::vector` iterators (`llsd.h:369–379`) and implicit conversions from C++ types, so a wrapper with an unchanged interface cannot hold a Rust value without re-materializing those containers. The refcount is non-atomic, so LLSD cannot be shared with Rust threads anyway. Rung two's node-buffer pattern already lets Rust produce and consume LLSD-shaped data without owning `LLSD`. |
| LLSD serialization | **Done (rung two)** | Remaining C++ is the adapters, sniffing, `realFormat` and zlib, and `rust_port.md` justifies keeping each. `llsdjson` (boost.json) is the one sibling format still in C++: a small port candidate if a later rung (for example web or LEAP) needs JSON on the Rust side. |
| `llsdutil` (`llsd_matches`, `llsd_equals`, clone/shallow) | Keep C++ | Operates on in-memory `LLSD`; nothing to gain while `LLSD` is C++. |
| Strings (`LLStringUtil`, `ll_convert`, `llformat`) | Keep C++ (port per use) | Header-template code used in 442+ files. UTF conversion at a boundary should use Rust's `str`/`char` on the Rust side and pass UTF-8 byte slices, which the existing ABI already does. `llformat`'s `printf` semantics are load-bearing (rung two kept `realFormat` in C++ for byte identity). |
| `LLUUID` | Keep C++ type; share as POD | Already crosses as `const uint8_t[16]`. A Rust `Uuid([u8;16])` mirror plus parity of the string form and `generate(stream)` (MD5) is enough for the inventory and object-store rungs. |
| `LLDate`, `LLTimer`, `LLFrameTimer` | Keep C++; pass values | An `F64` epoch crosses trivially. Rust code that needs "now" should take a host clock, as rung five does (`LLTextureCache::sClock`). |
| Logging (`LL_*`) | Keep C++; **generalize the Rust log callback** | Macros are compile-time call sites in C++. The Rust side needs one viewer-wide sink (today only `vks_assets_set_log`). A prerequisite for later rungs is a single `vks_log_set_sink` (name hypothetical) with tags, so every crate logs into the same recorders and `logcontrol.xml` levels. Rust must never trigger `LL_ERRS`'s deliberate crash from inside a Rust frame; it should report and let C++ decide. |
| Exceptions and crash paths | Keep C++ | Unwinding across the ABI is undefined behaviour (the comment at `llsdserialize.cpp:292–296`). Every callback that C++ hands Rust must be `noexcept` in effect or catch into `exception_ptr`, as rungs two and five do. SEH, BugSplat and `LLApp` stay with the C++ application shell. |
| Memory (`ll_aligned_malloc_16`, `LLRefCount`, `LLPointer`) | Keep C++ | The renderer and volume buffers are allocated here and written in place by Rust (the mesher rung). A Rust allocator swap is out of scope. Note that the Tracy-enabled build replaces global `operator new`, but Rust allocations go through Rust's global allocator and do not appear in Tracy memory views. Inference; not checked. |
| `LLThread`, `LLMutex`, `LLQueuedThread`, `LLWorkerThread`, `WorkQueue`, `ThreadPool` | **Keep C++ (rung five's verdict applies)** | Rung five measured and judged this: the main loop depends on these threads' pause, wake, priority and shutdown semantics, and Rust threads would add a second scheduler. Rust work should run *on* these threads, called from C++ workers. Prerequisite for later rungs: Rust state touched from several C++ threads must carry its own lock inside the crate (the `VksTexFetcher` pattern). |
| `LLCoros` (Boost.Fiber) and `lleventcoro` | **Keep C++; never call into a fiber from Rust** | A fiber switch while a Rust frame is on the stack is legal for the CPU but breaks any Rust assumption about thread-locals and borrows held across a call. Inference: it is also untested in this codebase. The rule so far (rung four kept `HttpCoroutineAdapter`, rung five kept `assetRequestCoro`) is that coroutines stay C++ and Rust sees only completed results. The inventory (AIS) and UI rungs are coroutine-heavy (65 newview files launch coroutines), so this rule fixes their shape: Rust as a synchronous state machine, driven by the C++ coroutine. |
| `LLEventPumps`, `LLEventDispatcher`, `LLEventAPI`, `LLLeap` | Keep C++ (defer) | They are LLSD- and `boost::signals2`-typed fan-out, mostly glue. LEAP's wire format is already Rust (notation and binary LLSD). Defer to the UI rung if at all. |
| `LLMainThreadTask` | Keep C++ | 5 call sites. Its blocking `future.get()` on a worker must never run on a thread that Rust holds a lock on. |
| `LLSingleton` and initialization order | Keep C++ | The dependency-ordered `deleteAll` is how shutdown order is decided. Rust objects should be owned by a C++ singleton or member (as `LLDiskCache` owns `VksDiskCache`), so their lifetime follows the existing order. Rust `static` state needs an explicit teardown entry point if it holds OS resources. |
| `LLInitParam` | **Defer to the UI rung** | 475 uses, deeply templated, reached through descriptor offset tables. Porting it is meaningful only together with LLUI and XUI (see `doc/rust_port_ui.md` §3). |
| `LLControlGroup`, `LLCachedControl` | Keep C++; **snapshot settings into Rust** | 5,252 `gSavedSettings` references, signals into C++ listeners, no locking. Pattern for later rungs: pass the needed values in an environment struct on each call, or register a C++ commit listener that pushes changes into the crate. Do not give Rust a reference to `LLControlVariable`. File I/O already uses the Rust XML parser. |
| `LLXMLNode`, `LLXmlTree`, `LLXmlParser` (expat) | **Port the tokenizer later; defer the trees to the UI rung** | Rung two already owns an expat-compatible XML tokenizer in `vkstorm-llsd`. Reusing it under `LLXmlParser` would remove expat from `llxml` (expat would still remain in `llui`'s `LLXUIParser` and `llvoicevivox.cpp`). The trees themselves (`LLXMLNodePtr`, interned names) are UI-rung material. |
| SIMD math (`LLVector4a`, `LLMatrix4a`) | **Keep C++ types; mirror per hot path** | 2,030 uses in 120 files; header-inline SSE. The mesher rung reproduced the lane-for-lane semantics in Rust where needed (`rust_port.md:289–295`). The renderer rung must keep shared buffers 16-byte aligned with identical lane layout. |
| Scalar math (`LLVector3`, `LLColor4`, `LLQuaternion`, `LLMatrix4`) | Keep C++; `#[repr(C)]` mirrors at boundaries | They are the API types of the message system, object store and UI. Each later rung needs layout-asserted mirrors (as the mesher did for `PathPt`). |
| `lloctree` | Defer to the renderer rung | A template over renderer-owned element types with listener callbacks. |
| `llvolume` remaining C++ | Defer (object store and renderer) | Sculpt vertices, silhouettes and intersection follow their consumers. |
| `LLDir` / `LLDirIterator` | Keep C++ | Path policy and skin search are data the UI and asset rungs pass to Rust as strings, as `LLDiskCache` does. |
| `LLDiskCache`, `LLFileSystem`, `LLLFSThread` | **Done (rung five)** | Thin C++ wrappers remain by design. |
| `LLFile` | Keep C++ | CRT and UTF-8 path wrapper; Rust uses `std::fs`. |
| `llmeshoptimizer` | Keep C++ (3p) | Thin wrapper over a C++ library; rung five kept `cacheOptimize`. A Rust `meshopt` binding would only make sense for an uploader port. |
| `LLTrace`/Tracy | Keep C++ | Instrumentation; Rust crates could emit Tracy zones through their own client (not present, not checked). |

### Concrete boundary candidates (in rough order of value)

1. **A shared Rust-side LLSD value and a two-way node buffer.** Rung two's
   `VksLlsdDoc` goes one way (Rust → C++). The inventory rung (AIS JSON/LLSD
   updates, `llinventory` `fromLLSD`/`asLLSD`) and the object store will
   want Rust to *consume* an in-memory `LLSD` without a text round trip.
   Candidate: `vks_llsd_doc_from_nodes` plus a C++ `flattenLLSD`, the
   inverse of `buildLLSD`, so a C++ `LLSD` crosses as one flat buffer.
   Prerequisite: decide whether keys and strings are copied or borrowed for
   the call's duration (borrowing is safe only while the C++ `LLSD` is not
   mutated, because copy-on-write can reallocate). Inference: the branch
   name `codex/rust-inventory-model` and the CI test list
   (`INTEGRATION_TEST_llinventory_parity`, `…llinventorymodel_parity`,
   `…llaisupdate_parity`) suggest this need is near. The pinned
   `vkstorm_ffi.h` has no `vks_inv*` symbols yet (grep count 0).
2. **A viewer-wide Rust log sink**, as above, so every crate after rung five
   logs consistently.
3. **An `LLXmlParser` backend on the Rust XML tokenizer** (drops expat from
   `llxml`; small surface: start, end and character callbacks).
4. **A `#[repr(C)]` math mirror module** (`LLVector3`, `LLVector4a`,
   `LLQuaternion`, `LLMatrix4a`) with `static_assert` and `const` layout
   checks, shared by the object-store and renderer rungs.

### Which later rungs depend on decisions here

| Decision | Inventory | Object store | Renderer | UI |
|---|---|---|---|---|
| LLSD stays C++ and crosses as node buffers | **Yes**: AIS and inventory LLSD | Yes: extra params, materials | Little | Yes: notifications, params |
| Coroutines stay C++; Rust is synchronous | **Yes**: AIS and fetch coroutines | Some | No | Yes |
| Threads stay C++; Rust state locked in-crate | Yes | **Yes**: decode and update threads | **Yes** | No |
| Settings snapshot pattern | Yes | Yes | **Yes**: many render settings | **Yes** |
| `LLUUID`/math POD mirrors | **Yes** | **Yes** | **Yes** | Some |
| `LLInitParam`/XUI | No | No | No | **Decides the rung** |
| SIMD layout identity | No | Some | **Yes** | No |

---

## Existing tests and oracles

### llcommon

- **Parity and oracle (Vulkanstorm):**
  `indra/llcommon/tests/llsdserialize_parity_test.cpp` (601 lines) against
  `tests/llsdserialize_oracle.cpp` (2,835 lines, the former C++
  implementation in namespace `llsd_oracle`, linking expat) over
  `tests/llsdserialize_corpus.cpp` (936 lines). The target
  `INTEGRATION_TEST_llsdserialize_parity` compiles in
  `VKS_LLSD_APP_SETTINGS_DIR` so that every LLSD XML file under
  `newview/app_settings` is in the corpus (`llcommon/CMakeLists.txt:372–376`).
  `rust_port.md` describes 10 groups (`:247–256`).
- `tests/llsdserialize_tool.cpp`: `bench` and `dump` (fixture generator for
  the crate's 3,638-case `tests/fixtures.rs`). It is built with
  `LL_TESTS=ON` and not run (`CMakeLists.txt:378–385`).
- **Upstream integration tests** (`LL_ADD_INTEGRATION_TEST`,
  `llcommon/CMakeLists.txt:347–395`): apply, bitpack, classic_callback,
  commonmisc, lazyeventapi, llbase64, llcond, lldate, lldeadmantimer,
  lldependencies, llerror, lleventcoro, lleventdispatcher, lleventfilter,
  llframetimer, llheteromap, llinstancetracker, llleap, llmainthreadtask,
  llpounceable, llprocess, llprocessor, llprocinfo, llrand, llsdserialize
  (the original 71-test suite, now exercising Rust), llsingleton,
  llstreamqueue, llstring, llsys, lltrace, lltreeiterators, llunits, lluri,
  stringize, threadsafeschedule, tuple, workqueue. `llexception_test.cpp`
  exists but is deliberately not registered (comment at `:397–399`).
  Allocator test files (`llallocator_test.cpp`,
  `llallocator_heap_profile_test.cpp`, `llmemtype_test.cpp`, `lllazy_test.cpp`)
  exist but are not in this list.
- `lltrace_compile_check` (statistics type coverage), built in CI
  (`.github/workflows/build.yaml:195`).

### llmath

- **Oracle corpus (Vulkanstorm):** `indra/llmath/tests/llvolume_test.cpp`
  (1,250 lines) against `tests/fixtures/llvolume_corpus.vksv` (Windows) and
  `llvolume_corpus.linux.vksv` (Linux), each 8,389,298 bytes, with different
  contents (md5 `bdef56f8…` against `fa227c61…`). The test selects the
  corpus by platform (`llvolume_test.cpp:42–54`). It covers 881 cases per
  `rust_port.md:271`. Environment hooks are `VKS_VOLUME_DUMP`,
  `VKS_VOLUME_BENCH` and `VKS_VOLUME_BENCH_OUT`.
- Upstream unit tests: llbboxlocal, llmodularmath, llrect, v2math, v3color,
  v4color, v4coloru. Integration tests: alignment, llbbox, llquaternion,
  mathmisc, m3math, v3dmath, v3math, v4math, xform
  (`llmath/CMakeLists.txt:113–142`). No test targets `LLVector4a` or
  `LLMatrix4a` arithmetic directly except `alignment_test.cpp`.
- Mesh unpack parity lives outside this library:
  `INTEGRATION_TEST_llmeshunpack_parity` (newview).

### llxml

- `indra/llxml/tests/llcontrol_test.cpp` (154 lines,
  `INTEGRATION_TEST_llcontrol`). No test of `LLXMLNode` or `LLXmlTree`.

### llfilesystem

- `tests/lldir_test.cpp` (integration) and `tests/lldiriterator_test.cpp`
  (unit). No viewer-side test exercises the Rust-backed `LLDiskCache`
  purge, `LLFileSystem` or `LLLFSThread`: grep for
  `LLDiskCache|LLFileSystem|LLLFSThread` in `newview/tests` and
  `llfilesystem/tests` finds nothing. `lltexturecache_parity_test.cpp`
  calls `vks_diskcache_metadata_to_filepath` directly (`:799`). Per
  `rust_port.md:1377–1384`, the disk-cache purge and naming are covered by
  the crate's 44 unit tests in vkstorm-core-rust. That repository was not
  inspected for this chapter.

### What CI runs

`build.yaml:211–225` builds a focused list. Each `LL_ADD_INTEGRATION_TEST`
runs as a POST_BUILD step (`indra/cmake/LLAddBuildTest.cmake:277–281`), so
building a target runs it. From these libraries the list contains only
`INTEGRATION_TEST_lltrace`, `INTEGRATION_TEST_llsdserialize_parity` and
`INTEGRATION_TEST_llvolume`. The original `INTEGRATION_TEST_llsdserialize`,
the threading, coroutine and workqueue tests, `llcontrol`, `lldir` and the
math tests are not in CI's target list. Inference: those run only when a
developer builds them.

---

## Risks and open questions

1. **LLSD thread-safety versus Rust worker threads.** The `LLSD` refcount
   is non-atomic (`llsd.cpp:103,766–781`). Any future boundary that hands an
   `LLSD` (or a borrowed view of one) to Rust code that runs on another
   thread is a data race, unless the C++ side deep-copies first or flattens
   to a node buffer on the owning thread. This needs a stated rule before
   the inventory rung.
2. **Fibers and Rust frames.** No current boundary calls into C++ code that
   can suspend a Boost fiber from inside a Rust call. A host callback that
   reaches `llcoro::suspend…`, `WorkQueue::waitForResult` or
   `LLMainThreadTask::dispatch` would suspend or block with Rust frames
   live. Open: should the ABI document a "callbacks must not suspend or
   block" contract, and can it be checked (for example by asserting
   `LLCoros::on_main_coro()` state in debug builds)?
3. **`LL_ERRS` is a crash, not an exception.** A C++ host callback that
   logs `LL_ERRS` crashes the process with Rust frames on the stack. The
   intended behaviour matches, but crash reports will show Rust frames.
   Open: are Rust symbols/PDBs in the package available to BugSplat? Not
   checked.
4. **Windows coroutine exception asymmetry** (`llcoros.cpp:334–353` against
   `:406–413`). This upstream behaviour differs by platform. Any parity test
   that throws from a coroutine will behave differently on Windows and
   Linux.
5. **Doc and header drift.** `rust_port.md` (rungs one, two, mesher, five)
   still names `indra/llcommon/vkstorm_ffi.h`, `indra/rust/include/…` and
   `indra/rust/vkstorm-ffi/src/*.rs`, none of which exist in this checkout.
   The package header's banner has the same stale paths. Survey readers
   should use the package header and vkstorm-core-rust's
   `crates/vkstorm-ffi`.
6. **CI coverage gap.** The foundation tests most likely to catch a
   boundary-adjacent regression are not in CI's focused list: the original
   `llsdserialize` suite (Python interop), `lleventcoro`, `workqueue`,
   `llmainthreadtask`, `llsingleton`, `llerror`. Open: add them, or confirm
   they are run elsewhere.
7. **No viewer-side oracle for the Rust disk cache and asset file I/O.**
   Only crate tests and the texture-cache parity test's naming check cover
   them (see above). Whether the C++ position bookkeeping in
   `LLFileSystem` matches the old behaviour on short reads, appends and
   seeks past end is covered only if the crate tests transcribe it.
   Undetermined here.
8. **Recursive C++ walk in LLSD formatting** (`walkLLSD`). This is fine for
   parsed data, but the "iterative" claim in `rust_port.md` covers only the
   Rust side and the builder.
9. **expat still linked by llcommon** for its include directory
   (`llcommon/CMakeLists.txt:312–316`). Header hygiene: an accidental
   `#include <expat.h>` in llcommon would compile silently.
10. **Static-library cycle `llmath ↔ llmeshoptimizer`.** It is harmless
    today, but a reorganisation that turns either into a shared library or
    a separate crate boundary must break it first.
11. **Tracy memory accounting misses Rust allocations** (inference: Rust
    uses its own global allocator, which the `operator new` override in
    `llcommon.cpp` does not see). Memory budgets that read
    `LLMemory`/`LLMemoryInfo` see process totals, so they are unaffected.
    Tracy views are partial. Not checked.
12. **Open: where should a Rust `Uuid`/`Date`/`LLSD` mirror crate live?**
    In vkstorm-core-rust, as a shared `vkstorm-types` (name hypothetical),
    before inventory and the object store each invent their own. Not
    decided anywhere I found.
13. **Could not determine:** whether the vkstorm-core-rust crates already
    define shared UUID, LLSD-value or math types (that repository was not
    read), and the exact feature list inside the pinned `ffi-v0.1.0`
    package beyond the symbol families visible in its header (`vks_msg_`,
    `vks_tfetch_`, `vks_audio_`, `vks_http_`, `vks_mesh_`, `vks_llsd_`,
    `vks_texcache_`, `vks_dxt_`, `vks_dp_`, `vks_diskcache_`, `vks_j2c_`,
    `vks_asset_`, `vks_volume_`, `vks_retry_`, image codecs; no `vks_inv*`).
