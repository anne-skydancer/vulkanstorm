<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Rooftops Ultra: where the shadow cost goes (six Tracy captures, 2026-11-16)

Captures: `.captures/mc1-sl-20261116-165840/`. Rooftops, 3577 m, "TT Rooftops dark", Ultra, vsync and frame cap off, about 45-50 avatars updated per frame. Build: devel 2026-11-15 + stage-1 Tracy zones + the projector change-index hotfix. All numbers are main thread, ms per frame, from `tracy-csvexport -u` per-event exports (frame zone `FTM_FRAME`), with Tracy attached. "p95" is the 95th percentile over frames. Scratch scripts and small summaries are in `.fixture-tmp/rt-ultra/` (`tab2.out`, `casc.out`); the large CSVs are deleted.

## 0. The short version

1. **The moving captures are not comparable.** The camera turned only in `RT-moving__none`. In `RT-moving__sun` and `RT-moving__all` the view was frozen (section 3). The same happened in the earlier mid-setting run. So "moving" costs for sun and all are really "still, at a lighter heading", and the none/sun/all moving comparison is invalid. Only sun vs all moving (same frozen heading) is a fair pair.
2. **The 8.1 ms (still) vs 2.1 ms (moving) projector gap is mostly an artifact.** `RT-still__all` was the first capture after the shadow setting changed: its first 15 s ran at 8-23 fps (frames of 127, 80 and 44 ms) with the sun passes at 54, 27 and 14 ms. Counted from 15 s on, still all-minus-sun is 3.6 ms, of which the projector zones are 1.25 ms plus 0.07 in the lighting pass. The real lamp cost is about 1.3-1.5 ms a frame.
3. **The sun/moon cascades cost 8.9 ms net (28 % of the frame) at the still view**, 5.4 ms at the lighter frozen heading. They are CPU on the main thread: cull + stateSort are 58 % of the cascade time, GL draw submission 42 %. The frames are mostly CPU-bound; GPU back-pressure shows up only as `Swap` stalls in 13-19 % of frames.
4. **Lamps: one tile is re-rendered every frame**, about 1.0 ms each (the 0.35 ms cap is exceeded by the "progress pick"), plus 0.6 avatar layers a frame. Something keeps tiles stale all the time. The projector update's own time is only 0.3 ms now (the 5.4 ms self time in the baseline is gone).

## 1. Frame time and fps

| | still all | still sun | still none | moving all | moving sun | moving none |
|---|---|---|---|---|---|---|
| fps (log, Tracy on) | 25.39 | 31.95 | 44.12 | 36.38 | 39.34 | 64.58 |
| `FTM_FRAME` mean (p95), ms, whole capture | 40.48 (87.8) | 31.80 (56.0) | 22.91 (46.4) | 27.95 (58.1) | 25.88 (53.4) | 15.66 (26.2) |
| frames in the capture | 1467 | 1888 | 2621 | 2151 | 2322 | 3834 |
| `FTM_FRAME` mean (p95), steady part (15-58 s) | 35.53 (65.7) | 31.90 (55.5) | 23.07 (47.4) | 27.97 (59.6) | 25.92 (53.1) | 15.32 (25.9) |
| fps, steady part | 28.1 | 31.4 | 43.3 | 35.8 | 38.6 | 65.3 |
| camera | still | still | still | frozen | frozen | **turning** |

`RT-still__all` over time (fps per 5 s bucket): 8, 12, 23, 23, 26, 29, 29, 28, 29, 29, 29, 26. Only this capture has a warm-up; the others are flat from the start.

## 2. Sun/moon cascades (sun minus none)

Zone structure (checked on single frames): `generateSunShadow` is called twice a frame. The big call has 4 cascade passes (`renderShadowImpl`, outermost); the other call has 2 tiny passes (0.4 ms in all). The projector pool's `projector update` runs **inside** `generateSunShadow`, so in the `all` captures its time is part of the whole-zone number; the table separates the two. Sun-context cull passes are 6 a frame, as in the baseline report.

Mean (p95) ms per frame:

