<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Point-light shadows: implementation plan (study, 2026-11-15)

Status: study only. Nothing here is built or measured in-world yet. **Decisions D1, D2, D3, D4, D6, D7 and D8 were taken by
the owner on 2026-11-15 ("as recommended") and are marked decided in section 8.** The auditor's findings of the same day
are applied (sections 3.2, 3.6, 3.7, 3.4, 3.3, 5 and 9). Code references are to `origin/vkstorm-devel` at
`1457ed0ea7`. Labels: **[V]** read in the code, **[M]** measured (machine and date given), **[E]** estimate, **[I]** inferred
and not checked. Owner go: 2026-11-15. **Additions of 2026-11-15** (owner: "OK"): method D′ and face masks per spatial group (3.3), Q3
levers (f) to (h), a Q2a seed gate, the lists of later options, rejected methods and uncovered topics (8.1 to 8.3), and
**Q5, tinted transmission, which is PROPOSED and not decided** (7.1; decisions T1 to T3 are open in section 8). Sources of
the additions: the literature synthesis of 2026-11-15 as corrected by its audit, and `research_notes/Shadow methods for
avatars and many lights/transmission.md` (cited as "TM"; the other notes are cited as "notes", with RT for
`reports/Real time shadow casting techniques.md`). Earlier work: the planner verdict and the first survey of 2026-11-14
(`reports/point-light-shadows-2026-11-14/` on the owner's machine, with the offline lookup bench `bench_point_lookup.py`).

**Citation keys.** The notes are our research notes, kept on the owner's machine, not in the repository (`research_notes/` and
`reports/` are untracked). Keys: TM = `research_notes/Shadow methods for avatars and many lights/transmission.md`; RT =
`reports/Real time shadow casting techniques.md` (L:n = a line of its note `research_notes/Real time shadow casting
techniques/local_light_shadows.md`, R:n = a line of the report); SM = `reports/Shadow methods for avatars and many lights.md`
(R:n = a line of the report); in `research_notes/Shadow methods for avatars and many lights/`: GG1, GG2, GG3 = `gpugems1.md`,
`gpugems2.md`, `gpugems3.md`; M = `proxies.md`; in `research_notes/Real time shadow casting techniques/`: A = `api_and_performance.md`,
FILT = `filtering_soft_shadows.md`. Line numbers are those of the files on the owner's machine. The GPU Gems chapter numbers
follow our notes and were not checked against the books.

## 1. Summary

The in-world check of 2026-11-14 found 55 of the 59 lamps near the owner's living room are point lights with no shadow, so
their light goes through walls. This plan gives every point light six cached shadow faces in an atlas of its own, built on
the projector shadow pool's parts (change feed, change index, LOD pin, scheduler, PCSS filter) and looked up in the two
deferred point-light programs.

- **The pool's avatar layer (P4) is done** [V]: it merged on vkstorm-devel as e7f368f1b9 (#515); its review follow-up is
  PR #516 (`codex/projector-pool-p4-review`, not merged yet). Avatars are a dynamic layer L over the static atlas S and are
  paired by position alone; `LOCAL_SHADOW_DYNAMIC` is in use; one combined frame cap serves all caches; tracking and lamp
  roots are per owner; `LLLocalShadowGL::Scope` saves and restores the GL state once (`doc/shadows/projector-shadow-pool.md`,
  "Stage P4"). The point lights reuse these parts. P4's walk session in-world is still owed, and Q2a relies on the avatar
  layer, so it waits for that session.
- Stages: **Q0** measure → **Q1** GL-free core → **Q2a** faces, the cheap
  tap for every lamp, avatars → **Q2b** near tier with PCSS → **Q3** cost levers. Optional **Q4** puts the cheap tap on
  forward alpha. **Q5**, tinted transmission for points and projectors, is **proposed only** (7.1; decisions T1 to T3, section 8).
- Cost, estimated, in the living room at 1440p: **0.3 to 1.2 ms** a frame in steady state, against the **1.11 ms** of
  headroom measured at one place (devel 7.93 ms against the ceiling of 9.04 ms). The top of the range does not fit. Q0
  measures the owner's rooms before any code is written for the faces.
- Memory: **+128 MiB** of video memory (two 4096² D32F atlases). That holds the owner's 59-lamp room at 52 %.
- Effort, calibrated against git history: **about 15 to 30 hours of pipeline time** (two to three days of wall clock) and
  three owner sessions. The projector pool's S0 to P3, specified at 14.5 to 19 days, took about 19 hours (section 10).

## 2. Current state

### 2.1 Where point lights are gathered, culled and shaded today

| What | Where | Notes |
|---|---|---|
| Light set | `LLPipeline::calcNearbyLights`, `pipeline.cpp:6395-6581` [V] | `mNearbyLights`, sorted nearest first, within `RenderFarClip` with deferred rendering (`:6415-6418`). The six-light cap (`:6411`, `:6523`) applies only to the non-deferred path. Point lights and projectors are in one set. Skipped for reflection renders and probes (`:6400`) |
| Draw-loop cap | `pipeline.cpp:10716`, `:10756-10762` [V] | `RenderLocalLightCount` (256): the loop stops after that many lights |
| Lit size | `pipeline.cpp:10782` [V] | `s = radius * 1.5` (the deferred light reaches 1.5 x its radius) |
| Frustum cull per light | `pipeline.cpp:10805` [V] | `AABBInFrustumNoFarClip` on the light's cube; only lights in view are shaded |
| Camera outside the light's box (s + 0.2 m) | `pipeline.cpp:10812-10831` [V] | one draw of the light's cube with `gDeferredLightProgram` (`pointLightF.glsl`); projectors go to `spot_lights` (`:10816-10820`) |
| Camera inside the box | `pipeline.cpp:10833-10847`, `:10900-10938` [V] | batched, up to 16 per full-screen pass (`gDeferredMultiLightProgram[count-1]`, `multiPointLightF.glsl`, uniforms `light[]`, `light_col[]`) |
| Point program, per light | `class3/deferred/pointLightF.glsl:70-158` [V] | discard beyond `size` (`:88`), legacy attenuation (`:93`), PBR (`:95-118`) and legacy (`:119-152`) branches; no shadow term |
| Point program, batch | `class3/deferred/multiPointLightF.glsl:102-125` (PBR), `:133-171` (legacy) [V] | loop over `LIGHT_COUNT`; no shadow term |
| Program set-up | `llviewershadermgr.cpp:1962-2003` [V] | both have `hasShadows = true`, so under Stochastic they already link `shadowStochasticUtil.glsl` (`llshadermgr.cpp:262-275`) |
| GPU scopes | `pipeline.cpp:10741` (local lights: `LL_PROFILE_GPU_ZONE` only, **no `VKS_GLSTAT_PASS`**), `:10903-10904` (fullscreen lights: both) [V] | Q0 adds the missing scope |
| Forward alpha | `setupHWLights`, `pipeline.cpp:6583`, slot loop `:6689-6760` [V]; `class2/deferred/alphaF.glsl:63`, `:303`; `pbralphaF.glsl:78`, `:212`; `class3/deferred/materialF.glsl:91`, `:428`; `class1/gltf/pbrmetallicroughnessF.glsl:132`, `:342` [V] | the nearest lights fill the fixed-function light slots, `LIGHT_LOOP(1..7)` in the shaders; no shadow for lamps or projectors |
| Water | `class*/environment/*` [V] | no shader there reads `light_position` or calls a point-light function: lamps do not light the water surface today |
| Probes and mirrors | `pipeline.cpp:10719` [V] | local lights are drawn in probe renders when `RenderReflectionProbeLevel > 0`; the pool's lookup uses the current modelview, so it works in a probe face (`llprojectorshadows.cpp:1269`) [V] |

### 2.2 The projector pool, and what a second cache reuses

`doc/shadows/projector-shadow-pool.md:267-301` lists what it hands to a second cache. Checked in the code:

| Part | Where [V] | Reuse for points |
|---|---|---|
| Sizes, level, shrink and grow | `llprojectorshadowpool.h:72` (`baseSize`), `:141` (`levelSize`), `:148` (`demand`), `:161` (`LevelController`) | needs a demand overload for faces (it takes base sizes per light) |
| Atlas tiers | `:85-111` (`atlasTier`, 2048/4096/8192, `atlasBytes`) | the point atlas takes its own rule (decision D3) |
| Buddy placement, repack | `:283` (`BuddyAtlas`), `:455` (`planRepack`) | as is: six blocks per lamp |
| Volumes, change index | `:531` (`Pyramid`), `:578` (`makePyramid`), `:641` (`intersects`), `:778` (`TileGrid`), `:889` (`ChangeIndex`) | one pyramid per face, key = lamp key x 8 + face |
| Fixed-layer rules | `:958` (`castsFixed`), `:968` (`hookSkips`), `:990` (`becomesMover`), `:1017` (`rebuildDirties`), `:1032` (`texturedDirties`) | as is |
| LOD pin books | `:1049` (`PinBook`); counter on the volume (`LLVOVolume::pinLampLOD`) | a second `PinBook`; the counter is already shared |
| Verdict and order of work | `:1174-1228` (`TileFacts`, `classifyTile`), `:1260` (`selectStatic`), `:1318` (`selectDynamic`), caps (before P4: 0.35 ms static, 0.30 ms dynamic, 24 avatar draws; since P4 `kFrameCapMs` 0.35, `kDynamicCapMs` 0.20, `kStaticCapMs` 0.15) | shared across both pools through `selectFrame` (section 3.6) |
| PCSS filter | `shadowStochasticUtil.glsl:262-381` (G1 block): `stSpotSetup` `:282`, `stSpotQuadTile` `:312`, `stSpotFilterTile` `:337`; caller `sampleProjectorShadow` `:385-426` | the tile is a uniform (`proj_shadow_tile`, `:269`); it must become a parameter |
| GL half | `llprojectorshadows.cpp`: `allocateAtlases` `:172`, `freeAtlases` `:223`, `ensureAllocated` `:245`, `release` `:307`, `copyTile` `:464`, `blitTile` `:488`, `setTileOrigin` `:625`, `renderTile` `:649-757`, `pinCasters` `:920`, `update` `:949-1230`, `setupLight` `:1232`, `bindAtlas` `:1277` | atlas helpers to share (section 3.1) |
| Pass flag | `pipeline.h:1268-1273`, `pipeline.cpp:221` | `sLocalShadowPass` and `RenderSpotLight` are already generic |
| Fixed-layer cull filter | `pipeline.cpp:3243` (`bridgeCasts(root, RenderSpotLight)`) | as is |
| Own-geometry skip | `pipeline.cpp:3638` (SL-11353: `stateSort` skips `RenderSpotLight`) | runs for points too; whether it removes a prim batched into a group's buffer is not verified [I], so the plan does not rely on it (section 3.4) |

