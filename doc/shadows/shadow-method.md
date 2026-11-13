<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Shadow method: Legacy and Stochastic

`RenderShadowMethod` (U32, saved) selects how the sun and moon shadows are fitted and filtered:
0 = Legacy, 2 = Stochastic. Value 1 was PCSS, which is removed and stays unused (see "PCSS is removed"
below). Preferences > Graphics > Rendering 2 shows the setting as a "Legacy / Stochastic" combo next to the
Shadows heading. It replaced the "Soft shadows (PCSS)" checkbox (`RenderShadowPCSS`).

| | Legacy (0) | Stochastic (2) |
|---|---|---|
| Sun/moon cascades | Firestorm 7.2.5's: split distances from the visible point cloud, the camera-angle blend of the split exponent (x, y, z), each cascade fitted to the visible receivers, orthographic or perspective-warped | Stable (stage S1, `llshadowcascadefit.h`): splits from the shadow distance, each cascade the bounding sphere of its frustum slice, texel-snapped on a world-anchored grid, orthographic |
| Sun map size | Screen-sized (`BlurHappySize(width)` x `BlurHappySize(height)`) | Square, same texel count (`sqrt(width * height)`) |
| Filtering | PCF with the receiver-plane bias | 8 rays per pixel toward the light's disc, accumulated over time |
| Sun N·L under shadow | Legacy `min(N·L, shadow)` | N·L × shadow (`SHADOW_NL_MULTIPLY`) |
| Later shadow stages (S2-S9, S7b) | No | Yes (S2 below). The deferred sun pass uses the world-unit bias and the geometric receiver plane; the forward receivers (alpha, PBR, water) use Stochastic's own util with fixed rays |
| Settings used | `RenderShadowSplitExponent` x/y/z, `RenderShadowErrorCutoff`, `RenderShadowFOVCutoff` | `RenderShadowDistance`, `RenderShadowSplitExponent` y, `RenderShadowLightSize` |

A device below OpenGL 4.0 runs Stochastic as Legacy (`LLShadowMethod::effective`).

## PCSS is removed

PCSS (value 1: rotated Poisson PCF, blocker search, contact hardening, the `PCSS_SHADOW` shader build and the
`pcss_shadows` blur gate) was removed from `vkstorm-devel` once Stochastic replaced it. The code is kept in the tag
`archive/pcss/2026-11-11` (the devel tip before the removal), and the removal as a reverse patch in
`H:\vulkanstorm\archive-pcss\pcss-devel-2026-11-11.patch` (`git apply` on the post-removal tree brings it back; the
folder's README.txt says how). `vkstorm-release` still has PCSS until the stochastic integration PR lands there.

- `LLShadowMethod::RETIRED_PCSS = 1` stays unused, so 2 keeps meaning Stochastic in stored settings and presets.
  `DEFAULT = STOCHASTIC`: a stored 1 and any other unknown value read as Stochastic (`sanitize`), which `effective`
  turns into Legacy where Stochastic is not supported. At startup a stored 1 (or unknown value) is also written
  back as 2, so the combo shows it. A preset file that stores 1 selects Stochastic the same way.
- The `RenderShadowPCSS` checkbox migration (once per install) now maps on to Stochastic, off to Legacy; the
  preset rule is the same.
- `RenderShadowPCSSLightSize` is renamed `RenderShadowLightSize`. At startup a stored old value moves to the new
  setting unless the new one is already set (it wins), and the old setting is reset to its default (it stays
  declared, hidden and retired, so the user file can be read). A preset file with the old key and no new key maps
  it on loading. The rules are `migrateLightSize` and `presetNeedsLightSizeMapping` in `llshadowmethod.h`.
- Feature tables: every list that recommended PCSS (1) recommends Stochastic (2); the device gate keeps GL < 4.0
  on Legacy.
- Kept, as they serve Stochastic (and Legacy later): the stable cascades (S1), the method-neutral
  `SHADOW_COMMON` helpers (S2), the N·L multiply, and the stochastic util and resolve. Legacy is unchanged.

## Stochastic (2)

Stochastic replaced PCSS and shares nothing with it. It uses
the stable cascades and square sun maps of stage S1, only the method-neutral helpers of stage S2 (the
`SHADOW_COMMON` block of `shadowUtil.glsl`), and the light's disc size from the `shadow_light_size`
uniform (EEP `sun_scale` times `RenderShadowLightSize`). It is built on the stochastic transparency's
engine: `LLStochasticHistory` and the resolve object (the history, camera reprojection and the 3x3 clamp,
weight 1/8). The estimator's noise is its own (interleaved gradient noise of the pixel and the frame), not
the transparency's hash and mask table. 8 rays per pixel in every program (the forward ones too, with fixed
rays), full resolution, no other mode and no user setting. If the history cannot be
allocated, only the accumulation is turned off, with a log line, and the 8 rays per frame stay, fixed per pixel,
still Stochastic. No run-time fallback (owner decision Q8, 2026-11-13, replacing the 2026-11-12 exception): if the stochastic
transparency targets cannot be allocated or a stochastic shader does not build, the failure is logged, the selected
methods stay (no switch to sorted alpha or Legacy shadows, no notice) and the retry comes at the next natural point (the
allocation backoff, the next shader reload). Devices below OpenGL 4.0 are Legacy and sorted by design
(`LLStochasticEngine::usable()` is the capability).