| zone | still all | still sun | moving all (frozen) | moving sun (frozen) |
|---|---|---|---|---|
| `generateSunShadow`, whole | 15.07 (43.0) | 9.79 (17.1) | 6.95 (13.0) | 5.39 (8.0) |
| the same minus `projector update` | about 13.5 | 9.79 | about 5.6 | 5.39 |
| sun passes (6): `updateCull` | 1.71 (2.7) | 1.52 (2.1) | 0.82 (1.2) | 0.81 (1.2) |
| sun passes: `stateSort` (incl. postSort) | 6.93 (28.8) | 4.17 (10.3) | 2.54 (8.3) | 2.37 (4.0) |
| sun passes: draw `shadow simple` | 1.83 (3.3) | 1.66 (2.1) | 0.89 (1.2) | 0.88 (1.2) |
| sun passes: draw `shadow alpha` (all alpha kinds) | 2.16 (2.8) | 1.77 (2.1) | 0.93 (1.2) | 0.91 (1.2) |
| sun passes: draw `shadow geom` | 0.73 (1.2) | 0.63 (0.8) | 0.35 (0.5) | 0.39 (0.5) |
| of which avatar pool `renderShadow` | 0.62 | 0.59 | 0.32 | 0.36 |
| of which rigged batches `pushRigged*` | 0.99 | 0.89 | 0.51 | 0.50 |
| of which `updateSkinInfoMatrixPalette` | 0.33 | 0.30 | 0.27 | 0.26 |

The three "of which" rows are nested inside the draw rows and overlap each other: do not add them. Cull + sort + draw is 99-100 % of the sun-pass time, so there is no unzoned setup cost in the passes. Calls per frame in the cascades: avatar pool `renderShadow` about 510-560, `updateSkinInfoMatrixPalette` about 350.

Per cascade, still sun (ms):

| cascade | total | cull | stateSort | draw | avatar pool | rigged batches | skin palettes |
|---|---|---|---|---|---|---|---|
| 0 | 2.71 | 0.07 | 1.72 | 0.93 | 0.46 | 0.32 | 0.20 |
| 1 | 2.04 | 0.38 | 0.79 | 0.86 | 0.05 | 0.26 | 0.10 |
| 2 | 2.31 | 0.50 | 0.80 | 1.00 | 0.04 | 0.14 | 0 |
| 3 | 2.29 | 0.46 | 0.76 | 1.08 | 0.04 | 0.13 | 0 |

Frozen heading (moving sun): 1.92, 1.70, 0.75, 0.68 ms (the far cascades have almost no casters in that direction). Notes:

- Cascade 0 has almost no cull of its own (0.07): its cull is folded into the world cull earlier in the frame. Its stateSort (1.72) carries work the world pass pays when shadows are off.
- **Shared sort work.** The world-pass `stateSort` is 2.49 ms with shadows off but 0.94 ms with the cascades on (the first sort of the frame does the rebuild). So about 1.5 ms of the cascades' 4.17 ms `stateSort` would be paid anyway. Net, sort is +2.6 ms (all contexts 5.12 vs 2.49 ms), not 4.2.
- Avatars and skinned meshes are almost only in cascades 0 and 1 (the near ones): pool 0.5 ms, rigged batches 0.6 ms, palettes 0.3 ms, partly overlapping, about 1 ms of the 9.8 ms.
- The tail is heavy: `stateSort` p95 is 10-29 ms against a mean of 4-7. Some frames re-sort a lot.

Net effect of the cascades on the frame (still, sun minus none): frame +8.9 ms (23.07 to 31.90 steady). By zone: cascades +9.9, world `stateSort` -1.65, `renderGeomDeferred` -0.65, `renderDeferredLighting` -0.44, post-deferred -0.42, `Swap` -0.8, and `df idle` +1.95 (crowd/noise, not shadow work).

### CPU that could move to workers vs serial GL submission (still sun, sun passes 9.8 ms)

| part | ms | kind |
|---|---|---|
| cull (`updateCull`, 6 passes) | 1.5 | CPU, read-only on the octree: worker candidate |
| `stateSort` | 4.2 (net about 2.6) | CPU, builds the draw lists: worker candidate if per-pass state is separated (it writes the shared draw pools) |
| rigged batch assembly + skin palette math | about 0.9-1.2 (inside draw) | CPU, per avatar: worker candidate (palettes) |
| GL draw calls, state, uniforms, buffer binds | about 2.9-3.2 | serial: one GL context |

