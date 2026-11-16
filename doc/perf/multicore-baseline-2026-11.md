<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Multi-core stage 1: baseline report

Status: **measured, 2026-11-16** (results in sections 4 to 6; sections 1 to 3 are the plan the session followed, with the corrections noted in 4.0).
The session ran on Second Life, scripted (Risk, three captures) and by hand (Risk, Isle of Repose, Rooftops, Ultra). Study: draft PR #517
(`doc/perf/multicore-simd-study.md` on `codex/multicore-simd-study-2026-11-15`; sections 1, 3.2, 3.6, 5.2 K4, 7.1 row 1,
7.2, 10.2). The owner accepted D1 to D5 as recommended on 2026-11-15; D1 means the session build also carried the particle
stage-0 probe (`codex/particles-s0-probe`, never merged).

**Summary.** (1) Neither crowded place reaches the 60 fps standing floor: Risk 56 fps, Rooftops 30 fps without the profiler; the 1 % low at Rooftops is 13 fps with it.
(2) Parallel culls, state sort and fill are 25 % (Risk) and 19 % (Rooftops) of the main-thread frame; **at best 1.26 and 1.19 times** at 6 wide, **only if the frame is CPU-bound**.
(3) The sun shadows are 38 to 42 % of the frame, and their serial GL submission (5 to 10 ms) is as large as, or larger than, the whole parallel part.
(4) **The projector pool's update costs 1.8 ms at Risk and 6.4 ms at Rooftops, mostly outside its zoned children**: the largest unclaimed item.
(5) N4, K7 and N8 are not worth their stages on these numbers; N1 and N3 are arrival-hitch fixes only.
(6) The settle after a teleport (23 s, 105 to 113 s into the crowd) is the biggest cost the owner sees. Details and the changes to the plan's order are in section 5.

Everything here is devel only: Tracy builds are never shipped, and the zones and counters compile to nothing without Tracy.

## 1. What this PR adds to the viewer

Zones (existing macros, `llprofiler.h`; they are gated by `LLProfiler::active` and compile to nothing with Tracy off):

| Zone | Where | Covers (study 3.2 row) |
|---|---|---|
| `projector update` | `LLProjectorShadows::update` | the whole per-frame pool update (P4), after the once-a-frame check |
| `projector updateLightSet` | `LLProjectorShadows::updateLightSet` | lamp set, nearest-first |
| `projector relayout` | `LLProjectorShadows::relayout` | atlas repack |
| `projector pinCasters` | `LLProjectorShadows::pinCasters` | pinned casters of the world cull |
| `projector renderTile` | `LLProjectorShadows::renderTile` | one static tile render (cull, sort and submit inside, through `renderShadow`) |
| `projector drawAvatars` | `LLProjectorShadows::drawAvatars` | one avatar-layer pass |
| `projector renderLayer` | `LLProjectorShadows::renderLayer` | the avatar layer's composite |
| `update particles` | `LLAppViewer::idle`, around `LLWorld::updateParticles` | particles (idle row) |
| `LLVOAvatar::updateCharacter` | `LLVOAvatar::updateCharacter` | avatar character update (idle row; `idleUpdate` already had a zone, this one splits motions and skeleton from it) |
| `LLVOAvatar::cullAvatarsByPixelArea` | `LLVOAvatar::cullAvatarsByPixelArea` | avatar cull by pixel area (delta report section 8 step 2, O2; Avatar category) |
| `LLAudioData::load` | `LLAudioData::load` | sound decode on the main thread (O2; Media category) |
| `LLMeshRepository::notifyMeshLoaded` | `LLMeshRepository::notifyMeshLoaded` | mesh-loaded notification to waiting objects (O2; Volume category) |

`LLHUDNameTag::updateAll` (O2) already had a zone (UI category), so nothing was added for it.

Parts of study 3.2 that already had a zone and were left alone: `df MainLoop`, `df idle`, `df Display`, `idle`,
`idle network`, `LLViewerObjectList::update`, region update (`FTM_REGION_UPDATE`), `updateMove` (`world update`), apparent
angles (`FTM_LOD_UPDATE`), `createObjects`, `updateGeom`, `rebuildPriorityGroups`, `updateCull`, `generateSunShadow` (and
`renderShadowImpl` below it), `updateImages`, `stateSort`, the reflection manager update, the hero probe update, `render_ui`
(`HUD`, `UI 3D`, `UI 2D`), `Swap`.

Plots and log line (`indra/newview/llframecounters.h`; per frame, plotted just before the frame mark, reset every frame):

| Plot | Counts | Why |
|---|---|---|
| `skin_palette_calls` | calls of `LLSkinningUtil::initSkinningMatrixPalette` | K4 call count (study 5.2) |
| `skin_palette_joints` | sum of the joint counts of those calls | K4 work per frame |
| `cull_passes` | `LLPipeline::updateCull`, including the projector tiles and avatar layers (they go through `renderShadow`) | study 3.3: world-view and shadow passes = `cull_passes - projector_tiles - projector_avatar_layers` (9 expected without a mirror) |
| `state_sort_passes` | `LLPipeline::stateSort(camera, result)`, same composition | study 3.3 |
| `projector_tiles` | projector pool static tile renders that reached `renderShadow` (after the early returns) | study 3.3 and the 0.35 ms cap |
| `projector_avatar_layers` | projector pool avatar-layer passes | study 3.3 |

`csvexport` cannot export plots (Tracy 0.11.1), so once a second the viewer log also gets one line,
`FrameCounters: frames=N skin_palette_calls=... skin_palette_joints=... cull_passes=... state_sort_passes=...
projector_tiles=... projector_avatar_layers=...` (sums over the second). Divide by `frames` for a per-frame mean. The line is
written only when Tracy is compiled in.

Counts not taken: study 7.1 and 7.2 name no other count. The counters are compiled in during every Tracy capture (relaxed atomics, one plot per counter per frame, one log line a second); that is part of the measured baseline. Teleport settle time comes from Tracy (below).

Zone names: zones declared by function take the function name in csvexport (`LLAppViewer::idle`, `LLPipeline::createObjects`, `LLPipeline::stateSort`); the names above are shorthand. `-f` matches substrings (`-f idle` also hits `df idle` and `idle network`), so use `csvexport` on the full list once and pick exact names. `updateCull` and `stateSort` fire in every pass (world, 4 sun cascades, reflection probe, HUD, projector tiles); the per-event export `-u` is needed to attribute them to a pass by their parent zone. Category-gated zones need the Pipeline, App and Avatar profiler categories active at runtime.

## 2. The owner session (study 7.2, with the counters and the particle probe)

