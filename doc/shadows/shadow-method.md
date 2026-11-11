<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Shadow method: Legacy and PCSS

`RenderShadowMethod` (U32, saved, default 0) selects how the sun and moon shadows are fitted and
filtered. Preferences > Graphics > Rendering 2 shows it as a "Legacy / PCSS" combo next to the
Shadows heading. It replaces the "Soft shadows (PCSS)" checkbox (`RenderShadowPCSS`).

| | Legacy (0) | PCSS (1) |
|---|---|---|
| Sun/moon cascades | Firestorm 7.2.5's: split distances from the visible point cloud, the camera-angle blend of the split exponent (x, y, z), each cascade fitted to the visible receivers, orthographic or perspective-warped | Stable (stage S1, `llshadowcascadefit.h`): splits from the shadow distance, each cascade the bounding sphere of its frustum slice, texel-snapped on a world-anchored grid, orthographic |
| Sun map size | Screen-sized (`BlurHappySize(width)` x `BlurHappySize(height)`) | Square, same texel count (`sqrt(width * height)`) |
| Filtering | PCF with the receiver-plane bias | PCSS: rotated Poisson kernel, blocker search and contact hardening |
| Sun N·L under shadow | Legacy `min(N·L, shadow)` | PCSS and Stochastic: N·L × shadow (`SHADOW_NL_MULTIPLY`) |
| Later shadow stages (S2-S9, S7b) | No | Yes (S2 below). The deferred sun pass uses the world-unit bias and the geometric receiver plane; forward receivers (alpha, PBR, water) still use the Legacy build until D11 |
| Settings used | `RenderShadowSplitExponent` x/y/z, `RenderShadowErrorCutoff`, `RenderShadowFOVCutoff` | `RenderShadowDistance`, `RenderShadowSplitExponent` y, `RenderShadowPCSSLightSize` |

## Shared by both methods

These are not gated by the method:

- the light model: L2 cloud shadows, L3 `primary_light_dir` / `primary_shadow_strength`, the
  caster direction that follows the sun/moon handover, skipping the cascades while the shadow is
  invisible;
- three Vulkanstorm fixes made to the stock shadows: the sun-bias altitude term is gone, stock PCF
  has a receiver-plane bias (`pcfDepthGradient`), and the projector shadow fades with the
  Euclidean camera distance and `spot_shadow_reach`, not with the sun cascade splits.

So Legacy is Firestorm 7.2.5's cascade fit and map sizes with the PCF shader as it is today
(the shader program is the one PCSS-off used before the switch); only the cascades and the map
size differ in the C++ code. The one shader difference is the sun's N·L under shadow: every method but Legacy defines `SHADOW_NL_MULTIPLY` (a global define set in `loadBasicShaders`, `llviewershadermgr.cpp`, where the method is read), which switches `sunShadowedDiffuse` in `deferredUtil.glsl` from Firestorm's `min(N·L, shadow)` to `N·L × clamp(shadow, 0, 1)`. Legacy never defines it and keeps `min()`. In classic-mode PBR (`pbrBaseLight`) the multiply form applies the shadow once, where `min()` applied it twice. The define changes the shader text, so the shader cache is rebuilt once after the update.

## Stage S2: the method-neutral groundwork

`shadowUtil.glsl` has a block that no method owns, `SHADOW_COMMON` (GLSL 1.50, no `textureGather`).
`SHADOW_MODERN` (the PCSS build, and the Stochastic build when it exists) implies it. The Legacy
stages that follow S2 can define it to call the same helpers; today Legacy defines neither, nothing in
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
  PCSS calls them, and so can any other method. Under PCSS `RenderShadowBias` and `RenderShadowOffset` no
  longer act on the deferred sun pass (they stay for Legacy); the forward receivers (alpha, PBR materials,
  water) still link the Legacy build and use them until decision D11, and the projector shadows keep their
  own bias.
- `shadowPlaneTap` (`SHADOW_GATHER`, GL 4.0 and later, under `SHADOW_MODERN`): one bilinear compare tap
  with the signed plane through `textureGather`; the PCSS no-blocker tap uses it. The plane only ever lowers
  the reference (one-sided), because past a crease the extrapolated plane is not the surface. GL 3.3
  keeps the hardware compare.