M0 to M2 shipped as one PR.

### M0: the plumbing

- `LLShadowMethod::STOCHASTIC = 2`. `usesStableCascades` is true for every method but Legacy. The gates
  "not Legacy" are the square sun maps (`allocateShadowBuffer`), the `shadow_light_size` uniform of the sun
  pass, and the light-size slider. Stochastic's own denoising is the temporal accumulation (M2), independent of
  the light-map blur, which runs with SSAO only.
- `shadowStochasticUtil.glsl` is Stochastic's shadow util. `loadBasicShaders` builds it only when the
  method is Stochastic, and `LLShaderMgr::attachShaderFeatures` then links it, instead of
  `shadowUtil.glsl`, into every program that links a shadow util (sun, probe, spot and forward). The
  method-neutral block is built alone from `shadowUtil.glsl` (`SHADOW_COMMON_ONLY`, with the S2 defines
  from `addShadowCommonDefines`) and linked next to it, in two builds: for the deferred sun programs
  (permutation `STOCHASTIC_SHADOW_SUN`, which read the depth buffer for the geometric receiver plane) and
  with `SHADOW_COMMON_NO_DEPTH` for every other program (the shading normal is the plane; no
  `getPosition`, so programs without `deferredUtil.glsl` link). None of the groundwork's `SHADOW_*` defines is global (the neutral block's go to its two builds only). The
  program binary cache hashes defines, not attached objects, so the method is part of the program hash
  (`LLGLSLShader::sShadowUtilVariant`, hashed only when not 0: Legacy keeps its cached binaries). `bindDeferredShader` sends `shadow_light_size` to every program, the
  probe pass included.
- Capability gate, decided at load: below OpenGL 4.0 (`LLStochasticEngine::deviceSupported`, as for the stochastic
  transparency) a device that cannot run Stochastic has no Stochastic: the combo drops the "Stochastic"
  item and a stored 2 reads as Legacy (`LLShadowMethod::effective`, unit tested). The setting is never written, so the
  choice survives a GPU upgrade. This is a capability choice, not a run-time fallback.
- The combo has the items "Legacy" and "Stochastic". `scripts/tests/test_shadow_method_tables.py`
  checks that the feature tables recommend 0 or 2 and never 1, the combo, that PCSS is gone from the shaders and
  the shader manager, and that the util has no groundwork defines.
- No debug settings: the ray count (8), the history weight and the resolution are fixed by design.
- Legacy is untouched (`scripts/shadows/check_legacy_shadows.py`).

### M1: the estimator

Per pixel, 8 rays toward points on the light's disc (a Vogel spiral, rotated per pixel and frame by
interleaved gradient noise of the pixel and `shadow_st_frame`), each marched through the cascade's
depth map in 2 to 8 steps. The maps are read as plain depth (`texelFetch`): `allocateShadowBuffer` sets
their compare mode off in this method, so every program that samples them is a Stochastic program. A ray
is blocked when it crosses a stored surface between two march nodes (the first interval starts at the
receiver), or is still behind one at the end of the march. The pixel's value is the fraction of free
rays. The march reaches 4, 8, 16 and 32 m in cascades 0 to 3, and no farther than the cull guard's
footprint at the disc's angular radius. The steps follow from the cascade's texel size. The receiver
plane, the world-unit bias and the normal offset are S2's helpers. The cascade is drawn per pixel by the
weights of the other methods' blend bands. A projector is the same estimator toward its emitter (a disc of
radius 0.075 m, a per-slot uniform), 4 steps over at most 4 m. The disc-edge profile converges to 0.84 to 0.87 of a disc light's
width on the bench (within one map texel plus one pixel).

The march is split into `stMarchRay` (one ray, 1 when blocked) and `stMarch` (the mean of the 8), and a light's visibility is
`stTransmittance` (the rays toward one light's emitter disc, marched through its map). A projector's receiver is described like the
sun's: `shadowReceiverGeometry` gives the geometric normal from the depth buffer for the receiver plane and the normal offset,
because the shading normal of a bump or normal map tilts both and striped the lit surface in columns.

What a projector's march excludes, exactly: its own prim, and nothing else. The own prim is not drawn into the projector's map
(`stateSort` skips it) and the map's near plane lies at the prim's face, so the stretch from the eye (the apex, behind the face) to
the face holds no depth. The march therefore ends `r_excl` short of the eye, with `r_excl` = that stretch plus a 5 cm margin
(`LLLocalLight::projectorExclusion`, `lllocallight.h`), and a stored depth nearer the eye than that along the receiver's line is no
surface (`z_floor` of `stReference`; 0 for the sun, which is unchanged). That plane is at the eye's distance `r_excl` along the
receiver's line, so for rays off the axis it lies nearer than the margin's depth: it can only exclude less than the margin, and
everything past the face casts. Linked parts of a fixture (a shade, a housing beyond the face), a partition or a table's object
in front of the face all shade the light. Receivers nearer than `r_excl` march a sliver and are mostly outside the map's depth range.
The pipeline sets `spot_emitter_radius` (0.075 m, a per-slot uniform) and `spot_shadow_excl` for the two slots in `bindDeferredShader`.
A point light (U2, not used yet) will exclude half its own drawable's extent (`exclusionRadius`; `LLDrawable::getRadius` is the full
scale diagonal). `scripts/perf/test_local_light_u0.py` gates this stage: the sun channel equals the previous code bit for bit (raw
and resolved), the bump no longer changes a projector's result, a housing inside the stretch does not black out the light while a
shade plate 0.1 to 0.5 m in front of the face, a thin partition and an object over a table all cast, and the sun pass costs at most
0.02 ms more.

