<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Multi-core stage 1: baseline report (skeleton)

Status: **skeleton, tables empty.** They are filled after the owner session (below). Study: draft PR #517
(`doc/perf/multicore-simd-study.md` on `codex/multicore-simd-study-2026-11-15`; sections 1, 3.2, 3.6, 5.2 K4, 7.1 row 1,
7.2, 10.2). The owner accepted D1 to D5 as recommended on 2026-11-15; D1 means the session build also carries the particle
stage-0 probe (`codex/particles-s0-probe`, never merged).

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
   each), the teleports (5 within the region, 5 between the venue and the login region, plus one "return" teleport after each odd group, so 12 in all; one capture `MC1-teleports` of 360 s, each teleport timed in
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
tracy-csvexport.exe -u -f "df MainLoop" X.tracy > X-frames.csv       # every frame: main-thread ms, hitches, p99
tracy-csvexport.exe -m X.tracy > X-messages.csv                      # messages, if any
```

Commands that redirect output (`>`) run in Git Bash, cmd or PowerShell 7 (PowerShell 5.1 writes UTF-16).

From the files:

- main-thread ms per frame: `df MainLoop` mean; its split into the study 3.2 rows from `X.csv` and `X-self.csv`;
- average fps, 1 % low (p99 frame time) and hitches (frames over 2x the median) from `X-frames.csv`;
- the counters from the `FrameCounters:` log lines in the window of the capture (select by timestamp);
- settle time after each teleport: from Tracy, the time from the teleport until `createObjects` and `updateGeom` fall back under their steady-state mean (the log has no settle line; `SceneLoadingMonitorEnabled` is off by default);
- noise: `MC1-moving` against `MC1-moving-2`, the study's gate is under 3 %.

## 4. Tables (to fill)

### 4.1 Main-thread frame, measured against the study 3.2 estimates

| Part | Zone | Study estimate | MC1-still ms | MC1-moving ms | MC1-moving-2 ms | Share of `df MainLoop` |
|---|---|---|---|---|---|---|
| idle | `idle` | 30 to 45 % | | | | |
| network | `idle network` | 3 to 10 % | | | | |
| object updates | `LLViewerObjectList::update` | | | | | |
| avatars | `LLVOAvatar::idleUpdate` (`updateCharacter` is a sub-zone of it: do not add the two) | 10 to 25 % | | | | |
| regions | `FTM_REGION_UPDATE` | 1 to 5 % | | | | |
| moves | `world update` | 1 to 5 % | | | | |
| particles | `update particles` | 1 to 10 % | | | | |
| apparent angles | `FTM_LOD_UPDATE` | 1 to 3 % | | | | |
| render preparation | `Render` minus the GL parts | 35 to 50 % | | | | |
| geometry create and rebuild | `createObjects`, `updateGeom`, `rebuildPriorityGroups` | 3 to 15 % | | | | |
| world cull | `updateCull` under the display (world) parent zone | 4 to 8 % | | | | |
| sun shadows | `generateSunShadow` | 10 to 20 % | | | | |
| projector pool | `projector update` | at most 0.35 ms | | | | |
| texture list | `updateImages` | 3 to 10 % | | | | |
| world state sort | `LLPipeline::stateSort` under the world parent zone (the inner per-drawable `stateSort` zone is separate) | 8 to 15 % | | | | |
| reflection probe | reflection manager update, `Render Cube Face` | 3 to 8 % | | | | |
| mirror | hero probe update | 0 to 20 % | | | | |
| GL submission | `display - 5` and below | 15 to 30 % | | | | |
| UI | `HUD`, `UI 3D`, `UI 2D` | 3 to 8 % | | | | |

### 4.2 Ranked main-thread zones (top 20 by self time, `MC1-moving`)

| Rank | Zone | Self ms/frame | Total ms/frame | Calls/frame |
|---|---|---|---|---|
| | | | | |

### 4.3 Noise between the two moving captures

| Metric | MC1-moving | MC1-moving-2 | Difference |
|---|---|---|---|
| `df MainLoop` mean ms | | | |
| p99 frame ms | | | |
| fps | | | |

### 4.4 Frame counters (from the `FrameCounters:` log lines, per frame)

| Counter | MC1-still | MC1-moving | Note |
|---|---|---|---|
| `skin_palette_calls` | | | K4: the call count; G3 cuts uploads first |
| `skin_palette_joints` | | | |
| `cull_passes` | | | minus projector_tiles and projector_avatar_layers: 9 expected without a mirror |
| `state_sort_passes` | | | |
| `projector_tiles` | | | |
| `projector_avatar_layers` | | | |

### 4.5 Teleports

| # | Kind | Wall clock | Settle time (s) |
|---|---|---|---|
| 1 to 5 | within the region | | |
| 6 to 10 | to a second region and back | | |

### 4.6 fps without the profiler

| Capture | fps (Statistics bar, 60 s, dolly) | fps with Tracy connected (`MC1-moving`) |
|---|---|---|
| | | |

### 4.7 Stage 1 gate (study 7.1)

Run-to-run noise under 3 % on S1: **not measured**. Ranked main-thread zones: **not measured**. Study 3.2 table measured:
**not measured**.
