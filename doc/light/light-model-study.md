<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Light like real light: sun, sky and clouds (study, before the shadow stages)

Status: study and design. Nothing here changes the viewer. The branch `codex/light-study` (from `vkstorm-devel` 10aecc0153) carries:

- this document;
- an offline GL bench, `scripts/light/bench_volumetric_clouds.py`. It holds a GLSL transcription of the owner's Aurora volumetric clouds, as fragment and compute shaders, and the ground cloud-shadow passes proposed here.

The owner's direction (2026-11-08):

> let's get SL light to behave like real world light first (this would include getting dimmed or blocked by clouds); then we get to work on shadows behave like shadows.

> at the center of both [PCSS and stochastic] is the idea that light needs to behave like light does in real world

> volumetric clouds (a PR of mine in Aurora), can we use it in vulkanstorm?

Code references are file:line at `origin/vkstorm-devel` 10aecc0153. Shader paths are relative to `indra/newview/app_settings/shaders/`.

Each claim is tagged:

- **[code]**: read in the code;
- **[measured]**: from the bench on this machine's RTX 5070 Ti (driver 621.01);
- **[inferred]**: reasoning or estimate, not yet checked.

The shadow study (#330, `doc/shadows/shadow-model-rework.md` on `codex/indoor-shadow-study`) is called "the shadow study" here, and its stages S1–S9.

---

## 1. Summary

**Today's light model** is the WindLight/EEP model with PBR shading on top. The sun's colour follows the EEP haze through a Beer–Lambert extinction. The sky light comes from reflection-probe irradiance. Exposure and tone mapping come after both. The differences from real light, ranked by how much they show on screen:

1. **Clouds never dim the sun on the ground [code].** `cloud_shadow` only brightens the ambient and dims the haze. The `sunlit` term leaves undimmed (atmosphericsFuncs.glsl:113-125). The sky meanwhile draws the clouds over the sun's disc (lldrawpoolwlsky.cpp:694-703). On an overcast sky the ground keeps full sun and hard shadows under a grey dome.
2. **An overcast sky does not change the sky light [code].** Clouds are kept out of the irradiance probes (lldrawpoolwlsky.cpp:701). In PBR the probe irradiance replaces the EEP ambient (reflectionProbeF.glsl:569-571). So EEP's own "more ambient under clouds" term (atmosphericsFuncs.glsl:113) is lost wherever probes cover the scene.
3. **The light in shadow is grey, not sky-blue [code].** The ambient is reduced to its luminance (atmosphericsFuncs.glsl:156-157). The sky colours are desaturated in every irradiance capture (llsettingsvo.cpp:695-713, `RenderDesaturateIrradiance` = 1). Real shade under a clear sky is distinctly bluer than sunlit ground.
4. **The sun-to-moon handover is a switch at the horizon [code].** The moon's light also ignores EEP `moon_brightness`: the shader uses the raw `moonlight_color` (atmosphericsFuncs.glsl:62); `moon_brightness` only scales the disc (moonF.glsl:53). A sky with no moon still moonlights the ground. When the moon is up, its light appears all at once at sunset.
5. **Exposure adapts by ±1 EV only, and ignores the EEP exposure settings by default [code]** (pipeline.cpp:7841-7870). This matters once clouds really dim the sun: an overcast scene goes darker than a camera or an eye would show it.
6. **Smaller [code]:**
   - The airmass is plane-parallel, so the sun turns black rather than red in the last few degrees (atmosphericsFuncs.glsl:75-76).
   - Sunlight is not attenuated with depth under water.
   - EEP's Rayleigh/Mie/absorption layers are stored but never used by the renderer.

**Clouds dimming light (§3).** Proposed: one **cloud transmittance map** (CTM), two producers and one consumer.

- **The map:** world-locked, 512² over 4 km, snapped like S1's cascades.
- **The producers:**
  - the flat EEP layer, exactly the `alpha1` that `cloudsF.glsl` draws;
  - the volumetric layer, marched along the sun.
- **The consumer:** one bilinear tap that multiplies `sunlit` in every shader that lights with the sun.
- **Probes** take the map's mean, so they never bake a dappled ground.
- **Direct and sky light are separate.** Clouds remove direct light. The skylight change under overcast is its own stage, a uniform veil in the irradiance captures.
- **Stability.** The map is a function of world position, sun direction and the EEP scroll, never of the camera. To couple the sky and the ground exactly ("the sun goes behind that cloud, and here it goes dark"), the flat dome gets a parallax offset from the camera's horizontal position: invisible while walking, a drift when flying (§3.3).

**Cost [measured], 5070 Ti, 1080p:**

| Item | Flat layer | Volumetric layer |
|---|---|---|
| Building the map | 0.016 ms | 0.023 ms |
| Per-pixel lookup | below the timer's resolution (< 0.001 ms) | below the timer's resolution |

It combines with the sun shadow map and PCSS by multiplication.

**Volumetric clouds (§4): yes, as a fragment-shader port at the GL 4.0 tier the viewer already uses.**

- **What the viewer requires:**
  - It runs from GL 3.x: the context is clamped to 3.0–4.6 (llwindowwin32.cpp:1951). Below 4.0 it runs a GL3 feature mask with no probes.
  - Stochastic transparency needs GL 4.0 and GLSL 4.00 (llviewershadermgr.cpp:290-296). The brief's belief is right for that feature, but 4.0 is the PBR tier, not the floor.
  - The shader manager has no compute stage at all.
- **What the trace needs.** Aurora's trace is per-pixel and independent, so it needs no compute and no image load/store: 3D textures, MRT and integer arithmetic are enough.
- **Fragment against compute, timed [measured].** The two ports differ by at most 18 % either way (compute is usually a little faster, fragment won one case), so there is no reason to raise the baseline to 4.3.
- **Total cost [measured]**, trace + reconstruct + composite, every pixel treated as sky:

  | Resolution | Cost |
  |---|---|
  | 1080p | 0.13–0.26 ms |
  | 1440p | 0.17–0.32 ms |

- **The one hitch.** Aurora's noise generation, 8 slices a frame, costs 7–13 ms a frame for 16 frames [measured]. It must move to a worker thread with a disk cache, or ship as data.

**Stage order (§5).**

1. L1, the reference numbers.
2. L2, flat-layer cloud shadows.
3. L3, the sun/moon handover and moonlight.
4. L4, overcast skylight.
5. L5, sky-coloured shade with white balance.
6. L6, exposure.
7. L7, the volumetric clouds, which become a second producer of the map.

Then the shadow stages S2–S9 continue. S3's light size and EEP cross-fade and S6's transmittance build on L2–L3.

**Decisions for the owner** are in §6. The main one is whether existing EEP skies should cast ground shadows at full strength ("what the sky draws, the ground gets"). Measured on a stand-in cloud texture, a `cloud_shadow` 0.38 sky would remove two thirds of the direct sun on average.

---

## 2. Today's light model, read from the code

### 2.1 The path of sunlight, step by step [code]

1. **EEP values.** `LLSettingsVOSky::applySpecial` sends the raw `sunlight_color` and `moonlight_color` (llsettingsvo.cpp:782-786), the light direction (clamped to z ≥ −0.1, llenvironment.cpp:1719-1727), `sun_up_factor` (863), the haze uniforms (`applyToUniforms`, 729-747) and the scales (817-818).
2. **Sun colour at the ground.** `calcAtmosphericVars` (class1/windlight/atmosphericsFuncs.glsl:51-131) computes:

   `sunlit = light_color × exp(−(blue_density + 0.25·haze_density) · density_multiplier · max_y / sin(elevation))`

   This is a Beer–Lambert extinction with a plane-parallel airmass (66, 75-76). `light_color` is the sun's or the moon's (62). It returns `sunlit` without any cloud term (124).
3. **Ambient.** `amblit = pow(ambient + (1 − ambient)·cloud_shadow·0.5, 0.9) × 0.57` (113, 125). `calcAtmosphericVarsLinear` then shapes it with `ambientLighting` (137-144, 152). For non-classic skies it reduces it to its luminance (156-157). Last, `sunlit` and `amblit` are scaled by `RenderSkySunlightScale`/`RenderHDRSkySunlightScale` and `RenderSkyAmbientScale` (163-164). All three are 1.0 in settings.xml (13356, 13367, 13378); the 1.5 in the code is only the fallback.
4. **Deferred composite** (class3/deferred/softenLightF.glsl):
   - The light is the sun or the moon by `sun_up_factor` (129). The shadow is `scol = max(lightMap.r, albedo.a)` (139).
   - **PBR:** the probe irradiance is sampled (179), then `pbrBaseLight` (188). There the sun term is `nl·(diffuse/π + spec)·sunlit·3.0·scol` (deferredUtil.glsl:422, 615), and the IBL term is `irradiance·albedo` (deferredUtil.glsl:387-393).
   - **Legacy materials:** `irradiance·albedo + min(nl, scol)·sunlit·albedo` (213-240).
   - **Classic mode:** legacy skies with no probe ambiance, while `RenderSkyAutoAdjustLegacy` = 0 (llsettingsvo.cpp:810, settings.xml:13279), mix in sRGB with `pow(nl, 1.2)` and fixed factors 0.9, 0.7, 1.35, 1.1 (softenLightF.glsl:159-160, 226-233, 280-281; deferredUtil.glsl:591-611). This mode exists to match the pre-PBR look. Every change below keeps it working, and its look is the oracle for legacy skies.
5. **Sky light (irradiance).**
   - The probes capture the sky and the scene. In an irradiance capture the ambient uniform is zeroed (llsettingsvo.cpp:825-828), `blue_horizon` and `blue_density` are desaturated (695-713), and the clouds are not drawn (lldrawpoolwlsky.cpp:701).
   - At sampling, each probe's irradiance is scaled by `max(sky probe ambiance, probe ambiance)` (llreflectionmapmanager.cpp:1142-1148, 1220-1221). It replaces `amblit` entirely once that scale reaches 1 (reflectionProbeF.glsl:569-571).
6. **Exposure.**
   - A luminance sample of the middle 60 % of the screen, nudged down "to favour ground over sky" (class1/deferred/luminanceF.glsl:46-57), drives `exposureF.glsl`, eased over time (47-62).
   - The range is `[1/s, s]` with `s = 2·sqrt(gamma)`, so ±1 EV for gamma 1. It applies only when the sky has a probe ambiance (pipeline.cpp:7858-7870).
   - EEP's own HDR offset, min and max are used only with `RenderUseExposureSkySettings`, which defaults to 0 (pipeline.cpp:7843-7856; settings.xml:13401).
7. **Tone mapping.** ACES (Hill fit) by default (`RenderTonemapType` = 1, settings.xml:14103; tonemapUtilF.glsl:77-151), with exposure `RenderExposure` clamped to [0.5, 4] (pipeline.cpp:7937-7941). Before it, the HDR values are clamped to 11.2 (deferredUtil.glsl:83-93).
8. **Clouds in the sky.**
   - The flat layer is a dome drawn around the camera: it is translated to the camera every frame, so it has no parallax (lldrawpoolwlsky.cpp:289-293).
   - It uses planar UVs from the view direction (llvowlsky.cpp:431; cloudsV.glsl:83-87), scrolled by `cloud_pos_density1` plus the client-side scroll (llsettingsvo.cpp:768-777; llenvironment.cpp:1811-1831).
   - Its opacity is `alpha1` (cloudsF.glsl:91-99). It is drawn after the sun and moon (lldrawpoolwlsky.cpp:694-703), so it covers the disc.

### 2.2 Where it differs from real light, ranked

Rank is by how often and how strongly it shows on screen. "Real" here means the physical behaviour, with EEP's values kept as the creator's description of the atmosphere.

| # | Difference | Evidence | How it shows | Rank |
|---|---|---|---|---|
| D1 | **Clouds do not dim direct sun or moon light on the ground.** EEP's `cloud_shadow` only lifts the ambient and dims the haze (`cs` in the haze only). `mSunDiffuse` is also taken before the cloud term in C++ | atmosphericsFuncs.glsl:113, 119-125; cloudsV.glsl:157-166; llsettingssky.cpp:1738 vs 1743 | Any sky with clouds. Most visible overcast: a grey sky, a hidden sun disc, full sun and crisp shadows below. The owner's "dimmed or blocked by clouds" | **HIGH** |
| D2 | **Overcast does not change the skylight in PBR.** No clouds in irradiance captures; the probe replaces `amblit`; EEP's cloud ambient boost is lost under probes | lldrawpoolwlsky.cpp:701; reflectionProbeF.glsl:569-571; llsettingsvo.cpp:825-828 | Once D1 is fixed, overcast scenes would be lit by a clear-sky irradiance with no sun: too dark and too contrasty. D1 and D2 must be designed together | **HIGH** (once D1 is fixed) |
| D3 | **Shade is grey, not sky-coloured.** Non-classic `amblit` is reduced to luminance; irradiance captures desaturate the sky colours | atmosphericsFuncs.glsl:156-157; llsettingsvo.cpp:695-713; settings.xml:11433 | Every outdoor shadow under a clear sky. Real shade is bluish against neutral sunlit ground; here both are neutral, so shadows read as "darker", not "in shade". Linden made this choice "to avoid tinting the world blue" (comment at llsettingsvo.cpp:826) | MEDIUM-HIGH |
| D4 | **Sun to moon is a hard switch at elevation 0, and the moonlight ignores `moon_brightness`.** Light direction, colour and shadow switch on `getIsSunUp()`; the shader's moonlight is the raw `moonlight_color`; C++ `mMoonDiffuse` multiplies by `moon_brightness`, but no shader uses it | llsettingssky.cpp:1324-1334, 1353-1366, 1749-1756; llsettingsvo.cpp:786, 863; softenLightF.glsl:129; atmosphericsFuncs.glsl:62; moonF.glsl:53 | The sun's light fades continuously to 0 at the horizon (the airmass term does it). A moon above the horizon then lights the ground at once, and shadows jump to the moon. A "no moon" sky (`moon_brightness` 0) still has full moonlight on the ground | MEDIUM |
| D5 | **Exposure range ±1 EV; EEP HDR settings ignored by default; centre metering** | pipeline.cpp:7841-7870; settings.xml:13401; luminanceF.glsl:46-47 | Indoors look darker than an eye would see them. After D1, an overcast scene loses up to ~1–2 EV of direct light [inferred], and the exposure can recover only 1 EV | MEDIUM (after D1) |
| D6 | **Direct-to-sky ratio set by fixed factors** (`3.0` and `/π` for the sun, the probe ambiance for the sky) and not checked against real ratios | deferredUtil.glsl:422, 615; llreflectionmapmanager.cpp:1221 | Real clear-day direct : diffuse on a horizontal surface is about 3–8 : 1 with elevation and haze [inferred, textbook range]. The viewer's ratio is unmeasured; if it is low, shadows look filled. Measure first (L1) | MEDIUM, to measure |
| D7 | **Plane-parallel airmass.** `1/sin(elevation)` diverges at the horizon, so the sun goes black, not deep red, in the last ~3°. The light direction is also clamped to z ≥ −0.1 | atmosphericsFuncs.glsl:75-76; cloudsV.glsl:130, 170; llenvironment.cpp:1722 | Only near sunrise and sunset | LOW |
| D8 | **No attenuation of sunlight under water.** Only the view path is fogged | class3/deferred/waterHazeF.glsl; softenLightF.glsl (no water term) | Underwater scenes are lit like dry land. The shadow study decided "no shadows under water" (2026-11-03) | LOW (later) |
| D9 | **EEP's Rayleigh/Mie/absorption layers are unused.** Only moisture, droplet and ice reach the renderer (rainbow and halo); the sky colour is the legacy haze model | llsettingsvo.cpp:743-745; lldrawpoolwlsky.cpp:384; no renderer caller of `getRayleighConfig` | The sky's colour is creator-authored through the legacy fields. A physical sky would change every existing sky. **Not proposed** | – |

**How much each matters, in one line:**

- D1+D2 are the owner's request and the largest visual change.
- D3 is the largest everyday difference on clear days.
- D4 is a twice-a-day pop.
- D5 and D6 decide whether D1–D3 look right or look broken.
- D7–D9 are small or out of scope.

---

## 3. Clouds dimming light

### 3.1 The model

Sunlight reaching a point splits into three parts:

- **direct**: `sunlit × visibility`, where visibility = shadow map (PCSS, later transmittance S6) × **cloud transmittance T(p)**;
- **sky light**: irradiance from the sky dome, including what the clouds scatter down;
- **local light**: unchanged.

Clouds act on the first two differently:

| | Real behaviour | Proposal |
|---|---|---|
| Direct | A cumulus core passes ≈ 0 of the direct beam. A thin edge passes some. Its shadow on the ground moves with the cloud, with a penumbra of (cloud height × sun angle), ≈ 14 m at 1.5 km for the real sun | `T(p)` from the cloud layer, along the sun (or moon) direction, multiplied into `sunlit`. The flat layer's T is exactly what the sky draws: `1 − alpha1`, the same fraction of the sun disc the sky shows |
| Sky light | Broken cloud: about the same, slightly brighter (cloud sides scatter). Overcast: no direct, a grey sky brightest at the zenith, horizontal illuminance about 10–30 % of a clear day [inferred, textbook] | L4: the clouds enter the irradiance captures as a **uniform veil of the layer's mean cover**, lit as the sky lights the flat layer (cloudsV.glsl's colours). No texture, so nothing pops |

