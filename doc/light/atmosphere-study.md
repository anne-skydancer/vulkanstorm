<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# An atmosphere that behaves like a real atmosphere (study, EEP-driven): refresh of 2026-11-16

Status: study and design. Nothing here changes the viewer. This refresh supersedes study #439 (`codex/atmosphere-study`, 2026-11-09)
and re-reads it on `vkstorm-devel` 191c9985b9. AT0 (#441), L3 + AT1 (#447, #452, #461), L2 (#438), L2b (#465), AT2-0 (#462),
L7-0 (#454), the GL 4.6 floor (#537) and the dark-cascade skip (#540) have landed since. #439's renders and its bench move here
unchanged, so they survive when #439 is closed.

| Piece | File |
|---|---|
| This document | `doc/light/atmosphere-study.md` |
| GPU bench of the remaining passes, on the viewer's AT2-0 GLSL twin (fragment passes, GL 4.6, lock file) | `scripts/light/bench_atmosphere_passes.py` |
| CPU bench of the per-frame C++, and the translated skies the GPU bench marches | `scripts/light/bench_atmosphere_cpu.cpp` |
| Accuracy check of the sky-view march (new finding R2) | `scripts/light/check_skyview_steps.cpp` |
| #439's bench and renders, carried unchanged (a note in its docstring says what no longer models the viewer) | `scripts/light/bench_atmosphere.py`, `doc/light/atmosphere/*.png` |
| Raw output of every run in this refresh | `doc/light/atmosphere/bench-2026-11-16.txt` |

Related documents on devel: `doc/light/atmosphere-scattering-core.md` (AT2-0, "the core doc"), `doc/light/handover-l3-at1.md`
(L3 + AT1), `doc/light/cloud-shadows.md` (L2, L2b), `doc/light/volumetric-clouds.md` (L7-0), `doc/light/light-reference-numbers.md`
(L1). Shader paths are relative to `indra/newview/app_settings/shaders/`; file:line references are at 191c9985b9.

Each claim is tagged:

- **[code]**: read in the code at 191c9985b9;
- **[measured]**: run for this refresh. GPU: RTX 5070 Ti, driver 621.14, OpenGL 4.6. CPU: Ryzen 9 9950X3D, g++ 13.2 `-O2`;
- **[inferred]**: reasoning or an estimate, not checked;
- **[decided]**: an owner decision already recorded (2026-11-09 for #439, 2026-11-11 for the AT2 survey and AT2-0).

---

## 1. Summary

**Where it stands.** The light the scene gets from the sun and the moon is already physical:

- AT1 computes the curved-path transmittance on the CPU, from the EEP profiles, once per frame (about 15 µs [measured]).
- It hands over between sun and moon smoothly by the sun's elevation.
- It feeds `sunlit`, the flat clouds' sun, the shadow strength and #540's dark gate.

What the viewer **draws** is still the 2007 Windlight model: the sky dome, the distance haze, the probe sky, the stars' extinction
and the clouds' ambient. AT2-0 left a tested CPU core and an **unregistered** GLSL twin of Hillaire 2020 behind it. Nothing renders
with them yet.

**Findings, ranked** (§2 has the file:line evidence):

| # | Finding | Verdict on devel |
|---|---|---|
| R1 | The PBR sky is ~8× brighter than a physical sky lit by the same sun (the display gain that matches today's sky; per pixel 3.6× at the zenith, 10.7× at the horizon), and its horizon ~6× its zenith | **STILL TRUE**: re-measured on today's shaders, zenith 0.278 and horizon 1.676 against 0.078 and 0.157 physical |
| R2 | **New.** The AT2-0 sky march (`sky_radiance`, `pa_sky_radiance`) uses uniform steps to the top of the atmosphere and skips the 0.25 km haze slab | **NEW, CONFIRMED** [measured]: up to 98 % low in Foggy and 88 % in Sunset at 32 steps; median 27-33 %. The slab-split march at 64 steps is within 1.2 %. AT2-0's "no whiteout" library check samples the uniform march |
| R3 | Distance haze is grey in extinction, homogeneous and blind to altitude | **STILL TRUE** |
| R4 | No twilight, and a hard switch at 0° | **CHANGED**: AT1 replaced the switch with a smooth ±0.3° band. There is still no twilight *sky*: the dome is repainted, not lit by a sun below the horizon |
| R5 | Plane-parallel airmass | **CHANGED**: fixed for the ground light and the clouds' sun (AT1). Still true for the dome and for the stars' extinction |
| R6 | EEP Rayleigh, Mie and absorption layers unused | **CHANGED**: the ground light reads the profiles and radii. The drawn sky and haze do not. The Density tab is still hidden ([decided]: until after AT2) |
| R7 | The stored ozone default has the wrong sign | **FIXED in use, unchanged in storage** (by design): `fromLegacy` recognises it and substitutes Bruneton's profile |
| R8 | The object haze mixes eye-space and world-space vectors | **STILL TRUE, moved**: the glow term now compares an eye-space position with the world-space `sun_glow_norm`. The damping term was made consistent by L3 |
| R9 | The clouds are lit by their own sun term | **CHANGED**: the sun comes through AT1's T at the cloud layer and has its own handover. The ambient is still authored |
| R10 | **New, a design risk.** The plan put about 45 `pa_*` uniforms on the shared sky uniform list. That list is re-applied to every program on its first bind each frame | **CONFIRMED** [code]: a main-thread cost against a CPU-bound frame. The design moves them to one uniform buffer |

**What remains of Hillaire 2020** (§3):

- the passes that fill the tables (T, Ψ, sky irradiance E);
- the sky-view table;
- the aerial-perspective froxels and their apply;
- the far table for the clouds;
- the sky drawn in the lighting pass.

All are fragment passes, under the GL 4.6 floor. No GL compute.

**Cost [measured], the measured passes only, net of today's haze pass** (the sky view is rebuilt only when the translated sky or the
light directions change, §4.1):

| | Steady state | During a day-cycle blend | Share of a 60 fps frame |
|---|---|---|---|
| 1080p | **0.07-0.11 ms** | 0.08-0.12 ms | 0.4-0.7 % if GPU-bound |
| 1440p | 0.12-0.16 ms | 0.12-0.17 ms | |

- **This is not "everything AT2 adds".** It leaves out the forward and alpha apply with overdraw (68 programs, AT2-3) and the extra
  views (mirrors, hero probes, water reflection, the cube faces). They are unmeasured (§9) and gated in AT2-3. Read the figures as
  a lower bound.
- The range is measured two ways: back-to-back draws (the GPU at its clock) and one draw per query (idle clock between passes).
- In today's crowded-room frame, which is CPU-bound on the main thread (Rooftops, 31.9 ms), the GPU part overlaps. The main thread pays
  about 0.02-0.2 ms [inferred, §6.2], so 31.35 fps would become about 31.15-31.33.
- Other GPUs, scaled by FP32 throughput [inferred, not timed; the passes are ALU-bound marches]: the 5070 Ti is about 3.5× an RTX 3060
  and about 9× a GTX 1660. The measured passes then cost 0.25-0.6 ms (3060-class) and 0.6-1.5 ms (GTX 16xx-class). That is 60 → 57.9-59.1
  and 55.0-57.8 fps when GPU-bound, and 25 → 24.6-24.8 and 24.1-24.6 moving. The unmeasured forward and view costs come on top.

**Effort, calibrated on this repo's history:** about **3.5-5.5 days of agent time** for AT2-1 to AT2-5, with review rounds. It excludes
owner turnaround and the one-build-at-a-time queue; the owner checks in-world once, on the installed viewer after release. For comparison, AT0 took about 4 h, L3 + AT1 about a day with two fix rounds, and AT2-0 about 8 h. The AT2 survey's 12-16
agent-days was inflated about 3×.

**Recommendation:**

- Run AT2-1 next on devel. It has no visible change, and it fixes R2 and R10 first.
- Then AT2-2 to AT2-5, one build at a time, queued behind the open shadow work.
- Release waits for L6 ([decided]).

Five new owner decisions are in §8; the decisions already taken are restated, not re-asked.

---

## 2. The #439 findings on today's devel

### R1. The drawn PBR sky is ~8× a physical sky: STILL TRUE

- **[code]** The dome is still the Windlight formula per vertex. The steps that brighten it are all where they were:
  - skyF ×2 and clamp to 5: `class1/deferred/skyF.glsl:426-427`;
  - the lighting pass decodes it and scales it by `sky_hdr_scale` (`class3/deferred/softenLightF.glsl:194-204`);
  - `sky_hdr_scale` is 2√γ for PBR skies (`llsettingsvo.cpp:845`);
  - irradiance captures zero `ambient` (`llsettingsvo.cpp:836-839`).
- **[code]** L3 touched the dome only where the sun hands over to the moon: `skyV.glsl:120`, `:157`, `skyF.glsl:413-424`. By day these
  reduce to today's formula (`sun_moon_blend` = 0, the sun's glow factor 1; the handover doc's tests).
- **[measured]** #439's bench re-run on today's shaders (`bench_atmosphere.py --no-images --no-library --no-timing`):
  - PBR Default zenith 0.278 and horizon 1.676 (in 2026-11 it was 0.279 and 1.711), against 0.078 and 0.157 physical. Per pixel that is
    3.6× at the zenith and 10.7× at the horizon. The headline "~8×" is the display gain that matches today's sky to the physical one
    (7.74×, 2.95 EV in the bench output);
  - capture irradiance 0.115 today, physical 0.0962;
  - AT2-0 states 0.0965 for its own translation (core doc, "Sun disc and sky irradiance").
- **[code]** The sky target is still 8-bit: `deferredScreen` is `GL_RGBA` (`pipeline.cpp:1063`) unless `RenderEnableEmissiveBuffer`
  is set. Physical twilight cannot live there. This is why the sky moves into the lighting pass ([decided], AT2 survey decision 5).

### R2. New: the AT2-0 sky march skips the haze slab

- **[code]**
  - `LLScatter::sky_radiance` (`llatmosphericlight.h:1911-1934`) and its twin `pa_sky_radiance` (`atmosphereCommonF.glsl:517-539`) march
    `pa_march_seg(h0, d, 0, tmax, steps, 1.0, ...)`: uniform midpoint steps over the whole ray, to the top of the atmosphere or the ground.
  - Looking up through 60 km of air with 32 steps, the first midpoint is at 0.94 km, above the whole 0.25 km slab.
  - AT2-0 fixed exactly this for `atmosphereAP`, by splitting at the slab top. The core doc says the split march is the one march of the
    sky view (`atmosphereCommonF.glsl:24-25`), but `pa_sky_radiance` does not call it.
- **[measured]** `check_skyview_steps.cpp`:
  - setup: Default, Foggy and Sunset at their files' suns; observer at 0 and 30 m; elevations −2° to 90°; three azimuths;
  - reference: the slab-split march at 4096 steps. The uniform march at 4096 agrees with it to 0.03 %, so the reference holds.

  | Sky | Uniform 32 steps, worst / median | Slab-split 32 | Slab-split 48 | Slab-split 64 |
  |---|---|---|---|---|
  | Default (66°) | 5.2 / 2.9 % | 2.6 / 0.9 % | 1.2 / 0.3 % | 0.64 / 0.15 % |
  | Foggy (90°) | **98 / 33 %** | 0.9 / 0.03 % | 0.19 / 0.01 % | 0.11 / 0.00 % |
  | Sunset (4.3°) | **88 / 27 %** | 4.3 / 0.6 % | 2.1 / 0.16 % | 1.19 / 0.06 % |

- **[measured]** The library "no whiteout" check (`atmosphere_library_check.cpp:216`) samples `sky_radiance(d, 0, 24)`, the
  uniform march. Re-run with a converged slab-split sky (512 steps, a local edit, not committed):
  - still **0 whiteouts** outside 20° of the light;
  - the largest ratio is 1.248× (was 1.162×);
  - the set of aureole exceedances changes: "Phototools- Hospital Light 01" enters at 1.25× (inside the 2× aureole rule), and
    "[TOR] SCIFI - Arrakissed 2" leaves.

  So the no-whiteout result stands, but the expected list in `check_atmosphere_library.py` must be updated with the fix (decision D2).
- Nothing renders this march yet, so this is not a viewer bug and not a hotfix. It is the first item of AT2-1. The GPU vectors and
  the SHA lines must be regenerated with it.

### R3. Grey, homogeneous, altitude-blind haze: STILL TRUE

[code]

- `hazeF.glsl:87` blends with `atten.r` only.
- The extinction is `exp(−(bd + hd)·dm·distance_multiplier·d)`, homogeneous (`atmosphericsFuncs.glsl:120-133`).
- The "altitude" clamp is on the eye-space y (`atmosphericsFuncs.glsl:110`).
- The forward shaders haze through `atmosFragLighting` / `atmosLighting` (`class1/windlight/atmosphericsF.glsl:35-51`);
  `applySkyAndWaterFog` appears in 6 shader files, its definition included.
- `doAtmospherics` draws hazeF with `BF_ONE, BF_SOURCE_ALPHA` (`pipeline.cpp:11069-11171`).

### R4. Twilight and the switch at 0°: CHANGED

- **Fixed [code]:** the hard switch is gone.
  - Light, dome, glows and clouds hand over across ±0.3° of sun elevation (smoothstep, C1), with the shadow fades out to ±0.6°.
  - The clouds' band is centred on −1.24° (the horizon dip of a 1.5 km layer), so clouds stay sunlit after the ground's sunset.
  - Evidence: `handover-l3-at1.md`; `skyV.glsl:120` (`mix(sunlight_color, moonlight_color·0.7, sun_moon_blend)`), `:157`;
    `cloudsV.glsl:170`, `:186`.
- **Still true [code]:** there is no twilight sky. Below the band, the dome shows the keyframe's colours lit by the moon's colour. A
  sun at −2° to −18° lights nothing: no blue hour, no Belt of Venus, no Earth shadow.
- **Still true [code]:** the stars use a fixed airmass extinction, `exp(−0.3(airmass − 1))` with `airmass = 1/(d.z + 0.025)`
  (`skyF.glsl:318-319`). Their glare uses the sky's luminance (`:320-322`).

### R5. Plane-parallel airmass: CHANGED

- **Fixed [code]:** the ground light is AT1's curved-path transmittance from `LLAtmosphere`:
  - `LLEnvironment::updatePrimaryLight` (`llenvironment.cpp:1937-2043`) → `compute_primary_light`;
  - uploaded as `primary_light_color` (`llsettingsvo.cpp:890`);
  - noon is identical to today's, to 1e-11 (handover doc);
  - the clouds' sun is the T at the cloud layer (`cloudsV.glsl:186`, `cloud_light_transmittance`).
- **Still true [code]:** the dome's own sun attenuation is `exp(−light_atten / (max(0, dir.y) + lightnorm.y))` over a slab `max_y`
  thick (`skyV.glsl:125`, `:133-134`). It still uses `lightnorm`, on purpose (handover doc). The stars' airmass is above.

### R6. EEP layers unused: CHANGED

- **[code]** `updatePrimaryLight` now reads `getRayleighConfigs`, `getMieConfigs`, `getAbsorptionConfigs`, the bottom and top radii
  (`llenvironment.cpp:1978-1982`).
- `fromLegacy` takes them, and replaces an empty profile by the default with a clamp bit (`llatmosphericlight.h:530-571`).
- No shader reads them: `physicalAtmosphereF.glsl` and `atmosphereCommonF.glsl` are not registered with the shader manager
  (`grep` finds no C++ reference).
- **[code]** The Density tab is still commented out (`skins/default/xui/en/floater_edit_ext_day_cycle.xml:601-611`). [decided]: it
  stays hidden until after AT2.

### R7. The stored ozone default: FIXED in use

- **[code]** `llsettingssky.cpp:869` still writes `linear −1/25000, constant −2/3`. By design: "No stored default changes"
  (`llatmosphericlight.h:34-39`).
- `fromLegacy` recognises the stored pair (`is_stored_ozone_default`, `llatmosphericlight.h:366`) and uses Bruneton's
  `ozone_profile()` (`:346`, `:568`).
- **[measured]** The translated Default carries absorption layer 0 = `linear +6.667e-5, constant −0.667`, which is +1/15000
  (`bench_atmosphere_cpu --dump`).

### R8. The eye/world mix in the object haze: STILL TRUE, moved

- **[code]** `atmosphericsFuncs.glsl:141` forms `dot(rel_pos_norm, sun_glow_norm)`, and `:165` the moon's. `rel_pos_norm` is the
  eye-space position. `sun_glow_norm` is `getClampedSunNorm()`, world space (`llsettingsvo.cpp:881-882`).
- L3 made the damping term consistent: the eye-space `sun_dir` against the eye-space position (`:140`).
- **[measured]** #439 measured a ≈1 % effect. It was not re-measured: #439's camera-turn check does not set the new `sun_dir` uniform
  and now returns identical values for every yaw, so it no longer models the viewer.
- AT2-3 replaces this term, so no separate fix is proposed.

### R9. The clouds' light: CHANGED

- **[code]** The sun term is AT1's T at the cloud layer, with the clouds' own band (`cloudsV.glsl:158-170`, `:186`).
- The ambient is still the authored `ambient_color` lifted by `cloud_shadow` (`:174-175`).
- AT2-4 lights the flat layer from the E table, with L2b's beam and diffuse split (`cloud-shadows.md:154`).

### R10. New: how the atmosphere's uniforms would reach the shaders

- **[code]** `LLEnvironment::update` marks every program and variant dirty each frame (`llenvironment.cpp:1800-1824`). On its first
  bind, a program re-applies the whole SG_ANY list and its group's list (`llglslshader.cpp:1076-1079` →
  `LLEnvironment::updateShaderUniforms`, `llenvironment.cpp:1867-1878`).
- The AT2 survey attached `atmosphereCommonF.glsl` to all 68 atmospherics programs, with `pa_atm` and the `pa_*` scattering values
  (about 45 scalars and vectors, including the struct's arrays) as ordinary uniforms. Each one would be re-applied on every program's
  first bind of every frame, on the main thread, against a frame that is CPU-bound already.
- `LLShaderUniforms` holds only int, float, vec3 and vec4 settings by reserved index, so the struct's arrays would need about 45 new
  reserved names.
- The per-program uniform value cache (`mValue`; `uniform1f/3fv` skip `glUniform*` when the value is unchanged, `llglslshader.cpp:1340-1341`,
  `:1584-1586`, `:2098-2120`) turns each re-apply into a lookup, not a GL call: about 45 uniforms × about 100 programs × 30 ns, roughly
  0.1-0.15 ms per update pass [inferred]. That recurs for every cube face (§4.2). `LLShaderUniforms::apply` already has a Tracy zone
  (`:2100`); reading it in the next capture turns this into a number before AT2-1 is written.
- The cost was not measured in the viewer. It is the kind of main-thread work the multi-core baseline lists. The block is sound either
  way: programs that do not declare it pay nothing.
- **Design (§4.3):** one std140 uniform block, `Atmosphere`. It is filled once per frame with one `glBufferSubData`, bound at a fifth
  block index beside `ReflectionProbes` and the GLTF blocks, through the same `glUniformBlockBinding` loop
  (`llglslshader.cpp:1007-1024`).

### Other notes [code]

- `pa_sky_radiance`'s ground term calls the exact irradiance integral per light (`atmosphereCommonF.glsl:534`): 64 directions × 16
  steps per texel below the horizon. AT2-1 takes it from the E table, as the C++ does when `e_lut` is bound
  (`llatmosphericlight.h:1929`).
- `RenderAtmospherePhysicalLight` (old-airmass A/B) is still read (`llenvironment.cpp:1954`); [decided]: it is removed in AT2-5.
- L2b's diffuse channel (G) has no reader yet (`atmosphericsFuncs.glsl:59-63`). L4 is not started.

---

## 3. What AT2-0 already implements, and what remains of Hillaire 2020

| Hillaire 2020 part (plus Vulkanstorm's additions) | In the core (C++ `LLScatter`, GLSL twin) | Still to build |
|---|---|---|
| Transmittance T(h, μ), 256×64 | parameterisation (Bruneton and Neyret's u; v = √(ρ/H)); texel = exact `pa_transmittance` with cuts; lookup `pa_T_lut`; light T with disc and excess (`atmosphereCommonF.glsl:155-240`) | the pass (AT2-1); upload the C++ cut altitudes instead of the per-texel search (`physicalAtmosphereF.glsl:29-31`) |
| Multiple scattering Ψ(h, μ_s), 32×32 | `pa_ms_texel` (8×8 directions, 20 quadratic steps, f_ms ≤ 0.6), the reference single-pass form | the split pass (one fragment per texel × direction) and the reduce (AT2-1) |
| Sky irradiance E(h, μ_s), 64×16 (for clouds and probes; not in Hillaire) | `pa_sky_irradiance_texel` | the split pass and the reduce (AT2-1) |
| Sky view, 192×108 | `pa_sky_radiance` per texel, **uniform march (R2)**; no table parameterisation | the slab-split march; the latitude/longitude parameterisation; the pass; the lookup (AT2-1, AT2-2) |
| Aerial perspective froxels, 32³ | `atmosphereAP`: the slab-split, weighted march; R-ACC 1 % at 12 steps over the core doc's domain | the froxel atlas pass per view; `atmosphereApply`; hazeF replaced; the forward shaders (AT2-3) |
| Far AP for the clouds, 32³ (not in Hillaire) | the same `atmosphereAP` | the pass and `atmosphereAPFar` (AT2-4, built only while L7 asks) |
| Sky in the lighting pass, sun disc, stars | — | AT2-2 |
| Translation from the legacy fields, bounds, clamp bits | `LLScatter::fromLegacy`, tested (core doc) | — |

---

## 4. Design of the remainder

The AT2 survey's design stands where it is not changed here; the decisions it took are [decided]. Changes are marked **changed**.

### 4.1 Passes (fragment only, GL 4.6, no compute)

| Pass | Size, format | Built from | When |
|---|---|---|---|
| T | 256×64 RGBA16F | `pa_trans_texel`, with the C++ cut altitudes in the block | on a change of the translated atmosphere |
| Ψ | 32×32 (+ 256×256 ×2 partials) | split + reduce | with T |
| E | 64×16 (+ 512×128 partial) | split + reduce | with T |
| Sky view | 192×108 ×1-3 RGBA16F | **changed:** `atmosphereAP` to the end of the ray (slab-split), **64 steps** (D1), plus the ground term from E; observer fixed at h = 0 ([decided]) | **changed:** only when the quantised hash changes (below) |
| Main-view froxels | 2D atlas 256×128 ×2 (32³) | `atmosphereAP`, 12 steps, the camera's real height ([decided]) | every frame |
| Probe-face, mirror and water-reflection froxels | atlas 64×64 ×2 (16³) | same | per view render |
| Far AP | atlas 256×128 ×2 | `atmosphereAP`, 16 steps (48 if D1's rule is applied to the clouds' domain) | only while L7 asks |

**Changed: the rebuild cadence.**

- The AT2 survey rotated T, Ψ and E one per frame. Measured split costs are 0.033 + 0.019 + 0.015 ms batched, 0.071 + 0.034 + 0.028
  unbatched.
- So T, Ψ, E **and the sky view** are rebuilt together, and only when the change test fires. The sky view's observer is fixed at
  h = 0 ([decided]), so it depends on the translated atmosphere and the light directions, never on the camera. It is not a per-frame
  pass. Only the froxels and the apply run every frame.
- The change test is a hash of the translated values and the light directions, **quantised** (for example to 1e-3). A day-cycle blend
  then rebuilds at 1-4 Hz instead of every 4 frames: the sun moves about 4.4e-4 rad/s on a 4 h day, so a 1e-3 step falls about every
  2 s. The sets are never half-updated.
- A rebuild costs 0.067-0.133 ms for T, Ψ and E plus 0.024-0.051 ms for the sky view. At 4 Hz on 60 fps that is about 0.008-0.016 ms a
  frame amortised; at steady state it is zero. An instant EEP switch rebuilds in the next frame.
- The hash is 0.3 µs, plus `fromLegacy`'s 3.5-4.1 µs on frames where the sky changed [measured; the bench hashes the input struct
  `LegacySky`, padding included, a stand-in for the real quantised hash].

**Changed: no tiers** (D3).

- The survey's three tiers are dropped:
  - the GL3 list is gone (#537);
  - the default quality costs at most 0.17 ms (measured passes) on the 5070 Ti.
- One quality, and no `RenderAtmosphereTier` setting. The step counts (sky view 64, froxels 12) stay debug settings, a structural lever
  for a slow GPU and for the AT2-3 gate, not a Preferences entry and not a tier.

**Why fragment, not compute.**

- The owner measured GL compute hurting fps while moving.
- Every pass here is small (≤ 65 k fragments) and fits a fragment draw.
- The Ψ and E split passes give the parallelism that compute would give: 0.075 → 0.019 ms and 0.059 → 0.015 ms batched, against the
  single-pass form [measured].
- The froxels go in a 2D atlas in one draw: a layered draw would need `gl_Layer` from a geometry shader, or the non-core
  `ARB_shader_viewport_layer_array`.
- The Vulkan line may use compute for the same passes.

### 4.2 Frame, threads, failure

- **Order [code].** In `display()`:
  1. the hero-probe update and render (`mHeroProbeManager.update()/renderProbes()`, `llviewerdisplay.cpp:853-854`), which runs
     **before** Env Update;
  2. Env Update (`llviewerdisplay.cpp:874-876`, which runs `updatePrimaryLight` and steps the dark gate);
  3. `generateSunShadow` (`:954`);
  4. `renderGeomDeferred` (`:1172`);
  5. `renderDeferredLighting` (`:1198`).
- **Paths outside that order [code].** The reflection-probe update runs after `display()` (`mReflectionMapManager.update()`,
  `llappviewer.cpp:1803`). `display_cube_face()` runs once per face (`llviewerdisplay.cpp:1319-1421`) and calls `LLEnvironment::update`
  again (`:1359`), which re-marks every program dirty (`llenvironment.cpp:1805-1823`) and re-applies SG_ANY on each first bind, per
  face. `updateSettingsUniforms` is guarded by `!gCubeSnapshot` (`llenvironment.cpp:2049`).
- **Where the passes go (changed).** The table, sky-view and far-AP passes run in `LLPipeline::updateAtmosphere()`, reachable from any
  view render through a lazy `ensureTables()`: the first caller (a mirror or hero probe on frame 1, or the main view) builds them
  synchronously, so no view draws a sky from tables not yet built. The call is guarded by `!gCubeSnapshot` like
  `updateSettingsUniforms`, so a cube face does not rebuild them. The main frame calls it right after Env Update. The hero probes at
  `:853` run before it, so they read **last frame's** block and tables (one frame of lag, invisible on a sky that changes at 1-4 Hz);
  moving the update before `:853` is the alternative if a gate shows a pop. The froxels run at the start of each view's
  post-deferred pass. `updateAtmosphere()` sits outside the marked `<ShadowLegacy>` regions of `pipeline.cpp`.
- **R10's per-frame cost, restated:** per main frame plus per cube face (and per mirror or hero view), not per main frame alone.
- **Threads.** Everything is on the main (GL) thread. No worker touches it.
- **GL resources.**
  - allocated lazily on the main thread;
  - released in `LLPipeline::releaseGLBuffers` / `releaseLUTBuffers`, never in a `shutdown()` that worker contexts run (#403);
  - invalidated on `restoreGL`, a shader reload and a sky switch.
- **Failure.**
  - A pass whose shader fails to compile logs an error, and the sky draws from the tables' last content.
  - The first caller builds them synchronously (`ensureTables()`).
  - There is no old-model path ([decided], no fallback). A shader failure is a build defect, caught by the shader-compile CI step.

### 4.3 Uniform transport (changed, R10)

- The `Atmosphere` std140 block holds `pa_atm`, the scattering values, the lights, the cut altitudes and the camera height.
- It is filled once per main frame, and per probe or mirror view only for the camera height, in a small second block or as a push of
  one float.
- Programs that do not declare the block pay nothing.
- `atmosphereCommonF.glsl` and `physicalAtmosphereF.glsl` read the block instead of `uniform` declarations. The GPU vectors' harness
  sets the block the same way. The test's SHA lines change.

### 4.4 Sky, haze, probes, stars

As the AT2 survey, all [decided]:

- the sky radiance is computed in float in the lighting pass, scaled by a coverage alpha that the cloud, sun and moon draws leave;
- the aerial perspective is applied with a dual-source blend;
- the forward shaders use `atmosphereApply` (froxel taps);
- the probes capture the physical sky;
- the stars' extinction is T relative to the zenith, and their timing is physical;
- the moonlit sky gets the compensated gain plus a scotopic desaturation;
- under the cloud layer, the sun's in-scatter is multiplied by the L2 map's mean.

**New: HDR off (D5).** `RenderHDREnabled` (`settings.xml:24178`, read at `pipeline.cpp:451`; default true) is still a persisted setting.
With it off, `addDeferredAttachments` (`pipeline.cpp:998`) falls to 8-bit `norm` and `emissive`. The float sky and the dual-source RGBA16F
apply assume HDR. Proposal for AT2-2, under the GL 4.6 no-fallback rule: with HDR off the sky is accepted clamped to 8 bits (a visibly
poorer twilight, no second code path), and the AT2-2 gate checks that it draws without GL errors. The alternative is to refuse HDR off.

---

## 5. How it feeds the sun, the moon and the shadows

| Consumer | From the atmosphere | Change in AT2 |
|---|---|---|
| `sunlit` (every lit shader) | AT1's CPU T at the ground observer, through `primary_light_color` | **none**. The GPU T table is held to the CPU T, within 0.04 % above 2° (core doc); the sun disc's colour must equal the ground light's (AT2-2 gate) |
| **#540's dark gate** | `LLDirectionalShadowGate::decisionLight` on `primary_light_color` × the sky sunlight scale, stepped in `updatePrimaryLight` (`llenvironment.cpp:2029-2041`) **before** `generateSunShadow` reads `dark_skip` (`pipeline.cpp:12954-12960`) | **none, by rule**. AT2 must keep the ground light a CPU value decided before the shadow pass, and must never route it through a GPU table. The gate leaves the cloud map out, and its decision light is "the sky's and the light's alone, never the camera" (`lldirectionalshadowgate.h:12-18`). The map is world-anchored around the camera, so a map term would make the gate depend on where the camera is [inferred]. AT2 keeps the map out |
| Shadow strength, the handover | AT1's band and fades | none. The twilight sky lights the sky and the probes, not the ground's direct light |
| L2 cloud shadows on the ground | the map's R (beam) multiplies `sunlit` (`atmosphericsFuncs.glsl:229`) | none |
| Haze under an overcast layer | — | × the map's mean, R top mip (one tap, already bound in SG_SKY) [decided] |
| Flat clouds (AT2-4) | AT1's `cloud_T`; L2b's G for the lit underside and 1 − R − G for the top (`cloud-shadows.md:154`) | the sky light becomes E(h_cloud)/π from the table; far AP at the dome distance |
| L7 volumetric clouds | T, Ψ and E bound; far AP (R-AP), AT1's `cloud_T` | built only while L7 asks. L7 is at L7-0 (the noise file) |
| Probe irradiance, i.e. the **shade** in PBR skies | the probes capture the physical sky | Default capture irradiance 0.115 → 0.0965 (−16 %). On the clear PBR Default, L1's sun-to-shade ratio of 3.8 would become about **4.3** [inferred: L1's grey panel, (2.78 / 0.839) + 1, the shade scaled by 0.0965/0.115]. Deeper shade, within the textbook 3-8 : 1. Classic skies' shade is the authored `amblit` and does not change (decision D4) |
| Legacy shadow method | — | the shadow passes, maps and lookups are not touched, so they stay byte-identical. The image around the shadows changes with the sky (the shade above) |

Softness is not coupled. Sun and moon softness stays the EEP sun size × `RenderShadowLightSize` 0.23. The atmosphere does not widen
it, although forward scattering in haze would. Not proposed.

---

## 6. Costs

### 6.1 GPU [measured]

Method:

- `bench_atmosphere_passes.py`, on the viewer's AT2-0 GLSL with the viewer's own translation of each sky;
- `GL_TIME_ELAPSED`, the median of 60;
- the first number is 8 draws back to back, divided by 8 (the GPU at its clock). The second is one draw per query (the GPU idles
  between passes, the conservative figure);
- results for Default with sun and moon. Foggy and Sunset are within ±15 %;
- the apply is one full-screen draw on a pseudo-random depth buffer, with no overdraw and no blending over geometry.

| Pass | 1080p | 1440p | When |
|---|---|---|---|
| T 256×64 (cuts searched per texel) | 0.033 / 0.071 | = | on change |
| Ψ split + reduce | 0.015 + 0.004 / 0.024 + 0.010 | = | on change |
| — Ψ single pass (the reference form) | 0.075 / **0.62** | = | rejected |
| E split + reduce | 0.014 + 0.001 / 0.023 + 0.005 | = | on change |
| — E single pass | 0.059 / **0.42** | = | rejected |
| Sky view, uniform march, 32 steps (R2: wrong in haze) | 0.010 / 0.021 | = | rejected |
| **Sky view, slab-split, 64 steps** | **0.024 / 0.051** | = | on a change |
| — slab-split, 32 steps | 0.013 / 0.027 | = | |
| **Main froxels 32³, 12 steps** | **0.010 / 0.015** | = | every frame |
| — 24 steps | 0.018 / 0.026 | = | |
| Probe-face froxels 16³ | 0.003 / 0.010 | = | per probe face |
| Far AP, 16 / 48 steps | 0.015 / 0.023; 0.042 / 0.062 | = | while L7 asks |
| **AP apply** (4 taps, dual-source blend into RGBA16F) | **0.048 / 0.053** | **0.086 / 0.094** | every frame |
| Today's haze pass (stand-in: `calcAtmosphericVarsLinear` + blend, depth tap only) | 0.018 / 0.020 | 0.032 / 0.033 | replaced |
| **Sky radiance, every pixel sky** (sky-view tap, aureole, disc) | **0.023 / 0.048** | **0.041 / 0.063** | sky pixels |

Per frame, the default quality, measured passes only (the sky view and the tables run on a change, §4.1):

| Part | 1080p | 1440p |
|---|---|---|
| Main froxels | 0.010-0.015 | 0.010-0.015 |
| Apply, net of today's haze | 0.030-0.033 | 0.055-0.061 |
| Sky radiance (upper bound: all sky) | 0.023-0.048 | 0.041-0.063 |
| One probe face (froxels + apply at 512², scaled) | 0.009-0.016 [inferred scaling] | same |
| **Steady state** | **0.07-0.11 ms** | **0.12-0.16 ms** |
| + a day-cycle blend (T, Ψ, E and the sky view at 4 Hz, amortised) | +0.008-0.016 | same |
| **During a blend** | **0.08-0.12 ms** | **0.12-0.17 ms** |
| + L7 on (far AP, 16 steps) | +0.015-0.023 | same |

(The first draft rebuilt the sky view every frame and the tables every 4th frame, for 0.11-0.20 ms and 0.16-0.24 ms. That stays a safe
envelope.)

**Not in these totals** (unmeasured, AT2-3 gate): the forward and alpha apply with overdraw (68 programs, including alpha, water and
particles), and the extra views per frame (mirrors, hero probes, water reflection, `display_cube_face()` once per face). The probe row
counts one face, scaled.

**Against the fps floor** (60 fps standing = 16.7 ms, 25 fps moving = 40 ms), the measured passes only:

- **GPU-bound frame, 5070 Ti:** 60 → 59.4-59.7 fps (1080p) and 59.3-59.4 fps (1440p); 25 → 24.9 fps.
- **Other GPUs [inferred, FP32 ratio, not timed]:** the GL 4.6 floor admits GTX 10xx/16xx, RX 5xx and Intel Gen9+ parts. The 5070 Ti is
  about 3.5× an RTX 3060 and about 9× a GTX 1660. The passes are ALU-bound marches, so cost scales with that ratio.

  | Reference | Scale | Measured passes (1080p to 1440p) | 60 fps floor | 25 fps moving |
  |---|---|---|---|---|
  | RTX 5070 Ti (measured) | 1× | 0.07-0.17 ms | 59.3-59.7 | 24.9 |
  | RTX 3060-class | 3.5× | 0.25-0.6 ms | 57.9-59.1 | 24.6-24.8 |
  | GTX 1660-class | 9× | 0.6-1.5 ms | 55.0-57.8 | 24.1-24.6 |

  On a GTX 16xx-class card the floor is met only at about 55 fps, before the unmeasured forward and view costs. The debug-setting step
  counts (sky view 64 → 32, froxels 12) are the lever for such a card (D3).
- **Today's crowded room is CPU-bound.** Rooftops, about 50 avatars, Ultra: 31.9 ms main thread, `Swap` stalls in 1 frame in 7
  (`reports/rooftops-ultra-shadow-split-2026-11-16.md`).
  - The GPU passes overlap the main thread there. They show only in the stall frames.
  - The atmosphere does not move that floor. The main thread does (§6.2).
- **Compared with earlier estimates:**
  - #439: ≈0.21 ms, 32 slice draws at GL 4.0;
  - the AT2 survey: 0.06-0.14 ms;
  - this refresh is higher because it includes the slab-split 64-step sky view, a probe face, the amortised rebuild and the measured
    apply, but it still excludes the forward apply and the extra views.

### 6.2 Main-thread CPU

| Item | Cost | Note |
|---|---|---|
| `compute_primary_light` (AT1), every frame | 14-15 µs [measured, g++ -O2; the viewer is MSVC + LTO] | already paid today. Excludes the `profile_from_llsd` conversions and the LLSD getters in `updatePrimaryLight` (`llenvironment.cpp:1976-1982`); that zone is not captured yet |
| `LLScatter::fromLegacy`, frames where the sky changed | 3.5-4.1 µs [measured] | AT2-1 |
| Hash of the translated atmosphere | 0.3 µs [measured, an input-struct stand-in] | AT2-1 |
| One uniform-block upload; 3-6 more draws and binds (FBO bind, viewport, texture binds, draw: about 10-30 µs each) | ~0.02-0.2 ms [inferred; wide until measured on the first AT2-1 build]; the hero, mirror and cube views add more | AT2-1 to AT2-3 |
| `pa_*` as SG_ANY uniforms on every bound program (rejected, R10) | not measured; about 0.1-0.15 ms per update pass with the value cache, per cube face too [inferred] | avoided by the block |
| **Total added** | **≈ 0.02-0.2 ms** [inferred]: 31.35 fps → about 31.15-31.33 at Rooftops; 60 fps → 59.3-59.9 if CPU-bound | |

### 6.3 Memory

About 2.8 MB of VRAM (AT2 survey): the tables, the partials and two froxel atlases. No new CPU memory beyond `LLScatter`.

---

## 7. Staged plan (each a `codex/` PR from `vkstorm-devel`, RelWithDebInfo, staged, no installer)

Common gates for every stage:

- GPU time by `GL_TIME_ELAPSED` timer queries (`bench_atmosphere_passes.py` has the code), on the owner's Rooftops and one quiet scene,
  reported against the fps floor. Tracy GPU zones are not usable for this: the Rooftops exports have `gpu_frames=0`, `tracy-csvexport`
  does not export GPU zones, and the stage-1 zones are CPU zones. A working GPU-zone export is a separate item, not costed here;
- the stage's own cost and the running light-and-shadow total, in the PR (AGENTS.md);
- `check_legacy_shadows.py` unchanged **and** the Legacy shadow-map shader hashes unchanged, plus one Legacy-method capture of the shadow
  maps before and after. The script alone only diffs the marked `<ShadowLegacy>` regions of `pipeline.cpp`, and the atmosphere stages
  edit `softenLightF`, `hazeF`, the forward shaders and `display()`, which Legacy renders through;
- no new Preferences entries; debug settings only.

| Stage | Scope | Gate | Check (LEAP / scratch profile) | Risk | Effort, agent time (calibrated) |
|---|---|---|---|---|---|
| **AT2-1** Tables, no visible change | **R2:** `sky_radiance` and `pa_sky_radiance` use the slab-split march (64 steps) and E for the ground; vectors, SHA, library aureole list (D2). **R10:** the `Atmosphere` uniform block. The cut-altitude upload; T, Ψ (split), E (split) and sky-view passes; rebuild cadence; `llatmosphereluts.{h,cpp}`; `updateAtmosphere()` hook; release and restore | GPU texels against C++ (T 1e-3, Ψ and E 1 %, sky view 1 %); library check OK; no GL errors in a 1 h soak; GPU timer queries within ±50 % of §6.1; `check_atmosphere_library.py --acc` over the froxel domain up to the draw distance, errors reported, and the froxel step count chosen from it (core doc: 130 skies exceed 1 % at 12 steps, worst 12 / 80 / 36 % at 2 / 10 / 40 km); `ensureTables()` reached from a mirror on frame 1 | LEAP or scratch-profile check | low | 0.5-1 d |
| **AT2-2** Physical sky, probes, stars | sky in the lighting pass with the coverage alpha; separate alpha blend for clouds, sun and moon; sun disc through T; stars' extinction and glare; probe captures; below-horizon ground | sky ≤ 0.05 ms at 1080p; the disc's colour = AT1's within 1 % above 2°; no pop in a sped-up day cycle (light_check scenario); classic and PBR; HDR off draws without GL errors (§4.4, D5) | LEAP or scratch-profile checks: noon, sunset into −6°, moonlit night, skybox at 3000 m, a classic and a PBR sky | medium: blend states, the moon's depth write (SL-14113), a darker sky until L6 | 1-1.5 d |
| **AT2-3** Aerial perspective | froxel pass per view; `atmosphereApplyF` replaces hazeF; `atmosphereApply` in the forward callers of `applySkyAndWaterFog`, `atmosFragLighting` and `atmosLighting`; real camera height; L2 mean under the layer | net ≤ +0.04 ms at 1080p for the opaque apply; **plus the apply on a 4×-overdraw blended pass and the per-view cost (mirror, hero, water, cube faces), measured and reported before the stage is called done**; froxel accuracy out to the draw distance (as AT2-1); no swimming in a 360° turn; stochastic transparency's composite order unchanged | LEAP or scratch-profile checks: hazy valley from a skybox; alpha and particles over haze; water edge | medium-high: 68 programs, forward paths, wider than L3 + AT1 | 1-1.5 d, likely the upper end |
| **AT2-4** Flat clouds through AT2; far AP | cloudsV/F from E, `cloud_T` and the far AP; far-AP pass on request; `check_atmosphere_far.py` | far table ≤ 1 % on the clouds' domain; the flat layer's mean luminance within ±10 % of a physically lit reference on 4 skies | clouds at noon, at sunset, overcast | low-medium | 0.5-1 d |
| **AT2-5** Removal and cleanup | remove the `RenderAtmospherePhysicalLight` old branch ([decided]); delete the dead dome and haze formulas (keep `legacy_transmittance` as the test oracle); beacon fog colour; doc `doc/light/atmosphere.md` | whole-stage cost against the floor | 10 popular skies before and after | low | 0.5 d |

- **Total:** about 3.5-5.5 days of **agent time**, including the review rounds. It excludes owner turnaround and the one-build-at-a-time
  queue. The owner's in-world check happens once, on the installed viewer after release (the recorded rule); the stage checks above are
  LEAP or scratch-profile checks. History supports about a day per renderer stage as agent time (AT0 4 h, L3 + AT1 23 h, AT2-0 7.5 h).
- The status document's "size per #439" line (`reports/light-l-stages-status-2026-11-16.md`, not on this branch) needs updating when #439
  is closed.
- **Order:** AT2-1 → 2 → 3 → 4 → 5. Then L4-L6. L7 after AT2 ([decided]).
- AT2-1 can run now. It touches no shader that renders today, and no shadow file.
- **Release integration waits for L6** ([decided]). L6 is not started.

---

## 8. Decisions for the owner (new ones only; the recommended option first)

1. **D1. How the sky view is marched (R2).**
   - (a) **Recommended:** the slab-split march at 64 steps. It is within 1.2 % on Default, Foggy and Sunset, and costs 0.024-0.051 ms,
     +0.014-0.030 over the uniform 32.
   - (b) The slab-split march at 32 steps: 0.013-0.027 ms, worst 4.3 % on Sunset and 2.6 % on Default.
   - (c) Keep the uniform march: up to 98 % low in fog. Not viable.
2. **D2. The library's aureole list with the corrected sky.** "Phototools- Hospital Light 01" (a light rig) enters at 1.25× of the
   white card, 10-20° from the light. "[TOR] SCIFI - Arrakissed 2" leaves.
   - (a) **Recommended:** accept the new list under the 2026-11-11 aureole rule. There are no whiteouts.
   - (b) Tighten a bound until Hospital Light 01 is under the card.
3. **D3. Quality tiers.**
   - (a) **Recommended:** one quality for every GPU, and no `RenderAtmosphereTier`. The cost is ≤ 0.17 ms (measured passes) on the 5070 Ti,
     about 0.25-0.6 ms on an RTX 3060-class and 0.6-1.5 ms on a GTX 1660-class card [inferred, FP32 ratio]. The step counts stay
     debug settings. The GL3 tier is gone with #537.
   - (b) Keep the AT2 survey's three tiers.
4. **D4. Deeper shade under PBR skies** once the probes capture the physical sky: sun-to-shade about 3.8 → 4.3 on the clear PBR Default
   [inferred].
   - (a) **Recommended:** accept it as physical. L4 and L5 handle the shade's colour and the overcast veil.
   - (b) Hold the probe sky's irradiance at today's level until L4/L5. That is a compensation factor, so not physical.
5. **D5. HDR off after AT2-2** (§4.4).
   - (a) **Recommended:** accept a clamped 8-bit sky there; no second code path.
   - (b) Refuse `RenderHDREnabled` off once AT2-2 lands (GL 4.6 no-fallback rule).

Restated, not re-asked ([decided]): every sky physical, classic bounded, no fallback; the drawn sky physical, with L6 compensating;
slab 0.25 km; sky observer fixed, haze at the real height; the moon compensated plus scotopic desaturation; stars on physical timing;
the sky in the lighting pass; the far table for the clouds; the L2 mean under the layer; the Density tab hidden; AT2 on devel, release
after L6; `RenderAtmospherePhysicalLight` removed in AT2-5.

---

## 9. Risks, and what was not verified

**Not measured:**

- in-world frame time, and GPU zones in the viewer (Rooftops has none);
- any GPU but the 5070 Ti; the 3060-class and GTX 1660-class rows are FP32 ratios, not timings;
- **the forward and alpha apply with overdraw** (68 programs, blended passes, particles, water);
- **the extra views per frame**: mirrors, hero probes, water reflection and each `display_cube_face()` (one probe face, scaled, is all
  the doc counts);
- the main-thread cost of the uniform block and the extra draws, and of the real `updatePrimaryLight` zone (LLSD getters, profile
  conversions);
- the cost of the R10 alternative, and of `LLShaderUniforms::apply` (an existing Tracy zone to read);
- the probe-face cost, scaled from the full-screen apply;
- the shade change of D4, inferred from L1's numbers and the capture irradiance;
- the 1440p apply under clock changes;
- the froxel accuracy out to the draw distance (a gate in AT2-1 and AT2-3).

**Unverified (auditor, PLAUSIBLE, not checked):**

- whether Hillaire's 2016 slides contain froxels and sky irradiance: the PDF was over the fetch limit. §10 therefore also cites the 2015
  Frostbite volumetrics talk for froxels;
- the AT2 survey's own numbers (12-16 agent-days, 0.06-0.14 ms, 68 programs, 2.8 MB): the survey is not in the repo;
- the header comment in `llatmosphericlight.h:38-39` repeats the 2008 attribution for the ozone profile (§10). It is viewer code and is
  corrected in AT2-1, not here.

**Bench limits:**

- Today's haze stand-in reads depth only (hazeF also reads the normal), so the net apply cost is slightly pessimistic.
- The froxel and sky-view parameterisations are bench code, not the final ones.
- The sky-view accuracy is checked on 3 skies and 66 directions per sky, not on the library. AT2-1's gate runs the library with the split march.

**Not re-verified since #439:**

- the library ΔE (median 30 over 768 skies) and the renders; they are carried unchanged;
- the classic skies' look under the physical sky (never benched);
- the 47 out-of-range skies, now clamped;
- the eye/world ≈1 % of R8.

**Risks:**

- R2's fix changes the AT2-0 vectors and the aureole list, so the library check and its expected set must change in the same PR.
- The darker drawn sky waits for L6, which is not started, so devel shows it before release does.
- The coverage alpha touches the sun, moon and cloud blend states (AT2-2).
- Thick fog: Hillaire's multiple scattering is weak there. With f_ms ≤ 0.6 a fog can come out grey rather than white (core doc).
- The 130 library skies that miss 1 % at 12 steps on the clouds' domain (core doc) matter for the far table (16 → 48 steps:
  +0.027-0.039 ms, only with L7). The main froxels reach only the far clip (256 m by default), where the error is small [inferred].

---

## 10. Sources (references only; no NVIDIA or Epic sample code is used)

- S. Hillaire, *A Scalable and Production Ready Sky and Atmosphere Rendering Technique*, EGSR 2020: transmittance, multiple
  scattering, sky view and aerial perspective LUTs.
- S. Hillaire, *Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite*, SIGGRAPH 2016 course, Physically Based Shading:
  sky, atmosphere, clouds, aerial perspective and cloud interaction (froxels and sky irradiance in it: unverified, §9).
- S. Hillaire, *Physically-based and Unified Volumetric Rendering in Frostbite*, SIGGRAPH 2015 course: froxel volumes.
- E. Bruneton, F. Neyret, *Precomputed Atmospheric Scattering*, EGSR 2008: parameterisation, Earth coefficients.
- E. Bruneton, *A Qualitative and Quantitative Evaluation of 8 Clear Sky Models*, IEEE TVCG 2017, and its reference implementation
  (ebruneton.github.io/precomputed_atmospheric_scattering): the ozone layer and its profile (the 2008 paper has none); also Hillaire 2020.
- B. Wronski, *Volumetric Fog: Unified Compute Shader-Based Solution to Atmospheric Scattering*, SIGGRAPH 2014 Advances: froxel volumes.
  Here they are built as a fragment atlas.
- H. W. Jensen, F. Durand, J. Dorsey, M. Stark, P. Shirley, S. Premože, *A Physically-Based Night Sky Model*, SIGGRAPH 2001: star
  extinction, moonlight, scotopic vision.
- A. J. Preetham, P. Shirley, B. Smits, SIGGRAPH 1999; L. Hosek, A. Wilkie, SIGGRAPH 2012: plausibility checks only.
- F. Kasten, A. T. Young, *Applied Optics* 28, 1989: superseded by AT1's T.
- Study #439 (`codex/atmosphere-study` 5f0d405d58), the AT2 survey (2026-11-11, owner decisions recorded), AT2-0 (#462).
