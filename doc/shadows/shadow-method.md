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
| Later shadow stages (S2-S9, S7b) | No | Yes |
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
size differ in the C++ code. No shader define is needed (`SHADOW_LEGACY` does not exist).

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