Build (the bench README's `RelWithDebInfoOS` line fails with "Unknown argument --avx2"; use `RelWithDebInfoFS_open` with `--no-opensim` (OpenSim stays off); the Tracy dispatch of Build Windows / Build Linux (PR #518) is the CI route):
`autobuild configure -A 64 -c RelWithDebInfoFS_open -- --tracy --lto --avx2 --no-opensim "-DLL_TESTS:BOOL=FALSE"` then `autobuild build -A 64 -c RelWithDebInfoFS_open --no-configure`. Scenes: for stage 1 the session of 2a below on Second Life replaces the
S1 scene of the first study (the local OpenSim region with a dense OAR and 30 NPCs). Settings for every capture, written down once: vsync off, frame limiter
off, one window size and field of view, default graphics preset, Shadow method Stochastic (the default). Use a scratch
APPDATA/LOCALAPPDATA and log in by hand to the local region.

**Steps 1 to 8 below are the superseded OpenSim procedure (kept for the particle stage 0); the stage 1 session on Second Life is 2a.**

1. Load the dense OAR, `/8 spawn` the NPCs.
2. **MC1-still:** sit on the camera seat, wait 20 s, `tracy-capture.exe -a 127.0.0.1 -o MC1-still.tracy -s 60 -f`.
3. **MC1-moving:** sit on the dolly (or loop an `LLAgentPilot` recording), wait one leg, capture 60 s as `MC1-moving.tracy`.
4. **MC1-moving-2:** repeat step 3 once (noise check).
5. **MC1-teleports:** five teleports within the region between two landmarks and five to a second region and back, one
   continuous capture of up to 300 s (`MC1-teleports.tracy`). Write the wall-clock time of each teleport.
6. **fps without the profiler:** close Tracy and the capture, read the viewer's fps for 60 s on the dolly (Statistics bar), once.
7. The particle stage-0 captures of the bench README section 5 (scenes A to D, still and moving, D-still again, D-still at
   `RenderMaxPartCount` 8192, and the NPC-only baseline).
8. Keep the viewer log (`<scratch APPDATA>\<viewer folder>\logs\*.log`), the window size and field of view, and send the
   `.tracy` files, the log and the teleport times.

### 2a. The Second Life session (replaces the OpenSim region for stage 1)

The owner session runs on Second Life at a crowded venue, driven by the LEAP plugin (`scripts/inworld/README.md`, "Second Life session"). The
OpenSim region and the controlled particle-kit scenes are not used (the kit scenes are deferred; the particle probe is not part of this session).
The measuring build is built without OpenSim.

1. Run `scripts\inworld\mc1_sl_launch.ps1` (a scratch profile `.captures\mc1-profile-<stamp>`, new for every launch; it may keep your credentials if you tick "remember": delete it afterwards; the run folder is printed).
2. Log in by hand at home (any region). The plugin settles 20 s and records the login region and position.
3. A pop-up says "Ready for the busy region: teleport there now." Teleport to the venue and leave the viewer alone. The plugin sees the arrival
   (region cell changed, position still for 3 polls) and settles 20 s.
4. Unattended: `MC1-still` (current camera, 60 s capture), `MC1-moving` and `MC1-moving-2` (the camera turns around the avatar at 10 deg/s, 60 s
   each), the teleports (5 within the region, 5 between the venue and the login region, plus one "return" teleport after each odd group, so 12 in all at 30 s pauses; one capture `MC1-teleports` of 900 s, each teleport timed in
   `perf.json`, the returns are kind `return`; a teleport that fails is recorded and the series goes on), and the frame rate for 60 s with nothing connected (`fps_without_profiler`, from the `FrameCounters` log lines).
5. The pop-up "Session done: you can quit." appears ("Session ended with problems: see perf.json" if anything failed, or the run was stopped); settings (vsync off, frame limiter off) are restored. Send the run folder (`*.tracy`,
   `perf.json`, `viewer_log_excerpt.txt`) and the viewer log. Shadow method stays Stochastic (the default) and the scratch profile gives the default preset.

## 3. Pipeline: exports

Tools: Tracy 0.11.1 `tracy-capture.exe`, `tracy-csvexport.exe`, `tracy-profiler.exe` (in the build's
`build-vc180-64\packages\bin`; also from the wolfpld/tracy v0.11.1 Windows release). The versions must match the viewer's
client (0.11.1).

For each capture `X` in `MC1-still`, `MC1-moving`, `MC1-moving-2`, `MC1-teleports`:

```bash
tracy-csvexport.exe X.tracy > X.csv                                  # per zone: count, total, mean, min, max, std
tracy-csvexport.exe -e X.tracy > X-self.csv                          # self times (the split without double counting)
tracy-csvexport.exe -u -f "FTM_FRAME" X.tracy > X-frames.csv          # every frame: start and length, hitches, p99 (not "df MainLoop": that is a 5 microsecond zone)
tracy-csvexport.exe -m X.tracy > X-messages.csv                      # messages, if any
```

Commands that redirect output (`>`) run in Git Bash, cmd or PowerShell 7 (PowerShell 5.1 writes UTF-16).

From the files:

- main-thread ms per frame: `FTM_FRAME` mean (the frame zone; not `df MainLoop`); its split into the study 3.2 rows from `X.csv` and `X-self.csv`;
- average fps, 1 % low (p99 frame time) and hitches (frames over 2x the median) from `X-frames.csv` (the distance between two `FTM_FRAME` starts);
- the counters from the `FrameCounters:` log lines in the window of the capture (select by timestamp);
- settle time after each teleport: from the log (the per-second `FrameCounters` fps and the `Mesh Download Quiescent` marker), because no teleport was captured under Tracy (4.8); the log has no settle line and `SceneLoadingMonitorEnabled` is off by default;
- noise: `MC1-moving` against `MC1-moving-2`, the study's gate is under 3 %.

## 4. Results, 2026-11-16

Measured on the stage-1 build (`mc-stage1`, `RelWithDebInfoFS_open`, Tracy 0.11.1, LTO, AVX2, no OpenSim), Second Life, one dev machine
(Ryzen 9 9950X3D), window 2560 x 1369 maximised, vsync off. Everything below is **main-thread CPU time with the Tracy client attached**
unless it says otherwise (section 4.9 says what that means for reading it). CSV summaries of the tables sit in
`doc/perf/multicore-baseline-2026-11/` (`frames.csv`, `blocks.csv`, `amdahl.csv`, `fps-timeline-manual.csv`); the `.tracy` files are not committed. The CSV files in this folder are summaries of this report and carry its licence, GPL-3.0-or-later.

### 4.0 The captures and which are valid


| Capture | Place / preset | Camera | Frames / seconds | Zones per frame | Valid |
|---|---|---|---|---|---|
| `MC1-still` (scripted) | Risk, default preset | current, still | 2117 / 59.9 | 272 k | **only as a "settling" capture.** Taken 59 to 120 s after arrival; the mesh fetch went quiet 212 s after arrival (4.8). Fill, mesh notify and hitches are inflated |
| `MC1-moving` (scripted) | Risk, default preset | turns 10 deg/s | 2518 / 60.1 | 289 k | yes (the mesh fetch still finishing in its first seconds) |
| `MC1-moving-2` (scripted) | Risk, default preset | turns 10 deg/s | 2544 / 59.8 | 304 k | yes; the cleanest scripted capture (3 hitches) |
| `MC1-risk-ultra-still` (manual) | Risk, Ultra, crowd | still | 2431 / 60.0 | 317 k | yes. About 370 skinned draws a frame |
| `MC1-home-ultra-still` (manual) | Isle of Repose (home), Ultra | still | 5478 / 60.1 | 117 k | yes. One 499 ms frame (section 5, item 6) |
| `MC1-busy2-ultra-still` (manual) | Rooftops, Ultra, crowd (about 57 distinct avatars rezzed in the first minute) | still | 1690 / 60.0 | 392 k | yes. The stress case. About 400 skinned draws a frame |
| `MC1-risk-still` (manual) | Risk | still | about 15 fps |  | **invalid**: viewer in the background (BackgroundYieldTime). Not used |
| `mc1-manual-20261116-134026` |  |  |  |  | **invalid**. Not used |
| `MC1-teleports` (scripted) |  |  | empty file |  | no capture: the scripted teleport series failed (4.8) |


"Zones per frame" is the zone count of the Tracy capture log divided by the frames. The scripted sessions ran at the scratch profile's default
preset, the manual ones at Ultra, so the scripted and the manual Risk numbers differ in more than the camera.

How the numbers were made. `tracy-csvexport` 0.11.1 from the measuring build: the aggregate and the self-time (`-e`) exports of each capture, plus
per-event exports (`-u -f NAME`) for `FTM_FRAME`, `updateCull`, `stateSort`, `generateSunShadow`, `projector `, `Render Cube Face`, the four O2 zones,
`rebuildGeom`, `rebuildPriorityGroups`, `createObjects`, `idleUpdate`, `df idle` and `df Display`. Every per-frame number is the sum of the zone over the capture divided by the
number of `FTM_FRAME` zones, **main thread only** (the thread of `FTM_FRAME`; worker zones such as `ImageRequest::processRequest`,
`LLVolumeFace::cacheOptimize`, `LLAPRFile::readEx` and `cglt - sync` are left out, each ambiguous one was checked per event). The pass split of `updateCull` and `stateSort` is by
containment: a pass that starts inside a `projector renderTile` or `projector drawAvatars` zone is a projector pass, inside `generateSunShadow` a
sun pass, inside `Render Cube Face` a probe pass, otherwise a world pass. `stateSort` includes `postSort`. The skeleton named `df MainLoop` as the frame zone (corrected in section 3): it
is not (`df MainLoop` and `df mainloop` are 5 and 12 microsecond bookkeeping zones). **The frame zone is `FTM_FRAME`**, and the frame time is the distance between two
`FTM_FRAME` starts. The log fps and the counters come from the `FrameCounters:` lines of each capture's time window (their frame counts match Tracy's within 0.2 %
for the manual captures and within 2 % for the scripted ones).

