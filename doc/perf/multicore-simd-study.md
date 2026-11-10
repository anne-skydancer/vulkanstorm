<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Multi-core everywhere, SIMD where it pays (study)

Status: study and implementation plan. Nothing here changes the viewer. The branch `codex/multicore-study` (from `vkstorm-devel` 6e96662e6c) carries:

- this document;
- an offline C++ bench, `scripts/perf/bench_multicore_simd.cpp` (standalone, not part of the viewer build). It measures fork-join dispatch and wake cost, atomic against non-atomic reference counting, and the viewer's own SSE affine transform against a bit-exact AVX2 version and an FMA version;
- its output: the tables in §3.

The owner's direction (2026-11-10):

> We'll be working with C++ and OpenGL code for a while, so we can and should optimize it so that the Rust port, when it comes in, is faster... Starting with a parallelized OpenGL renderer will give us a better starting point when we do start to plan and code the Vulkan.

> Evaluate the risks and implications of FMA. If precision is compromised, bit-exact; otherwise, experiment FMA independently.

Code references are file:line at `origin/vkstorm-devel` 6e96662e6c. The survey was done at `vkstorm-release` 3e47fa43c4. Every file cited here is identical on both branches, except three: `pipeline.cpp`, `newview/CMakeLists.txt` and `settings.xml`. For those, the lines given are devel's. Each claim is tagged:

- **[code]**: read in the code;
- **[measured]**: from the bench or from a binary on the dev machine (§3.1);
- **[inferred]**: reasoning or estimate, not yet checked.

---

## 1. Summary

**Today** the whole per-frame loop runs on the main thread: object and avatar updates, moves, geometry rebuilds, culling, state sorting and every render pass (§2.1). Each frame runs 9 to 11 cull and state-sort passes with PCSS on and no mirror, and up to 29 with a mirror (§2.2). The thread pools are small and mostly fixed. `General` is persisted as 1 for every existing user (§2.6). All hand-written SIMD is 128-bit SSE, although release builds have targeted AVX2 (x86-64-v3) for a while.

**The plan** (decisions in §5):

- A frame job system, `LL::FrameJobs`. It is fork-join: the main thread takes part, all jobs finish inside one frame phase, and results are merged in a fixed order. No GL calls run in jobs; OpenGL submission stays on the main thread. "Parallel OpenGL renderer" here means parallel CPU preparation: culling, geometry fill, and later state sorting, animation and skin palettes.
- Bit-exact AVX2 kernels in the C++ that stays C++ on every line.
- FMA: not adopted (the memo is §7).
- A CPU check so pre-AVX2 machines get a message instead of a crash.

**What decides the design** (measured, §3):

- A bit-exact AVX2 version of the viewer's affine transform runs **1.83–3.44×** faster than the SSE one, with **0 of 4.2 M floats different**.
- FMA changes **about half** of the output floats, for at most a 13% gain.
- An atomic ref+unref pair costs **7.3–7.5 ns**, against 0.2–1.0 ns today.
- Waking parked helper threads costs **9–41 µs** (median); back-to-back dispatch costs **1.3–2.8 µs**.
- The shipped Windows Release binary has **exactly one** FMA instruction, in `std::lerp`.

**Scope.** The full plan is about 20–27 weeks, against the planner's first 10–16. The owner committed to **Phase A** (stages 0, 1, 2, 3, the stage-4 memo, 5a, 5b, 5c), about **13–17 weeks**. **Phase B** (5d state-sort split, 5e avatars, asynchronous region-cache loading) will be re-planned from Phase A's measurements.

---

## 2. Current state

### 2.1 The per-frame loop runs on the main thread [code]

| Step | Where |
|---|---|
| Object idle updates (the list is copied first because `idleUpdate` may change it) | `llviewerobjectlist.cpp:1081-1142` |
| `gPipeline.updateMove()`: retextures, the moved list, balancing every octree | `llappviewer.cpp:6307` → `pipeline.cpp:2307` |
| `updateGeom` with a budget of 5% of frame time (`0.005*10*gFrameIntervalSeconds`) | `llviewerdisplay.cpp:883-886` → `pipeline.cpp:3030` |
| World `updateCull`, then sun shadows, then the world `stateSort` | `llviewerdisplay.cpp:914`, `:939`, `:1015` |
| Reflection-probe cube face: cull, shadows, sort, render | `llviewerdisplay.cpp:1288-1385` |
| HUD cull and sort | `llviewerdisplay.cpp:1462`, `:1490` |
| `rebuildGeom` / `rebuildMesh` | `llvovolume.cpp:5963`, `:6531` |
| `LLFace::getGeometryVolume` | `llface.cpp:1277` |

Nothing in the pipeline, spatial partition, volume, face, drawable, particle or skinning code posts work to a thread pool.

