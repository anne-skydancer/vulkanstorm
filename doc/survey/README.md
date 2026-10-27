# Vulkanstorm codebase survey

A survey of the whole viewer source tree, written to plan the incremental
Rust port (`doc/rust_port.md`). It replaces earlier survey documents that
were kept outside the repository and lost; the inventory fixture READMEs still
cite them as "survey 1 §…" to "survey 4 §…", and chapter 03 §5.7
reconstructs those references where the repository allows.

- **Source baseline:** `vkstorm-rust` at `8d101eccb6` (2026-10-26), surveyed
  on 2026-10-27 from branch `codex/rust-inventory-model`. Companion Rust
  repository: `vkstorm-core-rust` `main` at `1b183c7`.
- **Method:** source reading only. Counts give the command that produced
  them; statements not read directly from source are labelled
  *Inference*. Nothing was measured at runtime, and no chapter makes
  performance claims.
- **Line references** are to the baseline above and will drift. Where a
  fixture README and a copy-check script disagree, the script is
  authoritative.

## Chapters

| # | Chapter | Covers |
|---|---|---|
| 01 | [Foundation libraries](01-foundations.md) | `llcommon`, `llmath`, `llxml`, `llfilesystem`, `llmeshoptimizer`: LLSD, logging, threads, coroutines, events, singletons, SIMD math |
| 02 | [Networking and messaging](02-networking.md) | `llmessage`, `llcorehttp`, capabilities, event queue, message handlers, AIS, login, teleport |
| 03 | [Content and data model](03-content-model.md) | **Inventory in depth** (library layer, viewer layer, the oracle phase, port plan inputs), then primitives, characters, appearance, the object store |
| 04 | [Rendering, windowing and imaging](04-rendering.md) | `llrender`, `llwindow`, `llimage`, the pipeline, draw pools, textures, shaders; relation to `doc/vulkan/` |
| 05 | [User interface](05-ui.md) | `llui`, XUI and skins, floaters, notifications; builds on and corrects `doc/rust_port_ui.md` |
| 06 | [Application core and features](06-app-core.md) | Lifecycle, startup, settings, agent, world, RLVa, Firestorm modules, Vulkanstorm additions; **module map of all of `newview`** |
| 07 | [Media, audio, voice and plugins](07-media-audio-plugins.md) | `llaudio`, `llplugin`, media plugins, WebRTC and Vivox voice, crash loggers |
| 08 | [Build, dependencies, tests and port infrastructure](08-build-tests-infrastructure.md) | CMake/autobuild, every 3p package, tests and oracles, CI and release flow, `vkstorm-core-rust` and the C ABI |

## The codebase at a glance

- About 1.3 million lines of C/C++ under `indra/`; `indra/newview` alone is
  about 927k lines (837 `.cpp` files, 732k lines in them). Chapter 06 places
  every `newview` file in one of 26 clusters and assigns it to a chapter.
- The viewer is held together by global singletons: 333 distinct `g*`
  globals are declared in `newview` headers; `gSavedSettings` is used 5,111
  times in 437 files and `gAgent` 4,074 times in 367 files (06).
- `LLSD` appears in 1,573 of 3,132 source files (01). The class stays C++;
  its serialization is already Rust.
- Firestorm patches upstream files in place: 6,809 `<FS` marker lines in
  611 `ll*` files (06). RLVa adds 16,980 lines and 907 hook lines in 134
  files outside `rlv*`, with no tests (06).
- The renderer is tightly bound to OpenGL: 3,485 `gGL.` uses in 93 `newview`
  files (04).

## What is Rust today

Eight parts, all behind `vkstorm-ffi` (390 exported functions in the
hand-written header; 08). The viewer-side callers sit in the libraries;
no file in `newview`'s networking, startup or UI code calls `vks_*` directly
(02, 06).

| Rung | Viewer side | FFI prefix | Chapter |
|---|---|---|---|
| JPEG 2000 | `llimagej2coj` | `vks_j2c_*` | 04 |
| LLSD serialization | `llcommon/llsdserialize*` | `vks_llsd_*` | 01 |
| Prim mesher | `llmath/llvolume.cpp` | `vks_volume_*` | 01, 03 |
| PNG, TGA, BMP, DXT | `llimage` | image functions | 04 |
| Message templates, data packers | `llmessage` | `vks_msg_*`, `vks_dp_*` | 02 |
| HTTP | `llcorehttp` | `vks_http_*` | 02 |
| Asset pipeline (texture cache and fetch, disk cache, file system, mesh) | `llfilesystem`, `llimage`, `llmessage`, viewer | `vks_texcache_*`, `vks_tfetch_*`, `vks_asset_*`, … | 01, 04 |
| Audio | `llaudio` | `vks_audio_*`, `vks_music_*` | 07 |

## The inventory rung (current work)

- **State:** oracle phase complete; implementation not started. Six
  recorders (R1, R2, R3a, R3b, R4a, R4b), about 55 MB of fixtures, six
  replay tests and six copy-check scripts run in CI. The `vkstorm-inventory`
  crate is module doc comments only (120 lines); `vkstorm-ffi` has no
  inventory feature and no `vks_inv*` functions (03 §5, 08).