### 4.1 Frame time


| Measure | MC1-still | MC1-moving | MC1-moving-2 | Risk Ultra | Home Ultra | Rooftops Ultra |
|---|---|---|---|---|---|---|
| frames | 2117 | 2518 | 2544 | 2431 | 5478 | 1690 |
| seconds | 59.9 | 60.1 | 59.8 | 60.0 | 60.1 | 60.0 |
| fps (frames / time) | 35.3 | 41.9 | 42.5 | 40.5 | 91.2 | 28.2 |
| frame ms, mean | 28.3 | 23.9 | 23.5 | 24.7 | 11.0 | 35.5 |
| frame ms, median | 24.0 | 22.2 | 23.1 | 24.0 | 10.6 | 33.5 |
| frame ms, p99 | 97.8 | 45.3 | 34.3 | 35.2 | 20.1 | 75.3 |
| 1 % low fps (1000 / p99) | 10.2 | 22.1 | 29.2 | 28.4 | 49.9 | 13.3 |
| mean fps of the worst 1 % of frames | 8.8 | 18.7 | 25.3 | 24.0 | 22.2 | 11.8 |
| frame ms, max | 140 | 66 | 55 | 60 | 499 | 96 |
| hitches (frame > 2 x median) | 123 | 26 | 3 | 4 | 47 | 38 |


Against the owner's budget (about 60 fps standing still in a crowded room, never below 25 while moving), with Tracy attached: Risk Ultra holds 40 fps with a
1 % low of 28; Rooftops holds 28 fps with a 1 % low of **13** (38 hitches in 60 s). Without the profiler the owner read 56 fps at Risk and 30.2 at Rooftops
(4.9): **neither crowded place reaches 60 fps standing still today.** Isle of Repose is far above the floor and partly GPU- or present-limited (`Swap` 4.7 ms, 4.2).

### 4.2 Main-thread ms per frame, by top-level block

Indented rows are inside the row above. Block totals overlap their children: do not add a row to its parent.


| Block | MC1-still | MC1-moving | MC1-moving-2 | Risk Ultra | Home Ultra | Rooftops Ultra |
|---|---|---|---|---|---|---|
| **`FTM_FRAME`** | 28.32 | 23.88 | 23.50 | 24.71 | 10.96 | 35.51 |
| idle (`df idle`) | 7.17 | 5.94 | 5.70 | 6.42 | 1.33 | 6.46 |
| &nbsp;&nbsp;object updates (`LLViewerObjectList::update`) | 5.07 | 4.15 | 4.08 | 4.26 | 0.58 | 4.61 |
| &nbsp;&nbsp;avatars (`LLVOAvatar::idleUpdate`) | 4.46 | 3.56 | 3.50 | 3.54 | 0.56 | 4.03 |
| &nbsp;&nbsp;&nbsp;&nbsp;animation (`updateCharacter`) | 3.02 | 2.46 | 2.44 | 2.40 | 0.36 | 2.76 |
| &nbsp;&nbsp;particles (`update particles`) | 0.50 | 0.38 | 0.34 | 0.75 | 0.09 | 0.17 |
| &nbsp;&nbsp;network incl. message handling (`idle network`) | 0.17 | 0.14 | 0.14 | 0.15 | 0.14 | 0.23 |
| &nbsp;&nbsp;apparent angles (`FTM_LOD_UPDATE`) | 0.45 | 0.40 | 0.38 | 0.49 | 0.04 | 0.52 |
| display (`df Display`) | 20.75 | 17.59 | 17.53 | 18.10 | 9.49 | 28.78 |
| &nbsp;&nbsp;`Render` | 19.25 | 15.94 | 15.83 | 16.44 | 8.84 | 27.71 |
| &nbsp;&nbsp;&nbsp;&nbsp;sun shadows (`generateSunShadow`) | 10.71 | 9.82 | 9.79 | 9.40 | 2.42 | 14.95 |
| &nbsp;&nbsp;&nbsp;&nbsp;projector pool (`projector update`) | 0.66 | 1.03 | 1.12 | 1.78 | 0.30 | 6.40 |
| &nbsp;&nbsp;&nbsp;&nbsp;geometry rebuild (`rebuildPriorityGroups`) | 3.26 | 1.34 | 1.25 | 0.59 | 0.21 | 0.27 |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;fill (`rebuildGeom`) | 2.26 | 1.15 | 1.08 | 0.33 | 0.10 | 0.13 |
| &nbsp;&nbsp;&nbsp;&nbsp;texture list (`updateImages`) | 2.06 | 2.05 | 2.03 | 2.09 | 0.40 | 2.15 |
| &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;decode priority (`updateImageDecodePriority`) | 1.70 | 1.85 | 1.85 | 1.95 | 0.30 | 1.96 |
| &nbsp;&nbsp;&nbsp;&nbsp;deferred submission (`renderGeomDeferred` + `renderDeferredLighting` + `renderGeomPostDeferred`) | 6.41 | 4.71 | 4.94 | 5.60 | 1.69 | 7.89 |
| &nbsp;&nbsp;UI (`render_ui`) | 1.09 | 0.80 | 0.74 | 0.74 | 0.33 | 0.89 |
| &nbsp;&nbsp;`Swap` | 0.38 | 0.10 | 0.06 | 0.10 | 4.70 | 3.47 |
| reflection probe (`df Snapshot`) | 1.53 | 1.65 | 1.70 | 1.67 | 0.64 | 1.08 |


The culls, the state sort and the sun-shadow passes by pass (ms per frame). The passes a frame are world 2, sun 6, probe 1, plus the projector tiles: world + sun + probe = 9, as study 3.3 expected (the `cull_passes` counter agrees, 4.6).


