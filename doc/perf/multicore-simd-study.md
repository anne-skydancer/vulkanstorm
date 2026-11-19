<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Multi-core everywhere, SIMD where it pays (study, refreshed 2026-11-15; stage-1 measurements 2026-11-16)

Status: study and implementation plan. Nothing here changes the viewer. The branch `codex/multicore-simd-study-2026-11-15`
(written on `origin/vkstorm-devel` 670593308b, rebased onto 191c9985b9 on 2026-11-16) carries:

- this document, rewritten on today's devel. It supersedes the 2026-11-10 version (PR #448, `d07e99ec0b`, which is
  cherry-picked first on this branch so the history shows what changed);
- the offline bench `scripts/perf/bench_multicore_simd.cpp` (standalone, not part of the viewer build), extended with the
  normal rotation, a structure-of-arrays box test (with NaN and inf boxes), the `Sleep(1)` wake floor and a parallel scaling
  run; special values (±0, ±inf, NaN, denormals) are spread through the whole input buffers;
- its output: the tables in §4.1–4.7.

**Refreshed on 2026-11-16 with the stage-1 measurements.** The frame shares, the Amdahl bounds, the expected gains and the stage
order were estimates in the 2026-11-15 text; they are replaced by the Tracy captures of the stage-1 session
(`doc/perf/multicore-baseline-2026-11.md`, merged as #538, CSVs in `doc/perf/multicore-baseline-2026-11/`) and by the Rooftops
shadow split of the same day (`reports/rooftops-ultra-shadow-split-2026-11-16.md`, a local report, not committed). §4.8–4.10 hold
the new tables; §3.2, §5, §6.7, §7 and §11 are rewritten on them. The standalone bench results (§4.1–4.7) are hardware results
and are unchanged: they were taken on 2026-11-15 on the configuration of §4.1 and were **not re-run** (other studies were using
the machine).

Code references are file:line at `origin/vkstorm-devel` 670593308b (2026-11-15, after the projector pool P4 review round
#516); the line numbers were **not** re-checked at 191c9985b9. Where devel changed a cited area since, §2 says so. Each claim is tagged:

- **[code]**: read in the code at that commit (or at 191c9985b9 where §2 says so);
- **[measured]**: from the bench on the dev machine (§4.1–4.7, 2026-11-15) or from the stage-1 Tracy captures (**[stage 1]**,
  §4.8–4.10, 2026-11-16; main-thread CPU ms with the Tracy client attached, Second Life, Ryzen 9 9950X3D 8C/8T, 2560 x 1369,
  Ultra, vsync off) or from the Rooftops split (**[Rooftops split]**, same day, about 50 avatars, Tracy attached);
- **[inferred]**: reasoning or estimate, not checked.

Tracy-attached ms are upper bounds on what an unprofiled viewer spends: the profiler cost +5.4 ms a frame (about 1.38 times) at
Risk, +1.6 ms at Rooftops, where the wait in `Swap` absorbs most of it (§4.8). The shares are more reliable than the ms.

The owner decisions of 2026-11-10 stand and are not re-opened here (§10.1). New standing rules since then: GL 4.6 base with no
fallback paths, no OpenGL compute (it costs fps in motion in this codebase), no Preferences entries, identical OpenGL output with
equivalence tests for renderer-free logic, literature first, upstream-conflict risk per stage, effort calibrated against git
history.

---

## 1. Summary

**Today.** The whole frame loop still runs on the main thread: object and avatar updates, moves, geometry rebuilds, culling,
state sorting and every render pass (§3.2). Since #448 the frame has gained the projector shadow pool: each projector tile is a
full cull, state sort and submit of its own on the main thread, now with an avatar layer on top (P4), under a per-tile cap of
0.35 ms that the "progress pick" exceeds by design (§3.3). The point-light plan will add more such light views. The existing thread
pools are background queues whose idle workers wake by polling `Sleep(1)`, so they cannot carry sub-millisecond frame work (§4.4).

**Measured since (stage 1, 2026-11-16, §4.8–4.10).** The frame shares of the 2026-11-15 text were estimates; the Tracy captures
replace them, and they change the picture [stage 1]:

- Neither crowded place reaches the 60 fps standing floor: **Risk 56 fps, Rooftops 30 fps** without the profiler (Rooftops'
  1 % low is 13 fps with it).
- **The sun cascades are 38 % (Risk) and 42 % (Rooftops) of the main-thread frame**, not 10–20 %. Their cull + sort is 4.3 and
  4.6 ms (13–17 % of the frame). The rest, **5.1 and 10.4 ms, is serial GL submission and setup** that no thread can take.
- **The world cull is 1.5–1.8 %** (0.4–0.6 ms), not 4–8 %; all culls together are 6.4–7.5 %. The world state sort is 3–4 %, not
  8–15 %; three quarters of all sorting is in the cascades.
- **Parallel culls + sort + fill are 25 % (Risk) and 19 % (Rooftops) of the frame: at best 1.26 and 1.19 times at 6 wide**, and
  only if the frame is CPU-bound (Risk 56 → about 71 fps; Rooftops 30 → about 36, possibly no gain if the GPU is the limit).
  With avatar animation and the decode-priority pass added the bound is about 88 and 42 fps.
- **Fill is a loading cost, not a frame cost** (0.1–0.3 ms settled): 5b buys no steady-state fps.
- The projector pool's update, 1.8 ms (Risk) and 6.4 ms (Rooftops), was the largest unclaimed item; **the change-index hotfix
  removed most of it** (the pool's own time is about 0.3 ms now, §2, §4.10).

**Stage 0 is done on every line** (release, devel, canary, rust): the exe entry point CPU check, the delay-loaded `llwebrtc.dll`,
the installer's `MinVersion=10.0` and AVX2 check, and the Linux wrapper check, with CI object checks (§3.5).

**The plan keeps its shape and is re-ordered by the measurements** (§7):

1. Stage 1, **done**: Tracy measuring build and the Second Life session (§4.8–4.10). The projector pool's per-frame cost, which it
   found, is **largely solved by the change-index hotfix** (§2).
2. A **GPU-time capture before any FJ fps gate** (Rooftops waits in `Swap`; the exports hold no GPU zones).
3. `LL::FrameJobs`, our own fork-join job system (main thread takes part), and a width rule: `clamp(cores − 3, 2, 7)`, that is 5
   helpers on 8 cores, chosen to leave cores for the GL driver, decode and window threads (§6.2). **Built in stage 2 (#553), with
   no user yet; what was built differs in a few points, listed in §6.2 "Stage 2 as built".** The bench does **not** pick
   the width (§4.5); the stage-2 gate does. **Its first users: the cascade cull + sort, and the decode-priority /
   `calcPixelArea` pass**; then avatar animation (`updateCharacter`). The projector tiles wait.
4. Bit-exact AVX2 kernels: positions, normals and tangents, and an 8-boxes-per-register frustum test. K4 (palette SIMD) stays low:
   at most 0.27 ms. **Built in stage 3: K1 and K2 in `LLFace::getGeometryVolume`; K5 as a kernel with its `LLCamera` entry
   points, which 5c's cull split will call; see §6.3 "Stage 3 as built".**
5. FMA: the memo only (§9). Not adopted.
6. 5a the audit (the multi-core trio), 5c parallel culling (**cascades first**, then the world view; tiles last). 5b geometry fill
   is **not justified by steady-state fps** (§5, §7.1) and stays after the OpenGL arenas stage G5 (2026-11-11) as a loading-time item.
   The R items N4 and K7-a are dropped. The teleport settle (23 s into a light region, 105–113 s into the Rooftops crowd) is
   mesh-side work, not FJ.

**What decides the design** (measured, §4):

- A bit-exact AVX2 affine transform (K1) is **at least 1.5× and up to about 3×** faster than the viewer's SSE one, with **0 floats
  different** out of 4.2 M, edge cases spread through the whole buffer. The ratio depends on the SSE reference's timing, which
  moved between two builds of the bench (§4.2); the AVX2 times themselves are stable. FMA changes **about 44 %** of the floats
  (1,862,692 of 4,194,304) for **12–24 %** more at 1 MB and above (about 30 % in L1-sized data).
- Testing eight boxes per register against the frustum gives **identical decisions** (stage 3: 2.1 M box tests and 0.59 M
  boundary probes on eight real cameras, NaN and inf boxes included) and is **3.3–3.7×** faster as a kernel than the viewer's
  `LLCamera` test (4.3–4.6 → 1.25–1.29 ns per box), **1.9–2.0×** when the boxes must first be gathered from the `LLVector4a`
  pairs the octree keeps; the sphere half is 3.6–4.9× faster (§6.3). §4.3's 10× (14.4–15.0 → 1.44–1.48 ns) was against a slower
  bench copy of the test. The cull gain in the viewer will be smaller, since the octree is walked node by node. The measured
  cull share is small (all culls 1.8–2.3 ms a frame, world view 0.4–0.6 ms [stage 1]), which caps what K5 can return.
- An idle `LL::ThreadPool` worker wakes by `Sleep(1)`: **2.0 ms** per sleep with the viewer's 1 ms timer period (the Windows worst
  case; a worker polling at a random phase waits about 1 ms on average; non-Windows uses `usleep(1)`), **15.6 ms** without the
  timer period. A FrameJobs dispatch costs **1.1–2.5 µs** back to back.
- A geometry-fill-like job scales to **5.8× (median) with 5 helpers and 6.8× with 7**; a cull-like job to **6.1× with 5 and 7.2×
  with 6–7** (7 runs, §4.5). An earlier single run showed a dip at 7 helpers; seven runs did not reproduce it.
- An atomic ref+unref pair costs **7.5 ns** against 0.18–0.75 ns: job bodies must not copy `LLPointer`s.

**Effort, calibrated** (§7.3; the figures predate the stage-1 data and are **estimates**, not re-measured): Phase A is about
**8–13 working days of pipeline time plus three owner sessions still to come** (stage 1 is done) in the optimistic
case (the projector pool's ratio), about two to three weeks of calendar time. The pessimistic case, with Stage 0's smaller
1.5–2× ratio, is **about 32–57 working days**, almost all of it in 5a, 5c and 5b. The 2026-11-10 figure of 13–17 weeks came from
human-day estimates; this project's history runs 1.5 to 32 times faster than such estimates, and the closest analogue, the
projector pool S0–P3, ran 6–8 times faster (§7.3).

---

## 2. What changed since #448 (2026-11-10)

| Area | Then | Now (devel 670593308b; 2026-11-16 changes in the last rows, devel 191c9985b9) |
|---|---|---|
| Stage 0 CPU check | planned | **done on all four lines** (§3.5) |
| Shadow baseline | 82080, PCSS on | Stochastic frame of 82178 (8a797c1d61) + 5 % on 2026-11-15; **since 2026-11-16 an fps floor** (`AGENTS.md`, #533): about 60 fps standing in a crowded room (about 60 avatars), never below 25 fps in motion. The +5 % is retired |
| Projector shadows | two spot slots, culled per frame | the projector pool: one tile per lamp in an atlas, cull + sort + submit per tile, avatar layer (P4), one 0.35 ms CPU cap for every cache together (`llprojectorshadowpool.h:1365-1379`) |
| Point lights | none | planned: six faces per lamp, one sphere cull per lamp and one submit per face (`doc/shadows/point-light-shadows-plan.md:213-231`, decision D7) |
| Stochastic engine | first port | one core, three pools (shadows, light, transparency); GPU side; its CPU cost is small [inferred] |
| OpenGL instrumentation | proposed | G0 done: per-pass CPU and GPU ms and GL counters behind `VKS_GL_STATS` (`llglsubmitstats.h`, `doc/gl/instrumentation.md`) |
| OpenGL modernisation | none | study accepted 2026-11-11: G1–G7, with **arenas (G5) before multicore 5b**; G1+ not started |
| Bench kit | none | the local OpenSim kit with NPC spawner, camera seat and dolly, Tracy procedure (`scripts/inworld/opensim-bench/README.md`) |
| Rust | crates on vkstorm-rust | the Rust port will be redone from the modernised C++; no crate is the target for SIMD today |
| GL floor | 3.3 | 4.6 base for new work, no fallback paths. **Since #537 (`eadd674999`, 2026-11-16) the viewer refuses to start below GL 4.6 and the GL3 path is removed** (`46dde07414`: `LLGLFloor::enforce`; the Intel GL 3.3 downgrade is gone). Persistent mapped buffers (GL 4.4 `glBufferStorage`) are therefore guaranteed; the vertex buffer code does not use them yet [code: no `GL_MAP_PERSISTENT_BIT` in `indra/llrender`] |
| Frame shares and Amdahl bounds | estimates | **measured** by the stage-1 captures, #538 (`fcfc7ac614`): §4.8–4.10 |
| Projector pool update cost | at most 0.35 ms by the cap | stage 1 found `projector update` at 1.8 ms (Risk) and 6.4 ms (Rooftops), mostly self time in the change index rebuilding an 8 m grid every frame with dirty boxes. **Hotfixes `13b7d28981` and `e74e5c85be` (2026-11-16) scan the tiles instead and skip boxes outside every tile**; hits and order are identical (differential test against the old grid). In the Rooftops split taken on a build with the hotfix the pool's own time is **about 0.3 ms** and the update is 1.25–1.5 ms whole, almost all of it the tile renders [Rooftops split; commit messages read in the code] |
| Sun cascades under a dark sky | always drawn | **#540 (`191c9985b9`, from `6d409c8e07`) skips the cascade pass when the light reaching the ground is negligible** (decision light below 5e-6, rendered again above 5.5e-6, full strength from 1e-5; Stochastic only; the projector, point-light and avatar layers are untouched). Where it triggers it saves up to the whole cascade cost: **8.9 ms net at the Rooftops still view** (§4.10). It also removes the parallel part that FJ's first user would take, at night [code; commit message] |
| Projector tile sizing | one size class per lamp | #539 (`85236f92b6`): tiles sized by the camera position, wide lamps (above about 93 degrees) split into up to 4 x 4 cells, so a wide lamp costs several tile renders; #536 (`12a08821fe`): edge reconstruction in the tile lookup (GPU lookup, not CPU frame work). Both raise the tile count per lamp, so they bear on §5.1 C3 and the 5c GPU gate [code; commit messages] |

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

### 3.2 The main-thread frame [code; shares measured by stage 1]

Order of one frame (`llappviewer.cpp:1690-1840`): native events and input, the `mainloop` post and coroutine resume, `idle()`,
then `display()`, then the reflection-probe manager update and snapshot floaters.

**The shares below are measured** [stage 1, `doc/perf/multicore-baseline-2026-11.md` §4.2, blocks and shares tables; Risk and
Rooftops, Ultra, Second Life, Tracy attached, ms per `FTM_FRAME`, main thread only]. They replace the 2026-11-15 estimates (the
second column), which were for "a crowded scene with about 40 avatars"; the whole-frame numbers of that text (7.93 ms devel
against 8.61 ms for 82178 at the projector-pool measurement place, `doc/shadows/projector-pool-budget.md:191-195`; 0.07 ms CPU
per projector tile, `:171`) were of a lighter place and are not comparable. The frame is **24.71 ms at Risk (40.5 fps with
Tracy, 56 without) and 35.51 ms at Rooftops (28.2 fps with Tracy, 30.2 without)**; Rooftops' 1 % low is 13.3 fps with Tracy.
The captures predate the change-index hotfix (§2): the `projector update` row is the pre-hotfix cost.

| Part | Where | 2026-11-15 estimate | **Risk Ultra** ms (share) | **Rooftops Ultra** ms (share) |
|---|---|---|---|---|
| **idle** (`df idle`) | `LLAppViewer::idle` | 30–45 % | **6.42 (26.0 %)** | **6.46 (18.2 %)** |
| network (`idle network`) | `idleNetwork` `llappviewer.cpp:6757-6830` | 3–10 % | 0.15 (0.6 %) | 0.23 (0.6 %) |
| object updates | `gObjectList.update` `llappviewer.cpp:6270` → `llviewerobjectlist.cpp:1013-1142` | | 4.26 (17.2 %) | 4.61 (13.0 %) |
| — avatar `idleUpdate` | `llvoavatar.cpp:3127` | 10–25 % | 3.54 (14.3 %) | 4.03 (11.4 %) |
| — — animation `updateCharacter` | `llvoavatar.cpp:5723`; motions `:5820-5825`; skeleton `:5850` | | 2.40 (9.7 %) | 2.76 (7.8 %) |
| regions and moves | `updateRegions` `:6237`; `updateMove` `:6368` → `pipeline.cpp:2439` | 1–5 % each | 0.07 (0.3 %) | 0.08 (0.2 %) |
| particles | `updateParticles` `llappviewer.cpp:6371` (own plan: particle CPU survey) | 1–10 % | 0.75 (3.0 %) | 0.17 (0.5 %) |
| apparent angles, LOD | `llviewerobjectlist.cpp:932` | 1–3 % | 0.49 (2.0 %) | 0.52 (1.5 %) |
| **`Render`** (render preparation + GL submission) | `display()` | 35–50 % + 15–30 % | **16.44 (66.5 %)** | **27.71 (78.0 %)** |
| geometry create and rebuild | `llviewerdisplay.cpp:887-895` → `createObjects` `pipeline.cpp:2257`, `updateGeom` `:3175`, `rebuildPriorityGroups` `:3149` | 3–15 % | 1.07 (4.3 %) (fill proper `rebuildGeom` 0.33) | 0.49 (1.4 %) (fill 0.13) |
| world cull | `llviewerdisplay.cpp:923` → `pipeline.cpp:2798-2895` | 4–8 % | **0.38 (1.6 %)** | **0.63 (1.8 %)** |
| all culls (every `updateCull` pass) | | | 1.84 (7.5 %) | 2.26 (6.4 %) |
| **sun shadows** (`generateSunShadow`: 4 cascades × cull + sort + submit) | `llviewerdisplay.cpp:954` → `pipeline.cpp:12758`; cascades `:12575` (Stochastic) or `:12197` (Legacy); each `renderShadowImpl` `:11625-11760` | 10–20 % | **9.40 (38.0 %)** | **14.95 (42.1 %)** |
| — of which cascade cull + sort | | | 4.3 (about 17 %) | 4.6 (about 13 %) |
| — of which serial GL submission and setup | | | **5.14** | **10.38** |
| projector pool (`projector update`, includes the tile renders) | `mProjectorShadows.update` `pipeline.cpp:12980` → `llprojectorshadows.cpp:1099`, tile `:637-719`, avatar layer `:802-865` | at most 0.35 ms | 1.78 (7.2 %), pre-hotfix | 6.40 (18.0 %), pre-hotfix; unzoned self 5.36 |
| texture list (`updateImages`) | `llviewerdisplay.cpp:1004-1006` → `llviewertexturelist.cpp:1290-1344` | 3–10 % | 2.09 (8.5 %) | 2.15 (6.0 %) |
| — decode priority (`updateImageDecodePriority`) | | | 1.95 | 1.96 |
| world state sort | `llviewerdisplay.cpp:1032` → `pipeline.cpp:3496-3622`; `rebuildMesh` at `:3541`, `:3603` | 8–15 % | **0.96 (3.9 %)** | **1.13 (3.2 %)** |
| all state sorts (every pass) | | | 4.02 (16.3 %) | 4.43 (12.5 %) |
| reflection probe face | `llreflectionmapmanager.cpp:757`; `display_cube_face` `llviewerdisplay.cpp:1319` | 3–8 % | 1.67 (6.7 %) | 1.08 (3.0 %) |
| mirror (hero probe) | `llheroprobemanager.cpp:290` | 0–20 % | no zone fired | no zone fired |
| **GL submission, deferred** (`renderGeomDeferred` + `renderDeferredLighting` + `renderGeomPostDeferred`) | `llviewerdisplay.cpp:1172`, `:1198` | 15–30 % | 5.60 (22.7 %) | 7.89 (22.2 %); stays on the main thread |
| **UI** (`render_ui`) | `llviewerdisplay.cpp:1494`, `:1522` | 3–8 % | 0.74 (3.0 %) | 0.89 (2.5 %) |
| `Swap` (wait) | | not estimated | 0.10 (0.4 %) | 3.47 (9.8 %) |

Reading [stage 1]: Risk never waits for the GPU (`Swap` 0.1 ms) and is CPU-bound; Rooftops spends 10 % of its frame in `Swap`.
Rows overlap their parents; do not add a row to its parent. Where the estimates were wrong:

- **Sun shadows 10–20 % → 38–42 %**, and 5.1 and 10.4 ms of that is serial submission (as large as, or 1.5 times, the parallel part).
- **World cull 4–8 % → 1.6–1.8 %**; **world state sort 8–15 % → 3.2–3.9 %**; the sort is 16 % and 12.5 % over all passes, three
  quarters of it in the cascades.
- **Idle 30–45 % → 18–26 %**; **network 3–10 % → 0.6 %**; geometry rebuild 3–15 % → 1.4–4.3 % (fill 0.1–0.3 ms settled, a loading
  cost); particles 1–10 % → 0.5–3 %.
- **Inside the estimate:** avatars (11–14 %), texture list (6–8.5 %), probe (3–7 %), GL submission (22–23 %); UI just under.
- **The projector pool was not "at most 0.35 ms"**: 1.8 ms and 6.4 ms, of which 1.4 and 5.4 ms was self time; the hotfix removed
  most of that (§2).

Coroutines and `mainloop` work run only inside `idle()` (`gMainloopWork.runFor`, `llappviewer.cpp:6007`) and at the
`mainloop.post` / `llcoro::suspend` step (`:1727-1740`). Object kills, region removal and object shifts are handled in `idle()`.
A fork-join batch inside `display()` therefore cannot overlap any of them [code].

### 3.3 Cull and state-sort passes per frame [code]

| Pass | Count | Where |
|---|---|---|
| World view | 1 | `llviewerdisplay.cpp:923`, `:1032` |
| Sun cascades | 4 (2 during a cube snapshot) | `pipeline.cpp:12575` (`gCubeSnapshot ? 2 : 4`), cull and sort in `renderShadowImpl` `:11665-11667` |
| Projector tiles (Stochastic) | 0–n; **measured 0.99 tiles and 0.09 avatar layers a frame at Risk, 1.09 and 0.62 at Rooftops** [stage 1]; one tile nearly every frame (1 tile in 97 % of Rooftops frames [Rooftops split]); each is a static pass, then an avatar-layer pass per tile holding avatars | `llprojectorshadows.cpp:689`, `:858`; partition filter `pipeline.cpp:2847-2868` |
| Spot slots (Legacy) | 0–2 | `pipeline.cpp:13125` |
| Reflection probe | 1 face per frame: own cull + 2 cascades | `llreflectionmapmanager.cpp:757` |
| Mirror | up to 6 faces × (1 + 2) | `llheroprobemanager.cpp:290` |
| HUD | 1 | `llviewerdisplay.cpp:1494`, `:1522` |

So 9 passes plus the projector tiles without a mirror, up to 27 plus tiles with one. **Measured: `cull_passes` minus the two
projector counters is 9.0 in every capture, as expected; no mirror zone fired in any capture** [stage 1, baseline §4.6]. Each
shadow pass is cull → sort → submit, interleaved. The point-light plan will add one sphere cull per lamp with dirty faces (D7).

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
  volume code (`LL_RECORD_BLOCK_TIME` maps to Tracy zones, `llprofiler.h:108`). **Stage 1 (#529, #538) added zones** to the
  projector pool (`projector update`, `updateLightSet`, `relayout`, `pinCasters`, `renderTile`, `drawAvatars`, `renderLayer`),
  particles, `updateCharacter`, `cullAvatarsByPixelArea`, `LLAudioData::load` and `notifyMeshLoaded`, and per-frame counters
  (`llframecounters.h`: skin palette calls and joints, cull and sort passes, projector tiles and avatar layers; also a
  `FrameCounters:` log line a second when Tracy is compiled in). There are still **no GPU zones**. Profiling zones are allowed on release
  (`check_release_hooks.py:14-24` forbids none of them).
- **G0 instrumentation** (`VKS_GL_STATS`, devel only): per view and pass CPU and GPU ms and GL counters, Frame Profile JSON and
  LEAP (`doc/gl/instrumentation.md`).
- **The local OpenSim bench kit**: NPC spawner (30 NPCs; OpenSim's default cap is 40), camera seat and dolly, scenes, Tracy capture
  and report procedure (`scripts/inworld/opensim-bench/README.md`). Its build line is the one stage 1 uses.
- `LLSceneMonitor` (scene settle), `LLAgentPilot` (recorded camera paths), `EmulateCoreCount` (`llappviewer.cpp:2633`).

---

## 4. Measurements

§4.1–4.7 are the standalone bench results of **2026-11-15** (hardware results, taken on the configuration of §4.1,
not re-run on 2026-11-16 because other studies were using the machine). §4.8–4.10 are the **stage-1 in-viewer captures of
2026-11-16**.

### 4.1 Hardware and method [measured, bench, 2026-11-15]

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
(`indra/cmake/00-Common.cmake`). Stage 3 counted again in devel `df1a0ce92f`'s RelWithDebInfo CI build: the same single
`vfmadd213ss` in `std::lerp`. The Linux counts and the CI gate are in §6.3.

### 4.8 Stage-1 frame measurements, 2026-11-16 [stage 1]

Source: `doc/perf/multicore-baseline-2026-11.md` (#538) §4.1–4.2, §4.5, §4.6 and §4.9, with the CSVs `frames.csv`, `blocks.csv`,
`amdahl.csv` in `doc/perf/multicore-baseline-2026-11/`. Build `mc-stage1` (`RelWithDebInfoFS_open`, Tracy 0.11.1, LTO, AVX2, no
OpenSim) on devel before the change-index hotfix; Second Life on the 9950X3D (8C/8T), window 2560 x 1369, vsync off. Main-thread
CPU ms with the Tracy client attached. **These are in-viewer captures taken a day after the bench of §4.1–4.7, which was not
re-run.**

| Measure | Risk Ultra (still) | Rooftops Ultra (still, about 57 avatars) | Isle of Repose Ultra (home) |
|---|---|---|---|
| frames in 60 s | 2431 | 1690 | 5478 |
| fps with Tracy / without (owner, Statistics bar) | 40.5 / **56.0** | 28.2 / **30.2** | 91.2 / 93.5 |
| `FTM_FRAME` mean (median, p99) ms | 24.71 (24.0, 35.2) | 35.51 (33.5, 75.3) | 10.96 (10.6, 20.1) |
| 1 % low fps with Tracy | 28.4 | **13.3** | 49.9 |
| hitches (frame over 2 x median) | 4 | 38 | 47 |
| `Swap` ms | 0.10 | 3.47 | 4.70 |
| skinned palette calls a frame (K4) | 358 | 400 | 47 |

Against the owner's floor (about 60 fps standing in a crowded room, never below 25 in motion): **neither crowded place reaches
60 fps standing still.** The moving floor is not checked at Rooftops (only still captures exist there). Run-to-run noise is under
3 % for the frame mean and the blocks above 3 ms, not for small blocks or the 1 % low (baseline §4.7): a stage judged on a small
block or on the 1 % low needs a settled scene and the median of three captures.

**Ranked main-thread zones** (self time, ms a frame; baseline §4.5, Risk / Rooftops Ultra):

| Rank | Zone | Risk | Rooftops |
|---|---|---|---|
| 1 | `calcPixelArea - rigged` | 1.49 | 1.52 |
| 2 | `build render map` | 1.02 | 1.26 |
| 3 | `LLRenderPass::pushBatch` | 0.91 | 1.01 |
| 4 | `LLMotionController::updateMotionsByType` | 0.79 | 0.91 |
| 5 | `projector update` (pre-hotfix) | 1.39 | **5.36 (first at Rooftops)** |
| 6 | `ra - push batch` | 0.83 | 1.20 |
| 7 | `LLKeyframeMotion::onUpdate` | 0.66 | 0.70 |

At Rooftops `Swap` is second (3.44 ms) and `calcPixelArea - rigged` third. `calcPixelArea` totals 1.7–1.96 ms in the crowds and
about 1.3–1.6 ms of it is under `updateImageDecodePriority` (at Risk Ultra 1.61 of 1.95 ms; the idle path through
`LLFace::getTextureVirtualSize` holds about 0.35 ms): most of the texture list's flat 2.0–2.1 ms is the per-frame pixel area of the
avatars' rigged faces. Both callers share a 0.1 s per-face gate, so a cache must cover both.

**Culls, sorts and fill** (ms a frame; baseline §4.2). Pass counts: world 2, sun 6, probe 1 (the sun passes are inside
`generateSunShadow`, which runs twice a frame; the second call is 0.4 ms in all).

| ms a frame | Risk Ultra | Rooftops Ultra |
|---|---|---|
| `updateCull` world / sun / projector / probe | 0.38 / 1.30 / 0.04 / 0.11 | 0.63 / 1.46 / 0.11 / 0.06 |
| **`updateCull` total** | **1.84** | **2.26** |
| `stateSort` world / sun / projector / probe | 0.96 / 2.96 / 0.04 / 0.07 | 1.13 / 3.12 / 0.15 / 0.04 |
| **`stateSort` total (with `postSort`)** | **4.02** | **4.43** |
| sun shadows minus their cull and sort (serial GL submission and setup) | 5.14 | 10.38 |
| fill (`rebuildGeom`) | 0.33 | 0.13 |
| `LLFace::getGeometryVolume` (fill inner loop) | 0.30 | 0.06 |

**Fill is a loading cost**: 2.3 ms a frame in the scripted `MC1-still` (taken during the mesh settle), 0.1–0.3 ms settled.
`tgd - transform`, the texture-coordinate transform of the dropped K7-a, is 0.003–0.04 ms a frame. Palette work (K4):
`initSkinningMatrixPalette` 0.22–0.27 ms and `uploadMatrixPalette` 0.45–0.65 ms a frame in a crowd (3.3 % of the Risk frame, 2.6 %
of Rooftops); SIMD can touch only the 0.27.

**The four O2 candidates** (baseline §4.3): `cullAvatarsByPixelArea` 0.03–0.05 ms a frame (the estimate was 3 to 30 times higher;
**N4 dropped**); `LLAudioData::load` a once-per-sound hitch (N8 not justified); `LLHUDNameTag::updateAll` 0.015–0.033 ms;
`notifyMeshLoaded` 0.001–0.002 ms settled with one 17.7 ms call during an arrival (N1 and N3 only as arrival-hitch fixes, N3's bound
0.6 ms a frame while meshes load). `idle network` is 0.15–0.23 ms (message handling 0.10–0.17), so C-msg is not justified by
averages; one 482 ms `idle network` call in the Isle of Repose capture is a hitch to find.

**Settle** (baseline §4.8, log at one-second resolution; no teleport was captured under Tracy): a teleport costs 3–4 s of loading
screen; the fps then settles after **23 s into Isle of Repose** (mesh fetch quiet after 22 s, 3,739 fetches) and **105 s into
Rooftops** (the fps fell to 10 and spent 21 s under 25; mesh quiet after 113 s, 38,123 fetches). A login into the Risk crowd
needed 226 s for the mesh fetch. The scripted teleport series failed in two sessions.

### 4.9 Amdahl: what parallel culls, fill and sort can remove [stage 1]

Baseline §4.10. The parallel part is every cull pass, every state sort pass and the fill; 6 wide (5 helpers + the main thread);
best case, no merge cost, no serial prefix, no GL work; the fps assumes the frame stays CPU-bound.

| Capture (Tracy on) | Frame ms | Culls | Sort | Fill | Parallel part (share) | Removed at 6 wide | fps now | fps, 6 wide | fps, unbounded |
|---|---|---|---|---|---|---|---|---|---|
| Risk Ultra | 24.71 | 1.84 | 4.02 | 0.33 | 6.19 (25 %) | 5.16 | 40.5 | 51.2 | 54.0 |
| Rooftops Ultra | 35.51 | 2.26 | 4.43 | 0.13 | 6.82 (19 %) | 5.68 | 28.2 | 33.5 | 34.9 |
| Isle of Repose Ultra | 10.96 | 0.65 | 0.95 | 0.10 | 1.70 (15 %) | 1.41 | 91.2 | 104.8 | 108.0 |

With the profiler's cost scaled out (a guess that the cost is uniform) and the owner's no-profiler fps as the start:

| Place | fps now | fps, 6 wide | fps, unbounded | Condition |
|---|---|---|---|---|
| Risk Ultra | 56.0 | **about 71** (1.26 times) | about 75 | CPU-bound (`Swap` 0.1 ms) |
| Rooftops Ultra | 30.2 | **about 36** (1.19 times) | about 37 | an upper bound: `Swap` is 10 % of the frame and the GPU time is unknown; if the GPU is the limit the gain is smaller, **possibly none** |
| Isle of Repose | 93.5 | about 107 | about 111 | GPU- or present-limited (`Swap` 43 %): probably no gain |

**Adding avatar animation (`updateCharacter`, 2.4 and 2.8 ms) and the texture list (`updateImages`, 2.1 ms, whose decode-priority
pass is data-parallel)** the parallel part grows to 10.7 ms at Risk (43 %) and 11.7 ms at Rooftops (33 %); at 6 wide, profiler cost
scaled out, Risk reaches **about 88 fps** and Rooftops **about 42 fps**, under the same conditions (the exactness of the animation
split is not studied).

These replace the 2026-11-15 per-candidate gains (a cull at "4–6 times" of a 10–20 % shadow share, 4–8 % for the world cull, 8–15 %
for the sort). **Everything above is a best-case bound, not a prediction;** the real gain also pays the merge, fragment and spin
costs of §6.2 and §6.5, which stage 2 measures. The Rooftops figures predate the change-index hotfix, which cuts the frame
itself; the 19 % share would rise with a shorter frame.

### 4.10 Rooftops Ultra: the shadow split, 2026-11-16 [Rooftops split]

Source: `reports/rooftops-ultra-shadow-split-2026-11-16.md` (a local report, not committed; six Tracy captures taken with
`.captures/mc1-sl-20261116-165840/`). Rooftops at 3577 m, Ultra, vsync and frame cap off, about 45–50 avatars updated a frame, on devel
of 2026-11-15 **with the change-index hotfix** and the stage-1 zones. Main-thread ms a frame with Tracy attached; GPU zones do not exist.

| ms a frame (steady part, 15–58 s) | Still, no shadows | Still, sun only | Still, sun + lamps |
|---|---|---|---|
| `FTM_FRAME` mean | 23.07 (43.3 fps) | 31.90 (31.4 fps) | 35.53 (28.1 fps) |

- **The cascades cost 8.9 ms net, 28 % of the frame** (23.07 → 31.90). The six sun passes' zone time is 9.8 ms: cull 1.5,
  `stateSort` 4.2 (**about 2.6 net**: about 1.5 ms of the cascades' sort would be paid by the world pass anyway) and draw
  submission, serial GL, about 3–4 ms. Cull + sort + draw is 99–100 % of the sun-pass time. Per cascade: 2.71, 2.04, 2.31 and 2.29
  ms; avatars and skinned meshes are almost only in cascades 0 and 1 (about 1 ms of the 9.8 ms, overlapping rows).
- **The bound with 6 workers is about 4–4.5 ms of the 8.9** (perfect scaling on cull + sort + palettes, the shared sort staying in
  the world pass): still sun 31.9 → about 27.5 ms, **about 36 fps** from 31.4. The serial GL part is about 2.9–3.2 ms. The
  `stateSort` tail is heavy (p95 10–29 ms against a mean of 4–7): some frames re-sort a lot.
- **Lamps cost about 1.3–1.5 ms a frame**, one tile (about 1.0 ms, median 0.6) rendered nearly every frame (1 tile in 97 % of
  frames, 0.6 avatar layers a frame): the 0.35 ms cap is exceeded by the "progress pick" and something keeps a tile stale all the
  time (the cause is not captured; `projector_stats` was null in that run). `projector pinCasters` is 0.4 ms inside every tile.
  The pool's own time outside the tiles is **about 0.3 ms** (5.4 ms before the hotfix). An earlier 8.1 ms lamp figure was a 15 s
  warm-up and sort noise.
- **No-shadow frame, 22.9 ms:** avatar `idleUpdate` 3.6 (`updateCharacter` 2.55); `Update Images` 2.1 (`calcPixelArea - rigged` 1.35
  self; about 1.6 ms of `calcPixelArea` under decode priority, 0.35 ms on the idle path); lighting + deferred + post-deferred
  submission 8.3; world `stateSort` 2.5; vertex buffer flush 0.98; probe 0.69; UI 0.76; **`Swap` 5.1**.
- **CPU-bound with a GPU or driver stall:** the added frame time equals the added main-thread zone time, and `Swap` does not grow
  with shadows (5.1 / 4.5 / 5.7 ms), but a frame with `Swap` over 10 ms is 13–19 % of frames (about one in seven). Whether the GPU is
  the next limit once the CPU drops cannot be answered: **no GPU zones**.
- **Only `RT-moving__none` turned the camera**; the moving sun and all captures were frozen at a lighter heading (sun passes 5.4
  against 9.9 ms), so no moving series is usable for the shadow cost.
- **#540 (§2) skips the cascades under a dark sky.** Where its gate triggers the saving is up to this whole 8.9 ms (28 %); whether
  the gate triggers in these captures was not checked.

---

## 5. Candidates

### 5.1 Parallel candidates

The **Expected gain** column is rewritten on the stage-1 measurements (§4.8–4.10): ms a frame at Risk / Rooftops Ultra, Tracy
attached, a best-case share of the frame and not a prediction (the Amdahl bound is §4.9). Marked **[stage 1]** where measured and
**[inferred]** where still an estimate.

| # | Candidate | Kind | Data dependencies and shared state | Hazards | Expected gain | Bit-exactness |
|---|---|---|---|---|---|---|
| C1 | Sun cascade culls (4) hoisted and run together | data-parallel, one job per cascade, subtrees inside | each cascade has its own camera id (`SUN_SHADOW0..3`) and result; `rebound()` and region planes shared | `static` results, `sCull`, `sCurCameraID` globals; bridge reads avatar visibility | **measured: the cascades' cull + sort is 4.3 / 4.6 ms (13–17 % of the frame), of 9.4 / 15.0 ms for the whole sun-shadow pass; 5.1 / 10.4 ms is serial GL submission** [stage 1]. At the Rooftops still view cull + sort is 5.7 ms gross (about 4.1 net of the sort the world pass pays anyway) and the 6-worker bound is **about 4–4.5 ms of the 8.9 ms net** [Rooftops split]. The first FJ user. At night #540 skips the cascades (§2) | cull lists must be identical in serial order; hoisting changes the order of `rebound()` against cascade renders: prove with compare mode |
| C2 | World cull: regions × partitions, octree subtrees | data-parallel | as C1, plus occlusion queries (GL) and `LLVOCachePartition` region planes | GL query reads must move to a main-thread pre-pass; `mNumVisibleNodes`, occluder lists | **measured: the world cull is 0.38 / 0.63 ms (1.6–1.8 %), the world state sort 0.96 / 1.13 ms (3–4 %)** [stage 1]: 1.3 / 1.8 ms together, second after the cascades | merge fragments in depth-first order |
| C3 | Projector tile culls (and the avatar-layer culls) | data-parallel across the frame's chosen tiles | all tiles share `CAMERA_SPOT_SHADOW0`, `static LLCullResult`, the LOD pin book (`mPins`, main thread) | visibility slot and pin writes must become per-job records applied serially; submit stays serial | **measured: the tiles' cull + sort is 0.08 / 0.26 ms a frame** [stage 1]: **the tiles can wait**. A tile costs about 1.0 ms whole, mostly its draw and `pinCasters` (0.4 ms) [Rooftops split]; more tiles in the same cap remains [inferred]; #539's cell splitting raises the tiles per wide lamp; the point-light faces get the same | identical tile contents (cull lists) |
| C4 | Geometry fill (`getGeometryVolume` per group) | data-parallel, one job per spatial group | buffers belong to one group; static `sSimpleFaces`; `sMappedBuffers` | `getMaterialParams` by value, lazy `genTangents`, GLTF selection path allocates buffers | **measured: fill is 0.33 / 0.13 ms settled (0.1–0.3 ms) and 2.3 ms only while a scene loads** [stage 1]: a loading cost, no steady-state fps. 4.8× on the fill share [measured shape, §4.5, bench]. After GL 4.6 (#537) persistent mapped buffers are guaranteed, so a worker could fill vertex data (§6.6) | same bytes in the buffers (compare mode) |
| C5 | State sort, pure part | data-parallel then serial apply | `markRebuild`, pool enqueue, LOD timestamps, `markVisible` | many globals; render-type masks | **measured: the sort over every pass is 4.0 / 4.4 ms (12.5–16 %), three quarters of it in the cascades**; the world pass is 3–4 %, not 8–15 % [stage 1]. Part of C1 | Phase B |
| C6 | Avatar animation (`updateMotions` + skeleton) | data-parallel per avatar | keyframe cache (`LLKeyframeMotion` shared data), attachments call `gPipeline.markMoved` | motion callbacks, sound triggers, `LLPointer` in motions | **measured: `updateCharacter` 2.4 / 2.8 ms (8–10 %), the whole avatar `idleUpdate` 3.5 / 4.0 ms (11–14 %)** [stage 1]: a bigger block than all the culls; **next after the cascades**; the exactness of the split is not studied | Phase B |
| C7 | Prim volume generation (`LLVolume::generate`, `llvolume.cpp:2051`; faces `:5146`) | background per volume | `LLVolumeMgr` has a mutex (33 uses in `llvolumemgr.cpp`); faces written once | LOD switches wait for the result | settle and teleports [inferred] | same code on another thread with the same MXCSR: identical |
| C8 | Texture priority pass (`llviewertexturelist.cpp:1290-1344`) | data-parallel compute + serial apply | `LLPointer` entries, fetch state | refcounts, `updateFetch` side effects | **measured: `updateImages` is a flat 2.1 ms (6–8.5 %) in every crowd, `updateImageDecodePriority` 1.95 ms of it, about 1.6 ms of that `calcPixelArea` (mostly rigged faces); the idle path adds 0.35 ms** [stage 1]. As big as all the culls, data-parallel and probably cacheable per frame; a per-frame cache must cover both callers. **Joins FJ's first users** (this study had it as low) | Phase B |
| C9 | VO cache read on region arrival (`llviewerregion.cpp:847`) | background | region's cache map | region deletion while reading | teleport hitch [inferred] | Phase B |

### 5.2 SIMD candidates beyond `LLVector4a`'s SSE

| # | Kernel | Where | Status | Gain | Note |
|---|---|---|---|---|---|
| K1 | position transform + texture-index select | `llface.cpp:2126-2132` | measured (bench, 2026-11-15); built in stage 3 (§6.3) | at least 1.5× (1.48–3.2× across two builds), bit-exact. It sits in the fill, which is 0.1–0.3 ms a frame settled [stage 1]: a loading-time gain | stage 3 |
| K2 | normal and tangent rotation | `llface.cpp:2161-2178`, `:2196-2216` | measured, reference anomaly; built in stage 3 (§6.3) | expect 1.5–1.9×, bit-exact | stage 3 |
| K5 | box against frustum, eight per register | `llcamera.cpp:207-290`, callers `llspatialpartition.cpp:1089-1155` | built in stage 3 (§6.3), no viewer caller yet | 3.3–3.7× as a kernel against the viewer's `LLCamera` test, 1.9–2.0× with the gather from `LLVector4a` pairs; the sphere half 3.6–4.9×; decisions identical (§4.3's 10× was against a slower bench copy); needs a node layout for the full gain | stage 3 kernel, used by 5c. The measured cull share is small (world cull 0.4–0.6 ms, all culls 1.8–2.3 ms a frame [stage 1]), so the in-viewer return is bounded by that |
| K4 | skin matrix palette (`matMulUnsafe` per joint) | `llskinningutil.cpp:128-182`, called lazily per draw (`llvoavatar.cpp:11331`) | **measured** [stage 1] | 277–400 calls and 9,900–14,100 joints a frame in a crowd (47 and 1,800 at home); `initSkinningMatrixPalette` **0.22–0.27 ms** a frame, `uploadMatrixPalette` 0.45–0.65 ms | **stays low: at most 0.27 ms (about 1 % of the frame)**; the upload half is G3's (skin-palette UBO) |
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
- **Stage 2 as built (#553, 2026-11-18).** `indra/llcommon/llframejobs.h/.cpp`, `llframejobsguard.h` and
  `tests/llframejobs_test.cpp`; no cull, sort or fill uses it yet, so the stage has no fps gain of its own. Where it differs
  from the text of this section:
  - **Lazy wake.** The helpers are woken at the start of the frame-work window only when the previous window dispatched a
    batch, and otherwise at the first batch: a frame without parallel work costs nothing.
  - **Bounded spin.** A helper that was just woken or just finished a batch spins for about 50 µs inside the window and then
    parks; the next dispatch arms it again. Spinning through the whole window kept the pool at 1.7–3.3 cores for one trivial
    batch a window (review of #553); bounded, the same pattern cost 0.01–0.06 core in a scratch bench (taken on a loaded machine). Batches more than about 50 µs apart pay
    the wake of a parked helper, about 9–34 µs median (§4.4). The spin time is the tuning point of the gate below.
  - **Floating-point state per batch.** The helpers do not call `LLVector4a::initClass()` (llcommon cannot include llmath)
    and do not keep a start-up copy: every dispatch carries the owner's MXCSR (without the sticky flags) and a helper writes
    its own only when it differs. A change of rounding or flush-to-zero on the main thread after start-up therefore cannot
    make helpers compute other bits.
  - **Width floor.** No helpers below 4 fast cores (every batch then runs serially on the main thread), otherwise
    `clamp(fastPhysicalCores − 3, 2, 7)`, which never exceeds cores − 1: 2 helpers on 4 cores, 3 on 6, 5 on 8. With the former
    floor of 2 helpers a batch window stalled for up to 98 ms on one CPU and 55 ms on two (review of #553). On hybrid
    processors (Windows) only the cores of the highest efficiency class count. Helpers are **not pinned** (future work, with
    the dual-CCD case). `EmulateCoreCount` counts logical processors and is applied as an upper bound to the core count.
  - **Guards as a CMake option.** `llassert` is off in RelWithDebInfo, so the dev guards are `-DVKS_FRAMEJOBS_GUARDS=ON`
    (off by default, like `VKS_GL_STATS`): `LL_FRAMEJOBS_ASSERT_NOT_IN_JOB()` in `LLRefCount::ref` / `unref`,
    `LLVertexBuffer::genBuffer` / `flushBuffers` and `LLRender::flush`. The reference-count guard is "not inside a job", not
    a per-object owner-thread check.
  - **Pool widths deferred.** The background pool widths and priorities below are **not** changed yet: with no FrameJobs
    user they would only slow texture loading (4 → 2 decode threads on 8 cores). They land with the first user.
  - **Debug settings:** `FrameJobsSerial` (every batch on the main thread, same partition) and `FrameJobsHelpers` (helper
    count for the width sweep; 0 = the rule).
  - **Exceptions:** the exception of the lowest throwing chunk is rethrown, and every chunk below it has run, as in a serial
    loop.
- **Not built on `LL::WorkQueue`:** the 2 ms wake floor (§4.4), `std::function` per item, and a joining main thread could run
  arbitrary `mainloop` work.
- **Job-body rule:** no `LLPointer` copies, no GL, no `markDead` / `killObject`, no `LLCachedControl` first use, no floating-point
  reduction across jobs (sums run in a serial apply step in serial order). The main thread deletes nothing while a batch runs.
- **Width rule** (computed at every start-up; the persisted `ThreadPoolSizes` values for these pools are overwritten in code):
  - fast domain = the cores sharing the main thread's L3 (one CCD on AMD parts, the V-cache CCD on X3D; on Intel hybrid parts the
    P-cores and E-cores share one L3, so the domain is the whole package there and the helpers should prefer P-cores), from
    `GetLogicalProcessorInformationEx` on Windows and `/sys/devices/system/cpu` on Linux;
  - **frame helpers = `clamp(fastPhysicalCores − 3, 2, 7)`**: 5 on 8 cores, 3 on 6, 2 on 4 (#448 had `− 2`, i.e. 6 on 8); **as
    built, none below 4 cores** ("Stage 2 as built" above). The
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
- **C8 as built (2026-11-19, `codex/mc-c8-pixel-area`): the first FrameJobs user**, the texture list's pixel-area pass
  (decision D-A of `reports/multicore-5a-2026-11-18/report.md`).
  - **Two steps.** `LLFace::calcPixelArea` is now `computePixelArea` (the values, written to an `LLFacePixelArea::Result`)
    followed by `applyPixelArea` (the writes to the face and the `ll_frand` draw). The arithmetic is in
    `indra/newview/llfacepixelarea.h` (llmath only). The serial path and the jobs run the same code, so a face computed on a
    helper gets the bits the main thread would give it.
  - **The pass** (`LLViewerTextureList::updateImagesFetchTextures`). `precomputePixelAreas` lists the faces the loop can
    reach, each once and in the loop's order (the loop's own rules: referenced, not boosted, at most 1,024 faces a texture,
    not measured in the last 10 frames), and computes them with `parallelFor` (64 faces a chunk) in read-only mode. The loop
    is unchanged otherwise: at a face's turn `calcFacePixelArea` stores the value computed ahead, so face state, texture
    state and the main thread's random numbers change in the serial order. The list may hold faces the loop then leaves out
    (its early exits, the time box); their values are dropped.
  - **Batches (review of #575).** The faces are listed and computed a batch at a time (entries until 1,024 faces are
    listed), each batch when the loop gets to its first entry. The loop tests its time box after every entry, so the work
    done ahead is inside the box: a pass the box ends has computed at most one batch for entries it does not reach. With no
    helpers or in serial mode (`FrameJobsSerial`) nothing is computed ahead.
  - **Back to the main thread.** In read-only mode a face returns `NEEDS_SERIAL` where the serial code writes: rigging info
    to build (`updateRiggingInfo` writes the shared volume face and skin), a joint whose world matrix is dirty
    (`getWorldMatrix` updates the chain), a texture entry without a volume face (the log line). The loop runs
    `calcPixelArea` for those, and for any face it finds without a value.
  - **H3.** The worn rank's avatar tests (`isVisible`, `isVisuallyMuted`, the avatar's area) run once an avatar a pass
    (`wornAvatarArea`), not once a face.
  - **Exactness.** For a fixed number of textures processed, the faces, the textures and the random stream equal the
    serial pass's bit for bit. What changes is how many textures a frame reaches before the time box ends the pass (it ended
    32 % of the frames at Rooftops): textures are re-ranked sooner.
  - **Debug settings:** `TexturePixelAreaJobs` (on; off is the pass as it was), `TexturePixelAreaCompare` (every value
    computed ahead is computed again on the main thread at the face's turn and compared bit for bit; so is the avatar
    snapshot) and `TexturePixelAreaStats` (probe R0: one `TexturePixelArea:` log line a second with the quota, the entries,
    the entries processed, the passes cut by the time box, the batches and faces computed ahead, the faces applied and
    measured serially, the compare mismatches, the time spent ahead (mean and largest) and the pass time; the same values as Tracy plots in a Tracy build). The probe and the
    compare mode are development only: `scripts/tests/check_release_hooks.py` rejects them on `vkstorm-release`.
    `TexturePixelAreaCompare` is not saved between sessions.
  - **Tests.** `INTEGRATION_TEST_llfacepixelarea` (`indra/newview/tests/llfacepixelarea_test.cpp`): the two steps against
    the former `calcPixelArea` transcribed, bit for bit over 12 frames; the pass on 0 to 7 helpers against the serial pass
    at a fixed count, in one batch and in batches of 1,024 and 700; two deliberate breaks; an opt-in bench (`LL_FACE_PIXEL_AREA_BENCH=1`). Source guards in
    `scripts/tests/test_texture_streaming_fixes.py` (`PixelAreaJobs`, `PixelAreaThrottle`, `WornFetchRank`).
  - **Offline bench** (dev machine, 8 cores, other work running; 2,600 model faces, two thirds rigged, 1 ms of main-thread
    work before each batch so the helpers have parked): batch median 284 µs on the main thread alone, 146 / 110 / 82 / 70 /
    62 / 57 µs with 1 to 6 helpers (4.6x at the rule's 5), 117 µs with 7 (every core taken). Beside four busy threads at
    normal priority the 5-helper batch takes 118 µs (median) and 3 helpers 83 µs; with those threads below normal priority,
    66 µs and 84 µs. The model faces cost about a fifth of real ones (no pointer chasing), so the ratios carry over and the
    absolute times do not.
  - **Still open (the stage-2 first-user gates, in world):** fps and main-thread ms at Rooftops and Risk with
    `TexturePixelAreaJobs` on and off, the width sweep with `FrameJobsHelpers`, the settle after a teleport, and D-D (the
    decode pool below normal priority, which the bench above supports but which is not built here).

### 6.3 Stage 3: bit-exact AVX2 kernels

- K1, K2 and K5 move into `llmath` as pure functions (new file, e.g. `llsimdkernels.h`); the SSE versions stay in the unit test as
  the reference; tests compare bit for bit, including NaN, ±inf, denormals and −0 (§4.2 inputs).
- Rules: identical per-lane operation order; multiply and add stay separate; no reduction across lanes; `_mm256_rsqrt_ps` only where
  SSE already used `_mm_rsqrt_ps`.
- **CI gate:** count `vfmadd`, `vfmsub` and `vfnm*` in `dumpbin /disasm` (Windows) and `objdump -d` (Linux) of the exe; the count
  must not rise above today's.
- The K5 bit-for-bit test covers the whole `AABBInFrustumNoFarClip` (box and sphere halves) with NaN and inf boxes and a real
  camera's planes, not only the AABB half benched here (§4.3).
- K4 stays low: stage 1 counted 277–400 palette calls a frame and at most 0.27 ms for the matrix math, and the uploads are G3's (it was in stage 3 in the 2026-11-10 plan; the move is decision D2).
- **Stage 3 as built (2026-11-18, `codex/multicore-stage3-avx2-kernels`).** `indra/llmath/llsimdkernels.h` (K1, K2, K5),
  `indra/llmath/tests/llsimdkernels_test.cpp` (`INTEGRATION_TEST_llsimdkernels`, 5 tests, about 2 s), `LLCamera::AABBInFrustum8` /
  `AABBInFrustumNoFarClip8`, the K1 and K2 call sites in `LLFace::getGeometryVolume` with a Tracy zone `getGeometryVolume -
  position` (normals and tangents already had one), `scripts/tests/check_fma_count.py` and its CI step, and the test in CI's
  regression-test step on Windows, Linux and linux-lts, where it passed 5 of 5 on each (PR #558 at `466ec6c425`). Each of the
  three zones holds more than its kernel: its stream's strider mapping (`mapVertexBuffer`), in the position zone the padding
  loop, and in the tangent zone the `genTangents` call (`createTangents`, which runs on a face's first build).
  `mapVertexBuffer` and `createTangents` have zones of their own, so a capture reads each kernel in its zone's self time; the
  stage-1 captures have no position zone, so K1's before figure needs a capture of the base with that one zone added. Where it
  differs from the text above:
  - **K5 has no viewer caller yet**, as §5.2 plans ("stage 3 kernel, used by 5c"). The octree is walked one node at a time
    (`LLViewerOctreeCull::traverse`, through each cull type's virtual `frustumCheck`); testing a node's children eight at a time
    changes the traversal, which is 5c's cull split. Stage 3 ships the kernel, the `LLCamera` entry points and the tests; 5c wires
    them in. The entry points belong to stage 3 because the kernel needs the camera's plane masks and plane count, which are
    private to `LLCamera`: they are the only way to it, for the tests now and for 5c. Two things are left to 5c:
    - **The region culls.** `llvocache.cpp`'s culls go through `AABBInRegionFrustum` / `AABBInRegionFrustumNoFarClip`, which
      test against `mRegionPlanes`, also private. 5c adds the two eight-box region entry points beside its first region caller,
      one line each over the shared implementation, as the one-box pair is; the tests already pass shifted plane sets as
      `planes`.
    - **The box layout.** The octree keeps each group's box as a pair of `LLVector4a` (`mBounds`, `mObjectBounds`). Unless
      5c's node layout keeps the children's boxes as structures of arrays, the gain to expect is the gathered figure,
      1.9–2.0×, not 3.3–3.7× (tables below).
  - **The sphere half moved to llmath unchanged.** `AABBSphereIntersectR2` for `LLVector4a` boxes is now
    `LLSimdKernels::aabbSphereIntersectR2`, which `llvieweroctree.cpp` calls, so the eight-box kernel is tested against the
    function the viewer runs, not a copy.
  - **The references are the code the kernels replace.** K1 and K2 against the former `llface.cpp` loops verbatim (the padding
    and the last transform included); K5 against `LLCamera::AABBInFrustum` / `AABBInFrustumNoFarClip` on real `LLCamera` frusta
    (perspective, axis-aligned, 5 and 160 degrees, a user clip plane, an ignored plane, a box-shaped cascade frustum, a degenerate
    one, and shifted plane sets) and against the whole `LLOctreeCull::frustumCheck` (the frustum test, then the sphere test).
  - **Inputs and floating-point states.** Random values and random bit patterns; ±0, ±inf, quiet and signalling NaNs with
    payloads, denormals, ±FLT_MAX, values whose squares overflow or underflow; in vertices, seven kinds of matrix, boxes and planes;
    counts 0–9, 15–17, 31–33, 255–257, 1,000, 1,001 and 4,099; both 16-byte phases of a 32-byte line; flush-to-zero and
    denormals-are-zero on and off in all four rounding modes (16 MXCSR states). Boundary probes set a plane distance or a squared
    radius exactly to the value computed for a box and to the floats next to it, so that any change in a kernel's arithmetic
    (operation order, fusion) flips a decision, which random boxes almost never do.
  - **Every deliberate break is caught.** 27 breaks of the kernels, each built and run against the unchanged test: the operation
    order and an FMA in each kernel, the blend masks, the two-vertex step and the scalar tail, the vertex repeated into the
    padding, the far-plane skip, the compare predicates and their NaN behaviour, the corner signs, the order of the sphere test's
    two branches, an axis mix-up and the result decoding. All 27 made the test fail. The review fix round broke what it
    changed, 7 times, each built in the worktree's CMake build and run: `AABBInFrustum8` skipping the far plane,
    `AABBInFrustumNoFarClip8` testing it, the skip choice swapped, the user clip plane dropped from the plane count, the region
    planes as the default, the kernel never skipping the far plane, and the octant bound one short. All 7 made the test fail.
  - **One IEEE caveat.** Where two NaNs meet in one multiply or add, x86 returns the first operand's NaN, and the compiler may swap
    the operands of a commutative operation in either version. So there "a NaN" is guaranteed, not which one; the tests require
    both NaN at those floats and identical bits everywhere else. Each of the three streams shows the swap in some build, and
    where depends on the compiler, the build and the place in the code: it is not a property of a kernel. The floats that
    carried the other NaN's payload, of the 18,336 (K1), 9,836 (normals) and 7,008 (tangents) where two NaNs meet:

    | Build of `INTEGRATION_TEST_llsimdkernels` | K1 positions | K2 normals | K2 tangents |
    |---|---|---|---|
    | CI Windows (MSVC 19.51, RelWithDebInfo, LTO), PR #558 at `466ec6c425` | 0 | 0 | 1,504 |
    | CI Linux (GCC 15.2) and linux-lts (GCC 14.2), RelWithDebInfo, LTO, the same head | 3,072 | 0 | 0 |
    | The review fix round's local build, the worktree's own CMake tree with CI's flags (MSVC 19.51.36260) | 0 | 0 | 1,504 |
    | The first round's local build: the test and `llcamera.cpp` alone with LTO, linked against another tree's `llmath` | 0 | 2,128 | 1,504 |

    The two local builds use the same compiler on the same machine and differ only in how they were built. Every other float
    of the 3.97 M (K1), 3.16 M (normals) and 3.95 M (tangents) compared was identical on all three CI platforms and in both
    local builds, and no box decision depends on a payload.
  - **Two registers per iteration.** With one, the normal loop ran 0.28–0.30 ns a vertex during development; with two it runs
    0.19–0.26 (table below).
  - **Compile-time selection, no dispatch.** `__AVX2__` (the `USE_AVX2_OPTIMIZATION` builds: CI and the releases) compiles the AVX2
    loops; any other build compiles today's SSE loops. The test does not compile when `USE_AVX2_OPTIMIZATION` is set without AVX2
    code generation, so CI always tests the AVX2 path.
  - **FMA gate.** `check_fma_count.py` counts FMA-family instructions in the viewer executable of the RelWithDebInfo test builds
    (where the regression tests run; Release builds run no tests and are not counted, but they compile with the same
    floating-point flags, `/fp:precise` and, with AVX2, `-ffp-contract=off` in every configuration, and the 2026-11-08 Release
    exe held the same single `std::lerp` FMA, §4.7). The count must equal the platform's baseline: above it something new
    fuses, below it a library or the compiler dropped some or the scan broke, and either way the change that moves it sets
    the new baseline with the reason, so the baseline holds no slack (995 of Linux's 998 are SoLoud's; a SoLoud update that
    dropped them must not leave room for new ones in the viewer's code). The baselines were counted in the CI builds of devel
    `df1a0ce92f` (Windows, Linux) and of PR #554 at `dae06b2b5b` (linux-lts): **Windows 1** (`std::lerp(float, float, float)`,
    which MSVC's library computes with `fma()`), **Linux 998** (995 in the prebuilt SoLoud package of the Ubuntu 26.04 build:
    SoLoud, miniaudio, dr_mp3, stb_vorbis and its FFT; 3 in GCC's expansion of vectorised unsigned-int-to-float conversions,
    `float(hi)·65536 + float(lo)`, whose product is exact, so fused and unfused give the same bits), **linux-lts 3** (the same 3;
    its SoLoud package holds none). PR #558's CI at `466ec6c425` counted exactly these: 1 of 8,937,074 instructions (Windows),
    998 of 11,547,446 (Linux), 3 of 11,366,455 (linux-lts); the fix round's local build, 1 of 8,935,418. The kernels add none:
    0 in `llface.obj`, `llvieweroctree.obj` and the test executable. On a Windows viewer executable the check takes 10–27 s on
    the dev machine; Check PR runs its self-test (the parsers, the verdict and the tool runner).

  Measured with `LL_SIMD_KERNELS_BENCH=1` (the test's opt-in bench) in the viewer's floating-point state, best of 7 trials of at
  least 20 ms each, on the configuration of §4.1 with background desktop load only (no build or viewer running). [measured,
  stage 3] The tables are CI's configuration, measured in the review fix round: the worktree's own CMake build with CI's flags
  (RelWithDebInfo `/O2 /Ob1`, `/arch:AVX2`, LTO), 12 runs of four builds that share the K1 and K2 code (`466ec6c425` and the
  fix round's), CPU 1–28 % around the runs. The first round's standalone builds (the test and `llcamera.cpp` alone, linked
  against another tree's libraries; three runs with LTO and two without, CPU 6–40 %, three of 150 vertex samples and one run's
  K5 lines left out as disturbed by that load) follow the tables.

  | Kernel, 384–65,536 vertices | Today's SSE loop, ns a vertex | AVX2 kernel, ns a vertex | Speed-up | One 1,500-vertex face |
  |---|---|---|---|---|
  | K1 positions | 0.64–0.70 | 0.24–0.30 | 2.15–2.67× | 0.96–1.00 → 0.38–0.39 µs |
  | K2 normals | 0.39–0.45 | 0.19–0.26 | 1.58–2.16× | 0.59–0.62 → 0.30–0.31 µs |
  | K2 tangents | 0.56–0.62 | 0.22–0.29 | 2.02–2.59× | 0.86–0.89 → 0.34–0.35 µs |
  | Each of the three at 24 vertices | 1.14–1.30 | 0.96–1.19 | 1.02–1.26× | — |

  | K5, 8,192 boxes near a real frustum (53 % out, 23 % cut, 24 % in) | One box at a time, ns a box | Eight per call | Speed-up |
  |---|---|---|---|
  | `AABBInFrustumNoFarClip` | 4.31–4.62 | 1.25–1.29 on structure-of-arrays data; 2.26–2.30 with the gather from `LLVector4a` pairs | 3.37–3.69×; 1.87–2.04× |
  | the sphere half (`AABBSphereIntersectR2`) | 1.65–1.70 | 0.35–0.36 | 4.70–4.88× |

  The AVX2 times are the same in every build; the SSE references are not. The first round's standalone builds gave K1
  0.64–0.70 ns a vertex with LTO and 0.74–0.78 without (2.25–3.03×), the normal loop 0.73–0.78 with LTO and 0.40–0.44 without
  (3.04–3.78× and 1.74–2.12×), tangents 0.57–0.61 (2.18–2.58×), the one-box frustum test 4.27–4.52 ns a box (3.33–3.62×,
  gathered 1.86–1.98×) and the one-box sphere test 1.27–1.29 ns (3.60–3.65×). So the normal loop's 0.73–0.78 ns belongs to
  that one build, not to LTO: CI's configuration has LTO and takes 0.39–0.45. As in §4.2, the ratio inside the viewer is what
  the Tracy zones show. The K5 reference is the viewer's `LLCamera` code (4.3–4.6 ns a box), not §4.3's bench copy (14.4 ns),
  hence 3.3–3.7× rather than 10×; the eight-box time is close to §4.3's (1.25–1.29 against 1.44–1.48 ns). In the fix round, a
  call from the eight-box entry points to their shared implementation, which `/Ob1` did not inline, made the eight-box test 4 %
  slower (1.30–1.31 ns a box); the implementation is inline since.

  **Per frame.** The three streams of a 1,500-vertex face take 1.02–1.06 µs instead of 2.41–2.50 µs in CI's configuration
  (56–59 % less; 59–66 % in the first round's builds). `LLFace::getGeometryVolume`, which also writes texture coordinates,
  colours and weights, is 0.30 / 0.06 ms a frame at Risk / Rooftops Ultra (§4.8) and 1.49 ms in the capture taken during a mesh
  settle (`MC1-still`, baseline §4.2), Tracy attached; the kernels' share of it is not measured. Upper bound, as if the function
  were only these loops: 0.17–0.20 / 0.03–0.04 ms a frame settled (Risk 56.0 → at most 56.6 fps; Rooftops 30.2 → 30.2) and
  0.8–1.0 ms a frame during a settle. **No steady-state fps gain is
  expected**; the gain is in loading and geometry rebuilds, and one Tracy capture with the new position zone gives the real figure.
  Against the fps floor the stage costs nothing: no memory, no thread, no GPU work and no change to what is drawn. K5 has no caller
  yet, so it changes no frame; for 5c, the culls are 1.8–2.3 ms a frame (§4.8) and the box tests' share of them is not measured.

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
- **Order of targets (set by the stage-1 data):** the four cascades first (C1: cull + sort 4.3 / 4.6 ms, 13–17 % of the frame), then
  the world view (C2: cull + sort 1.3 / 1.8 ms), then the projector tiles (C3: 0.08 / 0.26 ms; they can wait). Cascade 0's cull is
  folded into the world cull earlier in the frame (0.07 ms of its own) and its `stateSort` carries work the world pass pays when
  shadows are off, so about 1.5 ms of the cascades' 4.2 ms sort is not saved by a parallel cascade sort alone [Rooftops split].
  The sort writes the shared draw pools: it is a worker candidate only if per-pass state is separated. At night #540 skips
  the cascades, and with them this first user's gain (§2).
- **Not reachable by threads:** the shadow passes' GL submission (5.1 ms at Risk, 10.4 ms at Rooftops; about 3 ms in the Rooftops
  split) is serial on one GL context and as large as, or larger than, everything 5c can take. It belongs to the OpenGL programme
  (fewer or cheaper shadow draws, reuse of cascade results across frames).
- **Shared with the point-light plan:** both need `renderShadowImpl` split into its cull and its submit
  (`pipeline.cpp:11661-11675` against `:11677-11760`; `point-light-shadows-plan.md:215`). The split is done once, by whichever
  lands first, and the other builds on it.
- **5c core as built (2026-11-19, `codex/mc-5c-core`): the sun cascades' culls and render maps on `LL::FrameJobs`**, as
  designed in `reports/multicore-5a-2026-11-18/report.md` section 2. It covers the Stochastic and PCSS cascades
  (`generateSunCascadesStable`); the Legacy cascades, the world pass, the projector tiles and the point-light faces are
  unchanged. Lists, draws and image are meant to be identical; the compare mode below checks it in a running viewer.
  - **5c-0, serial.** `renderShadowImpl` is `shadowCullAndSort` followed by `shadowSubmit` (decision D5; each holds the
    pass's flags, `sShadowRender` and occlusion off, for its own duration). The occlusion block in the submission is gone
    (D12: it needed `sUseOcclusion > 1` inside a pass that holds it at 0). `generateSunCascadesStable` builds every
    cascade's camera in a first loop and renders in a second; what the draws and the temporal history read (the current
    and "last" matrices, `mShadowModelview` / `mShadowProjection`, `mSunShadowMatrix`, the camera id, the viewport) is set
    in the second loop, where it was. `LLVOCachePartition::cull` tests the camera before it rebounds its root. The
    per-node cull zones are in the OCTREE profiler category.
  - **5c-1, culls.** `LLPipeline::updateCullSunCascades` makes one item per (cascade, region, partition) in
    `updateCull`'s loop order and by its rule (`cullsPartition`, shared with `updateCull`). On the main thread, before
    the batch: `updateCull`'s own start for each cascade (`updateCullBegin`: the pass count, the water clip plane, the
    result cleared) and `rebound()` of every partition root. A job (`LLSpatialPartition::cullShadowJob`) traverses its
    partition with `LLOctreeCullShadowJob`: the traversal and frustum tests of `LLOctreeCullShadow` unchanged, and a
    `processGroup` that is `markNotCulled` for a shadow camera with occlusion off: it stamps the group for its cascade
    (`setVisibleFor`) and records it in its own fragment. No GL call, no `LLPointer` copy, no occlusion state read or
    written, no global made per-thread. The main thread then appends the fragments in item order with the cull result's
    push functions. `mAnyVisible` is a relaxed atomic (`LLRelaxedS32`), since several cascades' jobs store the same frame
    number into it. A probe's two cascades take the same code as a single chunk on the main thread.
  - **5c-2, render maps.** For cascades 1 to 3, `buildRenderMapJobs` replaces `postSort`'s loop: a pre-pass on the main
    thread applies the skip rule and runs the rebuild (GL) group by group in the loop's order, chunks of 128 kept groups
    append to a fragment each (`renderMapPushGroup`, the loop's own body), and the fragments are appended in chunk order
    (`LLCullResult::appendDrawInfos`). Cascade 0 and every other pass keep the loop.
  - **What moved in time.** The four culls now run before the first cascade's sort and submission instead of each before
    its own. That rests on the reading that no sort or submission of an earlier cascade writes what a later cascade's cull
    reads (octree bounds, element counts, the partitions' render types); the compare mode is the check.
  - **Shared code and test.** `indra/newview/llculljobs.h` (fragments, runners, merges; renderer-free) with
    `tests/llculljobs_test.cpp`: a model octree with single-child chains, fully-inside subtrees, empty groups and both
    kinds of partition, culled serially and as jobs at 0 to 7 helpers, in serial mode and as one chunk; the render map
    at several grains; deliberate breaks (fragments swapped or dropped, another cascade's camera id, a root left dirty).
  - **Debug settings:** `RenderSunCascadeJobs` (on; off, each cascade is culled by `updateCull` and its render map built
    by the loop inside its own render, as before). Development only, compiled with `VKS_FRAMEJOBS_COMPARE` (on by default
    on this line, rejected on `vkstorm-release` by `scripts/tests/check_release_hooks.py`): `RenderSunCascadeJobsCompare`
    (1: every frame each cascade is culled again by `updateCull` at its former place and its render map built again by
    the loop, and every list is compared element for element; 2: the same with a deliberate fault in cascade 1) and
    `RenderSunCascadeJobsStats` (one `SunCascadeJobs:` log line a second: the cascades' cull and render-map times on the
    main thread per sun pass, the jobs, groups and chunks, and the compare counts with session totals).
  - **Not proven by the compare mode:** the sort loops (untouched), and the order of rebuilds against appends inside the
    render-map build (the reference loop runs after the pre-pass's rebuilds).

### 6.6 Stage 5b: plan / fill / flush geometry, after G5

- Unchanged from #448 in principle: plan on the main thread (classify faces, allocate, register mapped), fill in parallel per
  spatial group into the CPU shadow (`mMappedData`), flush on the main thread (`glBufferSubData`).
- **Order (owner, 2026-11-11):** the OpenGL arenas (G5) land first and 5b's plan step is designed on the arena allocator. G1–G7 have
  not started, so 5b moves to the end of Phase A.
- **Not justified by steady-state fps:** fill is 0.33 / 0.13 ms a frame settled and 2.3 ms only while a scene loads
  [stage 1]; 5b is a loading-time item (settle after arrival, §4.8) and stays after G5.
- Since #537 (GL 4.6 floor, 2026-11-16) persistent mapped buffers are guaranteed, so a worker thread could fill vertex data
  through a persistent mapping instead of the CPU shadow; fill is only 0.1–0.3 ms settled, so this is a loading cost too. Phase A
  still keeps the CPU shadow and today's GL behaviour (§10.2, D4).

### 6.7 Costs

| Stage | CPU | GPU | Memory |
|---|---|---|---|
| 1 | none in Release; Tracy build only | none | none |
| 2 | dispatch about 2 µs per batch, 0.02–0.05 ms a frame at 10–20 batches; helpers spin only in the preparation window | none | about 1 MB stack per helper |
| 3 | kernels 1.5–1.9× faster (positions, normals) | none | none |
| 5a | none (dev guards off release) | none | none |
| 5c | the parallel part is 6.2 / 6.8 ms (25 % / 19 % of the frame), at best 5.2 / 5.7 ms removed at 6 wide (§4.9; Rooftops cascades alone about 4–4.5 ms of 8.9); more projector tiles in the cap [inferred] | **none for the parallel culls themselves; more with D3(a)**: keeping the cap redraws more tiles a frame, and each 2048 tile adds a clear, a render and a 16 MB copy that `projector-pool-budget.md` (`:112`) notes no number includes | per-job result fragments, a few MB |
| 5b | none in steady state (fill 0.1–0.3 ms settled [stage 1]); settle faster [inferred] | none | per-worker scratch, a few MB |

Stages 1–3, 5a and 5b change no shader or pass, so the light and shadow budget is untouched by them; each stage's PR still
reports its frame cost. **The budget is an fps floor since 2026-11-16** (`AGENTS.md`, #533: about 60 fps standing in a crowded room,
never below 25 in motion, at the default tier; the former 82178 Stochastic + 5 % is retired), and each PR reports its cost in ms
and in fps against it. **5c with D3(a) does change the GPU load**: parallel culls make main-thread room, D3(a) spends it on more
tile draws. The 5c gate therefore includes a **GPU gate**, and **a GPU-time capture comes before any FJ fps gate** (§7.1, stage
1b): the exports of stage 1 hold no GPU zones, and Rooftops waits in `Swap` (10 % of the frame, a stall in about one frame in
seven), so without GPU time a Rooftops fps gate may be unreachable by any CPU change. If D3(a) pushes the GPU cost below the
floor the cap is lowered (D3(b)) rather than the floor lowered.

---

## 7. Refreshed Phase A plan

### 7.1 Stage table

Re-ordered on 2026-11-16 by the stage-1 data (§4.8–4.10; the plan-order changes 1–9 of `doc/perf/multicore-baseline-2026-11.md`
§5). Effort figures are the 2026-11-15 estimates, **not re-measured**; the data changes the scope of 5c and 5b, not yet their
estimate. Order: 0 ✓, 1 ✓, 1a ✓ (hotfix), 1b, 2, 3, 4 ✓, 5a, 5c, 5b.

| Stage | Scope | Files (upstream commits in the last year on `upstream/master` 4384e9506a) | Upstream conflict risk | Gate | Owner's check | Calibrated effort | Risk |
|---|---|---|---|---|---|---|---|
| 0 | CPU check | — | — | — | — | **done** (estimated 2–3 d; landed in about 12 h on release, all lines the next day) | — |
| 1 | Tracy measuring build, zones and counters, the Second Life session, baseline report | `llprojectorshadows.cpp` (ours), `llframecounters.h`, zones in `llvoavatar.cpp` and others | none | **met**: ranked zones and the §3.2 table measured; noise under 3 % for the frame mean and blocks above 3 ms, not for small blocks or the 1 % low (§4.8) | the session, done | **done** (#529, #538) | — |
| 1a | projector pool per-frame cost (the stage-1 finding: 1.8 ms Risk, 6.4 ms Rooftops, 5.4 of it unzoned self time) | `llprojectorshadowpool.h` and the change index (ours) | none | the pool's own time about 0.3 ms [Rooftops split] | — | **largely solved** by hotfixes `13b7d28981` and `e74e5c85be`. What is left is a tile rendered nearly every frame (about 1.0 ms, 1 tile in 97 % of Rooftops frames, cause not captured: `projector_stats` was null) and `pinCasters` 0.4 ms inside every tile | — |
| 1b | **GPU-time capture** (GPU zones or a GL timer-query capture) at Rooftops and Isle of Repose | Tracy GPU context / `llglsubmitstats.h` (G0) | none | GPU ms of the shadow passes and the frame; settles whether Rooftops is CPU- or GPU-limited | — | small [inferred] | low. **Comes before FJ's fps gates** (§6.7) |
| 2 | `LL::FrameJobs`, width rule, pool widths, dev guards, unit and stress tests. **Built (#553), without the pool widths and with no user: see §6.2 "Stage 2 as built"; the gates wait for the first user** | new `llcommon/llframejobs.*`; `llappviewer.cpp` `initThreads` (179) a few lines; `threadpool.cpp` (0); `llviewerdisplay.cpp` (32) the `beginFrameWork` / `endFrameWork` hooks; dev guards in `llrefcount.h` (1), `llvertexbuffer.cpp` (6), `gGL` | low–medium (small hunks in two busy files, `llappviewer.cpp` and `llviewerdisplay.cpp`) | no fps loss on S1; settle not slower; width sweep; spin-window power and driver-thread check; 4-core affinity run. **FJ's first users: the cascade cull + sort and the decode-priority / `calcPixelArea` pass; then `updateCharacter`** | sanity run | **1–1.5 d** | **low–medium**: thread start and shutdown order, hooks in `display()`, spin window |
| 3 | K1, K2, K5 + bit-for-bit tests + FMA-count CI gate (K4 stays low: at most 0.27 ms). **Built: see §6.3 "Stage 3 as built"; K5 has no viewer caller until 5c** | new `llmath` kernel file and test (`llmath/CMakeLists.txt` (3): the header and the test); `llface.cpp` (18) three loops and a zone; `llcamera.cpp` / `llcamera.h` (0) the eight-box entry points; `llvieweroctree.cpp` (2) the sphere test moved to llmath; the viewer-build action and Check PR | low–medium | bit-for-bit tests on Windows and Linux (**met** in CI on Windows, Linux and linux-lts); kernel zones faster (**waits** for a Tracy capture); FMA count unchanged (**met**: exactly the baselines) | — | **0.5–1 d** | low |
| 4 | FMA memo | this document §9 | none | — | accepted with this study | **done** | — |
| 5a | trio audit of C1–C4 (**C1 and the decode-priority pass first**), compare mode, `getMaterialParams` and tangent fixes (the dev guards landed with stage 2) | `lltextureentry.h` (0), `llface.cpp` (18), `llvolume.cpp` (18) | medium | compare mode clean on S1–S3; Release login test | — | **1–2 d** (0.8–1.2 M tokens; the main session asks first) | medium: races found become hotfixes |
| 5c | `ViewContext`-lite, cull split, parallel **cascade cull + sort first, then the world view; projector tiles last** (they are 0.08–0.26 ms) | `pipeline.cpp` (32), `llspatialpartition.cpp` (7), `llvieweroctree.cpp` (2), `llvocache.cpp` (18), `lldrawable.cpp` (5), `llviewerdisplay.cpp` (32), `llprojectorshadows.cpp` (ours) | **high** (pipeline.cpp, llviewerdisplay.cpp) | cull lists identical (compare mode); main-thread ms lower on S1; no fps loss; byte-identical OpenGL captures of S1 and S2; **GPU gate against the fps floor (§6.7)**; Release login test. Expected: at best 1.19–1.26 times at 6 wide, only if CPU-bound (§4.9) | in-world once, after release | **2–4 d** | high: the attachment/avatar-visibility order, occlusion |
| 5b | plan / fill / flush on the G5 arenas, **a loading-time item**: fill is 0.1–0.3 ms settled, so no steady-state fps | `llvovolume.cpp` (29), `llface.cpp` (18), `llvertexbuffer.cpp` (6), `pipeline.cpp` (32) | **high** | buffer bytes identical; settle better on S1 teleports; no fps loss; Release login test | teleport check | **2–3 d after G5** | high: buffer lifetime, shutdown. Not justified by the frame data |

The data **drops** R's N4 (0.03–0.05 ms a frame against 0.1–1.5) and K7-a (0.003–0.04 ms), and N8 (async sound load) is not
justified; N1 and N3 stay only as arrival-hitch fixes (the 17.7 ms `notifyMeshLoaded` call; 0.6 ms a frame while meshes load) and
do not hold FJ back; R's steady-state gain is under 0.1 ms a frame. C-msg is not justified by averages. **The teleport settle (23 s
into Isle of Repose, 105–113 s into Rooftops, fps down to 10) is the biggest cost the owner sees and it is mesh-side work (N1, N3,
N2, C-mesh), not FJ**; it needs per-frame data (a teleport capture) before a stage is chosen. The noise gate is re-run on settled
scenes; gates on the 1 % low use the median of three captures.

Each LL/Firestorm edit stays small and marked `<Vulkanstorm>`; new logic goes into new Vulkanstorm files, so canary's upstream
syncs meet hunks, not rewrites. Feature stages branch from `vkstorm-devel` (`codex/` prefix, RelWithDebInfo) and reach release,
canary and rust through integration PRs. Phase B (C5 state-sort split, C6 avatar animation, C7–C9 background work, K4) is
re-planned from Phase A's numbers; the stage-1 data moves C6 (avatar animation) and the decode-priority pass of C8 up, to follow the
cascades.

### 7.2 Stage 1: the owner session (done, 2026-11-16)

Stage 1 ran on **Second Life** at crowded venues (Risk, Rooftops) and at home (Isle of Repose), scripted through the LEAP plugin
(`scripts/inworld/README.md`) and by hand, on a Tracy build without OpenSim (`RelWithDebInfoFS_open`, `--tracy --lto --avx2
--no-opensim`); it replaced the OpenSim S1 scene and the dense-OAR procedure that this section described on 2026-11-15. The
procedure, the captures and which are valid, the exports and the results are in `doc/perf/multicore-baseline-2026-11.md` (#538);
the numbers are in §4.8–4.9 above. The owner accepted D1 (one session for two studies; the build carried the particle stage-0
probe, never merged). A scripted teleport series failed in two sessions; three manual teleports were logged without a profiler.
The OpenSim S1 scene was not used by stage 1; which scenes the stage-2 and later gates use (Risk and Rooftops on settled scenes, or
the OpenSim kit) is open (§11).

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
  5b 2–3). Calibrated: **8–13 working days of pipeline time** plus three owner sessions still to come (the stage-2 sanity run, the 5b
  teleport check, the 5c in-world check after release; stage 1's session is done). These are the 2026-11-15 estimates; the
  stage-1 data narrows 5c's expected gain (§4.9) and 5b's justification (§7.1) but is not used to re-estimate the days. One viewer build at a time and the owner's sessions set the calendar:
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
  real. (i) On 2026-11-10 stage 3 was K1, K2, K4 and K5; this study moves **K4 behind G3** (the skin-palette UBO
  cuts the uploads first). Stage 1 counted the calls: K4 can return at most 0.27 ms a frame, so it stays low, which is a change to that decision. (ii) On 2026-11-11 the owner said
  **"modernise OpenGL first, Phase A deferred"**, then arenas (G5) before 5b. (a) **Recommended:** keep the owner's order for
  the G-series, but run the cheap Phase A stages that need no OpenGL change now: 0 ✓ → 1 ✓ → 1b (GPU-time capture) → 2 → 3 (K1, K2, K5) → 5a → 5c →
  (G-series up to G5) → 5b, with K4 after G3; 5b is a loading-time item (§7.1); (b) honour the deferral fully: the G-series first, the whole of Phase A after
  G5; (c) the K4 placement of 2026-11-10 (stage 3) unchanged, without waiting for G3. Choose the order and the K4 placement.
- **D3. Projector pool cap after parallel culling.** Stage 1 shows the tiles' cull + sort at 0.08 / 0.26 ms a frame, so the parallel gain for tiles is small and D3 matters less than
  when it was written; the tile render itself is about 1.0 ms and one tile is drawn nearly every frame (§4.10). (a) **Recommended:** keep `kFrameCapMs` = 0.35 ms of main-thread wall time,
  so more tiles fit each frame (faster shadow updates at the same **CPU** cost, **more GPU work**: each 2048 tile adds a clear,
  a render and a 16 MB copy, so 5c carries a GPU gate against the fps floor, §6.7; the 82178 + 5 % baseline is retired); (b) lower the cap and keep today's
  tile rate.
- **D4. Persistent mapped buffers (GL 4.4) for 5b.** The GL 4.6 floor is now enforced (#537), so the "needs the floor" condition is
  met and persistent mapping is guaranteed; fill is 0.1–0.3 ms settled, a loading cost only. (a) **Recommended:** not in Phase A; the CPU shadow and `glBufferSubData`
  keep OpenGL behaviour identical; consider with G5; (b) design 5b on persistent mapping now (changes the GL calls).
- **D5. Who does the `renderShadowImpl` cull/submit split.** (a) **Recommended:** whichever of point-light Q1 and multicore 5c
  lands first, and the other builds on it; (b) 5c owns it.

---

## 11. Risks, and what was not verified

**Not verified:**

- **The frame shares of §3.2 are now measured** (stage 1, Risk and Rooftops Ultra, Tracy attached, before the change-index hotfix),
  but only for two crowded Second Life places on the dev machine's 8C/8T configuration, still cameras; the moving floor at Rooftops
  is not checked and the Tracy-attached ms are upper bounds (+30 % at Risk).
- **GPU time is not measured** (no GPU zones): whether Rooftops is CPU- or GPU-limited is inferred from `Swap` (10 % of the
  frame, a stall in about one frame in seven). The Rooftops 6-wide bound (about 36 fps) may be unreachable.
- The Amdahl bounds (§4.9) are best cases with a uniformly scaled profiler cost, a guess; the animation split's exactness is not studied.
- The Rooftops split (§4.10) is a local report, not committed; its moving captures are invalid (the camera was frozen), and the
  cause of the tile that is stale every frame is not captured (`projector_stats` null).
- Whether #540's dark gate triggered in the captured Rooftops view, and the fps gain after the change-index hotfix without the
  profiler, were not measured. That the hotfix and #539 behave as their commit messages say was read there, not re-run.
- A real teleport capture does not exist (the scripted series failed; settle is read from the log at one-second resolution).
- The bench results of §4.1–4.7 were not re-run on 2026-11-16 (the machine was in use); the line numbers of §3 were not re-checked
  at 191c9985b9.
- The in-viewer gain of K5: the octree is walked node by node, and stage 3's 3.3–3.7× (1.9–2.0× with the gather from
  `LLVector4a` pairs, which today's octree layout needs) is a kernel number (§6.3); §4.3's 10× was against a slower bench copy.
- The scaling run's job shapes (§4.5) are assumptions, not captures, and the table does not choose the width; the default of 5 is
  a reservation for the GL driver, decode and window threads and is decided by the stage-2 width sweep.
- The GPU cost of the extra tiles (§6.7). The cull + sort of the tiles is measured now (0.08 / 0.26 ms a frame, §4.8).
- The in-viewer behaviour of helpers spinning through the preparation window (§6.2): power, X3D boost, driver-thread contention.
- The K2 and K1 SSE reference moved between two bench builds (§4.2); the speed-up carried is a floor of 1.5×. Stage 3 saw the same
  with the viewer's own loops (the normal loop took 0.73–0.78 ns a vertex in one standalone LTO build and 0.39–0.45 in CI's
  configuration, also LTO, and without LTO), while the AVX2 kernels' times did not move (§6.3); the ratio inside the viewer
  needs the Tracy zones.
- The §4.3 bench covers only the AABB half of the real cull test; stage 3's bit-for-bit test covers the whole test (§6.3), its
  speed only as a kernel.
- That hoisting the cascade culls is bit-exact against today's interleaved order (5c compare mode).
- Stage 0's SDE and installer gate results were not re-run here. The FMA count was re-run by stage 3 on the CI builds of devel
  `df1a0ce92f` (§4.7, §6.3).
- Dual-CCD + SMT behaviour (the stock setup of most X3D users) cannot be measured on the dev machine as configured; the width
  rule's dual-CCD branch is designed but unmeasured, to be confirmed on another machine or from user reports.
- `std::lerp` agreement between Windows and Linux: MSVC's library fuses it (§4.7), and the Linux executables hold no FMA in
  `std::lerp` (stage 3's count, §6.3), so the two may differ in the last bit.

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