Coroutines run only when the main loop posts `mainloop` (`llappviewer.cpp:~1708`), before `idle()` and `display()`. Messages that kill objects, remove regions or shift objects (`llviewermessage.cpp:3767/3927` → `llviewerobjectlist.cpp:1938`) are handled in `idle()`. So a fork-join job inside `display()` cannot overlap any of them [code].

### 2.2 Cull and state-sort passes per frame [code]

| Pass | Count | Where |
|---|---|---|
| World view | 1 | `llviewerdisplay.cpp:914`, `:1015` |
| Sun cascades | 4 (2 during a cube snapshot) | `pipeline.cpp:11663` (`gCubeSnapshot ? 2 : 4`), `renderShadow` `:11829` |
| Spot (projector) shadows | 0–2 | `pipeline.cpp:11982` |
| Reflection probe | 1 face per frame (its own cull + 2 cascades) | `llreflectionmapmanager.cpp:757` |
| Mirror (hero probe) | up to 6 faces × (1 + 2) | `llheroprobemanager.cpp:284-291` |
| HUD | 1 | `llviewerdisplay.cpp:1462`, `:1490` |

Each shadow pass is cull → sort → render, interleaved (`renderShadow`, `pipeline.cpp:11092-11094`).

### 2.3 Culling makes GL calls and changes shared state [code]

- `LLOctreeCull::earlyFail` → `checkOcclusion` calls `glGetQueryObjectuiv`, including a blocking `GL_QUERY_RESULT` (`llspatialpartition.cpp:1060-1067`, `llvieweroctree.cpp:1107-1165`). The state sort calls it again (`pipeline.cpp:3351`, `:3417`).
- `LLVOCachePartition::cull` writes the **shared camera** (`calcRegionFrustumPlanes`, `llvocache.cpp:~1090`) and the region's visible lists.
- Culling also mutates:
  - `rebound()` on each partition root (`llspatialpartition.cpp:1458`);
  - the `mNumVisibleNodes++` counter (`pipeline.cpp:2784`);
  - the occlusion state of parent groups (`markOccluder`);
  - the globals `sCull`, `LLViewerCamera::sCurCameraID`, `LLPipeline::sShadowRender`, `sUseOcclusion` (set to 0 for shadows, `pipeline.cpp:11065`) and `gCubeSnapshot`.
- **One partition reads another's result.** `LLSpatialBridge::setVisible` (`lldrawable.cpp:1513-1557`) tests the parent avatar's visible frame for attachments. That value is written by the avatar partition, which is culled *after* the bridge partition (`llviewerregion.h:100-101`). So serial code reads last frame's value. Run in parallel without care, an attachment can appear one frame early.
- `group->setVisible()` writes `mVisible[sCurCameraID]`, which is one slot per camera. Culls for different cameras therefore touch different slots.

### 2.4 Geometry fill [code]

- **Buffer allocation makes GL calls** (`gen_buffer`, `glBufferData`: `llvertexbuffer.cpp:446-456`).
- **`mapVertexBuffer` returns CPU memory** (`mMappedData`, `:1292-1325`). But `_mapBuffer` pushes onto the static `sMappedBuffers` (`:1029`, `:1416-1420`) and edits the buffer's `mMappedVertexRegions`.
- **The upload is `glBufferSubData`** in `flushBuffers` (`:1388-1404`).
- **`rebuildGeom` uses static scratch arrays** (`sSimpleFaces[i]` and the rest, `llvovolume.cpp:~6497`), so it is not re-entrant. Buffers are created per spatial group in `genDrawInfo` (`:6875`).
- **`rebuildMesh` runs inline** from the state sort for every visible group (`pipeline.cpp:3366`, `:3428`) and ends with `flushBuffers` (`llvovolume.cpp:6604`).
- **Hazards inside `getGeometryVolume`:**
  - **`LLTextureEntry::getMaterialParams()` returns `LLMaterialPtr` by value** (`lltextureentry.h:164`). That is a non-atomic ref and unref of a shared `LLMaterial` (`llmaterial.h:36`). It is a confirmed race once two jobs use the same material.
  - **`getVolume()->genTangents(face)`** (`llface.cpp:~1677/1724/2035/2187`) allocates and writes tangents lazily into the `LLVolume` (`llvolume.cpp:2025`). Several objects share that volume through `LLVolumeMgr`.
  - **The GLTF selection-marker path** allocates a buffer, clones it and unmaps it (`llface.cpp:1363-1381`).

### 2.5 Reference counting and FPU state [code]

- **`LLRefCount` is non-atomic** (`llrefcount.h:56`, `:64`). It is the base of:
  - `LLViewerObject` (`llviewerobject.h:125-127`) and `LLViewerOctreeEntryData`, hence `LLDrawable`;
  - `LLTreeListener` (`lltreenode.h:41`), hence every `LLSpatialGroup`;
  - `LLVertexBuffer`, `LLVolume`, `LLMaterial` and `LLGLTFMaterial`.