| ms per frame | MC1-still | MC1-moving | MC1-moving-2 | Risk Ultra | Home Ultra | Rooftops Ultra |
|---|---|---|---|---|---|---|
| `updateCull`, world view | 0.44 | 0.45 | 0.41 | 0.38 | 0.16 | 0.63 |
| `updateCull`, sun-shadow passes (inside `generateSunShadow`) | 1.17 | 1.45 | 1.40 | 1.30 | 0.41 | 1.46 |
| `updateCull`, projector tiles and avatar layers | 0.04 | 0.03 | 0.02 | 0.04 | 0.03 | 0.11 |
| `updateCull`, reflection probe face | 0.08 | 0.11 | 0.12 | 0.11 | 0.05 | 0.06 |
| **`updateCull` total** | 1.73 | 2.04 | 1.93 | 1.84 | 0.65 | 2.26 |
| `stateSort`, world view | 1.20 | 0.56 | 0.57 | 0.96 | 0.14 | 1.13 |
| `stateSort`, sun-shadow passes | 5.45 | 3.92 | 3.82 | 2.96 | 0.74 | 3.12 |
| `stateSort`, projector tiles and avatar layers | 0.06 | 0.02 | 0.01 | 0.04 | 0.05 | 0.15 |
| `stateSort`, reflection probe face | 0.05 | 0.06 | 0.06 | 0.07 | 0.02 | 0.04 |
| **`stateSort` total (incl. `postSort`)** | 6.75 | 4.56 | 4.47 | 4.02 | 0.95 | 4.43 |
| sun shadows minus their culls and sorts (GL submission and setup: serial) | 4.09 | 4.45 | 4.57 | 5.14 | 1.27 | 10.38 |
| `LLFace::getGeometryVolume` (fill inner loop) | 1.49 | 0.83 | 0.98 | 0.30 | 0.01 | 0.06 |


Shares against the study's estimates (the 4.1 table of the skeleton), Risk Ultra and Rooftops Ultra, ms and share of `FTM_FRAME`:


| Part | Study estimate | Risk Ultra | Rooftops Ultra |
|---|---|---|---|
| idle (`df idle`) | 30 to 45 % | 6.42 (26.0 %) | 6.46 (18.2 %) |
| network (`idle network`) | 3 to 10 % | 0.15 (0.6 %) | 0.23 (0.6 %) |
| object updates (`LLViewerObjectList::update`) |  | 4.26 (17.2 %) | 4.61 (13.0 %) |
| avatars (`idleUpdate`) | 10 to 25 % | 3.54 (14.3 %) | 4.03 (11.4 %) |
| regions + moves (`FTM_REGION_UPDATE`, `world update`) | 1 to 5 % each | 0.07 (0.3 %) | 0.08 (0.2 %) |
| particles | 1 to 10 % | 0.75 (3.0 %) | 0.17 (0.5 %) |
| apparent angles | 1 to 3 % | 0.49 (2.0 %) | 0.52 (1.5 %) |
| `Render` (render preparation + GL submission) | 35 to 50 % + 15 to 30 % | 16.44 (66.5 %) | 27.71 (78.0 %) |
| geometry create and rebuild (`createObjects` + `Update Geom` + `rebuildPriorityGroups`) | 3 to 15 % | 1.07 (4.3 %) | 0.49 (1.4 %) |
| world cull (world passes of `updateCull`) | 4 to 8 % | 0.38 (1.6 %) | 0.63 (1.8 %) |
| all culls (every `updateCull` pass) |  | 1.84 (7.5 %) | 2.26 (6.4 %) |
| sun shadows (`generateSunShadow`) | 10 to 20 % | 9.40 (38.0 %) | 14.95 (42.1 %) |
| projector pool (`projector update`) | at most 0.35 ms | 1.78 (7.2 %) | 6.40 (18.0 %) |
| texture list (`updateImages`) | 3 to 10 % | 2.09 (8.5 %) | 2.15 (6.0 %) |
| world state sort (world passes) | 8 to 15 % | 0.96 (3.9 %) | 1.13 (3.2 %) |
| all state sorts (every pass) |  | 4.02 (16.3 %) | 4.43 (12.5 %) |
| reflection probe (`df Snapshot`) | 3 to 8 % | 1.67 (6.7 %) | 1.08 (3.0 %) |
| mirror (hero probe) | 0 to 20 % | no zone fired (no mirror) | no zone fired |
| GL submission, deferred (`renderGeomDeferred` + `renderDeferredLighting` + `renderGeomPostDeferred`) | 15 to 30 % | 5.60 (22.7 %) | 7.89 (22.2 %) |
| UI (`render_ui`) | 3 to 8 % | 0.74 (3.0 %) | 0.89 (2.5 %) |
| `Swap` |  | 0.10 (0.4 %) | 3.47 (9.8 %) |


Where the estimates were wrong, in the crowded places:

- **Sun shadows are 38 % (Risk) and 42 % (Rooftops) of the main-thread frame, not 10 to 20 %.** Their cascade cull + sort is 4.3 ms and 4.6 ms; the remaining **5.1 ms and 10.4 ms are GL submission and setup of the shadow passes, which no thread can take** (one GL context). That serial rest is as large as the whole parallel part at Risk and 1.5 times it at Rooftops (4.10).
- **The projector pool is not "at most 0.35 ms".** `projector update` costs 1.8 ms (7 %) at Risk and **6.4 ms (18 %) at Rooftops**. The tile renders inside it are 0.36 and 0.93 ms (`renderTile`, which includes its avatar layer); **1.4 ms and 5.4 ms are the zone's own time**, outside every zoned child (4.4). At Rooftops it is the largest single self-time zone of the frame.
- **The state sort is larger and the world cull smaller than estimated.** The world cull is 0.4 to 0.6 ms (1.5 to 1.8 %, estimate 4 to 8 %); all culls together are 7.4 % and 6.4 %. The sort is 16 % and 12.5 % over all passes but only 3 to 4 % in the world pass (estimate 8 to 15 %): three quarters of the sort is the shadow cascades.
- **Idle is 18 to 26 %** (estimate 30 to 45 %) and **network is 0.6 %** (estimate 3 to 10 %): message handling is 0.10 to 0.17 ms a frame in the crowd, 5 to 15 times below the estimate. Particles are 3 % at Risk and 0.5 % at Rooftops.
- **Avatars are inside the estimate** (11 to 14 %; animation alone 8 to 10 %).
- **Geometry rebuild is 1.4 to 4.3 %**, the low end of its 3 to 15 % range; in a settled scene the fill proper (`rebuildGeom`) is 0.1 to 0.3 ms.
- **The texture list is 6 to 8.5 %**, inside the estimate, but it is a flat 2.0 to 2.1 ms in every crowded capture (4.5).
- **Reflection probe 3 to 7 %**, inside the estimate. **UI 2.5 to 3 %**, just under it. **GL submission holds** (22 to 23 % deferred, plus the shadow submission counted under sun shadows).

### 4.3 The four O2 zones and the remove/hoist candidates N1 to N4


| O2 zone | MC1-still | MC1-moving | MC1-moving-2 | Risk Ultra | Home Ultra | Rooftops Ultra |
|---|---|---|---|---|---|---|
| `LLVOAvatar::cullAvatarsByPixelArea` ms per frame | 0.037 | 0.050 | 0.030 | 0.035 | 0.005 | 0.053 |
| &nbsp;&nbsp;calls per frame | 1.85 | 3.35 | 1.61 | 1.62 | 1.62 | 2.43 |
| &nbsp;&nbsp;p99 call ms | 0.049 | 0.040 | 0.033 | 0.042 | 0.006 | 0.049 |
| &nbsp;&nbsp;worst call ms | 0.90 | 0.46 | 0.06 | 0.46 | 0.10 | 0.16 |
| `LLAudioData::load` ms per frame | 0.002 | 0.002 | 0.002 | 0.000 | 0.001 | 0.000 |
| &nbsp;&nbsp;calls per frame | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| &nbsp;&nbsp;p99 call ms | 0.911 | 0.870 | 0.740 | 0.337 | 1.285 | 0.269 |
| &nbsp;&nbsp;worst call ms | 0.91 | 0.87 | 0.74 | 0.34 | 1.29 | 0.27 |
| `LLMeshRepository::notifyMeshLoaded` ms per frame | 0.096 | 0.022 | 0.002 | 0.002 | 0.001 | 0.001 |
| &nbsp;&nbsp;calls per frame | 3.70 | 1.13 | 0.07 | 0.04 | 0.00 | 0.04 |
| &nbsp;&nbsp;p99 call ms | 0.182 | 0.117 | 0.333 | 0.182 | 0.949 | 0.225 |
| &nbsp;&nbsp;worst call ms | 17.65 | 1.07 | 0.42 | 0.29 | 0.95 | 0.33 |
| `LLHUDNameTag::updateAll` ms per frame | 0.025 | 0.017 | 0.015 | 0.028 | 0.002 | 0.033 |
| &nbsp;&nbsp;calls per frame | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 |
| &nbsp;&nbsp;p99 call ms | 0.058 | 0.034 | 0.025 | 0.041 | 0.003 | 0.050 |
| &nbsp;&nbsp;worst call ms | 0.21 | 0.13 | 0.04 | 0.48 | 0.02 | 0.15 |


