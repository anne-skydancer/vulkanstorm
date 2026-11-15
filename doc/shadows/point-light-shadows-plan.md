<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Point-light shadows: implementation plan (study, 2026-11-15)

Status: study only. Nothing here is built or measured in-world yet. Code references are to `origin/vkstorm-devel` at
`1457ed0ea7`. Labels: **[V]** read in the code, **[M]** measured (machine and date given), **[E]** estimate, **[I]** inferred
and not checked. Owner go: 2026-11-15. Earlier work: the planner verdict and the first survey of 2026-11-14
(`reports/point-light-shadows-2026-11-14/` on the owner's machine, with the offline lookup bench `bench_point_lookup.py`).

## 1. Summary

The in-world check of 2026-11-14 found 55 of the 59 lamps near the owner's living room are point lights with no shadow, so
their light goes through walls. This plan gives every point light six cached shadow faces in an atlas of its own, built on
the projector shadow pool's parts (change feed, change index, LOD pin, scheduler, PCSS filter) and looked up in the two
deferred point-light programs.

- **The pool's avatar layer (P4) is not done** [V]: avatars are still drawn into the static atlas S, and their visibility
  follows the camera (`llprojectorshadows.cpp:645-648`, `:1059-1066`; `doc/shadows/projector-shadow-pool.md:13`, `:104-105`,
  `:282-283`). `LOCAL_SHADOW_DYNAMIC` is declared and used nowhere (`pipeline.h:1271-1272`). No branch or PR carries P4. So
  **P4 is stage 1 of this plan**, written so the point lights reuse it.
- Stages: **P4** avatar layer, generic for both pools → **Q0** measure → **Q1** GL-free core → **Q2a** faces, the cheap
  tap for every lamp, avatars → **Q2b** near tier with PCSS → **Q3** cost levers. Optional **Q4** puts the cheap tap on
  forward alpha.
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
| Verdict and order of work | `:1174-1228` (`TileFacts`, `classifyTile`), `:1260` (`selectStatic`), `:1318` (`selectDynamic`), caps `:1364-1367` (0.35 ms static, 0.30 ms dynamic, 24 avatar draws) | merged across both pools (section 3.6) |
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

P4 fixes all three, because it re-divides the caps anyway.

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
| Coordinator | `indra/newview/lllocalshadows.h/.cpp` (new, from P4) | once a frame: both pools prepare, one merged selection under shared caps, the renders, one GL state restore |
| Change feed | `llshadowdirtyfeed.h/.cpp` | tracking becomes an owner mask (`acquireTracking(owner)`, `releaseTracking(owner)`); lamp roots kept per owner and merged for the lookup (P4) |
| Pipeline hooks | `pipeline.cpp` | allocation and release beside `mProjectorShadows` (`:865`, `:1225-1230`, `:1547`), `forget` (`:2197`), `bindShadowMaps` (`:10148-10152`), the light loops (`:10756-10974`), `renderShadowImpl` cull and submit split (`:11596-11646`), two-sided faces (`:11625`), the update call (`:12951`) |
| Shaders | `shadowStochasticUtil.glsl` (filter takes a tile; point lookups), `pointLightF.glsl`, `multiPointLightF.glsl` (permutation `POINT_SHADOW_POOL`, and `POINT_SHADOW_NEAR` for the near program) | |
| Shader manager | `llviewershadermgr.cpp:1982-2003` (permutations), a near program `gDeferredPointShadowNearProgram` (`multiPointLightF`, `LIGHT_COUNT` 1), `llshadermgr.cpp:271` (the depth-common key also for `POINT_SHADOW_POOL`) | |
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
  checked in the near tier at those sizes): 0 taps left a face, and the seam step (the same receiver looked up in both
  neighbouring faces) was above 0.05 for 0.91 % of seam receivers at 64 texels and for 0.13 % or fewer at the larger sizes
  [M, RTX 5070 Ti, 2026-11-14]. Q1's test asserts the margin for every tier, and the shader also clamps `kmax` to G - 2 as
  a guard.