- **Only the main thread sets flush-to-zero and round-to-nearest** (`LLVector4a::initClass`, `llvector4a.h:63-66`, called at `llappviewer.cpp:881`). The FPU control register (MXCSR) is per thread. Any new worker that should give the same results as the main thread must set it too.

### 2.6 Thread pools [code]

- `General` is coded as 3 (`llappviewer.cpp:2585`).
- The `ThreadPoolSizes` default is `General = 1`, `ImageDecode = 9` (`settings.xml:16090-16105`, `Persist = 1`).
- At start-up the code replaces ImageDecode with `clamp(cores − 4, 2, 8)`, or with `FSImageDecodeThreads`, and writes the map back (`llappviewer.cpp:2600-2628`). Settings that differ from the default are saved (`llcontrol.cpp:1072-1084`), so **existing users keep `General = 1` forever**. A new default would not reach them; the width has to be computed in code [code, inferred for the saved files].
- `MeshLodProcessing` is 2 (`llmeshrepository.cpp:984`).
- Other threads: the window thread, the GL image-upload thread (shared context), texture fetch, texture cache, mesh repository, audio and WebRTC.

### 2.7 Avatars [code]

- `LLVOAvatar::idleUpdate` (`llvoavatar.cpp:3127-3329`) mixes animation with everything else:
  - animation: `updateCharacter`, `:5723`, i.e. `updateMotions` (`:5820/5825`) and the skeleton's `updateWorldMatrixChildren` (`:5850`);
  - also name tags, voice, sounds, wind and moving drawables.
- Skin matrix palettes are computed lazily while drawing (`lldrawpool.cpp:666/702/739` → `llvoavatar.cpp:11317` → `LLSkinningUtil::initSkinningMatrixPalette`, `llskinningutil.cpp:128`).

### 2.8 The CPU requirement is not enforced [code]

- **Builds target AVX2.** Windows uses `/arch:AVX2` (`cmake/00-Common.cmake:134-145`). Linux uses `-march=x86-64-v3 -ffp-contract=off` (`:216-231`). CI forces it.
- **No AVX2 check anywhere the viewer starts:**
  - the Inno installer's `[Code]` (`installer_template.iss:136`) has none, and `MinVersion = 6.1sp1` (`:52`);
  - the unused NSIS template has one (`installer_template.nsi:301-332`, `IsProcessorFeaturePresent(40)`);
  - `linux_tools/wrapper.sh` and the viewer itself have none.
- **`llwebrtc.dll` is built in-tree, SHARED, with `/arch:AVX2`** (`llwebrtc/CMakeLists.txt:25`), and is not delay-loaded (`newview/CMakeLists.txt:2313` delay-loads only opengl32 and glu32). Its static initialisers run before the exe's entry point. It exports only functions (`llwebrtc.h:309-322`).

### 2.9 Bit-exact reference data and measuring tools [code]

- **No bit-exact reference data on release or devel.** The recorded llvolume test data exists only on vkstorm-rust, where the prim mesher is the Rust crate `vkstorm-volume` (`doc/rust_port.md`, "Rung: the parametric prim mesher"). Release and devel do not link Rust.
- **Tracy:** 0.11.1 is packaged; the option is in `cmake/Tracy.cmake:7-40`, forced OFF in CI.
- **`LLSceneMonitor`** (`SceneLoadingMonitorEnabled`) measures how long a scene takes to settle after loading.
- **`LLAgentPilot`** records and plays back camera paths.
- **Profiling zones are allowed.** `scripts/tests/check_release_hooks.py` does not forbid `LL_PROFILE_ZONE*` macros, which compile to nothing without Tracy.

---

## 3. Measurements

### 3.1 Hardware and method [measured]

- **Machine:** AMD Ryzen 9 9950X3D on a B850 board, Windows 11 Pro 10.0.26300, MSVC from Visual Studio 18 (2026).
- **The machine shows 8 cores / 8 logical processors with 96 MB of L3.** The owner disabled CCD1 and SMT on purpose, to keep everything on the V-cache CCD, and the machine stays that way. Stage-1 profiling and every gate in this plan run on that configuration. Dual-CCD and SMT behaviour, the stock setup most 7950X3D/9950X3D users run, cannot be measured here (§8).
- **Build:** `cl /O2 /Oi /arch:AVX2 /fp:precise /std:c++20 /EHsc`, the viewer's Windows code-generation flags. Single run; the figures were stable to the shown digits across the two refcount repetitions.

### 3.2 AVX2 against SSE: the viewer's affine transform [measured]

The kernel is `LLMatrix4a::affineTransformSSE` (`llmatrix4a.h:194-209`), as used per vertex in `getGeometryVolume` (`llface.cpp:2123-2128`):

