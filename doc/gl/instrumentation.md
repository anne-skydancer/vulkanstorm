<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# OpenGL submission instrumentation (stage G0)

Development tooling for the OpenGL modernisation (stage G0 of the study). It measures what the renderer asks
of the GL driver, per view and pass, and it checks that a changed submission path draws the same image. Every later stage
(G1 to G7) uses it twice: the counters and timers show whether the stage paid, and the identical-image check is its gate.

Phase 2b's step 1 (the gates and counters, audit C10 and C11) added: compares 13 to 15 for a probe face, the HUD and an
impostor (G4-0b with b-28's corrections), a statistics frame that also covers the reflection probe update (G5c-0 (a)), the
`impostor` and `hero face` views (28-0), the run tracker's missing breakers, texture-matrix loads, draw-state keys and the
runs per-draw records would leave (G5c-0 (b, c), G6r-0), and the copy census of instancing and vertex pulling (X1-0 with
G5b-0's counters and N-SF1's run lengths). See "The copy census" and "Compares 13-15" below.

Nothing here changes a pixel and nothing here is part of a release. With the option off there is no behavioural change
except five inert debug settings (nothing reads them).

- **Build option:** `-DVKS_GL_STATS=ON` (off by default, off in CI, off on release). Off, every hook expands to nothing and the
  objects are the ones the code produced before. On, but not collecting, a hook is one pointer test.
- **Where it lives:** `vkstorm-devel` and the feature branches built from it. The names (`VKS_GL_STATS`, `VKS_GLSTAT_*`,
  `VKS_GLCMP_*`, `LLGLSubmit*`, `RenderGLSubmit*`, `llglsubmitstats*`, and for the projector shadow pool `ProjectorShadowStats*`, `llprojectorshadowstatsapi*`) are in the forbidden list of
  `scripts/tests/check_release_hooks.py`, which runs on every PR except those into `vkstorm-devel` and `codex/` branches.
  An integration into release leaves these edits out: the `VKS_GLSTAT_*` / `VKS_GLCMP_*` lines, the two
  `llglsubmitstats*` and `llglsubmitstatsapi*` file pairs (they are added to the CMake source lists only when the option
  is on), the five debug settings, the CMake option, `indra/llrender/tests/llglsubmitimage_test.cpp` and the ci-checks step that
  runs it, and the `check_release_hooks.py` names. The `check-pr.yaml` condition may stay. The directional shadows' dark-gate
  readout (`lldirectionalshadowreadout.*`, always compiled on devel) is another devel-only hook: an integration also deletes its
  include and its `DirectionalShadowReadout::note` block in `pipeline.cpp`, and its `.cpp` and `.h` CMake lines.
- **No Preferences entries.** Five debug settings, none persisted (see "How to read it").

## What is measured

Counters are per frame, per **view** (`main`, `sun cascade 0..3`, `spot shadow 0..1`, `projector tiles` for the whole
update of the Stochastic method's projector pool (since multicore stage 1b; before, it was counted into `frame`),
`probe face` (a reflection probe's face) and `hero face` (a mirror's), `impostor` (an avatar impostor's regeneration, its
sort and its renders), `avatar preview` and `avatar profile` (the appearance preview and the performance floaters' profile
renders of `generateImpostor`), `hud`, `frame` for everything else, and `probe face > sun cascade 0` for a view inside a
view) and per **pass** (a path of scopes inside the view:
`gbuffer`, `geom deferred/SIMPLE 0`, `shadow opaque 1`, `deferred lighting/geom post-deferred/ALPHA 0`, `update geom`, ...).
The reading of the sun's shadow maps is two passes under `deferred lighting`: `sun program` (the lookup) and, with the Stochastic
method's temporal resolve, `shadow stochastic resolve` (since multicore stage 1b).

**Counters are exclusive** (attributed to the innermost scope); **CPU ms and GPU ms are inclusive** of the scopes inside
them, so sums of times over a view's passes are not the view's time (the view's own row is).

**The frame.** The main loop's statistics frame begins in `display()` (as before: frames that do not render are not counted)
and ends after the reflection probe update that follows it (`llappviewer.cpp`), so the probe face and its two sun cascades
are in every window (b-8 F2). The snapshot floaters' `display()` calls come after that and get frames of their own. Windows
from before this change (mc1b) lack the probe update: their frame times and totals are smaller by it.

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
| `vbFormatChanges` | `setBuffer` of a buffer whose vertex streams (type mask) differ from the previous buffer's: `setupVertexBuffer` re-points every attribute, and a G5 page holds one format, so it ends a run |
| `texUnbinds` | `LLTexUnit::unbind` and `unbindFast` calls that rebound the unit (a texture change that ends a run) |
| `texMatrixLoads` | texture matrices loaded (an animated texture's matrix; it reaches GL as a uniform, so `uniformMatrixGL` hid it) |
| `runs`, `mergeableDraws`, `run_hist` | the **mergeable runs**: a draw continues the previous draw's run when nothing separates them but the vertex buffer: no program change, no texture bind or unbind (a redundant bind does not count), no uniform that reached GL (the model matrix included), no `LLGLState` toggle, no change of vertex format. `mergeableDraws = draws - runs`; `run_hist` counts runs of 1, 2, 3-4, 5-8, 9-16, 17+ draws. It is the upper bound of what one `glMultiDrawElements*` per run (G5c) could merge over the arenas of G5. Format changes and unbinds end runs since phase 2b's step 1 (b-8 F3): `runs` of earlier windows are lower bounds of today's |
| `runsNoUniform`, `mergeableDrawsNoUniform`, `run_hist_no_uniform` | the same runs if uniforms, skin palettes and model matrices came from per-draw records (G6r-0): only program, texture, cap and format changes end them. `runsNoUniform <= runs` in every scope; the ratio decides G6r-c and any reopening of G7 (b-10) |
| `stateKeysDistinct` | census windows only: distinct draw-state keys in the scope, the buckets a by-state sort would form (b-8's bucket count, for the record). A key is the program, the textures on its channels, the modelview and projection, the last palette and the `LLGLState` caps toggled since the frame began; other uniform values (alpha cutoff, material factors) are not in it, so it is a lower bound |

A frame in which the identical-image check runs renders its passes twice, so its counts and times are about doubled: **do not
collect a window across it** (`perf_check.py` runs the check after the window).

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
| `windowBegin {frames, gpu, census}` / `windowStop` | collect N frames, averaged per frame; poll `status` (`window_active`, `window_gpu_frames`, `window_census`). `census` true also counts the copy census and the draw-state keys (a window of its own: they cost about a hash and a set insertion per draw and per face) |
| `summary {kind}` | headline totals and per-view draws and times of the window (`window`) or the last frame (`frame`) |
| `dump {kind, path}` | the full JSON (`totals`, `views`, `passes`, run histograms, compare results) to `path`: an absolute `.json` path under the viewer's log folder or a `.captures` folder, no `..`, no UNC paths (a LEAP client cannot truncate other files) |
| `compareArm {pass, inject, frames}` / `compareResults` | arm the check for the next frame, or with `frames` (1-600) for up to that many frames until every pass asked for has run (an impostor regenerates now and then); read its results |
| `census {path}` | the resident copy census of the scene to `path` (rules as `dump`); the reply has its headline (faces, the instancing ratio) |
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

The check covers the **main view** and three second submitters, each pass once per arming. Passes: 1 G-buffer (all colour
attachments and depth), 2-5 the four sun cascades, 6-7 the spot shadows (depth), 8 the post-deferred geometry (alpha, glow,
fullbright over the lit scene; sorted alpha only), 13 a probe face, 14 the HUD, 15 an impostor (see "Compares 13-15"); 9-12
are reserved for G0b. A probe face reaches the sites of 1-8 with other targets, so those compare the main view only. While the pass is rendered the second
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
- Post-deferred, HUD and impostor renders whose tail cannot repeat (see "Compares 13-15"); reported as not run.
- Passes that are not drawn in the frame (no spot light, no cascades with shadows off) report "did not run".
- GL state that a replay leaves different (it should not) would show as a difference: that is a harness bug and the first
  thing to rule out when a pass reports DIFFERENT with both renders on today's path.

## Compares 13-15: the probe face, the HUD and an impostor

G4-0b with b-28's three corrections (`reports/gl-modernisation-2026-11-17/phase2/b-28-second-submitters.md` §4.3). The
draw-list stages change the recipes these views share with the main view, and the arenas and instancing change their
geometry, so each gets its own compare.

| Id | View | Compared | The replay |
|---|---|---|---|
| 13 | the first probe face rendered while armed: a reflection probe's (`probe face`, in the reflection probe update, now inside the frame) or a mirror's (`hero face`, inside `display()`) | the face's G-buffer (`mRT->deferredScreen`: the auxiliary set, or the hero set) after `renderGeomDeferred` in `display_cube_face` | clears the target again with the colour mask on (the first render left alpha writes off); occlusion is off in cube snapshots and the probe buffers are re-uploaded with the same data |
| 14 | the HUD's geometry | the **back buffer** (colour and depth) after `renderGeomPostDeferred` in `render_hud_attachments`: read with `glReadPixels`, the inputs put back by a blit from a copy in textures of the back buffer's own formats (8 bit or 10 bit RGB, 16-bit float; depth 16, 24 or 32F, with or without stencil). A format the copy cannot match, or a blit the driver refuses, reports not run and the HUD is drawn once | the back buffer as the first render found it |
| 15 | the first impostor regenerated while armed (not previews or profiles) | `avatar->mImpostor` after the whole block of `generateImpostor`: the clear, the G-buffer render and both post-deferred renders | the clear colour and the colour mask as the block found them (the depth-only render left colour writes off), and `sImpostorRenderAlphaDepthPass` as the block sets it |

**The tail rule** (b-28 F9). `renderGeomPostDeferred` ends with `renderHighlights` and `renderDebug` in every non-probe view: they
clear the highlight faces, rebuild and redraw selected faces (a rebuild frees draw items the second render would read) and draw
debug displays and update blips that age. Compares 8, 14 and 15 report **not run** while highlight faces are pending, faces are
selected in the edit tools' face mode, a debug display (`RenderDebugMask`) is on, or object update blips are shown
(`LLGLSubmitCompare::tailNotRepeatable`).
The pathfinding console's world drawing is not detected: keep it closed in a compare session.

**Impostors until H28.** On devel before the impostor hotfix (H28: each item's full material, blended parts sorted and blended
once) a PBR-dressed avatar's impostor reads texture units and factors left by whatever ran before, and the second render starts
from the first one's end state: compare 15 can report DIFFERENT with no code change. Read 15 on avatars without PBR items until
H28 is on devel; afterwards on all, and the compare also proves H28.

**Arming.** A probe face is rendered in every frame with reflection probes on; the HUD needs a worn HUD; an impostor is
regenerated only now and then. `compareArm {frames: N}` keeps the arming open for up to N frames, each pass compared once in
the first frame that renders it; `perf_check.py` arms for the scenario's `compare.frames`. The light fades of a replayed
post-deferred render follow the first render's log, as for 8; a light that finishes fading out between the two renders of an
impostor would leave the set and show as a difference (rare, as for 8).

## The copy census (G5b-0, X1-0, N-SF1)

Counts what instancing (one object-space copy per repeated face, a record per placement) and vertex pulling (smaller copies)
could take over from the bake, in the viewer's real draw order, before either is built. Nothing here draws or changes a pixel.

**Recorded at registerFace** (every group rebuild, development builds, main thread): for each face that joins a draw item, in
draw order, a record (`LLDrawInfo::mGLSubmitCensus`): its **geometry cache key** (`LLGLSubmitMath::censusKey`: the shared
volume and face, the vertex and index counts, the buffer's streams, the texture transform, colour, glow and the material's
hash, which carries the normal, specular and glTF texture transforms; a rigged face adds its skin and texture index), the
**first rule that keeps it baked** (`censusExclusion`, b-11 §3.3: no key, HUD, flexible, glTF asset, rigged, blended pass,
invisiprim, texture animation, planar mapping, media, legacy bump without a normal map, selected, a transform entry outside the
exactness guard), its vertices, its bytes and its last bake time (`getGeometryVolume`, timed around the call). The guard is
checked on the position and normal matrices; the vertices are not (a nonzero value under 2^-50 in a mesh is not expected). A
debug display that reads face geometry is not a rule here (no census session has one on). The cost is a hash and a vector
entry per face per pass at every rebuild, in development builds only: the key, the exclusion and the guard take 56-59 ns per
face in a standalone bench of these functions (Ryzen 9 9950X3D, MinGW g++ -O2, a viewer build running beside it), the viewer's
getters, the bake timer (two clock reads per face) and the vector entry come on top. Nothing per draw unless a census window
runs; in one, the crediting takes 15-17 ns per drawn face in the same bench, plus the draw-state key (a hash of the program,
the textures and two matrices, and a set insertion) per draw.

**Credited at the draw** (census windows only): the batch recipes (`pushBatch`, `pushUntexturedBatch`, the two glTF ones,
`pushBumpBatch`, the bump group draw, the materials pool, the alpha pool's draws and emissives) note each draw item. Per view and
pass, `census` holds:

| Field | Counts |
|---|---|
| `faces`, `vertices` | faces drawn (a face drawn by several draws counts at each) |
| `by_reason` | faces and vertices by the rule that keeps them baked; `eligible` is the rest |
| `eligible_by_size` | eligible faces by size class (`under_64`, `64_255`, `256_575`, `576_up`): faces, vertices, the bytes they hold, their bake ms |
| `items`, `instanced_items`, `mixed_items`, `sets` | draw items; those with an instanced face (eligible, 64 vertices or more, the floor of research `size-floor.md`); those with a baked face too; instanced sets (runs of one key inside items: one command each) |
| `p0_extra_calls`, `p0_switches` | policy P0 (per face): calls over today's one per item, and baked/instanced changes in list order (program and record switches) |
| `p1_lost_vertices` | policy P1 (an item is instanced only when all its faces are): the instanced vertices of mixed items, which stay baked |
| `run_hist` | strict order (N-SF1): runs of one key in list order among eligible faces, by size class, of 1, 2, 3-4, 5-8, 9-16, 17-32, 33-64, 65+ copies. Runs over 16 copies under 576 vertices are the replica rule's trigger |
| `relaxed_hist` | relaxed order, for the record (strict order is the rule everywhere, audit C7): copies of one key per scope and frame, same buckets |

b-11's decision rule reads `p0_extra_calls` (P0 under 200 extra calls a frame at Rooftops, else P1); N-SF1's reads `run_hist`.

**The resident census** (`GLSubmitStats.census`, `census_resident_<run>.json`): every face of every draw item the scene holds
now (volume, bridge, avatar and animesh partitions), each face once (it is in a draw item per pass it is drawn in; it can become
a copy only when every pass allows it, so the first reason seen keeps it baked): faces, vertices, bytes and bake ms by reason;
eligible faces by size class with **baked bytes** (every copy) against **unique bytes** (one per key), the bake ms of the copies
past each key's first (what a cache hit skips) and keys by copies; the instancing ratio (64 vertices and more), the measured
counterpart of the census's 4.98x; and rigged sharing (rigged, non-blended faces: total against unique bytes, G5b-1).
`items_without_record` counts draw items built before the viewer recorded censuses (none after a fresh start).

**Not in this census:** clusters (X1-0 (b): triangles in clusterable faces and the share fixed-128 clusters would cull) and
particles (X1-0 (c), with particles stage 0).

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

## The census session (phase 2b, step 1)

One owner session per place, on a devel build with `-DVKS_GL_STATS=ON` (RelWithDebInfo, staged): Rooftops in a crowd, then home
or a dense-mesh place. The scenario is `scripts/inworld/scenarios/gl_census.json`, launched like `gl_submit_g0.json` (see
`scripts/inworld/README.md`):

```powershell
$run = "$py $scripts/perf_check.py --scenario $scripts/scenarios/gl_census.json --out C:/Users/annes/source/repos/vulkanstorm/.captures/glcensus-$stamp"
& $exe --leap $run --leap $noop
```

Log in, wear a HUD, stand where the scene is worth measuring (in the crowd; near a mirror for the `mirrors` run), leave the viewer
alone; about 10 minutes. It proves compares 1, 13, 14 and 15 with one flipped bit, then measures `still` (sun shadows, all
shadows, few non-impostors, mirrors on) and `moving` (turning at 10 deg/s; sun, all): per run a timed window, every compare armed
for up to 120 frames, a census window and the resident census.

What it settles: the probe face's and its cascades' cost in the window (b-8 F2); `runsNoUniform` against `runs` per pass (G6r-c,
G7); the bucket count (G5c); the `impostor` and `hero face` views' draws (28-0's gate); compares 13-15 IDENTICAL with both renders
on path 0 and the injection caught in each (G4-0b's gate); the eligible share of repeated content and the instancing ratio
(D5, against 4.98x); P0's extra calls against P1's lost vertices (the policy); the run lengths of small templates (N-SF1's
replicas); small repeated faces' share of baked bytes and bake ms (X1-P2's trigger: 10 % of baked vertex bytes or 20 ms of bake
per teleport); rigged sharing in a crowd (G5b-1).

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

Phase 2b's step 1 was built and its pure parts tested (`llglsubmitimage_test.cpp`: the epochs of `runsNoUniform`, the census's
size classes, run buckets, exclusion order, exactness guard, key, item policies and template runs; `test_perf_check.py`: the
census and proof flow against the fake viewer). Not run in the viewer: the compares 13-15 on real targets (whether the
back-buffer blits are exact on the owner's GPU, and whether a probe face, the HUD and an impostor replay IDENTICAL), the main
loop frame with the probe update, the census on a real scene and its cost. The census session answers them.

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