58 % of the cascade time (5.7 ms gross) is cull + sort, plus about 1 ms of CPU prep inside the draw rows; the serial GL part is about 3 ms. An upper bound for six workers on cull + sort + palettes, assuming perfect scaling and the shared sort staying in the world pass: about 4-4.5 ms of the 8.9 ms, so still sun 31.9 to about 27.5 ms (about 36 fps).

### GPU or CPU

- There are **no GPU zones** in the exports (only `tracy::GpuCtx::Collect`; the manifest says `gpu_frames=0`; `tracy-csvexport` does not export GPU zones). The GPU time of the shadow passes is not available from these captures.
- Signs from the waits: `Swap` (the only GPU wait zone) is 5.1 ms in none, 4.5 in sun, 5.7 in all (still): it does not grow with shadows. A "stall frame" is one with `Swap` > 10 ms: 12.9 % of frames in none, 14.0 % in sun, 19.1 % in all (still; the warm-up is included), 15.4 % and 14.2 % in the frozen pair, 7.2 % in moving none. Outside those frames `Swap` is 1.1-2.3 ms. Verdict: the frames are CPU-bound on the main thread (the added frame time equals the added main-thread zone time), with a periodic GPU/driver back-pressure stall in about one frame in seven. The shadows add little to the stall share, so they are CPU first. Whether the GPU is the next limit once the CPU drops is not answerable here (needs GPU zones or a GL timer-query capture).

## 3. Did the camera turn? (question 3)

**Only in `RT-moving__none`.** Evidence (Tracy and log):

- View-dependent world-pass work per 5 s bucket. In `moving__none` the fps goes 42, 54, 79, 88, 76, 53, 45, 41, 50, 72, 86, 74 (a sine with a 36 s period, which is 360 degrees at 10 degrees/s); the world `stateSort` goes 2.9, 1.6, 0.9, 0.8, 0.9, 1.9, 2.5, 2.9, 2.0, 1.2, 0.9, 0.9 ms; `renderGeomDeferred` 1.9 to 0.7 ms; world cull 0.81 to 0.23 ms.
- In `moving__sun` and `moving__all` these are flat for all 60 s: fps 37-41 and 34-37, `stateSort` 0.88-1.08 and 0.94-1.03, `renderGeomDeferred` 1.3-1.5, cull 0.57-0.77 and 0.59-0.68. No sine. The log's skinned palette calls a frame confirm it: moving none swings 36 to 352 a frame; moving sun is 343-359 (flat); moving all is 362-499 (a flat base with a spike about every 6 s from the projector avatar layers, no oscillation).
- The orbit is not in the log (camera commands go through LEAP and are not logged; `perf.json` has no camera trace), so the cause is not visible. It looks like the orbit runs for the first moving variant only and stops (or is reset) at the next variant switch. The mid-setting run `mc1-sl-20261116-163602` shows the same split (moving none skinned calls 132-389, sun 296-307, all 317-500 with the projector spike), so it is the scenario, not a one-off.

Consequences: the owner's impression was right, and the capture before it was frozen too. `RT-moving__all` is comparable only to `RT-moving__sun` (same frozen heading; their world costs agree within 3 %). It is **not** comparable to `RT-moving__none` (a whole-circle average with fps 42-88), and the frozen heading is lighter than the still view (sun passes 5.4 vs 9.9 ms), so it is not comparable to the still captures either. Do not use the moving none/sun/all as a shadow cost series. The fix is to re-run with the orbit re-armed per variant (and log a camera heading per second).

## 4. Projector (lamp) shadows (all minus sun)

Mean (p95) ms per frame, `all` captures:

| zone | still all (whole) | still all (15-58 s) | moving all (frozen) |
|---|---|---|---|
| `projector update` (whole) | 1.52 (3.6) | 1.25 (3.1) | 1.39 (3.9) |
| `projector updateLightSet` | 0.15 | 0.14 | 0.10 |
| `projector pinCasters` (inside `renderTile`) | 0.42 (1.1) | 0.40 | 0.38 (1.2) |
| `projector renderTile` (includes the avatar layer, `pinCasters`, its own cull/sort/draw) | 1.07 (3.1) | 0.82 (2.7) | 1.01 (3.5) |
| `projector drawAvatars` (inside `renderTile`) | 0.23 (1.0) | 0.14 | 0.25 (1.2) |
| `renderDeferredLighting - projectors` (world lighting pass; sun/none are 0.17/0.17 still, 0.08/0.09 frozen) | 0.26 | 0.24 | 0.12 |
| update's own time (update - updateLightSet - renderTile) | about 0.31 | about 0.29 | about 0.29 |