`LLMeshRepository::notifyLoadedMeshes` (the whole mesh-notify pass, which holds the four locks of N2 and does the pending erase of N3), ms per frame: MC1-still 0.629, MC1-moving 0.055, MC1-moving-2 0.026, Risk Ultra 0.032, Home Ultra 0.026, Rooftops Ultra 0.035. Its worst call in `MC1-still` was 17.7 ms.

| Candidate | Estimate (delta survey) | Measured | Verdict |
|---|---|---|---|
| **N1** swap the loaded faces instead of copying (`notifyMeshLoaded`) | 0.05 to 0.5 ms per LOD, several ms in an arrival frame | settled: 0.001 to 0.002 ms/frame (0.04 calls/frame); `MC1-moving` 0.022; **`MC1-still` 0.096 ms/frame**, 3.7 calls/frame, one call of 17.7 ms. In the settled manual captures the worst call is 0.3 to 0.95 ms | an **arrival-hitch item only**: no steady-state gain. The 17.7 ms call is the real target. Keep (0.25 d) if arrival hitches matter |
| **N2** narrow the four-lock window | repo-thread stall, not main-thread ms | not visible in these zones (the waits are on the repo thread) | **not measured** |
| **N3** one range erase in the pending push | O(n squared) with thousands pending | no zone of its own; bounded above by `notifyLoadedMeshes`: 0.63 ms/frame while meshes load (`MC1-still`), 0.03 to 0.06 settled | small, only while the mesh queue is long. Keep (0.1 d) |
| **N4** hoist `cullAvatarsByPixelArea` | 0.1 to 1.5 ms in crowded and arrival frames | **0.03 to 0.05 ms/frame** at about 60 avatars (1.6 to 3.4 calls/frame, about 21 microseconds a call); worst call 0.9 ms while loading, 0.16 to 0.46 ms settled | **the estimate was 3 to 30 times too high.** The most it can return is about 0.05 ms (0.1 to 0.2 % of a frame) for a medium-risk change (impostor state): **drop it** |
| `LLAudioData::load` (N8) | 0.1 to 2 ms per first play | 6 loads in a 60 s scripted capture, none in the others; 0.3 to 1.3 ms a load | a once-per-sound hitch: the async load is **not worth a stage on this evidence** |
| `LLHUDNameTag::updateAll` (O(n squared) name tags) | | 0.015 to 0.033 ms/frame at about 60 avatars (worst 0.5 ms) | negligible at this crowd |

### 4.4 The projector pool


| ms per frame | MC1-still | MC1-moving | MC1-moving-2 | Risk Ultra | Home Ultra | Rooftops Ultra |
|---|---|---|---|---|---|---|
| `projector update` (the whole per-frame pool update) | 0.664 | 1.034 | 1.115 | 1.783 | 0.305 | 6.399 |
| &nbsp;&nbsp;`updateLightSet` | 0.013 | 0.020 | 0.020 | 0.020 | 0.005 | 0.108 |
| &nbsp;&nbsp;`renderTile` (includes its `drawAvatars`) | 0.398 | 0.280 | 0.170 | 0.363 | 0.169 | 0.931 |
| &nbsp;&nbsp;`renderLayer` (includes its `drawAvatars`) | 0.008 | 0.000 | 0.000 | 0.003 | 0.088 | 0.000 |
| &nbsp;&nbsp;`drawAvatars` (inside the two above) | 0.011 | 0.000 | 0.000 | 0.006 | 0.100 | 0.205 |
| `pinCasters` (outside `update`: called from `renderShadowImpl`) | 0.211 | 0.179 | 0.109 | 0.208 | 0.101 | 0.419 |
| **`projector update` minus `updateLightSet`, `renderTile`, `renderLayer`** (the unzoned part) | 0.245 | 0.734 | 0.925 | 1.397 | 0.043 | 5.360 |
| tile renders per frame | 0.33 | 0.31 | 0.15 | 0.99 | 0.18 | 1.09 |
| avatar-layer passes per frame | 0.10 | 0.00 | 0.00 | 0.09 | 1.89 | 0.62 |


The unzoned part equals the zone's self time in the `-e` export (0.73 at `MC1-moving`, 1.39 at Risk, 5.36 at Rooftops). It is what the code does every frame outside a tile render: `layoutChanged()`, the per-lamp pose and parameter read that detects a moved lamp (the code comment: "what a static scene still costs per frame"), the dirty-feed test (`LLShadowDirtyFeed::beginFrame`) and the plan. That reading is from the code comments [inferred]; the capture only shows that the time is in the zone and in no child. It is 0.2 to 0.9 ms in the scripted captures, 1.4 ms at Risk Ultra and **5.4 ms at Rooftops**: it does not follow the number of tile renders per frame (0.15 to 1.1), so it may follow the number of lamps in range [inferred, not measured]. It is **4 times the 0.35 ms cap at Risk and 15 times at Rooftops** if the cap is counted in main-thread ms.

### 4.5 Ranked main-thread zones (top 20 by self time, `MC1-moving`; the same zones at Risk Ultra and Rooftops)

Self time from the `-e` export, worker-thread zones removed (texture and mesh decode, the file system, the texture cache, `cglt`). Zone times include Tracy's own cost (4.9); zones with thousands of calls a frame are inflated most.


| Rank | Zone | MC1-moving self ms/frame | calls/frame | Risk Ultra | Rooftops Ultra |
|---|---|---|---|---|---|
| 1 | `calcPixelArea - rigged` | 1.287 | 1639.1 | 1.486 | 1.518 |
| 2 | `build render map` | 1.092 | 9.4 | 1.016 | 1.256 |
| 3 | `LLRenderPass::pushBatch` | 0.955 | 9914.0 | 0.907 | 1.010 |
| 4 | `LLMotionController::updateMotionsByType` | 0.779 | 35.5 | 0.793 | 0.913 |
| 5 | `projector update` | 0.731 | 1.0 | 1.392 | 5.356 |
| 6 | `ra - push batch` | 0.661 | 2561.8 | 0.825 | 1.195 |
| 7 | `LLRenderPass::pushUntexturedBatch` | 0.660 | 11239.7 | 0.634 | 0.732 |
| 8 | `LLKeyframeMotion::onUpdate` | 0.621 | 151.3 | 0.663 | 0.698 |
| 9 | `LLViewerOctreeCull::traverse` | 0.582 | 15367.0 | 0.507 | 0.666 |
| 10 | `LLViewerObjectList::update` | 0.532 | 1.0 | 0.687 | 0.568 |
| 11 | `LLVertexBuffer::mapVertexBuffer` | 0.532 | 1346.2 | 0.188 | 0.021 |
| 12 | `LLVOAvatar::updateCharacter` | 0.481 | 45.7 | 0.313 | 0.329 |
| 13 | `AllInside` | 0.407 | 10628.9 | 0.368 | 0.542 |
| 14 | `LLFace::calcPixelArea` | 0.384 | 2972.9 | 0.368 | 0.336 |
| 15 | `LLViewerTextureList::updateImageDecodePriority` | 0.358 | 361.2 | 0.336 | 0.358 |
| 16 | `LLGLSLShader::uniform1f` | 0.329 | 27315.3 | 0.340 | 0.422 |
| 17 | `FTM_SIMULATE_PARTICLES` | 0.327 | 1.0 | 0.669 | 0.144 |
| 18 | `StateSort: visible groups` | 0.325 | 9.5 | 0.345 | 0.415 |
| 19 | `LLOctreeCull::processGroup` | 0.324 | 7507.9 | 0.297 | 0.357 |
| 20 | `LLRender::syncMatrices` | 0.308 | 22481.7 | 0.343 | 0.414 |


