<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Sun and moon shadow passes: what they cost and how to make them cheaper (study)

Status: study. Documents and an offline model only; no viewer code changes. Code references are file:line at
`origin/vkstorm-devel` a18403dd0d. Labels:

- **[measured]**: from a capture or a bench, with its source named.
- **[inferred]**: reasoning or a model on measured inputs. The assumptions are named (A1, A2, ...) in
  `scripts/shadows/sun_shadow_cost_model.py`.
- **[lit]**: a published technique, with its source.
- **[own idea]**: no published precedent found. These are kept apart in section 5.

This study builds on four inputs and does not redo them:

- the Rooftops split (`reports/rooftops-ultra-shadow-split-2026-11-16.md`, untracked in the main checkout);
- the multicore baseline and study (`doc/perf/multicore-baseline-2026-11.md`, `doc/perf/multicore-simd-study.md`);
- #542 (`doc/shadows/sun-cascades-refresh.md` on `codex/sun-cascades-refresh-2026-11-16`), the leading candidate;
- the OpenGL modernisation study of 2026-11-11 (G0-G7; G0 merged as #466, `doc/gl/instrumentation.md`).

It adds:

- `scripts/shadows/sun_shadow_cost_model.py` (the offline model) and `scripts/perf/tracy_cascade_zones.py` (the
  per-cascade attribution of a Tracy capture);
- new per-cascade measurements from the same Rooftops captures (section 2);
- the full option space, each option tied to its source (section 4);
- how the options combine and in what order (section 6);
- code findings in the shadow path (section 7);
- the capture list for the stage-1b session (section 9).

The literature is Real-Time Rendering, 4th ed. (RTR4), ch. 7 "Shadows" (§7.4 and §7.4.1, with their reference
numbers) and ch. 19 (§19.6 detail culling, §19.8 culling systems, §19.9 level of detail); GPU Gems 1-3; and the talks
and papers those cite. These are references only. No NVIDIA or Epic sample code was read or copied.

## 1. Summary

**What the sun/moon passes cost.** At Rooftops (Ultra, still, about 50 avatars) the four cascades cost 9.35 ms of
main-thread time. That is 8.9 ms net, or 28 % of the frame [measured, Rooftops split].

This study's new numbers from the same captures [measured]:

- **82 % of every draw call of the frame is in the four cascades:** 6,894 of 8,385 `pushBatch` calls.
- **The GL draw itself is cheap on this driver.** The whole `pushBatch` call, GL draw included, averages 0.11-0.17 µs
  in the cascades, 0.77 ms in all four.
- **The time goes to the work around the draws:**
  - building the render map and the sort, about 0.8 ms per cascade;
  - the cull, about 0.4-0.5 ms per far cascade;
  - the **alpha-blend caster pass**, 1.01 ms. It is larger than the opaque static draws in every cascade past the first;
  - avatars and rigged attachments, about 1.4 ms.

**Where this disagrees with earlier readings.**

- The multicore baseline's "5-10 ms of serial GL submission" included the projector pool's unzoned 5.4 ms (fixed
  since) and the CPU work inside the draw rows.
- The part that is really bound to the GL context is about 1 ms [inferred]. So shadow multi-draw-indirect (G6) acts on
  a smaller slice than the GL study's 10-16× suggests. Its 10-16× is a driver-side bench ratio, and the driver side here
  is under 1 ms.
- The cost is per-frame CPU work that repeats an unchanged answer. **Reuse across frames is the lever, as #542 says.**
  Nothing in the wider option space beats it.

**Ranked recommendations** (CPU saving at Rooftops still, from 9.35 ms of cascade time; each option alone;
section 4 has the detail):

| Rank | Option | Technique and source | Saving alone | Image change | Effort (calibrated) |
|---|---|---|---|---|---|
| 1 | **Static/dynamic cache on camera-position levels (#542 C1+C2)** | Clipmap levels (Tanner et al. 1998; GPU Gems 2 ch. 2); CSM scrolling (Day/Acton 2012, RTR4 [329]); "if nothing changes within a map's frustum, it need not be recomputed" (RTR4 §7.4.1); static maps plus a per-frame dynamic map (Flagship Studios, RTR4 §7.4.1) | **5.2-7.4 ms** [inferred, #542 model] | C1: level texels change, distance edge becomes a circle; C2: none for a static scene | 2-3 d (#542) |
| 2 | **Stage Q: code findings plus the alpha-blend loop hoist** | Pure efficiency, keeps the logic (section 7); state sorting and batching, RTR4 ch. 18 | 0.2-0.5 ms [inferred from measured 1.01 ms pass] | none (bit-exact: depth-only order is free) | 0.5 d |
| 3 | **Small-caster culling per level** | Remove tiny occluders (Persson 2012 [1381], Valient 2012 [1811], in RTR4 §7.4.1); detail culling (RTR4 §19.6) | 0.2-1.2 ms today; it also cuts the cache's refresh frames and the rigged attachments [inferred, A1-A2] | sub-texel casters leave the far levels (their shadow is under one texel already) | 0.5-1 d |
| 4 | **Parallel culls (multicore 5c)** | Data-parallel culling (multicore study §6.5) | 0.6-1.7 ms today; 0.3-0.6 ms under the cache [inferred, A4] | none | 2-4 d (multicore study) |
| 5 | **Skin palettes in one UBO (G3)** | Uniform buffers per draw range (AZDO, Everitt et al., GDC 2014; GL study P3) | 0.3-0.5 ms in the cascades, plus the main view [inferred, A5] | none | about 1 d |
| 6 | **Shadow MDI on arenas (G4-G6)** | Multi-draw-indirect (RTR4 §19.8; AZDO 2014); per-cascade single submission (GPU Gems 3 ch. 10 §10.3) | 1.3-2.3 ms today; 0.2-0.5 ms per frame under the cache, but a large cut of refresh frames [inferred, A6-A7] | none | 5-9 d with arenas (LL core: high upstream risk) |
| 7 | Probe faces read the main view's levels | **[own idea]** (section 5) | 0.34-0.40 ms [measured probe call] | probe shadows coarser | 0.5 d after C1 |
| — | Fewer levels, or a shorter shadow distance at Ultra | PSSM split count (GPU Gems 3 ch. 10); z-partitioning (RTR4 §7.4.1) | 0.5-1.2 ms (3 levels); 1.2-2.1 ms (128 m) [inferred] | coarser or shorter shadows | 0.25 d |

**Combined, in the recommended order** (Q → C1 → C2 → 5c → G3 → G4-G6), the cascades go from 9.35 ms to about
**0.8-3.1 ms** at Rooftops still [inferred, model 5a]. The frame goes from 31.9 to about 24.8-27.1 ms (31 → 37-40 fps).

Without the cache (C2 declined) the same CPU stages give 4.3-7.0 ms (model 5b), with no image change.

**Neither path reaches 60 fps at Rooftops Ultra.** The same frame with no sun shadow at all is 23.1 ms (43 fps). The
rest is the crowd's update, deferred submission and Swap stalls (multicore baseline). The fps floor at Rooftops needs
the multicore and GL programmes as well, not shadows alone.

**The GPU side is unknown.** Every number above is main-thread CPU with Tracy attached. Section 9 lists what the
stage-1b session must capture so that the GPU cost of each pass can be folded in.

**Proposed order:**

1. **1b session** (capture list, section 9).
2. **Q**: code findings, the alpha-blend hoist, small-caster culling. Small, bit-exact or near, and every saving stays
   after the cache.
3. **#542 C1, then C2.** Their decisions D1-D8 are with the owner.
4. **5c.** Its first target changes under the cache: the cascades' culls become small dynamic-only passes, so the
   world view becomes 5c's best first user.
5. **G3**, then the G-series as planned. G6 is not pulled ahead for shadows: under the cache it gains mainly on refresh
   frames.
6. Optional, on C2's measured refresh counts: C3/C4 (#542), the static caster mesh cache [own idea], single-pass
   levels for the dynamic layer.

**Owner decisions** are in section 10: the order (recommended above); the small-caster threshold; the tier defaults;
the moon; the probe faces; the impostor rule (Q9a of 2026-11-13 is not in the code, and doing it costs time); and the
G-series scope for shadows.

## 2. What the cascades cost today (Rooftops Ultra, still) [measured]

Source:

- the six Tracy captures `.captures/mc1-sl-20261116-165840/` (devel 2026-11-15 plus stage-1 zones), `RT-still__sun`,
  1,888 frames;
- per-event exports (`tracy-csvexport -u`), with each zone attributed to the `renderShadowImpl` that contains it inside
  the main `generateSunShadow` call.

The attribution tool is `scripts/perf/tracy_cascade_zones.py` (added here); `LLRenderPass::pushBatch` as a zone
name gives the draw calls. The outputs are in `.fixture-tmp/sun-cost-study/`. The table is the model's section 1
(`sun_shadow_cost_model.py`).

| ms per frame | cascade 0 | 1 | 2 | 3 | sum |
|---|---|---|---|---|---|
| total (cull + sort + draw) | 2.71 | 2.04 | 2.31 | 2.29 | **9.35** |
| `updateCull` | 0.07 | 0.38 | 0.50 | 0.46 | 1.41 |
| `stateSort` | 1.73 | 0.82 | 0.83 | 0.79 | 4.18 |
| of which mark visible + `rebuildMesh` | 0.14 | 0.31 | 0.23 | 0.21 | 0.90 |
| of which build render map | 0.09 | 0.32 | 0.39 | 0.37 | 1.17 |
| of which delayed `rebuildMesh` (first sort of the frame) | **0.85** | 0 | 0 | 0 | 0.85 |
| of which `rebuildPriorityGroups` (first sort) | **0.55** | 0.01 | 0.01 | 0.01 | 0.57 |
| opaque static (`pushBatches`) | 0.03 | 0.15 | 0.25 | 0.28 | 0.71 |
| **alpha-blend casters** (`renderAlphaObjects`) | 0.05 | 0.24 | 0.35 | 0.38 | **1.01** |
| alpha masked + material masks + grass | 0.14 | 0.16 | 0.14 | 0.14 | 0.58 |
| rigged (`pushRigged*Batches`) | 0.32 | 0.27 | 0.14 | 0.13 | 0.86 |
| avatar pool (`renderShadow`) | 0.46 | 0.05 | 0.04 | 0.04 | 0.59 |
| skin palettes (nested in rigged) | 0.20 | 0.10 | 0 | 0 | 0.30 |
| draw calls (`pushBatch`) | 371 | 1,562 | 2,354 | 2,607 | **6,894** |
| `pushBatch` zone time (the call, GL draw included) | 0.06 | 0.19 | 0.26 | 0.26 | 0.77 |

The two probe passes (the second `generateSunShadow` call, `gCubeSnapshot`) add 0.4 ms and 489 draws. All other passes
of the frame together issue 1,002 draws.

How to read it:

1. **Cascade 0 is mostly the frame's first-sort work.** The delayed mesh rebuild (0.85) and the priority groups
   (0.55) run in whichever pass sorts first. With shadows off the world pass pays them. That is the 1.5 ms "shared
   sort" of the Rooftops report, now located exactly (`postSort`, pipeline.cpp:3975-3995 and :4097-4114).
2. **Cascades 1-3 are each about 0.4-0.5 cull + 0.8 sort + 1.0 draw.** About two thirds of that is CPU work per caster
   that does not touch GL:
   - the octree walk;
   - mark-visible;
   - pushing every draw info into the render map (pipeline.cpp:4023-4041).
3. **The alpha-blend casters are the largest draw category in the far cascades.** `renderAlphaObjects`
   (pipeline.cpp:7781-7860) does the following per draw:
   - rebinds the program and sets four or five uniforms;
   - walks the whole alpha render map twice, once per rigged flag.

   Section 7, F4.
4. **The GL draw is cheap here.** 0.77 ms for 6,894 draws is about 0.11 µs each with the Tracy zone. The GL study
   measured 0.22 µs per depth-only draw in a single-threaded bench. The viewer runs with the NVIDIA driver's threaded
   optimisation, so the app thread queues rather than validates [inferred].
5. **Avatars and rigged attachments cost about 1.45 ms**, two thirds of it in cascades 0-1. Rigged meshes appear in
   every cascade: 0.13-0.14 ms in each far one.

**Where earlier numbers disagree, with the evidence.**

- The multicore baseline's "serial GL submission 5-10 ms" (`multicore-baseline-2026-11.md` §4.10) is
  `generateSunShadow` minus its culls and sorts. At Rooftops that rest held the projector pool's 5.4 ms of unzoned
  self time, which the hotfixes `13b7d28981` and `e74e5c85be` removed (multicore study §2 table). It also holds the
  draw rows' CPU work: render map iteration, texture binds, palette math.
- In the split capture the four cascades' draw rows are 3.87 ms. Of these, 0.77 ms is the draw call itself and
  1.45 ms is avatars. The part that no worker thread can take is therefore about 1-1.5 ms, not 5-10 [inferred].
- Consequence for the GL programme: shadow MDI (G6) saves real time, mostly by replacing the render map walk and
  per-draw state. It does **not** buy the 10-16× the bench ratio suggests at the frame level (section 4.5).
- #542's model: its inputs are the same per-cascade totals, so its cached figure (2.0-4.2 ms) stands. Its assumption
  A2 (the avatar share of the sort) is narrowed by the table above. The sort in cascades 1-3 is mostly static content
  (render map and mark visible), and the avatar share there is small. That favours the low end of #542's range
  [inferred].

## 3. How it works today (Stochastic method), file:line

1. `LLPipeline::generateSunShadow` (pipeline.cpp:12779) runs on the main thread, from three places:
   - the main view, `llviewerdisplay.cpp:954`;
   - each reflection-probe face (`display_cube_face`, `llviewerdisplay.cpp:1376`, with `gCubeSnapshot` and the
     auxiliary pack, two cascades with stretched splits, :12956-12961). The probes update from `llappviewer.cpp:1803`,
     after the main view; a realtime probe (`RenderReflectionProbeDetail` 2) renders six faces a frame
     (`llreflectionmapmanager.cpp:470-481`);
   - each hero-probe face, before the main view (`llviewerdisplay.cpp:853`, `llheroprobemanager.cpp:284-292`,
     6/rate faces a frame), only with `RenderMirrors` (off in every feature-table tier, `featuretable.txt:392`).
2. Render types are narrowed (:12805-12873). The caster direction is the primary light (:12891-12892). The splits come
   from `RenderShadowDistance` (Ultra 192 m, `featuretable.txt:385`), snapped, exponent 3 (:12943-12952). Then come the
   dark gate (#540) and the hidden-shadow branch (:12974-12992).
3. `generateSunCascadesStable` (:12573-12776), per cascade:
   - the slice corners (:12607-12620);
   - the sphere and the snapped box (:12654-12669);
   - the cull box narrowed to the slice (:12682-12704);
   - bind and **clear** (:12742-12744);
   - the world-texel dither origin (hotfix N1, :12751-12755);
   - `renderShadow` with a **static `LLCullResult result[4]`**, shared with the probe passes (:12757-12758).
4. `renderShadowImpl` (:11627-11852), in order:
   - occlusion off (:11635-11636);
   - `updateCull` and `stateSort`, with `postSort` (:11664-11677);
   - opaque: 13 pass types × {static, rigged} plus GLTF (:11704-11732);
   - `renderGeomShadow`: avatar pools, terrain, trees (:11741-11745, pools at :4721-4766);
   - alpha: masked, **blend** (`renderAlphaObjects`), fullbright masked, grass, four material masks, GLTF masked,
     each × {static, rigged} (:11747-11835).
5. Culling: `LLOctreeCullShadow` (`llspatialpartition.cpp:1142-1157`, chosen at :1472-1476). It tests frustum boxes
   only. **There is no size test.** The main view's pixel-area cut is explicitly skipped for shadow passes
   (`lldrawable.cpp:1590-1600`).
6. Avatars:
   - `LLDrawPoolAvatar::renderShadow` (`lldrawpoolavatar.cpp:358-419`). **Impostored avatars cast no sun shadow**, and
     neither do their attachments (:388-399; pipeline.cpp:3256). At Ultra only 16 avatars are not impostors
     (`featuretable.txt:366`).
   - Rigged attachments draw through `pushRiggedBatches`, which uploads each palette per pass (`lldrawpool.cpp:424-547`).
7. Maps: four `GL_DEPTH_COMPONENT24` maps, square `sqrt(w*h)` (`allocateShadowBuffer`, pipeline.cpp:1187-1215; 1920² at
   2560x1440). There is no colour attachment (`llrendertarget.cpp`, `addColorAttachment(0)` returns at once). The
   maps are released in `cleanup` (pipeline.cpp:850, `releaseShadowBuffers` :1544).

Every thread: all of the above runs on the main thread, with one GL context. No worker calls `generateSunShadow`,
`renderShadowImpl`, `allocateShadowBuffer` or the release paths (grep of the callers above). The texture worker
(`cglt - sync`) and the mesh threads do not enter the shadow path.

## 4. The option space

Each option has the same entries:

- **source**: the technique and where it is published;
- **here**: how it maps to our code;
- **saving**: CPU and GPU, at Rooftops still unless stated;
- **image**: what users would see change;
- **effort**: calibrated against this repo's history;
- **upstream risk**: how likely it is to conflict with upstream Firestorm/LL changes;
- **interactions**: with #542, the G-series and 5c.

Effort calibration from git history:

| Piece | Commits | Time |
|---|---|---|
| S1 stable cascades | one commit (9d99398c7c) | one day |
| S2 | — | within a day |
| projector pool P3 (feed, hooks, classifier, scheduler) | — | one day |
| P4 (avatar layer, copy) | — | one morning |
| #540 dark gate | 6d409c8e07 to 191c9985b9 | one afternoon (merged 18:55) |
| N1 dither hotfix | 6a9c4745cb to a18403dd0d | 21:32-21:53 |
| stochastic transparency port | — | about two days |

Figures below are pipeline days, not calendar days. They leave out the stabilisation tail, which for a new subsystem
in this repo has been one to three hotfixes (P1-P4).

### 4.1 Reuse across frames

**4.1.1 Static/dynamic cache on camera-position levels (#542 C1+C2).** Recommended, rank 1.

- **Source [lit]:**
  - RTR4 §7.4.1, "Efficiency": a map whose frustum saw no change need not be recomputed.
  - RTR4 §7.4.1 cites Flagship Studios' split into a per-frame map for nearby dynamic objects and once-generated
    static maps.
  - Clipmaps: Tanner, Migdal, Jones, SIGGRAPH 1998; Asirvatham and Hoppe, GPU Gems 2 ch. 2.
  - Day/Acton, "CSM Scrolling", SIGGRAPH 2012 [329].
  - DOOM (2016) regenerates only the maps where something moved [294].
- **Here:** #542 §5 in full. The sun's change feed reuses the projector pool's `LLShadowDirtyFeed`.
- **Saving:** 9.35 → 2.0-4.2 ms still, the same while turning [inferred, #542 §6].
  - Section 2 narrows #542's A2 toward its low end, because the far cascades' sort is mostly static.
  - Worst single frame: 5.4-7.4 ms with one scheduled refresh.
  - A refresh-all frame: 10.7-12.6 ms (teleport, gate reopening).
  - GPU: the S → L copy is 0.13-0.26 ms at 1920² [measured, #542 bench]; the static raster is saved on frames without
    a refresh.
- **Image:** #542 §7.
- **Effort:** 2-3 d (C1 + C2) plus a hotfix tail.
- **Upstream risk:** medium. The changes sit in `pipeline.cpp` regions Vulkanstorm already owns, and in the feed's
  hooks.
- **Interactions:** this subsumes per-level update rates for static content, makes 5c's cascade target small, and
  moves G6's value to refresh frames.

**4.1.2 Lower update rate for the far levels' dynamic layer.**

- **Source [lit]:** RTR4 §7.4.1, "more distant maps updated less than once a frame (risk with large moving objects)",
  citing Kasyan 2013 [865], Pesce 2011 and 2012 [1389, 1391], and Crytek [1678, 1679]. Cyberpunk 2077 refreshes its
  distant levels every few seconds (Dybisz and Witanowski, SIGGRAPH 2021).
- **Here:** after C2, levels 2-3 draw their dynamic layer (avatars beyond about 25 m, active movers) every second
  frame, and copy S → L only on those frames.
- **Saving:** the far levels' dynamic share. Measured rigged + pool in cascades 2-3 is 0.35 ms, plus their dynamic
  culls, so about 0.2-0.4 ms [inferred].
- **Image:**
  - far avatar shadows lag one frame (25-33 ms at the floor);
  - the temporal resolve (1/8) smooths it;
  - a fast vehicle at 30 m steps visibly.
- **Effort:** 0.25 d inside C3.
- **Verdict:** a C3 tuning knob, decided on C2's numbers. It is #542's D7-B applied to the dynamic layer only.

**4.1.3 Scrolling (#542 C4).**

- **Source [lit]:** Day/Acton 2012 [329]; toroidal addressing, GPU Gems 2 ch. 2.
- Unchanged from #542: optional, and only if coverage refreshes dominate while flying.

**4.1.4 A high-resolution static map instead of the far cascades.**

- **Source [lit]:** RTR4 §7.4.1, citing Rainbow Six Siege [415] and Ryse [1590].
- **Here:** C2's cached level 3 is this already, kept up to date by the feed. A separate baked map would need a fixed
  sun, and the sun moves on every EEP day cycle. **Not pursued separately.**

### 4.2 Caster culling

**4.2.1 Small-caster culling per level.** Recommended, part of stage Q.

- **Source [lit]:**
  - RTR4 §7.4.1, "remove tiny occluders", citing Persson, "Creating Vast Game Worlds" (Avalanche), SIGGRAPH 2012 [1381]
    and Valient, "Shadows + Games: Practical Considerations", SIGGRAPH 2012 [1811];
  - RTR4 §19.6, detail culling: drop objects whose projection is below a size threshold. Here the projection is into the
    level's texels, not the screen.
- **Here:**
  - A caster whose bounding diameter is below **one texel of its level** is not drawn into that level. At 1920² and
    192 m that is 0.7 / 4.2 / 14.2 / 33.4 cm for levels 0-3 (model section 3).
  - Two places, both gated to the Stochastic sun cascades:
    1. in `LLOctreeCullShadow::frustumCheckObjects` (`llspatialpartition.cpp:1153`), a group whose object extents are
       below the threshold is rejected. This saves cull, sort and draw;
    2. in `postSort`'s render-map build (pipeline.cpp:4031-4041), under `sShadowRender`, a non-rigged `LLDrawInfo` whose
       `mExtents` are below the threshold is not pushed. This saves the draw.
  - Rigged attachments are tested by the attachment drawable's radius at `markVisible`.
  - The threshold is a pure function in `llshadowcascadefit.h`, with a unit test.
  - Level 0 is exempt.
  - The avatar body is never culled.
  - Projector tiles are not affected (their pass flag differs).
- **Saving:** 0.2-1.2 ms today, depending on how many of the 6,523 draws in levels 1-3 are that small (A1 10-30 %, A2
  0-50 %; model section 4).
  - It also shrinks every C2 refresh and the rigged-attachment part of the dynamic layer: rings, nails, eyelashes,
    small jewellery.
  - GPU: fewer tiny triangles, which rasterise worst [inferred].
- **Image:**
  - The casters removed throw a shadow under one texel of the level, which today appears as a single snapped texel
    that blinks as the box moves by a texel.
  - Removing it is stabler, not worse.
  - The 0.25 m filter footprint and the 8 rays already average over 4-36 texels in levels 0-1, so a sub-texel caster
    contributes almost nothing there [inferred].
  - Risk: a lattice of many thin casters (a fence of 3 cm rails in level 2) loses a shadow it should have as a whole.
    That is why the threshold is per object and is a debug setting (`RenderShadowSmallCasterTexels`, default 1.0,
    0 = off), not a Preferences entry.
- **Effort:** 0.5-1 d.
- **Upstream risk:** low to medium (two LL functions, a few lines each).
- **Interactions:**
  - C1 changes the level texels (finer at the default view). The threshold follows the level, so the rule carries
    over.
  - C2 refreshes get cheaper.
  - It adds nothing to G6.
- **Needs from 1b:** a histogram of draw-info sizes per level (section 9, item 7).

**4.2.2 Receiver-aware culling.**

- **Source [lit]:**
  - RTR4 §7.4 (tighten the light frustum to visible receivers, King 2004 [896], Valient 2014 [1812]);
  - Bittner, Mattausch, Silvennoinen, Wimmer, "Shadow Caster Culling for Efficient Shadow Mapping", I3D 2011 [152];
  - Mattausch et al., GPU Pro 3 [1137].
- **Here:** S1 already culls each cascade to its view slice plus the filter margin (pipeline.cpp:12682-12704), the
  geometric half of this.
- The full method marks visible receivers from the camera's depth buffer. That has three problems:
  - it makes the caster set depend on what the camera sees;
  - it breaks C2's omni static layer;
  - it needs the depth before the shadow pass, which is drawn after it here (or last frame's, reprojected, which gives a
    one-frame miss on disocclusion).
- **Rejected for the sun** under the camera-independence rule. #542 keeps the slice bound only for the dynamic pass.

**4.2.3 Occlusion culling from the light.**

- **Source [lit]:**
  - Bittner and Wimmer, "Hardware Occlusion Queries Made Useful", GPU Gems 2 ch. 6;
  - RTR4 §19.7;
  - Haar and Aaltonen, "GPU-Driven Rendering Pipelines", SIGGRAPH 2015 [625] (30-80 % of shadow triangles culled
    with a reprojected depth mask).
- **Here:** under C2, a dynamic caster fully behind the cached static depth (an avatar indoors, seen from the sun)
  could be skipped with a box query against S and `glBeginConditionalRender`. That is same-frame and needs no
  read-back.
- **Saving:** GPU raster only. The CPU submission stays, and the frame is CPU-bound, so close to 0 now [inferred].
  Read-back of last frame's result would save CPU but drops a shadow for a frame when the avatar steps out.
- **Verdict:** **Low.** Re-evaluate only if 1b shows the cascades GPU-bound. Haar and Aaltonen's GPU culling is compute:
  Vulkan line only.

**4.2.4 One cull for all levels.**

- **Source [lit]:** Zhang, Sun, Nyman, "Parallel-Split Shadow Maps on Programmable GPUs", GPU Gems 3 ch. 10, §10.3:
  each object's first and last split are found once, and it is drawn only into those.
- **Here:** today there are four octree walks with four camera IDs (`sCurCameraID`, the visibility stamps per
  camera). One walk against the union box, binning each group by the levels its box overlaps, would replace them.
- **Saving:** 0.6-0.9 ms of the 1.41 ms cull [inferred].
- **Interaction:** it overlaps 5c, which parallelises the same walks. Under C2 the static walk runs only on refreshes.
- **Verdict:** **not recommended separately.** Fold the binning idea into C2's refresh and into 5c's job layout.

### 4.3 LOD and simpler shadow geometry

**4.3.1 Shadow LOD bias.**

- **Source [lit]:** RTR4 §7.4.1, "LOD proxies cast shadows", citing Cars 2 [652] and Killzone Shadow Fall [1812];
  RTR4 §19.9; Cyberpunk 2077 swaps character meshes in shadows (2021).
- **Here:** a drawable's LOD is chosen from the main camera's distance. Each LOD's geometry lives in its own vertex
  buffers, rebuilt on change. Two consequences:
  - for the sun, the far levels already hold distant objects at the low LODs the main view picked by distance, which is
    the metric a light-derived LOD would use. Little is left to gain;
  - a separate shadow LOD would need a second geometry set per drawable: memory, rebuild time, and self-shadow acne
    where caster and receiver LODs differ (RTR4 §7.4 bias discussion).
- **Verdict:** **not recommended for the sun** on OpenGL. It belongs with instanced geometry (GL study G5b) or the
  Vulkan line.

**4.3.2 Alpha casters.**

- **Source:** the alpha-blend pass is the Stochastic method's own transmittance dither, with no published counterpart
  for cost.
- **Here:** the measured 1.01 ms is mostly per-draw state rebinding (section 7, F4). The fix is code hygiene
  (stage Q). Under C2 nearly all alpha-blend casters are static and move into refreshes.
- Drawing far alpha-blend casters as opaque would be cheaper but changes their shadow: no source, so not proposed.

**4.3.3 Avatars and attachments.** The per-frame cost the cache cannot remove. Epic's VSM documentation: skeletal
deformation "always invalidates cached pages".

- **Skin palettes in one UBO (G3).**
  - **Source [lit]:** Everitt, McDonald, Riccio, Hart, "Approaching Zero Driver Overhead in OpenGL", GDC 2014; GL
    study P3.
  - **Saving:** the rigged batches are 0.86 ms in the cascades and palettes are uploaded per pass. 0.3-0.5 ms [inferred,
    A5], plus the main view and probes.
  - **Image:** none (same floats, std140 stride 48).
  - **Effort:** about 1 d (one shader include, three upload overloads).
  - Recommended (rank 5).
- **Skin once, reuse in every view (transform feedback).**
  - **Source [lit]:** stream output to reuse transformed vertices across passes (RTR4 ch. 3, the stream-output stage);
    the GPU skin caches of shipped engines use compute (repo crowd report).
  - **Here:** the emitter plan's P3b, transform feedback being a graphics-pipeline feature that the OpenGL rules allow.
  - **Saving:** GPU vertex work in cascades 0-1, the main view and the lamp layers. CPU only through fewer palette
    uploads, which G3 already takes.
  - After G3, on 1b's GPU numbers.
- **Impostors.** They cast no sun shadow today (section 3.6). Shadow impostors break the silhouette rule (repo crowd
  report). Not proposed.
- **Small rigged attachments:** covered by 4.2.1.

### 4.4 Cascade count, resolution and splits

- **Source [lit]:**
  - Zhang et al., GPU Gems 3 ch. 10 (split count, practical split scheme);
  - RTR4 §7.4.1 (z-partitioning, Eq. 7.5; "each slice 2-3× the depth range of the previous").
- Texel per level at 1920², 60 degrees, 16:9 (model section 2) [port of `llshadowcascadefit.h`]:

  | Distance | Levels | Texel per level (cm) | Far edges (m) |
  |---|---|---|---|
  | 192 (Ultra) | 4 | 0.70 / 4.17 / 14.2 / 33.4 | 4.1 / 24.2 / 81.1 / 192 |
  | 192 | 3 | 1.56 / 10.0 / 33.4 | 9.2 / 57.1 / 192 |
  | 160 (High Ultra) | 4 | 0.60 / 3.54 / 11.7 / 28.4 | 3.4 / 20.2 / 67.6 / 160 |
  | 128 (High) | 4 | 0.50 / 2.81 / 9.17 / 21.7 | 2.8 / 16.2 / 54.1 / 128 |

- **Three levels instead of four.**
  - Level 0 costs 1.2 ms net of the shared sort, about half of it avatars that move into the wider level 1.
  - **Saving:** 0.5-1.2 ms [inferred].
  - **Image:** the near level's texel goes from 0.7 to 1.6 cm and level 1's from 4.2 to 10 cm. Contact shadows of
    small things soften visibly.
  - **Not recommended.** Level 0 is cheap once the shared sort is moved. Under C2, level 0's static part is cached
    anyway.
- **Shorter shadow distance at Ultra (192 → 128 or 160 m).**
  - **Saving:** 1.2-2.1 ms at 128 m [inferred, A9].
  - **Image:** shadows end at 128 m; the texels get finer.
  - A tier-default change. Owner decision D3, with the recommendation to keep 192 m once C2 lands. Under the cache, the
    far level costs only its refreshes.
- **Resolution.** It changes GPU time only (raster, copy, clear). The CPU is unchanged. Decide on 1b's GPU numbers.

### 4.5 Submission

**4.5.1 Shadow multi-draw-indirect on geometry arenas (G4-G6).**

- **Source [lit]:**
  - RTR4 §19.8 (indirect draw commands in culling systems);
  - Everitt et al., AZDO, GDC 2014;
  - GPU Gems 3 ch. 10 §10.3 (one submission per split);
  - the GL study's bench: arena + `glMultiDrawElementsIndirect` 10-16× on the depth-only submission loop, image
    identical [measured, 2026-11-11, RTX 5070 Ti].
- **Here:** GL study §4.6: a draw list (G4), arenas with base vertex (G5), MDI per cascade with matrices in an SSBO
  (G6). Depth-only order is free (GL_LESS, min depth), so every draw can merge.
- **Saving [inferred, A6-A7]:**
  - today: 1.3-2.3 ms. The static opaque draws are 0.71 ms; the alpha, masked and grass passes need their texture per
    draw, so they merge only per texture run (1.6-3.4× in the bench). The render-map walk is replaced by the draw list;
  - under C2: 0.2-0.5 ms per frame (the dynamic layer, rigged after G3). The **refresh frames** gain most: a level
    refresh is a static draw of thousands of casters.
  - GPU: +2-3 % on the passes [measured, GL study].
- **Image:** none (bit-exact, compare harness of G0).
- **Effort:** the GL study gave 2-3 weeks per stage in human terms. Calibrated against this repo, about 5-9 pipeline
  days for G4+G5+G6.
- **Upstream risk:** **high.** `llvertexbuffer`, `lldrawpool` and `llvovolume` are LL core, and canary's syncs touch
  them.
- **Interactions:** C2 lowers its shadow value; it serves the main view (G7) regardless. Keep the G-series order;
  do not pull G6 forward for shadows.

**4.5.2 State sorting and depth-only programs.**

- **Source [lit]:**
  - RTR4 ch. 18 (pipeline optimisation: sort by state, minimise program and texture changes);
  - vendor guidance for depth-only passes: no fragment shader, or one that writes nothing.
- **Here:**
  - the opaque passes already bind one program per {static, rigged} (pipeline.cpp:11704-11732);
  - the alpha-blend pass does not (F4);
  - the opaque fragment shader writes a colour to a target with no colour attachment (`shadowF.glsl`, F3).
- **Saving:** CPU, F4's 0.2-0.5 ms. GPU: a program without colour output may let the driver skip the fragment stage
  for opaque casters; unknown until 1b [inferred].
- **Image:** none.
- **Effort:** inside Q.

**4.5.3 Single-pass rendering of several levels for the dynamic layer.**

- **Source [lit]:**
  - RTR4 §7.4.1, "single-pass multi-map rendering": geometry-shader replication (Frostbite, Andersson and Johansson,
    GDC 2009 [41]) and instanced layered rendering into depth arrays (Rákos, "Massive Number of Shadow-Casting Lights with Layered Rendering", OpenGL Insights 2012 [1456]);
  - GPU Gems 3 ch. 10 §10.3 (geometry-shader cloning).
- **Here:** after C2, the avatars drawn into levels 0 and 1 (and 2) each frame could be drawn once, instanced per level.
  This needs:
  - the four maps as one `GL_TEXTURE_2D_ARRAY` with layered rendering;
  - `gl_Layer` from a geometry shader (core since 4.0: no extension, no fallback path);
  - the Stochastic lookup reading an array (a shader change, Legacy untouched).
- **Saving:** the dynamic layer's repeated per-level submission, about 0.2-0.5 ms [inferred].
- **Drawback (RTR4):** all occluders go to all maps. For avatars, which overlap levels 0-1 anyway, it is small.
- **Effort:** 1-2 d.
- **Verdict:** optional, after C2 and G3, on the 1b GPU numbers. Geometry-shader cost on AMD/Intel is unmeasured.

### 4.6 Parallel cull and sort (multicore 5c)

Not redesigned here; see `multicore-simd-study.md` §6.5.

- **Saving:** today 0.6-1.7 ms. That is the culls (1.41 ms) plus whatever part of the sort becomes parallel. The sort
  writes the shared draw pools, and its `rebuildMesh` makes GL calls, so it is main-thread work until Phase B's
  sort split [inferred, A4, A4b].
- **What changes under #542:** the per-frame cascade culls become dynamic-only (avatar and control-avatar partitions,
  active bridges), and the static walks run only on refreshes. **5c's "cascades first" target shrinks** to about
  0.3-0.6 ms, and the world view (cull + sort 1.3-1.8 ms, multicore baseline) becomes the better first user. The
  refresh frames still profit.
- **Recommendation:** keep 5c's design. Decide its first target after C2's numbers. If C2 is declined, cascades first,
  as planned.

### 4.7 GPU side

**Unknown: there are no GPU zones yet** (Rooftops split §2). Signs from the waits:

- `Swap` does not grow with shadows (5.1 ms none, 4.5 sun);
- stall frames make up 13-19 % with or without the cascades;
- so the cascades are CPU-first.

What could matter once the CPU drops, each to be decided on 1b:

| Item | Today | Lever | Source |
|---|---|---|---|
| Clears | four D24 clears a frame, 0.022 ms at 1920² [measured, #542 bench] | none needed; F5 removes the hidden-shadow per-frame clear | — |
| S → L copy (C2) | 0.13-0.26 ms at 1920² [measured, #542 bench] | half maps (view footprint) 0.07 ms | #542 §6 |
| Caster raster | unknown | small-caster cull (4.2.1); depth-only program (4.5.2); D16 maps halve the bandwidth for raster, clear and copy (Infinite Warfare keeps static maps in D16, Drobot 2017) | RTR4 §7.4 precision; Drobot 2017 |
| Overdraw | unknown | opaque before masked (already so); front-to-back within the opaque pass helps only if fragment-bound | RTR4 ch. 18 |
| Sun lookup in the lighting pass | unknown (8 rays × 2-8 steps) | not a shadow-pass cost; capture it to separate it | `shadowStochasticUtil.glsl` |

D16 changes depth precision. Level 3's range of about 3R ≈ 600 m gives 0.9 cm steps against a 33 cm texel. It is not
bit-exact, so it needs an owner decision, and only if 1b shows bandwidth matters.

### 4.8 Moon nights

- Today a moonlit night costs exactly what a day costs. #540 skips the cascades only below the visibility threshold
  (E < 5e-6, pipeline.cpp:12974-12987).
- **Source [lit]:**
  - RTR4 §7.4.1, "many lights": spend shadow work by the light's contribution;
  - Call of Duty: Black Ops Cold War scales each light's shadow resolution by importance under a budget (Myers 2021,
    in the repo's literature report).
- **Options:**
  - (a) **as the day (recommended once C2 lands).** The moon moves at the sun's rate, so the cache works the same way;
  - (b) a reduced moon: levels 0-2 only, or half the shadow distance, when the primary light is the moon. That saves
    25-45 % of the cascade time at night [inferred]. Moon shadows end at 81 m, or keep 4 levels over 96 m.
- **Effort:** 0.25 d (one branch on `primary_light`).
- **Verdict:** a decision (D4). Before C2 it is a real night-time saving. After C2 it is small.

### 4.9 Probe and hero-probe shadow passes

- **Reflection probes:**
  - each probe face renders two cascades of its own: 0.4 ms and 489 draws a frame at Ultra [measured];
  - a realtime probe (`RenderReflectionProbeDetail` 2, not a tier default) renders six faces a frame, which is twelve
    shadow passes.
- **Hero probe (mirrors):** 6/rate faces a frame, two passes each, into `mHeroProbeRT`. Off in every tier. The mirrors
  study (2026-11-16) is replacing hero probes, so no change here.
- **Literature:** no published method for sharing a camera's cascades with its probes was found. The option is in
  section 5.

## 5. Own ideas (no published precedent found; labelled)

**5.1 Probe faces read the main view's camera-position levels (after C1).**

- After C1 the main levels are omni squares around the camera position, holding casters within 1.25 d of the camera
  in every direction.
- A reflection probe near the camera (they are distance-sorted and updated nearest first) could sample them instead of
  drawing its own two cascades:
  - no cull, no sort, no draw;
  - one bound set of maps and matrices.
- **Saving:** 0.34-0.40 ms and 489 draws a frame [measured probe call]. Under C2 the probe would also see the cached
  static layer at no cost.
- **Image:**
  - probe reflections get the main levels' texels (finer than the probe's own 2-cascade fit);
  - a probe farther than level 3's reach from the camera gets no shadow beyond it. The probe's own passes reach 1.5 d.
    The fallback is the probe's own passes for those probes.
- **Effort:** 0.5 d.
- **Risk:** the lookup of the probe programs (forward probe shaders) must take the main matrices. The `mSunShadowMatrix`
  array is shared today, and the probe pass overwrites entries 0-1 (pipeline.cpp:12738). The probe update runs after
  the main view (`llappviewer.cpp:1803`), so the order is safe, but it is fragile.
- **Literature anchor:** the closest is RTR4 §7.4.1's static maps "generated once" and reused (Flagship Studios). No
  source shares cascades with probes.

**5.2 A static caster mesh per level for light-step refreshes (after C2).**

- Under C2 the most frequent static refresh on a day cycle is the light trigger: every level, about every 1.2 s on a
  4-hour day (#542 D7). On a light refresh the caster set is unchanged; only the light matrix moves.
- At a level's refresh, the opaque static casters (non-rigged, non-masked; their positions are already baked into
  agent space) could be copied once into a position-only buffer with one index list. A light-only refresh is then one
  draw per level instead of 1,500-2,600.
- **Literature anchor:** precomputed caster lists per light (Pettineo, "Rendering the Alternate History of The Order:
  1886", SIGGRAPH 2015 [1405], RTR4 §7.4.1). Merging the casters into one private position buffer is our variant.
- **Saving:** light refreshes become nearly free. The worst moving frame drops by a level's static draw.
- **Cost:** memory. Level 3 may hold millions of triangles; at 12 B a vertex plus indices, tens of MB. Cap it by level,
  or levels 2-3 only.
- **Upstream risk:** low (no LL buffer changes).
- **Verdict:** decide on C2's measured refresh causes. If the light trigger dominates, this beats G6 for the sun, at a
  fraction of the effort (1-2 d) [inferred].

**5.3 Reduced moon (4.8 b).** Partly anchored in Myers 2021's importance scaling; the moon-specific rule is ours.

## 6. How the options combine, and the order

Overlap and dependencies:

| | Q (code + small casters) | C1 | C2 | 5c | G3 | G4-G6 | 5.1 probes | 5.2 static mesh |
|---|---|---|---|---|---|---|---|---|
| depends on | — | — | C1 | FrameJobs (stage 2) | — | G3 for rigged; arenas | C1 | C2 |
| per-frame saving after C2 | small casters: rigged attachments; F4 for dynamic alpha | — | — | dynamic culls only | full | dynamic layer only | full | none |
| refresh-frame saving after C2 | full | — | — | full | — | full | — | light refreshes |
| upstream risk | low-medium | medium | medium | high (multicore study) | medium | high | low | low |

Cumulative, Rooftops still, cascade CPU (model section 5, [inferred]):

| Stage | Cascade CPU (ms) | Frame (ms) | fps |
|---|---|---|---|
| today | 9.35 | 31.9 | 31.3 |
| Q | 7.66-8.92 | 30.2-31.5 | 31.8-33.1 |
| C1 | same | same | same |
| C2 | 2.00-4.20 | 26.0-28.2 | 35.4-38.4 |
| 5c | 1.74-3.62 | 25.8-27.7 | 36.1-38.8 |
| G3 | 1.23-3.37 | 25.3-27.4 | 36.5-39.6 |
| G4-G6 | 0.76-3.07 | 24.8-27.1 | 36.9-40.3 |

The frame column includes the 1.5 ms shared sort, which the world pass pays again once the cascades stop sorting
static content (#542 A5).

Without C2 (no image change):

| Stage | Cascade CPU (ms) | Frame (ms) | fps |
|---|---|---|---|
| Q | 7.66-8.92 | 30.2-31.5 | 31.8-33.1 |
| 5c | 6.14-8.33 | 28.7-30.9 | 32.4-34.8 |
| G3 | 5.63-8.08 | 28.2-30.6 | 32.7-35.5 |
| G4-G6 | 4.33-6.98 | 26.9-29.5 | 33.9-37.2 |

Why this order:

1. **Q first.** It is small, it is needed in either path, it reduces the refresh cost the cache will pay, and its
   cleanups make C2's diff smaller. Section 7 lists its contents.
2. **C1 then C2.** C2 is the largest saving by a wide margin (5-7 ms). Its decisions are #542's D1-D8.
3. **5c after C2.** Its target is decided on C2's numbers (4.6).
4. **G3** helps every view, shadows included, at low risk.
5. **The G-series continues on its own track.** Under C2, G6's shadow value is mostly the refresh frames. 5.2 may serve
   those cheaper.

Against the floor: the model's end point at Rooftops Ultra is 37-40 fps still. Moving is unmeasured (the Rooftops
moving captures had a frozen camera). Under C2, turning costs what standing costs. The 1 % low (13 fps, multicore
baseline) is dominated by load hitches and the sort's p95 (10-29 ms). The cache removes most of the per-frame sort, so
it should cut the tail too. That needs 1b and C0 numbers.

## 7. Code findings in the shadow path

Owner rule: report every bug, leak, inefficiency, overengineering or cruft met in the sun/moon shadow path; prune
dead code only with proof, including by name.

**Hotfix candidates (bugs):** none proven. F11 is a behaviour against the owner's camera-independence rule, inherited
from upstream, not a regression. Its fix adds cost, so it is a decision (D6), not a hotfix.

| # | Label | Where | Evidence | What it should become | Stage |
|---|---|---|---|---|---|
| F1 | **dead code** | pipeline.cpp:11734-11738, `if (LLPipeline::sUseOcclusion > 1) doOcclusion(shadow_cam);` in `renderShadowImpl` | The same function sets `sUseOcclusion = 0` at :11636 and restores it at :11850. Every writer of `sUseOcclusion` is outside the render path: the `UseOcclusion` settings handler (`llviewercontrol.cpp:540`), `refreshCachedSettings` (pipeline.cpp:1352), the display and snapshot paths (`llviewerdisplay.cpp:912, 1052, 1365, 1396, 1487, 1542`; `llviewerwindow.cpp:6672`; `llfloater360capture.cpp:489, 622`) and `LLDisableOcclusionCulling` (pipeline.cpp:12066-12081, used around `generateSunShadow`). None is reachable between :11636 and :11734 (the cull, sort and opaque draws make no settings change). The branch is unreachable | delete the branch (LL code, a five-line upstream diff) | Q |
| F2 | **dead code** | pipeline.cpp:11694-11700, `struct CompareVertexBuffer` | a local type, never referenced in the function (it cannot be referenced elsewhere) | delete | Q |
| F3 | **cruft** | pipeline.cpp:11711-11717 (`if (shadow_detail <= 2) setColorMask(false, false)`, "if not using VSM"); `shadowF.glsl` writes `frag_color` | The sun maps have no colour attachment: allocated with colour format 0 (pipeline.cpp:1211), and `addColorAttachment(0)` returns at once (`llrendertarget.cpp`). Colour writes are no-ops for any `RenderShadowDetail`. Value 3 is reachable only through Debug Settings: the XUI combos offer 0-2 (`floater_preferences_graphics_advanced.xml:750`, `floater_phototools.xml:876`), and the menu toggles 0/2 (`menu_viewer.xml:4842`, `LLAdvancedClickRenderShadowOption`, `llviewermenu.cpp:10420-10436`). No VSM path exists | mask colour unconditionally in the sun cascades. A depth-only opaque program (no colour output) is a GPU candidate, gated on 1b (4.5.2). Legacy shares the function; the change is identical in image, but it keeps `check_legacy_shadows.py` green only if outside `<ShadowLegacy>` regions (it is) | Q |
| F4 | **inefficiency** (measured 1.01 ms) | `renderAlphaObjects`, pipeline.cpp:7781-7860 | per draw: `bind()` (a `gGL.flush()` and a profile zone even when bound, `llglslshader.cpp:1052-1080`) plus 4-5 uniform sets through the cache; the whole `PASS_ALPHA` map is walked twice, once per rigged flag (:7803-7814) | one walk that buckets draws by (rigged, GLTF); bind each program once and set its uniforms once; draw each bucket. Depth-only, so order across buckets cannot change the map (min depth); bit-exact by the G0 compare harness | Q |
| F5 | **inefficiency** | pipeline.cpp:12988-12992 (`shadow_hidden`) → `skipRenderingShadows` :14042-14052 | clears the four maps every frame while the shadow is hidden (handover band, moonless sky). The dark gate beside it clears once (`mDirectionalShadowsDarkCleared`) | share the clear-once flag (the maps are not read with weight while hidden) | Q |
| F6 | inefficiency (low) | pipeline.cpp:12631-12651 | a cascade turned off by `RenderShadowSplits` (a debug setting) is bound and cleared every frame | clear once while off | Q |
| F7 | overengineering (low) | `LLDisableOcclusionCulling` in `generateSunShadow` (:12789) and the save/zero/restore in `renderShadowImpl` (:11635-11636, :11850) | occlusion is turned off twice on the sun path | keep `renderShadowImpl`'s (the projector tiles call `renderShadow` directly, `llprojectorshadows.cpp:859, 1073`); the outer one is redundant but harmless (LL code: leave unless touched anyway) | — |
| F8 | cruft (needs proof) | pipeline.cpp:11837 and :12774, `gDeferredShadowCubeProgram.bind()` | binds the occlusion-box program after every shadow pass; nothing in the shadow pass draws with it (F1 is the only occlusion use) | verify that the next draw after `generateSunShadow` and after the projector tiles binds its own program, then drop. Not proven dead: callers may rely on "some program is bound" for `gGL` immediate-mode draws | Q (verify), else leave |
| F9 | structure (blocks 5c and C2) | pipeline.cpp:12757, `static LLCullResult result[4]` | shared by the main view and the probe passes (`gCubeSnapshot` uses 0-1); a per-pass, per-pack result is needed for parallel culls and for keeping a level's result across frames | per-pack, per-layer results (#542 §5.5 already plans this) | C2 |
| F10 | inefficiency (low, Tracy-inflated) | `renderGeomShadow` (pipeline.cpp:4721-4766) → `LLDrawPoolAvatar::renderShadow` (`lldrawpoolavatar.cpp:358-366`) | every avatar pool is called 3 times per cascade (about 145 calls in cascades 1-3), most returning at `mDrawFace.empty()`; 0.04-0.05 ms per cascade with the zone on, near 0 without | none needed; noted so that nobody chases it | — |
| F11 | **behaviour against the rule** (inherited) | `lldrawpoolavatar.cpp:388-399`; pipeline.cpp:3256 (attachments) | an impostored avatar and its attachments cast no sun shadow; impostor status follows the camera's ranking (16 non-impostors at Ultra, `featuretable.txt:366`), so an avatar's shadow appears and vanishes with the camera. The 2026-11-13 decision Q9a ("impostored avatars in full in sun maps") is not in the code | decision D6: implement Q9a (cost below) or keep the upstream rule | after C2 (the dynamic layer is where it would cost) |

What implementing Q9a would cost [inferred]:

- At Rooftops about 30-35 more avatars would cast. They are the farther ones, so mostly levels 1-2.
- At the measured avatar cost per level (rigged plus pool, about 0.15-0.3 ms per level for about 16 avatars), that is
  roughly +0.5-1.5 ms per frame.
- Small-caster culling of their attachments and G3 bring it down.

Also checked, no finding:

- the shadow targets are released while the context lives (pipeline.cpp:850 → :1544-1557). The 2026-11-13 atlas
  survey's "never released" finding is fixed;
- the per-cascade dither origin (N1) resets to 0 after the loop (:12769-12775);
- the probe pass overwrites `mSunShadowMatrix[0..1]` (:12738), but probes update after the main view
  (`llappviewer.cpp:1803`), and the next main frame rewrites them before use.

## 8. Staged plan

Each stage is a `codex/` feature PR against `vkstorm-devel` (Q's measurement additions stay on devel). Every stage has
the same gates:

- Legacy byte-identical (`check_legacy_shadows.py`, `check_legacy_glsl.py`, `bench_shadow_legacy.py`);
- the G0 compare harness on the cascades where the stage claims bit-exactness;
- cost reported in ms and fps against the floor.

| Stage | Scope | Gate: offline | Gate: in-world (owner) | Expected (Rooftops still) | Effort |
|---|---|---|---|---|---|
| **1b** (running) | GPU zones per cascade, GL window with GPU timers | — | the session, section 9 | GPU numbers; the CPU/GPU split | (other agent) |
| **Q** | F1-F6 (and F8 if proven); F4 alpha-blend buckets; small-caster cull (4.2.1) with a debug setting, a unit-tested threshold in `llshadowcascadefit.h`, and per-level dropped counts in the Tracy zone arguments | G0 compare: cascades bit-identical with the cull at 0, **under both Legacy and Stochastic** (F1-F6 and F4 touch `renderShadowImpl` and `renderAlphaObjects`, which both methods run; neither is in a `<ShadowLegacy>` region, so the text gate does not cover them: the compare must); unit test of the threshold; Legacy gates | one Rooftops capture: cascade zones, dropped draws per level; a look at fences and railings in levels 2-3 | −0.4 to −1.7 ms (model 5a, Q row) | 0.5-1 d |
| **C1** (#542) | camera-position levels, radial selection | #542 | #542 (turn, zoom, edge) | ± 0 (image stage) | 0.5-1 d |
| **C2** (#542) | S/L cache, triggers, dynamic set | #542 | #542 | 2.0-4.2 ms cascade CPU | 1.5-2 d |
| **5c** (multicore) | parallel culls; first target chosen on C2's numbers | multicore study | multicore study | −0.3 to −0.6 (under C2) | 2-4 d |
| **G3** (GL study) | skin palette UBO | G0 compare (G-buffer, cascades) | crowded room | −0.3 to −0.5 (cascades), more in the main view | about 1 d |
| **P1** (own idea 5.1) | probe faces read the main levels | probe face shadow vs own passes (image diff, within the probe's texel) | reflections of shadowed scenes | −0.35 to −0.4 | 0.5 d |
| optional, on C2's refresh counts | #542 C3/C4; 5.2 static caster mesh; 4.1.2 far dynamic rate; 4.5.3 layered dynamic | per item | per item | refresh frames; −0.2 to −0.5 each | 0.25-2 d each |
| G4-G6 (GL study) | draw list, arenas, shadow MDI | G0 compare | — | refresh frames; −0.2 to −0.5 per frame under C2 | 5-9 d |

## 9. What the stage-1b session must capture for this study

The 1b measuring build (8c4814273c, `work/mc1b-gpu-timing`) already has:

- the Tracy GPU zones `sun cascade 0..3`, `projector tiles`, `projector tile` and `projector layer`;
- the existing GPU zones (`generateSunShadow`, `renderShadow`, `shadow simple`, `shadow alpha`, `shadow alpha masked`,
  `shadow alpha blend`, `shadow alpha grass`, `shadow alpha material`, `renderGeomShadow`, `renderDeferredLighting`);
- `perf_check.py --gpu-window`: one GLSubmitStats window with GPU timers after each capture (`window_<preset>.json`),
  since `tracy-csvexport` exports no GPU zones.

**Scenarios** (Rooftops, Ultra, the same spot):

1. a throwaway warm-up capture after every setting change;
2. the orbit re-armed for each variant (0f6c074e9e), with the camera heading logged once a second;
3. 60 s each:

| # | Scenario | Why |
|---|---|---|
| a | still, sun | the baseline of every number here |
| b | still, shadows none | the floor the cascades cannot go below; GPU headroom |
| c | orbiting (10 deg/s), sun | today's turning cost (never captured: the earlier moving captures were frozen) |
| d | orbiting, none | its pair |
| e | still, moonlit night (an EEP preset with the moon above #540's gate) | the moon's cost (4.8) |
| f | still, sun, **High** preset (128 m, detail 1) | the default-tier question and the 128 m option (4.4) |
| g | optional: still, sun, Ultra with `RenderShadowSplits` 2 | the cost of three passes; image not valid, since today's code ends the shadows at 81 m |

**Per scenario, from the GL window (GPU timers on):**

1. GPU ms per view `sun cascade 0..3` and `probe face > sun cascade 0..1`, split by pass: opaque static, opaque rigged,
   `shadow geom` (avatar pools, terrain, trees), masked, **blend**, grass and material masks, GLTF masked.
2. GPU ms of the sun lookup: the sun light pass and the stochastic shadow resolve, as named in the pass list. This
   separates map generation from map reading.
3. Frame GPU ms against main-thread ms, and the share of frames with `Swap` > 10 ms. Is the GPU the next limit once
   about 5-7 ms of CPU go?
4. Counters per cascade view:
   - draws, triangles and indices;
   - shader binds;
   - texture binds and the redundant share;
   - uniform calls that reached GL;
   - palette uploads and distinct (avatar, skin) pairs (G3's gain);
   - the mergeable-run histogram (G6's gain);
   - vertex buffer binds.

**Per scenario, from Tracy (CPU):**

5. The zones of section 2 per cascade, re-run with `scripts/perf/tracy_cascade_zones.py`.
6. `renderAlphaObjects` per cascade, and `pushBatch` calls inside it (F4's draw count).

**Not in the 1b build. Add it if the build can still take it; otherwise it is Q's first commit and #542's C0:**

7. The size histogram of shadow draw infos per level: `LLDrawInfo::mExtents` diameter in level texels, in buckets
   <0.5, 0.5-1, 1-2, 2-4, >4. That gives A1 for the small-caster cull. A devel-only counter (`check_release_hooks.py`
   names).
8. Static versus dynamic draws per level (bridge kind: avatar, attachment, active, static). That gives #542's A1-A3.
9. Change-feed boxes per frame per level, by cause (#542 C0).

## 10. Decisions for the owner

The recommended option is first in each.

1. **D1, order.**
   - **A:** 1b → Q → C1 → C2 → 5c (target on C2's numbers) → G3 → optional items; G-series on its own track.
   - B: G-series first (G3-G6), cache later. No image change for longer, but the smaller saving (model 5b), and
     LL-core risk first.
   - C: 5c first, as the multicore plan has it. Its cascade target shrinks once C2 lands.
2. **D2, small-caster cull.**
   - **A:** diameter below 1 texel of the level, levels 1-3, static casters and rigged attachments, never avatar bodies,
     debug setting `RenderShadowSmallCasterTexels` (1.0; 0 = off).
   - B: 0.5 texel (safer for thin lattices, about half the saving).
   - C: 2 texels (more saving, visible on fences).
3. **D3, Ultra tier defaults.**
   - **A:** keep 4 levels and 192 m. Under C2 the far level costs only its refreshes.
   - B: 160 m at Ultra (as High Ultra).
   - C: 128 m.
   - D: 3 levels.

   B-D are only worth it if C2 is declined.
4. **D4, moon nights.**
   - **A:** as the day (the cache treats the moon like the sun).
   - B: reduced moon, levels 0-2, or half distance, until C2 lands.
5. **D5, probe faces.**
   - **A:** after C1, probes near the camera read the main levels (5.1); probes beyond level 3's reach keep their own
     passes.
   - B: keep the probes' own two passes.
6. **D6, impostored avatars (F11, Q9a of 2026-11-13).**
   - **A:** implement Q9a after C2 and G3, on the dynamic layer, with small-caster culling of attachments; budget
     +0.5-1.5 ms at Rooftops [inferred].
   - B: keep the upstream rule (impostors cast nothing; shadows follow the camera's impostor ranking).
   - C: implement it now. It costs before the savings land.
7. **D7, G-series scope for shadows.**
   - **A:** G3 now; G4-G6 on their own track for the main view; shadows take G6 when it comes. The static caster mesh
     (5.2) is decided on C2's refresh causes.
   - B: pull G6 ahead for shadows.
8. **D8, GPU levers (D16 maps, depth-only program, resolution).**
   - **A:** decide on 1b's GPU numbers; nothing before.
   - B: D16 now (not bit-exact).

## 11. Risks, and what was not verified

- **No GPU numbers.** Every saving is CPU, with Tracy attached. The multicore baseline measures Tracy at +4.6 % of the
  frame at Rooftops and 1.38× on zone-dense work such as cull and sort, so these ms are upper bounds and the shares are
  more reliable.
- **One scene, one GPU, one driver** (RTX 5070 Ti, NVIDIA 621.14, threaded optimisation on). The cheap GL draw
  (0.11 µs) may not hold on AMD, Intel or Mesa, where G6's value could be larger.
- **Small-caster share (A1) is assumed.** Section 9 item 7 measures it. If few casters are small, Q's saving is the
  low end (0.4 ms).
- **The alpha-blend share (A3) is assumed.** The pass is measured (1.01 ms); how much of it is rebinding is not. F4
  is right on its own terms (fewer redundant binds and uniform sets).
- **5c's parallel share of the sort (A4b) is assumed.** The sort writes shared pools, and `rebuildMesh` is GL.
- **#542's cache figures carry its own assumptions** (quiet change feed, A1-A5). Section 2 supports its A2 low end but
  does not measure the feed.
- **The moving case is unmeasured.** The Rooftops moving captures had a frozen camera for sun and all (Rooftops split
  §3).
- **F8 is unproven**, and is only to be dropped after the check.
- **Own idea 5.1** depends on probe programs taking the main matrices. Not traced in every forward probe shader.
- **Literature:**
  - the RTR4 chapter 7 text and the bibliography were read in the owner-supplied copy (cited by section and
    reference number, nothing copied);
  - GPU Gems chapters are cited by number and title from the books' tables of contents; their listings were not
    re-read for this study;
  - Kasyan 2013 and Valient 2012 are known through RTR4's summary only.
- **No viewer build was made** (one build at a time on the owner's PC). Nothing here is called safe.

## 12. Running the tools

```
python scripts/shadows/sun_shadow_cost_model.py           # the tables of sections 2, 4, 6 (seconds, no GPU)
python scripts/shadows/sun_shadow_cost_model.py --check   # invariants: sums, S1 port values, monotone plans
```

`scripts/perf/tracy_cascade_zones.py` reads a Tracy capture through `tracy-csvexport -u` and attributes each zone
to its cascade (section 2). It takes about 1.5 minutes per zone on a 1.4 GB capture; the docstring gives the call.
