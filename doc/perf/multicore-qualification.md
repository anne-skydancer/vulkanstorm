<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Multicore qualification checkpoint, 2026-11-20

This checkpoint records the implementation at `vkstorm-devel` `886b58b16f` and the C6 proposal at `5c2c989918`.
The stage-5a synthesis and the stage-1b capture analysis are local records under
`reports/multicore-5a-2026-11-18/report.md` and `reports/mc1b-2026-11-17/analysis.md`. The tracked study describes the
original sequence, the designs and the kernel benchmarks. [Multicore and SIMD study](multicore-simd-study.md)

## Implementation status

| Stage | Area | Evidence and remaining qualification |
|---|---|---|
| 0 | AVX2 CPU requirement | The entry point, installer and Linux launcher guard AVX2 compatibility. This is a compatibility check. |
| 1 and 1b | Measurement | The November 16 baseline measured main-thread time. The November 17 session added a separate timer-query window and verified camera motion at Rooftops. [Stage 1 baseline](multicore-baseline-2026-11.md), [stage 1b tooling](https://github.com/anne-skydancer/vulkanstorm/pull/544) |
| 2 | Multicore scheduling | `LL::FrameJobs` runs CPU batches on a fixed helper pool with the main thread participating. The tests cover its batch protocol. The in-world width, contention and settle checks belong to C8, its first user. [PR #553](https://github.com/anne-skydancer/vulkanstorm/pull/553) |
| 3 and 4 | SIMD and FMA | AVX2 K1 and K2 batch vertex transforms in geometry fill. K5 has a tested eight-box frustum kernel and a camera API. Viewer traversal still uses the single-box tests. CI checks bit equivalence and the existing FMA instruction count. The stage-4 decision keeps contraction disabled. The kernel timings leave the in-viewer gain to measure. [PR #558](https://github.com/anne-skydancer/vulkanstorm/pull/558) |
| 5a | Thread-safety audit | The local synthesis records the caller and lifetime audit and the revised order. The occlusion-parent, random seed, face-index, mesh joint table and decode-worker fixes are on devel. Runtime checks carry into the candidate stages. |
| C8 | Multicore texture priority | Frame jobs compute ahead in read-only batches. The main thread applies the results in texture order inside the existing time box and handles faces that require serial work. Fixture comparisons passed. Live comparison, timings and settle remain open. [PR #575](https://github.com/anne-skydancer/vulkanstorm/pull/575) |
| 5c core | Multicore sun cascades | The 5c-0 split is serial. 5c-1 batches the Stable cascade culls, and 5c-2 batches render-map construction for cascades 1–3. GL submission stays on the main thread. Tests cover model culls and maps. The live equivalence and performance gates remain open, including whether 5c-2 earns its place. [PR #576](https://github.com/anne-skydancer/vulkanstorm/pull/576) |
| C6 | Multicore avatar animation | The animation cores of other avatars and eligible animated objects run as frame jobs. The viewer user's avatar stays on the main thread. Synthetic character comparisons and the Windows, Linux and Linux LTS CI builds passed at the recorded head, which matches the source tree merged as `5da54d3ab6`. The object walk and timing still need an in-world run. [PR #577](https://github.com/anne-skydancer/vulkanstorm/pull/577) |

## What stage 1b measured

On November 17 the measuring build `2b6c56a1d5` ran at Rooftops Ultra with about 52 avatars, an RTX 5070 Ti and a
2560 × 1369 window. The run is stored locally in `.captures/mc1-sl-20261117-185404/`. This build preceded C8, 5c and C6.
For each variant, the CPU and GPU columns come from the same 600-frame GLSubmitStats window with Tracy disconnected.
The reflection-probe update runs outside the timed frame. The FPS and moving 1% lows come from the separate Tracy capture.

| Rooftops variant | CPU window, ms/frame | GPU window, ms/frame | FPS with Tracy | Moving 1% low with Tracy |
|---|---:|---:|---:|---:|
| Still, sun | 19.44 | 25.72 | 38.47 | — |
| Still, sun and projector shadows | 22.54 | 27.64 | 34.91 | — |
| Moving, sun | 18.70 | 22.07 | 42.62 | 25.3 |
| Moving, sun and projector shadows | 19.83 | 23.61 | 39.78 | 24.6 |

Those Rooftops windows were GPU-bound. The stage-5a estimates of about 1.0 ms for C8, 0.5 ms for 5c and 2.1 ms for C6
are projected main-thread savings. Each needs a viewer A/B. The 25 FPS moving floor with collection off remains
unverified. The reports use different builds and capture conditions, so the November 16 Risk baseline and the November 17
Rooftops run each retain their own scope. [Stage 1 baseline](multicore-baseline-2026-11.md), [GPU timing procedure](../../scripts/inworld/README.md#second-life-session-for-the-multicore-baseline-mc1_sljson-mc1_sl_launchps1)

## In-world qualification

Use the staged Windows Autobuild `RelWithDebInfoFS_open` build with LTO and AVX2 and without an installer. A measurement
build can add `--tracy` and `-DVKS_GL_STATS:BOOL=ON` for the separate GPU window. Build with `-DVKS_FRAMEJOBS_GUARDS:BOOL=ON`
for the worker guard run. Record the commit, resolution, tier, avatar count, process affinity, and settings alongside
the captures. Use the same location, camera path and settings in each arm of a comparison. Turn compare modes off for
timings, verify that the moving camera actually moved, and report the median of three moving captures for the 1% low.
Report CPU ms, GPU ms and FPS against about 60 FPS standing in a crowd of about 60 avatars and the 25 FPS moving floor
at the default tier. The login-screen dry run in a fresh scratch profile verifies startup and the LEAP API. The live
checks below require an owner-assisted session. Rendering, shutdown or login changes require a Release + LTO build that
reaches the world before they are called safe. [Development conventions](../../AGENTS.md), [GL instrumentation](../gl/instrumentation.md)

For stage 3, compare matched geometry rebuild and loading windows before and after K1/K2. Export the self times of
`getGeometryVolume - position`, `getGeometryVolume - normal` and `getGeometryVolume - tangent` alongside total fill
time and settle. The standalone bit-exact tests and the in-viewer timing answer different questions.
[Stage 3 measurements](multicore-simd-study.md#63-stage-3-bit-exact-avx2-kernels)

1. **C8 and FrameJobs width.** At Rooftops, enable `TexturePixelAreaStats`, then run `TexturePixelAreaCompare` with
   `TexturePixelAreaJobs` enabled. Move and teleport once. `mismatch` must stay zero and the log must show batches and
   applied results. With compare off, use jobs off/on to measure `pass_ms_mean`, `cut`, `applied` and the serial fallback.
   Compare the same teleport and its texture settle in both arms. Sweep 3–7 helpers, restarting for each
   `FrameJobsHelpers` value. Zero selects the computed width, normally five helpers on the 8-core test machine.
   Compare main-thread time, the moving 1% low, package power, driver-thread CPU time and settle. The plan selects the
   smallest width within noise of the best and calls for a 4-core affinity run. [C8 procedure](https://github.com/anne-skydancer/vulkanstorm/pull/575), [Stage 2 design](multicore-simd-study.md#62-stage-2-llframejobs-and-the-width-rule)
2. **5c core.** Enable `RenderSunCascadeJobsStats` and compare mode `1` for ten minutes each at Risk and Rooftops.
   Use Stochastic (`RenderShadowMethod=2`) in an active sun view with `RenderShadowDetail=1` and `RenderShadowSplits=3`.
   Legacy has its own cascade path and can be checked separately. Check for nonzero cull and map comparisons and
   zero mismatches through standing, movement, a teleport and reduced split counts. For mode `2`, enable cascade 1 and aim at enough geometry
   that the cascade contains two distinct visible groups and two distinct draw infos of one render type. Both mismatch
   counters must rise on that view. Start a fresh process for a later clean run because the totals are cumulative.
   With compare off, alternate `RenderSunCascadeJobs` off/on. Require active helpers, compare combined `cull_ms` and
   `map_ms` on the same scene across three paired windows, and verify cascade GPU ms and the moving 1% low do not regress.
   The 5a plan targets at least 0.4 ms less main-thread time per sun pass at Rooftops. The PR sets a separate retention
   gate for 5c-2 of at least 0.1 ms less `map_ms` per sun pass. Compare bytes in a still scene. With `FrameJobsSerial`
   on, the 5a plan allows about 0.05 ms overhead against the pre-5c build. This also compares the 5c-0 restructure,
   since `RenderSunCascadeJobs` switches only 5c-1 and 5c-2. [5c procedure](https://github.com/anne-skydancer/vulkanstorm/pull/576)
3. **C6.** Enable `AvatarAnimationJobsStats` and compare mode `1` in a crowd. Require active job frames and nonzero joint
   and isolation checks with zero mismatches. Walk, teleport, and inspect seated avatars, attachments and animesh.
   With joint comparisons still nonzero, mode `2` must raise the joint mismatch count. With compare off, alternate
   `AvatarAnimationJobs` on/off/on at the same camera and read `loop_ms`, `cores_ms`, `job_batches` and `early_flushes`.
   The live compare checks recomputed joint
   transforms and batch isolation. The fixture supplies the serial whole-motion comparison. [C6 procedure](https://github.com/anne-skydancer/vulkanstorm/pull/577)

## Follow-up order from 5a

- **H5, serial projector-pool work.** Remeasure at the current devel tip before changing lamp pose bookkeeping or the
  work list. The earlier S1b capture reported about 2.1 ms per frame in projector update bookkeeping, including the
  pool's first fill. Use lamps-on and sun-only presets with `mc1b_rooftops` and `-GpuWindow` under owner-assisted login.
  Export Tracy timings for `projector update` (now the projector preparation scope), its subzones, shared `local shadows
  select`, and the tile and layer redraws. Record the initial fill and settled windows. The GL statistics view named
  `projector tiles` now spans both local-shadow pools and their shared selection. `last_frame_render_ms` is a single
  frame's redraw sample, so use the per-frame trace to attribute bookkeeping cost. [Capture procedure](../../scripts/inworld/README.md#second-life-session-for-the-multicore-baseline-mc1_sljson-mc1_sl_launchps1)
- **D-D, background decode scheduling.** C8 keeps the current `ImageDecode` width. The below-normal priority result
  in #575 used synthetic busy threads. A viewer priority comparison needs an experimental switch for that pool. Record
  the width and teleport settle with the current policy first. Hold `FSImageDecodeThreads` and the actual pool width
  fixed for a later priority A/B, then measure the texture pass, settle and moving lows at the same helper count.
  This is background multithreading with a separate queue.
  [C8 contention measurements](https://github.com/anne-skydancer/vulkanstorm/pull/575)
- **D6, serial mesh publication.** The draft from `886b58b16f` moves mesh publication to the start of the display
  geometry update. The world and sun-cascade sorts then share the published mesh data for that display frame. This is
  the prerequisite for C1-3's sort comparison. Its test results are recorded in the PR. Verify the pump count through
  mesh arrivals, compare mesh-heavy teleport settle with the base build, and run the owner-assisted Release world check.
  [D6 draft PR #578](https://github.com/anne-skydancer/vulkanstorm/pull/578)
- **OpenGL and later frame jobs.** The G-series targets GL submission. After D6, C1-3 records cascade `stateSort`
  effects for ordered main-thread application. C2/C5 takes the pure parts of world culling and sorting, keeping GL occlusion
  queries on the main thread and applying recorded changes in order. C8b covers the idle pixel-area caller. Geometry
  fill 5b waits for G5 arenas and is gated on load and streaming settle. The 5a synthesis leaves projector-tile culls
  (C3) out of the implementation queue. [Stage 5 design](multicore-simd-study.md#64-stage-5a-the-audit-the-multi-core-trio)
- **SIMD follow-ups.** K5 still needs a profitable viewer batch and equivalent cull results. Its measured speedup is a
  kernel result. K8 follows the compact rigged-joint list if it pays, and K4 remains low priority on its measured share.
  [SIMD candidates](multicore-simd-study.md#52-simd-candidates-beyond-llvector4as-sse)