- **SSE:** one vertex per register.
- **AVX2:** two vertices per register, with exactly the same per-lane multiply and add order.
- **FMA:** the same work as three fused multiply-adds.

| Vertices (input size) | SSE ns/vertex | AVX2 ns/vertex | AVX2 speed-up | AVX2 floats different | FMA ns/vertex | FMA floats different |
|---|---|---|---|---|---|---|
| 1,024 (16 KB, L1) | 0.745 | 0.216 | 3.44× | 0 | 0.188 | 2,032 of 4,096 (max 175 ulp) |
| 65,536 (1 MB) | 0.736 | 0.301 | 2.45× | 0 | 0.282 | 132,123 of 262,144 |
| 1,048,576 (16.8 MB) | 0.737 | 0.403 | 1.83× | 0 | 0.444 | 1,984,216 of 4,194,304 |

The FMA ulp maxima for the larger sizes (21,734 and 911,012 ulp) come from results near zero after cancellation. The absolute error is small, but the bits differ.

### 3.3 Reference counting [measured]

| ref+unref pair | Non-atomic | Atomic (`fetch_add` / `fetch_sub`) |
|---|---|---|
| 64 k objects in random order (4 MB) | 0.65–0.96 ns | 7.29–7.52 ns |
| One hot object | 0.18–0.27 ns | 7.39–7.43 ns |

An atomic `LLRefCount` would cost about 7 ns per pair on every pair in the viewer, so 0.7 ms per frame at 100 k pairs [inferred]. So the rule is the other way round: job bodies never copy `LLPointer`s (§4.2).

### 3.4 Fork-join dispatch [measured]

The pool is 64 chunks of trivial work, the main thread takes part, and helpers spin and then park on `std::atomic::wait`.

| Helpers | Spin window | Back-to-back median / p99 | After 2 ms idle median / p99 |
|---|---|---|---|
| 1 | 0 µs | 1.6 / 2.7 µs | 9.4 / 69 µs |
| 1 | 50 µs | 1.3 / 1.9 µs | 7.6 / 171 µs |
| 3 | 0 µs | 2.1 / 2.8 µs | 15.0 / 72 µs |
| 3 | 50 µs | 1.9 / 3.3 µs | 23.3 / 1,557 µs |
| 5 | 0 µs | 2.3 / 10.2 µs | 20.7 / 138 µs |
| 5 | 50 µs | 2.0 / 2.5 µs | 43.9 / 230 µs |
| 7 | 0 µs | 2.8 / 9.6 µs | 41.3 / 130 µs |
| 7 | 50 µs | 2.3 / 3.3 µs | 76.3 / 190 µs |

What follows from this:

- Helpers stay awake (spinning) through the frame's CPU-preparation window and park at swap.
- A job needs at least about 50–100 µs of work; smaller ones run serially.
- With 9–11 passes per frame, the dispatch overhead is about 0.03–0.1 ms per frame [inferred].

### 3.5 FMA in today's binaries [measured]

- `dumpbin /disasm` of the Release `Vulkanstorm-Release.exe` built on 2026-11-08 contains **one** FMA instruction, a `vfmadd213ss` in `std::lerp`. It also contains about 50,000 lines using `ymm` registers, from auto-vectorisation and the library.
- So MSVC `/fp:precise` does not fuse multiply-add on its own. The single FMA comes from the standard library's `lerp`, which `llmath.h:335` exports as the viewer's `lerp`.
- Linux libstdc++'s `lerp` presumably does not fuse, so where `lerp` is used, Windows and Linux probably already differ in the last bit [inferred; the Linux binary was not disassembled].

---

## 4. Design

### 4.1 Stage 0: CPU check (release hotfix)

**Windows, in the executable.**

- New file `indra/newview/llcpucheck_win.cpp`:
  - SPDX GPL-3.0-or-later, C linkage, no static data;
  - compiled per source file with `/arch:SSE2 /GS-`, and the function marked `__declspec(safebuffers)`.
- `extern "C" int vkstormEntry(void)` checks every x86-64-v3 feature:
  - CPUID leaf 1, ECX: FMA (bit 12), MOVBE (22), OSXSAVE (27), AVX (28), F16C (29);
  - leaf 7, EBX: BMI1 (3), AVX2 (5), BMI2 (8);
  - leaf 0x80000001, ECX: LZCNT (5);
  - `(_xgetbv(0) & 6) == 6`, i.e. the OS saves the XMM and YMM registers.
