<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# An atmosphere that behaves like a real atmosphere (study, EEP-driven)

Status: study and design. Nothing here changes the viewer. The branch `codex/atmosphere-study` (from `vkstorm-devel` 8d8e31b797) carries:

- this document;
- an offline GL bench, `scripts/light/bench_atmosphere.py`. It runs today's sky and haze with the viewer's own shaders, unmodified, and the proposed model as GLSL written for this study. It produces the renders in `doc/light/atmosphere/`, the tables below and the timings.

The owner's direction (2026-11-09): "Atmosphere that behaves like real atmosphere." It follows the local-lights study (#437) and L2's cloud shadows (#438), and comes before the sun/moon handover (L3).

Code references are file:line at `origin/vkstorm-devel` 8d8e31b797. Shader paths are relative to `indra/newview/app_settings/shaders/`. The light study (#429, `doc/light/light-model-study.md` on `codex/light-study`) is "the light study"; its differences are D1–D9 and its stages L1–L7.

Each claim is tagged:

- **[code]**: read in the code;
- **[measured]**: from the bench on this machine's RTX 5070 Ti (driver 621.14);
- **[inferred]**: reasoning or estimate, not yet checked.

---

## 1. Summary

**Today's atmosphere [code]** is the Windlight haze model, unchanged in substance since 2007.

- One set of formulas (`atmosphericsFuncs.glsl`, `skyV.glsl`, `cloudsV.glsl`) colours the sky, the sun at the ground, the clouds and the distance haze.
- Its quantities are computed in sRGB-like space, then the sky is doubled, clamped at 5 and decoded.
- The EEP Rayleigh, Mie and absorption layers are stored, interpolated and saved, but nothing reads them.
- Linden built a physical sky on Bruneton's library in 2018 (`RenderUseAdvancedAtmospherics`). They removed it in March 2019 (SL-10664, d8eff424a5) and deleted the dead code in 2025 (#5078).

**The differences that show most, ranked (§2.3):**

1. In the PBR path, the drawn sky is about **8× brighter** than a physical sky lit by the same sun **[measured]**, and its horizon is about 6× brighter than its zenith. The sky that lights the scene (the probe capture, ambient zeroed) is close to physical (0.115 against 0.096). So the sky you see is not the sky that lights the scene.
2. Distance haze is grey in extinction (only `atten.r` is used, hazeF.glsl:89), coloured in in-scatter, homogeneous, and it ignores altitude.
3. Twilight is a switch: below the horizon the sky becomes the moon's, with the sun's own colour.
4. The sun's path is plane-parallel (D7). For the Default sky, the curved path changes the sun's light only in the last 3° **[measured]**.
5. EEP's physical layers are unused (D9). The Density tab that edits them is commented out of the editor, so no sky has ever been authored with them.
6. Smaller:
   - multiple scattering is replaced by the authored `ambient`;
   - there is no ozone;
   - the object haze uses an eye-space position against a world-space sun (≈1 % effect, measured).

**Proposed method: Hillaire 2020**, as fragment passes at the GL 4.0 tier the viewer has. It needs no compute stage, and the shader manager has none.

| LUT | Size | Updated |
|---|---|---|
| Transmittance | 256×64 | when the sky's parameters change (every frame during a day-cycle blend) |
| Multiple scattering | 32×32 | same |
| Sky view | 192×108 | every frame |
| Aerial-perspective froxels | 32×32×32 | every frame |

- **Profiles:** EEP's stored layers.
- **Coefficients:** mapped from the legacy haze fields so that the sun at noon is unchanged (§4.2).
- **The sun's colour at the ground and on the clouds** is the same transmittance. The C++ mirror computes it for the uniforms and for the L1 equivalence test.

**Cost [measured], 5070 Ti, 1080p:**

| Part | Cost |
|---|---|
| LUTs per frame (transmittance + sky view) | 0.019 ms |
| Aerial-perspective volume as benched (32 slice draws) | 0.156 ms; ~0.09 ms of it is per-slice pass overhead |
| Per-pixel sky + AP apply | 0.055 ms |
| Today's haze pass, which it replaces | −0.022 ms |
| Multiple-scattering LUT | 0.33 ms, so it runs only when the sky's parameters change, time-sliced |

- **As benched:** ≈ 0.21 ms a frame.
- **With a layered AP draw:** ≈ 0.08–0.10 ms [inferred].

It fits the +5 % budget on this GPU. Mid-range needs the cheaper AP form (§6).

**Compatibility [measured]:**

- The light at the ground stays as the creator made it above ~10° of sun.
- The sky changes visibly for nearly every existing sky, even at matched brightness. Over the 768 shipped sky files, the median sky ΔE is 30; only 176 stay under ΔE 10.
- Default: ΔE 19. Fog, sci-fi and "painted" skies change most, with ΔE 90–113 (§7).

**Recommendation:**

- *Physical light* for every non-classic sky: the sun's and moon's colour at the ground and on the clouds, through the transmittance.
- The *physical sky* (dome, aerial perspective, probe sky) for skies that ask for it, and for the viewer's own defaults.
- Existing PBR skies keep their drawn look unless the owner decides otherwise (decision 1).
- Classic mode is untouched.

---

## 2. Today's atmosphere, read from the code

### 2.1 What each part computes [code]

| Part | Where | Formula (essentials) |
|---|---|---|
| **Sun (or moon) at the ground**, `sunlit` | atmosphericsFuncs.glsl:62-76, 124 | `light × exp(−(blue_density + haze_density/4) · density_multiplier · max_y / lightnorm.y)`. A plane-parallel slab `max_y` thick; `light` is `sunlight_color` or `moonlight_color`, which are the same value (llsettingssky.cpp:1684-1687). In the PBR path the result is decoded from sRGB afterwards (atmosphericsFuncs.glsl:154-159), so the effective optical depth in linear light is ~2.4× the authored one |
| **Ambient**, `amblit` | atmosphericsFuncs.glsl:113, 125, 152-157 | `pow(ambient + (1−ambient)·cloud_shadow/2, 0.9)·0.57`, shaped by `ambientLighting`, reduced to luminance outside classic mode. In PBR the probe irradiance replaces it (reflectionProbeF.glsl:569-571) |
| **Sky colour** | class1/deferred/skyV.glsl:91-170, skyF.glsl:404-412, softenLightF.glsl:195-205 | Per dome vertex: `blue_horizon·blue_weight·(sun+ambient) + haze_horizon·haze_weight·(sun·glow + ambient)`, times `1 − exp(−(bd+hd)·density·distance)`. The sun's airmass toward each point is `1/(dir.y + light.y)` (121-122). Blended below the "cloud line" with `cloud_shadow` (154-167). Then ×2, clamp 5 (skyF.glsl:411-412), decoded, × `sky_hdr_scale` = 2√gamma for PBR skies, 1 for classic (llsettingsvo.cpp:834) |
| **Glow (sun aureole)** | skyV.glsl:133-144 | `(glow.x · (1 − cos θ))^glow.z + 0.25`, only on the haze term; off for the moon (`sun_moon_glow_factor` < 1) |
| **Distance haze on objects** (aerial perspective) | class3/deferred/hazeF.glsl:65-90; atmosphericsFuncs.glsl:79-130; pipeline.cpp:10575 | `atten = exp(−(bd+hd)·density_multiplier·distance_multiplier·d)`, homogeneous, altitude-clamped at `max_y` in eye space (57). Blended `dst·atten.r + srgb_to_linear(2·additive)·sky_hdr_scale`. **Only the red channel of the transmittance is used** (hazeF.glsl:89) |
| **Clouds' light** | class1/deferred/cloudsV.glsl:117-180 | Their own sun attenuation with airmass `1/(2·light.y)` (170-171), the glow, `ambient`; not the sky's |
| **Horizon / below** | skyV.glsl:91-98 | Directions below the horizon are pushed to −32 km, so the dome below the horizon is a haze colour. No planet, no Earth shadow |
| **Night** | llsettingssky.cpp:1324-1366, skyV.glsl:109, llsettingsvo.cpp:860 | When the sun's z < 0 the light becomes the moon (if up): same colour (×0.7 in the sky only), no glow. The sky's colour at night is whatever the keyframe's fields paint; there is no twilight from the sun below the horizon |
| **Under water** | class3/deferred/waterHazeF.glsl; softenLightF.glsl | The view path is fogged; the sunlight that reaches an underwater surface is not attenuated (D8) |

### 2.2 Which EEP fields feed what [code]

| EEP field | Used for |
|---|---|
| `sunlight_color` | sun **and** moon light (shared), sky, clouds, haze |
| `ambient` | sky colour (half of Default's), haze, `amblit`, clouds; **zeroed** in irradiance captures (llsettingsvo.cpp:825-828) |
| `blue_density`, `haze_density`, `density_multiplier`, `max_y` | extinction of the sun, sky gradient, haze |
| `blue_horizon`, `haze_horizon` | colours of the sky and haze in-scatter (`blue_*` desaturated in captures, 695-713) |
| `distance_multiplier` | aerial perspective only |
| `glow` | sky and cloud aureole |
| `gamma` | `sky_hdr_scale` for PBR skies |
| `cloud_shadow` | sky below the cloud line, ambient boost, cloud density |
| `moisture_level`, `droplet_radius`, `ice_level` | rainbow and halo (skyF.glsl) |
| `rayleigh_config`, `mie_config`, `absorption_config`, `planet_radius`, `sky_bottom_radius`, `sky_top_radius`, `sun_arc_radians` | **nothing**: validated, interpolated, saved (llsettingssky.cpp:470-472, 670-672, 1211-1219) |

**Are the physical layers ever authored?** No, in practice.

- The editor's Density tab exists (llpaneleditsky.cpp:778-960) but is commented out of `floater_edit_ext_day_cycle.xml:601-611` ("added programatically so it doesn't show up"). Nothing adds it.
- So skies carry the defaults `LLSettingsSky::defaults()` writes:
  - Rayleigh exp, 8 km;
  - Mie exp, 1.2 km, anisotropy 0.8;
  - ozone two layers;
  - planet 6360 km, top 6420 km.
- No shipped sky file mentions them: the 769 are Windlight files.
- The layers hold only **profiles** (density against altitude), not scattering coefficients. Linden's 2018 code took the coefficients from libatmosphere's Earth values.

**A bug in the stored ozone default [code, arithmetic].** `absorptionConfigDefault()` (llsettingssky.cpp:866-875) writes the lower layer as `linear −1/25000, constant −2/3`. That is negative everywhere below 25 km, so it clamps to 0. Bruneton's lower ramp is `+1/15000, −2/3`. With the stored values, ozone exists only between 25 and 40 km: half the intended column (7.5 instead of 15 km·density). A physical model that "uses the stored layers" must either fix this, or knowingly use half an ozone layer. Proposed: read layers as stored, but recognise this exact default and substitute Bruneton's ramp (§4.2).

### 2.3 Where it differs from a real atmosphere, ranked

"Real" means a planet-curved atmosphere with Rayleigh, aerosol and ozone, single + multiple scattering, lit by the same sun the ground gets. Rank is by how much and how often it shows.

| # | Difference | Evidence | How it shows | Rank |
|---|---|---|---|---|
| A1 | **The drawn sky is ~8× brighter than the sky that lights the scene.** Sky colours are formed in sRGB-like space, ×2, clamp 5, decoded, ×2√γ; half of Default's comes from `ambient`. Captures zero `ambient` and desaturate | skyV/skyF above; llsettingsvo.cpp:825-834. **Measured:** PBR Default zenith 0.28, horizon 1.71 (linear), against a sunlit 18 % grey panel at 0.08. The physical sky for the same sun is 0.08 / 0.16 (§7) | Every PBR sky: a sky brighter than a white wall in sun, a white horizon band. A real clear sky is about as bright as an 18 % grey card in sun | **HIGH** |
| A2 | **Distance haze is grey in extinction, homogeneous, and blind to altitude** | hazeF.glsl:89 (`atten.r`); atmosphericsFuncs.glsl:57, 79-84 | Far objects do not redden or blue-shift as they fade. A skybox at 3000 m sees the same haze as the ground. Haze does not thin upward, so looking down from a height is as foggy as looking across | **HIGH** in hazy skies, MEDIUM in clear |
| A3 | **No twilight; night is a repaint.** The sky lit by a sun below the horizon (the blue hour, the Belt of Venus, Earth's shadow) does not exist. When the sun crosses 0°, the light, the glow and the sky's light colour switch to the moon's | llsettingssky.cpp:1324-1366; skyV.glsl:109, 141-144. Bench sweep (`sunset_sweep.png`): at −2° today the sky jumps to a moon-lit navy; physically it would still glow orange at the horizon | Twice a day, every day cycle | **MEDIUM-HIGH** |
| A4 | **The horizon at sunset: a dark band and a black sun (D7).** Sun at the ground: `1/sin(el)`. Sky: `1/(dir.y + light.y)`, both singular at 0 | atmosphericsFuncs.glsl:75-76; skyV.glsl:121-122; cloudsV.glsl:170 | **Measured:** the Default sky's sun is the same curved or flat above 3° (0.0027 against 0.0024 at 3°, both 0 at 1°). Its column is ~3× Earth's, so even a physical sun sets dark in it. The visible part is the sky's dark band near the horizon at low sun, and creator sunsets with thinner air (Sunset.xml: 0.080 → 0.144 at 3°, 0.0004 → 0.0021 at 1°) | MEDIUM for sunsets, LOW otherwise |
| A5 | **The physical layers are unused, and cannot be authored (D9)** | §2.2 | No way to make "a thinner atmosphere", "a Mars sky" or "more ozone" except by painting | MEDIUM (authoring) |
| A6 | **Multiple scattering is an authored constant (`ambient`)**, the same in every direction, colour chosen by the creator | skyV.glsl:147-148, 154-160 | The sky does not brighten toward the horizon by itself, and a thick haze does not get lighter as it should | LOW-MEDIUM (folded into A1) |
| A7 | **No ozone** | — | The zenith at twilight is not deep blue/violet | LOW (twilight only) |
| A8 | **Clouds are lit by their own sun term** (`1/(2·light.y)`), not the sky's | cloudsV.glsl:169-174 | Clouds and sky can disagree near sunset. L2's map and L7 need one source | LOW now; it matters for L7 |
| A9 | **Object haze mixes spaces:** eye-space position against world-space `lightnorm` in the glow term | atmosphericsFuncs.glsl:90 with hazeF.glsl:31-41 (eye-space `sun_dir`, pipeline.cpp:10052-10055) | **Measured:** ≈1 % of the haze colour on a hazy sky when the camera turns 90° | LOW (bug, invisible) |
| D8 | **No sunlight attenuation under water** | §5 | Underwater lit like land | LOW (§5) |

---

## 3. What a physical atmosphere needs from EEP

**The physics (Bruneton & Neyret 2008; Hillaire 2020) [design]:**

- a planet and a top radius;
- per constituent, a density profile and coefficients:
  - Rayleigh scattering (= extinction);
  - aerosol scattering and absorption, plus a phase function;
  - ozone absorption;
- the sun's irradiance at the top;
- a ground albedo for the bounce.

**What EEP stores:**

- the radii and the profiles (as stored, with the ozone fix);
- one anisotropy;
- the sun's colour.

**What EEP does not store:** coefficients. They have to come from somewhere, and the only authored source is the legacy haze fields.

---

## 4. A physical atmosphere driven by EEP

### 4.1 Method: Hillaire 2020 LUTs, fragment passes [design]

| Pass | Size, format | Inputs | Update | Notes |
|---|---|---|---|---|
| Transmittance T(r, μ) | 256×64 RGBA16F | profiles, coefficients | on parameter change; every frame while a day cycle blends | Bruneton's (r, μ) parameterisation; 40 steps |
| Multiple scattering Ψ(r, μ_s) | 32×32 RGBA16F | T | as T | Hillaire's isotropic 2nd-order + infinite series, 64 directions × 20 steps, ground albedo 0.2 |
| Sky view | 192×108 RGBA16F | T, Ψ, sun & moon | every frame | latitude spaced √ about the horizon, longitude about the light; 32 steps; the observer at a fixed altitude (decision 4) |
| Aerial perspective | 32³ ×2 (in-scatter RGB, transmittance RGB), RGBA16F | T, Ψ | every frame | one draw per slice (no compute stage); slices quadratic to the far clip; 4–20 steps |
| Sky apply | per pixel | sky view, T | every frame | replaces skyV's per-vertex colour; adds the sun disc through T |
| AP apply | per pixel | depth, froxels | every frame | replaces hazeF; **dual-source blend** `dst·T_rgb + L` (GL 3.3) for coloured extinction |
| Sun/moon at the ground | CPU | profiles, coefficients | per frame | the same integral in C++, for `sunlit`, the clouds' light and L2/L3. The renderer-free mirror (§4.5) |
| Probes | — | sky apply | as today | captures draw the same sky, so the irradiance is the physical sky's |

**Why Hillaire over the alternatives:**

- **Bruneton 2008** is precomputed 4D scattering. It is heavier to update, which a day-cycle blend does every frame. It was Linden's 2018 choice and was dropped.
- **Preetham / Hosek–Wilkie** are clear-sky fits from turbidity. They have no aerosol profile, no aerial perspective, no night, and they cannot take EEP's colour fields. They serve here as a plausibility check on the sky's shape.

Hillaire's model:

- is per-frame cheap;
- handles any sun elevation, including below the horizon (twilight), any profile, and multiple scattering;
- gives the aerial perspective and the sun transmittance from the same data.

**No NVIDIA or Unreal sample code was used.** The bench's GLSL is written from the papers' equations.

### 4.2 Mapping the legacy fields onto physics [design + measured]

Every sky in the wild carries the legacy fields and default layers. So the mapping is not an edge case: it is the design. It keeps what the creator saw in **the light**, which can be matched exactly, and lets the sky follow physics.

1. **Profiles:** EEP's stored layers. A non-default layer wins. The broken ozone default is replaced by Bruneton's.
2. **The column (the sun's colour at noon), matched exactly.** Today's PBR sun at the zenith is `lin(sc · exp(−(bd + hd/4)·dm·max_y))`. The physical one is `lin(sc) · exp(−τ_lin)`. Solve τ_lin per channel.
   - Split τ into Rayleigh (`bd` share) and aerosol (`hd/4` share). Earth's ozone column comes out of the Rayleigh part, so noon stays matched.
   - Then the ground coefficients are τ/H with the profiles' scale heights.
   - *Measured:* the sun at the ground then equals today's at 90° and 30°, within 1 % at 10°, and departs only below ~5° (§7, table 2).
3. **Distance haze at the ground.** Today's coefficient is `(bd + hd)·dm·distance_multiplier`. What the column gives at the ground is usually less (Default: 0.108 of 0.136 /km). The rest becomes a **ground haze slab** (aerosol, constant density to `H_slab`, smooth top), and its column comes out of the aerosol column.
   - This is the physical reading of SL's dense near-ground haze: a boundary layer.
   - `H_slab` = 0.25 km in the bench (decision 3).
4. **Phase:** the creator's glow lobe itself, normalised over the sphere, as the aerosol phase function. The aureole stays the creator's.
5. **Colours of the scattered light.** Default.xml is the reference:
   - Rayleigh albedo `blue_horizon / Default's`;
   - aerosol albedo `0.9 · haze_horizon / Default's`;
   - both capped at 1 (above 1 the multiple-scattering series diverges);
   - `ambient / Default's` scales Hillaire's multiple scattering.
6. **Light above the atmosphere:**
   - The sun is `3·lin(sunlight_color)`: the viewer's sun on a surface is `sunlit·3` (deferredUtil.glsl:615), so the scattered light uses the same irradiance.
   - The moon is the same colour (EEP shares it). Its scale is decision 5.
7. **Gamma:** √γ on the sky, as `sky_hdr_scale` today.
8. **Out of range → today's model for that sky:**
   - `density_multiplier` ≤ 0, `max_y` ≤ 0, negative `distance_multiplier`;
   - a noon column τ > 8;
   - a ground haze > 50 /km.

   The library numbers are in §7.

**What the mapping can and cannot keep [measured]:**

- It keeps the light: the sun, the clouds' light and the sky irradiance (Default: physical 0.096 against today's capture 0.115).
- It cannot keep today's drawn sky, for three reasons:
  - its brightness is ~8× physical (2.95 EV for Default, fitted);
  - its colour saturation comes from the sRGB-space arithmetic: B/R ≈ 2.6 in the formula becomes ≈ 10 once decoded;
  - its horizon band is ~6× the zenith (physical ≈ 2×).
- At matched brightness, Default still differs by ΔE 19; with a fitted per-channel tint as well, by 13.6. Hazy and painted skies by much more (§7).

### 4.3 Transmittance, sky, aerial perspective, night [design]

- **Sun colour at the ground and red sunsets:**
  - T along the curved path, so no singularity at 0°;
  - the sun reddens and dims through the column the creator set;
  - behind the planet it is 0, softened over the sun's disc (±0.27°).
- **Sky radiance:** single + multiple scattering, with the Earth's shadow and the twilight arch for a sun below the horizon.
- **Aerial perspective:** coloured in-scatter and coloured extinction, thinning with height.
- **Multiple scattering:** Hillaire's Ψ.
- **Ozone:** Earth's coefficients on the (fixed) EEP profile.
- **Night / twilight:**
  - Two lights, the sun and the moon, both through T.
  - The sun keeps lighting the sky down to about −18° (the planet's shadow does the rest).
  - The moon lights the sky as a dim sun.
  - Stars and the Milky Way (the real night sky, skyF.glsl) are added after, through T toward each star. This replaces their fixed extinction.

### 4.4 Which skies get what [design]

| Sky | Light at the ground and clouds (T) | Drawn sky, AP, probe sky |
|---|---|---|
| **Classic** (no probe ambiance, `RenderSkyAutoAdjustLegacy` 0) | **today's**, unchanged | today's, unchanged (the oracle for legacy skies) |
| **PBR sky with legacy fields only** (all existing PBR skies) | **physical**, mapped (§4.2) | today's by default; physical by decision 1 |
| **Sky with physical intent** (non-default layers, written by the restored Density tab; Vulkanstorm's own default skies) | physical | **physical** |

The dome and the aerial perspective must switch together. A physical AP in front of a legacy horizon leaves a seam where distant objects no longer fade into the sky behind them.

### 4.5 Renderer-free core and the L1 mirror [design]

- `llatmosphericlight.h` (L1) gains `LLAtmosphere`. It holds the mapping (§4.2) and the transmittance integral, in double, with no GL.
- The C++ feeds the uniforms (`sunlit`, the clouds' sun) and the CPU fallback.
- The GLSL transmittance LUT is checked against it by vectors generated on the GPU, as L1 does (`gen_atmosphere_vectors.py` extended, the CI test without a GPU).
- The Vulkan line shares the header unchanged.

---

## 5. How it feeds the light

| Consumer | From the atmosphere | Note |
|---|---|---|
| `sunlit` (softenLight, alpha, water, materials) | C++ T at the camera's altitude × lin(sunlight); moon: × decision 5 | replaces `exp(−τ/sin)`; classic keeps today's |
| `amblit` fallback | the physical sky's irradiance, mean (the sky-view LUT, cosine-weighted, CPU-side once per frame from a 16×8 readback or SH) | |
| Probes (L4, L5) | captures draw the physical sky; the irradiance is physical, coloured | L5's "keep the colour" becomes natural. L4's overcast veil multiplies it |
| Exposure (L6) | the physical sky is 2–3 EV darker on screen than today's; L6's metering and range must accept it (decision 2) | |
| L2 cloud map | multiplies `sunlit`; unchanged | T is the clear-sky part, the map is the cloud part |
| L7 clouds | Aurora's trace takes `sun_col`, `amb_col`, haze colour and distance (Aurora `clouds.rs:89-91, 147-149`). They become T at the cloud altitude × E_sun, Ψ/irradiance at the cloud altitude, and the AP | one source for sky, ground and clouds |
| Flat clouds (cloudsV) | sun colour = T at ~2 km toward the sun; ambient = the sky irradiance | replaces `1/(2·light.y)` (A8) |
| Stars, Milky Way | × T toward the star | |
| L1 mirror | `LLAtmosphere` + GPU vectors | §4.5 |

**Water (D8), statement only.**

- Today the view path under water is fogged (waterHazeF.glsl) and sunlight on underwater surfaces is not.
- The physical fit: sunlight reaching depth z is `sunlit · exp(−k·z/μ_refracted)`, with k from the EEP water fog density, colour from the water fog colour. That is one exp per pixel below the water plane, given the plane height that hazeF already has.
- It is cheap. It belongs with the water work, not with the atmosphere; no design here.
- The 2026-11-03 decision "no shadows under water" stands.

---

## 6. Cost

**Bench [measured]:** `scripts/light/bench_atmosphere.py`, RTX 5070 Ti, driver 621.14, GLSL 4.00 shaders, `GL_TIME_ELAPSED` median of 60 after 10 warm-up. The sky is Default (two lights: sun and moon). The depth buffer is a stand-in: 1 m to 1 km, a noisy gradient.

| Pass | 1080p | 1440p | Frequency |
|---|---|---|---|
| Transmittance LUT 256×64 | 0.0035 ms | same | on change (every frame in a blend) |
| Multiple-scattering LUT 32×32 (64 dirs × 20 steps) | 0.33 ms | same | on change, time-sliced over 8 frames → ≤ 0.04 ms/frame [inferred] |
| Sky-view LUT 192×108 (32 steps, 2 lights) | 0.016 ms | same | every frame |
| AP volume 32³, 32 slice draws, 4–20 steps | 0.156 ms | same | every frame |
| — same with 2 steps per froxel | 0.098 ms | same | (the cost of an incremental scheme) |
| — same with 1 step per froxel | 0.087 ms | same | (shows the per-slice pass overhead; both variants by changing `4 + slice / 2` in `AP_FS`) |
| AP apply (2 3D taps, dual-source blend) | 0.037 ms | 0.064 ms | every frame |
| Sky apply (every pixel treated as sky; LUT + T tap + disc) | 0.018 ms | 0.030 ms | every frame |
| **Today's haze pass** (hazeF.glsl + atmosphericsFuncs, same stand-in) | **0.022 ms** | **0.141 ms** | replaced |

The 1440p haze figure is 6× the 1080p one for 1.8× the pixels. It was not investigated; treat it as an upper bound.

**Per frame:**

| | 1080p | 1440p |
|---|---|---|
| As benched | 0.0035 + 0.016 + 0.156 + 0.037 + 0.018 − 0.022 ≈ **0.21 ms** | ≈ **0.13–0.15 ms** (the haze pass it replaces is dearer) |
| Plus the multiple-scattering LUT, time-sliced in a blend | +0.04 ms | +0.04 ms |
| AP built in one instanced draw with `gl_Layer` (geometry shader, GL 4.0) instead of 32 separate draws | **≈ 0.08–0.10 ms** [inferred] | — |

The 32 separate draws are needed because the shader manager has no compute stage, and their pass overhead dominates the AP volume. The layered draw removes it.

Probe captures draw the sky from the same sky-view LUT: one tap per capture pixel, negligible. The CPU transmittance integral (400 steps, 3 channels) is a few µs [inferred].

**Mid-range [inferred]:** ×3, as the light study used. It assumes a GPU of a third of the 5070 Ti's rate (e.g. an RTX 3060).

- As benched: ≈ 0.6 ms at 1080p.
- With the layered AP: ≈ 0.25–0.3 ms.
- Low-end (×10): the legacy model stays (a tier, §8).

**Against the +5 % budget [inferred].** The budget covers light and shadows together, at the default tier. At 60 fps (16.7 ms) it is 0.83 ms; at 144 fps (6.9 ms) it is 0.35 ms. On this GPU at 1080p:

| Item | Cost |
|---|---|
| L2 cloud map | 0.02 ms |
| L7 volumetric clouds | 0.13–0.26 ms |
| Atmosphere, layered | 0.08–0.10 ms |
| **Total** | **0.23–0.38 ms**, before the local lights' own +5 % on their pass |

- **At 144 fps it is tight.** The AP volume can update every other frame, or with 16 slices.
- **On mid-range GPUs** L7 and the atmosphere do not both fit at full rate. The default tier scales the AP slices and rate and the multiple-scattering rate, and keeps the legacy model below the tier.

---

## 7. Compatibility: before and after

**Renders [measured]**, in `doc/light/atmosphere/`:

- Each PNG is a 360° panorama centred on the light, from 12° below the horizon to the zenith. A grey ground (albedo 0.18) runs out to 1 km, seen from 20 m.
- Three rows per sky:
  1. **today**: skyV.glsl by transform feedback, skyF's ×2/clamp/decode, hazeF over the ground; the ground lit by today's `sunlit` and the capture's sky irradiance (ambient zeroed, desaturated);
  2. **proposed, physical radiance** (display gain 1);
  3. **proposed, display gain 7.74** (2.95 EV, fitted so Default's sky matches today's in mean log luminance).
- All are tone-mapped with the viewer's `toneMapACES_Hill` at exposure 1. There is no sun disc, no clouds and no stars.

| Sky (file, sun as in the file) | Sky ΔE (gain-matched) | Ground ΔE | Zenith / horizon lum, today → physical | Sky irradiance, today's capture → physical | Sun at ground, today → proposed |
|---|---|---|---|---|---|
| `default.png`: Default.xml, 66° | 19.4 | 29.2 | 0.279 / 1.711 → 0.078 / 0.157 | 0.115 → 0.096 | 0.371 0.369 0.406 → 0.372 0.371 0.408 |
| `sunset.png`: Sunset.xml, 4.3° | 40.5 | 54.0 | 0.142 / 0.422 → 0.061 / 0.177 | 0.154 → 0.111 | 0.879 0.129 0.010 → 1.161 0.206 0.013 |
| `night.png`: Night.xml, moon at zenith | 10.1 | 1.9 | 0.003 / 0.004 → 0.002 / 0.0005 | 0.002 → 0.002 | 0.045 0.047 0.167 → 0.045 0.047 0.173 |
| `hazy.png`: Foggy.xml, 90° | 62.7 | 21.6 | 85.9 / 1.49 → 0.014 / 0.054 | 1.45 → 0.018 | 0.151 0.135 0.113 → 0.098 0.087 0.073 |

`sunset_sweep.png` shows Sunset.xml at +8°, +3°, +1°, −2° and −6°: today and proposed (gain-matched) alternate, today first.

**What the renders show:**

- **Default** keeps a blue sky and its aureole (the creator's glow lobe is the phase function). Physically:
  - the sky is ~3.6× darker than today's PBR sky at the zenith and ~11× darker at the horizon;
  - the white horizon band goes;
  - at matched brightness it is paler (Default's air is ~3× Earth's column, much of it aerosol);
  - distant ground fades to a cooler colour.

  The sky irradiance, i.e. what lights the shade, is within 16 % of today's probes.
- **Sunset.xml:**
  - The sun on the ground is 30 % brighter at 4.3°, 1.8× at 3° and 5× at 1°: the last degrees D7 was about.
  - The sky turns teal-green with an orange horizon arch, where today it is blue with a white glow. The creator's strong blue Rayleigh, lit by a red low sun, gives that hue physically.
  - Below the horizon (−2°, −6°) today's sky jumps to the moon's navy. The physical sky keeps the twilight arch.
  - Because EEP gives the moon the sun's colour, a moon at +6° lights the sky like a second sun (the last row). This is decision 5.
- **Night.xml:**
  - Both are near black.
  - Painted nights with the sun *up* (e.g. "[TOR] NIGHT - Moony", sun at 13.7° with a blue `sunlight_color`) become a dim day sky physically; they are among the large changes.
- **Foggy.xml:**
  - Today's PBR sky is clamped white: the zenith is 86 linear (clamp 5 decodes to 47, ×2).
  - The physical fog is a 0.25 km slab of 5.8 /km. It is dark grey at gain 1 and blue at the fitted gain, and the sun dims by a third.
  - This is the method's weakest case. Hillaire's multiple-scattering term is built for optically thin air, and thick fog needs more orders [inferred].

**The library [measured]:** 768 sky files, each at its own sun, 240×72 panoramas, gain 7.74.

| | Median sky ΔE | 90th pct | Under ΔE 10 | Under ΔE 20 |
|---|---|---|---|---|
| All 768 | 29.6 | 67.0 | 176 | 277 |
| 721 inside the physical range | 30.8 | 66.5 | 147 | 245 |
| All, with Default's per-channel tint (0.76, 0.95, 1.61) as well | 30.6 | 67.5 | 174 | — |

- **47 skies are outside the range** (negative distance multiplier, zero density and the like): they stay on today's model.
- **The tint does not help.** The difference is in shape (the horizon band, the saturation from sRGB arithmetic), not in a colour cast.

**Changed most:**

| Sky | Sun | ΔE |
|---|---|---|
| [TOR] SCIFI - Kelf shelf | −2° | 113 |
| [TOR] BIG SUN - Slips through walls 6 | 18° | 101 |
| [TOR] SCIFI - Wild Palms | 90° | 101 |
| Phototools- Trilogy Rain 02 | 79° | 98 |
| [TOR] FOGGY - Das fog | 0° | 98 |
| [TOR] SUNSET - Azure desertation 2 | 2° | 96 |
| [TOR] SCIFI - Arrakissed 1 | −12° | 95 |
| Phototools- Hospital Light 01 | 20° | 94 |
| [TOR] FOGGY - Fogvari | 86° | 93 |

They are creative skies built on the clamp and the sRGB arithmetic. The least changed are out-of-range Phototools light rigs, which stay on today's model (ΔE 0).

**The light alone [measured]:** the sun at the ground against elevation, luminance, linear, today → proposed.

| Sky | 90° | 30° | 10° | 5° | 3° | 2° | 1° |
|---|---|---|---|---|---|---|---|
| Default.xml | 0.386 → 0.388 | 0.264 → 0.265 | 0.070 → 0.070 | 0.013 → 0.013 | 0.0027 → 0.0024 | 0.0006 → 0.0005 | 0 → 0 |
| Sunset.xml | 8.10 → 8.30 | 5.88 → 6.17 | 1.91 → 2.06 | 0.43 → 0.53 | 0.080 → 0.144 | 0.014 → 0.040 | 0.0004 → 0.0021 |
| [TOR] SUNSET - Bangko 1 | 1.94 → 1.82 | 1.79 → 1.58 | 1.36 → 0.86 | 0.95 → 0.38 | 0.63 → 0.16 | 0.43 → 0.065 | 0.18 → 0.005 |

- **Where the ground-haze slab fits inside the creator's column** (Default, Sunset), the light is today's above 10°, and brighter and redder in the last degrees.
- **Where the creator's near-ground haze is much denser than the column** (Bangko: distance multiplier 16), the slab dims the low sun physically: it sets into the haze bank. Noon is then not exactly matched (−6 %).
- **The slab height decides** how many skies fall in the second group (decision 3).

**Which skies change most, in one line:** fog, sci-fi and painted-night skies, built on the white clamp and on `ambient`, change completely. Clear and sunset skies change in brightness (≈3 EV darker drawn sky) and in their horizon band. The light on the ground changes only near the horizon, except in skies with dense near-ground haze.

---

## 8. Staged plan

Same rules as the light study:

- a `codex/` feature PR from `vkstorm-devel`, RelWithDebInfo, staged, no installer;
- renderer-free logic in `llatmosphericlight.h` with tests;
- no Preferences entries; debug settings only;
- the split-screen today/proposed diagnostic only on the feature branch, guarded by `check_release_hooks.py`.

Gates are measured in-world with Frame Profile on the four baseline scenes (PCSS on, against the windows-latest 82080 baseline) at 1080p and 1440p.

| Stage | Scope | Performance gate | Owner checks in-world | Effort |
|---|---|---|---|---|
| **AT0** Mirror and mapping | `LLAtmosphere` in llatmosphericlight.h: the §4.2 mapping, the ozone-default fix, the CPU transmittance; GLSL transmittance LUT; GPU-generated vectors and the CI test | none (no render change) | nothing | 1–2 days |
| **AT1** Physical light (**absorbs L3's airmass**) | `sunlit` and the flat clouds' sun from T for non-classic skies; moonlight per decision 5; L3's cross-fade then fades T instead of switching. Classic unchanged | 0 GPU; CPU < 0.05 ms | Sunsets in 3 PBR skies: the sun reddens and stays to ~1°; no change at noon | 1–2 days (inside L3) |
| **AT2** Physical sky + AP for opted-in skies | multiple-scattering, sky-view and AP passes (layered draw), sky apply, AP apply with dual-source blend; Density tab restored (decision 7), and non-default layers mark physical intent; probe captures draw the physical sky; stars × T | ≤ 0.10 ms at 1080p and ≤ 0.12 ms at 1440p on the 5070 Ti (net of hazeF); mid-range tier rules; no pop over a sped-up day cycle; stable under camera motion | An authored physical sky: clear noon, sunset into twilight (−6°), a moonlit night, a hazy valley seen from a skybox | 6–8 days |
| **AT3** Existing PBR skies (decision 1) | the mapping applied to legacy-field PBR skies, or not; debug setting `RenderAtmospherePhysical` (0 legacy, 1 physical) for the comparison; exposure hand-off to L6 (decision 2) | as AT2 | 10 popular PBR skies before and after, chosen by the owner | 2–3 days |
| then **L4** | the overcast veil multiplies the physical irradiance | as there | as there | as there |
| **L5** | the shade's colour comes from the physical sky; no desaturation for physical skies | as there | as there | as there |
| **L6** | exposure range and metering accept the ~3 EV darker drawn sky | as there | as there | as there |
| **L7** | Aurora's cloud light inputs from T, Ψ and the AP at cloud altitude | as there | clouds and sky agree at sunset | +1 day inside L7 |

**Order:**

- AT0 and AT1 go right after L2, merged with L3. The handover needs T anyway, and a Kasten–Young formula would be thrown away.
- AT2 can run in parallel with L4–L6.
- AT3 comes after L6, because the exposure decides how a physical sky looks.
- The shadow stages are unaffected, except S3's light colour (from AT1) and S6 (T at the occluder is the same T).

**Effort:** ≈ 10–15 days for AT0–AT3. L3 shrinks by its airmass part.

---

## 9. Decisions for the owner

1. **Which skies get the physical sky (dome + AP + probe sky)?**
   - (a) only skies with physical intent, and Vulkanstorm's own defaults;
   - (b) also every existing PBR sky, through the mapping;
   - (c) also classic skies.

   *Recommended: (a) now, (b) judged in AT3 on real PBR skies with the debug setting.* The mapping keeps the light but not the drawn look (median ΔE 30 over the library). (c) breaks the classic oracle.
2. **Brightness of the drawn sky:**
   - physical: ~3 EV darker than today's PBR sky; the sky seen equals the sky that lights; L6 brings the exposure up;
   - or a display gain (7.7, fitted on Default) on the dome and AP only, which keeps today's brightness but not "the sky that lights".

   *Recommended: physical, with L6.*
3. **The ground-haze slab height** (`H_slab`, 0.25 km in the bench). It decides how far SL's dense distance haze dims a low sun (the Bangko row). *Recommended: 0.25 km, tuned in AT1 on the owner's sunset skies.*
4. **Observer altitude.**
   - Fixed at the ground: skyboxes at 3000 m keep their sky and haze, as today.
   - Real altitude above the water: thinner air and a darker sky up high, and looking down through the haze.

   *Recommended: fixed for the sky; the AP uses the real height difference, so haze thins upward from the camera.*
5. **The moon's light.** EEP gives the moon the sun's colour.
   - ×`moon_brightness` for the ground (the light study's decision 5) and a fixed sky factor (≈1/50) so a moon at 6° does not paint a day sky.
   - Or keep the moon out of the sky's scattering.

   *Recommended: `moon_brightness` on both, with the sky factor tuned in AT1.*
6. **Fix the stored ozone default** when reading layers (§2.2). *Recommended: yes; it shows only at twilight.*
7. **Restore the Density tab** in the sky editor so creators can author physical skies. It edits the profiles only; the coefficients stay derived from the familiar fields. It is a hidden Linden UI, not a new Preferences entry. *Recommended: yes, in AT2.*

---

## 10. What was verified and what is inferred

**Verified in code:**

- every row of §2.1–§2.3 with file:line;
- the unused layers and the hidden Density tab;
- the ozone default arithmetic;
- the 2018–2019 history;
- the eye/world space mix in the object haze;
- Aurora's sky being the same legacy model.

**Measured (5070 Ti, offline GL bench, the viewer's shaders unmodified for "today"):**

- the panoramas and ΔE of the four skies and the 768-file library;
- the sun at the ground against elevation;
- the sky irradiance against today's capture;
- the 2.95 EV gain between today's PBR sky and a physical one;
- the ≈1 % camera-turn effect;
- every pass time in §6.

**Inferred:**

- mid-range and low-end costs (×3, ×10);
- the layered-AP cost;
- the time-sliced multiple-scattering cost;
- the fog weakness of Hillaire's multiple scattering;
- how skies are distributed in-world (the library is the shipped Windlight set, not SL's live EEP skies);
- that LSL cannot set the layers (LSL's environment constants are not in this repository).

**Limits of the bench:**

- no clouds, sun disc, stars, rainbow or halo;
- one ground plane and one probe (the sky only);
- PBR path only (classic stays today's by design);
- exposure fixed at 1;
- the AP in the renders is integrated per pixel (what the froxels store), while the timing uses the froxels.

**Not measured:** in-world frame time; any second GPU.

---

## 11. Sources

- E. Bruneton, F. Neyret, *Precomputed Atmospheric Scattering*, EGSR 2008 (parameterisation, Earth coefficients).
- S. Hillaire, *A Scalable and Production Ready Sky and Atmosphere Rendering Technique*, EGSR 2020 (LUTs, multiple scattering).
- A. J. Preetham, P. Shirley, B. Smits, *A Practical Analytic Model for Daylight*, SIGGRAPH 1999; L. Hosek, A. Wilkie, *An Analytic Model for Full Spectral Sky-Dome Radiance*, SIGGRAPH 2012 (comparison only).
- W. M. Cornette, J. G. Shanks, *Physically reasonable analytic expression for the single-scattering phase function*, Applied Optics 31, 1992.
- F. Kasten, A. T. Young, *Revised optical air mass tables and approximation formula*, Applied Optics 28, 1989 (L3; superseded by the T integral, §8).
- Linden Lab history in this repository: 99b82d9449 (2018-02-18, "Atmospherics WIP"), 6cc4a091b3, d8eff424a5 (2019-03-02, SL-10664, removal), 19d9ded23d (2025-12-01, #5078).
- Aurora Viewer (github.com/anne-skydancer/Aurora-Viewer, GPL-3.0-or-later):
  - `main` 5fdc11fa `crates/aurora-render/src/shaders/common.wgsl` `atmos_eval` (the same legacy haze model, transcribed);
  - `env.wgsl` `sky_stars` (an airmass extinction for stars);
  - `feat/volumetric-clouds` `clouds.rs` (the cloud light inputs).

  Aurora has no physical atmosphere to port; it is data for the cloud interface only.
