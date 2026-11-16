<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Sun and moon shadow cascades: stable, camera-independent, cached (study refresh)

Status: study, docs and offline tools only. It supersedes the sun-cascade parts of study #330
(`codex/indoor-shadow-study`, `doc/shadows/shadow-model-rework.md`). That branch is not changed. Code
references are file:line at `origin/vkstorm-devel` 191c9985b9. Measured numbers say where they come from.
Estimates are marked **(est.)** and assumptions **(A)**.

The branch adds:

- this document;
- `scripts/shadows/sun_cascade_cache_model.py`: an offline model (no GPU). It ports today's stable fit and the
  proposed levels, and prints every table below. `--check` runs its invariants;
- `scripts/shadows/bench_sun_cascade_copy.py`: a GPU timing bench for the cache's per-frame depth copy. It holds
  `.fixture-tmp/gpu-bench.lock` while it times.

## 1. Summary

**What changed since #330.** Most of what #330 found wrong with the sun cascades is fixed under the Stochastic
method:

- Stage S1 (9d99398c7c) brought fixed splits from `RenderShadowDistance`, slice bounding spheres, a north-locked
  light "up", world-anchored texel snapping, orthographic only, and no clearing of empty cascades.
- Stage S2 (68cddf9c45) brought the bias in world units.
- L3/AT1 brought the sun/moon handover without a pop.
- #540 skips the cascades under a dark sky.

Legacy keeps Firestorm 7.2.5's fit on purpose, so every #330 finding is still true there, by design.

**What is still wrong under Stochastic**, ranked by cost and by how visible it is:

1. **The cascades are drawn from scratch every frame, and that is the largest single cost in a crowd.** At
   Rooftops (Ultra, about 50 avatars) the sun/moon cascades cost **8.9 ms net of a 31.9 ms frame**, all on the main
   thread: cull 1.5 ms, sort 4.2 ms, GL submission about 3 to 4 ms. Nothing is reused from one frame to the next,
   although a static scene gives identical maps every frame (S1 snaps them).
2. **The maps follow the view direction.** Each cascade is centred on its view slice, so a degree of yaw moves
   every map by 12.6 texels (model §2). A cache cannot survive a camera turn.
3. **Which cascade a point uses depends on the view direction.** The selection is by view depth. Turning in
   place changes a fixed ground point's mix of cascade texel sizes in 11 of 16 turned views of the model (§2).
4. **Semi-transparent casters' grain re-rolls as the camera walks. This is a bug on every line.** The Stochastic
   alpha dither hashes the map-relative texel. The sun maps step by one texel whenever the camera moves one texel,
   so the grain under glass and sheer curtains changes while walking. The shader comment promises "stable wherever
   the map is", which holds for projector tiles only (§2, N1). It is a hotfix, not a stage.

**Design**, with each element named after its published technique:

- **Levels.** Four square, orthographic, texel-snapped levels of fixed world size, centred on the camera
  *position*. Each level is selected by the point's distance from the camera, not by view depth. This is the
  clipmap family: Tanner et al. 1998; Unreal's virtual shadow map clipmaps; Cyberpunk 2077's boxes around the
  player.
- **Cache.** Each level keeps a cached **static layer**, refreshed only when the camera has moved a guard band,
  when the light has moved half a texel, or when the scene changed inside the level. Each frame the static layer
  is copied into the live map and the **dynamic layer** is drawn over it: avatars, attachments, animesh and
  moving objects. This is the static/dynamic split of Cyberpunk 2077, Insomniac's CSM scrolling, DOOM 2016 and
  Unreal VSM, and it is what the projector pool already does (P3/P4).
- **Reuse.** The scene change feed, change index, mover classifier and S-to-L copy of the projector pool are
  reused.

**Cost** at Rooftops, still, estimated from the measured per-cascade numbers plus marked assumptions:

- cascade CPU **9.35 to 2.0-4.1 ms**;
- frame **31.9 to 26.1-28.1 ms** (31 to 35.5-38 fps);
- turning in place costs the same as standing still, against a full redraw every frame today;
- the worst single frame while moving is 5.4-7.4 ms of cascade work against 9.35 today;
- GPU: the per-frame copy measures **0.13 ms** for four 1920² maps (0.07 ms for half maps);
- memory: +59 MB at 2560x1440.

**The 60 fps still target cannot be met by the cascades alone at Rooftops Ultra.** The same frame without any sun
shadow is 23.1 ms (43 fps). The cache closes about 45 to 65 % of the gap the cascades open.

**Image.** Legacy does not change. Under Stochastic:

- stage C1 changes the texel size per level (finer at the default 60 degrees and 16:9, coarser when zoomed in);
- the shadow distance edge becomes a circle around the camera;
- stage C2 changes nothing for a static scene, and moving casters update every frame as today.

**Stages**, about 3 to 4.5 days of agent time with C4 optional. Effort is calibrated in §8.

| Stage | Content |
|---|---|
| H | dither hotfix |
| C0 | measure |
| C1 | camera-position levels and radial selection |
| C2 | static/dynamic cache |
| C3 | partial refresh and tuning |
| C4 | toroidal scrolling, only if C2's numbers ask for it |

## 2. #330's findings re-verified on devel 191c9985b9

#330's model (`scripts/shadows/cascade_fit_model.py` on `codex/indoor-shadow-study`) was re-run unchanged: its
Legacy tables are identical, as they must be, since the Legacy fit is CI-checked against Firestorm 7.2.5
(`scripts/shadows/check_legacy_shadows.py`). The Stochastic column uses this branch's model.