- On failure it shows `MessageBoxW(NULL, L"Vulkanstorm needs a processor with AVX2 (x86-64-v3): Intel Haswell (2013) or newer, AMD Zen or Excavator or newer. This computer's processor does not support it.", L"Vulkanstorm", MB_ICONERROR)` and calls `ExitProcess(1)`. Otherwise it returns `wWinMainCRTStartup()`.
- Link flags (`newview/CMakeLists.txt:2313`): add `/ENTRY:vkstormEntry /DELAYLOAD:llwebrtc.dll`.
- Effect: the check runs before CRT start-up and before every static initialiser. Delay-loading `llwebrtc.dll` (functions only) also keeps its AVX2 initialisers from running first.

**Installer (`installer_template.iss`).**

- Set `MinVersion=10.0`.
- In `[Code]`, declare `function IsProcessorFeaturePresent(Feature: DWORD): BOOL; external 'IsProcessorFeaturePresent@kernel32.dll stdcall';`.
- In `InitializeSetup`, when `not IsProcessorFeaturePresent(40)` (PF_AVX2_INSTRUCTIONS_AVAILABLE), call `SuppressibleMsgBox(<same text>, mbCriticalError, MB_OK, IDOK)` and return `False`. That aborts silent installs too.

**Linux (`linux_tools/wrapper.sh`, before the launch at line 180).**

- **Primary test:** `ld-linux-x86-64.so.2 --help` (`/lib64/…` or `/lib/x86_64-linux-gnu/…`, glibc ≥ 2.33) must print `x86-64-v3 (supported`.
- **Fallback, when `ld.so --help` is not available:** the first `flags` line of `/proc/cpuinfo` must contain all of `avx avx2 bmi1 bmi2 f16c fma movbe abm`.
- **On failure:** show the message with `zenity --error` or `kdialog --error` if present, otherwise on stderr, then `exit 1`.

**Gate.**

- Intel SDE (Software Development Emulator, which emulates older CPUs): `sde64 -snb -- <exe>` (AVX without AVX2) shows the message and exits 1; `sde64 -hsw` starts normally.
- A silent install on a machine without AVX2 aborts. SDE does not emulate the installer's call to `IsProcessorFeaturePresent`, so this needs a real machine or a VM with AVX2 masked.
- The wrapper, tested with the feature tests stubbed off.
- A Release login test.

**Lines.** Release first (Release + LTO + installer), batched with another hotfix; then the same change on devel, canary and rust.

### 4.2 Stage 2: `LL::FrameJobs` and thread widths

**`LL::FrameJobs`** (llcommon, about 400 lines of our own code; no external scheduler):

- **Threads:** fixed helper threads, and the main thread takes part. `parallel_for(count, grain, fn)` uses dynamic chunks, and each index writes its own result slot.
- **Waking:** helpers spin and then park on `std::atomic::wait`. `beginFrameWork()` at the start of `display()`'s CPU preparation keeps them awake; they park at swap (§3.4).
- **Small jobs** run serially below a size threshold.
- **Per-worker set-up:** `LLVector4a::initClass()` (FTZ and rounding, §2.5) and a Tracy thread name.
- **Exceptions** are caught and rethrown on the main thread.
- **Dev-build guards (not in release):** `LL::FrameJobs::inJob()` asserts in `LLVertexBuffer::genBuffer` / `flushBuffers`, in `gGL`, and an owner-thread assert in `LLRefCount::ref` / `unref`.
- **Not built on `LL::WorkQueue`.** Its per-item mutex and `std::function` are too heavy, and a joining main thread could run arbitrary `mainloop` work.
- **Job-body rule:** no `LLPointer` copies, no GL, no `markDead` or `killObject`, no floating-point reductions across jobs. Floating-point sums run in a serial apply step in serial order.

**Width rule** (computed at every start-up; the persisted `ThreadPoolSizes` values for these pools are overwritten in code):

- **Fast domain:** the cores that share the main thread's L3, i.e. the V-cache CCD on AMD X3D. On Intel hybrid CPUs, the P-cores only. Read from `GetLogicalProcessorInformationEx` (`EfficiencyClass`, L3 groups) on Windows and from `/sys/devices/system/cpu` on Linux.
- **Frame helpers** = `clamp(fastPhysicalCores − 2, 1, 7)`, which leaves one core for the GL driver's thread. Helpers get CPU sets on the fast domain (soft affinity).

| CPU | Frame helpers |
|---|---|
| 4 cores | 2 |
| 6 cores | 4 |
| 8 cores (the dev machine as configured) | 6 |
| 9950X3D | 6 on the V-cache CCD |
| 8 P-cores + 12 E-cores | 6 on the P-cores |

- **Background pools:** ImageDecode `clamp(logical − helpers − 2, 2, 8)`, General 2, MeshLodProcessing `clamp(logical / 8, 2, 4)`. They run below normal priority and prefer E-cores or the other CCD.
- **During teleport and scene settle,** the background widths may rise to the cap.
- **When the fast domain is the whole machine** (a single CCD, as on the dev machine with 8 cores, 8 threads and 6 helpers), there are no other cores to steer background pools to. They share the same 8 cores with the frame helpers. They run below normal priority, so the main thread and helpers win during the frame window, and one core stays free for the GL driver thread. The stage-2 gates are measured on exactly this setup.

