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

Every device that starts meets the OpenGL 4.6 floor, so both methods run everywhere; Legacy is a user choice.

## PCSS is removed

PCSS (value 1: rotated Poisson PCF, blocker search, contact hardening, the `PCSS_SHADOW` shader build and the
`pcss_shadows` blur gate) was removed from `vkstorm-devel` once Stochastic replaced it. The code is kept in the tag
`archive/pcss/2026-11-11` (the devel tip before the removal), and the removal as a reverse patch in
`H:\vulkanstorm\archive-pcss\pcss-devel-2026-11-11.patch` (`git apply` on the post-removal tree brings it back; the
folder's README.txt says how). `vkstorm-release` still has PCSS until the stochastic integration PR lands there.

- `LLShadowMethod::RETIRED_PCSS = 1` stays unused, so 2 keeps meaning Stochastic in stored settings and presets.
  `DEFAULT = STOCHASTIC`: a stored 1 and any other unknown value read as Stochastic (`sanitize`). At startup a stored 1 (or unknown value) is also written
  back as 2, so the combo shows it. A preset file that stores 1 selects Stochastic the same way.
- The `RenderShadowPCSS` checkbox migration (once per install) now maps on to Stochastic, off to Legacy; the
  preset rule is the same.
- `RenderShadowPCSSLightSize` is renamed `RenderShadowLightSize`. At startup a stored old value moves to the new
  setting unless the new one is already set (it wins), and the old setting is reset to its default (it stays
  declared, hidden and retired, so the user file can be read). A preset file with the old key and no new key maps
  it on loading. The rules are `migrateLightSize` and `presetNeedsLightSizeMapping` in `llshadowmethod.h`.
- Feature tables: every list that recommended PCSS (1) recommends Stochastic (2); no device is
  below the OpenGL 4.6 floor.
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
allocation backoff, the next shader reload). A device below OpenGL 4.6 does not start (see "No capability gate" below), so there is no Legacy-by-capability case.

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
- No GL-version gate for the shadow method: the viewer does not start below OpenGL 4.6 (`indra/newview/llglfloor.h`), so there is no
  device that cannot run Stochastic. Legacy stays a valid, stored and selectable method.
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
weights of the other methods' blend bands. A projector is not marched: it is a filtered lookup (lamp shadows L1: a blocker search of 8
gathers on a disc plus one at the receiver's own texels, then a percentage-closer filter of 32 bilinear gathers at the physical
penumbra radius from the nearest blocker, for an emitter of 4.5 cm times `RenderShadowLightSize`, whatever the prim, at least 1.5 and at
most 12 map texels), at a fixed disc of offsets in the map's own uv, with S2's texel bias and per-texel receiver plane. No edge is
reconstructed and no radius is capped in metres: a coarser tile gives a softer edge, never a blockier one (`doc/shadows/projector-shadow-pool.md`,
"The filter"). At the default softness a head's edge on fine tiles widens from about 0.4 texel (the release) to about 1.8 to 1.9 texels:
the default edge's physical width there and the 1.5-texel floor each give about that; see "The default edge against the release" there. The sun cascades' disc-edge profile converges to 0.84 to 0.87 of a disc light's width on the bench (within one map texel
plus one pixel); the projector's straight edges are 0.82 to 0.90 of a disc light's where the radius lies between the floor and the cap
(the CPU twin and the GL bench). The projector lookup costs 9 search gathers, plus 32 filter gathers on penumbra pixels; a lamp whose
tiles hold a see-through caster adds 9 gathers of its light-through layer, and 16 taps of it where a caster lies in front of the pixel
(lamp shadows L3). See the Cost tables in `projector-shadow-pool.md`. Under Stochastic with
detail 2 each projector reads its own tile of the projector shadow pool's atlas, in its own program (`spotLightF.glsl`,
`doc/shadows/projector-shadow-pool.md`), not the two slots through the sun pass.

