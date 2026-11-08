<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Light reference numbers (stage L1 of the light study)

What today's OpenGL viewer does with the sun and the sky on a PBR panel, measured before the light
stages L2-L6 change it (`doc/light/light-model-study.md` §2.2 D6 and §5 L1). Nothing in the
renderer changes in L1.

## What is in L1

- `indra/newview/llatmosphericlight.h`: renderer-free C++ for the `sunlit` and `amblit` terms of
  `class1/windlight/atmosphericsFuncs.glsl` (`calcAtmosphericVars`, `ambientLighting`, and the
  shaping `calcAtmosphericVarsLinear` adds). The viewer still runs the GLSL. The header is the
  reference that the later stages change first and that the Vulkan renderer will share.
- `indra/newview/tests/llatmosphericlight_test.cpp`, run in CI by `.github/actions/ci-checks`
  beside `llshadowcascadefit_test.cpp`. Its 96 vectors (`llatmosphericlight_vectors.inc`) were
  produced by the viewer's GLSL on a GPU (`scripts/light/gen_atmosphere_vectors.py`; `--check`
  re-evaluates the GLSL and compares), so CI needs no GPU. C++ and GLSL agree to 1e-4 relative
  (the GLSL is float). The test also pins the physical shape of the terms (a lower sun is dimmer
  and redder, the moon replaces the sun when the sun is down, clouds leave `sunlit` alone and lift
  the ambient, the ambient is grey outside classic mode).
- `scripts/light/measure_sun_shade.py`: the measurement below, with the viewer's own shader code in
  a hidden OpenGL context (no viewer launch).

## Method

The shaders are the viewer's, unmodified, compiled in a hidden context:

- `calcAtmosphericVarsLinear` (atmosphericsFuncs.glsl) gives `sunlit` and `amblit`;
- the PBR branch of `softenLightF.glsl` is repeated line for line around `pbrBaseLight`,
  `pbrPunctual` and `pbrIbl` of `deferredUtil.glsl` (`sunlit *= 1.35` and `final_scale` 1.1 in
  classic mode);
- the panel is horizontal, metallic 0, roughness 1, ao 1, seen from 35 degrees above its plane. The
  white panel has albedo 0.9 and the grey one 0.18 (linear). Half the panel is in sun (`scol` 1)
  and half in shadow (`scol` 0), no other light;
- the sky values are the sky files' own (Windlight skies translated as `LLSettingsSky` does), with
  `RenderSkySunlightScale`, `RenderHDRSkySunlightScale` and `RenderSkyAmbientScale` at their
  settings.xml value of 1.0.

What lights the shade depends on the sky (`reflectionProbeF.glsl`, `doProbeSample`):

- A **classic** sky (every sky file shipped with the viewer has no `reflection_probe_ambiance`)
  takes the sky's `amblit` and no probe at all.
- A **PBR** sky (`reflection_probe_ambiance` 1) takes the sky probe's irradiance,
  `mix(amblit, E, min(ambiance, 1))`. E is computed here the way the irradiance capture sees the
  sky: the sky colour (skyV/skyF) with the capture's zero ambient and `blue_horizon`/`blue_density`
  reduced to their HSL lightness (`RenderDesaturateIrradiance` 1), times `sky_hdr_scale`,
  cosine-convolved over the upper hemisphere. **Only the sky is in E.** The real probe also holds
  the ground and the scene, and the sun disc; they are not modelled, so a real PBR scene's shade
  is somewhat brighter and warmer than the PBR rows. The "fallback" column shows the result
  where no probe covers the surface (E replaced by `amblit`).

The three skies:

1. **Default (classic):** `Default.xml`, the viewer's default sky file (sun at 66 degrees).
2. **Clear PBR sky:** `Default.xml` with `cloud_shadow` 0 and `reflection_probe_ambiance` 1. No
   shipped sky is a PBR sky, so this is the default sky made clear and PBR.
3. **Overcast creator sky:** "Nacon's Natural Noon" (`cloud_shadow` 0.71, sun at zenith in the
   file), a shipped creator sky that is meant as overcast. Row **3b** is the same sky as a PBR sky
   (ambiance 1), which is what a creator of a PBR overcast sky would get today.