At Rooftops the order changes: `projector update` is **first** (5.36 ms self), then `Swap` (3.44 ms: the main thread waiting for the GPU or the driver), then `calcPixelArea - rigged` (1.52 ms). `calcPixelArea - rigged` is 1.3 to 1.5 ms in every crowded capture, and the enclosing `LLFace::calcPixelArea` is 1.95 ms at Risk. `calcPixelArea` has two callers: `updateImageDecodePriority` (llviewertexturelist.cpp:972, under `updateImages`) and `LLFace::getTextureVirtualSize` (llface.cpp:2357), reached from `LLVOVolume::updateTextures` (llvovolume.cpp:974) in `LLViewerObjectList::update`, the idle path. Each face is gated at 0.1 s, so whichever caller comes first pays. At Risk Ultra, about 1.61 ms of the 1.95 ms sits under `updateImageDecodePriority` (which has 0.34 ms self) and about 0.35 ms under `getTextureVirtualSize`; MC1-still has 0.46 ms (26 %) on the idle path. So `calcPixelArea` is 1.7 to 1.96 ms in total in the crowds, of which about 1.3 to 1.6 ms is under `updateImageDecodePriority`: **most of the texture list's flat 2.1 ms is the per-frame pixel area of the rigged faces of the avatars** (consistent with nesting; not checked per event).

### 4.6 Frame counters (per frame, from the `FrameCounters:` log lines in each capture's window)


| Counter | MC1-still | MC1-moving | MC1-moving-2 | Risk Ultra | Home Ultra | Rooftops Ultra | Note |
|---|---|---|---|---|---|---|---|
| `skin_palette_calls` | 321.9 | 277.0 | 287.8 | 358.0 | 47.0 | 399.6 | K4: 277 to 400 calls a frame in a crowd, 47 at home |
| `skin_palette_joints` | 11381 | 9922 | 10312 | 14058 | 1798 | 14085 |  |
| `cull_passes` | 9.45 | 9.31 | 9.15 | 10.08 | 11.06 | 10.72 | minus the two projector counters: 9.0 in every capture, as study 3.3 expected |
| `state_sort_passes` | 9.65 | 9.49 | 9.30 | 10.31 | 11.06 | 11.05 | minus the two projector counters: 9.0 to 9.3 (the world view sorts 2.0 to 2.3 times) |
| `projector_tiles` | 0.35 | 0.31 | 0.15 | 0.99 | 0.18 | 1.08 | tiles that reached `renderShadow` |
| `projector_avatar_layers` | 0.10 | 0.00 | 0.00 | 0.09 | 1.88 | 0.63 |  |


Cost of the palette work (K4): `initSkinningMatrixPalette` 0.22 to 0.27 ms/frame and `uploadMatrixPalette` 0.45 to 0.65 ms/frame in a crowd (together 3.3 % of the Risk frame and 2.6 % of the Rooftops frame). The texture-coordinate transform of K7, the zone `tgd - transform`, is 0.003 to 0.04 ms/frame (0.004 at Risk, 0.003 at Rooftops).

### 4.7 Noise between the two moving captures


| Metric | MC1-moving | MC1-moving-2 | Difference |
|---|---|---|---|
| `FTM_FRAME` mean ms | 23.88 | 23.50 | -1.6 % |
| frame ms, median | 22.20 | 23.11 | +4.1 % |
| frame ms, p99 | 45.27 | 34.29 | -24.3 % |
| fps | 41.9 | 42.5 | +1.6 % |
| sun shadows ms | 9.82 | 9.79 | -0.3 % |
| `updateCull` all passes ms | 2.04 | 1.93 | -5.3 % |
| `stateSort` all passes ms | 4.56 | 4.47 | -2.0 % |
| avatars (`idleUpdate`) ms | 3.56 | 3.50 | -1.7 % |
| fill (`rebuildGeom`) ms | 1.15 | 1.08 | -5.6 % |


The study's gate is a run-to-run noise under 3 %. **The frame mean and the big blocks pass** (frame mean, fps, sun shadows, avatars, sorts: 0.3 to 2.0 %). **The small blocks and the tail do not:** the culls differ by 5.3 %, the fill by 5.6 %, the median frame by 4.1 %, the p99 by 24 % and the 1 % low by 32 %, because the first moving capture still had hitches from the mesh and texture settling (26 frames over twice the median against 3). So a stage judged on fps or on a block above 3 ms can use the 3 % gate; a stage judged on a small block or on the **1 % low needs a settled scene and the median of three captures**. The scripted `MC1-still` is not a noise partner (4.0).

### 4.8 Time to settle