### 4.3 Stage 3: bit-exact AVX2 kernels

- **Where:** only C++ that stays C++ on every line. The kernels move into `llmath` as pure functions. The SSE version stays in the unit test as the reference, and the tests compare bit for bit, including NaN, ±inf, denormals and −0.

| Kernel | Source | Note |
|---|---|---|
| K1 position transform + texture-index select | `llface.cpp:2123-2128` | §3.2: 1.83–3.44×, 0 floats different |
| K2 normal and tangent rotation | `llface.cpp:2152+` | same width rule as K1 |
| K4 skin matrix palette (`matMul` per joint) | `llskinningutil.cpp:128`, `llmatrix4a.h:240` | |
| K5 AABB against frustum planes | `LLCamera::AABBInFrustum*`, called from `llspatialpartition.cpp:1081-1100` | up to 8 planes per register, same per-plane dot order |

- **Rules:**
  - identical per-lane operation order;
  - multiply and add stay separate;
  - no reductions across lanes;
  - `_mm256_rsqrt_ps` only where SSE already used `_mm_rsqrt_ps` (the approximation table is the same per lane on one CPU, and differs between vendors as it does today).
- **Not in C++:** the prim mesher, JPEG 2000, mesh and image decoding. On vkstorm-rust these are the Rust crates (`vkstorm-volume`, `vkstorm-openjp2` and the rest); their SIMD belongs there, gated by their recorded test data.
- **CI gate:** count `vfmadd`, `vfmsub` and `vfnm*` in `dumpbin /disasm` (Windows) and `objdump -d` (Linux). Today the Windows count is 1 (§3.5); it must not rise.

### 4.4 Stage 5: parallel CPU preparation for the OpenGL loop

**5a. Whole-thread audit and guards.**

- The dev-build asserts of §4.2.
- A dev-build compare mode (behind `#ifdef`, kept out of release per `check_release_hooks.py`). It runs a phase serially and in parallel and compares cull lists, draw maps and filled buffer bytes.
- Two fixes that also help serial code:
  - `getMaterialParams()` returns `const LLMaterialPtr&`;
  - tangents are generated in a main-thread pre-pass, or under a once-flag per face.
- A complete list of callers and threads for every target, including shutdown paths. The sVBCache login crash fixed in 3e47fa43c4 is the warning here.

**5b. Geometry fill.**

- `rebuildGeom` / `genDrawInfo` split into three phases:
  - **Plan (main thread):** classify faces, allocate buffers (GL), register them as mapped.
  - **Fill (parallel):** one job per `LLSpatialGroup`. Buffers belong to one group, so no two jobs share `mMappedVertexRegions`. `getGeometryVolume` writes into `mMappedData`, and the static scratch becomes per worker.
  - **Flush (main thread):** `glBufferSubData`.
- Selected GLTF faces and failed fills (`markRebuild`) are deferred to the main thread.
- `rebuildMesh` calls from the state sort are collected and run as one parallel batch after the sort.
- **The gain is mainly settle time:** more groups fit into the 5% `updateGeom` budget.

**5c. Culling.**

- **`ViewContext`-lite** replaces the globals: camera ID, the shadow, reflection and occlusion flags, and the result reference. This is the same idea as `doc/vulkan/scene-ui-auxiliary.md` S03, and it carries straight into the Vulkan design.
- **Main-thread pre-passes:**
  - resolve pending occlusion queries for the camera (GL);
  - `rebound()` every partition root.
- **Jobs** are octree subtrees (depth 2–3) of each region × partition. One job per partition is too coarse, because the volume octree dominates.
  - Each job has its own camera copy and its own result fragment.
  - Fragments are merged in serial depth-first order, so the lists are identical to today's.
  - Counters are summed.
- **Attachment bridges** read a snapshot of avatar visibility taken before the cull (§2.3).
- **The four cascade culls are hoisted** to run together. Each camera writes its own `mVisible[...]` slot. Bit-exactness against the interleaved order still has to be proven with the compare mode.

**Phase B (re-planned after Phase A):**

- **5d:** the state sort split into a pure part (visibility, distance and LOD decisions written to per-drawable records) and a serial apply part (`markRebuild`, pool enqueue, `markDead`, LOD timestamps).
- **5e:** avatar animation evaluated in parallel per avatar (`updateMotions` + skeleton matrices) before the rest of `idleUpdate`, and skin palettes precomputed per avatar before rendering.
- **Asynchronous `LLVOCache::readFromCache`** (`llviewerregion.cpp:847`) for teleports.

### 4.5 Stage 6: Vulkan carry-over (Phase A ends with a note; the document comes with the Vulkan line)

These carry over:

- `ViewContext` and immutable per-view draw lists;
- the plan / fill / flush split, which maps to staging arenas;
- the `FrameJobs` widths;
- one secondary command buffer recorded per job.

It extends `doc/survey/04-rendering.md`'s coupling item "Global current-view state".

---

## 5. Decisions as taken (owner, 2026-11-10)

1. **Scope:** Phase A now (stages 0, 1, 2, 3, the stage-4 memo, 5a, 5b, 5c). Phase B is re-planned from Phase A's measurements.
2. **Stage 0 is the full CPU check:**
   - a custom entry point;
   - a delay-loaded `llwebrtc.dll`;
   - a blocking installer check with `MinVersion = 10.0`;
   - the Linux wrapper check.
3. **The frame job system is our own `LL::FrameJobs`.** No external scheduler.
4. **FMA:** the memo only (§7), no experiment branch.
5. **Primary benchmark scene:** a local OpenSim region (a dense OAR and about 40 NPC avatars); SL scenes confirm.
6. **Dev machine:** the 9950X3D stays 8 cores / 8 threads on the V-cache CCD. CCD1 and SMT are off on purpose. Stage 1 and all gates run on that configuration. Dual-CCD + SMT behaviour is not verified on the dev machine. It can be checked in one temporary BIOS session during stage 2, or confirmed later on another machine or from user reports (§8).

Standing decisions: bit-exact by default; AVX2 (x86-64-v3) only; measured with a Tracy RelWithDebInfo build that never ships; new work must not reduce fps; the light and shadow work keeps its budget of today's PCSS-on frame + 5%. The baseline is `windows-latest` 82080 with PCSS on.

---

## 6. Phase A stages

### 6.1 Measurement procedure (stage 1)

**Builds.** RelWithDebInfo + LTO + `-DUSE_TRACY=ON` (on-demand), made for:

- the baseline commit (`windows-latest` 82080);
- the head of each stage.

These builds are never shipped.

**Scenes.**

- **S1:** a local OpenSim region with a dense OAR and about 40 NPCs (`osNpcCreate`). Fully repeatable; this is the A/B scene.
- **S2:** a dense static SL region.
- **S3:** a crowded SL venue, for confirmation only.

Every scene uses a fixed camera preset, plus an `LLAgentPilot` path.

**Capture.** 3 × 60 s per scene, exported with `tracy-csvexport`. The metrics:

- main-thread ms per frame (the `display` + `idle` zones);
- average fps;
- 1% low (the p99 frame time);
- hitches (frames longer than 2× the median).

Take the fps runs with Tracy disconnected.

**Teleports.** 10 within a region and 10 between regions, on S1's grid and on SL. Each is timed from the teleport-state timestamps in the viewer log to settle as reported by `LLSceneMonitor`. No new probes.

**Hardware.** The 9950X3D as configured: 8 cores / 8 threads on the V-cache CCD (§3.1, §5.6). Plus one 4-core/8-thread machine for the width rule.

### 6.2 Stage table

| Stage | Scope | Gate (tests and performance) | Owner's check | Effort | Branch and lines |
|---|---|---|---|---|---|
| 0 | CPU check: exe entry point, delay-loaded `llwebrtc.dll`, installer, wrapper (§4.1) | SDE `-snb` shows the message, `-hsw` starts; silent install aborts without AVX2 (real machine or VM with AVX2 masked); wrapper with the tests stubbed off; Release login test | One install on a real machine | 2–3 days | Hotfix on release (Release + LTO + installer), batched; then devel, canary, rust |
| 1 | Tracy builds, scenes, procedure, baseline report (§6.1) | Run-to-run noise < 3% on S1; ranked main-thread zones | Approve the scene set | 1–1.5 weeks | No viewer code; report under `doc/perf/` |
| 2 | `LL::FrameJobs`, width rule, pool widths (§4.2) | No fps loss on S1–S3; settle not slower; measured on the 8C/8T single-CCD dev machine with background pools sharing its cores; 4-core run | Sanity run on the 4-core machine | 2–2.5 weeks | `codex/mc-framejobs` from devel → integration PRs to release, canary, rust |
| 3 | K1, K2, K4, K5 + bit-for-bit tests + FMA-count gate (§4.3) | Bit-for-bit tests pass on Windows and Linux; the kernels' Tracy zones get faster; FMA count unchanged | — | 2–3 weeks | `codex/mc-avx2-kernels` (C++ only; the rust line takes the newview kernels) |
| 4 | FMA memo (§7) | — | Accepted with this study | done here | Document only |
| 5a | Audit, guards, compare mode, the material and tangent fixes | Compare mode clean on S1–S3; Release login test | — | 1.5 weeks | `codex/mc-audit` |
| 5b | Plan / fill / flush geometry | Buffer bytes identical (compare mode); settle time better on S1 teleports; no fps loss; Release login test | Teleport check | 3 weeks | `codex/mc-geomfill` |
| 5c | `ViewContext`-lite, parallel cull, cascade hoist | Cull lists identical (compare mode); main-thread ms lower on S1; no fps loss; byte-identical OpenGL captures of S1 and S2 | In-world once, after release | 3–4 weeks | `codex/mc-cull` |