- **Stored value: the face's perspective depth** in the D32F atlas, the projectors' format, written by the existing caster
  programs with early-Z intact. Along each texel's ray, depth and radial distance are monotonic in each other, so the depth
  test keeps the same nearest caster either way. Seams are safe because every tap reads the one face the receiver picked
  (the guard band). They do not depend on the stored quantity. The lookup works in metres along the ray, where it needs
  metres (bias, penumbra, kernel). LearnOpenGL and Gerasimov store radial distance because they sample a `samplerCube`
  across seams, which this design does not do. Storing radial distance would need either `gl_FragDepth` in a new variant of
  every caster program (losing early-Z) or an R32F colour target, as in ogldev (+64 MiB per 4096 atlas, and a colour write
  in every caster program). This differs from the "store radial distance" takeaway, so it is decision **D1**.
- **Near plane = the emitter prim's box.** Per face, `near = clamp(half-extent of the emitter prim's agent AABB along that
  face's axis, 2 cm, 1 m)`. The faces' near planes together clip exactly the box `|x| < hx, |y| < hy, |z| < hz` around the
  light. So the emitter prim never casts into its own lamp, whether or not its geometry is batched with other prims. The
  rest of the linkset (a shade, a base) casts: the linkset is the fixture. Decision **D4** covers the look this gives.
  `far = 1.5 r` (the lit size).
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
| E. Multi-draw indirect | fewer driver calls within one vertex buffer | as A or D | the viewer draws per `LLDrawInfo` with its own buffer, texture and matrix (`lldrawpool.cpp:432`, `:576`); MDI covers only draws that share a buffer | core 4.3 |

**Recommendation: A.** Static faces are drawn only when something changes, so their cost is spread over the fill and the
rare edits. A needs no new program, so the caster programs Legacy and the sun share stay untouched. D is the only option
with a real CPU gain, but it needs an extension. Without that extension, D would need A as a fallback, which the rules
forbid. Decision **D7**.

**Q0 measures it offline** (`scripts/shadows/point_faces_bench.py`, standalone GL through `scripts/perf/gl_test_context.py`;
no viewer, and only while no build runs). The scenes are synthetic rooms: 50k / 200k / 1M triangles in 50 / 300 / 1500 draws,
and a skinned 146k-triangle avatar proxy with four bone matrices. The faces are 64 / 128 / 256 / 512, with face masks of
1, 2, 3 and 6. For each of A to E it measures CPU submit time (QPC) and GPU time (timer queries), median of 9 x 20. The
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
  picks it. Cost [M, 2026-11-14 bench, RTX 5070 Ti, 2560x1440]: **0.0038 to 0.0041 ms per lit Mpx per lamp** inside the
  16-lamp batch, and 0.025 ms worst case (receivers in random order).
- **Near tier, PCSS:** `stSpotFilterTile` unchanged, with 8 search gathers and 32 filter gathers. Penumbra from the emitter
  extent: the emitter prim's scale clamped to 2 to 15 cm (`emitterExtent`, `llprojectorshadowpool.h:129`), the projectors'
  rule. Largest radius 4 cm at the receiver (`ST_SPOT_RADIUS_MAX_M`), at most 12 texels and at most G - 2. Cost [M, same
  bench]: **0.046 ms per lit Mpx at 512, 0.051 at 256** (worst case 0.19 / 0.14). A near lamp filling the 1440p screen costs
  about 0.19 to 0.21 ms.
- **Bias in metres, camera-independent:** a normal offset of 0.04 m, plus one texel of this face at the receiver
  (`w * 2 tan_half / N` metres), lowered by the receiver plane where the near tier uses it. The camera position enters only
  through the shadows' reach fade, as for projectors (`sampleProjectorShadow:389-395`).

