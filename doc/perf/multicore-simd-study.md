<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Multi-core everywhere, SIMD where it pays (study, refreshed 2026-11-15)

Status: study and implementation plan. Nothing here changes the viewer. The branch `codex/multicore-simd-study-2026-11-15`
(from `origin/vkstorm-devel` 670593308b) carries:

- this document, rewritten on today's devel. It supersedes the 2026-11-10 version (PR #448, `d07e99ec0b`, which is
  cherry-picked first on this branch so the history shows what changed);
- the offline bench `scripts/perf/bench_multicore_simd.cpp` (standalone, not part of the viewer build), extended with the
  normal rotation, a structure-of-arrays box test (with NaN and inf boxes), the `Sleep(1)` wake floor and a parallel scaling
  run; special values (±0, ±inf, NaN, denormals) are spread through the whole input buffers;
- its output: the tables in §4.

Code references are file:line at `origin/vkstorm-devel` 670593308b (2026-11-15, after the projector pool P4 review round
#516). Each claim is tagged:

- **[code]**: read in the code at that commit;
- **[measured]**: from the bench on the dev machine (§4.1) or from a dated in-world measurement that is cited;
- **[inferred]**: reasoning or estimate, not checked.

The owner decisions of 2026-11-10 stand and are not re-opened here (§10.1). New standing rules since then: GL 4.6 base with no
fallback paths, no OpenGL compute (it costs fps in motion in this codebase), no Preferences entries, identical OpenGL output with
equivalence tests for renderer-free logic, literature first, upstream-conflict risk per stage, effort calibrated against git
history.

---

## 1. Summary

**Today.** The whole frame loop still runs on the main thread: object and avatar updates, moves, geometry rebuilds, culling,
state sorting and every render pass (§3.2). Since #448 the frame has gained the projector shadow pool: each projector tile is a
full cull, state sort and submit of its own on the main thread, now with an avatar layer on top (P4), all under one combined CPU
cap of 0.35 ms a frame (§3.3). The point-light plan will add more such light views. The existing thread pools are background
queues whose idle workers wake by polling `Sleep(1)`, so they cannot carry sub-millisecond frame work (§4.4).

**Stage 0 is done on every line** (release, devel, canary, rust): the exe entry point CPU check, the delay-loaded `llwebrtc.dll`,
the installer's `MinVersion=10.0` and AVX2 check, and the Linux wrapper check, with CI object checks (§3.5).

**The plan is unchanged in shape, re-ordered by today's code** (§7):

1. Tracy measuring build plus **one owner session in motion** in the crowded OpenSim scene, before any threading code.
2. `LL::FrameJobs`, our own fork-join job system (main thread takes part), and a width rule: `clamp(cores − 3, 2, 7)`, that is 5
   helpers on 8 cores, chosen to leave cores for the GL driver, decode and window threads (§6.2). The bench does **not** pick
   the width (§4.5); the stage-2 gate does.
3. Bit-exact AVX2 kernels: positions, normals and tangents, and an 8-boxes-per-register frustum test.
4. FMA: the memo only (§9). Not adopted.
5. 5a the audit (the multi-core trio), 5c parallel culling (world, sun cascades, **projector tiles**), then 5b geometry fill
   **after** the OpenGL arenas stage G5, as decided on 2026-11-11.

**What decides the design** (measured, §4):

- A bit-exact AVX2 affine transform (K1) is **at least 1.5× and up to about 3×** faster than the viewer's SSE one, with **0 floats
  different** out of 4.2 M, edge cases spread through the whole buffer. The ratio depends on the SSE reference's timing, which
  moved between two builds of the bench (§4.2); the AVX2 times themselves are stable. FMA changes **about 44 %** of the floats
  (1,862,692 of 4,194,304) for **12–24 %** more at 1 MB and above (about 30 % in L1-sized data).
- Testing eight boxes per register against the frustum gives **identical decisions** for 65,536 boxes (NaN and inf boxes
  included) and is **about 10×** faster as a kernel (14.4–15.0 → 1.44–1.48 ns per box). Only the AABB half of the viewer's cull
  test is benched. The cull gain in the viewer will be smaller, since the octree is walked node by node.
- An idle `LL::ThreadPool` worker wakes by `Sleep(1)`: **2.0 ms** per sleep with the viewer's 1 ms timer period (the Windows worst
  case; a worker polling at a random phase waits about 1 ms on average; non-Windows uses `usleep(1)`), **15.6 ms** without the
  timer period. A FrameJobs dispatch costs **1.1–2.5 µs** back to back.
- A geometry-fill-like job scales to **5.8× (median) with 5 helpers and 6.8× with 7**; a cull-like job to **6.1× with 5 and 7.2×
  with 6–7** (7 runs, §4.5). An earlier single run showed a dip at 7 helpers; seven runs did not reproduce it.
- An atomic ref+unref pair costs **7.5 ns** against 0.18–0.75 ns: job bodies must not copy `LLPointer`s.