Phase A totals about 13–17 weeks. Phase B (5d, 5e, asynchronous region cache) is about 7–9 weeks more [inferred], to be re-planned.

Feature stages branch from `vkstorm-devel` with the `codex/` prefix and RelWithDebInfo, and reach release, canary and rust through integration PRs after qualification.

---

## 7. FMA memo (stage 4 deliverable)

- **Precision.** A fused multiply-add rounds once instead of twice, so each operation is at least as accurate as today's. Precision as such is not compromised.
- **Determinism is.**
  - About half of the output floats of a three-term transform change (§3.2).
  - Results near cancellation move by up to 10⁵–10⁶ ulp.
  - Any FMA in shared code would therefore break:
    - the recorded llvolume test data on vkstorm-rust (Windows and Linux files);
    - Windows/Linux agreement, unless both use FMA in exactly the same places;
    - equivalence with the Rust crates;
    - byte-identical OpenGL captures used as gates in §6.2.
- **The gain is small:** at most 13% in L1-resident data, none or negative once memory-bound (§3.2). The bit-exact AVX2 kernels already capture the larger win (1.8–3.4×).
- **Compilers.** MSVC `/fp:precise` does not fuse on its own (§3.5). GCC fuses by default and is held off by `-ffp-contract=off` (`00-Common.cmake:216-231`).
- **Verdict: FMA is not adopted.**
  - No `/fp:contract`; `-ffp-contract=off` stays.
  - No FMA intrinsics in shared kernels.
  - The FMA-count gate (§4.3) keeps it that way.
- **Revisit only** for a kernel whose output is never compared bit for bit (for example a purely visual effect on the Vulkan line). That needs its own measured gain of at least 10% and the owner's approval.

---

## 8. Open points, risks and what was not verified

**Not verified:**

- Whether MSVC accepts the per-file `/arch:SSE2` override under `/WX`. The global `/arch:AVX2` is added with `add_compile_options`; warning D9025 may appear.
- That delay-loading `llwebrtc.dll` fits how voice starts up.
- That the executable's dynamic `thread_local` initialisation (`gGL`) runs only after the custom entry point. The CRT start-up calls the TLS initialiser for the main thread; the SDE gate checks it.
- How reliable `IsProcessorFeaturePresent(40)` is on Windows 10 builds older than about 2004. Raising `MinVersion` limits the risk but does not remove it.
- Linux: no `objdump` FMA count yet; `std::lerp` bit agreement between Windows and Linux (§3.5).
- Whether hoisting the cascade culls is bit-exact against today's interleaved order (stage 5c, compare mode).
- 5e (Phase B): motion callbacks, the keyframe cache and attachments under parallel evaluation.
- The main-thread gain: 1.3–1.7× main-thread CPU if 30–50% of it is parallel with 6 helpers [inferred]. Stage 1 decides. The fps gain is only that, where the frame is CPU-bound.

**Risks:**

- **Races the audit misses**, i.e. job paths that reach an `LLPointer` copy or GL. The dev-build owner-thread and GL asserts and the compare mode guard against them.
- **Cleanup on the wrong thread.** Every cleanup and shutdown path must be checked for job threads, with a Release login test at every stage (3e47fa43c4).
- **Oversubscription on 4-core machines** with the GL driver's thread. The width rule leaves a core free, and the stage-2 gate includes a 4-core run.
- **Dual-CCD + SMT is not verified.** The dev machine is single-CCD 8C/8T on purpose (§3.1, §5.6), so the width rule's dual-CCD branch is designed but not measured. This is the stock setup most 7950X3D/9950X3D users run: fast domain = the V-cache CCD, background pools on the other CCD. It can be checked in one temporary BIOS session during stage 2, or confirmed later on another machine or from user reports.

**Costs.**

- CPU: dispatch about 0.03–0.1 ms per frame while helpers are awake. Helpers park between frames.
- Memory: about 1 MB of stack per helper plus a few MB of per-worker scratch.
- GPU: no change (no shader or pass changes), so the PCSS + 5% light and shadow budget is untouched.

**What users see.**

- Pre-Haswell CPUs get a clear message at install and at start-up instead of an illegal-instruction crash.
- 4–7 more threads, which park between frames.
- `ThreadPoolSizes` values for the computed pools are ignored (it is a debug setting).
- Rendering is bit-exact by design (compare mode, byte-identical captures).
- If `llwebrtc.dll` is missing, voice fails at first use instead of at start-up.
