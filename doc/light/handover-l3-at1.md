<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Sun and moon handover from the physical atmosphere (stages L3 and AT1 of item 4)

The ground is lit by one light. It is the sun's by day and the moon's at night, and the two hand over
smoothly around the horizon, by the sun's elevation (see Three phases). The sun's light comes from the physical atmosphere (`LLAtmosphere`,
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
4. `primary_light()` on the ground transmittances: direction, transmittance, shadow strength, moon weight t, band weight, moon
   availability.
5. `primary_light()` again on the cloud-layer transmittances: the clouds' own band weight t_cloud (the band centred on -dip).

`RenderCloudShadowPenumbraHeight` is also the cloud altitude for the horizon dip (clamped to 0 .. 20 km once, `h_cloud`).

## Three phases, by the sun's elevation

The first version drove the swing by the moon's share of the light, E_moon / (E_sun + E_moon). That ratio goes 0 to 1 within
+-0.1 degree of the horizon because the sun's light collapses there, which switched the clouds and dimmed the frame abruptly;
it is gone. The handover is a function of the sun's elevation e alone (stateless, no hysteresis, smoothstep so C1):

| sun elevation | what happens |
|---|---|
| +0.6 to +0.3 deg | the sun's shadow fades 1 to 0 |
| +0.3 to -0.3 deg | the light's direction and colour swing from sun to moon, t = smoothstep(+0.3, -0.3, e) x moon availability (nlerp, bent over the zenith when the two face apart); no shadow |
| -0.3 to -0.6 deg | the moon's shadow fades 0 to 1, scaled by the moon's availability |

No usable moon: the light's direction and colour only swing as far as the moon can take over (`moon_weight` = band x
availability). The moon's availability is the product of two smoothsteps: its light before the compensation,
luminance(source x moon_T), against the dark threshold `dark x luminance(source)` (the disc's visibility is in moon_T, so a
moon rising or setting fades in and out), and its gain against 1 % of the physical moon (a sky with moon_brightness 0 has no
moon). With no moon the light keeps the sun's direction and fades to dark with the sun's transmittance, with no shadow below
+0.3 degree; a moon that rises during the band takes over smoothly. The dome, its glow and the clouds' sun term are not
"staying with the sun" at night: they follow the band alone (`band_weight`, 1 below it whether or not a moon is there), so a
moonless night has no sun colour and no sun glow, as before.