**Effort, calibrated** (§7.3): Phase A is about **8–13 working days of pipeline time plus four owner sessions** in the optimistic
case (the projector pool's ratio), about two to three weeks of calendar time. The pessimistic case, with Stage 0's smaller
1.5–2× ratio, is **about 32–57 working days**, almost all of it in 5a, 5c and 5b. The 2026-11-10 figure of 13–17 weeks came from
human-day estimates; this project's history runs 1.5 to 32 times faster than such estimates, and the closest analogue, the
projector pool S0–P3, ran 6–8 times faster (§7.3).

---

## 2. What changed since #448 (2026-11-10)

| Area | Then | Now (devel 670593308b) |
|---|---|---|
| Stage 0 CPU check | planned | **done on all four lines** (§3.5) |
| Shadow baseline | 82080, PCSS on | Stochastic frame of 82178 (8a797c1d61) + 5 % (`AGENTS.md`) |
| Projector shadows | two spot slots, culled per frame | the projector pool: one tile per lamp in an atlas, cull + sort + submit per tile, avatar layer (P4), one 0.35 ms CPU cap for every cache together (`llprojectorshadowpool.h:1365-1379`) |
| Point lights | none | planned: six faces per lamp, one sphere cull per lamp and one submit per face (`doc/shadows/point-light-shadows-plan.md:213-231`, decision D7) |
| Stochastic engine | first port | one core, three pools (shadows, light, transparency); GPU side; its CPU cost is small [inferred] |
| OpenGL instrumentation | proposed | G0 done: per-pass CPU and GPU ms and GL counters behind `VKS_GL_STATS` (`llglsubmitstats.h`, `doc/gl/instrumentation.md`) |
| OpenGL modernisation | none | study accepted 2026-11-11: G1–G7, with **arenas (G5) before multicore 5b**; G1+ not started |
| Bench kit | none | the local OpenSim kit with NPC spawner, camera seat and dolly, Tracy procedure (`scripts/inworld/opensim-bench/README.md`) |
| Rust | crates on vkstorm-rust | the Rust port will be redone from the modernised C++; no crate is the target for SIMD today |
| GL floor | 3.3 | 4.6 base for new work, no fallback paths (the viewer still accepts GL 3 for Intel without 4.6, `llfeaturemanager.cpp:732`) |

---

## 3. Current state

### 3.1 Threads and work queues [code]

| Thread or pool | Width | What runs there | Where | Reusable for frame jobs? |
|---|---|---|---|---|
| Main thread | 1 | idle, display, all GL submission, coroutines (`mainloop` pump in `idle()`) | `llappviewer.cpp:1690-1840`, `:6007` | it takes part in every FrameJobs batch |
| `General` (LL::ThreadPool) | 3 coded, **1 from settings** | audio decode, GLTF material work, LLPerfStats, HTTP utility callbacks | `llappviewer.cpp:2608`; settings `settings.xml:16232-16246`; users `llaudiodecodemgr.cpp:583`, `llgltfmateriallist.cpp:547`, `llcorehttputil.cpp:313` | **No.** Idle workers poll `Sleep(1)` (`threadpool.cpp:36-58`; `usleep(1)` off Windows): up to 2 ms wake, about 1 ms on average [measured §4.4]; `std::function` per item |
| `ImageDecode` (LL::ThreadPool) | `clamp(cores−4, 2, 8)` = 4 here, or `FSImageDecodeThreads` | JPEG 2000 and other decodes | `llimageworker.cpp:70`, width `llappviewer.cpp:2644-2651` | No (same wake floor); its width is a background budget |
| `LLImageGL` (LL::ThreadPool, shared GL context) | 1 | texture and media uploads when `sEnabledTextures` (GL > 3.95) | `llimagegl.cpp:2681-2687`, `:294`; users `llviewertexture.cpp:855`, `llviewermedia.cpp:1692` | **No, never**: it holds a GL context; FrameJobs workers make no GL calls |
| `Window Thread` (LL::ThreadPool) | 1 | Win32 message pump | `llwindowwin32.cpp:5231` | No |
| `MeshRepoThread` + `MeshLodProcessing` | 1 + 2 | mesh header and LOD fetch, then inflate + `unpackVolumeFaces` in the pool | `llmeshrepository.cpp:959`, `:984`, `:1648`, `:2175`, `:4014` | No; MeshLodProcessing width is a background budget |
| `LLTextureFetch`, `LLTextureCache` (LLWorkerThread) | 1 each | HTTP texture fetch state machine, disk cache | `lltexturefetch.h:56`, `lltexturecache.h:41` | No |
| HTTP service (llcorehttp) | 1 | curl multi | `_httpservice.cpp:203` | No |
| LFS, disk purge, physics decomposition, WebRTC, plugins, watchdog | 1 each | I/O and helpers | `lllfsthread.h:42`, `lldiskcache.h:207`, `llmeshrepository.h:116`, `llvoicewebrtc.cpp:300`, `llpluginprocessparent.cpp:58`, `llwatchdog.cpp:37` | No |
| `LLGLWorkerThread` (shared context) | 0 | dead upstream code | `llvertexbuffer.cpp:75` (`ENABLE_GL_WORK_QUEUE 0`), `:227-251` | No |

- **Thread priorities and affinity.** None in use for pools: the `LLThread` affinity mask is compiled out (`llthread.cpp:162-168`,
  `#if 0`). The window code raises one thread's priority (`llwindowwin32.cpp:1098`). Nothing uses CPU sets or
  `GetLogicalProcessorInformationEx`.
- **Timer period.** The viewer calls `timeBeginPeriod(1)` (`llwindowwin32.cpp:5418`), so `Sleep(1)` is about 2 ms rather than 15.7 ms.
- **Settings.** `ThreadPoolSizes` is persisted (`Persist 1`) with `General = 1`; `initThreads` writes the map back
  (`llappviewer.cpp:2650-2651`), so saved files keep their values. A new width must be computed in code, not shipped as a default
  [code; inferred for the saved files].
- **Other threads not in the table**, none usable for frame jobs: the model-loader and log-chat threads and the websocket
  threads (background network and file work).
- **FPU state.** Only the main thread sets flush-to-zero and round-to-nearest (`llvector4a.h:63-66`, called at
  `llappviewer.cpp:883`). MXCSR is per thread: every FrameJobs helper must set the same, or results can differ in denormals.

### 3.2 The main-thread frame [code; shares inferred]

Order of one frame (`llappviewer.cpp:1690-1840`): native events and input, the `mainloop` post and coroutine resume, `idle()`,
then `display()`, then the reflection-probe manager update and snapshot floaters.

No per-part profile of today's devel exists. The only measured frame numbers are whole frames: **7.93 ms** on devel at the
projector-pool measurement place against **8.61 ms** for the 82178 release (RTX 5070 Ti, 1440p, 2026-11-14): the P2c row, devel
at `38176bf35c` (`doc/shadows/projector-pool-budget.md:191-195`), so **older than today's devel**, which also has P3 and P4. The
**0.07 ms CPU per projector tile render** is from the P2b session of 2026-11-14 (same document, `:171`). The
shares below are estimates for a crowded scene with about 40 avatars; stage 1 replaces them with measurements.

| Part | Where | Share of main-thread CPU (crowded) |
|---|---|---|
| **idle** | `LLAppViewer::idle` | **30–45 %** [inferred] |
| network: message decode, object updates | `idleNetwork` `llappviewer.cpp:6757-6830`; object creation in `llviewerobjectlist.cpp` | 3–10 % |
| object idle updates (avatars included) | `gObjectList.update` `llappviewer.cpp:6270` → `llviewerobjectlist.cpp:1013-1142` (list copied first, `:1083-1101`) | |
| — avatar `idleUpdate` / `updateCharacter` | `llvoavatar.cpp:3127`, `:5723`; motions `:5820-5825`; skeleton `:5850` | 10–25 % with 40 avatars |
| region and VO cache | `updateRegions` `llappviewer.cpp:6237`, `:6331`; cache read at region creation `llviewerregion.cpp:847` | 1–5 %; spikes on arrival |
| moves, octree balance | `gPipeline.updateMove()` `llappviewer.cpp:6368` → `pipeline.cpp:2439` | 1–5 % |
| particles | `updateParticles` `llappviewer.cpp:6371` (own plan: particle CPU survey) | 1–10 % by scene |
| apparent angles, LOD | `updateApparentAngles` `llappviewer.cpp:6401` → `llviewerobjectlist.cpp:932` | 1–3 % |
| **render preparation** | `display()` | **35–50 %** [inferred] |
| geometry create and rebuild (5 % frame budget) | `llviewerdisplay.cpp:887-895` → `createObjects` `pipeline.cpp:2257`, `updateGeom` `:3175`, `rebuildPriorityGroups` `:3149` | 3–15 %; dominant while settling |
| world cull | `llviewerdisplay.cpp:923` → `pipeline.cpp:2798-2895` | 4–8 % |
| sun shadows: 4 cascades × (cull + sort + submit) | `llviewerdisplay.cpp:954` → `generateSunShadow` `pipeline.cpp:12758`; cascades `:12575` (Stochastic) or `:12197` (Legacy); each `renderShadowImpl` `:11625-11760` | 10–20 % |
| projector pool tiles (P4) | `mProjectorShadows.update` `pipeline.cpp:12980` → `llprojectorshadows.cpp:1099`, tile `:637-719`, avatar layer `:802-865` | ≤ 0.35 ms by the cap (`llprojectorshadowpool.h:1377`) |
| texture list update (2–5 ms budget) | `llviewerdisplay.cpp:1004-1006` → `llviewertexturelist.cpp:1290-1344` | 3–10 % |
| world state sort, inline `rebuildMesh` | `llviewerdisplay.cpp:1032` → `pipeline.cpp:3496-3622`; `rebuildMesh` at `:3541`, `:3603` | 8–15 % |
| reflection probe face (own cull + 2 cascades) | `llreflectionmapmanager.cpp:757`; `display_cube_face` `llviewerdisplay.cpp:1319` | 3–8 % |
| mirror (hero probe), when present | `llheroprobemanager.cpp:290` (up to 6 faces) | 0–20 % |
| **GL submission** (G-buffer, lighting, alpha, post) | `llviewerdisplay.cpp:1172`, `:1198` | **15–30 %** [inferred]; stays on the main thread |
| **UI** | HUD cull and sort `llviewerdisplay.cpp:1494`, `:1522`; UI draw | 3–8 % [inferred] |

Coroutines and `mainloop` work run only inside `idle()` (`gMainloopWork.runFor`, `llappviewer.cpp:6007`) and at the
`mainloop.post` / `llcoro::suspend` step (`:1727-1740`). Object kills, region removal and object shifts are handled in `idle()`.
A fork-join batch inside `display()` therefore cannot overlap any of them [code].

### 3.3 Cull and state-sort passes per frame [code]

| Pass | Count | Where |
|---|---|---|
| World view | 1 | `llviewerdisplay.cpp:923`, `:1032` |
| Sun cascades | 4 (2 during a cube snapshot) | `pipeline.cpp:12575` (`gCubeSnapshot ? 2 : 4`), cull and sort in `renderShadowImpl` `:11665-11667` |
| Projector tiles (Stochastic) | 0–n under the 0.35 ms cap; each is a static pass, then an avatar-layer pass per tile holding avatars | `llprojectorshadows.cpp:689`, `:858`; partition filter `pipeline.cpp:2847-2868` |
| Spot slots (Legacy) | 0–2 | `pipeline.cpp:13125` |
| Reflection probe | 1 face per frame: own cull + 2 cascades | `llreflectionmapmanager.cpp:757` |
| Mirror | up to 6 faces × (1 + 2) | `llheroprobemanager.cpp:290` |
| HUD | 1 | `llviewerdisplay.cpp:1494`, `:1522` |

So 9 passes plus the projector tiles without a mirror, up to 27 plus tiles with one. Each shadow pass is cull → sort → submit,
interleaved. The point-light plan will add one sphere cull per lamp with dirty faces (D7).

### 3.4 Hazards for parallel work [code]

**Culling makes GL calls and writes shared state.**

- `LLOctreeCull::earlyFail` calls `checkOcclusion` (`llspatialpartition.cpp:1067-1075`), which reads
  `glGetQueryObjectuiv(…, GL_QUERY_RESULT_AVAILABLE)` and may block on `GL_QUERY_RESULT` (`llvieweroctree.cpp:1108-1154`). The state
  sort calls it again (`pipeline.cpp:3526`, `:3592`). Shadow passes switch occlusion off (`pipeline.cpp:11633-11634`), so the
  cascade and tile culls make no GL calls through this path.
- `LLVOCachePartition::cull` writes the **shared camera's** region planes (`llvocache.cpp:1090`) and the region's visible lists.
- Every partition cull calls `rebound()` on its root (`llspatialpartition.cpp:1444`, `:1466`).
- `markNotCulled` writes `mNumVisibleNodes++` and the occluder lists (`pipeline.cpp:2897-2930`); `sCull` is a global pointer to the
  current `LLCullResult` (`grabReferences`, `:2833`).
- Globals that steer a cull: `LLViewerCamera::sCurCameraID`, `sShadowRender`, `sUseOcclusion`, `gCubeSnapshot`,
  `sLocalShadowPass`, `RenderSpotLight`.
- **Visibility is one slot per camera id** (`mVisible[sCurCameraID]`; ids `llviewercamera.h:49-58`). **All projector tiles share
  `CAMERA_SPOT_SHADOW0`** (`llprojectorshadows.cpp:687`), so tiles cannot be culled in parallel with each other without per-job
  visibility records merged afterwards.
- **One partition reads another's result.** `LLSpatialBridge::setVisible` reads the parent avatar's visibility and, in the
  avatar layer, `isBuddy()` and a `static LLCachedControl` (`lldrawable.cpp:1520-1564`). The avatar partition is culled after the
  bridge partition (`llviewerregion.h:100-101`), so serial code reads last frame's value. A function-local static
  `LLCachedControl` first touched on a worker would read `gSavedSettings` off the main thread.
- **Static results.** The tile render and the cascades keep `static LLCullResult` objects (`llprojectorshadows.cpp:686`,
  `llviewerdisplay.cpp:918`); parallel culls need one result per job.

**Geometry fill makes GL calls at the edges and uses static scratch.**

- Buffer creation calls `glGenBuffers` / `glBufferData` (`llvertexbuffer.cpp:263-294`, `:459`); the upload is
  `glBufferSubData` in `flushBuffers` (`:1045-1056`, `:1402-1420`); `_mapBuffer` pushes onto the static `sMappedBuffers`
  (`:1042`, `:1436`). `mapVertexBuffer` returns CPU memory (`:1306`, `mMappedData` `llvertexbuffer.h:293`).
- `rebuildGeom` uses static face arrays (`sSimpleFaces`, `llvovolume.cpp:5566`, `:5603`, filled `:6330-6428`), so it is not
  re-entrant; `genDrawInfo` creates buffers (`:6688`). `rebuildMesh` runs inline from the state sort (`pipeline.cpp:3541`,
  `:3603`) and flushes (`llvovolume.cpp:6624`).
- Inside `getGeometryVolume` (`llface.cpp:1277`): `getMaterialParams()` returns `LLMaterialPtr` **by value**
  (`lltextureentry.h:164`; used `llface.cpp:1475`, `:1533`): a non-atomic ref and unref of a shared `LLMaterial`. `genTangents`
  writes tangents lazily into a shared `LLVolume` (`llface.cpp:1677`, `:1724`, `:2035`, `:2187` → `llvolume.cpp:2025`).

**Reference counting.** `LLRefCount::mRef` is a plain `S32` (`llrefcount.h:81`); `LLThreadSafeRefCount` exists (`:128`) but the
scene types (`LLViewerObject`, `LLDrawable`, `LLSpatialGroup`, `LLVertexBuffer` `llvertexbuffer.h:92`, `LLVolume`, `LLMaterial`
`llmaterial.h:36`) use the plain one.

**The projector pool is main-thread only by design.** `LLLocalShadowGL::Scope` and every pool entry point return at once off the
main render thread (`lllocalshadows.h:26-29`); `renderTile` changes the framebuffer, viewport, matrices and the camera id
(`llprojectorshadows.cpp:650-690`). Only the cull of a tile can move to jobs; its submit stays serial.

### 3.5 Stage 0: the CPU check is done on every line [code]

| Item | Release `ab8740c32e` | Devel `670593308b` | Canary `59034a036e` | Rust `86a6f4ad9e` |
|---|---|---|---|---|
| Installer `MinVersion=10.0` | `installer_template.iss:52` | same | same | same |
| Installer AVX2 check, `IsProcessorFeaturePresent(40)`, `SuppressibleMsgBox` | `:142-156` | same | same | same |
| Linux wrapper x86-64-v3 check (`ld.so --help`, `/proc/cpuinfo` fallback, test hooks) | `wrapper.sh:3-16` | same | same | same |
| Exe entry `/ENTRY:vkstormEntry` (`llcpucheck_win.cpp`) | `newview/CMakeLists.txt:2016`, `:2351` | `:2026`, `:2361` | `:2018`, `:2387` | `:2015`, `:2350` |
| `/DELAYLOAD:llwebrtc.dll` | yes | yes | yes | yes |
| CI object check (`check_cpucheck_obj.py`, instruction floor and TLS callbacks) and wrapper test (`test_wrapper_cpu_check.py`) | yes | yes | yes | yes |

Landed: release `0cfa65324a` (2026-11-11 01:48), review `0f2343e895` (02:13), follow-ups `5147ddbc53` and `cd7f3ccd14`
(authored 11-11 14:01); devel `031bca7527`, `c26d9304a6`, `46ca16829e`, `72a7d95dad`; canary `7079128a63` … `fdcece1cf2`; rust
`5103e49b9c` … `54a26574e1` (all 2026-11-12). The gate evidence recorded in those PRs (SDE runs, installer test) was not re-run
for this study.

### 3.6 Measuring tools that exist [code]

- **Tracy 0.11.1** (`indra/cmake/Tracy.cmake:7-40`), off by default, on-demand when on. Zones cover the frame (`df idle`, `df Display`,
  `df MainLoop`), `updateCull`, `stateSort` and its inner loops, `renderShadowImpl`, `generateSunShadow`, object list, avatar and
  volume code (`LL_RECORD_BLOCK_TIME` maps to Tracy zones, `llprofiler.h:108`). **The projector pool has no zones**
  (`llprojectorshadows.cpp`); its own CPU timer and stats API cover it. Profiling zones are allowed on release
  (`check_release_hooks.py:14-24` forbids none of them).
- **G0 instrumentation** (`VKS_GL_STATS`, devel only): per view and pass CPU and GPU ms and GL counters, Frame Profile JSON and
  LEAP (`doc/gl/instrumentation.md`).
- **The local OpenSim bench kit**: NPC spawner (30 NPCs; OpenSim's default cap is 40), camera seat and dolly, scenes, Tracy capture
  and report procedure (`scripts/inworld/opensim-bench/README.md`). Its build line is the one stage 1 uses.
- `LLSceneMonitor` (scene settle), `LLAgentPilot` (recorded camera paths), `EmulateCoreCount` (`llappviewer.cpp:2633`).

---

## 4. Measurements

### 4.1 Hardware and method [measured]

- AMD Ryzen 9 9950X3D, **CCD1 and SMT off on purpose** (8 cores, 8 logical processors, 96 MB L3 on the V-cache CCD), Windows 11
  Pro 10.0.26300, power plan Balanced. Every number here is for that configuration; the stock dual-CCD + SMT setup is not
  measured (§11).
- MSVC 19.51.36260 (Visual Studio 18), `cl /O2 /Oi /arch:AVX2 /fp:precise /std:c++20 /EHsc`, the viewer's Windows code-generation
  flags. Every bench thread sets flush-to-zero and round-to-nearest, as the viewer's main thread does.
- Kernel timings are the best of 3 (affine, rotate) or 5 (box test) runs; dispatch and scaling are medians. Two full runs agreed to
  within about 5 % except where noted. No viewer build ran at the time (checked: no `cl`, `link` or `MSBuild` process). **Machine load** during the 2026-11-15
  re-runs: background desktop software only (editor, chat and peripheral services), total CPU 14–22 % in the samples taken before
  and after the scale runs; Balanced power plan, no frequency pinning. That load is the likely source of the run-to-run spread in
  §4.5.

### 4.2 K1 affine transform and K2 rotation, SSE against bit-exact AVX2 [measured]

`LLMatrix4a::affineTransformSSE` (`llmatrix4a.h:194-209`), as used per vertex in `llface.cpp:2126-2132`, and `LLMatrix4a::rotate`
(`llmatrix4a.h:178-192`), as used for normals and tangents (`llface.cpp:2161-2178`, `:2196-2216`). AVX2 handles two vertices per
register with exactly the same per-lane multiply and add order. The inputs include ±0, ±inf, NaN, denormals and values near
±3·10³⁸, **spread through the whole buffer** (every 41st vertex rotates through them and the three lanes), not only the first
64 vertices as in the first version of this bench.

| Vertices | K1 SSE ns/v | K1 AVX2 ns/v | K1 speed-up | K1 floats different | K1 FMA ns/v | FMA floats different | K2 SSE ns/v | K2 AVX2 ns/v | K2 floats different |
|---|---|---|---|---|---|---|---|---|---|
| 1,024 (16 KB) | 0.784–0.833 | 0.261–0.269 | 2.94–3.19× | 0 | 0.188–0.189 | 1,806 of 4,096 | 0.398–0.401 | 0.207–0.208 | 0 |
| 65,536 (1 MB) | 0.776–0.797 | 0.291–0.345 | 2.28–2.73× | 0 | 0.232–0.279 | 116,585 of 262,144 | 0.399–0.420 | 0.254–0.263 | 0 |
| 1,048,576 (16.8 MB) | 0.803–0.825 | 0.276–0.312 | 2.58–2.99× | 0 | 0.235–0.259 | 1,862,692 of 4,194,304 | 0.397–0.399 | 0.258–0.270 | 0 |

Four runs of the current build (the table). The first build of the bench (the one behind the first draft of this study: special
values only in the first 64 vertices) gave K1 SSE 0.44 and K2 SSE 0.75 ns/v, i.e. 1.48–1.87× for K1.

- **K1:** bit-exact, 0 floats different, special values now through the whole buffer. Speed-up 2.3–3.2× in this build and
  1.48–1.87× in the first build.
- **K2:** bit-exact, 0 floats different. Speed-up 1.48–1.93× in this build, 3.2–3.9× in the first build.
- **The SSE reference is not stable across builds.** Between the two builds the SSE K1 loop went from 0.44 to 0.80 ns/v and the
  SSE K2 loop from 0.75 to 0.40 ns/v, although both loops have the same instruction shape (checked in the listing for the first
  build). The cause was not isolated (the builds differ in the input data and in where the loops land in the code). The AVX2
  times are the stable figures: **K1 0.25–0.35, K2 0.20–0.27 ns/v**. The speed-up to carry is therefore **at least 1.5× for both
  kernels**; stage 3 measures the real gain inside the viewer. The #448 run's 1.83–3.44× for K1 fits the same spread.
- **FMA:** 2.8–4.4× against this build's SSE reference, but only **12–24 %** beyond the bit-exact AVX2 version at 1 MB and above
  (about 30 % in L1-sized data), and it changes about 44 % of the floats.

### 4.3 K5 box against frustum: one box at a time against eight per register [measured]

`LLCamera::AABBInFrustum` (`llcamera.cpp:207-244`): for each plane with a valid octant mask, the nearest corner along the normal
decides "outside", the farthest decides "partly in"; `dot3` is `(x·px + y·py) + z·pz` (`llvector4a.inl:315-326`) and `d` is the
sign-flipped `w` (`llsimdtypes.inl:62-67`). The SoA version computes the same per-lane operations for eight boxes and ORs the plane
tests (no early exit).

| Boxes | Planes | One box at a time | Eight per register | Speed-up | Decisions different |
|---|---|---|---|---|---|
| 65,536 (outside 61,476 / partial 3,326 / inside 734) | 6 | 14.4–15.0 ns/box | 1.44–1.48 ns/box | 9.9–10.1× | 0 |

Four runs. The boxes include denormal radii and, new in this version, boxes with a NaN centre or radius or an inf centre or
radius (every 1,009th to 1,031st box, about 0.5 % in all); both versions give false for every NaN comparison and the decisions
are identical. The boxes are still 94 % outside.

**Coverage.** Only the AABB half of the viewer's real test is benched: `LLOctreeCull::AABBInFrustumNoFarClip` is
`llmin(AABBInFrustum, AABBSphereIntersect)` (`llspatialpartition.cpp:1089-1100`). The sphere half, a real camera's plane set and
the early-out mix of a real octree walk are not covered, so the stage-3 bit-for-bit test must cover the whole function.

The viewer walks the octree one node at a time through virtual calls (`llspatialpartition.cpp:1062-1160`), so the in-viewer gain
needs the eight children of a node (or a node's object bounds) laid out together; it will be much smaller than 10× [inferred].
The random boxes here are mostly outside; real culls see more partial and inside nodes [inferred].

### 4.4 Wake floor of today's pools against FrameJobs dispatch [measured]

| What | Median | p99 |
|---|---|---|
| `Sleep(1)`, default timer | 15.64 ms | 16.45 ms |
| `Sleep(1)` after `timeBeginPeriod(1)` (the viewer's state, `llwindowwin32.cpp:5418`) | 2.00 ms (min 1.00 ms) | 2.52 ms |
| FrameJobs dispatch of 64 trivial chunks, back to back, 1–7 helpers | 1.1–2.5 µs | 1.8–13.7 µs |
| Same after 2 ms idle (helpers parked), spin window 0 µs | 8.8–33.6 µs | 78–205 µs |
| Same after 2 ms idle, spin window 50 µs | 8.7–84.4 µs | 78–222 µs |

An `LL::ThreadPool` worker that has gone idle waits in the fiber scheduler's `suspend_until`, which is `Sleep(1)` on Windows
(`threadpool.cpp:36-58`). Posting a frame job to `General` would wait **up to 2 ms** for it to wake on Windows (a whole frame budget at 125 fps). The 2.0 ms
figure is one full `Sleep(1)`; a worker polling at a random phase waits about 1 ms on average. The branch is Windows-only:
elsewhere the loop uses `usleep(1)` (`threadpool.cpp:44-47`) and the floor is lower, which this bench does not measure.
FrameJobs must keep its helpers awake (spinning) through the CPU-preparation window of `display()` and park them at swap; that
design has unmeasured side effects (§6.2, §11).

### 4.5 Parallel scaling on the 8 cores [measured]

`parallel_for` over the fork-join pool, main thread taking part, spin window 50 µs. Fill: 512 groups of 1,500 vertices, position
transform + normal rotation into separate outputs (12 MB in, 24 MB out). Cull: 65,536 boxes in 256 chunks. The job shapes are
assumptions, not viewer captures. **Seven runs** of `scale` (viewer flags, machine load as in §4.1); each cell is the median of
15 (fill) or 31 (cull) dispatches within a run, then min / median / max over the seven runs. Serial: fill 0.785–0.792 ms, cull
0.925–0.929 ms.

| Helpers (+ main) | Fill ms (min / median / max) | Fill speed-up (min / median / max) | Cull ms (min / median / max) | Cull speed-up (min / median / max) |
|---|---|---|---|---|
| 1 | 0.401 / 0.404 / 0.408 | 1.94 / 1.95 / 1.97× | 0.462 / 0.466 / 0.468 | 1.98 / 1.99 / 2.01× |
| 2 | 0.266 / 0.269 / 0.280 | 2.81 / 2.94 / 2.97× | 0.298 / 0.302 / 0.304 | 3.05 / 3.07 / 3.11× |
| 3 | 0.202 / 0.206 / 0.228 | 3.47 / 3.82 / 3.88× | 0.224 / 0.225 / 0.230 | 4.04 / 4.12 / 4.13× |
| 4 | 0.160 / 0.162 / 0.174 | 4.52 / 4.85 / 4.93× | 0.178 / 0.181 / 0.185 | 5.01 / 5.13 / 5.20× |
| 5 | 0.135 / 0.136 / 0.159 | 4.97 / 5.77 / 5.84× | 0.150 / 0.151 / 0.180 | 5.13 / 6.13 / 6.17× |
| 6 | 0.116 / 0.122 / 0.139 | 5.68 / 6.50 / 6.81× | 0.128 / 0.130 / 0.147 | 6.30 / 7.14 / 7.24× |
| 7 | 0.105 / 0.116 / 0.141 | 5.60 / 6.78 / 7.51× | 0.128 / 0.129 / 0.151 | 6.15 / 7.18 / 7.23× |

**Reading.** In the seven runs the speed-up rises to 7 helpers (every core busy) for both jobs. The dip at 6–7 helpers that the
first draft of this study showed (one run: 3.5× and 3.0× for the fill, 4.4× for the cull at 7) came back in none of the seven
runs; the lower minima in the table (fill 5.6× at 7, 5.0× at 5) show that a run can still be hit, most likely by the background
load (§4.1). So **the table does not pick the width**: with the bench alone on the machine more helpers are always faster, and the
bench has no GL driver thread, decode pools or window thread competing. The 5-helper default is justified in §6.2 by the cores
those threads need, and the final width is a **stage-2 gate result** measured inside the viewer.

### 4.6 Reference counting [measured]

| ref+unref pair | Non-atomic | Atomic (`fetch_add` / `fetch_sub`) |
|---|---|---|
| 64 k objects in random order (4 MB) | 0.72–0.75 ns | 7.54–7.59 ns |
| One hot object | 0.18 ns | 7.45–7.53 ns |

An atomic `LLRefCount` would cost about 7 ns on every pair in the viewer [inferred: 0.7 ms a frame at 100 k pairs]. So the rule
stays the other way round: job bodies never copy `LLPointer`s.

### 4.7 FMA in today's binaries [measured on 2026-11-08's Release exe, not repeated]

`dumpbin /disasm` of the 2026-11-08 Release exe had one FMA instruction, a `vfmadd213ss` in `std::lerp` (re-exported as the
viewer's `lerp`, `llmath.h`). MSVC `/fp:precise` does not fuse on its own; Linux builds use `-ffp-contract=off`
(`indra/cmake/00-Common.cmake`). Not re-run on today's binary.

---

## 5. Candidates

### 5.1 Parallel candidates

| # | Candidate | Kind | Data dependencies and shared state | Hazards | Expected gain | Bit-exactness |
|---|---|---|---|---|---|---|
| C1 | Sun cascade culls (4) hoisted and run together | data-parallel, one job per cascade, subtrees inside | each cascade has its own camera id (`SUN_SHADOW0..3`) and result; `rebound()` and region planes shared | `static` results, `sCull`, `sCurCameraID` globals; bridge reads avatar visibility | 10–20 % of main-thread CPU is shadow work; the cull part of it parallel at 4–6× [inferred] | cull lists must be identical in serial order; hoisting changes the order of `rebound()` against cascade renders: prove with compare mode |
| C2 | World cull: regions × partitions, octree subtrees | data-parallel | as C1, plus occlusion queries (GL) and `LLVOCachePartition` region planes | GL query reads must move to a main-thread pre-pass; `mNumVisibleNodes`, occluder lists | 4–8 % share at up to 5× [inferred] | merge fragments in depth-first order |
| C3 | Projector tile culls (and the avatar-layer culls) | data-parallel across the frame's chosen tiles | all tiles share `CAMERA_SPOT_SHADOW0`, `static LLCullResult`, the LOD pin book (`mPins`, main thread) | visibility slot and pin writes must become per-job records applied serially; submit stays serial | **more tiles inside the same 0.35 ms cap** [inferred: the cull share of a 0.07 ms tile is unmeasured]; the point-light faces get the same | identical tile contents (cull lists) |
| C4 | Geometry fill (`getGeometryVolume` per group) | data-parallel, one job per spatial group | buffers belong to one group; static `sSimpleFaces`; `sMappedBuffers` | `getMaterialParams` by value, lazy `genTangents`, GLTF selection path allocates buffers | settle time: more groups per 5 % budget; 4.8× on the fill share [measured shape, §4.5] | same bytes in the buffers (compare mode) |
| C5 | State sort, pure part | data-parallel then serial apply | `markRebuild`, pool enqueue, LOD timestamps, `markVisible` | many globals; render-type masks | 8–15 % share [inferred] | Phase B |
| C6 | Avatar animation (`updateMotions` + skeleton) | data-parallel per avatar | keyframe cache (`LLKeyframeMotion` shared data), attachments call `gPipeline.markMoved` | motion callbacks, sound triggers, `LLPointer` in motions | 10–25 % share with 40 avatars [inferred] | Phase B |
| C7 | Prim volume generation (`LLVolume::generate`, `llvolume.cpp:2051`; faces `:5146`) | background per volume | `LLVolumeMgr` has a mutex (33 uses in `llvolumemgr.cpp`); faces written once | LOD switches wait for the result | settle and teleports [inferred] | same code on another thread with the same MXCSR: identical |
| C8 | Texture priority pass (`llviewertexturelist.cpp:1290-1344`) | data-parallel compute + serial apply | `LLPointer` entries, fetch state | refcounts, `updateFetch` side effects | part of a 2–5 ms budget [inferred] | Phase B, low |
| C9 | VO cache read on region arrival (`llviewerregion.cpp:847`) | background | region's cache map | region deletion while reading | teleport hitch [inferred] | Phase B |

### 5.2 SIMD candidates beyond `LLVector4a`'s SSE

| # | Kernel | Where | Status | Gain | Note |
|---|---|---|---|---|---|
| K1 | position transform + texture-index select | `llface.cpp:2126-2132` | measured | at least 1.5× (1.48–3.2× across two builds), bit-exact | stage 3 |
| K2 | normal and tangent rotation | `llface.cpp:2161-2178`, `:2196-2216` | measured, reference anomaly | expect 1.5–1.9×, bit-exact | stage 3 |
| K5 | box against frustum, eight per register | `llcamera.cpp:207-290`, callers `llspatialpartition.cpp:1089-1155` | measured kernel | about 10× kernel (AABB half only), decisions identical; needs a node layout for a real gain | stage 3 kernel, used by 5c |
| K4 | skin matrix palette (`matMulUnsafe` per joint) | `llskinningutil.cpp:128-182`, called lazily per draw (`llvoavatar.cpp:11331`) | not measured | small per call (≤ 110 joints) [inferred] | measure the call count in stage 1; G3 (skin-palette UBO) cuts the uploads first |
| K6 | CPU skinning of rigged volumes | `llvovolume.cpp:5276-5390` | only for picking and edit (`:5028-5114`, `llselectmgr.cpp:6906`) | low | not in Phase A |
| — | volume, normal and tangent generation | `llvolume.cpp:2025`, `:2051`, `:5146`, `:6022`, `:6815` | irregular loops | low as SIMD; better as C7 | Phase B |
| — | octree traversal | `llspatialpartition.cpp:1062-1280` | pointer chasing | only with a SoA child layout | Phase B |
| — | mesh decompression | zlib-ng 2.2.5 (`autobuild.xml:3160`) on `MeshLodProcessing` | already SIMD inside the library | none to add | — |
| — | image scaling, JPEG 2000 | `ImageDecode` threads | off the main thread | none for the frame | — |
| — | LLSD parsing | mostly worker threads | not measured | low | — |

---

## 6. Design

### 6.1 Literature

- **Fork-join over a fixed helper set with the caller taking part:** Intel TBB `parallel_for` (Reinders, *Intel Threading Building
  Blocks*, 2007), Cilk work-first scheduling (Frigo, Leiserson, Randall, PLDI 1998), enkiTS (Doug Binks). Chosen: the viewer's
  passes are short, nested inside one phase, and need no fibers.
- **Fibers:** Naughty Dog, *Parallelizing the Naughty Dog Engine Using Fibers* (Christian Gyrling, GDC 2015); FiberTaskingLib.
  Not chosen: they suit frame-pipelined engines, and LL's coroutines already use boost fibers on the main thread
  (`threadpool.cpp:26`), so a second fiber layer would interact with `llcoro::suspend`.
- **Data-oriented culling with SoA bounds:** Daniel Collin, *Culling the Battlefield: Data Oriented Design in Practice* (DICE, GDC
  2011); Unity DOTS `FrustumPlanes` / Burst culling jobs. They give the eight-boxes-per-register layout K5 measures.
- **Plan / fill / flush of vertex data with per-thread staging:** the Vulkan staging model and Unity's `NativeArray` writers; in
  OpenGL the CPU shadow buffer (`mMappedData`) already plays that role, so no GL change is needed.

### 6.2 Stage 2: `LL::FrameJobs` and the width rule

As in #448, with two updates (the width rule, and the spin window's risks).

- **llcommon, our own code, about 400 lines** (new files `llframejobs.h/.cpp`, SPDX GPL-3.0-or-later):
  - fixed helper threads, the main thread takes part; `parallel_for(count, grain, fn)` with dynamic chunks; each index writes its
    own result slot;
  - helpers spin through `beginFrameWork()` … `endFrameWork()` (the CPU-preparation window of `display()`; the hooks are in
    `llviewerdisplay.cpp`) and park on `std::atomic::wait` at swap. No `Sleep(1)`;
  - jobs below about 50 µs of work run serially (wake after idle is 6–86 µs median, §4.4);
  - per-helper set-up: `LLVector4a::initClass()` (FTZ, rounding), Tracy thread name, `LLThread`-style naming;
  - exceptions caught and rethrown on the main thread;
  - dev-build guards (`#ifdef`, off release): `FrameJobs::inJob()` asserts in `LLVertexBuffer::genBuffer` / `flushBuffers`, `gGL`,
    and an owner-thread assert in `LLRefCount::ref` / `unref`. **These guards are part of stage 2** (files `llrefcount.h`,
    `llvertexbuffer.cpp`, `gGL`; §7.1), since stage 2's own stress tests need them; 5a then adds the compare mode on top.
- **Not built on `LL::WorkQueue`:** the 2 ms wake floor (§4.4), `std::function` per item, and a joining main thread could run
  arbitrary `mainloop` work.
- **Job-body rule:** no `LLPointer` copies, no GL, no `markDead` / `killObject`, no `LLCachedControl` first use, no floating-point
  reduction across jobs (sums run in a serial apply step in serial order). The main thread deletes nothing while a batch runs.
- **Width rule** (computed at every start-up; the persisted `ThreadPoolSizes` values for these pools are overwritten in code):
  - fast domain = the cores sharing the main thread's L3 (one CCD on AMD parts, the V-cache CCD on X3D; on Intel hybrid parts the
    P-cores and E-cores share one L3, so the domain is the whole package there and the helpers should prefer P-cores), from
    `GetLogicalProcessorInformationEx` on Windows and `/sys/devices/system/cpu` on Linux;
  - **frame helpers = `clamp(fastPhysicalCores − 3, 2, 7)`**: 5 on 8 cores, 3 on 6, 2 on 4 (#448 had `− 2`, i.e. 6 on 8). The
    `− 3` is a reservation, not a bench result: the **main thread** is the sixth worker, and the cores left over are for the **GL
    driver thread** (threaded optimisation of the vendor driver), the **decode pools** (`ImageDecode`, `MeshLodProcessing`, §3.1)
    and the **window thread**. The scaling table (§4.5) rises to 7 helpers on an otherwise empty machine, so it argues for more
    helpers, not fewer; the viewer's other threads argue for the reserve. **The final width is a stage-2 gate result**:
    S1 frame time and 1 % lows at 3, 4, 5, 6 and 7 helpers in the viewer, the default being the smallest width within noise of
    the best;
  - background pools below normal priority: ImageDecode `clamp(logical − helpers − 2, 2, 8)`, General 2, MeshLodProcessing
    `clamp(logical / 8, 2, 4)`;
  - when the fast domain is the whole machine (as on the dev machine), background pools share its cores at lower priority.
- **Gate:** no fps loss on S1–S3; settle not slower; 1 % lows not worse; the width sweep above; a 4-core run on the dev machine
  with the process affinity limited to four cores (`start /affinity F`), which also exercises the width rule's small branch.
- **Risk: spinning through the window.** Helpers spin through the whole preparation window, serial stretches included; the bench
  measured only a 50 µs spin. Power draw, X3D boost behaviour and contention with the GL driver thread (spinning helpers take
  core time, cache and SMT-less execution resources the driver thread wants) are unmeasured. The stage-2 gate covers the fps
  effect; it must also compare package power and the driver thread's CPU time, and the spin window (50 µs, or park at once
  during long serial stretches) is a tuning point of that gate.

### 6.3 Stage 3: bit-exact AVX2 kernels

- K1, K2 and K5 move into `llmath` as pure functions (new file, e.g. `llsimdkernels.h`); the SSE versions stay in the unit test as
  the reference; tests compare bit for bit, including NaN, ±inf, denormals and −0 (§4.2 inputs).
- Rules: identical per-lane operation order; multiply and add stay separate; no reduction across lanes; `_mm256_rsqrt_ps` only where
  SSE already used `_mm_rsqrt_ps`.
- **CI gate:** count `vfmadd`, `vfmsub` and `vfnm*` in `dumpbin /disasm` (Windows) and `objdump -d` (Linux) of the exe; the count
  must not rise above today's.
- The K5 bit-for-bit test covers the whole `AABBInFrustumNoFarClip` (box and sphere halves) with NaN and inf boxes and a real
  camera's planes, not only the AABB half benched here (§4.3).
- K4 waits for stage 1's call counts and for G3 (it was in stage 3 in the 2026-11-10 plan; the move is decision D2).

### 6.4 Stage 5a: the audit (the multi-core trio)

- The **parallelizer**, the **code-inspector** and the **bug-hunter** take the candidate list of §5.1 (C1–C4 first). They give
  every caller and every thread for each target, shutdown paths included, and find today's races; the races become hotfixes on
  every line.
- Two fixes that also help serial code: `getMaterialParams()` returns `const LLMaterialPtr&`; tangents are generated in a
  main-thread pre-pass (or under a once-flag per face).
- A dev-build compare mode (`#ifdef`, off release): it runs a phase serially and in parallel and compares cull lists, draw maps and
  filled buffer bytes.

### 6.5 Stage 5c: parallel culling

- **`ViewContext`-lite** replaces the globals inside a cull: camera id, the shadow, reflection, occlusion and local-shadow flags,
  the spot light, and the result reference. It is the same idea as `doc/vulkan/scene-ui-auxiliary.md` S03 and carries into the
  Vulkan design.
- **Main-thread pre-passes:** resolve the camera's pending occlusion queries (GL); `rebound()` every partition root; region planes
  per job instead of on the shared camera.
- **Jobs:** octree subtrees (depth 2–3) of each region × partition; each job has its own camera copy, result fragment and visibility
  records. Fragments merge in serial depth-first order, so the lists equal today's; counters are summed; visibility and LOD-pin
  records are applied serially.
- **Attachment bridges** read a snapshot of avatar visibility taken before the cull, and `LLCachedControl`s are read before the
  batch.
- **Order of targets:** the four cascades (C1), then the world view (C2), then the projector tiles (C3).
- **Shared with the point-light plan:** both need `renderShadowImpl` split into its cull and its submit
  (`pipeline.cpp:11661-11675` against `:11677-11760`; `point-light-shadows-plan.md:215`). The split is done once, by whichever
  lands first, and the other builds on it.

### 6.6 Stage 5b: plan / fill / flush geometry, after G5

- Unchanged from #448 in principle: plan on the main thread (classify faces, allocate, register mapped), fill in parallel per
  spatial group into the CPU shadow (`mMappedData`), flush on the main thread (`glBufferSubData`).
- **Order (owner, 2026-11-11):** the OpenGL arenas (G5) land first and 5b's plan step is designed on the arena allocator. G1–G7 have
  not started, so 5b moves to the end of Phase A.
- No persistent mapping in Phase A: the CPU shadow keeps today's GL behaviour (§10.2, D4).

### 6.7 Costs

| Stage | CPU | GPU | Memory |
|---|---|---|---|
| 1 | none in Release; Tracy build only | none | none |
| 2 | dispatch about 2 µs per batch, 0.02–0.05 ms a frame at 10–20 batches; helpers spin only in the preparation window | none | about 1 MB stack per helper |
| 3 | kernels 1.5–1.9× faster (positions, normals) | none | none |
| 5a | none (dev guards off release) | none | none |
| 5c | main-thread cull share divided by up to 4–6 [inferred]; more projector tiles in the cap | **none for the parallel culls themselves; more with D3(a)**: keeping the cap redraws more tiles a frame, and each 2048 tile adds a clear, a render and a 16 MB copy that `projector-pool-budget.md` (`:112`) notes no number includes | per-job result fragments, a few MB |
| 5b | settle faster [inferred] | none | per-worker scratch, a few MB |

Stages 1–3, 5a and 5b change no shader or pass, so the light and shadow budget (82178 Stochastic + 5 %) is untouched by them;
each stage's PR still reports its frame cost. **5c with D3(a) does change the GPU load**: parallel culls make main-thread room,
D3(a) spends it on more tile draws. The 5c gate therefore includes a **GPU gate**: the GPU ms of the Stochastic frame with the
projector pool on, in a scene with enough lamps to fill the cap, reported against the 82178 Stochastic baseline + 5 %
(`AGENTS.md`), together with the running total of the light and shadow work. If D3(a) pushes the total over the budget the cap
is lowered (D3(b)) rather than the budget raised.

---

## 7. Refreshed Phase A plan

### 7.1 Stage table

| Stage | Scope | Files (upstream commits in the last year on `upstream/master` 4384e9506a) | Upstream conflict risk | Gate | Owner's check | Calibrated effort | Risk |
|---|---|---|---|---|---|---|---|
| 0 | CPU check | — | — | — | — | **done** (estimated 2–3 d; landed in about 12 h on release, all lines the next day) | — |
| 1 | Tracy measuring build from devel tip, optional zones in the projector pool, the owner session (§7.2), baseline report under `doc/perf/` | `llprojectorshadows.cpp` (ours, 0) | none | run-to-run noise < 3 % on S1; ranked main-thread zones; the §3.2 table measured | **the session** (about 1 h) | **3–5 h** pipeline + one build | low |
| 2 | `LL::FrameJobs`, width rule, pool widths, dev guards, unit and stress tests | new `llcommon/llframejobs.*`; `llappviewer.cpp` `initThreads` (179) a few lines; `threadpool.cpp` (0); `llviewerdisplay.cpp` (32) the `beginFrameWork` / `endFrameWork` hooks; dev guards in `llrefcount.h` (1), `llvertexbuffer.cpp` (6), `gGL` | low–medium (small hunks in two busy files, `llappviewer.cpp` and `llviewerdisplay.cpp`) | no fps loss on S1; settle not slower; width sweep; spin-window power and driver-thread check; 4-core affinity run | sanity run | **1–1.5 d** | **low–medium**: thread start and shutdown order, hooks in `display()`, spin window |
| 3 | K1, K2, K5 + bit-for-bit tests + FMA-count CI gate | new `llmath` kernel file; `llface.cpp` (18) two loops; `llcamera.cpp` (0) | low–medium | bit-for-bit tests on Windows and Linux; kernel zones faster; FMA count unchanged | — | **0.5–1 d** | low |
| 4 | FMA memo | this document §9 | none | — | accepted with this study | **done** | — |
| 5a | trio audit of C1–C4, compare mode, `getMaterialParams` and tangent fixes (the dev guards landed with stage 2) | `lltextureentry.h` (0), `llface.cpp` (18), `llvolume.cpp` (18) | medium | compare mode clean on S1–S3; Release login test | — | **1–2 d** (0.8–1.2 M tokens; the main session asks first) | medium: races found become hotfixes |
| 5c | `ViewContext`-lite, cull split, parallel cascade, world and projector-tile culls | `pipeline.cpp` (32), `llspatialpartition.cpp` (7), `llvieweroctree.cpp` (2), `llvocache.cpp` (18), `lldrawable.cpp` (5), `llviewerdisplay.cpp` (32), `llprojectorshadows.cpp` (ours) | **high** (pipeline.cpp, llviewerdisplay.cpp) | cull lists identical (compare mode); main-thread ms lower on S1; no fps loss; byte-identical OpenGL captures of S1 and S2; **GPU gate against the 82178 Stochastic baseline + 5 % (§6.7)**; Release login test | in-world once, after release | **2–4 d** | high: the attachment/avatar-visibility order, occlusion |
| 5b | plan / fill / flush on the G5 arenas | `llvovolume.cpp` (29), `llface.cpp` (18), `llvertexbuffer.cpp` (6), `pipeline.cpp` (32) | **high** | buffer bytes identical; settle better on S1 teleports; no fps loss; Release login test | teleport check | **2–3 d after G5** | high: buffer lifetime, shutdown |

Each LL/Firestorm edit stays small and marked `<Vulkanstorm>`; new logic goes into new Vulkanstorm files, so canary's upstream
syncs meet hunks, not rewrites. Feature stages branch from `vkstorm-devel` (`codex/` prefix, RelWithDebInfo) and reach release,
canary and rust through integration PRs. Phase B (C5 state-sort split, C6 avatar animation, C7–C9 background work, K4) is
re-planned from Phase A's numbers.

### 7.2 Stage 1: the owner session

**Build** (one build; check first that no other viewer build runs): a worktree of the devel tip,
`autobuild configure -A 64 -c RelWithDebInfoOS -- --tracy --lto --avx2 "-DLL_TESTS:BOOL=FALSE"`, then
`autobuild build -A 64 -c RelWithDebInfoOS --no-configure` (the bench kit's line, `scripts/inworld/opensim-bench/README.md:22-26`).
The CMake cache must show `USE_TRACY=ON`, `USE_LTO=ON`, `USE_AVX2_OPTIMIZATION=ON`, `OPENSIM=ON`. Never shipped.

**Scenes** (as in #448): **S1**, the local OpenSim region with a dense OAR and the NPCs, the repeatable A/B scene used in this
session and in every later gate; **S2**, a dense static SL region, and **S3**, a crowded SL venue, for confirmation in the later
gates only.

**What the owner does** (about one hour, scratch APPDATA/LOCALAPPDATA as the kit describes, log in by hand to the local region):

1. Settings for every capture, written down once: vsync off, frame limiter off, one window size and field of view, default
   graphics preset, Shadow method Stochastic (the default).
2. Load the dense OAR in the bench region and spawn the NPCs (`/8 spawn`: the kit spawns 30; the decision says about 40 and
   OpenSim's default cap is 40, so 30 is the kit's safe default).
3. **Still, crowded:** sit on the camera seat, wait 20 s, then `tracy-capture.exe -a 127.0.0.1 -o MC1-still.tracy -s 60 -f`.
4. **In motion, crowded:** sit on the dolly (or loop an `LLAgentPilot` recording), wait one leg, capture 60 s as `MC1-moving.tracy`.
5. **Noise check:** repeat step 4 once (`MC1-moving-2.tracy`).
6. **Teleports:** five within the region between two landmarks and five to a second region and back, with Tracy connected for one
   continuous capture (`MC1-teleports.tracy`, up to 300 s). Write the wall-clock time of each teleport.
7. **fps without the profiler:** close Tracy and read the viewer's fps for 60 s on the dolly (Statistics bar), once.
8. Send the `.tracy` files, the viewer log and the times.

The pipeline then exports with `tracy-csvexport` and reports: main-thread ms per frame (`df MainLoop`) and its split into the
zones of §3.2; average fps; 1 % low (p99 frame time) and hitches (frames over 2× the median); settle time after each teleport
(log timestamps to `LLSceneMonitor` settle). Optionally the same build carries the particle stage-0 probe so that one session serves
both studies (D1).

### 7.3 Effort calibration against this repo's history [measured from git]

Ratios count an estimated day as 8 working hours. "Actual" differs per row, as the column says.

| Work | Estimated | Actual, and what it covers | Ratio |
|---|---|---|---|
| Stage 0 CPU check (#448 plan) | 2–3 d | wall clock from the first commit `0cfa65324a` (2026-11-11 01:48) to the follow-up authored 14:01 on release, about 12 h; all four lines by 2026-11-12 08:38. Commit times only: excludes survey and planning, includes review rounds | 1.5–2× |
| G0 OpenGL instrumentation | none recorded | `efc2ff3cc1` 2026-11-11 10:17 → `5b9c457b36` 13:18, about 3 h, first to last commit; excludes survey and planning | not computed |
| Projector pool S0–P3 | 14.5–19 d | about 19 h: **the owner's total figure**, planning and sessions included | 6–8× |
| Projector pool P3 alone | 6–8 d | about 2 h (the owner's figure) | 24–32× |
| Stochastic transparency port | — | calendar span: bench 2026-11-04 → shipping method 2026-11-05 → 8 samples + temporal 11-06, about 2 d | — |

(An earlier draft quoted "1.5–2 weeks" and 20–27× for G0; the repo has no source for that estimate, so it is dropped.)

The closest analogue to Phase A is the projector pool: several staged main-thread renderer changes, each with review rounds,
Release login tests and in-world sessions. Stage 0 is the only datapoint with CI and multi-line plumbing like Phase A's, and it ran
only 1.5–2×.

- **Optimistic case (the pool's ratio).** Phase A's #448 estimate of 13–17 weeks (65–85 working days) at 6–8× gives 8–14 working
  days. The stage figures of §7.1 add up to **6.9–12.1 days** (stage 1 0.4–0.6, stage 2 1–1.5, stage 3 0.5–1, 5a 1–2, 5c 2–4,
  5b 2–3). Calibrated: **8–13 working days of pipeline time** plus four owner sessions (stage 1, the stage-2 sanity run, the 5b
  teleport check, the 5c in-world check after release). One viewer build at a time and the owner's sessions set the calendar:
  about two to three weeks.
- **Pessimistic case (Stage 0's ratio).** 65–85 working days at 1.5–2× gives **32–57 working days**. Stages 1–4 are mostly
  tooling and kernels with firm gates and would keep their figures (about 2–3 d), so the difference sits in **5a, 5c and 5b**
  (about 30–54 d together): the audit's races, the compare-mode work, the in-world check, the canary conflicts in `pipeline.cpp`
  and `llviewerdisplay.cpp`, and the Release login tests. Per stage, 4–30 times faster than #448's per-stage weeks is what the
  optimistic figures imply (stage 3: 0.5–1 d against 2–3 weeks is 10–30×, with Windows and Linux tests and a disassembly CI gate),
  which is why the range is wide. The owner should read **8–13 d as the case where 5a finds few races, and 32–57 d as the case
  where it finds many**; the stage-5a report is the point to re-estimate 5c and 5b.

Threading bugs that show only in-world are the main way this could run over; that is why 5a comes before 5c and 5b.

---

## 8. Mapping to the Rust redo and the Vulkan renderer

- **Rust redo from the modernised C++:** `FrameJobs` maps to `rayon::scope` / `par_iter` over the same fixed helper set (rayon's
  pool, sized by the same width rule); the job-body rule becomes the borrow checker's (`&` shared, `&mut` per slot, no `Rc`
  clones in jobs). The bit-exact kernels become `std::arch` AVX2 functions with the same per-lane order, tested against the same
  reference data; FMA stays off (`-C target-feature` without `fma` contraction; Rust does not contract by default).
- **Vulkan:** `ViewContext` and immutable per-view cull results become per-view draw lists; one secondary command buffer (or
  wgpu render bundle) is recorded per job; the plan / fill / flush split maps to staging arenas and the G5 arena pages; the
  projector and point-light tiles become independent views culled and recorded in parallel. The coupling it removes is
  the global current-view state of §3.4 (`sCurCameraID`, `sShadowRender`, `sCull`).

---

## 9. FMA memo (stage 4 deliverable)

- **Precision.** A fused multiply-add rounds once instead of twice; each operation is at least as accurate. Precision as such is
  not compromised.
- **Determinism is.** About 44 % of a three-term transform's output floats change (§4.2); near cancellation they move by up to
  10⁵–10⁶ ulp (#448's run). That would break bit-for-bit kernel tests, Windows/Linux agreement unless both fuse in exactly the same
  places, the equivalence of the later Rust redo, and the byte-identical OpenGL captures the 5b and 5c gates use.
- **The gain is small where the data is large:** 12–24 % beyond bit-exact AVX2 at 1 MB and above, about 30 % in L1-sized data
  (§4.2); #448 measured at most 13 %. Vertex fills run over whole buffers, so the large-data figure is the relevant one [inferred].
- **Verdict: not adopted.** No `/fp:contract`; `-ffp-contract=off` stays; no FMA intrinsics in shared kernels; the FMA-count gate
  (§6.3) keeps it that way. Revisit only for a kernel never compared bit for bit, with a measured gain of at least 10 % and the
  owner's approval.

---

## 10. Decisions

### 10.1 Taken (owner, 2026-11-10 and 2026-11-11; not re-opened)

1. Scope: all of it, including the C++ OpenGL frame loop now; Phase A (0, 1, 2, 3, 4 memo, 5a, 5b, 5c); Phase B re-planned from
   Phase A's numbers.
2. Bit-exact by default; FMA memo only.
3. AVX2 (x86-64-v3) only, no runtime dispatch; stage 0 the full CPU check (done).
4. Success metrics: main-thread ms per frame in a crowded scene; load and streaming settle time; hitches and 1 % lows; average
   fps; teleports within and between regions.
5. Our own `LL::FrameJobs`, no external scheduler.
6. Bench scene: a local OpenSim region, dense OAR and about 40 NPCs.
7. The 9950X3D stays 8C/8T on the V-cache CCD; everything is profiled there.
8. Workers: no GL calls, no `LLPointer` copies; the main thread deletes nothing while a job runs.
9. SIMD for code that will live in Rust goes there later; the Rust port is redone from the modernised C++.
10. OpenGL arenas (G5) before 5b (2026-11-11).

### 10.2 Open (recommendation first)

- **D1. One measuring session for two studies.** (a) **Recommended:** stage 1's build also carries the particle stage-0 probe
  (`codex/particles-s0-probe`, rebased on the devel tip in the measuring worktree, never merged), so one owner session gives both
  baselines; (b) two separate builds and sessions.
- **D2. Order in Phase A, since G5 has not started.** Two earlier decisions bear on it and are put here so the question is
  real. (i) On 2026-11-10 stage 3 was K1, K2, K4 and K5; this study moves **K4 behind stage 1 and G3** (the skin-palette UBO
  cuts the uploads first and stage 1 counts the calls), which is a change to that decision. (ii) On 2026-11-11 the owner said
  **"modernise OpenGL first, Phase A deferred"**, then arenas (G5) before 5b. (a) **Recommended:** keep the owner's order for
  the G-series, but run the cheap Phase A stages that need no OpenGL change now: 0 ✓ → 1 → 2 → 3 (K1, K2, K5) → 5a → 5c →
  (G-series up to G5) → 5b, with K4 after G3; (b) honour the deferral fully: the G-series first, the whole of Phase A after
  G5; (c) the K4 placement of 2026-11-10 (stage 3) unchanged, without waiting for G3. Choose the order and the K4 placement.
- **D3. Projector pool cap after parallel culling.** (a) **Recommended:** keep `kFrameCapMs` = 0.35 ms of main-thread wall time,
  so more tiles fit each frame (faster shadow updates at the same **CPU** cost, **more GPU work**: each 2048 tile adds a clear,
  a render and a 16 MB copy, so 5c carries a GPU gate against the 82178 baseline + 5 %, §6.7); (b) lower the cap and keep today's
  tile rate.
- **D4. Persistent mapped buffers (GL 4.4) for 5b.** (a) **Recommended:** not in Phase A; the CPU shadow and `glBufferSubData`
  keep OpenGL behaviour identical; consider with G5; (b) design 5b on persistent mapping now (needs the GL 4.6 floor enforced and
  changes the GL calls).
- **D5. Who does the `renderShadowImpl` cull/submit split.** (a) **Recommended:** whichever of point-light Q1 and multicore 5c
  lands first, and the other builds on it; (b) 5c owns it.

---

## 11. Risks, and what was not verified

**Not verified:**

- No per-part profile of today's frame: every share in §3.2 is an estimate until stage 1.
- The in-viewer gain of K5: the octree is walked node by node; the 10× is a kernel number.
- The scaling run's job shapes (§4.5) are assumptions, not captures, and the table does not choose the width; the default of 5 is
  a reservation for the GL driver, decode and window threads and is decided by the stage-2 width sweep.
- The cull share of a projector tile's 0.07 ms (D3's gain), and the GPU cost of the extra tiles (§6.7).
- The in-viewer behaviour of helpers spinning through the preparation window (§6.2): power, X3D boost, driver-thread contention.
- The K2 and K1 SSE reference moved between two bench builds (§4.2); the speed-up carried is a floor of 1.5×.
- K5 covers only the AABB half of the real cull test (§4.3).
- That hoisting the cascade culls is bit-exact against today's interleaved order (5c compare mode).
- Stage 0's SDE and installer gate results were not re-run here; the FMA count was not re-run on today's binary.
- Dual-CCD + SMT behaviour (the stock setup of most X3D users) cannot be measured on the dev machine as configured; the width
  rule's dual-CCD branch is designed but unmeasured, to be confirmed on another machine or from user reports.
- Linux: no `objdump` FMA count yet; `std::lerp` agreement between Windows and Linux.

**Risks:**

- **Races the audit misses** (a job path reaching an `LLPointer` copy, a `LLCachedControl` first use or GL). The dev-build
  owner-thread and GL asserts and the compare mode guard against them.
- **Cleanup on the wrong thread.** Every cleanup and shutdown path is checked for job threads, with a Release login test at every
  stage (the #403 `sVBCache` clear on worker threads crashed at login).
- **Oversubscription** on 4- and 6-core machines with the GL driver thread: the width rule leaves three cores; the stage-2 gate
  includes a 4-core affinity run.
- **The spin-through-window design** (power, X3D boost, contention with the GL driver thread; §6.2) is a stage-2 risk; the stage-2
  gate measures it and may shorten the spin window or park helpers during long serial stretches.
- **Effort** may be far above the headline (§7.3): the pessimistic case is 32–57 days, carried by 5a, 5c and 5b.
- **Upstream conflicts on canary** for 5b and 5c (pipeline.cpp and llviewerdisplay.cpp change about 30 times a year upstream):
  small marked hunks, new logic in new files.

**What users will see:** pre-Haswell CPUs already get a clear message (stage 0); 3–7 more threads that park between frames;
`ThreadPoolSizes` values for the computed pools are ignored (it is a debug setting); rendering is unchanged by design (compare mode,
byte-identical captures).