Each sky is shown at its own sun elevation, and at 60 and 30 degrees.

## Result

Linear HDR values written by the lighting pass, before exposure and tone mapping. "Lit / shade" is
the luminance of the sunlit half over the shaded half. "Sun only / shade" is the sun's own
contribution over the shade, (lit - shade) / shade. B/R is the blue-to-red ratio (1.00 is neutral).

| Sky | Sun elevation (deg) | Panel | Lit R G B | Shade R G B | Lit / shade | Sun only / shade | Shade B/R | Lit B/R | PBR fallback lit / shade |
|---|---|---|---|---|---|---|---|---|---|
| 1 Default (classic) | 66 (sky file) | white 0.9 | 1.057 1.054 1.118 | 0.141 0.141 0.141 | 7.49 | 6.49 | 1.00 | 1.06 | - |
| 1 Default (classic) | 66 (sky file) | grey 0.18 | 0.192 0.191 0.204 | 0.028 0.028 0.028 | 6.80 | 5.80 | 1.00 | 1.06 | - |
| 1 Default (classic) | 60 | white 0.9 | 1.032 1.025 1.078 | 0.149 0.149 0.149 | 6.90 | 5.90 | 1.00 | 1.04 | - |
| 1 Default (classic) | 60 | grey 0.18 | 0.187 0.186 0.196 | 0.030 0.030 0.030 | 6.26 | 5.26 | 1.00 | 1.05 | - |
| 1 Default (classic) | 30 | white 0.9 | 0.755 0.714 0.687 | 0.203 0.203 0.203 | 3.55 | 2.55 | 1.00 | 0.91 | - |
| 1 Default (classic) | 30 | grey 0.18 | 0.137 0.129 0.124 | 0.041 0.041 0.041 | 3.20 | 2.20 | 1.00 | 0.90 | - |
| 2 Clear PBR sky | 66 (sky file) | white 0.9 | 0.365 0.376 0.437 | 0.092 0.105 0.139 | 3.62 | 2.62 | 1.51 | 1.20 | 2.68 |
| 2 Clear PBR sky | 66 (sky file) | grey 0.18 | 0.076 0.079 0.091 | 0.018 0.021 0.028 | 3.78 | 2.78 | 1.51 | 1.19 | 2.78 |
| 2 Clear PBR sky | 60 | white 0.9 | 0.347 0.356 0.412 | 0.092 0.105 0.139 | 3.43 | 2.43 | 1.51 | 1.19 | 2.47 |
| 2 Clear PBR sky | 60 | grey 0.18 | 0.073 0.075 0.086 | 0.018 0.021 0.028 | 3.59 | 2.59 | 1.51 | 1.18 | 2.57 |
| 2 Clear PBR sky | 30 | white 0.9 | 0.209 0.208 0.234 | 0.092 0.105 0.139 | 2.01 | 1.01 | 1.51 | 1.12 | 1.45 |
| 2 Clear PBR sky | 30 | grey 0.18 | 0.045 0.044 0.049 | 0.018 0.021 0.028 | 2.13 | 1.13 | 1.51 | 1.10 | 1.50 |
| 3 Overcast creator sky | 90 (sky file) | white 0.9 | 0.327 0.260 0.158 | 0.087 0.087 0.087 | 3.05 | 2.05 | 1.00 | 0.48 | - |
| 3 Overcast creator sky | 90 (sky file) | grey 0.18 | 0.054 0.042 0.024 | 0.017 0.017 0.017 | 2.46 | 1.46 | 1.00 | 0.45 | - |
| 3 Overcast creator sky | 60 | white 0.9 | 0.332 0.261 0.170 | 0.103 0.103 0.103 | 2.62 | 1.62 | 1.00 | 0.51 | - |
| 3 Overcast creator sky | 60 | grey 0.18 | 0.055 0.042 0.027 | 0.021 0.021 0.021 | 2.11 | 1.11 | 1.00 | 0.49 | - |
| 3 Overcast creator sky | 30 | white 0.9 | 0.306 0.232 0.180 | 0.139 0.139 0.139 | 1.75 | 0.75 | 1.00 | 0.59 | - |
| 3 Overcast creator sky | 30 | grey 0.18 | 0.051 0.037 0.031 | 0.028 0.028 0.028 | 1.43 | 0.43 | 1.00 | 0.62 | - |
| 3b Same, as a PBR sky | 90 (sky file) | white 0.9 | 0.151 0.132 0.044 | 0.099 0.099 0.034 | 1.37 | 0.37 | 0.35 | 0.29 | 1.35 |
| 3b Same, as a PBR sky | 90 (sky file) | grey 0.18 | 0.031 0.027 0.009 | 0.020 0.020 0.007 | 1.39 | 0.39 | 0.35 | 0.29 | 1.38 |
| 3b Same, as a PBR sky | 60 | white 0.9 | 0.143 0.125 0.042 | 0.099 0.099 0.034 | 1.30 | 0.30 | 0.35 | 0.29 | 1.24 |
| 3b Same, as a PBR sky | 60 | grey 0.18 | 0.029 0.025 0.009 | 0.020 0.020 0.007 | 1.32 | 0.32 | 0.35 | 0.29 | 1.26 |
| 3b Same, as a PBR sky | 30 | white 0.9 | 0.122 0.108 0.037 | 0.099 0.099 0.034 | 1.12 | 0.12 | 0.35 | 0.31 | 1.07 |
| 3b Same, as a PBR sky | 30 | grey 0.18 | 0.025 0.022 0.008 | 0.020 0.020 0.007 | 1.14 | 0.14 | 0.35 | 0.30 | 1.08 |