- The sun contact path keeps `foot`, half a shadow texel added to the plane term of the blocker search and the
  filter, so a tap anywhere inside its texel is covered.

What changes in the PCSS image with stage S2 alone: no per-pixel bump pattern in the receiver plane (a
normal-mapped wall no longer gets bias that follows its bumps), a bias and a normal offset that are
the same length in metres in every cascade (smaller than before in the near cascades, larger in the far
ones: no more 4 mm in cascade 0 to 70 cm in cascade 3), a search and a filter that stop at 25 cm
(visible only in cascade 1 under a large sun scale; it was 41 cm), and a wider caster guard at the cascade
edges (26 cm in the finest cascade instead of 14 cm). The filter itself (rotated 12 taps, light-map blur)
is unchanged.

Known limit: at a crease (a wall's foot on the ground) the plane of one surface does not continue into
the other. `test_shadow_bias_altitude.py` counts the 3 cm band above the ground apart from the rest of the
wall and bounds it (measured 2.3 to 4.3 % of that band's pixels, at most 5 %); the rest of the wall has no acne.

Cost, offline bench (`scripts/perf/bench_shadow_pcss.py`, sun light pass, median of 30 frames, 3 rounds,
same GPU, devel-tip PCSS against this tree's PCSS): +0.014 ms at 1080p with sun and spot shadows
(+0.013 ms sun only), +0.022 / +0.023 ms at 1440p; the GL 3.3 path (`pcss33`) is +0.011 / +0.010 ms at 1080p. In a view that is
74 % sky it is +0.003 ms or less at 1080p. The gate is +0.015 ms at 1080p. Stage S2 is the only cost added so
far, so the running total is the same +0.014 ms; its percentage of the `windows-latest` 82080 PCSS-on frame is
not known yet, because that baseline capture is still owed.

## Where the method is read

`LLPipeline::RenderShadowMethod` is a cached setting refreshed on change. A change calls
`handleSetShaderChanged` (shaders reloaded, render targets released and created again, shadow
buffers allocated again), the same path the old checkbox took. The gates are `use_pcss_shadows()`
(`llviewershadermgr.cpp`: the PCSS build of `shadowUtil.glsl` and the sun program's `PCSS_SHADOW`),
the sun-light uniforms and `pcss_shadows` in `pipeline.cpp`, `allocateShadowBuffer` (map size) and
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

- Feature table (`featuretable*.txt`, all three, no version bump): PCSS (1) at High, HighUltra and
  Ultra; Legacy (0) at Low, LowMid, Mid, MidHigh and Unknown; 0 in safe mode. The method follows
  the quality slider and the recommended-settings re-applies like every graphics setting, and the
  user can change it afterwards. The combo is disabled (the value is kept) when shadows or the
  feature are unavailable, and the PCSS light size slider (Sky tab) is enabled only with PCSS.
- Graphic presets carry `RenderShadowMethod` (`graphic_preset_controls.xml`). A preset file written
  before the switch has `RenderShadowPCSS` and no `RenderShadowMethod`: on loading it, the method
  becomes PCSS if the checkbox was on, Legacy if off (`LLPresetsManager::loadPreset`).
- One-time migration (`RenderShadowMethodMigrated`): at startup, after the user settings are read and
  before the command line and window creation, an existing install takes `RenderShadowPCSS` as the
  method; a fresh install gets the feature table's value when the window is created. The flag is set
  either way. `RenderShadowPCSS` stays declared, hidden and retired.
- The migration and preset rules are the renderer-free `llshadowmethod.h`, tested by
  `indra/newview/tests/llshadowmethod_test.cpp` (ci-checks).

## Cost

Measured on an RTX 5070 Ti (offline sun-light-pass bench, `scripts/perf/bench_shadow_pcss.py`, whose
`legacy` variant is an alias of the PCSS-off shader, identical by construction because the shaders Legacy compiles
are the PCSS-off ones): PCSS adds, over Legacy, +0.02 to +0.03 ms at High in the study (+0.02 to +0.03 ms in the
latest bench run) and +0.07 to +0.13 ms at HighUltra (1080p and 1440p). Legacy costs what the PCF with the
receiver-plane bias costs.