**Criteria.** The viewer log has one `FrameCounters` line a second; its `frames` is the fps of that second. Settled means (a) the fps stays at or above 90 % of the steady fps for 10 consecutive seconds, and (b) the mesh fetch has gone quiet (the viewer's own `Mesh Download Quiescent` event marker). The steady fps is the median of a window named in the tables. **The log has no texture-fetch signal**, so "quiet texture fetching" is not measured; the numbers are the fps and the mesh quiet only. One-second resolution: no per-frame view of a dip.

**Login** (`STATE_CLEANUP --> STATE_STARTED` is time zero; the mesh marker's start is the arrival in the login region, a few seconds earlier):


| Log (scratch profile) | STATE_STARTED | Steady fps used | fps settled after | Mesh quiet after arrival in the login region | Note |
|---|---|---|---|---|---|
| `mc1-profile-manual-20261116-134250` (Risk, Ultra) | 13:43:41 | 61 (13:49:00 to 13:52:00) | **320 s** (13:49:01) | 226 s (13:47:11), 210 s after `STATE_STARTED`; 57,558 fetches | **the fps part is not trustworthy**: from 13:44:00 to 13:48:30 the log shows a flat 14 to 18 fps, the BackgroundYieldTime pattern of the invalid `MC1-risk-still` capture (viewer in the background); it rose at about 13:48:40 |
| `mc1-profile-20261116-124158` (scripted, home region) | 12:43:05 | 60 (the limiter that is on until the plugin turns it off) | 20 s | 31.7 s |  |
| `mc1-profile-20261116-130240` | 13:04:26 | 60 (limiter) | 80 s | 159.9 s | 9 to 28 fps from 13:04:00 to 13:05:00 |
| `mc1-profile-20261116-133338` | 13:34:13 | 60 (limiter) | 20 s | 58.6 s |  |
| `mc1-profile-20261116-133624` | 13:36:57 | 60 (limiter) | 19 s | 30.0 s |  |
| `mc1-profile-20261116-124936` | 12:50:16 | 60 (limiter) | 15 s | no login marker (the teleport at 12:50:46 came first) |  |


After a login into a light region the viewer is at its frame rate in 15 to 20 s (80 s once) and the mesh fetch is quiet after 30 to 60 s (160 s once); into the Risk crowd the mesh fetch needed **226 s**.

**Teleports.** The start is the log line "A teleport request has been sent" (`TELEPORT_REQUESTED`); the movement complete is `Received ProcessAgentMovementComplete`; the arrival screen ends at "arrival_fraction is 1 ... TELEPORT_NONE".


| Teleport | Request | Movement complete | Arrival screen done | fps dip | fps settled after the request | Mesh quiet after the request | Steady fps used |
|---|---|---|---|---|---|---|---|
| Risk to Isle of Repose (the owner's) | 13:56:37 | +2 s | **+4 s** | four seconds at 29 to 46 fps (worst 29 at 13:56:44); 55 before | **23 s**: 10 s at 85 or more (90 % of 94), from 13:57:00 | 22.1 s (3739 fetches) | 94 (13:57:20 to 13:58:20, Tracy attached) |
| Isle of Repose to Rooftops (the second manual one) | 14:00:34 | +1 s | +3 s | **10 fps at 14:01:07**; 21 s under 25 fps in the first 110 s; 18 to 23 fps at 14:01:58 to 14:02:12; 98 before | **105 s**: 10 s inside 27 to 33 fps, from 14:02:19 | 112.8 s (38,123 fetches) | 30 (14:03:00 to 14:04:00) |
| scripted, home to the busy region (12:50:46) | 12:50:46 | +1 s | +3 s | 110 down to 31 to 39 within 60 s as the crowd rezzed; no steady value (the camera orbit changes the fps) | not defined | 211.7 s (39,153 fetches) |  |
| scripted, home to the busy region (13:37:44) | 13:37:44 | +1 s | +3 s | 8 fps in the 10 s bucket at 13:38:10; then 15 to 19 fps for two minutes (the run had stalled) | not defined | not logged |  |


The **teleport itself costs 3 to 4 s** of loading screen in all four cases. **What the owner feels is the settle after it:** 23 s into a light region (Isle of Repose) and 105 to 113 s into the crowd (Rooftops), where the fps fell to 10 and spent 21 s under 25 while 38,000 mesh fetches ran. The arrival frames themselves run at 150 to 225 fps (the cheap loading screen) and are no part of the dip.

The scripted teleport series (5 within the region, 5 to a second region, returns) **gave no result**: in two sessions the first within-region teleport landed 93 m off the target or did not arrive in 120 s, the series stopped, `MC1-teleports.tracy` is empty and `fps_without_profiler` is null.

### 4.9 Frame rate with and without the profiler


| Place | fps without the profiler (owner, Statistics bar) | fps with Tracy (owner) | fps with Tracy (Tracy frames / 60.1 s) | Zones per second | Zones per frame | Cost of the profiler per frame | `Swap` ms/frame |
|---|---|---|---|---|---|---|---|
| Risk Ultra | 56.0 | 42.9 | 40.5 | 12.9 M | 317 k | +5.4 ms (17.9 to 23.3 ms, +30 %; about 17 ns a zone) | 0.10 |
| Isle of Repose Ultra | 93.5 | 92.0 | 91.2 | 10.7 M | 117 k | +0.2 ms (1.6 %) | 4.70 |
| Rooftops Ultra | 30.2 | 28.8 | 28.2 | 11.0 M | 391 k | +1.6 ms (4.6 %) | 3.47 |


The profiler's cost is **hidden where the main thread waits for the GPU**. At Risk the main thread never waits (`Swap` 0.1 ms) and the full cost shows (about 17 ns a zone); at Isle of Repose (`Swap` 43 % of the frame) and Rooftops (10 %) most of it is absorbed by the wait. Reading the numbers:

- **At Risk the Tracy-attached ms are about 1.38 times the real ones, and not evenly.** The zones that hold many tiny zones are inflated most: per frame the octree cull has 41 k `llvieweroctree.cpp` and 19 k `llspatialpartition.cpp` zones, the shader binds 86 k, `lldrawpool` pushes 28 k. The cull and sort ms in this report are **upper bounds** of what a viewer without the profiler spends in them. Cull, sort and fill hold about a fifth of the zones and a quarter of the time, so they are not over-represented, but a uniform factor is a guess.
- The **shares** are more reliable than the ms. The **Amdahl bound** (4.10) is given with and without a uniform scaling by the measured fps ratio (0.72 at Risk, 0.93 at Rooftops, 0.98 at Isle of Repose).
- Isle of Repose and Rooftops are **not CPU-bound** (the `Swap` wait): their fps do not follow the main-thread ms. GPU time is not measured here.
- No-profiler windows of the log agree with the owner's numbers (Risk median 56 over 13:53:20 to 13:54:40; Rooftops median 30 over 14:03:00 to 14:04:00).
- The scripted session has no no-profiler reading (`fps_without_profiler` is null).

### 4.10 Amdahl: what parallel culls, fill and sort can remove

The FrameJobs plan targets first (plan sections 5c and 5b): the **culls** (cascades, world, projector tiles), the **state sort** (with `postSort`) and the **fill** (`rebuildGeom`). The width rule of the study is 5 helpers + the main thread, 6 wide. Best case: the parallel part divides by 6 with no merge cost, no serial prefix and no GL work (everything else of the frame stays); "unbounded" removes the whole part. The fps assumes the frame stays CPU-bound.


| Capture (Tracy on) | Frame ms | Culls (all passes) | Sort (all passes) | Fill | Parallel part (share) | Removed at 6 wide | fps now | fps, 6 wide | fps, unbounded |
|---|---|---|---|---|---|---|---|---|---|
| scripted moving-2 | 23.50 | 1.93 | 4.47 | 1.08 | 7.49 (32 %) | 6.24 | 42.6 | 57.9 | 62.5 |
| Risk Ultra | 24.71 | 1.84 | 4.02 | 0.33 | 6.19 (25 %) | 5.16 | 40.5 | 51.2 | 54.0 |
| Rooftops Ultra | 35.51 | 2.26 | 4.43 | 0.13 | 6.82 (19 %) | 5.68 | 28.2 | 33.5 | 34.9 |
| Isle of Repose Ultra | 10.96 | 0.65 | 0.95 | 0.10 | 1.70 (15 %) | 1.41 | 91.2 | 104.8 | 108.0 |


With the profiler's cost scaled out (the part and the frame both multiplied by the measured fps ratio; a guess that the cost is even) and the owner's no-profiler fps as the start:

| Place | fps now (no profiler) | fps, 6 wide | fps, unbounded | Condition |
|---|---|---|---|---|
| Risk Ultra | 56.0 | **about 71** (x 1.26) | about 75 | CPU-bound (`Swap` 0.1 ms): real if the scaling holds |
| Rooftops Ultra | 30.2 | **about 36** (x 1.19) | about 37 | an upper bound: 10 % of the frame is `Swap` wait and the GPU frame time is unknown; if the GPU is the limit the gain is smaller, **possibly none** |
| Isle of Repose | 93.5 | about 107 | about 111 | GPU- or present-limited (`Swap` 43 %): probably no gain |

**In ms:** the parallel part is 6.2 ms at Risk (25 % of the main-thread frame; 5.2 ms removed at 6 wide) and 6.8 ms at Rooftops (19 %; 5.7 ms removed). It is made of: the **cascades' cull + sort 4.3 ms (Risk) and 4.6 ms (Rooftops)**, the world view's cull + sort 1.3 and 1.8, the projector tiles' 0.08 and 0.26, the probe face's 0.18 and 0.10, the fill 0.33 and 0.13. At 6 wide the Risk frame goes from 24.7 to 19.5 ms (Tracy on).

**Adding the next two blocks of the plan** (avatar animation `updateCharacter`, 2.4 and 2.8 ms, and the texture list `updateImages`, 2.1 and 2.1 ms, whose decode-priority pass is data-parallel): the part grows to 10.7 ms at Risk (43 %) and 11.7 ms at Rooftops (33 %); at 6 wide, with the cost scaled out, Risk reaches **about 88 fps** and Rooftops **about 42 fps** (same conditions as above; exactness of the animation split is not studied here).

**Where the estimates were wrong.**

- The study put the **world cull at 4 to 8 %** and the **sun shadows at 10 to 20 %**; measured, the world cull is 1.5 to 1.8 % and the shadows 38 to 42 %. The parallel win sits in the **cascade sort and cull** (13 to 17 % of the frame); the sort estimate (8 to 15 % for the world) was right in size and wrong in place: 3 to 4 % in the world view, 9 to 12 % in the cascades.
- The serial rest of the shadows (5.1 ms at Risk, 10.4 ms at Rooftops) is **as large as the whole parallel part at Risk and 1.5 times it at Rooftops** (6.2 and 6.8 ms). No estimate named it. Threads cannot reach it.
- The fill share (3 to 15 % with the rebuild) holds only while a scene loads: `MC1-still` shows 2.3 ms of fill, settled Risk 0.33 and Rooftops 0.13. **Fill is a loading cost, not a frame cost**, so a parallel fill buys no steady-state fps.
- The projector pool's 0.35 ms cap was a tile-render cap; the pool's own time outside the tiles is 4 times the cap at Risk and **15 times at Rooftops**.
- The delta survey's N4 (0.1 to 1.5 ms) is 3 to 30 times smaller than estimated (4.3); its K7 is 0.003 to 0.04 ms a frame (4.6) inside a fill share that is itself 0.1 to 0.3 ms settled.
- The 6-wide Amdahl bound, with the parallel part at 19 to 25 % of the main-thread frame, is **1.19 (Rooftops) to 1.26 (Risk) times**, against an unbounded 1.33 at Risk.

### 4.11 Stage 1 gate (study 7.1)

Run-to-run noise under 3 % on the scripted Risk scene: **met for the frame mean, the fps and the blocks above 3 ms, not for the small blocks (culls, fill) or the 1 % low** (4.7). Ranked main-thread zones: **measured** (4.5). The study 3.2 table: **measured** for Risk and Rooftops (4.2).

## 5. What the measurements change in the plan's order

The plan (`reports/cpp-modernisation-plan-2026-11-16/plan.md`, section 4): S, then F1 and F2, R (N1, N3, N2, N4, K7-a), FJ, K, 5a, then the C-stages, 5c, and 5b last. The data calls for these changes:

1. **Add a stage "projector pool per-frame cost" before FJ.** It is the largest unclaimed main-thread item: 1.8 ms (7 %) at Risk and 6.4 ms (18 %) at Rooftops, of which 1.4 and 5.4 ms are unzoned self time. The first step is cheap and in the repo's style: add zones inside `LLProjectorShadows::update` (pose read, dirty feed, layout, plan) and take one Rooftops capture; then remove or hoist the per-lamp work, which should not cost anything when nothing moves. It also bears directly on the owner's fps floor.
2. **R shrinks.** N4 (measured 0.03 to 0.05 ms/frame against 0.1 to 1.5) and K7-a (0.003 to 0.04 ms/frame) are dropped; N8 (async sound load) is not justified. N1 and N3 stay only as arrival-hitch fixes (the 17.7 ms `notifyMeshLoaded` call; 0.6 ms/frame while meshes load) and should not hold FJ back. R's steady-state gain is under 0.1 ms/frame.
3. **FJ's first user is the cascade cull + sort**, as the plan's order (cascades, world, tiles) says, and the data backs it: 4.3 to 4.6 ms (13 to 17 % of the frame) against 1.3 to 1.8 ms for the world view and 0.1 to 0.3 ms for the projector tiles. **The tiles can wait** until the pool update is understood; **5b (plan/fill/flush) is not justified by steady-state fps** (fill 0.1 to 0.3 ms; it matters only while a scene loads).
4. **The serial GL submission of the shadow passes (5.1 ms at Risk, 10.4 ms at Rooftops) is as large as, or larger than, everything FJ can take.** It belongs to the GL programme (fewer or cheaper shadow draws, reuse of cascade results across frames), not to threads. FJ's best case is a 1.2 to 1.3 times speed-up; the shadow submission and the GPU decide the rest.
5. **New FJ or R candidate: the texture list's pixel-area pass** (`updateImageDecodePriority`, 1.7 to 1.96 ms in every crowd; the parallel candidate is the whole decode-priority pass, of which about 1.6 ms is `calcPixelArea` work, mostly of rigged faces). `calcPixelArea` totals 1.7 to 1.96 ms, but 0.3 to 0.46 ms of that sits under `LLVOVolume::updateTextures` on the idle path (`getTextureVirtualSize`), so the candidate is about 1.6 ms and a per-frame cache must cover both callers. The Amdahl table uses `updateImages` (2.06 ms), not `calcPixelArea`, so nothing is counted twice. It is as big as all the culls, data-parallel and probably cacheable per frame; the plan names the priority loop (C8) but not this cost. It should join FJ's first users.
6. **Avatar animation (`updateCharacter`, 2.4 to 2.8 ms) is a bigger block than the culls** and comes next; K4 (palette SIMD) is worth at most 0.27 ms of the 0.8 ms of palette work, so it stays low, and K7 goes (see 2). **C-msg is not justified by averages** (`idle network` 0.15 to 0.23 ms, message handling 0.10 to 0.17 ms); one 482 ms `idle network` call in the Isle of Repose capture (the 499 ms frame) is a hitch to find, not an average cost.
7. **A GPU-time capture comes before the gain gates of FJ.** Rooftops and Isle of Repose wait in `Swap`; the exports hold no GPU zones. Without GPU time a Rooftops fps gate may be unreachable by any CPU change.
8. **Re-run the noise gate on settled scenes.** The scripted `MC1-still` is not a gate scene (taken during the mesh settle). Gates on the 1 % low use the median of three captures.
9. **The teleport settle is the biggest owner-visible cost in the data** (Rooftops 105 to 113 s with the fps down to 10; Isle of Repose 23 s). It is not an FJ item; the mesh-side work (N1, N3, N2, C-mesh) is the relevant part, and the dip needs per-frame data (a teleport capture) before a stage is chosen.

## 6. What was not measured

- **GPU time.** The exports carry CPU zones only; whether Rooftops and Isle of Repose are GPU- or present-limited is inferred from the `Swap` wait.
- **Moving-camera captures at Rooftops** (and at Isle of Repose). Only still captures exist there; the moving floor of 25 fps is not checked at the stress case.
- **A real teleport series.** The scripted series failed in two sessions (`MC1-teleports.tracy` is empty); three manual teleports are in the logs, none under a profiler, and a teleport's dip is seen only at one-second resolution.
- **Texture-fetch quiet.** The viewer logs no signal for it.
- **The no-profiler fps of the scripted session**, and Risk at the default preset without Tracy.
- **N2** (lock contention on the repo thread), **N3** (no zone of its own) and the 5.4 ms unzoned part of `projector update`.
- **The profiler's cost per block.** Only the per-frame total is known; the 17 ns a zone is an average.
- **Avatar counts.** Skinned draws are counted (about 370 and 400 a frame); avatars are not (the rez log names about 57 distinct avatars in the first minute at Rooftops).
- **Mirror and hero probe:** no zone fired in any capture.
