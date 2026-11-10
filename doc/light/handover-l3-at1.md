<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Sun and moon handover from the physical atmosphere (stages L3 and AT1 of item 4)

The ground is lit by one light. It is the sun's by day and the moon's at night, and the two hand over
smoothly while both are about dark. The sun's light comes from the physical atmosphere (`LLAtmosphere`,
stage AT0, `indra/newview/llatmosphericlight.h`) instead of the old `exp(-(blue_density + haze_density / 4) *
density_multiplier * max_y / lightnorm.y)`. At noon the result is today's, to 1e-11.

## The chain (once per main-thread frame)

`LLEnvironment::updatePrimaryLight()` (from `updateSettingsUniforms()`, not in cube snapshots, which reuse it)
fills `LLEnvironment::PrimaryLightState` by calling `compute_primary_light()`:

1. The legacy sky as the deferred pass receives it: the sun colour normalised to a maximum of 1, clamped, and
   scaled when the legacy sky is auto-adjusted (`LLEnvironment::getDeferredSunlight()`, also what
   `setupHWLights` stores in `mSunDiffuse`).
2. `LLAtmosphere::fromLegacy` and four transmittances: the sun's and the moon's at the ground, and at the cloud
   layer (`RenderCloudShadowPenumbraHeight`, 1500 m, path factor 0.5 for light rigs).
3. The moon's gain: `moon_brightness x moon_compensation`.
4. `primary_light()` on the ground transmittances: direction, transmittance, shadow strength, moon weight t.
5. `primary_light()` again on the cloud-layer transmittances: the clouds' own moon weight t_cloud.

## Three phases

r is the moon's share of the light, E_moon / (E_sun + E_moon), from luminances of source x transmittance.
Stateless, no hysteresis. Cuts at r = 0.10 / 0.40 / 0.60 / 0.90 (smoothstep, C1):

| r | what happens |
|---|---|
| 0.10 to 0.40 | the sun's shadow fades 1 to 0 |
| 0.40 to 0.60 | the light's direction and colour swing from sun to moon (nlerp, bent over the zenith when the two face apart) |
| 0.60 to 0.90 | the moon's shadow fades 0 to 1 |

When both lights are dark (a moonless sunset) r follows the sun's disc: the sun's side while it is up. The swing
takes 0.4 to 2.8 s of a 4-hour day cycle at the disc's edge, when both lights are about 0.

## Moon compensation

The moon is physical while the sun is up (2.5e-6 of the sun) and reaches `2^EV x moon_brightness` by
`RenderMoonAdaptationDepth` degrees below the horizon (smoothstep). EV is limited to -8 .. -2 (never more than a quarter of a sun of the same
colour). A sky with `moon_brightness` 0 has no direct light at night.

## Clouds

The clouds have their own handover: at 1.5 km the horizon dips 1.24 degrees, so the sun still shines on the
clouds after the ground's sunset. The dome and the ground use t; the flat clouds' sun term (`cloudsV.glsl`) uses
`cloud_moon_blend` = t_cloud and the sun's transmittance at the cloud layer (`cloud_light_transmittance`), taken
from the raw `sunlight_color` (the auto-adjust scale of `RenderSkyAutoAdjustSunColorScale` is not applied to the
clouds; the ground's sun keeps it). The clouds' glow direction (`cloud_light_dir`, the clamped `cloud_dir`) and
the sun attenuation of the below-cloud haze (`off_axis` in `cloudsV.glsl`) follow the same handover; the dome
keeps `lightnorm`. The L2 cloud map composes: its weight is multiplied by `primary_shadow_strength`
(`atmosphericsFuncs.glsl`).

## Limitations

- The cloud self-shade offset (`vary_texcoord1` in `cloudsV.glsl`) stays on `lightnorm`, the ground's light
  direction, on purpose: it matches the L2 cloud map.

## Uniforms

| Uniform | Set by | Read by |
|---|---|---|
| `primary_light_dir` (eye space) | `LLPipeline::bindDeferredShader` | the shaders that took `sun_dir`/`moon_dir` by `sun_up_factor` |
| `primary_light_color`, `primary_shadow_strength` | `LLSettingsVOSky::applySpecial` (every shader) | `atmosphericsFuncs.glsl` (`sunlit`, the haze), `shadowUtil.glsl` |
| `sun_moon_blend` (t) | `applySpecial` (sky group) | `skyV.glsl` |
| `cloud_light_transmittance`, `cloud_moon_blend`, `cloud_light_dir` | `applySpecial` (sky group) | `cloudsV.glsl` |

The shadow caster, the PCSS light size, the cloud map's penumbra and HW light 0 follow the primary light. The
sun shadow pass is skipped while the shadow is invisible.

## Debug settings (Preferences > Graphics > Sky, devel only)

| Setting | Default | Meaning |
|---|---|---|
| `RenderLightHandoverSwing` | 0.10 | half-width of the swing around r = 0.5 |
| `RenderLightHandoverFade` | 0.30 | width of each shadow fade (swing + fade is kept under 0.49) |
| `RenderMoonlightCompensation` | -3 | EV of the moon's direct light against the sun's, limit -8 .. -2 |
| `RenderMoonAdaptationDepth` | 6 | degrees of sun depression at which the compensation is reached |
| `RenderAtmosphereHazeSlab` | 250 | ground haze layer height of the physical atmosphere, m |
| `RenderAtmospherePhysicalLight` | on | off: today's airmass through the same handover (A/B) |

## What the tests assert (`llatmosphericlight_test.cpp`, no GPU)

- Noon identity of the ground light, physical and A/B, for 12 embedded skies, the light rigs included.
- Sunset sweeps in 0.01 degree steps from +10 to -20 degrees (4 skies, physical and A/B, moon high / opposite /
  none): unit finite directions; direction, shadow and glow jump only where the ground light is below 0.5 % of the
  source's; the physical ground light is continuous while the sun is up; the clouds' sun term
  (1 - t_cloud) Y(cloud_T) steps by less than 0.05 per 0.01 degree.
- Continuity across the four cuts; the moon never above a quarter of the sun's colour; no moon, no direct light at
  night; 2033 horizon, NaN and hostile-setting cases are finite.
- `scripts/light/gen_atmosphere_vectors.py` (GLSL against C++ for `sunlit`/`amblit`), `check_cloud_shadow.py --check`
  (the lookup follows `primary_shadow_strength`) need a GPU.