**Uniforms.** Per pass: `point_shadow_atlas` (bound in `bindShadowMaps`) and `point_view_to_agent` (mat3). Per lamp: three
vec4 holding the six face origins, and one vec4 `(N, 1/atlas, tier, emitter m)` with the near planes as a vec3
(`hx, hy, hz`). In the batch these are arrays over `LIGHT_COUNT`: 16 x 5 vec4 = 80 vec4 on top of today's 32, against 256 at
the GL minimum (1024 on the owner's card) [V limit]. The AMD dummy-reference block (`multiPointLightF.glsl:179-186`)
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
  nearest are near (**D2**, K = 2 recommended).
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
  **pose** is the global position, the radius and the emitter AABB. Rotation does not enter, because the faces are
  agent-aligned.
- **Change index:** six pyramids per lamp (`makePyramid` with the face fov, near and far), keyed `lamp * 8 + face`. A dirty
  box marks only the faces it touches, with the first cause kept per face. Causes are the projectors': `new`, `lod_up`,
  `add_remove`, `edit`, `mover`, `lamp`, `avatar`, `lowres`, `other`.
- **LOD pin:** `pinCasters` over the sphere cull's groups, with the point pool's own `PinBook`. Pins are held for the lamp
  (one key per lamp) and cover everything inside the sphere box.
- **One coordinator, shared caps (from P4):**
  - `LLLocalShadows::update` collects the static and dynamic items of both pools; items carry the pool, the key and the
    face mask;
  - selection is `selectStatic` / `selectDynamic` over the merged lists. Static: never-drawn items nearest first, then
    oldest first by frames. Dynamic: the 4-frame deadline first, then nearest by camera position;
  - **one** static cap (0.35 ms CPU) and **one** dynamic cap (0.30 ms CPU, plus `A_max` avatar draws counted as face draws);
  - the first pick of each pool's pending list always runs, so neither pool can starve the other. Q1 test P-7 checks this;
  - one GL state save and restore per frame (`llprojectorshadows.cpp:1164-1229`, moved).
- **Moving lamps** (rare: a scripted lamp, an animated fixture). A dynamic item redraws its faces under the dynamic cap,
  with the lookup built from the pose the faces were drawn with, so a lagging shadow stays coherent. This is the projectors'
  D10 rule.
- **Fill** after login or a teleport: never-drawn lamps nearest first, under the static cap. Estimated 0.3 to 2.5 s for
  55 lamps (0.2 to 0.42 ms CPU per lamp: A or B at the 0.07 ms per projector tile measured on 2026-11-14) [E]. In that
  time a lamp without drawn faces is lit without a shadow.

### 3.7 The avatar layer for point lights (owner decision 4)

P4 sets up the layer for both pools: S holds the fixed layer, and L is S plus the avatars drawn over it. For points:

- **Pairing, camera-independent:** every eligible avatar (`LLCharacter::sInstances`; not dead, muted, jelly-dolled, too
  slow or invisible), with its box tested against each face pyramid of each lamp within reach. The result is a face mask
  per (lamp, avatar), typically 1 to 2 faces for a lamp a few metres away and up to 4 close by [I]. No `isVisible()` test.
- **Draws:** a face with an avatar is copied S → L, then the avatar is drawn into L only (`LOCAL_SHADOW_DYNAMIC`, avatar
  render types only, two-sided). When the avatar leaves, one restore copy S → L.
- **Order** under the shared dynamic cap:
  1. the near-tier lamps' avatar faces every frame;
  2. projector tiles with an avatar every frame (the P4 rule);
  3. the far lamps' faces round-robin, oldest first, with the 4-frame deadline.
- **Lag** of a far lamp's avatar shadow [E]: `ceil(far faces / spare draws)` frames, where spare = `A_max` minus the
  every-frame draws (2 near lamps x 3 faces + 2 projector tiles = 8):

  | Avatar in far spheres (2 faces each) | `A_max` 12: frames / ms at 60 / 120 fps | `A_max` 24 |
  |---|---|---|
  | 10 | 5 / 83 / 42 | 2 / 33 / 17 |
  | 20 | 10 / 167 / 83 | 3 / 50 / 25 |
  | 55 | 28 / 467 / 233 | 7 / 117 / 58 |

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
| draws, triangles at the current LOD and at the pinned LOD, avatars inside the sphere | per light, on request (`census: true`): one sphere cull per lamp, only when asked |
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
| P4 | 0.04 to 0.15 while an avatar stands under projectors (1 to 4 avatar draws); 0 otherwise | ≤ 0.30 (dynamic cap) while an avatar is under a lamp; off-screen animation of paired avatars: unknown, measured | 0 | pool spec measurements [M], cap by construction |
| Q0 | 0 in release (development hooks only) | census only on request | 0 | |
| Q1 | 0 | 0 | 0 | header and tests |
| Q2a lookup, 53 far lamps | 0.16 to 0.31 (20 to 40 % coverage each, x 0.004 ms per lit Mpx) | < 0.05 (pose reads, uniforms) | **+128 MiB** (two 4096² D32F) | bench [M] x coverage [E] |
| Q2a avatar faces | 0.1 to 0.3 while the avatar is in lamp spheres (cap) | inside the shared 0.30 | 0 | 3.7 |
| Q2a static faces | 0 per frame after the fill | 0 | | P3 design |
| Q2a fill transient | up to 0.1 to 1.2 per frame for 0.3 to 2.5 s | ≤ 0.35 per frame (cap) | | A: vertex x 6 at 0.08 ms per Mtri |
| Q2b, 2 near lamps | +0.06 to +0.38 (0.19 ms full-screen each x 15 to 100 % coverage) | ~0 | 0 (faces counted above) | bench [M] x coverage [E] |
| Q3 | −11 % (4 lamps) to −27 % (one lamp) of the lookup for min/max [M offline]; the other levers unknown | +a fragment min/max pass per face draw (small faces: under 0.01 ms [E]) | +2 MiB (RG32F, 1/8 resolution of L) | 2026-11-14 bench |
| Q4 (optional) | alpha pixels x 7 x 0.004 ms per Mpx | ~0 | 0 | |

**Running total** (adding GPU and CPU as if serial, an upper bound) [E]:
- after Q2a: 0.2 to 0.7 ms;
- after Q2b: 0.3 to 1.2 ms;
- after Q3: about 0.25 to 1.0 ms.

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
| Stochastic, detail 2 | point lights pass through walls; avatars cast no lamp shadow | light stops at walls (64-texel far faces leak along the base of a thin wall: about 10 to 20 cm wide at 3 m from the lamp, 5 to 10 cm at 128 [computed from the texel size]); near lamps give soft PCSS shadows with the emitter's size; avatars cast from every lamp, far lamps lagging (3.7); lamps inside a closed opaque linked shade light only through its openings (D4); reflections from probes no longer carry lamp light through walls; +128 MiB of video memory |
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
| **P4** Avatar layer (both pools) | S/L split: static renders without avatar types, avatars into L under `LOCAL_SHADOW_DYNAMIC`, restore copies; camera-independent pairing; paired avatars marked visible (attachments, full motion); impostors drawn in full in the dynamic pass (`lldrawpoolavatar.cpp:389`, `pipeline.cpp:3232`, `lldrawable.cpp:1543`, plus every `isImpostor()` site the coder lists: `lldrawable.cpp:1636` and others); coordinator with shared caps; feed tracking owner mask and per-owner lamp roots | `llprojectorshadows.*`, `llprojectorshadowpool.h`, `lllocalshadows.*` (new), `llshadowdirtyfeed.*`, `pipeline.cpp/.h`, `lldrawpoolavatar.cpp`, `lldrawable.cpp` | unit: merged selection never starves, avatar draws ≤ `A_max`; spec gates: an avatar under 3 lamps casts every frame (nearest) and within 4 frames (far); an impostored avatar casts; an avatar behind the camera casts in pose; O-1, O-2, O-6 pass; Legacy guards | frame in the club and the living room within 82178 + 5 %; dynamic CPU ≤ 0.30 ms; report off-screen animation cost, the CPU of an avatar-only `renderShadow`, GPU per avatar draw | walk under 3 lamps; a dancer under club lights | 3 to 6 h |
| **Q0** Measure | census (3.10); `local lights` pass scope; `point_faces_bench.py`; `bench_point_lookup.py` into `scripts/shadows/` and run again on devel; budget page `doc/shadows/point-shadow-budget.md` with empty tables | `llprojectorshadowstatsapi.cpp`, `pipeline.cpp/.h` (stats accessor, scope), scripts | `check_release_hooks.py` on the release-bound diff; benches on an idle GPU | adds nothing to a release frame | session: living room, bedroom, outside (frame, census, coverage) | 2 to 3 h + session |
| **Q1** GL-free core | `llpointpool`: face table and matrices, pick with ties, guard band per tier, sizes and demand, tier rule with hysteresis, face masks, merged items, lag accounting; the GLSL twin of the face pick | `llpointshadowpool.h`, `tests/llpointshadowpool_test.cpp`, ci-checks step, `scripts/shadows/point_faces_g1.py` | P-1 pick against brute force on 10⁶ directions with exact ties; P-2 every tap of a seam receiver stays inside its tile for each tier's kmax; P-3 hysteresis never oscillates on a recorded walk, invariant under camera rotation; P-4 capacity (256 lamps at the floor ≤ 40 % of 4096); P-5 an upgrade keeps old faces until six new are drawn; P-6 lag bound; P-7 no pool starves; **G1-P**: GLSL face pick, uv and depth equal the C++ on shared vectors (pick exact, uv and depth ≤ 1 ulp) | none at runtime | — | 1.5 to 3 h |
| **Q2a** Faces, cheap tap, avatars | `LLPointShadows` (atlases through `llshadowatlas`, sphere cull + mask submits, two-sided, emitter near planes, pins, S → L, avatar faces); filter refactor; `samplePointShadow` cheap tier; `pointLightF` and batch permutations; uniforms and binding; stats | `llpointshadows.*`, `llshadowatlas.*`, `pipeline.cpp/.h`, `shadowStochasticUtil.glsl`, `pointLightF.glsl`, `multiPointLightF.glsl`, `llviewershadermgr.cpp`, `llshadermgr.cpp`, `check_legacy_glsl.py` | G1 stays max abs 0 (projectors); G1-P extended to the lookup (atlas against a standalone face); Legacy guards; static scene: 0 face renders a frame after the fill; O-1 style orbit on point faces (CRC unchanged while the camera orbits); light stops at a test wall; method and detail switches, window toggle, quit without GL errors; smoke launch | living room within 82178 + 5 %, the lookup ≤ 0.35 ms GPU, per-frame CPU ≤ 0.05 ms in a static scene; fill time and avatar lag reported | Release in-world: the living room (bleeding gone), walk for avatar shadows | 4 to 8 h |
| **Q2b** Near tier | tier rule live (K, hysteresis), 256/512 faces with G 16, PCSS program, upgrade and downgrade paths | `llpointshadows.*`, `llpointshadowpool.h`, shaders, shader manager | P-3 and P-5 in-world: walk through doorways, the tier log shows no flapping; no frame with a near lamp unshadowed during the switch | the near lamps' GPU ≤ 0.2 ms each at full screen; total within +5 % | walk into the living room and out; recording reviewed for size, shape, edges and stability | 2 to 4 h |
| **Q3** Levers | (a) hierarchical min/max early-out, a fragment pass per face draw, exact; (b) empty-face skip (a face with no caster: no sampling); (c) two-tier filter refinement: the near tier's lighter filter (8 + 16 taps) beyond a position-based distance with hysteresis (pool P5a); (d) importance skip: no lookup where the lamp's contribution is under one 8-bit step; (e) single-face perspective cull for one-face redraws. Only the ones Q2 shows are needed | shaders, `llpointshadows.cpp` | (a) and (b): image difference 0; (c) and (d): measured image change, penumbra at N and N/2 | each lever's gain measured; total within +5 % | — | 2 to 4 h |
| **Q4** (optional) Forward alpha | cheap tap in the 7 forward slots | forward shaders, `setupHWLights` | Legacy GLSL guard for the forward shaders; alpha in a lamp room against the deferred surface behind it | measured | glass and hair in the living room | 1 to 2 h |

**Total:** about 15 to 30 h of pipeline time, two to three days of wall clock, plus sessions for P4, Q0 and Q2.

## 8. Decisions for the owner (recommendation first)

| | Decision | Options (recommended first) |
|---|---|---|
| **D1** | What a face stores | (a) **the face's perspective depth (D32F), the projectors' format and filter; metres along the ray in the lookup.** Seams are safe through the guard band, early-Z is kept, and the caster programs are unchanged. (b) Radial distance through `gl_FragDepth` (LearnOpenGL, Gerasimov): a new variant of every caster program, no early-Z. (c) An R32F colour target (ogldev): +64 MiB per 4096 atlas, and a colour write in every caster program |
| **D2** | Number of near lamps K | (a) **2** (confirmed after Q0); (b) 1; (c) 3 |
| **D3** | Point atlas size | (a) **4096 on every card from 3 GB (2048 below): 128 MiB**, enough for 59 lamps at 52 %; (b) the projectors' rule (8192 from 11 GB dedicated, 512 MiB) |
| **D4** | What casts into a lamp of its own linkset | (a) **the emitter prim is cut by the per-face near planes; the rest of the linkset casts** (the fixture). Lamps inside a closed opaque shade light only through its openings, the same look change the projector ambiance made; Q0 counts how many of the owner's lamps that touches. (b) The whole lamp linkset casts nothing: light leaks through shades as today. (c) Only linked prims that contain the light centre are cut |
| **D6** | Forward alpha | (a) **unshadowed in Q2; Q4 after the owner has seen Q2**; (b) in Q2; (c) never |
| **D7** | Face rendering | (a) **one sphere cull, one submit per face in the mask (core GL, no new shader variants)**; switch to (b), the vertex-shader `gl_ViewportIndex` with instancing, only if Q0 shows at least 2x lower CPU and the extension is present on AMD and Intel GL 4.6 drivers; (c) one cull per face |
| **D8** | What turns it on | (a) **Stochastic, detail 2 ("Sun/Moon + Projectors"), with a debug setting `RenderShadowPointLights` (default on) for A/B; no Preferences entry**; (b) a new detail level 3 |

The other open points of 2026-11-14 are settled here: D2 of that list (pin for every lamp) is the LOD pin for every lamp
(3.6), and D3 is D3 above.

## 9. Risks, and what was not verified

**Risks**
- **Cost:** the top of the estimate exceeds the measured headroom (section 5). The levers and cut order are planned; Q0 decides.
- **Avatar layer:** with many lamps the GPU cost is real and the far lag is visible (3.7). It needs the owner's eye at Q2a.
- **Look change (D4):** lamps in closed shades go dark outside their openings.
- **Fill transient:** with method A each face goes through the vertex stage for every draw in the sphere (up to about 1 ms
  GPU per frame during the fill) [E].
- **Thin walls** at 64-texel faces leak along their base (section 6).
- **Filter refactor:** if bit-exactness to G1 fails on some compiler, the code is duplicated instead (a known fallback for
  the code, not a runtime path).
- **Shared code touched:** the `renderShadowImpl` split, the two-sided flag and the feed's owner mask. Each is checked by the
  Legacy guards and the projector orbit gates.
- **LOD pin with no ceiling** across lamps: a lamp over a dense room pins all of it in the main view too. `pinned_triangles` reports it.

**Not verified**
- `ARB_shader_viewport_layer_array` on AMD and Intel GL 4.6 drivers.
- That `updateFrustumPlanes` with an orthographic box gives the cull exactly the sphere's box (the sun cascades suggest so).
- Whether SL-11353's `stateSort` skip removes batched geometry.
- Whether the water reflection pass draws local lights.
- All in-world numbers: coverage, draws per sphere, avatar counts, the rooms' headroom, off-screen animation cost.
- The 2026-11-14 lookup bench was not run again here: a viewer build was running, and GPU timings taken then are unreliable.
- Scalability on AMD and Intel GPUs.

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
same way. The 2026-11-14 summary's 13 to 17 days for Q0 to Q3 corresponds to about 13 to 17 hours. With P4 and the
optional Q4 added, this plan's total is **15 to 30 hours**. Viewer builds (one at a time on the owner's PC) and owner
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
- LearnOpenGL *Point Shadows*: code CC BY-NC 4.0, not GPL-compatible. Used for the logic only, and no code is copied.
- ogldev tutorial 43 (Etay Meiri): GPL-3.0-or-later. This plan takes no code from it (the six-pass structure and the
  `FLT_MAX` clear are ideas, not code). If a stage copies any, that stage adds the attribution to `NOTICE.md` and
  `doc/licensing.md`.