`shadow_st_frame` is the frame of the seeds. The deferred sun pass sets it from the history while it
accumulates and to 0 otherwise; every other program leaves it at 0, which fixes each pixel's rays (a
static grain, 6 to 9 % in a penumbra). The reference depth of a read is the receiver's plane at the texel
actually read (S2's signed one-sided form), so a lit plane under a low sun stays lit.

### M2: the history

`LLStochasticHistory` (two full-size colours, counted in the texture memory: r sun, g linear view
depth; RG16F, 4 bytes a pixel each, RGBA16F on the compute A/B path; the projector slots were b and a until unified
lighting U1 moved them to the local light ratio, below) is made on the main view when the method is Stochastic, freed
otherwise and at a size change. The sun pass writes its rays as they are into `mRT->screen`;
`shadowStochasticResolveF.glsl` blends them with the last frame's history (weight 1/8) after the engine's
camera reprojection, reading the history from the 4 nearest texels, each kept only where its stored depth
agrees (a revealed surface has none; under a quarter kept, none), clamped per channel to the 3x3 neighbourhood
of this frame; `shadowStochasticComposeF.glsl` makes the light map of the result (r) and this frame's AO (g; b and a are 1).
The sun takes this frame's rays alone when its direction moved more than 0.5 degrees (the sun/moon handover, an editor
drag); a projector slot whose light, position or direction changed does the same for the local light ratio (below). Cube
snapshots and mirrors have no history (fixed seeds, the sun pass writes the light
map). A snapshot sums 8 sun passes of different rays (`shadow_st_frame` 1 to 8) and averages them (the light
map pass divides by 8). With SSAO on the light-map blur still runs, for the AO channel only when the
history or the snapshot's passes made the shadow channels, and for all channels when there is no history to take
them from. The resolve is the transparency's: the engine decides once (`LLStochasticEngine::computeResolve`), and the OpenGL viewer
runs the fragment resolve on every device (`shadowStochasticResolveF.glsl`, into the history's framebuffer; it needs no
extra target; the history is 2 x RG16F, 4 bytes a pixel each, RGBA16F on the compute A/B path). The compute resolve (`shadowStochasticResolveC.glsl`,
the history written as an image) is kept compiled for A/B tests (`VKSTORM_STOCH_COMPUTE_RESOLVE=1` on a GL 4.3 device)
and for the Vulkan renderer: an OpenGL compute dispatch and its `glMemoryBarrier` between graphics passes cost frame rate
while the camera or avatar moves, and OpenGL has no async compute. Both run the same
resolve source (`shadowStochasticResolveUtilF.glsl` and the engine's `stochasticResolveUtilF.glsl`), and
the offline bench finds their histories bit identical frame after frame. A Windows driver that does not load the compute
entry points cannot be forced to compute (the fragment resolve is the default everywhere anyway).

### Unified lighting U1: the light/shadow split

The projectors' visibility no longer rides the sun pass. The architecture is one stochastic core and three pools with one-way
interfaces: transparency -> shadows -> light. Nothing a later pool produces enters an earlier one. The light pool publishes a table
of its lights and a pure function of them (`localLightW`); the shadow pool calls that function to weight each light and returns
the visibility as a ratio; the light pool multiplies by it. That is a code dependency on a light-pool function, not a flow of
light-pool results into the shadow pool.

- **Core noise.** `stochasticNoiseUtil.glsl` has one function, `stNoiseStream(px, frame, stream)`, with named streams: SUN_RAYS
  (0, the shipped formula, bit identical), LOCAL_PICK (1), LOCAL_RAYS (2), CASTER_COVERAGE (3), TRANSPARENCY (4). **Streams 1, 3
  and 4 are reserved and wired in later stages** (the pick in U2; the caster coverage and the transparency pool when their code
  moves onto the core). In U1 LOCAL_RAYS uses the SUN_RAYS formula, so the projectors' rays are the ones U0 drew and the image is
  the same; U2 gives it its own offset together with the pick, and its gate checks that the two streams are decorrelated.
  #486's three IGN copies in the caster shaders stay on their own phase for now: moving them to CASTER_COVERAGE changes their
  noise (the stream adds an offset and a phase), so it is not bit identical, and it is done in the stage that retunes the casters
  (the transparency interface), with that stage's own gate.
- **Light pool: the table and the tile list** (`lllocallighttable.h`, header-only, tested standalone and in CI).
  `buildLocalLightTable` lists the lights `renderDeferredLighting` draws (the same cut by `RenderLocalLightCount` and the same
  filters; the code says to keep them together), the mapped projectors first, as rows of 8 RGBA32F texels (centre and radius,
  colour and falloff, origin and kind, the projector texture's average luminance, the projector matrix) and a list per 32 x 32
  pixel screen tile (at most 32 lights each, from each light's projected sphere box; a light the camera is inside covers the
  screen) as an R8UI texture. Only the tiles inside the screen rectangle the mapped projectors' frustums cover are built and
  uploaded. `localLightWeightUtil.glsl` is the light pool's side: the weight of a light, which the ratio program lists among its own
  files. Which row holds which projector slot's light, and whether it changed, are per-slot uniforms of the ratio pass (they
  are shadow-pool facts, not columns of the table).
- **What happens when it does not fit.** The ratio is a sum over the lights listed in the pixel's tile, so a light that is drawn
  but missing from it would be multiplied by a ratio computed without it. The table holds 256 lights and a tile 32. A light that
  does not fit (beyond the table, or dropped from a tile it reaches) is taken out of the ratio altogether: its pass is not
  multiplied by it (`local_ratio_on` is set per draw, from `mLocalRatioLights`; the fullscreen point lights are batched apart for
  it), and the tile lists are made again without it. The simpler alternative, drawing only the table's lights while the ratio is
  on, would make lights vanish. The first frame it happens is logged (once per session).
- **Shadow pool: the ratio.** `localLightRatioF.glsl` makes, per colour channel, R = sum w_i V_i / sum w_i over the pixel's tile
  list, with w_i = `localLightW`, the light's unshadowed contribution (colour x `calcLegacyDistanceAttenuation` x n.l, inside the
  projector's frustum for a projector, times the projector texture's average luminance) and V_i the projector map's visibility
  (`stTransmittance`, with the slot's fade) for the two mapped lights and 1 for every other. A pixel both maps leave lit is
  R = 1 without a loop. Both passes draw only inside the screen rectangle the mapped projectors' frustums cover (their corners
  through the camera, the whole screen when one reaches the camera plane); outside it R = 1, which the clears write.
  - *Main view:* the raw ratio (rgb) and the share of its light that comes from a projector that changed since last frame (a) go to
    the scratch screen target; `localLightRatioResolveF.glsl` blends it into its own `LLStochasticHistory` (RGB10_A2, 4 bytes a
    pixel each, made up front with the sun's, which shrank by as much) with the engine's reprojection, bilinear read and 3x3
    clamp. The history has no depth channel, so a texel is kept where the sun history's depth of the same frame agrees
    (`stoch_history_bilinear_ext`); the changed share takes this frame's value. `shiftObjects` invalidates it with the sun's.
    If the history could not be allocated (logged once per size) the ratio is read as made each frame, without the resolve.
  - *No history:* a mirror, a hero probe and a reflection probe make one pass with fixed seeds (frame 0) into an RGBA16F target of
    the view's size; a snapshot makes 8 additive passes (frames 1 to 8) and the light passes read the sum with the scale 1/8,
    as the sun's 8. The target is released with the snapshot.
- **Light pool: the passes.** The point, multi-point, spot and multi-spot programs (permutation `LOCAL_LIGHT_RATIO`, Stochastic
  only) multiply their direct light by R (the projector's ambiance and the sun are not touched). With no mapped projector, no
  ratio is made and the passes skip the fetch, exactly: every V is 1. Linking the four passes with the objects the viewer attaches
  (`scripts/perf/check_local_light_passes.py`) shows 5 to 7 sampler units kept, so the added sampler is within 16 units.
- **The ratio estimator is exact for the diffuse part and approximate elsewhere, by design.** Where the weights are proportional
  to the passes' light (diffuse), sum_i C_i R = sum_i C_i V_i. Specular and metallic light are not proportional to the weights
  (the weights are diffuse-dominant): where one light dominates the highlight and another the diffuse term at the same pixel with
  different visibility, the highlight of one is dimmed by the other's shadow. That is inherent to the ratio estimator (Heitz et
  al. 2018 accept it) and accepted. A projector's texture is not in the weights until the projector texture array (P3), but its
  average luminance is (read back once per texture from its coarsest mip), so that another light at a pixel the projector shades
  is not dimmed by the projector's untextured share where the texture is dark; where the texture varies across the frustum, the
  difference remains (printed by `test_local_light_u1.py`). Other lights' movement does not invalidate the ratio's history; the 3x3
  clamp limits the lag to a few frames.
- **Sun.** The sun pass drops the projector channels (the light map's b and a are 1); the sun history is RG16F.
- **Threads.** Render thread only: the table and tiles are built in `renderDeferredLighting` and freed with the screen buffers.


### Unified lighting U2: every light casts

The ratio pass no longer needs a mapped projector. Every local light of the table that lights a visible pixel casts, by one
stochastic pick per pixel and a screen-space march (Stochastic only; Legacy and the sun are untouched).

- **The estimator** (`localLightRatioF.glsl`, the shadow pool). Over the pixel's tile list the pixel computes each light's weight
  (`localLightWeight`: the light pool's) and picks ONE light j with probability p_j = ws_j / sum ws: a reservoir with one uniform
  number, remapped after each candidate (u = take ? u / pp : (u - pp) / (1 - pp)) so the draws are independent and the pick is
  exactly proportional (a golden-step version was off by up to 43 % per list position; the gate measures under 0.5 %). It shoots 2
  of the 8 rays at j's emitter disc (the pixel's lane of its 2 x 2 quad; the quad shares the rays' rotation, LOCAL_RAYS stream),
  each a march through the camera's depth buffer (`stMarchRayScreen`, in the core: `stochasticScreenMarchUtil.glsl`) and, where j is
  one of the two mapped projectors, also through its depth map ("blocked by either": a map only adds shadow; the slot's fade-in
  now weights the map's blocked rays, where U1 clamped visibility + fade). The occluded part of the pixel's light is
  D_c = a (blocked / rays) w_jc / p_j, R_c = 1 - D_c / sum_i w_ic, unbiased. The quad's four pixels average D over the pixels with
  light at the same depth (3 %, `dFdxFine`; without `GL_ARB_derivative_control` or GLSL 4.50 the program keeps each pixel's own
  estimate). The resolve (unchanged) averages the frames.
- **No pop.** There is no per-light cut at a share of its own peak. A pick under 0.5 % of the pixel's total light is lit
  unshadowed, weighted by a = smoothstep(0.5 t, t, ws_j), t = 1 % of the total, and its march is skipped: continuous along a light's
  radius (the first version's 2 %-of-peak cut popped the ratio by 0.8 where the true shadow was 0.2 to 0.4).
- **Signed estimate, wide history.** A single pick's share of a channel can exceed the channel's total, so the estimate is written
  unclamped; the ratio history is RGBA16F and the light passes clamp on read. (The clamp had biased a red lamp's shadow by +0.013.)
  The engine's shared 3 x 3 neighbourhood clamp in the resolve is unchanged; with a noisy raw it cuts the history's skewed tail and
  biases a coloured shadow by about +0.018 (measured by the shader expert on a red/blue scene): known, for the engine's owner.
- **The march.** Steps clamp(ceil(px length / 32), 2, `RenderLocalShadowStepsMax` (debug, 4 by default, clamped to 2 to 8)), dense
  near the receiver, a jitter per ray, stopped at the near plane read from the projection. A surface in the depth buffer blocks where
  the ray is behind it by at most 0.3 m (`ST_SCREEN_THICK_MIN`) or the step's depth span, and a hit within r_excl + 5 cm of the
  emitter's eye is the light's own housing and does not block. Thickness 0.5 m threw a halo (a pole 0.35 m in front of a wall
  shadowed the wall: mean 1-R 0.07); 0.3 m passes (0.003) and 0.15 m is exact but catches a thin shelf less. Club acne is
  independent of it (the step span dominates on long rays). It sees only what the depth buffer holds: occluders off screen, back
  sides of thick objects and things thinner than the step spacing are missed or caught partially (a 2 cm shelf: mean R 0.83 in its
  umbra). `RenderLocalShadowFloorTier` (debug) shoots 1 ray per pixel: fewer rays, a little cheaper, more noise; the owner's 4 x 4
  block floor is not built.
- **Emitter and exclusion** (table texel 3; `lllocallight.h`). Point lights: s = half the median scale, exclusion radius = half the
  drawable's radius (the scale diagonal) clamped to s .. 1 m: the own prim is excluded and linked parts cast. Projectors: s from the
  face (`projectorEmitterRadius`, replacing U0's fixed 0.075 m also for the maps) and U0's stretch to the face + 5 cm.
- **Worn lights** cast under #437's caps, for shadow purposes only: the table's colour is capped at max component 1.0, the weights
  and the march reach stop at 6 m, and beyond 6 m the light is lit unshadowed (out of the pixel's weights); the light passes keep
  the real light. The code had no other worn-light limit: attachments pass the same filters as any light.
- **Noise.** LOCAL_RAYS and LOCAL_PICK have their own streams: the pixel lattice turned by a per-stream angle with its own
  multiplier, R2 offset and phase. SUN_RAYS is unchanged (the sun pass is bit identical to U1).
- **Table and tiles.** Every light is in the table (the mapped projectors first), in a std140 uniform block of 256 rows (32 KB,
  orphaned each frame; a texture on a device whose blocks hold less): a warp reads one row, which the constant cache broadcasts
  (the texture pipe was the loop's bound). The screen rectangle of a light is its sphere at 0.9293 of its reach (beyond it the weight
  is under 0.5 % of the light's peak), cut by a projector's frustum with the four far corners at that reach from the eye; the
  ratio's rectangle is the union. The 32-per-tile rule on the CPU lists is unchanged (a light that misses a tile leaves the ratio).
  The GPU tile cull (main view) then drops the lights that have no weight on any pixel of a tile, by the tile's depth bounds of
  4 x 4 pixels (`localLightBlockF.glsl`, `localLightCullF.glsl`); the ratio is bit identical to the coarse lists' (the costs are in the PR).
- Gates, costs and what is not verified: the PR.

## Shared by all methods

These are not gated by the method:

- the light model: L2 cloud shadows, L3 `primary_light_dir` / `primary_shadow_strength`, the
  caster direction that follows the sun/moon handover, skipping the cascades while the shadow is
  invisible;
- three Vulkanstorm fixes made to the stock shadows: the sun-bias altitude term is gone, stock PCF
  has a receiver-plane bias (`pcfDepthGradient`), and the projector shadow fades with the
  Euclidean camera distance and `spot_shadow_reach`, not with the sun cascade splits.

So Legacy is Firestorm 7.2.5's cascade fit and map sizes with the PCF shader as it is today
(the stock `shadowUtil.glsl`); only the cascades and the map size differ in the C++ code. The one shader difference is the sun's N·L under shadow: every method but Legacy defines `SHADOW_NL_MULTIPLY` (a global define set in `loadBasicShaders`, `llviewershadermgr.cpp`, where the method is read), which switches `sunShadowedDiffuse` in `deferredUtil.glsl` from Firestorm's `min(N·L, shadow)` to `N·L × clamp(shadow, 0, 1)`. Legacy never defines it and keeps `min()`. In classic-mode PBR (`pbrBaseLight`) the multiply form applies the shadow once, where `min()` applied it twice. The define changes the shader text, so the shader cache is rebuilt once after the update.

## Stage S2: the method-neutral groundwork

`shadowUtil.glsl` has a block that no method owns, `SHADOW_COMMON` (GLSL 1.50, no `textureGather`).
The Stochastic build links it (`SHADOW_COMMON_ONLY`). The Legacy
stages that follow S2 can define `SHADOW_COMMON` to call the same helpers; today Legacy does not define it, nothing in
Legacy calls the block, and its light map stays bit-identical to the devel tip (offline bench). The shader
manager emits the define and every constant the block reads from one place (`addShadowCommonDefines`,
`llviewershadermgr.cpp`), the constants come from `llshadowcascadefit.h`, and the shader cache version
hashes the same list, so changing a constant rebuilds the cached shaders. The bench compiles the block
alone as GLSL 1.50.

- `SHADOW_FOOTPRINT_M` (0.25 m): how far a filter may reach from the receiver, and the same radius as
  the caster cull guard. `casterCullMargin(texel, normal_offset)` is `max(16 texels, footprint +
  1 texel) + normal offset` (`pipeline.cpp`, `generateSunCascadesStable`). The finest cascade (0.58 cm
  texels) goes from 14 cm (16 texels plus 5 cm) to 26 cm; the coarse cascades (11.6 cm texels and up)
  are texel limited and change by one texel. The unit test checks the formula and that the filter's
  reach stays inside it.
- Receiver plane: `shadowGeometricNormal` rebuilds the surface normal from the depth buffer instead of
  taking the G-buffer normal, which is the normal-mapped one and would extrapolate the bumps. Per screen
  axis `shadowAxisStep` picks the neighbour that lies in the surface: the step that is clearly flatter in
  depth (a crease or fold), and where the two are alike the one nearer the G-buffer normal's tangent plane,
  which only breaks the tie (either lies in the surface). So a normal map that tilts a flat wall by 35
  degrees does not bend the plane (the bench checks acne and the shadow's start at the foot with tilted
  normals). The G-buffer normal is the result only where an axis has no usable neighbour (screen edge, depth
  edge on both sides) or the two steps are parallel (tested on unit steps, so it does not depend on the
  pixel size). `shadowPlaneGradient` turns the normal into the signed d depth / d uv of the cascade from
  the rows of the (orthographic) cascade matrix, clamped to `SHADOW_GRAD_MAX`;
  `llshadowcascadefit_test.cpp` checks it against the two-tangent-step plane, with the matrix built the
  way `generateSunCascadesStable` builds it.
- World-unit bias: `SHADOW_BIAS_TEXELS` (1 texel of the cascade at the pixel, times the map's depth scale),
  the normal offset `SHADOW_NORMAL_OFFSET_TEXELS` x sin(angle to the light) along the geometric normal
  (per cascade, replacing the light-direction offset), and the cap of one tap's plane bias,
  `SHADOW_PLANE_BIAS_MAX_TEXELS`. The same length in metres in every cascade. `shadowCascadeSetup` computes
  the per-cascade scales, bias and cap once, and `shadowReceiverGeometry` the normal and its angle to the
  light once per pixel, only inside the shadow distance (sky and far pixels skip the four depth reads);
  Stochastic calls them, and so can any other method. Under Stochastic nothing reads `RenderShadowBias` and
  `RenderShadowOffset`: every program that links a shadow util (the sun and probe passes, and the forward
  `alphaF`, `pbralphaF`, `materialF`, `waterF` and `pbrmetallicroughnessF`) links Stochastic's util, which uses the
  world-unit bias. Only Legacy's stock `pcfShadow` reads them, in those same programs. The projector shadows keep
  their own bias (`spot_shadow_bias`, `spot_shadow_offset`) in both methods.

What stage S2 changed in the image of the stable-cascade methods: no per-pixel bump pattern in the receiver
plane (a normal-mapped wall no longer gets bias that follows its bumps), a bias and a normal offset that are
the same length in metres in every cascade (smaller than before in the near cascades, larger in the far
ones: no more 4 mm in cascade 0 to 70 cm in cascade 3), a filter reach that stops at 25 cm, and a wider caster
guard at the cascade edges (26 cm in the finest cascade instead of 14 cm).

Known limit: at a crease (a wall's foot on the ground) the plane of one surface does not continue into
the other.

## Stage AA-S1: semi-transparent casters let partial light through

Linden Lab's prim and glTF-blend shadow casters discard on a raster-column stipple
(`if (alpha < 0.88) if (fract(0.5*floor(target_pos_x/post_pos.w)) < 0.25) discard;`, in `shadowAlphaMaskF`
and `pbrShadowAlphaBlendF`). The stipple is fixed in the shadow map, so thin foliage (palm fronds) casts fixed
stripes. Under the Stochastic method only, the stipple is gone, the alpha is sampled with a LOD bias of -1
(a finer mip), and a Blending caster lets partial light through, following its alpha.

### The rule

- Blending casters (legacy Blending faces through `renderAlphaObjects` and `shadowAlphaMaskF` with
  `shadow_st_coverage` = 1, glTF BLEND through `pbrShadowAlphaBlendF`, avatar ALPHA_BLEND through
  `avatarAlphaShadowF`) keep a fragment when `alpha > shadowCasterThreshold()`. alpha = texture alpha x face
  or vertex alpha at LOD bias -1; below 0.05 the fragment is dropped whatever the threshold. The blend cutoff
  (`minimum_alpha` 0.598) does not apply. A dropped texel leaves the depth below it in the map (the opaque
  pass is drawn first), so the receiver's march sees the lace's holes.
- Masking casters (`PASS_ALPHA_MASK`, the fullbright mask, avatar ALPHA_MASK) set `shadow_st_coverage` = 0 and
  keep the stock cutoff, binary: the texture alpha below `minimum_alpha` casts nothing. `avatarAlphaMaskShadowF`
  also tests `minimum_alpha` now (the avatar pool sets it to `LLDrawPoolAvatar::sMinimumAlpha` while the casters
  are built for Stochastic); its stipple never ran before, as it was never set.
- Not changed: `pbrShadowAlphaMaskF`, the material masks and grass (`treeShadowF`), and the opaque programs.

User-visible change: Blending glass and sheer material at alpha 0.05 to 0.6 now casts partial shade; below
the blend cutoff it cast none before. Fully transparent texels (below 0.05) still cast none.

### The threshold

`shadowCasterThreshold()` (the same body in the three Blending casters; a test compares them) is
`fract(IGN(gl_FragCoord.xy) + hash(layer, 4 x 4 block) + phase)`:

- IGN is interleaved-gradient noise of the shadow-map texel, so a stable cascade gives a world-stable pattern.
- `shadow_st_layer` is constant per drawn batch (`LLDrawInfo::mStochasticSeed & 1023`, the batch's face
  identity; the avatar pass uses one constant), so stacked layers get independent noise. The depth test
  keeps the nearest kept layer, so n layers block with probability 1 - prod(1 - alpha_i), as stacked
  translucent material does: two panes of alpha 0.5 cast 0.75 (offline: 0.751, over 12 layer-id sets 0.747 to
  0.758), not 0.5. Faces of one batch share an id; they do not normally overlap.
- `shadow_st_caster_phase` = `fract(0.618034 * seedFrame())`, reduced on the CPU in double precision (a float32
  counter loses the fraction after about 850 thousand frames). The step stratifies the coverage over time.
  The caster reads the counter when the shadow maps are drawn, which can be the previous frame's value
  relative to the light pass; the caster threshold needs no match with the receiver's rays.

### Wiring

- `add_stochastic_shadow_alpha_permutations` (`llviewershadermgr.cpp`) adds `SHADOW_STOCH_ALPHA=1` and
  `SHADOW_ALPHA_LOD_BIAS=-1.0` to the programs that run those shaders when `use_stochastic_shadows()`, removes
  both otherwise, and records the result in `LLViewerShaderMgr::sShadowStochAlpha`. That is the effective
  method: the run-time stochastic fallback makes it false and reloads the shaders, so the stipple is back with
  the Legacy shadows. Programs: Shadow Alpha Mask and Shadow Fullbright Alpha Mask (each with its skinned
  variant), GLTF Shadow Alpha Blend (and skinned), Avatar Alpha Shadow, Avatar Alpha Mask Shadow. Projector
  maps, reflection-probe shadow passes and the sun cascades draw through the same programs, so all follow the
  method; the local light ratio has its own history (below), so projectors get coverage too.
- `LLPipeline::setShadowCasterCoverage(coverage, layer)` sets `shadow_st_coverage`, `shadow_st_caster_phase`
  and `shadow_st_layer` on the bound caster program. It returns at once unless `sShadowStochAlpha`. It is
  called after each bind in `renderAlphaObjects` (per batch: the layer changes), once per pass for the mask
  and fullbright-mask programs (coverage 0), and from `LLDrawPoolAvatar::beginShadowPass` for the avatar
  blend pass. The avatar pool reads the same flag for the mask pass's cutoff.
- The indexed-texture `diffuseLookup` generated in `llshadermgr.cpp` appends the bias to its `texture()`
  calls only when the program defines `SHADOW_ALPHA_LOD_BIAS`; otherwise the generated text is unchanged.
- Legacy: every changed shader keeps the stock lines in the Legacy branch; with the define absent the text
  is the previous text. `scripts/tests/test_shadow_alpha_permutation.py` compares each Legacy permutation with
  the stock text in `scripts/shadows/legacy_reference/` and checks that every program linking one of the four
  files gets the permutation call.

### The receiver's march and lace

The map holds one layer, and `stMarch`'s `behind` rule (a roof hidden under a higher caster) would count the
lace's own kept texels as a surface the ray had passed, darkening the lace (offline 2-D model of the march:
a = 0.5 lace over flat ground, light disc half-width 0.2 of the depth: 0.20 instead of 0.50). The rule now
needs the surface to be more than a tenth of the way from the hiding surface to the receiver below it
(model: 0.46; the roof-under-caster leak stays closed, 0.000). `scripts/perf/model_shadow_caster_clamp.py`
documents the clamp model.

### Known limits

- The forward receivers (`alphaF`, `pbralphaF`, `materialF`, `waterF`, `pbrmetallicroughnessF`) read the maps
  without history, so the coverage shimmers on them under sheer casters until stage S2.
- Whenever `seedFrame()` is 0 the coverage is a fixed dither: snapshots, cube snapshots, an unsupported
  device, a history allocation failure, and resolve programs not ready (the frame is not accumulating).
  Snapshots stay a dither until stage S4.
- At extreme contact (light rays within half a shadow texel of each other, 4 to 8 receiver pixels per caster
  texel) the 3 x 3 clamp of the resolve raises the noise (offline RMS 0.12 to 0.16 against 0.05); widening the
  clamp stride for the sun channel (stage S2) is the remedy.
- Nothing downstream (the light pass, the later AA stages, the unified lighting) reads the stipple.

## Where the method is read

`LLPipeline::RenderShadowMethod` is a cached setting refreshed on change. A change calls
`handleSetShaderChanged` (shaders reloaded, render targets released and created again, shadow
buffers allocated again), the same path the old checkbox took. The gates are `use_stochastic_shadows()`
(`llviewershadermgr.cpp`: the Stochastic util build and the sun program's `STOCHASTIC_SHADOW_SUN`),
the sun-light uniforms in `pipeline.cpp`, `allocateShadowBuffer` (map size) and
`generateSunShadow` (cascades).

## The Legacy code is checked against upstream

The Legacy code is copied from Firestorm 7.2.5 (`upstream/Firestorm_7.2.5`, 4ac201fdc5) between
`// <ShadowLegacy id="...">` and `// </ShadowLegacy>` markers in `pipeline.cpp`:

- `sun-splits`: `computeSunSplitsLegacy` (the up axis, the visible point cloud, `near_clip`, the
  split planes);
- `sun-cascades`: `generateSunCascadesLegacy` (the per-cascade fit);
- `sun-map-size`: the two `BlurHappySize` lines in `allocateShadowBuffer`.

`scripts/shadows/check_legacy_shadows.py` (in the ci-checks job) compares each region, ignoring
indentation and blank lines, with the stored upstream excerpt in
`scripts/shadows/legacy_reference/*.cpp.txt` (Linden Lab / Firestorm code, LGPL-2.1-only with the
upstream header, stored unchanged as the oracle; the `.txt` suffix keeps them out of source scans). The only difference allowed is `return;` becoming `return false;` in `sun-splits`.
`scripts/tests/test_check_legacy_shadows.py` shows that an edit, a deleted or added line, a missing
or extra region makes the check fail. `--verify-reference <git-ref>` re-checks the excerpts against
an upstream ref (run it, and update the excerpts and the allow-list, in canary upstream sync PRs).

## Defaults, presets, migration

- Feature table (`featuretable*.txt`, all three, no version bump): Stochastic (2) at High, HighUltra and
  Ultra; Legacy (0) at Low, LowMid, Mid, MidHigh and Unknown; 0 in safe mode. The method follows
  the quality slider and the recommended-settings re-applies like every graphics setting, and the
  user can change it afterwards. The combo is disabled (the value is kept) when shadows or the
  feature are unavailable, and the light size slider (Sky tab) is enabled with Stochastic.
- Graphic presets carry `RenderShadowMethod` (`graphic_preset_controls.xml`). A preset file written
  before the switch has `RenderShadowPCSS` and no `RenderShadowMethod`: on loading it, the method
  becomes Stochastic if the checkbox was on, Legacy if off (`LLPresetsManager::loadPreset`).
- One-time migration (`RenderShadowMethodMigrated`): at startup, after the user settings are read and
  before the command line and window creation, an existing install takes `RenderShadowPCSS` as the
  method; a fresh install gets the feature table's value when the window is created. The flag is set
  either way. `RenderShadowPCSS` stays declared, hidden and retired.
- The migration and preset rules are the renderer-free `llshadowmethod.h`, tested by
  `indra/newview/tests/llshadowmethod_test.cpp` (ci-checks).

## Cost

Legacy costs what the PCF with the receiver-plane bias costs. The offline sun-light-pass bench
(`scripts/perf/bench_shadow_legacy.py`) times the devel-tip shaders and this tree's Legacy shaders side by side
and checks that Legacy's light map is bit-identical to the devel tip; Stochastic's costs are measured by
`scripts/perf/bench_shadow_stochastic.py` against Legacy. PCSS's costs (+0.02 to +0.03 ms at High, +0.07 to
+0.13 ms at HighUltra over Legacy) are in the archived study.