**How projector tiles are scheduled and cached** (`llprojectorshadows.cpp:949-1230`) [V]: once a frame from
`generateSunShadow`'s Stochastic branch (`pipeline.cpp:12940-12951`), never for probes:

1. the change feed's boxes are swapped (`LLShadowDirtyFeed::beginFrame`, `:979`);
2. the light set is read (`updateLightSet`, `:390-462`: eligible spotlights in `mNearbyLights` within `min(far clip, 128 m)`, with hysteresis);
3. the layout is rebuilt (`layoutChanged`);
4. lamp poses are read, and volumes are rebuilt only when a pose changed or agent space shifted (`:990-1023`);
5. the lamp roots go to the feed (`:1024`);
6. the change index runs only on a frame with boxes (`:1028-1057`);
7. avatars are paired, using `isVisible()`, which is camera-driven (`:1059-1081`);
8. each tile gets a verdict (`:1090-1154`), and two schedules run under fixed CPU caps (`:1157-1210`). A tile render is `renderShadow` into S, then a copy S → L (`:701`, `:715`);
9. the GL state is put back (`:1212-1229`).

A static scene renders no tile.

**Three single-owner pieces block a second cache** [V]:
- `LLShadowDirtyFeed::setTracking` is a bool, not a count. `LLProjectorShadows::release` sets it false
  (`llprojectorshadows.cpp:315`), and switching off drops every pending box (`llshadowdirtyfeed.cpp:165-171`). A point pool
  would lose its boxes whenever the projector pool is released.
- `setLampRoots` swaps in one list (`llshadowdirtyfeed.cpp:328-337`), so a second caller would overwrite the projectors' lamp roots.
- `renderTile` saves and restores the GL state per pool (`llprojectorshadows.cpp:1164-1229`), and the caps are per pool. Two
  pools would each spend 0.35 + 0.30 ms.

P4 fixed all three: one frame cap for all caches, per-owner tracking and lamp roots, one GL save and restore. The point
pool reuses them (3.6).

### 2.3 Lifetime and threads (projector pool, the pattern to copy)

The pool is allocated in `allocateShadowBuffer` (`pipeline.cpp:1218-1230`) and released when the method or detail leaves
Stochastic detail 2, in `destroyGL` (`:865`) and in `cleanup` (`:1547`). `forget` runs from `unlinkDrawable` (`:2197`).
Every entry point runs on the main thread and returns at once off it or while the viewer exits
(`llprojectorshadows.cpp:247`, `:951`). Nothing is in `LLRender::shutdown`, `LLImageGL` or
`LLVertexBuffer::cleanupClass`, which worker threads with shared contexts run [V]. The point pool follows the same pattern
and adds its calls beside the projector pool's.

### 2.4 Tests and gates that exist

- `indra/newview/tests/llprojectorshadowpool_test.cpp` (1264 lines), built and run by `.github/actions/ci-checks/action.yml:59` [V].
- `scripts/shadows/projector_pool_g1.py`: the lookup in the atlas equals a standalone map, max abs 0.
- `check_legacy_shadows.py` (C++ regions) and `check_legacy_glsl.py` (GLSL text with the pool's permutations undefined;
  `PERMUTED` `:25`, `UNDEFINED` `:38`) [V].
- `scripts/inworld/projector_orbit_check.py` (O-1, O-2, O-6), `perf_check.py`.
- `scripts/tests/check_release_hooks.py:23`: the forbidden names include `ProjectorShadowStats\w*` and `llprojectorshadowstatsapi\w*` [V].

### 2.5 GL features (checked on the owner's GPU)

On the RTX 5070 Ti, driver 621.14, OpenGL 4.6 compatibility context (scratch script, 2026-11-15) [M]:

| Feature | Present |
|---|---|
| `ARB_shader_viewport_layer_array` | yes |
| `ARB_viewport_array` | yes |
| `ARB_multi_draw_indirect` | yes |
| `ARB_conservative_depth` | yes |
| `GL_MAX_VIEWPORTS` | 16 |
| `GL_MAX_FRAGMENT_UNIFORM_VECTORS` | 1024 |
| `GL_MAX_UNIFORM_BLOCK_SIZE` | 65536 |

The viewer already loads `glViewportIndexedf` (`llgl.cpp:785`, `:2108`), the instanced draws (`:564`, `:1871`, `:797-798`)
and `glMultiDrawElementsIndirect` (`:825`, `:2156`) [V]. **`ARB_shader_viewport_layer_array` is an extension, not part of
core 4.6**, and AMD and Intel drivers were not checked [I]. That matters for the face-rendering choice (3.3). The viewer
does not yet enforce a 4.6 floor (`llgl.cpp:1471-1474` still gates features at 4.0 and 4.3) [V]. The point pool is wanted
only on 4.6 contexts and has no other path.

## 3. Design

### 3.1 Parts and files

| Part | File | What |
|---|---|---|
| GL-free core | `indra/newview/llpointshadowpool.h` (new), namespace `llpointpool`, reusing `llprojectorpool` types | face table and matrices, face pick, guard band, sizes per tier, demand, tier rule with hysteresis, face masks (avatar box vs face), merged schedule items |
| GL half | `indra/newview/llpointshadows.h/.cpp` (new), class `LLPointShadows` | its own atlases S and L, records, `update`, `renderFaces`, `setupPointLight`, `setupPointBatch`, `bindAtlas`, `forget`, `pinCasters` |
| Shared atlas helpers | `indra/newview/llshadowatlas.h/.cpp` (new, extracted from `llprojectorshadows.cpp:172-243`, `:464-499`, `:625-643`, `SavedState` `:146-153`) | the D32F pair and its framebuffers, copy, blit, tile origin, GL state save and restore. Two instances (one per pool). Alternative: duplicate the code. Rejected, because the two would drift |
| Coordinator | `indra/newview/lllocalshadows.h/.cpp` (exists since P4) | once a frame: both pools prepare, one merged selection under shared caps, the renders, one GL state restore |
| Change feed | `llshadowdirtyfeed.h/.cpp` | tracking is an owner mask (`setTracking(owner, on)`) and lamp roots are kept per owner (`setLampRoots(owner, roots)`) since P4; the point pool adds the `Points` owner |
| Pipeline hooks | `pipeline.cpp` | allocation and release beside `mProjectorShadows` (`:865`, `:1225-1230`, `:1547`), `forget` (`:2197`), `bindShadowMaps` (`:10148-10152`), the light loops (`:10756-10974`), `renderShadowImpl` cull and submit split (`:11596-11646`), two-sided faces (`:11625`), the update call (`:12951`) |
| Shaders | `shadowStochasticUtil.glsl` (filter takes a tile; point lookups), `pointLightF.glsl`, `multiPointLightF.glsl` (permutation `POINT_SHADOW_POOL`, and `POINT_SHADOW_NEAR` for the near program) | |
| Shader manager | `llviewershadermgr.cpp:1982-2003` (permutations), a near program `gDeferredPointShadowNearProgram` (`multiPointLightF`, `LIGHT_COUNT` 1), `llshadermgr.cpp:271` (the depth-common key also for `POINT_SHADOW_POOL`) | |
| Program count | `llviewershadermgr.cpp:1982-2003` | **Growth.** `gDeferredMultiLightProgram[count-1]` has one program per batch size (16). Each gets a `POINT_SHADOW_POOL` variant (+16), `gDeferredLightProgram` gets one (+1) and the near program is new (+1): about **+18 programs** [I, from the loop at `:1982-2003`], and more if the same loop also multiplies by existing permutations. That adds shader compile and link time at start-up and in the program cache. Q2a counts the programs and the link time before and after, and builds the variants only under Stochastic detail 2 (`RenderShadowPointLights` on) so other settings pay nothing |
| Stats | `llprojectorshadowstatsapi.cpp` (a `point` block in the same op; names keep the forbidden prefix) | devel builds only |

### 3.2 Faces