`shadow_st_frame` is the frame of the seeds. The deferred sun pass sets it from the history while it
accumulates and to 0 otherwise; every other program leaves it at 0, which fixes each pixel's rays (a
static grain, 6 to 9 % in a penumbra). The reference depth of a read is the receiver's plane at the texel
actually read (S2's signed one-sided form), so a lit plane under a low sun stays lit.

### M2: the history

`LLStochasticHistory` (two full-size RGBA16F colours, counted in the texture memory: r sun, g linear view
depth, b and a the two projector slots of the Legacy method: 1 under Stochastic's projector shadow pool) is made on the main view when the method is Stochastic, freed
otherwise and at a size change. The sun pass writes its rays as they are into `mRT->screen`;
`shadowStochasticResolveF.glsl` blends them with the last frame's history (weight 1/8) after the engine's
camera reprojection, reading the history from the 4 nearest texels, each kept only where its stored depth
agrees (a revealed surface has none; under a quarter kept, none), clamped per channel to the 3x3 neighbourhood
of this frame; `shadowStochasticComposeF.glsl` makes the light map of the result (r, b, a) and this frame's AO.
A light that changed takes this frame's rays alone for its channel: the sun when its direction moved more
than 0.5 degrees (the sun/moon handover, an editor drag); a projector has no channel while the pool
draws its shadow (the slots stay empty, so the resolve takes 1). Cube snapshots and mirrors have no history (fixed seeds, the sun pass writes the light
map). A snapshot sums 8 sun passes of different rays (`shadow_st_frame` 1 to 8) and averages them (the light
map pass divides by 8). With SSAO on the light-map blur still runs, for the AO channel only when the
history or the snapshot's passes made the shadow channels, and for all channels when there is no history to take
them from. The resolve is the transparency's: the engine decides once (`LLStochasticEngine::computeResolve`), and the OpenGL viewer
runs the fragment resolve on every device (`shadowStochasticResolveF.glsl`, into the history's framebuffer; it needs no
extra target, the history stays 2 x RGBA16F, 8 bytes a pixel each). The compute resolve (`shadowStochasticResolveC.glsl`,
the history written as an image) is kept compiled for A/B tests (`VKSTORM_STOCH_COMPUTE_RESOLVE=1` on a GL 4.3 device)
and for the Vulkan renderer: an OpenGL compute dispatch and its `glMemoryBarrier` between graphics passes cost frame rate
while the camera or avatar moves, and OpenGL has no async compute. Both run the same
resolve source (`shadowStochasticResolveUtilF.glsl` and the engine's `stochasticResolveUtilF.glsl`), and
the offline bench finds their histories bit identical frame after frame. A Windows driver that does not load the compute
entry points cannot be forced to compute (the fragment resolve is the default everywhere anyway).

## Shared by all methods

These are not gated by the method:

- the light model: L2 cloud shadows, L3 `primary_light_dir` / `primary_shadow_strength`, the
  caster direction that follows the sun/moon handover, skipping the cascades while the shadow is
  invisible (Stochastic also skips them on a dark light, see "Dark gate");
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
  world-unit bias. Only Legacy's stock `pcfShadow` reads them, in those same programs. Legacy's projector shadows keep
  `spot_shadow_offset`; the Stochastic projector lookup offsets by one tile texel along the geometric normal instead
  (the sun's rule); `spot_shadow_bias` is Legacy's only (Stochastic uses S2's texel bias).

What stage S2 changed in the image of the stable-cascade methods: no per-pixel bump pattern in the receiver
plane (a normal-mapped wall no longer gets bias that follows its bumps), a bias and a normal offset that are
the same length in metres in every cascade (smaller than before in the near cascades, larger in the far
ones: no more 4 mm in cascade 0 to 70 cm in cascade 3), a filter reach that stops at 25 cm, and a wider caster
guard at the cascade edges (26 cm in the finest cascade instead of 14 cm).

Known limit: at a crease (a wall's foot on the ground) the plane of one surface does not continue into
the other.

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
  feature are unavailable, and the Shadow softness slider (Rendering 2, below the
  combo) is enabled with Stochastic. `RenderShadowLightSize` (0 to 1.0, 0 the sharpest) defaults to 0.23, the slider's
  centre, and sets how sharp the edges of the sun's, the moon's and every projector lamp's shadows are (it scales only their
  penumbra: a lamp's is that of a 4.5 cm emitter times it, so 1.035 cm at the default); see `projector-shadow-pool.md`.
- Graphic presets carry `RenderShadowMethod` and `RenderShadowLightSize` (`graphic_preset_controls.xml`). A preset file written
  before the switch has `RenderShadowPCSS` and no `RenderShadowMethod`: on loading it, the method
  becomes Stochastic if the checkbox was on, Legacy if off (`LLPresetsManager::loadPreset`).
- One-time migration (`RenderShadowMethodMigrated`): at startup, after the user settings are read and
  before the command line and window creation, an existing install takes `RenderShadowPCSS` as the
  method; a fresh install gets the feature table's value when the window is created. The flag is set
  either way. `RenderShadowPCSS` stays declared, hidden and retired.
- The migration and preset rules are the renderer-free `llshadowmethod.h`, tested by
  `indra/newview/tests/llshadowmethod_test.cpp` (ci-checks).

## Alpha casters

How a semi-transparent texel casts depends on the method.

- Legacy: the stock stipple. A texel below 0.88 alpha is stippled (every other texel column dropped), below 0.05 it casts
  nothing, and blend faces are cut below 0.598 (`ALPHA_BLEND_CUTOFF`) first. The shader text with
  `STOCHASTIC_SHADOW_ALPHA` undefined is the stock text (`scripts/tests/test_shadow_alpha_permutation.py`).
- Stochastic: alpha-mask faces follow the creator's cutoff and the stock 0.05 floor: a hard cut, solid above them, never
  the caster rule (`shadow_solid_alpha` is 0 for the mask and fullbright-mask draws), so a map without blended casters is
  the one it was.
- Stochastic, blended faces (classic, glTF, rigged, and the avatar's own blended parts: skirt, eyelashes, hair), since
  2026-11-18: the Aurora Viewer's caster rule (stage C1 of the Aurora port; `shadowStochasticCasterF.glsl` after its
  `shadow_alpha.wgsl`, Anne Skydancer, GPL-3.0-or-later). A blended face casts R = floor(4 alpha + xi) of the 4 texels of
  each 2 x 2 block of the map, a random R-subset, so the map holds its opacity as a coverage that the receivers' filter
  averages back (Enderton et al., "Stochastic Transparency", I3D 2010, section 3.1). Independent layers multiply their
  transmittance, as light through two sheers does: two layers of 0.5 pass 0.25 of the light (the former hashed per-texel
  dither was one field shared by every layer, so stacked layers cast max(alpha): two layers of 0.5 passed 0.5). No cut (owner decision D4): a 3 %
  sheer casts 3 % and a 98 % pane lets 2 % through (`shadow_solid_alpha` is 1 for the blend draws, the classic blend faces'
  minimum alpha 0; the glTF and avatar shaders test `alpha < 1.0`).
  - xi is the stochastic engine's dither at the block (`stoch_xi24`: the Aurora Viewer's blue-noise tile from the engine's
    table, rotated by the layer's own offset; owner decision D3). The method holds a reference to the table while it draws
    the casters (`LLPipeline::holdShadowCasterMasks`), and the draws bind it on their program's unit.
  - The layer's seed is the Aurora Viewer's (`sst_seed`): the draw's identity (`st_seed`: `LLDrawInfo::mStochasticSeed`,
    or the avatar's id for the avatar's alpha pass), the triangle's index in the draw (`gl_PrimitiveID`) and the side. Faces of
    different draws (identical meshes of different objects: rigged clothing, attachments, repeats), the triangles of one
    draw (coincident copies merged into one batch among them) and the two sides of a triangle are independent layers. The
    seed does not depend on the vertices' positions, so a swaying flexible prim or a moving object keeps its pattern. A
    draw that holds several faces (a batch) takes its first face's identity and counts its triangles from its start: when
    the batch is formed again (its group re-sorted once the direction from the camera to it has turned by about 37 degrees,
    or rebuilt) its pattern is drawn anew, a one-time change of grain with the same mean.
  - The blocks are anchored where the map's texels are: a sun cascade's wrapped world-grid index (`alphaDitherOrigin`, an
    even modulus, so the blocks keep their parity), a projector tile's corner in the atlas.
  - The pattern is fixed. The Aurora Viewer rotates it with the frame only under TAA or FSR, where every pixel of the view
    is averaged over the frames. Here the deferred sun pass keeps a history, but the forward receivers of the same
    cascades (`alphaF`, `pbralphaF`, `materialF`, `pbrmetallicroughnessF`, `waterF`) read each frame alone with fixed
    rays, so a pattern that changed every frame would flicker on water and on transparent surfaces under blended leaves or
    sheers. The receivers' filter (8 rays, and the deferred pass's history of rotating rays) averages the fixed grain over
    its footprint; where that footprint is one texel (a sheer's contact shadow seen from close by) the 2 x 2 grain stays
    visible, still, as the hashed dither's did before.
  - Lamps (projector tiles) use the same rule, and a static lamp tile that has a light-through layer (lamp shadows L3) takes
    its blend texels below full opacity out of the depth (`shadow_through` set on the blend draws): they go into the pool's
    light-through layer, which lets the lamp's light through each in proportion and coloured, `(1 - a)(1 + a C)` per channel
    (it goes to 0 as a reaches 1, so no cut is needed), stacked ones multiplying, and the lookup multiplies the depth's
    visibility by it. The sun's cascades, the lamps' avatar layer and rigged faces keep the caster rule; mask faces keep the
    cutoff. See `projector-shadow-pool.md` ("The light let through see-through casters"); `RenderShadowLightThrough` (a debug
    setting, on) turns it off for A/B.
  - The avatar's transparent parts cast in its alpha blend pass, with alpha = texture x mesh colour. Each mesh (skirt,
    eyelashes, hair) is its own draw under the avatar's id, its triangles counted from 0, so two of one avatar's meshes
    share a layer seed only where a triangle of one and the triangle with the same index of the other overlap in the light
    (there they are not independent: two equal opacities cast as one layer, not the product; rare, since it takes the same
    index and the same place).
    The stock alpha mask pass draws the same meshes into the same map again; under Stochastic it is left out
    (`LLDrawPoolAvatar::getNumShadowPasses`), and its program keeps the stock text. (In Legacy both passes run as in
    stock: the avatar shadow pass never sets `minimum_alpha`, so a texel from 0.05 up is solid.)
  - Checked by `indra/newview/tests/llstochastichistory_test.cpp` (a C++ twin with golden values computed from the Aurora
    Viewer's formulas by `scripts/tests/gen_stochastic_goldens.py`, and its properties: one layer lets 1 - alpha through
    within one stratum, 1/16384, over a tile period; products for two and three layers),
    `scripts/tests/test_shadow_alpha_permutation.py` (the GLSL lines the twin follows, the Legacy text, the plumbing) and
    `scripts/perf/test_shadow_caster_rule.py` (offscreen GL, no viewer: every caster program compiles and links, the GPU
    keeps the model's texels and the C++ test's golden texels, mask faces keep the creator's cutoff and the floor; `--bench`
    times the caster fragment).
    Tinted glass still casts a grey shadow: alpha is taken as transmittance.

## Dark gate (Stochastic only)

The sun cascades (cull, sort and draw of four maps, with every avatar in each) are the largest single shadow cost, and a sky
whose light is negligible gets nothing from them. Under Stochastic, `generateSunShadow` skips the cascade pass while the light
that reaches the ground is too dim for a shadow to show; Legacy never does (it keeps Firestorm 7.2.5's code).

- The decision light is `E = 3 x max(linear(primary light colour) channels) x sky sunlight scale` (`RenderSkySunlightScale`, or
  the HDR one): the factor `pbrBaseLight` gives a white surface facing the light in its brightest channel (the shader lights
  each channel on its own, so the largest channel is used, not the luminance). It depends on the sky and the light only,
  never on the camera, the view or the exposure. The primary light is the sun/moon handover's, so a lit moon counts. The cloud
  shadow map and the atmosphere's haze only lower the light. Classic-mode lighting multiplies `sunlit` by a further 1.35
  (`softenLightF.glsl`); that is not included.
- Skip below `E < 5e-6`; render again above `5.5e-6` (the hysteresis band, so a light at the threshold does not flicker the
  pass). The shadow strength (`primary_shadow_strength`) is multiplied by a fade that is 0 up to `5.5e-6`, a smoothstep to 1
  at `1e-5`, and 1 above: nothing pops, and above twice the threshold the shadow is unchanged. The strength also weights the
  cloud shadow map, so the fade moves the cloud shadows to the map's mean while `E < 1e-5`: invisible at that light level.
- Why `5e-6`: for a diffuse or rough surface a shadow changes a channel by at most `E` (albedo is at most 1; the BRDF's `1/pi`
  is left out). One 8-bit step near black is `1/(255 x 12.92) = 3.0e-4` in linear light. The brightest exposure is
  `RenderExposure`'s clamp (4) times the dynamic exposure's maximum (`2 x sqrt(gamma)`; the EEP validator allows gamma up to 20),
  a gain of 35.8 (16 for gamma 4), so a shadow of the threshold light is at most `1.8e-4`, 0.6 of a step, even then. With the classic-mode 1.35 factor (not in `E`) it is `0.6 x 1.35 = 0.81` of a step, still under one. A smooth
  specular peak can reach `10 x E` (the clamp in `pbrBaseLight`): a few highlight pixels of up to a few steps near black in that
  extreme, which the threshold accepts. With `RenderUseExposureSkySettings` (off by default) the exposure maximum comes from the
  sky and is not bounded by gamma.
- Exactly zero light: the skipped frame has strength 0, and the shadow term multiplies a zero light (`sunlit` is 0, and
  both sun terms of `pbrBaseLight` multiply it), so the image is the same with or without the cascades. This is an argument from
  the shader text, not a pixel comparison.
- Skipped frames have strength 0 exactly, so the maps are never read with weight. They are cleared once when the gate closes,
  and the frame that opens it renders them before the shading reads them (the decision is one per main frame, in
  `LLEnvironment::updatePrimaryLight`, read by both the pass and the uniform). The reflection probe renders call the same
  `generateSunShadow` with the same decision, so they skip with the main view and are lit the same way.
- A frame with strength 0 (skipped, or the resume band) also returns at the top of `sampleDirectionalShadow` in the Stochastic
  shading passes, so the per-pixel ray march is skipped too (the result would be `mix(1, x, 0) = 1`). The GPU saving is not
  measured yet. Legacy's `shadowUtil.glsl` is unchanged.
- Projector and point-light shadows and the avatar shadow layers are separate and unaffected.
- The renderer-free rule is `lldirectionalshadowgate.h`, tested by `indra/newview/tests/lldirectionalshadowgate_test.cpp`
  (ci-checks). Devel only: `lldirectionalshadowreadout.cpp` logs `directional shadows: skipped|rendered (light L=...)` when
  the state changes or `L` moves by a factor of 2, and the LEAP operation `DirectionalShadows.stats` returns the decision
  light, the threshold and the state.

## Cost

Legacy costs what the PCF with the receiver-plane bias costs. The offline sun-light-pass bench
(`scripts/perf/bench_shadow_legacy.py`) times the devel-tip shaders and this tree's Legacy shaders side by side
and checks that Legacy's light map is bit-identical to the devel tip; Stochastic's costs are measured by
`scripts/perf/bench_shadow_stochastic.py` against Legacy. PCSS's costs (+0.02 to +0.03 ms at High, +0.07 to
+0.13 ms at HighUltra over Legacy) are in the archived study.

The blended casters' rule (Stochastic, see Alpha casters) was measured offline on an RTX 5070 Ti (NVIDIA 621.14,
2026-11-18), not in-world and not on AMD, Intel or Mesa:

- Per blended caster fragment (`scripts/perf/test_shadow_caster_rule.py --bench REF`: 16 layers of alpha 0.5 into a
  2048 x 2048 depth-only map, 67.1 M fragments, five runs): 7.7 to 9.3 us per million fragments, against 3.9 to 4.8 for
  Legacy and for the hashed dither before the rule (both raster-bound), so +3.7 to +4.6 us per million. One blended layer
  over every texel of the four sun cascades at 2560 x 1440 (14.7 M texels) costs +0.05 to +0.07 ms of GPU time, about
  -0.2 fps at 60 fps and -0.04 fps at 25 fps; real scenes cover a fraction of that.
- Per blended draw, the seed (`st_seed`, a uniform that changes on every draw, so `LLGLSLShader`'s value cache never
  saves the call; `scripts/perf/bench_shadow_caster_seed.cpp`, tiny depth-only draws that each bind their own texture):
  +3.5 ns on the app thread, +30 to +44 ns of GPU time (the GPU's waits for the driver included) and +70 to +130 ns to
  the end of `glFinish`, the driver's own thread included. At the 4,399 blended caster draws of the Rooftops cascades
  (`sun-shadow-cost-study.md`) that is +0.015 ms on the main thread, +0.13 to +0.19 ms of GPU time (-0.5 to -0.7 fps at
  60 fps, -0.1 fps at 25 fps, where the frame is bound by the GPU) and +0.3 to +0.56 ms on the driver's thread, which
  runs beside the main thread under the NVIDIA driver's threaded optimisation. A draw parameter would carry the seed
  without a state change (`gl_BaseInstance`, GL 4.6); not done.