- There is no `renderLayer` zone in these captures; the avatar layer is `projector drawAvatars`, always inside a `renderTile`. The `renderShadowImpl` calls inside tiles total 1.06 ms (the tile's cull, sort and draw).
- **Tiles per frame:** `renderTile` 1.04 (still) and 1.10 (frozen); drawAvatars 0.58 and 0.62. Still: 1 tile in 1418 of 1467 frames, 2 in 46, 3 in 3. Frozen: 1 in 1974 of 2151 frames, 2 in 133, 3-4 in 44. The log agrees (`projector_tiles` 1.05 and 1.08, `projector_avatar_layers` 0.58 and 0.62 a frame, in every second of the capture). A tile costs 1.04 ms on average (median 0.59, p95 3.1, max 27.7 in the warm-up); frozen 0.92 ms (median 0.49, p95 3.5).
- **Is something invalidating every frame?** In effect, yes. The pool's frame cap is 0.35 ms (`kFrameCapMs`), but one tile alone costs about 0.6-1.0 ms, so the "progress pick" renders exactly one item every frame, and there is always a stale tile or avatar layer to render. 97 % of frames render one tile for the whole 60 s. A pool of static lamps would go quiet; this one never does. The captures do not say why: `perf.json` has `projector_stats: null` for every preset.
- **Why 8.1 ms still but 2.1 ms turning (fps-based)?** (a) `RT-still__all` is the first capture after the setting switch and has a 15 s warm-up (frame 127 ms in the first 5 s; the sun passes are 54 ms with `stateSort` 28 ms there; `projector update` is 5.9 ms in that bucket and 1.2-1.4 afterwards). Dropping the warm-up, still all-minus-sun is 35.5 - 31.9 = 3.6 ms (28.1 vs 31.4 fps). (b) Of those 3.6 ms the projector zones are 1.25 plus 0.07 in the lighting pass; the other 1.4 ms is the sun passes being slower in that capture (`stateSort` 5.8 vs 4.3 ms, p95 22 vs 10: a different crowd moment with more re-sorting). (c) The moving pair is a clean same-view comparison: 27.97 - 25.92 = 2.05 ms, which matches 1.4 ms of projector update plus 0.04 lighting plus noise. So the lamps cost about 1.3-1.5 ms; the 8.1 ms was mostly warm-up and sort noise.
- The mid-setting run (fps only, same scenario): all minus sun 4.4 ms still and 3.1 ms moving; sun minus none 9.0 ms still. Same picture: lamps a few ms at most, the cascades about 9 ms.

## 5. Without shadows (the `none` captures)

Still none: frame 22.9 ms (43.6 fps). Moving none (turning): 15.7 ms mean over a view that swings between about 11 and 24 ms. Inclusive ms per frame (not additive; they nest):

| block | still none | moving none (turn average) |
|---|---|---|
| `df idle` (the update half of the frame) | 5.85 | 3.74 |
| - `LLViewerObjectList::update` | 4.12 | 2.52 |
| - - avatar `idleUpdate` (about 50 avatars; `updateCharacter` 2.55) | 3.57 | 2.25 |
| `Swap` (wait) | 5.12 | 3.80 |
| `renderDeferredLighting` (CPU submit, 2 passes) | 3.33 | 1.99 |
| `renderGeomPostDeferred` (alpha and the rest) | 2.96 | 1.73 |
| `stateSort` (3 passes) | 2.49 | 1.42 |
| `renderGeomDeferred` | 1.99 | 1.19 |
| `Update Images` (decode priority incl. rigged `calcPixelArea`, 1.35 ms self) | 2.08 | 2.07 |
| vertex buffer flush (`glBufferSubData`) | 0.98 | 0.54 |
| world `updateCull` (3 passes) | 0.76 | 0.42 |
| `Render Cube Face` (probe) | 0.69 | 0.73 |
| UI (`updateUI` 0.46, `render_ui_2d` 0.30) | 0.76 | 0.63 |
| network (`idleNetwork`) | 0.16 | 0.09 |

Reading: the crowd costs about 3.6 ms in avatar update, about 1.4 ms in rigged pixel-area work inside `Update Images`, plus its share of the submission blocks. `Swap` (5.1 ms) is the largest single wait, with the periodic stalls described in section 2. Lighting + deferred + post-deferred submission is 8.3 ms, sort 2.5. The zone `cglt - sync` (2.2 ms a frame) is on the texture worker thread (`syncToMainThread` asserts not-main), so it is not a main-thread cost; texture and network work on the main thread is small.

## 6. Variance between the captures (question 5)

| | still all | still sun | still none | moving all | moving sun | moving none |
|---|---|---|---|---|---|---|
| avatars updated per frame (`LLVOAvatar::idleUpdate` count) | 44.8 | 49.0 | 50.9 | 50.7 | 48.5 | 49.3 |
| skinned palette calls per frame (log) | 393 | 371 | 417 | 406 | 351 | 192 (turning) |
| avatar `idleUpdate` ms | 4.22 | 4.71 | 3.55 | 3.72 | 3.59 | 2.12 |
| world `updateCull` ms | 0.78 | 0.80 | 0.75 | 0.63 | 0.64 | 0.42 |

- Avatar count varies by up to 12 % in the still set (44.8 to 50.9) and 4 % in the frozen/moving set; `df idle` varies by 2 ms between captures (5.8 to 7.8) without a count that explains it. A crowd/noise band of about 1-2 ms on a 23-36 ms frame (4-6 %) is a fair reading. The still set is comparable for the zone-level numbers (world cull 0.75-0.80 ms), but `RT-still__all` has the warm-up and the heavy sort tail.
- Palette counts are not a pure crowd measure (sun 371, all 393: the pool's avatar layers add calls). Avatars per frame is the cleaner one.
- **Comparable sets:** still none / sun / all (after dropping the first 15 s of all); moving sun vs moving all. **Not comparable:** moving none with anything; moving sun/all with the still ones.

## 7. What skipping the cascades under a dark sky would save at best

- Still view, this crowd: **8.9 ms a frame, 28 %** (sun mode 31.4 fps goes to the none level, 43.3 fps). The net is smaller than the zone time (9.9 ms) because with no cascades the world pass pays about 1.5 ms of sort work and about 1.5 ms of lighting/geometry/swap difference it otherwise avoids.
- Frozen lighter heading: the cascades are 5.4 ms (21 % of 25.9 ms), so about 4-5 ms net; there is no matching none capture.
- Skipping lamps as well (all to none, still steady): 35.5 to 23.1 ms, 12.5 ms or 35 % of the frame.
- GPU saving: unknown (no GPU zones).

## 8. What to look at next for the lamp cost

1. Find out why one tile is stale every frame. Capture `projector_stats` (dirty boxes per frame, mover objects, dirty renders, dirty age max, and the cause of each pick: pose stale, dirty box, avatar layer, never drawn). This run's `perf.json` has `null`; the stats API exists (`llprojectorshadowstatsapi`). With about 50 avatars walking through lamp volumes, the dynamic avatar layer probably re-triggers (0.6 avatar layers a frame); check whether the avatar layer or a mover's dirty box is the cause.
2. Per-tile cost: median 0.5-0.6 ms, p95 3.1-3.5 ms; a tile exceeds the 0.35 ms cap about three times by design (progress pick). Options to evaluate: an avatar-layer-only update that skips the static cull/sort (`pinCasters` 0.4 ms is inside every tile), sharing pinned casters between tiles of one frame, and finding what makes a p95 tile cost 3 ms (a lamp with many casters?). Zone arguments (lamp id, casters, cause) on `projector renderTile` would answer it.
3. `renderDeferredLighting - projectors` (0.08-0.26 ms) and `updateLightSet` (0.10-0.15 ms) are not worth work.
4. Re-run the moving series with the orbit re-armed per variant, and discard the first 15 s of the first capture after a setting change (or run a throwaway capture first), so the warm-up does not charge the lamps.
5. GPU: add GPU timer zones or a GL timer-query capture; the CPU numbers cannot say whether the cascades and tiles are GPU-heavy.