This keeps EEP authoritative:

- No new EEP field.
- The shadows follow `cloud_shadow`, the density fields, `cloud_scale`, the scroll and the cloud texture, as drawn.
- A sky that draws no clouds (`cloud_scale` < 0.001, cloudsF.glsl:71) casts none.

### 3.2 The cloud transmittance map (CTM)

**One renderer-free definition, shared by the flat and the volumetric layer, and by OpenGL and the later Vulkan renderer.**

- **Format.**
  - A 512² R8 texture covering 4 km × 4 km of the world at 8 m a texel.
  - It is centred on the camera, snapped to 512 m steps.
  - Texel *(i, j)* holds T for world point *x(i, j)*.
  - Because generation is a pure function of world position, a re-centred map holds the same values at the same world points: no pop, no camera dependence. This is S1's snapping applied to clouds.
  - Optional: a second level over 32 km at 64 m (+0.016 ms) for far terrain seen from height. Beyond it, T is the map's mean.
- **Indexing by ground position.**
  - The cloud layer is treated as lying a height **H above whatever receives the shadow**, as Aurora's layer lies above the camera. The point on the layer along the light from *p* is then `p.xy + H · L.xy / L.z`, the same offset for every receiver height.
  - Skyboxes get the same cloud shadows as the ground, which is what their sky shows them.
  - A layer at a fixed altitude would make the shadows slide when the camera flies up, if the layer followed the camera, or cut off skyboxes; this rule does neither.