## Reading

- **D6 (the direct-to-sky ratio).** A clear classic day has a sun-to-shade ratio of 6.3-7.5 at
  60-66 degrees (inside the study's textbook 3-8 : 1 range for sun over diffuse sky, near its top),
  falling to 3.2-3.6 at 30 degrees. A clear PBR sky is 3.4-3.8, lower because the PBR path's
  sun term is `nl * diffuse/pi * sunlit * 3.0` and the probe fills the shade with sky light. At
  30 degrees it is 2.0-2.1, a rather filled shadow. This is the number the later shadow
  tuning (S3) and the skylight stages (L4, L5) have to move from.
- **The shade is grey in every classic sky** (B/R 1.00) and the sun is nearly neutral in the
  Default sky (B/R 1.04-1.06 at 60-66 degrees): the study's D3. Only a PBR sky shows a blue shade
  (B/R 1.51), and then the lit side is blue-ish too (1.18-1.20) because the PBR path does not
  white-balance (L5).
- **An overcast sky does not look overcast on the ground (D1).** The overcast creator sky (3)
  keeps a sun-to-shade ratio of 2.1-2.6 at 60-90 degrees, i.e. 1.1-1.6 times the shade is still
  direct sun with crisp shadows, with `cloud_shadow` 0.71. In the PBR variant (3b) it is 1.3-1.4 only
  because that creator sky has a very dim sun colour. This is what L2 and L4 change.
- **Classic and PBR differ a lot** for the same sky file (the classic path adds the 1.35, 0.7, 1.1
  factors, and sRGB mixing). An L2 comparison must keep the two apart.

## Limits

- The probe irradiance E holds only the sky (see above); the real value includes the ground and the
  scene. The fallback column does not depend on it.
- One panel orientation (up) and one view; metallic 0, roughness 1. Specular and the other
  materials are not in the table.
- No ambient occlusion (`HAS_SSAO` off), no exposure, no tone mapping.
- Offline on one GPU (RTX 5070 Ti); the values do not depend on it beyond float rounding.
- The sky files' `sun_angle` is read as the light's elevation (sin of it is the light direction's
  vertical component, as in `convert_azimuth_and_altitude_to_quat`); the viewer's day cycle is not
  followed.

Reproduce: `python scripts/light/measure_sun_shade.py` (Windows, a GPU).
