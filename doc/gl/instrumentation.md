<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# OpenGL submission instrumentation (stage G0)

Development tooling for the OpenGL modernisation (stage G0 of the study). It measures what the renderer asks
of the GL driver, per view and pass, and it checks that a changed submission path draws the same image. Every later stage
(G1 to G7) uses it twice: the counters and timers show whether the stage paid, and the identical-image check is its gate.

Nothing here changes a pixel and nothing here is part of a release. With the option off there is no behavioural change
except five inert debug settings (nothing reads them).

- **Build option:** `-DVKS_GL_STATS=ON` (off by default, off in CI, off on release). Off, every hook expands to nothing and the
  objects are the ones the code produced before. On, but not collecting, a hook is one pointer test.
- **Where it lives:** `vkstorm-devel` and the feature branches built from it. The names (`VKS_GL_STATS`, `VKS_GLSTAT_*`,
  `VKS_GLCMP_*`, `LLGLSubmit*`, `RenderGLSubmit*`, `llglsubmitstats*`) are in the forbidden list of
  `scripts/tests/check_release_hooks.py`, which runs on every PR except those into `vkstorm-devel` and `codex/` branches.
  An integration into release leaves these edits out: the `VKS_GLSTAT_*` / `VKS_GLCMP_*` lines, the two
  `llglsubmitstats*` and `llglsubmitstatsapi*` file pairs (they are added to the CMake source lists only when the option
  is on), the five debug settings, the CMake option, `indra/llrender/tests/llglsubmitimage_test.cpp` and the ci-checks step that
  runs it, and the `check_release_hooks.py` names. The `check-pr.yaml` condition may stay.
- **No Preferences entries.** Five debug settings, none persisted (see "How to read it").

## What is measured

Counters are per frame, per **view** (`main`, `sun cascade 0..3`, `spot shadow 0..1`, `probe face`, `hud`, `frame` for
everything else, and `probe face > sun cascade 0` for a view inside a view) and per **pass** (a path of scopes inside the view:
`gbuffer`, `geom deferred/SIMPLE 0`, `shadow opaque 1`, `deferred lighting/geom post-deferred/ALPHA 0`, `update geom`, ...).