| # | #330 finding | Legacy | Stochastic | Evidence (devel 191c9985b9) |
|---|---|---|---|---|
| 3.1 | Split range follows the visible extents, occlusion off | STILL TRUE (by design) | **FIXED** (S1) | Legacy: `computeSunSplitsLegacy` pipeline.cpp:12089-12188 (point cloud 12117, `far_clip` 12160), occlusion off 12769. Stochastic: pipeline.cpp:12900-12933, splits from `snapShadowDistance(RenderShadowDistance)` and exponent y only (`llshadowcascadefit.h` `splitDistances`). Model: Ultra splits 0.25 / 4.06 / 24.22 / 81.14 / 192 m whatever the view; #330's rerun still gives far = 21 m at pitch -45 and 256 m at -30 for Legacy |
| 3.2 | No stable fit (light view at the camera, "up" = camera at-axis, sub-texel sliding, grid turning) | STILL TRUE | **FIXED for the grid; CHANGED** | Legacy: up 12096-12101, `look(camera.getOrigin())` 12286. Stochastic: `lightUp` (north) 12914-12918, sphere + quantised radius 12652-12653, `snappedSquare` on the world anchor 12662-12666. The view matrix is still anchored at the eye (12662), but the box is snapped relative to the world origin, so the grid is world-fixed (model §3: a world point keeps world column 426 while the camera walks). **Remaining:** the box centre is on the view axis (12654), so the map content moves 12.6 texels per degree of yaw (model §2, N2) |
| 3.3 | Ortho/warp flips per frame | STILL TRUE | **FIXED** | Legacy: 12390 (`RenderShadowErrorCutoff`), 12433 (`RenderShadowFOVCutoff`). Stochastic: ortho only, 12669-12671 |
| 3.4 | Bias in per-cascade depth units, plus an altitude term | depth units STILL TRUE; altitude **FIXED** | **FIXED** (S2) | Altitude term commented out for every method, pipeline.cpp:10412-10419 (`scripts/perf/test_shadow_bias_altitude.py`). Legacy stock `pcfShadow` still adds `shadow_bias` in [0,1] depth (shadowUtil.glsl:93). Stochastic: `SHADOW_BIAS_TEXELS`, normal offset, plane cap in metres (`llshadowcascadefit.h`, shadowStochasticUtil.glsl:198-213) |
| 3.5 | Empty cascades cleared | STILL TRUE | **FIXED** | Legacy 12250-12266. Stochastic clears only cascades turned off by `RenderShadowSplits` (12629-12649); every enabled cascade renders |
| 3.6 | Glass casts a 50 % column dither | STILL TRUE (stock stipple, shadowAlphaMaskF.glsl `#else` branch) | **CHANGED, with a new defect** | Stochastic: proportional hashed dither from 0.05 to 0.98, mask faces never dither (shadowAlphaMaskF.glsl:67-88). The hash reads `gl_FragCoord - shadow_tile_origin`, and the origin is 0 for the sun passes (only `LLLocalShadowGL::setTileOrigin` sets it, lllocalshadows.cpp:97-115, reset to 0 at :77). It is keyed to the *map* texel, which moves with the camera: finding N1 |
| 3.7 | Projector shadows chosen from the camera, fading with camera speed | STILL TRUE (`spot-slots`, 13008-13050; Vulkanstorm's distance-fade fix applies) | **SUPERSEDED** | The projector pool P1-P4 replaces the two slots under Stochastic (12993-13004, `llprojectorshadows.cpp`, `doc/shadows/projector-shadow-pool.md`): tiles chosen by lamp, not by camera, and cached |
| 3.8 | Objects instantiated only for the main camera | STILL TRUE, every method | STILL TRUE | llvocache.cpp:1052-1055 (`cull` returns for any camera but `CAMERA_WORLD`), 1005 (`selectBackObjects`). Transient (until each object is first seen or enters the 32 m rear sphere). Not changed by this design: under the cache, an object that loads arrives through the change feed (`add_remove`) and refreshes the levels it touches |
| 3.9 | Sun/moon switch pop | **FIXED** (L3/AT1, every method) | **FIXED**; dark gate on top | The caster direction is the primary light's (12871-12872). The shadow fades 1 to 0 from +0.6 to +0.3 degrees, the direction swings inside ±0.3 with no shadow, and the moon fades in from -0.3 to -0.6 (`doc/light/handover-l3-at1.md`). #540: the cascades are skipped below E = 5e-6 with hysteresis, cleared once, with a strength fade from 5.5e-6 to 1e-5 (12947-12966, `lldirectionalshadowgate.h`). Leftover, LOW: the `shadow_hidden` branch (inside the handover band, a moonless night) clears all four maps every frame (12968-12972) instead of once. Harmless: 0.02 ms GPU by the clear bench in §6 |
| 3.10 | Off-screen occluders culled (rejected in #330) | still rejected | still rejected | S1 culls from the slice up to the light with the footprint guard (12673-12701), `shadow_near_clip` 12876-12881, depth clamp in `renderShadowImpl` 11658 |

### New findings (Stochastic, devel 191c9985b9)

Ranked.

**N0 (HIGH, cost): nothing is reused between frames.**

- All four cascades are culled, sorted and drawn every frame (`generateSunCascadesStable`, pipeline.cpp:12572-12756;
  S1 was cut "uncached", 9d99398c7c).
- Measured at Rooftops still (sun minus none): **+8.9 ms net, 28 % of the frame**. Per cascade it is 2.71 / 2.04 /
  2.31 / 2.29 ms. Cull + sort is 58 % and GL submission 42 % (`reports/rooftops-ultra-shadow-split-2026-11-16.md` §2).
- The multicore baseline counts sun shadows at 38-42 % of the main-thread frame, and their serial GL submission at
  5-10 ms. It names "reuse of cascade results across frames" as the lever no thread can replace
  (`doc/perf/multicore-baseline-2026-11.md` §§ 4.x, conclusion 4).

**N1 (MEDIUM, visible, bug on every line): the alpha dither is keyed to the map, not the world.**

- `snappedSquare` keeps the box at a fixed offset from the eye, moved in whole texels of the world grid. So the box
  corner's world-grid index k changes by one for every texel the camera moves.
- A fixed world point's map column changes with it: model §3 shows 1386, 1383, ... 1372 over 10 cm of walking
  while its world column stays 426.
- The hash in shadowAlphaMaskF.glsl:78 uses the map column, so the grain of every semi-transparent sun caster
  (glass, sheers, alpha-blend foliage) re-rolls continuously while the camera moves. With 0.7 cm texels in level 0
  that is every frame while walking. The temporal resolve sees it as motion.
- **Fix (hotfix):** before the sun cascade's alpha passes, pass the box's world-grid offset as the origin:
  `setTileOrigin(-k_x, -k_y)`, with `k = (box.min - anchor) / texel` from `generateSunCascadesStable`, then reset to
  0. This is the mechanism the projector pool already uses.
- Present on `vkstorm-devel`, `-release`, `-canary` and `-rust` (all have `snappedSquare` and the hashed branch).
  Legacy is untouched (stock stipple).

**N2 (HIGH for caching, MEDIUM visible): the maps and the level choice follow the view direction.**

- The slice sphere's centre sits on the view axis, 5 to 240 m ahead of the camera (model §2), so turning moves
  each map 12.6 texels per degree. The static content stays texel-identical (same world grid, same size), so the
  *image* of a static scene survives a turn, but every map must be redrawn. A cache is impossible while turning.
- The lookup picks the level by view depth (shadowStochasticUtil.glsl:238-262). Turning changes a fixed point's
  mix of texel sizes: at 4.5 m it goes from 28 % level 0 at bearing 0 to 93 % at 45 degrees (model §2).

**N3 (LOW): shadow casters use the main camera's LOD.**

Their silhouettes change as the camera approaches (#330 §3.11, unchanged). The projector pool pins LOD for lamp
tiles. For the sun this design does not pin (the levels cover up to 256 m); see decision D5.

**N4 (LOW): the `shadow_hidden` branch clears the maps every frame.** See 3.9.

## 3. How it works today (Stochastic), with file:line

1. `LLPipeline::generateSunShadow` (pipeline.cpp:12759) runs on the main thread. It runs once for the main view,
   and once per reflection-probe face with `gCubeSnapshot`, two cascades, into `mAuxillaryRT` (the Rooftops
   capture's second call, 0.4 ms). It disables occlusion (12769) and narrows the render types (12785-12853).
2. Caster direction: the primary light (12871-12872); near plane 2 x far clip towards the light (12876-12881).
3. Splits (Stochastic) from `RenderShadowDistance`, snapped to 32-256 m (default 128; feature table High 128,
   HighUltra 160, Ultra 192), split exponent y = 3.0 (12900-12933).
4. Dark gate and hidden shadow (12947-12972), then `generateSunCascadesStable` (12572-12756). Per cascade:
   - the slice's corner rays (5 % wider);
   - the slice sphere and quantised radius;
   - `look(eye, lightDir, -up)`;
   - `snappedSquare`;
   - depth range = sphere + 1 R towards the light;
   - a cull box narrowed to the slice's light-space AABB plus `casterCullMargin`;
   - `renderShadow` → `renderShadowImpl` (11626): `updateCull`, `stateSort`, opaque/rigged, `renderGeomShadow`,
     alpha masked/blend/fullbright/grass/glTF passes.
5. Maps: `mRT->shadow[0..3]`, `GL_DEPTH_COMPONENT24` (llrendertarget.cpp:98), square `sqrt(w*h)`
   (`allocateShadowBuffer`, 1186-1201): 1440² at 1920x1080, 1920² at 2560x1440. Compare mode off under
   Stochastic.
6. Lookup (`sampleDirectionalShadow`, shadowStochasticUtil.glsl:224):
   - strength-0 early-out;
   - beyond the far split, lit;
   - blend weights of the bands [0.75 d_j, 1.25 d_j+1] by **view depth**, and one level drawn by weight;
   - 8 rays toward the light's disc (`stSunRays`: the *current* light direction, transformed by the cascade's
     matrix);
   - a march of 2-8 steps per ray, up to 4/8/16/32 m and the 0.25 m footprint;
   - temporal resolve with weight 1/8.

## 4. Literature (named techniques; references only, no sample code)

The repo's own reading (`reports/Real time shadow casting techniques.md`, sources marked read/fetched there) and
the standard references:

| Technique | Source | What this design takes |
|---|---|---|
| Parallel-split / cascaded shadow maps | Zhang et al., "Parallel-Split Shadow Maps on Programmable GPUs", GPU Gems 3 ch. 10 (2007); Dimitrov, "Cascaded Shadow Maps", NVIDIA (2007); Engel, ShaderX5 (2006) | split scheme (already S1) |
| Stable CSM: bounding sphere, texel snapping, fixed sizes | Valient, "Stable Rendering of Cascaded Shadow Maps", ShaderX6 (2008); Microsoft, "Common Techniques to Improve Shadow Depth Maps"; MJP, "A Sampling of Shadow Techniques" (2013) | already S1. MJP's caveat that stability holds only "provided that your cascade partitions never change" is why the levels become camera-position-centred |
| Normal offset; receiver-plane bias | Holbert, "Normal Offset Shadows", GDC 2011 poster; Isidoro, "Shadow Mapping: GPU-based Tips and Techniques", GDC 2006 | already S2 (world units) |
| Clipmaps: levels centred on the viewer, fixed sizes, toroidal update | Tanner, Migdal, Jones, "The Clipmap", SIGGRAPH 1998; Asirvatham and Hoppe, "Terrain Rendering Using GPU-Based Geometry Clipmaps", GPU Gems 2 ch. 2 (toroidal addressing); Epic, "Virtual Shadow Maps" docs (directional-light clipmap levels centred on the camera position) | the level geometry (C1) and optional scrolling (C4) |
| Camera-position boxes for distant sun shadows; static/dynamic slices; amortised far refresh with a day cycle | Dybisz and Witanowski, "Shadows Optimizations in Cyberpunk 2077", SIGGRAPH 2021 talk | near per frame / far cached is alternative D1-B; the static/dynamic split (C2) |
| Scrolling cached cascades (static geometry reused, about 70 % skipped) | Acton, Day, Hastings, "CSM Scrolling", SIGGRAPH 2012 Advances (Insomniac) | the static layer, "static" inferred at run time, and toroidal strips (C4) |
| Per-light cached static maps, copy static then draw dynamic | Courrèges, DOOM (2016) study; Unity HDRP cached shadows; Epic VSM page caching (static and dynamic copies) | the S-to-L copy (C2), as the projector pool does |
| Amortised cascade updates | Kasyan, "Playing with Real-Time Shadows", SIGGRAPH 2013 course (Crytek) | the per-frame refresh budget. *Not read in full* (the literature report lists it as unread); cited for the idea only |
| Temporal coherence and caching in general | Eisemann, Schwarz, Assarsson, Wimmer, *Real-Time Shadows* (CRC 2011) | background |
| Rejected: tight per-frame refits | Lauritzen, Salvi, Lefohn, "Sample Distribution Shadow Maps", I3D 2011 / SIGGRAPH 2010 | view-dependent texels by construction; cannot be cached |
| Rejected on OpenGL: virtual shadow maps | Epic VSM docs ("likely not a good fit" without Nanite); Sakmary 2025 (page marking about 1.0-1.5 ms) | revisit for the Vulkan line only |

Skinned characters invalidate every cache in every engine (Epic: skeletal deformation "always invalidates cached
pages every frame"). Avatars therefore stay a per-frame cost. The design's job is to make them the *only*
per-frame cost.

## 5. Design

### 5.1 Levels centred on the camera position (stage C1)

- **Geometry.** Level j is a square in light-view xy, centred on the camera position c_j (snapped to the level's
  texel grid, which is anchored at the agent origin as S1's grid is). Its half-side is
  `R_j = quantize((reach_j + margin_j) / (1 - g))`, where:
  - `reach_j = 1.25 d_j+1` for levels 0-2, and `d_4` (the shadow distance) for level 3;
  - `margin_j = casterCullMargin(texel_j)`, which covers the 0.25 m footprint and the normal offset;
  - g is the guard band (decision D3; recommended 0.125).

  R_j depends only on the shadow distance, the split exponent and the map size. It does not depend on the
  field of view or the view direction.
- **Depth range.** As today: the sphere of radius R_j around the camera, plus one R towards the light. Casters
  nearer the light are depth-clamped and kept by `shadow_near_clip`.
- **Selection by distance.** In `sampleDirectionalShadow`, the band weights read `length(pos)` instead of
  `-pos.z`, and so does the far cut and fade. The bands themselves ([0.75 d_j, 1.25 d_j+1]) and the stochastic
  pick are unchanged. Every program that links the Stochastic util takes it (sun, probe, forward), so they stay
  consistent.
- **Texel size** (model §1, Ultra, map 1920², guard 0.125):

  | View | Level 0 | Level 1 | Level 2 | Level 3 |
  |---|---|---|---|---|
  | 60 degrees, 16:9 (the default `CameraAngle`) | 0.65 cm (S1 0.70) | 3.75 (4.17) | 12.5 (14.2) | 23.4 (33.4) |

  - At the default view the levels are 7-30 % **finer** than S1, because S1's slice spheres are wider than the
    radial reach.
  - 4:3 is 6-14 % coarser in levels 0-2.
  - Zoomed in to a 30 degree field of view: 1.5-1.8x coarser than S1 gives at that field of view. At 15 degrees:
    2.1-2.5x coarser. S1 shrinks its slices with the field of view; fixed world sizes do not (decision D2).
- **Rotation invariance.** Turning in place changes neither the maps nor any point's level (model §2: 0 of 16
  turned views change; S1 changes 11). The map content moves 0 texels per degree.
- **Per-frame cost without the cache:** about today's. The level is drawn only inside the view's footprint
  (§5.3), the same cull box S1 computes (12680-12701), extended to the radial reach. C1 alone is therefore an
  image stage, not a cost stage.

### 5.2 World-anchored dither (hotfix H, ahead of the stages)

`setTileOrigin(-k_x, -k_y)` around each sun cascade's alpha passes, then the existing reset to 0. Here k is the
box corner's index on the world grid; it is an integer below 2^16 within a region at the finest texel, so it is
exact in float. The grain then belongs to the world texel. It changes only when a point changes level, or once
at a region crossing (the anchor shifts with agent space). `LLLocalShadowGL::setTileOrigin` already binds the
five alpha shadow programs and their rigged variants.

### 5.3 The cache: static layer S, live layer L (stage C2)

Per level j of the main view (`mMainRT` only; probes and the hero probe stay uncached):

- **S_j**, a `GL_DEPTH_COMPONENT24` map the size of L_j. It holds the **fixed casters** over the **whole square**
  (omni: all directions around the camera), drawn with the level's light matrix V_j, P_j held since its last
  refresh. It is never sampled.
- **L_j**, today's `mRT->shadow[j]`, the map the shaders read. Each frame, L_j = copy of S_j (`glCopyImageSubData`,
  the pool's call, llprojectorshadows.cpp:586), then the **dynamic casters** drawn over it with depth test. The
  dynamic casters are culled to the view's footprint in that level (the S1 cull box, extended to the radial
  reach), so the per-frame avatar work equals today's.
- The lookup matrix `mSunShadowMatrix[j] = trans · P_j · V_j · inv_view(current)` is rebuilt every frame from the
  held V_j, P_j. A cached level therefore stays geometrically correct while the camera moves inside its guard band.
- **Skip rule:** a level whose footprint held no dynamic caster this frame or the last, and whose S did not
  change, skips both the copy and the dynamic pass. Level 3 in a crowd usually qualifies.

**Refresh triggers for S_j.** These are decided once per frame on the main thread at the top of
`generateSunShadow`, before any cull:

| Trigger | Rule | Kind |
|---|---|---|
| invalid | first use, allocation, size or setting change (`RenderShadowDistance`, method, detail, resolution), the dark gate or `shadow_hidden` closing (cleared once) and reopening | hard: refresh this frame |
| coverage | the camera's light-space xy offset from c_j exceeds 0.75 g R_j (soft) or g R_j (hard); or its light-space depth leaves the headroom | soft is scheduled; hard is this frame |
| light | the light's direction moved by more than θ_j = 0.5 texel_j / min(R_j, 40 m) since S_j was drawn (decision D7) | scheduled |
| dirty | a change-feed box overlaps the level's square in light-space xy, at any depth towards the light | scheduled; C3 makes it a scissored partial refresh |
| age | older than 10 s (safety net for anything the feed misses: animated alpha textures, linkset parts; see D4) | scheduled, staggered |

- **Budget.** At most one scheduled static refresh per frame: invalid and hard first, then coverage, light, dirty,
  age, nearest level first, oldest first. Model §5 gives the demand at 40 fps: walking 0.13 refreshes per frame,
  running 0.20, flying 0.64, all under the budget of 1.
- **Region crossing (`shiftObjects`).** Translate the held V_j and c_j by the agent-space shift. The maps stay
  valid, and the next refresh snaps to the new anchor. This mirrors `LLShadowDirtyFeed::noteShift`.

**What counts as dynamic (decision D4).** Everything that casts a moving shadow today must keep doing so every
frame. The sun's dynamic set is therefore wider than the projector pool's:

- avatars and their attachments (`PARTITION_AVATAR`, the worn bridges) and animesh (`PARTITION_CONTROL_AV`),
  as P4 does;
- active bridges: movers and physical objects while active, flexi, client-spun parts;
- faces with texture animation on alpha (if they can be identified cheaply; otherwise the age refresh covers
  them).

The pool drops active movers from lamp tiles (P3 "known limits"); for the sun that would be a regression.

**What dirties S.** The pool's `LLShadowDirtyFeed` hooks, as they are: `put` / `remove` / `move`, `markRebuild`
(volume, rigged, position), `markTextured` for alpha prims, mesh loaded, `updateGeom` completion, `markMoved`, and
the classifier edges. All of them run on the main thread (projector-shadow-pool.md, "The hooks"). `beginFrame` is
idempotent per frame, so the pool and the sun cache read the same list.

The pool leaves terrain and grass out because their geometry follows the camera distance. For the sun, decision
D5 recommends that terrain patch rebuilds and LOD rebuilds *do* dirty the levels, with C3 adding a texel rule if
they refresh too often while walking.

`ChangeIndex` takes six planes plus an AABB per volume (`llprojectorshadowpool.h:535`). A level's light-space box
fits that form unchanged.

**Pass flag.** The static pass culls every partition but the avatar ones and drops dynamic bridges. The dynamic
pass culls the avatar partitions and the dynamic bridges only. This is the pool's `sLocalShadowPass` mechanism
(`LOCAL_SHADOW_STATIC` / `DYNAMIC`, 21 sites in pipeline.cpp, lldrawable.cpp, lldrawpoolavatar.cpp,
llprojectorshadows.cpp), extended with the sun's wider dynamic set.

One difference from the pool: the sun's dynamic pass **keeps stamping visibility**, as today's cascades do. An
avatar seen only through its sun shadow must keep animating as it does now.

### 5.4 Partial refresh and scrolling (stages C3, C4)

- **C3, scissored partial refresh.** A dirty box maps exactly to a light-space rect of the level (ortho
  projection). Refresh only that rect:
  - scissor the clear;
  - cull with a sub-box projection (the trick S1 already uses for `cull_proj`);
  - draw with the full projection and the scissor on.

  Cost scales with the changed area, not the level. C3 also tunes the light threshold and the terrain/LOD rule
  on C2's measured refresh counts.
- **C4 (optional), toroidal scrolling** (Insomniac CSM scrolling; clipmap toroidal update). On a coverage refresh,
  draw only the newly exposed L-shaped strip, and address the map with a wrap. The Stochastic util reads with
  `texelFetch`, so the wrap is one integer modulo per fetch. Do it only if C2 shows coverage refreshes dominating
  while walking or flying; the model says they do not (walking 0.13 per frame).

### 5.5 Interactions

- **Stochastic method** (8 rays, s = 8 temporal; `doc/shadows/shadow-method.md`).
  - The rays use the current light direction through each level's held matrix. A held direction that lags by
    less than θ_j displaces an occluder by at most half a texel, inside the penumbra.
  - The history's light-change reset (more than 0.5 degree) is unchanged. A refresh step under half a texel goes
    through the 3x3 clamp over about 8 frames.
  - A static scene gives texel-identical maps refresh after refresh (snapping), so nothing new reaches the
    history.
- **Sun softness rule** (EEP `sun_scale` x `RenderShadowLightSize` 0.23). It is a per-frame uniform for the rays
  only; the maps do not depend on it. The footprint (0.25 m), and with it the cull margin, are unchanged.
- **#540 dark gate and the handover.** Closing either clears the maps once and marks every level invalid; opening
  refreshes all four on that frame, which costs what today's every frame costs. During the handover's swing
  (strength 0) no refresh runs, even though the direction moves through tens of degrees.
- **Cloud shadows (L2).** These come from a separate world-anchored map multiplied in by `primary_shadow_strength`
  (`doc/light/cloud-shadows.md`). No interaction.
- **Projector pool.** It is separate (its own atlas and caps). It shares the change feed. The
  sun cache does not count against the pool's cap. It has its own rule: one static refresh per frame.
- **Reflection probes.** These use the aux pack, are uncached, and take the same level geometry and radial
  selection, because they link the same util.
- **Legacy.** `computeSunSplitsLegacy`, `generateSunCascadesLegacy` and the stock shaders are not touched.
  `check_legacy_shadows.py` and `bench_shadow_legacy.py` (bit-identical light map) are gates of every stage.
- **Planned worker-thread cull/sort** (FrameJobs; the multicore baseline names the cascade culls and sorts as the
  first candidates). The cache decides its pass list (which levels refresh, which dynamic passes run) on the main
  thread before any cull, from the already swapped feed. Each pass carries its own `LLCullResult` (the static
  `result[4]` in `generateSunCascadesStable` becomes per pack and per layer) and its own cull box. FrameJobs can
  then run those culls in parallel. GL submission and copies stay on the main thread. Two things are left to
  FrameJobs: the pass flag is still the global `sLocalShadowPass`, and `sCull` / `stateSort` write shared pools.
  C2 routes every new check through one accessor so FrameJobs converts one place. The cache shrinks what FrameJobs
  must parallelise (still: from about 5.7 ms to about 1-2 ms of cull + sort, est.), and the two add.

### 5.6 Threads, lifetimes, failure

- **Threads.** Everything runs on the main thread: the generation, the hooks (an off-thread push marks
  "everything stale", as in the pool), the copies and the allocations. No worker thread calls
  `generateSunShadow`, `allocateShadowBuffer` or `releaseSunShadowTargets`. C2 re-checks this with a grep of every
  caller, per AGENTS.md.
- **Lifetimes.** S_j sits beside `mRT->shadow[j]` for `mMainRT` only. It is allocated in `allocateShadowBuffer`
  under `!gCubeSnapshot` and released wherever `releaseSunShadowTargets` runs (900, 934, 1545, main thread). The
  cache's state is per pack, not static.
- **Failure.**
  - If S cannot be allocated, the levels are drawn every frame as today (same image, today's cost), logged once,
    and retried at the next allocation point. This follows the precedent of the stochastic history ("only the
    accumulation is turned off"); it is not a method fallback.
  - A NaN or out-of-range camera or light marks the levels invalid, and they are drawn uncached that frame.

### 5.7 Alternatives considered

| Alternative | For | Against | Verdict |
|---|---|---|---|
| A (recommended) all four levels camera-position-centred, each with S + L | turning costs nothing; the crowd-still case saves the most (near levels hold most static work); one rule everywhere | +4 depth maps of memory; zoomed views coarser | recommended |
| B: near levels per frame (S1), far levels 2-3 centred and cached (Cyberpunk) | half the memory; C1 image change only far | the still crowd keeps levels 0-1 (4.75 ms at Rooftops) every frame; turning still redraws 0-1 | fallback if D1 or D6 say so |
| C: keep S1, cache only while the camera is still | no image change | turning (constant in SL: orbit, alt-cam) redraws everything; caches thrash | rejected |
| D: amortise without a static/dynamic split (redraw far cascades every N frames, avatars included; Kasyan) | no extra memory, simplest | avatar shadows in far levels update at 1/N rate (visible lag); no help near | rejected |
| E: FrameJobs only (threads, no cache) | no image change | upper bound about 4-4.5 ms of the 8.9 (report §2), and the serial submission stays | complementary, not a substitute |
| F: virtual shadow maps | finest texels | OpenGL without Nanite-class geometry: page marking alone is about 1-1.5 ms | Vulkan line only |

## 6. Costs

### CPU (main thread)

Model §6, Rooftops Ultra still, inputs from the report's per-cascade table. Assumptions:

- (A1) the dynamic cull is 10-25 % of a level's cull;
- (A2) avatars are 30-70 % of the sort in levels 0-1 and 10-30 % in levels 2-3;
- (A3) the dynamic draw is the avatar rows, plus up to 40 % for non-rigged attachments;
- (A4) a static refresh costs the level's static work times the omni factor (model §4: 1.6 for levels 0-2, 1.0
  for level 3, sun at 35 degrees);
- (A5) the 1.5 ms of sort the first pass of a frame pays moves back to the world pass.

| Case | Cascade CPU today | Cascade CPU cached | Frame | fps |
|---|---|---|---|---|
| still, 4 h day cycle | 9.35 | 2.1-4.1 | 31.9 → 26.2-28.1 | 31.3 → 35.5-38.2 |
| still, fixed sky | 9.35 | 2.0-4.0 | 31.9 → 26.1-28.0 | 31.3 → 35.7-38.4 |
| turning in place | 9.35 (est.: today redraws every frame) | 2.1-4.1 | as still | as still |
| walking (translation, assumed 3.2 m/s) | 9.35 | 2.1-4.1 | 26.2-28.2 | 35.5-38.2 |
| flying (assumed 16 m/s) | 9.35 | 2.7-4.6 | 26.7-28.7 | 34.9-37.4 |
| worst single moving frame (largest refresh + dynamic) | 9.35 | 5.4-7.4 | | |

**Against the fps floor:**

- **Still crowded: 60 fps (16.7 ms) is out of reach at Rooftops Ultra from the cascades alone.** Without any sun
  shadow the frame is 23.1 ms (43 fps); the remaining gap is the crowd (avatar update about 3.6 ms), deferred
  submission (about 8 ms) and Swap stalls (report §5).
- **Moving: the 25 fps floor (40 ms) holds today** (35.8 fps at the frozen moving heading), and the cache only
  lowers it.
- Dirty-driven refreshes (edits, loads, terrain/LOD per D5) are **not** in these numbers. C0 measures their rate.

### GPU

| Item | 1920x1080 (map 1440²) | 2560x1440 (1920²) | 3840x2160 (2880²) | Basis |
|---|---|---|---|---|
| Copy S → L, four whole maps | 0.080 ms | 0.134 ms | 0.299 ms | **measured**, RTX 5070 Ti, driver 621.14, `bench_sun_cascade_copy.py`, `GL_TIME_ELAPSED` median of 60; another process held about 39 % GPU at the start, so these are upper bounds |
| Copy, four half maps (view footprint only) | 0.035 | 0.071 | 0.150 | measured, same run |
| Depth clear, four maps (reference) | 0.015 | 0.022 | 0.035 | measured, same run |
| Copy on a mid GPU (about 360-450 GB/s) | 0.16-0.20 | 0.27-0.33 | 0.6-0.75 | **(est.)** bandwidth ratio; the measured copy runs at about 880 GB/s, the 5070 Ti's peak |
| Static caster raster | **saved** on frames without a refresh | | | baseline unknown: the Rooftops captures have no GPU zones (report §2) |
| Static refresh raster | about 1.6x a level's raster today, on refresh frames only | | | (est.), omni factor |
| Sun pass (lookup) | unchanged, plus one `length()` per pixel | | | (est.); `bench_shadow_stochastic.py` gates it in C1 |

### Memory

S maps (D24, 4 bytes per texel), main view only:

| Screen | Four levels | Levels 2-3 only (D6-B) |
|---|---|---|
| 1920x1080 | +33 MB | +17 MB |
| 2560x1440 | +59 MB | +30 MB |
| 3840x2160 | +133 MB | +66 MB |

## 7. Compatibility: what users see

- **Legacy:** nothing changes (byte-identical code; CI and bench gates).
- **Stochastic, hotfix H:** semi-transparent sun shadows keep their grain while the camera moves. Today the
  grain re-rolls.
- **Stochastic, C1:**
  - turning in place no longer changes shadow detail anywhere;
  - at the default view, shadows get 7-30 % sharper (finer texels);
  - zoomed in, shadows are coarser than today (D2);
  - the shadow distance edge becomes a circle around the camera instead of a plane across the view;
  - points at the screen's sides use the level of their distance (they used a finer one by view depth).

  Before/after evidence: model §1-§2 now; in-world turn and zoom captures at C1's gate.
- **Stochastic, C2:**
  - a static scene's maps are texel-identical to C1's;
  - avatars and moving objects cast every frame as today;
  - a static change (an edit, a door, an object loading) shows one frame later, because the feed is swapped at the
    start of the frame, as in the pool;
  - a moving sun's shadows step by at most half a texel every few seconds (inside the penumbra; the history
    smooths it).

## 8. Staged plan

Effort is calibrated against this repo's history:

- S1 landed as one commit (2026-11-08) and S2 within one day with its review (2026-11-11);
- projector pool P3 (change feed, hooks, classifier, LOD pin, scheduler, stats, tests) landed in one day
  (2026-11-14), and P4 (avatar layer and S-to-L copy) in one morning (2026-11-15);
- the dark gate #540 took one afternoon (2026-11-16).

The cache reuses P3/P4's feed and copy.

| Stage | Scope | Cost / saving | Gate: offline | Gate: in-world (owner) | Risk | Effort |
|---|---|---|---|---|---|---|
| **H** (hotfix, every line) | world-anchored dither origin for the sun alpha passes (§5.2) | 0 CPU (a few uniform sets per cascade), 0 GPU | a unit test that `k` is constant per world texel while walking (`sun_cascade_cache_model.py --check` has the model of it); `test_shadow_alpha_permutation.py`; Legacy check | glass and sheers while walking: the grain stays | low | 0.25 d per line |
| **C0** (devel only) | measurement: GPU timer zones for the four cascades; per-level counts of static vs avatar drawables; Tracy zone args for sort share; feed boxes per frame and their causes in the sun levels; MC1 Rooftops still + orbiting (camera re-armed per variant, 0f6c074e9e) | none (probes, devel only, kept out of release by `check_release_hooks.py`) | none | one capture session | low | 0.5 d + a session |
| **C1** | camera-position levels of fixed world size, radial selection in the Stochastic util, probes follow; renderer-free in `llshadowcascadefit.h` with tests | CPU about today (± the footprint change); GPU +1 `length()` per pixel; texel per §5.1 | `llshadowcascadefit_test.cpp`: radius rule, selection by distance, rotation invariance (same world point, many yaws: same level shares and world texel); the model's `--check`; `bench_shadow_stochastic.py`; Legacy gates | turn in place, zoom, walk, look at the shadow-distance edge | low-medium (image change; D2) | 0.5-1 d |
| **C2** | S/L per level, refresh triggers, one refresh per frame, wider dynamic set, region-shift translate, skip rule, stats (devel) | still Rooftops: cascade CPU 9.35 → 2.0-4.1 ms (est.), frame 31.9 → 26-28 ms; GPU +0.07-0.13 ms copy at 1920²; +59 MB | a scheduler harness on scripted camera paths, like the pool's FakeViewer: still → 0 static renders; 360-degree turn → 0; walk/run/fly within ±10 % of model §5; light motion as §5; a dirty box → one refresh of the levels it touches. Unit tests for the rules; Legacy gates | Rooftops MC1 still/orbit: cascade zones and per-level refresh counters; image: avatars, vehicles, doors, flexi and particles-free movers cast live in every level; region crossing; sunset on a day cycle | medium: dynamic-set completeness; visibility-stamp consumers (§10) | 1.5-2 d |
| **C3** | scissored partial refresh; light threshold and terrain/LOD rule tuned on C2's counts | refresh cost proportional to the changed area | harness: a 1 m door in level 3 redraws about (1 m / 23 cm)² texels, not the level | edit and rez in a busy sim while watching the frame time | low-medium | 0.5-1 d |
| **C4** (optional) | toroidal scrolling of coverage refreshes | only if C2 shows coverage refreshes dominate moving frames | wrap addressing in the util bit-identical to the unwrapped map; strip culling | fly across regions | medium | 1 d |

Total without C4: about 3-4.5 days of agent time, plus the owner's sessions. Each stage is a `codex/` feature PR
against `vkstorm-devel` (C0's probes stay on devel). H is a hotfix applied to every line.

## 9. Decisions for the owner

The recommended option comes first in each.

1. **D1, level geometry.**
   - **A:** all four levels camera-position-centred with S + L.
   - B: only levels 2-3 (Cyberpunk); levels 0-1 stay S1 and are drawn every frame (saves about 4 ms instead of about
     6-7 at Rooftops still, half the memory).
   - C: keep S1 and cache only while still.
2. **D2, zoom.**
   - **A:** fixed world sizes; zoomed views get 1.5-2.5x coarser texels than S1 gives at that field of view today
     (shadows stop depending on the lens).
   - B: radii follow the field of view in steps (zoom invalidates the cache; shadows depend on the lens again).
3. **D3, guard band.**
   - **A:** g = 0.125: texels 7-30 % finer than S1 at the default view; walking 0.13 refreshes a frame.
   - B: g = 0.25: texels within -15 % to +7 % of S1; half the refreshes.
4. **D4, dynamic set.**
   - **A:** everything that moves today casts live: avatars, attachments, animesh, active movers/physical, flexi,
     client spin, plus alpha texture animation if it is cheap to identify.
   - B: the pool's set (active movers stop casting: a regression).
5. **D5, terrain, grass and LOD rebuilds.**
   - **A:** they dirty the levels they touch (correct; C2 measures the rate, C3 adds a texel rule if needed).
   - B: the pool's rule, ignored until the age refresh (cheaper; stale terrain silhouettes, possible acne on hills).
6. **D6, memory.**
   - **A:** S for all four levels (+59 MB at 2560x1440, +133 MB at 4K).
   - B: levels 2-3 only (goes with D1-B).
7. **D7, light stepping.**
   - **A:** redraw when a caster at min(R, 40 m) would move half a texel (about every 1.2 s for levels 0-1 and
     3.6-6.7 s for 2-3 on a 4 h day; never with a fixed sky).
   - B: Cyberpunk: near levels every frame, far every few seconds.
   - C: one texel.
8. **D8, the dither hotfix (H).**
   - **A:** hotfix on every line now (devel, release, canary, rust), ahead of the stages, as the bug rule says.
   - B: fold it into C1 (devel first).

## 10. Risks, and what was not verified

- **No GPU baseline.** The Rooftops captures have no GPU zones, so the GPU saving of the cache is not known; C0
  adds timer zones. The copy cost was measured offline with another GPU load present (upper bound).
- **The avatar share of the sort (A2) is assumed.** It moves the still estimate between 2.0 and 4.1 ms. C0
  measures it.
- **The omni factor is an area proxy** (model §4). Scenes are not uniform: a camera at a sim edge or in a skybox
  has less behind it, and a city has more.
- **Change-feed rates in SL scenes are unknown**: edits, mesh loads, LOD and terrain rebuilds while walking. They
  decide how often S refreshes outside the camera and light triggers. The pool's own steady-state
  `rerender_causes` is also not measured in-world (projector-shadow-pool.md, "Not verified").
- **Today's turning cost is not measured.** The Rooftops moving captures had a frozen camera for sun and all (report
  §3). The orbit fix 0f6c074e9e is not yet captured.
- **Visibility stamps.** Objects seen only through the sun shadow get a visibility stamp only on refresh frames
  under the cache. Not checked: which consumers read it (texture priority of alpha-mask casters, deferred geometry
  rebuilds, flexi and animated-texture updates, impostor ranking). The dynamic pass keeps stamping; the static
  layer stamps only when it refreshes. C2 must list them by grep and test a shadow-only alpha-mask tree.
- **Dynamic-set completeness.** Not checked: whether flexi, client-spun children and moving parts of a linkset are
  active bridges (and so dynamic). The pool documents them as "cast with the pose of the last render" for lamp
  tiles; the sun must not inherit that.
- **Speeds and day length are assumptions.** Walk 3.2, run 5.1, fly 16 m/s; a 4 h EEP day.
- **Mid-GPU numbers are bandwidth estimates.** No second GPU was available.
- **Not read in full:** Kasyan 2013 and the Fortnite VSM blog (as in the literature report).
- **Not built or run in the viewer.** This is a study: no viewer build was made (one build at a time on the owner's
  PC).

## 11. Running the tools

```
python scripts/shadows/sun_cascade_cache_model.py           # the tables of sections 1-6 (seconds, no GPU)
python scripts/shadows/sun_cascade_cache_model.py --check   # invariants: rotation, dither anchor, S1 port
python scripts/shadows/bench_sun_cascade_copy.py            # GPU: copy and clear timings (takes the GPU bench lock)
```

#330's own model (`scripts/shadows/cascade_fit_model.py` on `codex/indoor-shadow-study`) still runs unchanged and
gives #330's Legacy tables; it needs numpy.