- **Shape:** the record classes and `LLInventoryModel` containers stay C++
  (the viewer subclasses write the base classes' fields directly; 145 files
  use `gInventory`). Rust makes the decisions; flat records and op lists
  cross the C ABI (03 §4, §6.3–6.4).
- **Order:** within R1, types and flags → text, shadow, `LLCategory` →
  permissions, aggregate, sale → records and CRCs → LLSD forms → legacy
  forms → message field order; then R2 cache codec, R3a AIS parse, R3b
  scheduler, R4a task, R4b offer/give (03 §6.2).
- **Open decisions:** eight, listed in 03 §6.5. Decision 6 (the recording
  commit `60184e5054` missing from the clone) is resolved locally: the
  squash-merged oracle branch was restored from
  `H:\vulkanstorm\branch-archive-2026-10-25` into the local ref
  `refs/archive/codex/rust-inventory`. A fresh clone still lacks it; making
  `check_r1_copies.py --at` blob-based would remove the dependency.

## Recommended order after inventory

From the chapters' port assessments (each gives its reasons):

1. **Object store** (03): object-update decoding and the data side of
   `LLViewerObject`. A new `vks_msg_*` entry point that hands decoded
   message slots straight to Rust is the suggested boundary (02). There is no
   recorder yet; an oracle phase comes first.
2. **Textures** (04): little remains after rung five ported cache and fetch.
   The rung reduces to the VRAM budget and discard-bias policy, per-texture
   priority, and image pixel operations. `doc/rust_port.md` lists textures
   before inventory; that order needs updating.
3. **Leaf logic in the app core** (06): SLURL, chat-log parsing, RLVa
   command syntax, version and date parsing, auto-replace, anti-spam. They
   are small, have oracles or are easy to record, and can be scheduled
   alongside the larger rungs.
4. **UI data layer before widgets** (05): skin resolution and layered XML,
   strings, colours, commands, URL and Markdown text functions, then the
   notifications engine. Parameter parsing waits for the widget rung; the
   settings store would be its own rung.
5. **Renderer** (04): separate scene description from GL submission
   before porting; wrapping `LLRender` call by call is not recommended. The
   Vulkan design study in `doc/vulkan/` applies to this line unchanged (the
   renderer sources match its baseline).
6. **Keep C++:** the `LLSD` value class, threads, coroutines, singletons
   (01); thin wrappers around large engines: CEF, VLC, libwebrtc, Vivox (07).
   The plugin IPC host (about 7,000 lines, fixed wire protocol) is the one
   media candidate (07).

## Issues found

Checked against source while assembling this index (✓), or reported by one
chapter and not yet re-checked (–).

| | Issue | Where |
|---|---|---|
| ✓ | Vivox is compiled, packaged and staged in every build, although `doc/rust_port.md` says it is not part of the Release | `newview/CMakeLists.txt` (`llvoicevivox.cpp`), `cmake/ViewerMiscLibs.cmake:20`; 07 |
| ✓ | `gAudioDeviceMutex` is defined separately in the `llwebrtc` shared library and in `llcommon`, so on Windows audio and voice likely lock different mutexes (*Inference* for the runtime effect) | `llcommon/inlinemutexs.h:38`, `llwebrtc/llwebrtc.cpp:51`; 07 |
| ✓ | `RenderBackend=Vulkan` falls back to OpenGL with a log warning; there is no Vulkan code on this line | `newview/llappviewer.cpp:3735`; 04 |
| ✓ | Linux C++ is built without AVX2 (no `-mavx2`/`-march` in CMake or the build variables), while the Linux Rust package targets x86-64-v3 | `fs-build-variables/variables:59,89,93`, `cmake/00-Common.cmake:195`; 08 |
| ✓ | `LLSD`'s reference count is a plain `U32`, not atomic | `llcommon/llsd.cpp:103`; 01 |
| – | The crates' 334 tests never run in CI; the C ABI header is hand-written with no symbol or version check; 30 exported functions have no caller | 08 |
| – | Stock `ReleaseFS*` configurations pass `--opensim` and fail with the package path; no OpenSim, non-AVX2 or macOS package exists, and `VKSAudio.cmake` rejects macOS | 07, 08 |
| – | Event-queue messages reach the same handlers as UDP through a path the UDP-only parity tests do not cover | `llmessage/message.cpp:104-137`; 02 |
| – | `doc/rust_port.md` and the package header still cite the removed `indra/rust/...` and `indra/llcommon/vkstorm_ffi.h` paths | 01, 07, 08 |
| – | `doc/rust_port_ui.md` counts corrected (notifications 1,332; settings 2,397; commands 69; widget tags 139) | 05 |
| – | The installer does not ship the Rust package's license files (MPL-2.0 crates included) | 08 |
| – | `autobuild.xml` still lists `openjpeg` and `mesa`, which nothing uses | 04, 08 |

## Limits

- No runtime behaviour was observed and nothing was timed.
- Counts come from `grep`/`find` over the tree, not from a call graph or a
  build; each chapter states its commands.
- The split between Linden Lab, Firestorm and Vulkanstorm code is by file
  prefix and `<FS` markers, not by `git blame` against upstream.
- Each chapter ends with its own risks, open questions and gaps.