**Counters are exclusive** (attributed to the innermost scope); **CPU ms and GPU ms are inclusive** of the scopes inside
them, so sums of times over a view's passes are not the view's time (the view's own row is).

| Counter | Counts |
|---|---|
| `draws`, `drawIndices`, `drawTriangles`, `drawArraysCalls` | `glDrawRangeElements` / `glDrawArrays` calls from `LLVertexBuffer`, the indices they submit |
| `vbSetBuffer`, `vbChanges`, `iboBinds` | `setBuffer` calls; those that rebound the array buffer; element buffer binds |
| `attribRespecs`, `attribPointers` | full attribute respecifications (`setupVertexBuffer`) and the `glVertexAttrib(I)Pointer` calls in them |
| `vbosDistinct` | distinct array buffers set in the scope |
| `shaderBinds`, `attribToggles` | program changes; `glEnable/DisableVertexAttribArray` |
| `texBindFast`, `texBindFastRedundant` | `LLTexUnit::bindFast` calls (it always issues `glActiveTexture` + `glBindTexture`); those whose texture was already on the unit. This is the number that decides G2 |
| `texBindOther`, `texBindOtherFiltered`, `texGLBind`, `texGLActive` | `LLTexUnit::bind(LLTexture*)`, `bind(LLImageGL*)` and `bindManual` calls that reached GL, those the unit's cache skipped; `texGLBind` is the `glBindTexture` calls of `bindFast` and of those three paths together (not the cube map binds, `unbind`, or a `glBindTexture` made outside `LLTexUnit`); `texGLActive` the `glActiveTexture` calls of `bindFast` and `activate` |
| `uniformRequests`, `uniformGL`, `uniformArrayGL`, `uniformMatrixGL`, `uniformBytes` | `LLGLSLShader::uniform*` calls; `glUniform*` calls that reached GL (the rest were filtered by the `mValue` cache); of those the array calls, the matrix calls (palettes included), the bytes |
| `matrixSyncs`, `mdvSyncs`, `projSyncs`, `inverseMatrices` | `syncMatrices` calls with a shader bound; its modelview and projection branches; `glm::inverse` evaluations in it |
| `modelMatrixCalls`, `modelMatrixChanges` | `applyModelMatrix` calls and those that changed the matrix |
| `paletteUploads`, `paletteBytes`, `paletteDistinct` | skin palette uploads, their bytes, distinct (avatar, skin) pairs among them. `paletteUploads / paletteDistinct` is how many times a palette is sent that G3 would send once |
| `bufAllocHit`, `bufAllocMiss`, `bufAllocMissBytes`, `bufSubData`, `bufSubDataBytes`, `flushBuffers` | VBO pool hits and misses (a miss is a `glBufferData`), `glBufferSubData` calls and bytes, `flushBuffers` calls |
| `stateToggles` | `glEnable/glDisable` that reached GL through `LLGLState` |
| `occIssued`, `occPolled`, `occBlocking` | occlusion queries issued, polled, and read with a **blocking** `GL_QUERY_RESULT` (the timeout path: the only stall source in occlusion) |
| `gltfBinds`, `gltfSameAsLast` | `LLFetchedGLTFMaterial::bind` calls and those for the same material, media override, shader and shadow state as the call before (G1) |
| `runs`, `mergeableDraws`, `run_hist` | the **mergeable runs**: a draw continues the previous draw's run when nothing separates them but the vertex buffer: no program change, no texture change (a redundant bind does not count), no uniform that reached GL (the model matrix included), no `LLGLState` toggle. `mergeableDraws = draws - runs`; `run_hist` counts runs of 1, 2, 3-4, 5-8, 9-16, 17+ draws. It is the upper bound of what one `glMultiDrawElements*` per run (G6, G7) could merge over the arenas of G5 |

A frame in which the identical-image check runs renders its passes twice, so its counts and times are about doubled: **do not
collect a window across it** (`perf_check.py` runs the check after the window). Probe faces are never compared.

**GPU time** comes from `glQueryCounter(GL_TIMESTAMP)` pairs around every scope, in a ring of four frames, read without
waiting (the result of frame N arrives when frame N+1..N+3 starts). Up to 4096 scopes per frame are timed; more are counted
without GPU time (`gpu_truncated`). Timestamp queries are independent of the `GL_TIME_ELAPSED` query of Frame Profile (both can be active at once).

**Alpha samples.** Around the pools `ALPHA`, `ALPHA_PRE_WATER` and `ALPHA_POST_WATER` (not nested: one `GL_SAMPLES_PASSED` query
at a time, core since OpenGL 1.5, so also on GL 3.3 hardware) the pass records `samples`: the samples that passed the depth and stencil tests
(samples, not fragments: on a multisampled target one fragment counts several times). The window totals carry `alpha_samples`, the sum over all
views and passes that were measured (`analyze_perf_check.py` uses the main view's passes). With sorted alpha it is a hardware-independent
measure of overdraw; stochastic transparency draws other passes into multisample targets, so its samples are not comparable to sorted alpha's.
The query runs only in frames with GPU timers on, and not while Frame Profile or per-shader profiling is on (`LLGLSLShader::sProfileEnabled`: they hold the
`GL_SAMPLES_PASSED` target themselves). An active occlusion query can change how the GPU applies early-Z, so the frame rate with nothing collecting
(`fps_stats_off`) is the clean figure.

**CPU time** is wall time on the main thread between the scope's entry and exit (`steady_clock`), not CPU time of the thread.

Only the main thread counts (a hook on another thread, e.g. a worker with a shared context, is ignored).

## How to read it

### Debug settings (none persisted)

| Setting | Meaning |
|---|---|
| `RenderGLSubmitStats` (off) | collect every frame |
| `RenderGLSubmitStatsGPU` (on) | with it, the GPU timers |
| `RenderGLSubmitCompare` (0) | for **one** frame: the identical-image check on a pass, -1 on all; returns to 0 |
| `RenderGLSubmitCompareInject` (0) | nonzero: flips one bit of the second readback (the proof that the check can see) |
| `RenderGLSubmitPath` (0) | the submission path; 0 is today's. Stages register candidates under other values |

### Frame Profile

Develop > Render > Frame Profile (or `GLSubmitStats.profileFrame`) writes the usual `profile.v*.json` with one more section,
`submit`: the whole frame (`totals`, `views`, `passes`, GPU time resolved by waiting, since the profile frame stalls anyway) and
the compare results. Per-shader `samples` and `triangles` of two profiles of the same frame must be equal for an
image-identical stage; `scripts/perf/profile_cmp.py` compares two profiles.

### LEAP: `GLSubmitStats`

Replies are small on purpose (see `scripts/inworld/README.md`, "Big replies"); the full data goes to a file.

| Operation | Does |
|---|---|
| `status` | collection and compare state, `frame_count` |
| `windowBegin {frames, gpu}` / `windowStop` | collect N frames, averaged per frame; poll `status` (`window_active`, `window_gpu_frames`) |
| `summary {kind}` | headline totals and per-view draws and times of the window (`window`) or the last frame (`frame`) |
| `dump {kind, path}` | the full JSON (`totals`, `views`, `passes`, run histograms, compare results) to `path`: an absolute `.json` path under the viewer's log folder or a `.captures` folder, no `..`, no UNC paths (a LEAP client cannot truncate other files) |
| `compareArm {pass, inject}` / `compareResults` | arm the check for the next frame; read its results |
| `profileFrame` | request a Frame Profile; the reply names the log folder |
| `selfBench {iterations}` | the cost of the hooks in this binary (see "Overhead") |

`scripts/inworld/perf_check.py` drives all of it (scenario `scenarios/gl_submit_g0.json`); `analyze_perf_check.py` turns the
run folder into `report.txt`, and puts one run against another (`--compare`).

## The identical-image check

The question every stage must answer is "does the new path draw the same image". Two runs of the viewer are not comparable
(animation, streaming, water and particles move), so the check renders **the same pass twice in one frame** with the same
inputs and compares the attachments **bit for bit**:

1. `RenderGLSubmitCompare` (or `compareArm`) arms the next frame.
2. The pass runs. Its attachments are read back (`glGetTexImage`: 8 bit RGBA as bytes, anything wider as float RGBA, depth
   as float; lossless for the formats the renderer uses).
3. The pass runs again with `activePath()` returning `RenderGLSubmitPath` (the first run used 0, today's path); for a pass that
   draws over existing content the target is restored first.
4. The attachments are read again and compared (`LLGLSubmitImage::compare`, tested in `indra/llrender/tests/`). The log
   (`GLSubmitCompare`) and `compareResults` say, per attachment, how many pixels differ, the largest difference per channel
   (8 bit: units; float: ulps) and the first pixel.

The check covers the **main view only** (reflection probe faces reach the same sites with other targets and are skipped) and each
pass once per arming. Passes: 1 G-buffer (all colour attachments and depth), 2-5 the four sun cascades, 6-7 the spot shadows (depth), 8 the
post-deferred geometry (alpha, glow, fullbright over the lit scene; sorted alpha only). While the pass is rendered the second
time `LLGLSubmitCompare::replaying()` is true: culling, sorting and occlusion queries of the pass are not repeated, so only the
submission is compared. State a pass advances while it renders is made the same for the second render: the fade of the nearby lights
(`setupHWLights` advances it on every call; the first render's values are recorded and the second uses them). Anything else a pass
consumes is a defect of the harness to find: the first owner run shows it as a DIFFERENT with both renders on today's path.

Each result carries, per attachment, the number of distinct pixel values (capped at 1,024) and a hash of the first readback: an
IDENTICAL on an attachment with one distinct value is an empty pass and says little (`perf_check.py` logs it). Memory: the first
readback of the whole target is kept, the second is compared one attachment at a time; a 4K G-buffer needs about as much
system memory again as its textures (several hundred MB) for the duration of the frame.

**Proof that the check can see.** `perf_check.py` starts with a G-buffer compare in which one bit of the second readback is
flipped, and requires DIFFERENT. A run where that is not caught fails.

**Not covered, and why.**

- The final image (`renderFinalize`): exposure and temporal AA keep history between frames, so a second render is not the
  same input. A stage that changes the final image has no per-pass equivalent here.
- Post-deferred geometry with stochastic transparency: its accumulation history changes with every render. The check reports
  the pass as not run. The sorted-alpha method is checked.
- Multisampled targets (the check reads plain 2D textures).
- Post-deferred with highlight faces pending (the pass consumes that list); reported as not run.
- Passes that are not drawn in the frame (no spot light, no cascades with shadows off) report "did not run".
- GL state that a replay leaves different (it should not) would show as a difference: that is a harness bug and the first
  thing to rule out when a pass reports DIFFERENT with both renders on today's path.

## GL context lifetime

The timer queries are GL objects. `LLViewerWindow::stopGL` calls `LLGLSubmitStats::resetGL(true)`, which deletes them while the
context is still valid and forgets the frames in flight (their GPU times are lost, `gpuResolved` false); the pool is then
rebuilt on demand in the new context. If the GPU falls more than four frames behind, the oldest frame's GPU times are dropped
without waiting; nothing in the statistics ever blocks on the GPU except Frame Profile and `selfBench`.

## Overhead

Measured in the staged RelWithDebInfo + LTO viewer (Ryzen 9 9950X3D, RTX 5070 Ti, driver 621.14) with
`GLSubmitStats.selfBench`, which times the hooks of this very binary: one "draw" is the mix of 20 hook sites a real draw
runs (buffer and attribute counters, texture binds, matrix sync, three uniform uploads, the run tracker). A compiler barrier
after each site forces the collection pointer to be reloaded at every site, as it is in the renderer where GL calls sit
between the sites (without it the 20 tests collapse into one and the figure is too good).

| | per site | per draw (20 sites) |
|---|---|---|
| compiled out (`VKS_GL_STATS` off) | 0 (the macro is empty) | 0 |
| compiled in, not collecting | 0.14-0.15 ns (a load and a predictable branch) | 2.8-3.1 ns |
| collecting | 0.50 ns | 10.0-10.3 ns |

| | per scope |
|---|---|
| a pass scope, collecting, CPU time only | 120-137 ns |
| the same with GPU timers (two `glQueryCounter`) | 161 ns |
| the first use of a query object (the driver allocates it) | 110-230 us, once per object |

The last row means one hitch when the collection starts: a frame has about 600-1,200 timestamp queries, and they are
created and then reused (a pool). Start the collection, let it run a few frames, then read the window; the first
frames are the ones with the hitch (the window of `perf_check.py` is 120 frames after the camera has settled).

For a frame of 20,000 draws in 1,000 scopes that is about 0.06 ms not collecting (under 1 % of a 10 ms frame) and
about 0.36 ms collecting (about 4 %), before cache effects. A window therefore reads slightly slow frames; compare two
runs only collecting against collecting. `perf_check.py` also measures the frame rate over 5 s with nothing collecting
before every window (`fps_stats_off`) and reports `overhead_pct` per preset: the real in-world figure.

At the login screen (70 draws, windows of 1,500 to 3,000 frames, all through the real LEAP API) the frame rate varied from 124 to
487 fps between rounds with nothing collecting, with GPU timers or without (the login page loads in the background, and a window
that starts the GPU timers pays the one-time query allocation above): the spread is larger than any difference, so the login
screen cannot resolve the overhead more finely than the figures above. The login screen frame, as counted: 70 draws, 80 vertex buffer changes, 120
uniform requests of which 39 reached GL, 39 `glBindTexture`, 58 mergeable runs (1.2 draws per run).

## Owner's capture steps

1. Build RelWithDebInfo + LTO with `-DVKS_GL_STATS=ON` from this branch (`scripts/configure_firestorm.sh ... -DVKS_GL_STATS:BOOL=ON`,
   or `autobuild configure -A 64 -c RelWithDebInfoFS_open -- --no-opensim --lto "-DVKS_GL_STATS:BOOL=ON"`), staged, no installer.
   For the baseline of the next stages, cherry-pick the instrumentation commits onto a `vkstorm-release` tip in a
   `work\gl-measure` worktree and build that the same way; it is never pushed as a PR.
2. Start it with `perf_check.py` as in `scripts/inworld/README.md` ("In-world OpenGL submission capture"), log in by hand, go to
   the scene, leave the viewer alone.
3. Scenes (one run each): S1 local OpenSim with a dense OAR and about 40 NPCs, D a dense static build region, M a mesh-heavy
   region with repeated assets (a furnished store), C a crowded venue with 30+ mesh avatars (skin palettes), T heavy transparency.
4. `analyze_perf_check.py <run>`; keep the run folders. The report decides the order of G3, G5, G6 and G7.

## What was not verified here

- **No in-world run.** No login (the task forbids it), so the compare check was never executed on a G-buffer, a cascade or the
  alpha pass of a real scene, and no counter was compared with Frame Profile's binds and triangles on a real frame. What ran:
  the whole viewer with `VKS_GL_STATS` on to the login screen, the LEAP API (`status`, `windowBegin`, `dump`, `summary`,
  `compareArm`, `compareResults`, `selfBench`), the counters, run tracker, scopes and GPU timers on the login screen's 70 draws, and
  `perf_check.py --dry-run` against that viewer. The first owner run answers three open questions: whether a pass rendered
  twice on today's path is IDENTICAL on a real scene (a DIFFERENT with both renders on the same path is a harness defect,
  e.g. a pass that is not idempotent), whether the proof step catches the injected bit, and the real overhead.
- Only NVIDIA (RTX 5070 Ti, 621.14). `glGetTexImage` of float and depth formats, timestamp queries and the cost figures are untested on AMD, Intel and Mesa.
- No Tracy (`USE_TRACY` was off in this build). The multicore study's stage-1 measurement build adds it; the two are independent.
- The release-tip measurement build (instrumentation cherry-picked onto `vkstorm-release` in `work\gl-measure`) was not made:
  the owner decides which baseline to capture; the hooks are plain `VKS_GLSTAT_*` lines and cherry-pick onto release.
- `RenderGLSubmitPath` has no consumer yet. The compare check renders its second run with it set; with 0 (today's path) both runs are the same path.
- Shutdown, login and rendering: no Release build that reaches the world was run (owner-assisted, per the repository rules), so
  nothing here is called safe for release. The hooks add no cleanup, frees or shared state to any shutdown path (the GL query
  objects of the timers are not deleted at exit; the context goes away with them) and count only on the main thread, so the
  worker threads that run `LLVertexBuffer` code are not touched. With `VKS_GL_STATS` off the edits are macros that expand to nothing.