The shadow and the handover do not depend on `sun_scale` (the sun's size): they used to, through the sun disc's sine, and a
sun_scale of 16 at 1 degree elevation shadowed at 0.451, now 1.0. The size only reaches `light_scale` (the PCSS light size and
the cloud shadow's penumbra), limited to 100. The test covers sun_scale 1, 4, 16 and 100 by day; above +0.6 degree the shadow
is full for every size.

A sky with `moon_brightness` 0 and the moon up has no moon: the availability is 0, so the ground light is 0 and the classic
dome's moon colour (diffuse B) is scaled to 0 (before the handover the ground light was the unscaled moon colour, and the
dome kept the moon's). On a moonless night the same: the dome has no sun colour (the band) and no moon colour (the
availability), it is dark, as before. By day (band 0, availability irrelevant) nothing changes.

Hardware light 0 (`LLPipeline::setupHWLights`): its direction is the primary light's. Its colours reach only the classic sky
and cloud programs (`LLRender` sends diffuse A as `sunlight_color` and diffuse B as `moonlight_color`). Those programs blend
the sun into the moon themselves: the dome with `sun_moon_blend` (the band weight), the clouds' sun term with
`cloud_light_transmittance` and `cloud_moon_blend` (the dip-shifted band), and the haze below the clouds takes the sun
scaled by (1 - `cloud_moon_blend`). So A is the sun's own colour and B the moon's own colour times `moon_avail`, both taken
before the "neither body up" zeroing, with no hard switch at the band's middle. The dome is then continuous at sun elevation 0
(its sun share falls with the band while the moon's colour, scaled by the availability, rises); with no usable moon it fades
to dark across the band, as the non-classic dome does. The pick `SUN_UP_FACTOR` of light 0 follows `band_weight` < 0.5, but no
shader reads `sun_up_factor` any more (the uniform is only declared).

## Moon compensation

The moon is physical while the sun is up (2.5e-6 of the sun) and reaches `2^EV x moon_brightness` by
`RenderMoonAdaptationDepth` degrees below the horizon (smoothstep). EV is limited to -8 .. -2 (never more than a quarter of a
sun of the same colour). A sky with `moon_brightness` 0 has no direct light at night.

## Clouds

The clouds have their own handover: the same swing width and availability on the cloud-layer transmittances, but the band is
centred on -dip instead of 0 (`PrimaryLightParams::horizon_dip_deg`: 0 for the ground and for h <= 0, limited to 20 degrees).
dip = acos(R / (R + h)) = 1.24 degrees for the 1.5 km layer on a 6360 km planet, so the sun still shines on the clouds after
the ground's sunset: at dusk the clouds stay sunlit after the ground has switched to the moon, at dawn they take the sun before
it. The clouds use the band weight alone, and the shadow fades are the ground's only. The dome uses the ground's band weight, the
ground light uses t; the flat clouds' sun term (`cloudsV.glsl`) uses
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
| `sun_moon_blend` (band weight) | `applySpecial` (sky group) | `skyV.glsl` |
| `cloud_light_transmittance`, `cloud_moon_blend`, `cloud_light_dir` | `applySpecial` (sky group) | `cloudsV.glsl` |

The shadow caster, the PCSS light size, the cloud map's penumbra and HW light 0's direction follow the primary light. The
sun shadow pass is skipped while the shadow is invisible.

## Debug settings (Preferences > Graphics > Sky, devel only)

| Setting | Default | Meaning |
|---|---|---|
| `RenderLightHandoverSwing` | 0.30 | half-width of the swing, degrees of sun elevation (limit 0.05 .. 5) |
| `RenderLightHandoverFade` | 0.30 | width of each shadow fade outside the swing, degrees (limit 0 .. 5) |
| `RenderMoonlightCompensation` | -3 | EV of the moon's direct light against the sun's, limit -8 .. -2 |
| `RenderMoonAdaptationDepth` | 6 | degrees of sun depression at which the compensation is reached |
| `RenderAtmosphereHazeSlab` | 250 | ground haze layer height of the physical atmosphere, m |
| `RenderAtmospherePhysicalLight` | on | off: today's airmass through the same handover (A/B) |

## What the tests assert (`llatmosphericlight_test.cpp`, no GPU)

- Noon identity of the ground light, physical and A/B, for 12 embedded skies, the light rigs included.
- Sunset sweeps in 0.01 degree steps from +10 to -20 degrees (4 skies, physical and A/B, moon high / opposite /
  none): unit finite directions; the sun's direction above +0.3 degree, no shadow inside the band, the moon's direction
  below it (moon high), the sun's all the way with no moon; the shadow steps by less than 0.07 and the direction (physical)
  by less than 15 degrees per 0.01 degree; the physical ground light is continuous while the sun is up; the clouds' sun
  term (1 - t_cloud) Y(cloud_T) steps by less than 0.05 per 0.01 degree (0.15 with the glow near the light).
- The band: t = 0 above +0.3, 0.5 at 0, 1 below -0.3 degree, monotonic and C1; the shadow fades outside it; the limits of
  the two widths (swing 0.05 .. 5 degrees: at +0.06 the weight is 0, and 0.0003 at 4.9 degrees for a swing of 5); no moon
  (below the horizon, or moon_brightness 0): the sun's direction stays; a moon rising during the band: no jump in t.
- The clouds' band (`band_weight`): 0.5 at -dip, 0 above -dip + swing, 1 below -dip - swing; the dip formula and its degenerate
  inputs; the dip limit of 20 degrees (and a negative or non-finite dip counting as 0); dawn mirrors dusk (setting and rising sweeps
  give the same weights, and the clouds' weight is never ahead of the ground's); the cloud height limit (0 .. 20 km: 1e9 is 20,
  negative is 0); the ground is
  bit-identical with or without the cloud height; a dusk where the clouds are still sunlit after the ground switched.
- Night with no moon: band weight 1, no availability, no ground light, no glow; daytime weights 0, shadow 1 and glow
  factor exactly 1 for any sun size (1 to 100); the availability is continuous; the moon never above a quarter of the sun's
  colour; no moon, no direct light at night; 2033 horizon, NaN and hostile-setting cases are finite.
- The test passes at -O2 and at -O0 (the direction checks are in 1e-6 degree).
- `scripts/light/gen_atmosphere_vectors.py` (GLSL against C++ for `sunlit`/`amblit`), `check_cloud_shadow.py --check`
  (the lookup follows `primary_shadow_strength`) need a GPU.