- **Lookup.**
  - In `calcAtmosphericVars`, one `textureLod` at `uv = cloud_shadow_mat · pos_eye` multiplies `sunlit`. `cloud_shadow_mat` is a per-view mat4, uploaded beside `shadow_matrix` in `bindDeferredShader` (pipeline.cpp:9864) and in the forward alpha/water binds.
  - Every shader that lights with the sun calls this function: softenLight, alpha, pbralpha, water, materialF, fullbrightShiny. Water glints therefore vanish under cloud too, as they do in reality.
  - With the moon as the light, the same map is used, built for the moon's direction.
- **Probes and cube snapshots** (`cube_snapshot` / `gCubeSnapshot`) take the map's **mean**: the top mip, or a CPU reduction. A probe refreshed once in a while must not bake a passing shadow.
- **Penumbra.** The LOD of the tap gives width ≈ `H_pen · 0.0093 · sun_scale` (0.53° × sun_scale), with H_pen = 1.5 km by default: 14 m at sun_scale 1. Today's cloud texture is coarser than that on the ground (12.6 km / 512 px ≈ 25 m a texel at the Aurora tile size), so the edge softness mostly comes from the cloud itself.
- **Producers.**
  - **Flat layer (L2).** A 512² pass evaluating cloudsF.glsl's `alpha1` taps (disturbance, both noise textures, the next-texture blend, `cloud_pos_density1/2`, `cloud_variance`) at the uv of §3.3, then `T = 1 − alpha1` (`altitude_blend_factor` is 1 overhead). Every frame: 0.016 ms [measured].
  - **Volumetric (L7).** 16 steps of Aurora's cheap density along the light through the layer: `T = exp(−σ ∫ density)`. Every frame: 0.022–0.026 ms [measured]. The sky and the ground then show the same clouds.