Each point lamp gets six perspective faces, **aligned to the agent axes** (the region's axes), not to the prim. A spinning
light prim never redraws its faces, and one view-to-agent `mat3` per lighting pass is enough. This is cube shadow mapping
in an atlas, as in "six spotlights": Gerasimov, *Omnidirectional Shadow Mapping*, GPU Gems 1 ch. 12; Shishkovtsov,
*Deferred Shading in S.T.A.L.K.E.R.*, GPU Gems 2 ch. 9. It is cached as a static layer plus a dynamic overlay, as in DOOM
(2016) and Unreal's cached shadow maps.

- **Face pick: the major axis** of `d = light→receiver` in agent axes. Ties are broken x before y before z, the same in C++
  and GLSL. One face per receiver, and every tap of that receiver reads that face only.
- **Guard band** G texels on every side. The face's interior covers exactly 90 degrees, and the projection uses
  `tan(half fov) = (N/2) / (N/2 - G)`:

  | Tier | N | G | fov | Texel at 1 / 3 / 6 m |
  |---|---|---|---|---|
  | far | 64 | 2 | 93.7° | 3.3 / 10.0 / 20.0 cm |
  | far | 128 | 4 | 93.7° | 1.7 / 5.0 / 10.0 cm |
  | near | 256 | 16 | 97.6° | 0.9 / 2.7 / 5.4 cm |
  | near | 512 | 16 | 93.7° | 0.4 / 1.2 / 2.5 cm |

  [computed]. The far tier's single gather needs 1 texel of margin, so G = N/32 is enough. The near tier's PCSS radius is at
  most 12 texels (`ST_SPOT_KERNEL_TEXELS_MAX`) plus 1 for rounding, so G = 16 at both sizes. A receiver on the 45-degree
  boundary therefore never has a tap leave its face. The 2026-11-14 bench measured that with G = N/32 (all four sizes,
  checked in the near tier at those sizes): 0 taps left a face (**that zero came from the shader's `kmax` clamp to the
  guard band, not from the guard band alone**: the bench clamps `kmax` the way the shader does, so it shows the clamp and
  the band together are safe, not that G = N/32 alone is), and the seam step (the same receiver looked up in both
  neighbouring faces) was above 0.05 for 0.91 % of seam receivers at 64 texels and for 0.13 % or fewer at the larger sizes
  [M, RTX 5070 Ti, 2026-11-14]. Q1's test asserts the margin for every tier, and the shader also clamps `kmax` to G - 2 as
  a guard.
- **Stored value: the face's perspective depth** in the D32F atlas, the projectors' format, written by the existing caster
  programs with early-Z intact. Along each texel's ray, depth and radial distance are monotonic in each other, so the depth
  test keeps the same nearest caster either way. Seams are safe because every tap reads the one face the receiver picked
  (the guard band). They do not depend on the stored quantity. The lookup works in metres along the ray, where it needs
  metres (bias, penumbra, kernel). **Decided (D1, 2026-11-15).** LearnOpenGL and Gerasimov store radial distance because they sample a `samplerCube`
  across seams, which this design does not do. Storing radial distance would need either `gl_FragDepth` in a new variant of
  every caster program (losing early-Z) or an R32F colour target, as in ogldev (+64 MiB per 4096 atlas, and a colour write
  in every caster program). This differs from the "store radial distance" takeaway; the owner chose it as decision **D1**.
- **Normal-offset position (P-2).** The face pick and the lookup both use the receiver moved by the normal offset
  (`spot_shadow_offset`), never the raw position. Picking from the raw position and looking up from the offset one can land
  a tap on a face the receiver did not pick, outside its guard band. Q1 test P-2 therefore runs on the offset position: the
  GLSL twin and the C++ pick the face from `pos + offset * n`, and every tap of that receiver stays in that face's tile.
- **The emitter prim is excluded by identity, not by near planes (D4, decided).** Every face uses one small fixed near
  plane (2 cm). The emitter prim (the prim that carries the light) is left out of every face render of its own lamp: the
  face's caster filter skips that prim's drawable (and, where its faces are batched into a group's buffer, its draw
  infos, matched by the owning volume). The rest of the linkset (a shade, a base) casts: the linkset is the fixture.
  An earlier draft cut the emitter with per-face near planes, set from the half-extents of its agent AABB. That is wrong for
  a non-cubic or large emitter: the three clipped half-extents describe a box, so a long thin tube, a rotated prim or a
  ceiling panel either leaves part of itself casting into the lamp or cuts away neighbouring geometry (a wall, a shade)
  that lies inside the box. Identity has neither problem. The skip is by the volume's identity, the way
  `LLProjectorShadows` skips its own light's geometry. How the skip reaches batched draw infos is [I]; the Q2a coder lists
  it, and if a draw info does not name its volume, the emitter prim is kept out of the group's batch for point-light
  renders. Q1 tests the exclusion predicate (section 7). `far = 1.5 r` (the lit size).
- **Two-sided casters**: `renderShadowImpl`'s `LLGLEnable cull(GL_CULL_FACE)` (`pipeline.cpp:11625`) becomes
  `cull(sLocalShadowTwoSided ? 0 : GL_CULL_FACE)`. The flag is set only inside point-face renders, so every other pass is
  unchanged.

### 3.3 Rendering the faces (the choice, and how Q0 decides it)

One **sphere cull per lamp** (owner decision 1): an orthographic shadow camera around the lamp's lit box, through
`LLViewerCamera::updateFrustumPlanes(cam, true, ...)`, as the sun cascades are culled [I: path not traced end to end].
`updateCull` and `stateSort` run once, then the dirty faces are submitted. That needs `renderShadowImpl` split into its cull
and sort part (`pipeline.cpp:11633-11646`) and its submission (`:11648` on). The split is a text change with identical
behaviour for every existing caller, and it is not inside a ShadowLegacy region. The regions sit in `allocateShadowBuffer` (`pipeline.cpp:1187`, `:1231`,
`:1287`), `setupSpotLight` (`:11276`) and `generateSunShadow` (`:12062`, `:12167`, `:12959`, `:12998`) [V].

| Method | CPU | GPU | Code | GL |
|---|---|---|---|---|
| **A. One cull, one submit per face in the mask** (recommended start) | cull + sort once; submit x faces | every draw in the sphere goes through the vertex stage for every face in the mask; the rasteriser clips to the face | no new shader variant | core |
| B. One cull per face (the projector `renderTile`, six times) | (cull + sort + submit) x faces | each face draws only what is in it | none | core |
| C. Geometry shader, `gl_ViewportIndex` (one submit, GS emits to the faces a triangle touches) | one submit | GS amplification (LearnOpenGL warns it is not necessarily faster) | a GS variant of every caster program (opaque, masked, fullbright masked, tree, GLTF opaque, mask and blend, avatar; rigged and not) | core 4.1 |
| D. Vertex shader `gl_ViewportIndex`, instanced (one instance per face in the mask) | one submit | vertex x faces, as A | a VS variant of every caster VS; an instanced draw beside `LLVertexBuffer::drawRange` for the shadow pass | `ARB_shader_viewport_layer_array` (extension) |
| **D′. Instancing with no extension** (new, ours; 2026-11-15) | one submit | vertex x faces in the mask, as D; clipper work for the clip planes [E] | a VS variant of every caster VS | core: `gl_ClipDistance` 3.0, `glDrawElementsInstancedBaseInstance` 4.2, `gl_BaseInstance` 4.6 [I] |
| E. Multi-draw indirect | fewer driver calls within one vertex buffer | as A or D | the viewer draws per `LLDrawInfo` with its own buffer, texture and matrix (`lldrawpool.cpp:432`, `:576`); MDI covers only draws that share a buffer | core 4.3 |

**Recommendation: A.** Static faces are drawn only when something changes, so their cost is spread over the fill and the
rare edits. A needs no new program, so the caster programs Legacy and the sun share stay untouched. D is the only option
with a real CPU gain, but it needs an extension. Without that extension, D would need A as a fallback, which the rules
forbid. **Decided (D7, 2026-11-15):** A, one sphere cull and one submit per face; D only if Q0 shows at least 2x less CPU
and the AMD and Intel GL 4.6 drivers have the extension.

**Method D′ (new, 2026-11-15).** D without the extension. One viewport covers the whole atlas. The vertex shader maps the
face's clip space into its tile with an affine map, `x' = s*x + o*w` and the same for y, which keeps the interpolation
perspective-correct. Four `gl_ClipDistance` planes clip at the tile edges, guard band included, so a face never draws into
its neighbour's tile. The face list comes in through the base instance of the draw, and the shader takes the n-th set bit
of the mask at `gl_InstanceID` [I; the notes' proposal, not tested]. **The literature has no clip-distance routing to
tiles: this route is ours** [I; the notes' own proposal, with no published source]. Everything it needs is core GL, `gl_BaseInstance`
being core in 4.6 (notes M:13) [I], so the D7 condition "the extension is present on AMD and Intel" does not apply to D′.
- **Gate:** Q0's bench gets D′ as a column. D′ must give the 2x CPU gain of D7 and pass the image check. **If D′ passes, the
  AMD and Intel extension condition of D7 no longer applies; the owner confirms when Q0 reports.** The 2x CPU gate stays.
- **Risks** [I until Q0 measures them]:
  - a vertex-shader variant of every caster program (opaque, masked, fullbright masked, tree, GLTF, avatar, rigged or not),
    on top of the +18 programs (3.1);
  - the Legacy guard `check_legacy_glsl.py` must stay identical: the variants exist only in the point-pool permutation;
  - clipped triangles may differ from method A in the last ulp, so **G1-P needs a stated tolerance** for D′ (Q1 states it
    before Q2a relies on it);
  - the interaction with the clip-plane use that exists today is unknown (Q0 lists every user of `gl_ClipDistance` and
    of the fixed-function clip planes in the caster path);
  - instancing saves CPU only, never raster [notes SM R:96, I]: without masks it draws every triangle six times.

**Face masks per spatial group, for A and D′ (new, 2026-11-15).** Each spatial group's box (`LLViewerOctreeGroup`
`mObjectExtents`, `llvieweroctree.h:264-265` [V]) is tested against the six face frusta, and the group is drawn only into
the faces it touches. A draw info carries no box of its own [V: `LLDrawInfo`, `llspatialpartition.h:80-209`, has no
extents member and no volume pointer], so the test is per group, once per cull.
- **How it relates to what is in the plan.** Method B and Q3(e) already cull per face. The mask is new only as **one box
  test per group (6 plane sets against one box) in place of N culls** [I].
- **Source:** Olsson 2015, per-caster face masks. His "6x" saving includes camera-dependent receiver culling, whose stage
  alone costs 1.8 to 3.5 ms [M in source: RT L:79]. We take only the box-against-face part, which does not depend on the
  camera, and we do not take the receiver culling.
- **Risk:** large SL groups that straddle a lamp touch all six faces and gain nothing. Olsson found batches over 512
  triangles "significantly worse" [notes RT L:81, I].
- **Census:** Q0 records the number of faces each group's mask covers (3.10).

**Q0 measures it offline** (`scripts/shadows/point_faces_bench.py`, standalone GL through `scripts/perf/gl_test_context.py`;
no viewer, and only while no build runs). The scenes are synthetic rooms: 50k / 200k / 1M triangles in 50 / 300 / 1500 draws,
and a skinned 146k-triangle avatar proxy with four bone matrices. The faces are 64 / 128 / 256 / 512, with face masks of
1, 2, 3 and 6. Each of A and D′ also runs with the per-group masks and without them. For each of A to E and D′ it measures CPU submit time (QPC) and GPU time (timer queries), median of 9 x 20. The
in-world census (3.10) adds the real draws and triangles per lamp sphere, which say where the owner's rooms fall on that
grid.

### 3.4 Lookup

**Filter refactor (first commit of Q2a):** `stSpotQuadTile` and `stSpotFilterTile` take `(sampler2D atlas, vec4 tile)` as
parameters instead of the uniforms `proj_shadow_atlas` and `proj_shadow_tile`. `sampleProjectorShadow` passes its uniforms
in. Gate: `projector_pool_g1.py` stays max abs 0, so the projectors' output is bit-identical. If a compiler breaks
bit-exactness, the point lookup gets its own copy of the two functions instead.

**Point face lookup** (`samplePointShadow`, new in `shadowStochasticUtil.glsl` between G1-style markers):
`d = point_view_to_agent * (pos - centre_view)`; major axis → face, then `(u, v, w)` in face coordinates;
`uv = 0.5 + 0.5 * (u, v) / (w * tan_half)`; `z = c0 - c1 / w` from the face's near and far planes. The tile origin comes
from the lamp's six origins.

- **Far tier, the cheap tap:** one `textureGather`, four compares, bilinear weights (2 x 2 PCF). The receiver is moved by
  the normal offset `spot_shadow_offset` (0.04 m, `settings.xml:12190`) [V], and the bias is one texel at the receiver,
  converted to depth with the projector code's `z_tex`. The receiver plane is optional; Q0's lookup bench (V2 against V3)
  picks it. Cost [M, lower bound; 2026-11-14 bench, RTX 5070 Ti, 2560x1440]: **0.0038 to 0.0041 ms per lit Mpx per lamp**
  inside the 16-lamp batch, and 0.025 ms worst case (receivers in random order). It is a lower bound: the bench ran a
  synthetic room, with the receiver-plane term stubbed out, on NVIDIA only. AMD and Intel, a real scene and the full term
  will cost more. Q0 repeats it (section 3.10).
- **Near tier, PCSS:** `stSpotFilterTile` unchanged, with 8 search gathers and 32 filter gathers. Penumbra from the emitter
  extent: the emitter prim's scale clamped to 2 to 15 cm (`emitterExtent`, `llprojectorshadowpool.h:129`), the projectors'
  rule. Largest radius 4 cm at the receiver (`ST_SPOT_RADIUS_MAX_M`), at most 12 texels and at most G - 2. Cost [M, lower
  bound, same bench and same limits]: **0.046 ms per lit Mpx at 512, 0.051 at 256** (worst case 0.19 / 0.14). A near lamp filling the 1440p screen costs
  about 0.19 to 0.21 ms.
- **Q2a gate: the filter's seed comes from the light's texel, not the screen** (2026-11-15; the notes' check, SM R:175).
  The near tier reuses the projectors' Vogel disc. Today `stSpotDisc` has one fixed orientation everywhere in the map
  (`shadowStochasticUtil.glsl:295-301`, "the same orientation everywhere in the map") [V], and the G1 block contains no
  rotation or screen-space hash [V: read `:262-381`]. So the reuse is camera-independent now. The gate keeps it so: any
  rotation or jitter added to the disc for the point lookup is seeded from the lamp's texel (face, tile-local texel), never
  from `pos_screen` or `gl_FragCoord`, or the shadows become camera-dependent and Uralsky-style fixed holes appear (notes SM
  R:51, R:240 [I]). Test: the same receiver gives the same lookup value for two different camera orientations and screen
  positions (a CRC over the lookup output of a static scene while the camera orbits, as O-1).
- **Bias in metres, camera-independent:** a normal offset of 0.04 m, plus one texel of this face at the receiver
  (`w * 2 tan_half / N` metres), lowered by the receiver plane where the near tier uses it. The camera position enters only
  through the shadows' reach fade, as for projectors (`sampleProjectorShadow:389-395`).

**Uniforms.** Per pass: `point_shadow_atlas` (bound in `bindShadowMaps`) and `point_view_to_agent` (mat3). Per lamp: three
vec4 holding the six face origins, and one vec4 `(N, 1/atlas, tier, emitter m)`; the near planes are the one fixed 2 cm value and need no uniform. In the batch these
are arrays over `LIGHT_COUNT`: 16 x 4 vec4 = 64 vec4 on top of today's 32, against 256 at the GL minimum (1024 on the
owner's card) [V limit]. The AMD dummy-reference block (`multiPointLightF.glsl:179-186`)
extends to the new arrays. A lamp without six drawn faces has tier 0 (no lookup).

**Where it runs** (C++, `pipeline.cpp:10756-10974`):
- a far lamp with the camera outside its box: `pointLightF` with `POINT_SHADOW_POOL`, uniforms set beside `LIGHT_CENTER` (`:10823`);
- a far lamp with the camera inside its box: in the batch, arrays filled beside `fullscreen_lights` (`:10845`);
- a near lamp: the camera is inside its sphere, so it is always on the full-screen side. It is taken out of the batch and
  drawn alone with `gDeferredPointShadowNearProgram`. Measured 2026-11-14: a mixed tier loop cost 1.44 to 1.51 ms against
  1.04 to 1.12 ms for separate passes [M];
- probes: the cheap tap for every lamp. The near program is not used inside `gCubeSnapshot`, which bounds probe cost;
- mirrors and the water reflection: whatever path draws local lights with the current modelview, using the main view's
  tiers. Whether the water reflection draws local lights at all is not verified [I].

### 3.5 Tiers by camera position, with hysteresis (owner decision 2)

- **Near candidate:** the camera is inside the lamp's lit sphere (`|cam - centre| < 1.5 r`). Of the candidates, the K
  nearest are near (**D2**, decided: K = 2, confirmed after Q0).
- **Enter** when a candidate is closer than the current K-th near lamp by more than `max(0.5 m, 15 %)`. **Leave** when the
  camera is beyond `1.5 r + 1 m`, or when displaced by that rule. Each change is held for at least 0.5 s.
- Only the camera's **position** goes in, never its orientation. Q1 test: the same path with a spinning camera gives the
  same tier sequence.
- **Sizes:** near 512 (256 under 0.4 m radius), far 128 (64 under 1 m). When the atlas demand passes 60 %, far faces
  shrink first, down to the 64 floor, then near faces go to 256. They grow back after 3 s at or below 45 %
  (`LevelController`, with a demand overload).
- **No shadow vanishes:**
  - an upgrade keeps the old faces until all six new ones are drawn. The buddy atlas briefly holds both; the cost is in
    the capacity test;
  - a downgrade blits at once (nearest, `blitTile`), then redraws (cause `lowres`).

Atlas use [computed]:

| Lamps | 4096 atlas | 8192 atlas |
|---|---|---|
| 2 near + 57 far at 128 | 52 % | 13 % |
| 2 near + 57 far at 64 | 27 % | 7 % |
| 3 near + 100 far at 128 | 87 % (shrinks to 64: 43 %) | 22 % |
| 256 at the 64 floor | 38 % | 9 % |

### 3.6 Caching, scheduling, and the shared cap

- **Records** per lamp (eligible: a point light in `mNearbyLights` within the reach, not worn, not on an avatar, not a HUD,
  a radius, not black). This is the projectors' `eligible` (`llprojectorshadows.cpp:116-138`) without the spot test. The
  **pose** is the global position, the radius and a **rotation-invariant emitter size** (the length of the emitter prim's
  scale vector, quantised to 1 cm, not its agent AABB). The agent AABB changes whenever a non-cubic prim turns, so a
  spinning lamp prim would change the pose every frame and redraw all six faces every frame. Rotation does not enter, because
  the faces are agent-aligned. Q1 test: a prim spinning about any axis gives the same pose key on every frame.
- **Change index:** six pyramids per lamp (`makePyramid` with the face fov, near and far), keyed `lamp * 8 + face`. A dirty
  box marks only the faces it touches, with the first cause kept per face. Causes are the projectors': `new`, `lod_up`,
  `add_remove`, `edit`, `mover`, `lamp`, `avatar`, `lowres`, `other`.
- **LOD pin:** `pinCasters` over the sphere cull's groups, with the point pool's own `PinBook`. Pins are held for the lamp
  (one key per lamp) and cover everything inside the sphere box.
- **One ledger, shared caps (implemented in P4):**
  - the point pool joins the one frame ledger: its items go through `selectFrame` across pools, with the pool, the key and
    the face mask on each item, and `LLLocalShadowGL::Scope` (a scope counter) saves and restores the GL state once for
    all caches in the frame. The feed side is per owner: `setTracking(Points, on)` and `setLampRoots(Points, roots)`, so
    the point pool neither overwrites the projectors' lamp roots nor drops their boxes;
  - **one** combined cap: `kFrameCapMs` = **0.35 ms** for all caches together. The dynamic layer (`kDynamicCapMs` =
    0.20 ms) is spent first; the static tiles keep at least `kStaticCapMs` = 0.15 ms, and get the whole 0.35 ms when there
    is no dynamic work. A static tile whose predicted cost is above the cap runs alone as the progress pick once it has
    waited 4 frames (or was never drawn). Before P4 each pool had 0.35 ms static plus 0.30 ms dynamic, which two pools
    would have summed to 0.65 ms against about 0.4 ms of headroom inside +5 %; the combined cap holds the sum at 0.35 ms;
  - `A_max` = 24 avatars a face, the nearest to the lamp first. P4's unit tests cover the combined cap, the progress pick
    and the lag table; the sum measured in the club and the living room is owed to P4's walk session, then to Q2a;
  - selection order: never-drawn static items nearest first, then oldest first; dynamic items with the 4-frame deadline
    first, then nearest by camera position.
- **Moving lamps** (rare: a scripted lamp, an animated fixture). A dynamic item redraws its faces under the dynamic cap,
  with the lookup built from the pose the faces were drawn with, so a lagging shadow stays coherent. This is the projectors'
  D10 rule.
- **Fill** after login or a teleport: never-drawn lamps nearest first, under the static cap. Estimated 0.3 to 2.5 s for
  55 lamps (0.2 to 0.42 ms CPU per lamp: A or B at the 0.07 ms per projector tile measured on 2026-11-14) [E]. In that
  time a lamp without drawn faces is lit without a shadow.

### 3.7 The avatar layer for point lights (owner decision 4)

P4 built the layer for the projector pool and the point pool reuses it: S holds the fixed layer, and L is S plus the avatars drawn over it. For points:

- **Pairing, camera-independent:** every eligible avatar (`LLCharacter::sInstances`; not dead, muted, jelly-dolled, too
  slow or invisible), with its box tested against each face pyramid of each lamp within reach. The result is a face mask
  per (lamp, avatar), typically 1 to 2 faces for a lamp a few metres away and up to 4 close by [I]. No `isVisible()` test.
- **Draws:** a face with an avatar is copied S → L, then the avatar is drawn into L only (`LOCAL_SHADOW_DYNAMIC`, avatar
  render types only, two-sided). When the avatar leaves, one restore copy S → L.
- **Order** under the shared dynamic cap:
  1. the near-tier lamps' avatar faces every frame;
  2. projector tiles with an avatar every frame (the P4 rule: the nearest faces by the camera's position every frame);
  3. the far lamps' faces round-robin, oldest first, with the 4-frame deadline.
- **Lag** of a far lamp's avatar shadow, from P4's numbers [E, an estimate until the walk session and Q2a measure it]: an
  avatar face costs about **0.04 ms** of CPU [E] (the pool spec's 0.04 to 0.15 ms for 1 to 4 avatar draws); the scheduler
  predicts every item at no less than a **0.05 ms floor** (`kDefaultItemMs`), then at the item's own last measured cost.
  The 0.20 ms dynamic share therefore holds **4 faces per frame**, and at most `A_max` = 24 avatars draw per face, nearest
  first. The wait of the last face of a set is `ceil(faces / 4)` frames, the nearest faces being served every frame and the
  rest by the 4-frame deadline, oldest first. Two faces per avatar and far lamp, so 10 / 20 / 55 lamps are 20 / 40 / 110
  faces. Faces drawn every frame (2 near lamps x 3 faces + 2 projector tiles = 8) count against the same 4 a frame, so the
  table is a best case, without that share:

  | Faces with an avatar | frames | ms at 60 fps | ms at 120 fps |
  |---|---|---|---|
  | 4 | 1 | 17 | 8 |
  | 8 | 2 | 33 | 17 |
  | 12 | 3 | 50 | 25 |
  | 24 | 6 | 100 | 50 |
  | 40 (20 far lamps) | 10 | 167 | 83 |
  | 110 (55 far lamps) | 28 | 467 | 233 |

  The first four rows are P4's table; the last two extend it with the same formula. If the in-world lag is too long, the
  levers are a lower per-face cost (a measured `lastAvatarMs` below the 0.05 ms floor) or a bigger dynamic share.

- **GPU cost per avatar face draw:** 0.012 ms at 146k triangles by the pool's 0.08 ms per Mtri, up to the 0.037 ms measured
  for a 2048 projector view [M, pool spec]. So `A_max` = 24 costs 0.3 to 0.9 ms of GPU while the avatar is under many
  lamps. `A_max` must be set by what P4 and Q2a measure, not by this table.
- **Showing the lag** (devel only): per lamp `avatar_age_frames`, and per tier the max and median over a window, in the
  stats API. A gate script `scripts/inworld/point_shadow_check.py` walks the avatar through the owner's rooms and reports
  the lag per tier and per lamp count. The owner's check is a walk recording.

### 3.8 Failure behaviour

- An allocation that fails tries the next smaller tier. If 2048 fails, point lights are lit without shadows (as today), the
  failure is logged once, and the allocation is retried with a back-off of 5 s doubling to 60 s. The method never changes.
- A lamp the atlas cannot hold even at the floor stays unshadowed. Lamps that sort last lose out: smaller lamps first, then
  by key.
- A mode change (method or detail) releases the atlases and records. Exit and `destroyGL` release while the context lives.

### 3.9 Threads and shared state

- Everything runs on the main thread. Entry points return off it and while the viewer exits.
- New shared state:
  - `LLPipeline::sLocalShadowTwoSided` (set only inside face renders);
  - the feed's owner mask and per-owner lamp roots;
  - the coordinator (a member of `LLPipeline`, beside `mProjectorShadows`, `pipeline.h:916`).
- None of it is reached from `LLRender::shutdown`, `LLImageGL` or `LLVertexBuffer::cleanupClass`. `forget` and `forgetPinned`
  run from `unlinkDrawable` only (main thread), as now. Each stage PR lists every caller and thread of every function it
  touches, and the coder runs `git grep` on each.

### 3.10 Q0 census (what is measured, per point light)

From `ProjectorShadows.stats` with a `point` block:

| Field | Per light or per frame |
|---|---|
| key, radius r, lit size 1.5 r, distance from the camera to the centre | per light |
| camera inside r / inside 1.5 r / inside the box (the full-screen path) | per light |
| in the draw loop (within `RenderLocalLightCount`), frustum-visible | per light |
| attachment, on an avatar, `ACTIVE` (moving), root mover or physical | per light |
| emitter AABB half-extents; linked prims whose box contains the light centre (the closed-shade predictor for D4) | per light |
| estimated screen coverage (the projected sphere, clipped) | per light |
| faces each spatial group's mask covers (box against the six face frusta, 3.3): a histogram of 1 to 6 | per light, on request |
| lamps within 15 cm of another lamp (the `emitterExtent` clamp): the count and the cluster sizes | per frame |
| faces per lamp that contain at least one blended caster (Q5's tint tiles, 7.1) and the blended draws and triangles there | per light, on request |
| draws, triangles at the current LOD and at the pinned LOD, avatars inside the sphere | per light, on request (`census: true`): one sphere cull per lamp, only when asked |
| **LOD pin on all lamps' spheres**: total pinned triangles over all lamps (union of the spheres), against the main view's triangles at the natural LOD; the extra main-view GPU ms with the pins held (frame with and without) | per frame, in the three rooms |
| GPU ms of `local lights` (new `VKS_GLSTAT_PASS`) and `fullscreen lights` | per frame |

Then `perf_check.py --no-glstats` frames in the living room, the bedroom and outside, Stochastic detail 2, vsync off, at
1440p. Before any number is recorded, `bench_point_lookup.py` moves into `scripts/shadows/` (default ref `origin/vkstorm-devel`)
and is run again together with `point_faces_bench.py`, on an idle GPU.

## 4. Forward alpha and water

- **Forward alpha** (alpha blending under stochastic transparency, `alphaF`, `pbralphaF`, the forward `materialF`, GLTF
  `pbrmetallicroughnessF`). In Q2, lamps stay unshadowed on alpha surfaces, as projectors are today. Glass, hair and leaves
  inside a room can still catch a lamp in the next room. Optional **Q4**: the cheap tap in `LIGHT_LOOP(1..7)`, with
  `setupHWLights` recording which lamp sits in each slot and passing its face origins (7 x 4 vec4). Those shaders already
  link Stochastic's util (`doc/shadows/shadow-method.md:191`). Cost [E]: alpha pixels x 7 x 0.004 ms per Mpx, small. Decision **D6**.
- **Water:** lamps do not light the water surface today [V], so there is nothing to shadow. The refraction and the underwater
  view show the deferred scene, which carries the new shadows [I].

## 5. Costs

Budget: everything new for light and shadow, together, at the default tier, at most the Stochastic frame of 82178 (+5 %).

**Anchors** [M, RTX 5070 Ti, driver 621.14, about 2560 x 1350, one place 45 m from the projector room, 2026-11-14]:

| Build | Frame |
|---|---|
| 82178 | 8.61 ms |
| Ceiling (82178 + 5 %) | 9.04 ms |
| devel P2c | 7.93 ms |
| **Headroom** | **1.11 ms** at that place |

The headroom inside the owner's rooms is unknown; Q0 measures it.

**Per stage, steady state, the living room at 1440p (3.69 Mpx)** [E unless marked]:

| Stage | GPU ms | CPU ms | Memory | Basis |
|---|---|---|---|---|
| P4 (done, #515) | 0.04 to 0.15 while an avatar stands under projectors (1 to 4 avatar draws); 0 otherwise | ≤ 0.20 (`kDynamicCapMs`, inside the 0.35 ms combined cap, 3.6) while an avatar is under a lamp; off-screen animation of paired avatars: unknown, measured in P4's walk session | 0 | pool spec measurements [M], cap by construction |
| Q0 | 0 in release (development hooks only) | census only on request | 0 | |
| Q1 | 0 | 0 | 0 | header and tests |
| Q2a lookup, 53 far lamps | 0.16 to 0.31 (20 to 40 % coverage each, x 0.004 ms per lit Mpx) | < 0.05 (pose reads, uniforms) | **+128 MiB** (two 4096² D32F) | bench [M, lower bound: synthetic room, receiver-plane term stubbed, NVIDIA only] x coverage [E, a guess: the far-lamp coverage factor of 20 to 40 % is not measured] |
| LOD pin on all lamps' spheres (every lamp, D2 of 2026-11-14) | unknown: a lamp over a dense room pins that room at full LOD in the main view too (up to the extra triangles `pinned_triangles` counts) | pin and unpin bookkeeping per lamp, < 0.05 | 0 | not estimated: Q0 measures it (3.10) |
| Q2a avatar faces | 0.1 to 0.3 while the avatar is in lamp spheres (cap) | inside the shared dynamic 0.20 | 0 | 3.7 |
| Q2a static faces | 0 per frame after the fill | 0 | | P3 design |
| Q2a fill transient | up to 0.1 to 1.2 per frame for 0.3 to 2.5 s | ≤ 0.35 per frame (`kFrameCapMs`, 3.6; the static floor is 0.15 ms while dynamic work runs, and the fill takes the whole 0.35 ms when the dynamic layer is idle, so it lasts the 0.3 to 2.5 s above and only longer while avatars are drawn) | | A: vertex x 6 at 0.08 ms per Mtri |
| Q2b, 2 near lamps | +0.06 to +0.38 (0.19 ms full-screen each x 15 to 100 % coverage) | ~0 | 0 (faces counted above) | bench [M] x coverage [E] |
| Q3 | −11 % (4 lamps) to −27 % (one lamp) of the lookup for min/max [M offline]; the other levers unknown | +a fragment min/max pass per face draw (small faces: under 0.01 ms [E]) | +2 MiB (RG32F, 1/8 resolution of L) | 2026-11-14 bench |
| Q4 (optional) | alpha pixels x 7 x 0.004 ms per Mpx | ~0 | 0 | |
| Q5 (optional, **PROPOSED**) | small [E]; about 0 in rooms without glass; published: within +-1 ms of a plain map at 2048 squared on a GTX 280 [M in source, 2011] | one MRT pass per tile with blended casters, at tile draw only | 0 without glass; up to +96 MiB at a full 4096 pair (+6 bytes a texel) [computed upper bound] | TM section 8, 7.1 |
| D′ (if Q0 adopts it) | clipper work for four planes [E, unmeasured] | the CPU gate of 2x against A | 0 | 3.3 |

**Running total** (adding GPU and CPU as if serial, an upper bound) [E]. The benched terms are lower bounds [M, lower
bound] (synthetic room, receiver-plane term stubbed, NVIDIA only) and the far-lamp coverage factor is a guess, so the real
totals can be higher:
- after Q2a: 0.2 to 0.7 ms;
- after Q2b: 0.3 to 1.2 ms;
- after Q3: about 0.25 to 1.0 ms.

**These are this study's own stages only.** The budget is shared with every other open light and shadow stage (the
unified-lighting and light-atlas stages, the stochastic shadow and transparency upgrades, projector work and the
remaining shadow and light stages of the roadmap). Each stage's PR reports its own cost and the **running total including
all of those**; the 1.11 ms headroom is spent from the whole list, not from this plan alone. Q0 records the current
total on devel so each later stage subtracts from what is left.

The 1.11 ms headroom measured at one place covers the low and middle of that range and not the top. **Decision points:**
after Q0 (do the rooms have headroom?) and after Q2a (measured). If the cost does not fit, the order of cuts is:
1. Q3's min/max and the empty-face skip before Q2b;
2. K = 1;
3. a lower `A_max`;
4. far faces at 64.

Nothing integrates into release above 82178 + 5 %.

## 6. Compatibility (what users see)

| Setting | Before | After |
|---|---|---|
| Stochastic, detail 2 | point lights pass through walls; avatars cast no lamp shadow | light stops at walls (64-texel far faces leak along the base of a thin wall: about 10 to 20 cm wide at 3 m from the lamp, 5 to 10 cm at 128 [computed from the texel size]); near lamps give soft PCSS shadows with the emitter's size; avatars cast from every lamp, far lamps lagging (3.7); the emitter prim casts nothing and the rest of its linkset does, so a lamp inside a closed opaque linked shade lights only through its openings (D4); reflections from probes no longer carry lamp light through walls; +128 MiB of video memory |
| Stochastic, detail 0 or 1 | | unchanged |
| Legacy (any detail) | | byte-identical: `check_legacy_shadows.py`, and `check_legacy_glsl.py` with `pointLightF.glsl` and `multiPointLightF.glsl` added to `PERMUTED` and `POINT_SHADOW_POOL`, `POINT_SHADOW_NEAR` to `UNDEFINED` |
| Forward alpha | lamps unshadowed | unchanged until Q4 |
| Projectors | | bit-identical lookup (G1); the shared caps change scheduling only (P4) |

There is no before/after image yet: no viewer was built for this study (a build is running). Q2a's PR carries the captures.

## 7. Staged plan

Every stage is one PR from `vkstorm-devel`, RelWithDebInfo with `VKS_GL_STATS` for the measuring stages, one viewer build
at a time, one review round (reviewer + auditor → coder → checker).

| Stage | Scope | Files | Tests and gates | Performance gate (1440p) | Owner check | Effort (calibrated) |
|---|---|---|---|---|---|---|
| ~~P4~~ Avatar layer (both pools) | **Done**: merged as e7f368f1b9 (#515); review follow-up #516 pending. Owed: the walk session in-world (a walk under 3 lamps, a dancer under club lights) before Q2a relies on the avatar layer | | | | | |
| **Q0** Measure | census (3.10); `local lights` pass scope; `point_faces_bench.py`; `bench_point_lookup.py` into `scripts/shadows/` and run again on devel, with the receiver-plane term real and, if a card is available, on AMD or Intel; the LOD-pin measurement on all lamps' spheres (3.10); budget page `doc/shadows/point-shadow-budget.md` with empty tables | `llprojectorshadowstatsapi.cpp`, `pipeline.cpp/.h` (stats accessor, scope), scripts | `check_release_hooks.py` on the release-bound diff; benches on an idle GPU | adds nothing to a release frame | session: living room, bedroom, outside (frame, census, coverage) | 2 to 3 h + session |
| **Q1** GL-free core | `llpointpool`: face table and matrices, pick with ties, guard band per tier, sizes and demand, tier rule with hysteresis, face masks, merged items, lag accounting; the GLSL twin of the face pick | `llpointshadowpool.h`, `tests/llpointshadowpool_test.cpp`, ci-checks step, `scripts/shadows/point_faces_g1.py` | P-1 pick against brute force on 10⁶ directions with exact ties; P-2 every tap of a seam receiver stays inside its tile for each tier's kmax, with both the face pick and the lookup taken from the normal-offset position (3.2); **P-8 emitter exclusion by identity**: for a long thin emitter, a rotated emitter and a large panel emitter, the emitter prim casts nothing into its own lamp, while an adjacent linked prim whose box overlaps the emitter's box still casts (a near-plane cut fails this); **P-9 pose key**: a prim spinning about any axis keeps the same pose key every frame; P-3 hysteresis never oscillates on a recorded walk, invariant under camera rotation; P-4 capacity (256 lamps at the floor ≤ 40 % of 4096); P-5 an upgrade keeps old faces until six new are drawn; P-6 lag bound; P-7 no pool starves; **G1-P**: GLSL face pick, uv and depth equal the C++ on shared vectors (pick exact, uv and depth ≤ 1 ulp) | none at runtime | — | 1.5 to 3 h |
| **Q2a** Faces, cheap tap, avatars | `LLPointShadows` (atlases through `llshadowatlas`, sphere cull + mask submits, two-sided, emitter exclusion by identity, pins, S → L, avatar faces); filter refactor; `samplePointShadow` cheap tier; `pointLightF` and batch permutations; uniforms and binding; stats | `llpointshadows.*`, `llshadowatlas.*`, `pipeline.cpp/.h`, `shadowStochasticUtil.glsl`, `pointLightF.glsl`, `multiPointLightF.glsl`, `llviewershadermgr.cpp`, `llshadermgr.cpp`, `check_legacy_glsl.py` | G1 stays max abs 0 (projectors); G1-P extended to the lookup (atlas against a standalone face); Legacy guards; program count and link time before and after (3.1); static scene: 0 face renders a frame after the fill; O-1 style orbit on point faces (CRC unchanged while the camera orbits; this is also the seed gate of 3.4: no screen-derived seed in the lookup); light stops at a test wall; method and detail switches, window toggle, quit without GL errors; smoke launch | living room within 82178 + 5 %, the lookup ≤ 0.35 ms GPU, per-frame CPU ≤ 0.05 ms in a static scene; fill time and avatar lag reported | Release in-world: the living room (bleeding gone), walk for avatar shadows | 4 to 8 h |
| **Q2b** Near tier | tier rule live (K, hysteresis), 256/512 faces with G 16, PCSS program, upgrade and downgrade paths | `llpointshadows.*`, `llpointshadowpool.h`, shaders, shader manager | P-3 and P-5 in-world: walk through doorways, the tier log shows no flapping; no frame with a near lamp unshadowed during the switch | the near lamps' GPU ≤ 0.2 ms each at full screen; total within +5 % | walk into the living room and out; recording reviewed for size, shape, edges and stability | 2 to 4 h |
| **Q3** Levers | (a) hierarchical min/max early-out, a fragment pass per face draw, exact; (b) empty-face skip (a face with no caster: no sampling); (c) two-tier filter refinement: the near tier's lighter filter (8 + 16 taps) beyond a position-based distance with hysteresis (pool P5a); (d) importance skip: no lookup where the lamp's contribution is under one 8-bit step; (e) single-face perspective cull for one-face redraws; **(f)** an occlusion query of an avatar against the face's cached static depth (7.2: skip the avatar's draw into a face when it is hidden there); **(g)** Cold War's dilated PCF for the far tier, and the world-unit kernel converted to texels (7.2); **(h)** degrade instead of lag (7.2). Only the ones Q2 shows are needed, **each of (f) to (h) like the existing levers** | shaders, `llpointshadows.cpp` | (a) and (b): image difference 0; (c) and (d): measured image change, penumbra at N and N/2; (f): the avatar's shadow equal to the unculled one except in the frame after it enters a face (one frame late); (g): the same penumbra width at 64 and 128 tiles, measured against (c); (h): the lag table with the dropped casters listed | each lever's gain measured; total within +5 % | — | 2 to 4 h |
| **Q4** (optional) Forward alpha | cheap tap in the 7 forward slots | forward shaders, `setupHWLights` | Legacy GLSL guard for the forward shaders; alpha in a lamp room against the deferred surface behind it | measured | glass and hair in the living room | 1 to 2 h |
| **Q5** (optional, **PROPOSED, not decided**) Tinted transmission | one tint tile and one nearest-translucent-depth tile per face or projector tile that contains blended casters, one MRT pass at tile draw; the lookup multiplies the light by the tint (7.1) | `llshadowatlas.*`, `llpointshadows.*`, `llprojectorshadows.*`, the blended-caster programs, `shadowStochasticUtil.glsl`, `pointLightF.glsl`, `multiPointLightF.glsl`, `spotLightF.glsl`, shader manager, `check_legacy_glsl.py` | white tint gives max abs 0 against the untinted lookup (G1 style); a red pane in a test room: red light behind it, none in front; a tile with no glass costs nothing and allocates nothing; Legacy guards; no screen-derived seed | within the running +5 %; tint pass per glass tile and the extra fetch reported | glass in the living room, a stained-glass test prim | [E] 3 to 6 h, after Q2 and Q3; decisions T1 to T3 first |

**Total:** about 12 to 24 h of pipeline time still to go (P4 is done), plus P4's walk session and the sessions for Q0 and Q2.
The proposed Q5 would add [E] 3 to 6 h if the owner takes it.

### 7.1 Q5: tinted transmission, "tinting through glass" (PROPOSED, not decided; 2026-11-15)

The owner rule of 2026-11-13: transparent objects cast no shadow and tint the light that passes through. The solid cutoff is
alpha at or above 0.98 (Q6, 2026-11-14). Today the pools draw no tint at all (below). Q5 adds the tint for **point lights and
projectors**, because the two pools share `llshadowatlas`. Sources: TM sections 0, 1, 8, 14, 15 and 16 (the single-layer
translucency colour map: Gosselin 2004, StarCraft II 2008, Crytek 2011; none of them was opened in primary form except SC2 [TM
section 8: fetched]).

**What the casters do today** [V]:

| Caster | Under Stochastic detail 2 (`use_stochastic_shadows()`) | Legacy |
|---|---|---|
| Alpha-blended face | hashed dither of the map's texel (`ign`), keeps the fraction alpha of the texels, from 0.05 up to `shadow_solid_alpha` = 0.98, solid above (`shadowAlphaMaskF.glsl:58-82`, `pipeline.cpp:266-271`) | the 0.05 to 0.88 column stipple (`:84`), a fixed 50 % whatever alpha is |
| Alpha-masked face | the creator's cutoff only, never dithers (`shadow_solid_alpha` = 0) | the same stipple below 0.88 |
| Colour | none: `frag_color` is white | none |

The pools use these caster programs (3.2), so under Stochastic the lamps get the grain, not the stipple, and no tint [V for the
programs; I that the pool passes take the `STOCHASTIC_SHADOW_ALPHA` permutation, because the permutation is set per program in
`llviewershadermgr.cpp:2638-2661`, not per pass]. **The column stipple must not be used for lamps** (TM section 0: it is the likely origin of the
column bands and palm bars [I]); the Legacy permutation keeps it by the Legacy rule and is not the pools' path. Alpha-masked lace
(TM case A) belongs to the solid tile with an alpha test and no tint; Q5 does not touch it. If Q5 is taken, blended faces below
0.98 leave the dithered solid tile and go to the tint pass, or the lamp would be shadowed and tinted twice [I]; that is part of T1.

**Design** (TM section 8 and section 15 option 5; single-layer translucency colour map) [I]:
- When a tile is drawn, **one pass over the blended casters** writes, with MRT:
  - an **RGBA8 tint target**, cleared to white, with the multiplicative blend `glBlendFunc(GL_ZERO, GL_SRC_COLOR)`. The product
    is commutative, so the pass is order-independent;
  - the **nearest translucent depth** (R16F or R32F, with a MIN blend equation; per-attachment blend equations are core since
    GL 4.0);
  - the pass is depth-tested against the **opaque tile**, with depth write off, so colour hidden behind opaque casters is not
    written.
- **Colour rule:** the blocked fraction is rho = alpha * (1 - t), with t from base colour times texture, so stained glass casts
  its pattern (McGuire and Enderton 2011, TM section 1). The tint written is 1 - rho per channel.
- **Lookup:** a receiver behind the nearest translucent depth multiplies the lamp's light by the tint. It is one extra fetch, made
  only in tiles flagged as having glass.
- **Static glass only**, cached with the static faces. Movers and physical objects do not cast locally (the casting rule), so
  there is no dynamic tint layer.
- **Sun:** out of scope. It would change the sun path (A4).

**Memory.** Tint tiles exist only for faces that contain blended casters. Q0's census counts them (3.10). The upper bound is a
full atlas pair: RGBA8 is 4 bytes a texel, the nearest-translucent depth R16F 2 bytes, so +6 bytes a texel, which for a 4096 atlas
is 96 MiB [computed]; in a room with no glass it is 0 [E]. Q0's count decides whether the tiles go in the atlas pair or in
a smaller side atlas.

**Cost.** The published figure is within about +-1 ms of a plain map at 2048 squared and 1080p on a GTX 280 [M in source, TM
section 8, 2011 hardware]. Ours is [E] small, and about zero in rooms without glass, because the pass and the fetch exist only
for tiles with blended casters. It must fit the running +5 % total (section 5), and Q5 reports its own cost and the total.

**Limits** [I, TM section 8]: one layer. Stacked tints multiply correctly, but a receiver between two panes gets the full product.
It cannot tint on transmissive receivers, and there is no sheer or lace handling (TM case B, a sheer stays with the grain of the
dither above, or goes to CSSM2).

**Alternatives** (TM sections 7, 10, 14):
- **CSSM2** (McGuire and Enderton 2011): several layers, sheer as well, with grain. RGB16F per tile (+6 bytes a texel), +0.1 to
  3.2 ms a frame on a GTX 280 [M in source, 2011]. It also needs the lookup to run inside the PCSS kernel (TM section 15 item 3).
- the **McGuire-Mara prefiltered** variant of CSSM: noise-free, a quarter-resolution transmittance per cache update, loses
  pattern detail [I].

**Open before building:**
- How Second Life content sets alpha and colour for glass. Builders mostly use alpha blend with a tint in the diffuse colour [unverified], so
  t has to be derived from base colour and alpha in a defined way; glTF transmission is absent in SL [unverified] (TM section 16).
- Whether blended faces below 0.98 leave the solid tile (above).
- Whether the tint tile fits the atlas budget (T3).

### 7.2 Q3 levers added on 2026-11-15

Each is used only if Q2 shows it is needed, as for levers (a) to (e). None is costed yet [E]; each reports its gain and the running
total when it lands.

| Lever | What | Source | Fit and risk |
|---|---|---|---|
| (f) | An occlusion query of an avatar's box against the face's cached static depth: if the box is fully hidden there, skip the avatar's draw into that face | GPU Gems 2 ch. 6, Wimmer and Bittner: about 4x in a city scene [M in source]; applying it to point faces is the notes' own idea [I] (notes GG2 L175-180) | camera-independent: it tests against the light's own depth. The result arrives one frame late, so an avatar entering a face may miss its shadow for one frame [I]. Adds query bookkeeping to the dynamic cap |
| (g) | Cold War's dilated PCF for the far tier (contact hardening from one gather, no extra sample), and the world-unit kernel converted to texels so tiles of 64 and 128 look the same | Cold War, notes RT L:37 and L:183 [I] ("critical for scaling system") | the far tier's fixed 2x2 gather otherwise changes its world footprint when a tile changes size (3.5). Changes the look of the far tier: measured image change against the plain gather |
| (h) | Degrade instead of lag: when the dynamic share is spent, drop the smallest dynamic casters first; the update rate of far lamps falls with their distance from the camera **position** | Cold War, RT L:33 and L:182; Flax, RT L:134 and notes A:144 [I] | answers the avatar lag of 3.7 (today the only answer is the lag itself). Distance by position keeps it camera-independent in direction. Risk: a dropped caster pops when it returns; the rule needs hysteresis |

The cost lines of section 5 are unchanged until a lever is built.

## 8. Decisions

**Decided by the owner on 2026-11-15 ("as recommended"): D1, D2, D3, D4, D6, D7, D8.** The chosen option is the first one
in each row. (D5 does not exist: the numbering follows the 2026-11-14 list.)

| | Decision | Options (decided first) |
|---|---|---|
| **D1** DECIDED | What a face stores | (a) **the face's perspective depth (D32F), the projectors' format and filter; metres along the ray in the lookup.** Seams are safe through the guard band, early-Z is kept, and the caster programs are unchanged. (b) Radial distance through `gl_FragDepth` (LearnOpenGL, Gerasimov): a new variant of every caster program, no early-Z. (c) An R32F colour target (ogldev): +64 MiB per 4096 atlas, and a colour write in every caster program |
| **D2** DECIDED | Number of near lamps K | (a) **2** (confirmed after Q0); (b) 1; (c) 3 |
| **D3** DECIDED | Point atlas size | (a) **4096 on every card from 3 GB (2048 below): 128 MiB**, enough for 59 lamps at 52 %; (b) the projectors' rule (8192 from 11 GB dedicated, 512 MiB) |
| **D4** DECIDED | What casts into a lamp of its own linkset | (a) **the emitter prim casts nothing, and the rest of the linkset casts** (the fixture). The cut is made by **excluding the emitter prim by identity**, not with per-face near planes (3.2: near planes fail for non-cubic or large emitters). Lamps inside a closed opaque shade light only through its openings, the same look change the projector ambiance made; Q0 counts how many of the owner's lamps that touches. (b) The whole lamp linkset casts nothing: light leaks through shades as today. (c) Only linked prims that contain the light centre are cut |
| **D6** DECIDED | Forward alpha | (a) **unshadowed in Q2; Q4 optional, after the owner has seen Q2**; (b) in Q2; (c) never |
| **D7** DECIDED | Face rendering | (a) **one sphere cull, one submit per face in the mask (core GL, no new shader variants)**; switch to (b), the vertex-shader `gl_ViewportIndex` with instancing, only if Q0 shows at least 2x lower CPU and the extension is present on AMD and Intel GL 4.6 drivers; (c) one cull per face |
| **D8** DECIDED | What turns it on | (a) **Stochastic, detail 2 ("Sun/Moon + Projectors"), with a debug setting `RenderShadowPointLights` (default on) for A/B; no Preferences entry**; (b) a new detail level 3 |

**Proposed, not decided (2026-11-15): Q5, tinted transmission.** The owner decides T1 to T3 after Q0 has counted the faces with
blended casters. Recommendation first in each row.

| | Decision | Options (recommended first) |
|---|---|---|
| **T1** PROPOSED | Tint method | (a) **the single-layer translucency colour map** (SC2, Crytek, Gosselin): deterministic, no grain, about the cost of a plain map, one layer; (b) CSSM2 (McGuire and Enderton 2011): multi-layer and sheer, with grain, RGB16F a tile, more cost; (c) none: lamps are not tinted |
| **T2** PROPOSED | Scope | (a) **points and projectors** (the pools share `llshadowatlas`); (b) points only; (c) points and projectors now, the sun later (A4) |
| **T3** PROPOSED | Memory | (a) **tint tiles only for faces that contain blended casters** (counted by Q0); (b) a tint tile for every face |

**D7 amended (2026-11-15, owner "OK" to fold the plan additions in).** D7 is as decided (A, with D only on the 2x gate and the
extension). The new method D′ (3.3) needs no extension, and **if D′ passes Q0's gates, D7's AMD and Intel extension condition
no longer applies; the owner confirms when Q0 reports.** The 2x CPU gate stays.

The other open points of 2026-11-14 are settled here: D2 of that list (pin for every lamp) is the LOD pin for every lamp
(3.6), and D3 is D3 above.

### 8.1 Later options (not in any stage yet)

| Option | What | Why later | Source |
|---|---|---|---|
| Per-avatar maps | one shadow map per avatar and lamp (Unreal's per-object shadows) as an alternative to the dynamic layer L | the S to L copy plus avatar draw is the plan; this is the real alternative if the lag or the copy cost bites. Epic itself says a Movable light becomes the cheaper path at some caster count [I] | notes SM R:122, R:124 |
| Transform-feedback skinning | skin an avatar once a frame, then draw the captured vertices into every face | core GL, not compute. It saves **GPU skinning only**: the lag table's limit is CPU submit (3.7), which this does not lower. Many separately rigged attachments mean many captures; alpha-masked parts need UVs in the stream. Not measured | GPU Gems 1 ch. 4, GG2 ch. 3, GG3 ch. 3; notes SM R:184-185, R:233-234 [I] |
| Caster LOD chosen from the light | pick each caster's LOD from its size in the tile for far faces, then freeze it | conflicts with the LOD pin (3.6). Decide after Q0 measures the pin's main-view cost. SL creator LODs can change the silhouette [I]. No source says the shadow pass may use another LOD than the pinned one [I] | GG1 ch. 28 (fewer bones for distant characters), notes M:124 |
| Clustered emitters sharing one cube | one set of faces for lamps that lie close together (a chandelier) | no source in the literature. Casters inside the cluster's radius are placed wrongly; every member needs identity exclusion (D4). Q0 counts the lamps within 15 cm of another, the `emitterExtent` clamp (3.10) | none; rests on the PCSS light-size formula [I] |

### 8.2 Considered and rejected

| Method | Reason | Source |
|---|---|---|
| Selection by what the camera sees | shadows pop when the camera turns | notes RT R:35 [I] |
| Virtual or sparse maps | pages follow the depth buffer, so they are camera-dependent; the cost is for non-Nanite scenes; AMD advises never putting depth targets in sparse memory | notes A:79, A:94 [I] |
| GPU-driven culling | compute, and it belongs to the Vulkan line. Multi-draw indirect itself is not compute and stays in the G-series (method E, 3.3) | notes A:32-35, A:54 [I] |
| Screen-space noise and denoised shadows | they work in screen space, so they are camera-dependent | notes FILT L148, RT R:55-71 [I] |
| The VSM family | light bleeds indoors | GG3 L72, RT R:53 [I] |
| Dual-paraboloid maps | need finely tessellated casters (the test scene used 64 quads a wall); a 1024 map was needed to match a 512 cube; STALKER rejected them | notes M:71-72, L:120, GG2 L60 [I] |
| Tetrahedral maps | no evidence it pays once faces are cached; the GPU Pro chapters (Liao; Doghramachi) are known only as snippets | notes L:122-123, L:133, L:139 [I] |
| Imperfect shadow maps | valid only when the errors average out over many lights, not for direct shadows | notes M:38-48 [I] |

### 8.3 Not covered by our bibliography

- Sharing or clustering shadow maps between nearby lights, and treating a cluster as an area light, with error bounds.
- The cost and precision of clip-distance routing to tiles, and clipper throughput (method D′ rests on this).
- Point-light face timings on recent GPUs other than our own, and the cost of avatar-class casters.
- AMD and Intel GL drivers: viewport and layer routing, `glCopyImageSubData` cost, slow partial clears. The extension is listed on Mesa
  for Intel HD 3000 and HD 530 and on an NVIDIA MX450 [notes M:104]; **AMD on Windows and Intel Arc or Xe on Windows are
  unconfirmed**, and the viewer also ships on Linux.
- A perceptual threshold for an acceptable shadow lag.
- Cache invalidation for user-built scenes at scale, beyond Insomniac's "unmoved for N seconds" rule, which is unproven.
- Omnidirectional-shadow chapters of GPU Pro (Liao) and GPU Pro 6 (Doghramachi), which we have only as snippets. GPU Zen has none
  (its one mention is the sun's virtual shadow maps, not read).
- Modern-GPU numbers for coloured or stochastic transparent shadows: the newest are the GTX 280 figures of 2011 (TM section 16).

## 9. Risks, and what was not verified

**Risks**
- **Cost:** the top of the estimate exceeds the measured headroom (section 5). The levers and cut order are planned; Q0 decides.
- **Avatar layer:** with many lamps the GPU cost is real and the far lag is visible (3.7). It needs the owner's eye at Q2a.
- **Look change (D4):** lamps in closed shades go dark outside their openings.
- **Benched costs are lower bounds** [M, lower bound]: synthetic room, receiver-plane term stubbed, NVIDIA only. The
  far-lamp coverage factor is a guess. The running totals can be higher, and they share the budget with every other open
  light and shadow stage (section 5).
- **Emitter identity on batched geometry:** the exclusion needs the draw info to name its volume (3.2); if it does not, the
  emitter prim is kept out of the batch for point-light renders.
- **Program count:** about +18 shader programs (3.1).
- **LOD pin on all lamps:** its main-view cost is unmeasured until Q0.
- **Fill transient:** with method A each face goes through the vertex stage for every draw in the sphere (up to about 1 ms
  GPU per frame during the fill) [E].
- **Thin walls** at 64-texel faces leak along their base (section 6).
- **Filter refactor:** if bit-exactness to G1 fails on some compiler, the code is duplicated instead (a known fallback for
  the code, not a runtime path).
- **Shared code touched:** the `renderShadowImpl` split, the two-sided flag and the feed's owner mask. Each is checked by the
  Legacy guards and the projector orbit gates.
- **D′ (3.3):** a vertex-shader variant of every caster program, a last-ulp difference from A, and an unknown interaction with
  existing clip-plane use. It is adopted only if Q0's bench passes the 2x CPU gate and the image check.
- **Group masks (3.3):** a large group that straddles a lamp touches all six faces and gains nothing.
- **Q5 (proposed):** the memory of the tint tiles, one layer only, and the unknown way SL content sets glass alpha and colour (7.1).
- **LOD pin with no ceiling** across lamps: a lamp over a dense room pins all of it in the main view too. `pinned_triangles` reports it.

**Not verified**
- `ARB_shader_viewport_layer_array` on AMD and Intel GL 4.6 drivers.
- That `updateFrustumPlanes` with an orthographic box gives the cull exactly the sphere's box (the sun cascades suggest so).
- Whether SL-11353's `stateSort` skip removes batched geometry.
- Whether the water reflection pass draws local lights.
- All in-world numbers: coverage, draws per sphere, avatar counts, the rooms' headroom, off-screen animation cost.
- The 2026-11-14 lookup bench was not run again here: a viewer build was running, and GPU timings taken then are unreliable.
- Scalability on AMD and Intel GPUs.
- `gl_ClipDistance` routing to tiles (D′): nothing is built or measured, and the literature has no source for it.
- That the Q2a coder greps `LLDrawInfo` callers for a volume or box (3.3).
- That the pools' face renders take the `STOCHASTIC_SHADOW_ALPHA` caster permutation (7.1): read per program, not per pass.
- Whether Q5's tint tiles fit the atlas budget; how SL content sets alpha and colour for glass; the 2011 cost figures on today's GPUs.
- The occlusion query's gain on point faces (an idea from the notes, no measurement), and the dilated PCF's look.

## 10. Effort calibration

The projector pool's spec (`reports/projector-pool-2026-11-13/design-final.md`, section 5) estimated S0 1.5–2 d, P1 2 d,
P2 5–7 d and P3 6–8 d: **14.5 to 19 days**. Git on `vkstorm-devel` (dates 2026-11-14, UTC):

| Stage | First commit | Last review commit | Spec estimate |
|---|---|---|---|
| S0 | 01:35 | 01:49 | 1.5 to 2 d |
| P1 | 02:07 | 02:12 (fixes 13:49) | 2 d |
| P2 | 02:32 | 04:27 (fixes 13:50) | 5 to 7 d |
| P2b | 15:28 | 15:54 | — |
| P2c | 17:22 | 18:10 | — |
| P3 | 18:21 | 20:15 | 6 to 8 d |

**S0 to P3: about 19 hours of wall clock**, an owner session and the measured frame (19:34) included. P3 alone, specified at
6 to 8 days, took about 2 hours from its first commit to its review round. So one specified day is about one hour of this
pipeline. The stochastic transparency port (2026-11-04 to 11-06, about two days against a 16 to 25 day estimate) points the
same way. The 2026-11-14 summary's 13 to 17 days for Q0 to Q3 corresponds to about 13 to 17 hours. With P4 done and the
optional Q4 added, this plan's remaining total is **12 to 24 hours**. Viewer builds (one at a time on the owner's PC) and owner
sessions come on top.

## 11. References and licences

- Gerasimov, *Omnidirectional Shadow Mapping*, GPU Gems 1 ch. 12 (2004): cube shadow maps, distance to the light.
- Shishkovtsov, *Deferred Shading in S.T.A.L.K.E.R.*, GPU Gems 2 ch. 9 (2005): point lights as six spot faces.
- Fernando, *Percentage-Closer Soft Shadows*, SIGGRAPH 2005 sketch: the near tier's filter (already the projectors').
- Isidoro, *Shadow Mapping: GPU-based Tips and Techniques*, GDC 2006: the receiver-plane depth bias. Holbert, *Normal
  Offset Shadows* (2011): the normal offset.
- Dmitriev and Uralsky, *Soft Shadow Algorithms*, GDC 2007; hierarchical min/max shadow maps: the Q3 early-out.
- Cached static + dynamic shadow atlases: id Software, DOOM (2016), SIGGRAPH 2016 *The devil is in the details*; Unreal
  Engine's cached shadow maps for point lights.
- `ARB_shader_viewport_layer_array`, `ARB_viewport_array`, `ARB_multi_draw_indirect` (Khronos registry).
- Rejected by the owner: dual-paraboloid (Brabec et al. 2002), tetrahedron (Liao 2010), voxel occupancy, the screen-space
  march. ReSTIR (Bitterli et al. 2020) belongs to the Vulkan / Aurora side.
- Added 2026-11-15: Olsson, per-caster face masks (2015); Cold War (dilated PCF, world-unit kernel, degrade order); Flax (update
  rate by distance); GPU Gems 2 ch. 6, Wimmer and Bittner (occlusion queries); GPU Gems 3 ch. 19 (static and dynamic split);
  McGuire and Enderton, *Colored Stochastic Shadow Maps*, I3D 2011; Filion and McNaughton, *StarCraft II: Effects and
  Techniques*, SIGGRAPH 2008 (translucent shadows); Gosselin, Sander and Mitchell 2004 (ShaderX3, cited only); Crytek 2011 (via
  Bavoil and Jansen); McGuire and Mara 2016. Notes: `reports/Real time shadow casting techniques.md`,
  `reports/Shadow methods for avatars and many lights.md` and `research_notes/Shadow methods for avatars and many
  lights/transmission.md` (TM). Mostly secondary sources; figures marked [M in source] were measured by their authors, not by us.
- LearnOpenGL *Point Shadows*: code CC BY-NC 4.0, not GPL-compatible. Used for the logic only, and no code is copied.
- ogldev tutorial 43 (Etay Meiri): GPL-3.0-or-later. This plan takes no code from it (the six-pass structure and the
  `FLT_MAX` clear are ideas, not code). If a stage copies any, that stage adds the attribution to `NOTICE.md` and
  `doc/licensing.md`.