- **Combination.** `visibility = shadow(p) × T(p)`, with T after PCSS and the blur. Clouds and buildings are independent occluders of one source, so the product is right, apart from correlation inside a partly covered disc, which is negligible. S6's transmittance multiplies the same way. The shadow-map passes are untouched: no extra caster or cascade cost.

### 3.3 The flat layer: making the sky and the ground agree

The dome's UV depends only on the view direction, because the dome follows the camera (lldrawpoolwlsky.cpp:289-293).

**What is ruled out.** A ground shadow that matches "the cloud now in front of the sun" for the camera, but is computed from the camera's position, would slide along as the avatar walks. That is exactly the camera dependence S1 removed.

**What works: a parallax offset on the dome.** Add to the dome's cloud UV the camera's horizontal position divided by `2 · H · cloud_scale`, with the signs of the dome's axis permutation (llvowlsky.cpp:431, a 120° rotation about (1,1,1) at lldrawpoolwlsky.cpp:299) and the x flip (cloudsV.glsl:83). Then:

- **ground:** `uv_ground(p) = uv_dome(L) + (p.x, −p.y) / (2 · H · cloud_scale)`, a function of world position only. At *p* = camera it equals the sky's UV in the sun's direction: exact coupling, and stable by construction.
- **sky:** the dome clouds now drift past when the camera moves sideways.
  - With H = 15 km (the dome radius, and the scale Aurora's weather tile implies: 12.6 km at `cloud_scale` 0.42), walking 100 m moves the sky by 0.008 of a texture repeat, which is invisible. Flying 1 km moves it 0.08, a visible, real drift.
  - Vertical moves change nothing.
- **ground speed:** the default scroll (0.2, 0.01)/100 per second becomes ≈ 25 m/s of shadow motion, a plausible cloud speed. The direction matches the sky because it is the same offset.

The signs and scale must be pinned by a unit test of a renderer-free C++ function. The test asserts that the ground lookup at the camera equals the dome lookup in the light's direction, for random camera positions, sun directions and scales, using the same formula as llvowlsky.cpp:431 and cloudsV.glsl:83-87.

**The alternative:** world-anchored shadows with no dome parallax. Same statistics, scroll and stability, but the cloud you see on the sun is not the one shading you. That is the fallback if the owner rejects any change to how the flat sky looks (decision 2).

### 3.4 How strong: measured on a stand-in

The SL default cloud texture is a grid asset and is not available offline. The bench uses a tileable value-noise stand-in, as Aurora's demo does. The histogram of the real texture differs, so these numbers show the trend only.

Mean direct T over the map [measured, stand-in texture]:

| `cloud_shadow` | 0.27 (default) | 0.38 | 0.45 | 0.62 |
|---|---|---|---|---|
| Flat layer, T = 1 − alpha1 | 0.85 | 0.34 | 0.11 | 0.00 |
| Volumetric (Aurora mapping) | 0.96 | 0.35 | 0.02 | 0.01 |

Under "what the sky draws, the ground gets":

- A default sky keeps most of its sun.
- A 0.38 sky loses two thirds of its direct light on average, in moving patches.
- A 0.45+ sky is effectively overcast.

That is physically right, but it changes how every cloudy EEP sky made so far looks. Many creators used `cloud_shadow` 0.3–0.5 for "nice clouds, sunny day" because the ground never darkened. This is the main owner decision (§6, 1). A debug setting `RenderCloudShadowStrength` (1 = physical) allows tuning on the branch without a Preferences entry.

### 3.5 Stability, cost and gates

- **Stability.**
  - T depends on world position, light direction, EEP fields and the scroll clock. The scroll clock is client-local, so two viewers see different patterns; the sky already does this (llenvironment.cpp:1811-1831).
  - It does not depend on camera position, orientation or the visible set.
  - The map is snapped and the texels are deterministic.
  - Day-cycle changes are continuous: `blend_factor` between textures and interpolated fields.
  - No crawl. The pattern moves only with the scroll, smoothly, sub-texel, bilinear.
- **Cost [measured, 5070 Ti]:**

  | Item | 1080p | 1440p |
  |---|---|---|
  | Map build, flat | 0.016 ms | – |
  | Map build, volumetric | 0.023 ms | – |
  | Lookup tap, against an identical pass without it | < 0.001 ms | < 0.001 ms |

  Mid-class [inferred ×3]: ≤ 0.07 ms in total.
- **Gate for L2:**
  - ≤ 0.05 ms GPU at 1080p, map + all lookups, Frame Profile on the reference scenes (shadow study §2.5);
  - 0 when no clouds are drawn;
  - no CPU cost beyond uniform uploads;
  - the unit test of §3.3;
  - a test that the map value at a world point does not change when the camera moves and the map re-centres.

### 3.6 Light size under thin cloud (shadow decision 6, revisit?)

Under thin cloud, real shadows soften: the effective source becomes the bright aureole around the sun, several degrees wide. The 2026-11-05 decision was "no cloud-cover penumbra; light size from sun_scale and moon_scale only".

With the CTM, S3 could set a per-pixel light size, `L(T) = L_sun · (1 + k · (1 − T))` for 0.1 < T < 0.95, and fade the shadow altogether as T → 0, where only skylight remains. It costs nothing: the tap is already taken.

**Recommendation:** revisit decision 6 in S3, as an owner choice (§6, 7). Without it, the shadow of a building under a thin veil stays as crisp as in full sun, only fainter.

---

## 4. Porting the volumetric clouds

### 4.1 What the viewer requires today [code]

- **The context is clamped to 3.0–4.6 and asks for the highest** (llwindowwin32.cpp:1951-1960; `RenderMaxOpenGLVersion` 4.6, settings.xml:11571).
- **Below 4.0 the GL3 mask applies.** It turns off reflection probes, mirrors, FSAA, SMAA and background texture threads (llfeaturemanager.cpp:776-786; featuretable.txt:452-461). Pre-Haswell Intel is forced to 3.3 (llfeaturemanager.cpp:722-745).
- **4.0 is the PBR tier:**
  - probes need > 3.99 (llviewershadermgr.cpp:962);
  - stochastic transparency needs GL ≥ 4.0 and GLSL ≥ 4.00, on Windows and Linux (llviewershadermgr.cpp:290-296).

  **So the brief is right that stochastic needs 4.0, but the viewer's floor is 3.x.**
- **No compute anywhere.**
  - `glDispatchCompute` is only loaded (llgl.cpp:2135-2143).
  - The shader manager compiles vertex, fragment and geometry stages only (llshadermgr.cpp, no `GL_COMPUTE_SHADER`).
  - No shader uses image load/store.
- **Vulkanstorm ships Windows and Linux** (release tags), where 4.3+ drivers are the norm. macOS GL stops at 4.1 and would not matter for compute.

### 4.2 What Aurora's trace needs [code, Aurora `feat/volumetric-clouds` 7d480ee1]

| Pass | Aurora | Needs | OpenGL 4.0 fragment port |
|---|---|---|---|
| Noise | compute, 128³ RGBA8 base (Perlin-Worley), 32³ detail, 128² curl; 8 slices a frame for 16 frames | writes 3D storage | fragment pass per layer via `glFramebufferTextureLayer` (GL 3.2), **or a CPU worker thread** (see the hitch below) |
| Weather | fragment, 512² RGBA8, when the sky's cloud fields or texture change | – | same |
| Trace | compute, W/4 × H/4 invocations (a quarter of the half-res pixels), 64 steps, 5+1 light taps, 3 sky taps, bisection at cloud entry | `textureStore` rgba16f | fragment into a W/4 × H/4 MRT (RGBA16F + R16F), `texelFetch` for loads |
| Reconstruction | compute, half res, 3×3 clamp, reprojection for rotation and horizontal move | `textureStore` | fragment, ping-pong RGBA16F |
| Composite | fragment, full screen, `dst·T + L` | blending | same |

**Nothing needs compute.** Every invocation is independent, and the WGSL → GLSL translation is mechanical:

- `textureSampleLevel` → `textureLod`, `textureLoad` → `texelFetch`, `select` → `?:`, `saturate` → `clamp`, `bitcast<u32>(i32)` → `uint(int)`;
- **watch:** GLSL's `%` of a negative int is undefined, so `wrap3` needs `(c + 8p) % p`;
- reverse-Z sky test → forward-Z / G-buffer flag;
- y-up/z-up as Aurora already does.

The bench holds exactly this port.

### 4.3 Measured cost [measured]

`scripts/light/bench_volumetric_clouds.py`, RTX 5070 Ti, GL 4.6 compatibility context, medians of 20 frames after 5 warm-up. Setup:

- every pixel treated as sky, the worst case; the viewer traces only sky pixels;
- "horizon": the camera 12° up, 60° vertical fov, the lower screen below the horizon;
- "zenith": the camera 60° up, every ray through the layer;
- the "mean T" column (the trace's mean transmittance) shows that real cloud is being marched.

**1080p** (trace 480×270 a frame):

| Cover (`cloud_shadow`) | View | Trace, fragment | Trace, compute | Recon | Composite | **Total, fragment** | Mean T |
|---|---|---|---|---|---|---|---|
| fair 0.27 | horizon | 0.129 | 0.126 | 0.011 | 0.021 | **0.161** | 0.97 |
| fair 0.27 | zenith | 0.099 | 0.086 | 0.011 | 0.021 | **0.131** | 0.99 |
| broken 0.38 | horizon | 0.194 | 0.193 | 0.011 | 0.022 | **0.228** | 0.80 |
| broken 0.38 | zenith | 0.115 | 0.102 | 0.011 | 0.023 | **0.149** | 0.94 |
| stratocumulus 0.45 | horizon | 0.221 | 0.181 | 0.012 | 0.023 | **0.256** | 0.37 |
| stratocumulus 0.45 | zenith | 0.193 | 0.169 | 0.011 | 0.022 | **0.226** | 0.09 |
| overcast 0.62 | horizon | 0.189 | 0.179 | 0.012 | 0.022 | **0.222** | 0.33 |
| overcast 0.62 | zenith | 0.173 | 0.147 | 0.011 | 0.022 | **0.206** | 0.01 |

**1440p** (trace 640×360): totals 0.167–0.318 ms; the worst is broken/horizon 0.318 and stratocumulus/zenith 0.314. The trace is 0.12–0.27 ms.

**Ground transmittance map from the volumetric layer**, 512², 16 steps: 0.022–0.026 ms.

**Noise generation:**

- Fragment, once: base 128³ 110–206 ms (two runs), detail 32³ 14–23 ms.
- At Aurora's pace (8 slices a frame) that is **7–13 ms a frame for 16 frames**: a visible hitch on this GPU, worse on a mid-range one.
- Fix:
  - generate on a worker thread in C++ (deterministic, the same hash), cache 8.4 MB in the cache folder, upload once; **or**
  - ship the noise as an application asset.
  - Owner choice (§6, 9). One layer a frame (0.9–1.6 ms × 128 frames) is not good enough on mid-class GPUs.

**Verdict.**

- **Fragment against compute.** Compute was up to 18 % faster (stratocumulus/horizon at 1080p, 0.181 against 0.221 ms) and the fragment trace was faster in one case (overcast/horizon at 1440p, 0.220 against 0.233 ms); most cases are within a few per cent. That gap, at most 0.04 ms, does not justify a GL 4.3 baseline or a compute path in `LLShaderMgr` for OpenGL.
- **Mid-class estimate** [inferred, ×3 for ALU/fetch-bound passes, as in the shadow study]: 0.4–0.8 ms at 1080p, 0.5–1.0 ms at 1440p, all-sky worst case; less in-world, where buildings and terrain cover part of the sky.

### 4.4 Options, and the recommendation

| Option | For | Against |
|---|---|---|
| A. Raise the baseline to GL 4.3 and port as compute | identical structure to Aurora | drops GL 4.0–4.2 PBR users for no measured gain; adds a compute stage to `LLShaderMgr` |
| **B. Fragment port, enabled at the GL ≥ 4.0 PBR tier (recommended)** | no baseline change; one hardware tier for probes, stochastic and clouds; measured within 18 % of the compute port | up to 0.04 ms slower than compute on the worst preset |
| C. Vulkan line only | no OpenGL work | the shipping viewer gets no volumetric clouds for months; the Vulkan line is after the Rust port |

**Fallback.** Below GL 4.0 (GL3 mask), and wherever the volumetric layer is off, the flat layer is drawn exactly as today, and the flat-layer CTM (§3) still gives ground shadows: it needs only a 2D texture and a fragment pass, so it works on GL3 too. Aurora phase 1 also draws the flat layer in reflections, water, mirrors, probes and photos; keep that.

**Gate for L7 (volumetric, main view):**

- ≤ 0.30 ms at 1080p and ≤ 0.35 ms at 1440p on the 5070 Ti, worst preset, in-world Frame Profile;
- no frame > 1 ms over the median when the layer turns on (the noise comes from the worker or the asset);
- volumetric CTM ≤ 0.03 ms;
- off by default, as a debug setting `RenderVolumetricClouds`, until the owner asks for a Preferences entry (§6, 8).

### 4.5 How it maps onto the C++ sky and EEP code [code + design]

| Aurora | Vulkanstorm (OpenGL) |
|---|---|
| `Params::new` (clouds.rs): coverage ladder, `cloud_type`, `layer_km`, `tile_km`, `extinction`, `weather_key` | A renderer-free C++ unit, e.g. `llcloudlayer.{h,cpp}` in `llinventory` or `newview`, beside `LLSettingsSky`. Its inputs are `getCloudShadow()`, `getCloudScale()`, `getCloudPosDensity1/2()`, `getCloudVariance()`, the cloud textures and blend factor, `LLEnvironment::getCloudScrollDelta()`. Its unit tests port clouds.rs's tests (`the_cover_follows_the_weather_map`, `the_layers_follow_the_real_heights`, …) |
| `Lighting::new` (the Legacy cloud colour terms) | the same unit, from `cloudsV.glsl:117-187`; equivalence-tested against the GLSL (as `test_graphics_identity.py` does for other shaders) |
| `camera_km`, `offset_km` (wrapped in 105 km) | the camera's **global** position (`gAgent.getPosGlobalFromAgent`, double), so the pattern and the CTM are continuous across region crossings |
| weather map `fs_cloud_weather` | a fragment pass with the EEP cloud texture (`gSky.mVOSkyp->getCloudNoiseTex()` / `Next`, `blend_factor`), redrawn on a key change |
| noise `gen_*` | worker thread + cache (or asset), uploaded as `GL_TEXTURE_3D` RGBA8 |
| trace / recon | new `LLGLSLShader` programs (`deferred/cloudsVolTraceF.glsl`, `cloudsVolReconF.glsl`) in `LLViewerShaderMgr`, run after the G-buffer pass. The sky test is the G-buffer's `GBUFFER_FLAG_SKIP_ATMOS` on depth-cleared pixels. Today's clouds write depth (`LLGLSPipelineBlendSkyBox(true, true)`, lldrawpoolwlsky.cpp:507; llgl.cpp:3003-3015), so verify in the port that the dome leaves the depth at the far value |
| composite after the sky (`dst·T + L`) | in `LLDrawPoolWLSky::renderDeferred`, in place of `renderSkyCloudsDeferred` in the main view only (lldrawpoolwlsky.cpp:701-704), or a full-screen blend after `softenLight` on the linear HDR target. The flat clouds go into the G-buffer as sRGB and are scaled by `sky_hdr_scale` in softenLightF.glsl:195-205; the volumetric output is linear radiance, so composite after softenLight with the same `sky_hdr_scale` |
| `AURORA_CLOUDS_DEBUG` views | development diagnostics on the feature branch only; never on release (`check_release_hooks.py`) |

### 4.6 What is owner-only, and what must be checked against the C++ viewer

**Owner's own design** (new behaviour; the owner is the reference, no C++ oracle):

- the cloud shapes: noise, height profiles, the cumulus/stratocumulus/stratus ladder, the WMO layer heights, the heap knee;
- the Mie phase (Jendersie & d'Eon fit), multiple-scattering octaves, powder;
- the reconstruction, the 105 km period, the aerial-perspective fade;
- the gains `SUN_GAIN` 4.5 and `AMB_GAIN` 0.9, "fitted on the demo skies side by side" with Aurora's own port of the flat layer;
- the choice to draw the flat layer in reflections and photos.

**Must match the C++ viewer** (equivalence tests, C++ against GLSL, as for every OpenGL-referenced port):

- **Inputs.**
  - The weather coverage must follow cloudsF.glsl's `alpha1`: the same taps and the same `2·(cloud_shadow − 0.25)` shift.
  - Aurora omits `cloud_variance` (a TASKS "later" item). The port must add it, or state that it is ignored.
- **Lighting terms** from cloudsV.glsl, which Aurora's `Lighting::new` mirrors, including the night case (`sun_moon_glow_factor < 1` removes the glow) and the moon.
- **Scroll.** The direction and speed, including the x flip (llsettingsvo.cpp:775; cloudsV.glsl:83), and `blend_factor` set to 0 when the scroll rate is zero (lldrawpoolwlsky.cpp:519-522).
- **Tile size against `cloud_scale`** (Aurora: 12.6 km at 0.42), and **"no clouds" when `cloud_scale` < 0.001**.
- **Calibration of the gains.** Re-fit against the C++ viewer's flat layer, not Aurora's port of it; Aurora is unqualified, and the C++ viewer is the oracle.

---

## 5. Staged plan

Light first, then the shadow stages it unblocks. Each stage:

- is a `codex/` feature PR from `vkstorm-devel`;
- is built RelWithDebInfo, staged, with no installer;
- gets integration PRs per line after qualification;
- keeps renderer-free logic in units with tests;
- adds no Preferences entries unless the owner asks;
- keeps diagnostics off release.

Gates use the shadow study's in-world protocol (§2.5 there): Frame Profile, reference scenes, 1080p and 1440p, 3 runs.

| Stage | Scope | Performance gate | Owner checks in-world | Effort |
|---|---|---|---|---|
| **L1** Reference numbers | Measure today's sun : shade ratio and the colours on a white and a grey PBR panel, half in shadow, under 3 EEP skies (default, a clear PBR sky, an overcast creator sky), with `gSnapshotNoPost`. Record D6. Add renderer-free C++ for `sunlit`/`amblit` (atmosphericsFuncs.glsl) with an equivalence test against the GLSL | none (measurement) | nothing; reads the table | 1–2 days |
| **L2** Flat-layer cloud shadows | CTM (§3.2), flat producer, dome parallax (§3.3, if decision 2 = yes), lookup in every sun-lit shader, probes use the mean, `RenderCloudShadowStrength` debug setting; moon uses the same map | ≤ 0.05 ms at 1080p (map + lookups), 0 with no clouds; no CPU cost; tests of §3.5 | Under a broken sky the sun goes behind a cloud and the ground darkens; shadows drift with the clouds the way the sky scrolls; walking and turning change nothing; overcast presets look overcast | 3–4 days |
| **L3** Sun/moon handover and moonlight | Cross-fade the light (direction, colour, shadow strength) through a window around sunset, as the shadow study §8.2 designed, shared with S3. Moonlight × `moon_brightness` (decision 5). Spherical airmass (Kasten–Young) for the last few degrees (D7) | 0 GPU; no pop in a luminance curve over a sped-up day cycle | A sunset with the moon up: no jump in light or shadow; a "no moon" sky is dark | 1–2 days |
| **L4** Overcast skylight | The cloud layer's mean cover as a uniform veil in irradiance captures, lit with cloudsV.glsl's colours (decision 3); EEP's cloud ambient term respected in the PBR path | probe update GPU time ±5 % of today; no pop over a day cycle | An overcast sky lights the ground softly and evenly, not darkly; broken clouds look as before | 2–3 days |
| **L5** Sky-coloured shade, white balance | Keep the sky's colour in irradiance (`RenderDesaturateIrradiance` off, no luminance `amblit`) and add a von Kries white balance to the frame's mean illuminant (sun + sky) in the tonemap pass, so the world is not blue but shade is bluish (decision 4). Classic mode unchanged | ≤ 0.01 ms | Clear day: shade slightly blue, sunlit neutral, no blue cast overall; sunset: warm light, cool shade | 2–3 days |
| **L6** Exposure for real light | Range from EEP HDR settings when the sky provides them; a wider default (e.g. ±2 EV) only if the owner agrees (decision 6); centre-weighted metering kept | ≤ 0.01 ms; no oscillation (exposure curve over a walk indoors and out) | Going indoors and under overcast looks adapted, not dark; noon is not washed out | 1–2 days |
| **L7a** Volumetric clouds, main view | Fragment port (§4.4 B), noise by worker or asset, weather, trace, recon, composite; flat layer elsewhere; debug setting, off | §4.4 gate (≤ 0.30 ms 1080p / 0.35 ms 1440p worst; no hitch) | The 4 presets against Aurora's demo, side by side with the flat layer; no shimmer when turning; region crossing seamless | 6–8 days |
| **L7b** Volumetric ground shadows | Volumetric CTM producer (§3.2), consistent with the sky | ≤ 0.03 ms | Under the volumetric sky the shadows match the clouds seen | 1–2 days |
| then **S2** | Bias in world units (unchanged from the shadow study) | as there | as there | 1–2 days |
| **S3** | PCSS only, EEP cross-fade **shared with L3**, moon_scale; **light size from T if decision 7 = yes** | as there; T costs nothing extra | Shadows under thin cloud soften and fade; under clear sky as before | 3–4 days |
| **S4–S9** | Denoise, caching, **transmittance × T** (S6), projector atlas, tiers (cloud shadow and volumetric tiers join S8), contact shadows | as there | as there | as there |

**What the light stages unblock:**

- **S3** needs L3's handover and L2's T for the light size.
- **S6** composes with T.
- **S8's tiers** include the cloud stages.
- How dark a shadow is depends on the direct-to-sky ratio (L1), the overcast skylight (L4) and the shade colour (L5). Tuning S3's look before those lands would mean tuning it twice.

**Effort:**

| Part | Days |
|---|---|
| Light stages L1–L6 | ≈ 10–16 |
| Volumetric L7 | ≈ 7–10 |
| Shadow stages S2–S9 (shadow study) | 21–26, unchanged |

L7 can run in parallel with L3–L6 after L2, since it only adds a producer to the CTM.

**Per stage, the owner checks in-world once, on the installed build after release, as agreed.**

---

## 6. Decisions for the owner

1. **Strength of cloud shadows from existing EEP skies.**
   - Physical, "what the sky draws, the ground gets": a 0.38 sky loses ~⅔ of its direct sun (§3.4).
   - Or a lower default strength (debug setting) to keep old cloudy-sunny skies sunny.
   - *Recommended: physical (1.0), judged in-world in L2 on the real cloud texture before integration.*
2. **Dome parallax for the flat layer** (§3.3), so the cloud over the sun is the one shading you.
   - It changes the flat sky only when the camera moves horizontally (0.008 of a repeat per 100 m).
   - *Recommended: yes, H = 15 km.* The alternative is world-anchored shadows without exact coupling.
3. **Overcast skylight in the irradiance probes (L4).** It changes PBR ambient under cloudy skies; Linden kept clouds out "to avoid popping", and a uniform veil does not pop. *Recommended: yes.*
4. **Sky-coloured shade with white balance (L5).** It reverses Linden's desaturation of irradiance in non-classic skies. Classic mode stays as it is. *Recommended: yes, behind a debug setting for the comparison.*
5. **Moonlight × `moon_brightness` (L3).** It changes creator night skies that had `moon_brightness` 0 but relied on moonlight. *Recommended: yes; it is EEP's own field.*
6. **Exposure (L6).**
   - Honour EEP HDR settings: `RenderUseExposureSkySettings` today defaults to 0.
   - Widen the default range beyond ±1 EV, or not.
   - *Recommended: honour EEP; widen only after L2–L4 are seen.*
7. **Revisit 2026-11-05 decision 6 ("no cloud-cover penumbra").** Real shadows soften under thin cloud. *Recommended: revisit in S3, with T driving the light size.*
8. **Volumetric clouds: debug setting or a Preferences entry.** By the rules it is a debug setting, off, until the owner asks. Aurora puts it in Preferences, off by default.
9. **Volumetric noise source:** a worker thread + cache, or an application asset of ~8.5 MB. *Recommended: worker + cache, deterministic, no asset to ship.*
10. **The flat layer's penumbra height H_pen** (default 1.5 km, 14 m penumbra at sun_scale 1) and the map coverage (4 km, optional 32 km second level). Defaults are proposed; the owner tunes them in L2.

---

## 7. What was verified and what is inferred

**Verified in code:** every row of §2.1 and §2.2 with file:line, and the GL requirements of §4.1. The settings defaults were read from settings.xml, not from the code's fallbacks:

| Setting | Default |
|---|---|
| `RenderSkySunlightScale`, `RenderHDRSkySunlightScale`, `RenderSkyAmbientScale` | 1.0 |
| `RenderDesaturateIrradiance` | 1 |
| `RenderUseExposureSkySettings` | 0 |
| `RenderTonemapType` | 1 (ACES) |
| `RenderSkyAutoAdjustLegacy` | 0 |

**Measured** (offline GL bench, 5070 Ti only; no second GPU):

- the volumetric trace, reconstruction and composite, fragment against compute;
- the noise generation;
- the two CTM producers and the lookup;
- mean transmittances on a stand-in cloud texture.

**Inferred:**

- mid-class costs (×3);
- the real-world ratios (direct : diffuse, overcast illuminance);
- the visual effect of D3–D6;
- that the dome leaves depth at the far value, to be checked in L7a;
- the sign conventions of §3.3, to be pinned by its unit test;
- the strength numbers on the real SL cloud texture, which only L2 in-world shows.

**Not measured:** in-world frame costs and CPU costs. None is expected beyond uniform uploads, a weather redraw on change and the worker thread for the noise.

---

## 8. Sources

- Schneider & Vos, *The Real-time Volumetric Cloudscapes of Horizon Zero Dawn*, SIGGRAPH 2015 (the model in Aurora).
- Hillaire, *Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite*, SIGGRAPH 2016 (energy-conserving integration).
- Wrenninge, *Production Volume Rendering*, 2013 (multiple-scattering octaves).
- Jendersie & d'Eon, *An Approximate Mie Scattering Function for Fog and Cloud Rendering*, SIGGRAPH 2023 talk (Aurora's phase function; a paper, no sample code used).
- Kasten & Young, *Revised optical air mass tables and approximation formula*, Applied Optics 28, 1989 (L3's airmass).
- Aurora Viewer, `feat/volumetric-clouds` at 7d480ee1 (GPL-3.0-or-later; the volumetric cloud commits are the owner's), `crates/aurora-render/src/clouds.rs`, `shaders/clouds.wgsl`, `shaders/env.wgsl` `fs_cloud_weather`. The bench's GLSL is a transcription of it, with attribution in the script.
