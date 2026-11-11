<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# AT2-0: the scattering core of the physical atmosphere

Stage AT2-0 of the item-4 plan (physical sky and haze for every sky). No rendering change: this is the renderer-free core that AT2-1 to
AT2-5 put on the GPU, and the bounds, vectors and accuracy tests they are held to. Measurements are offline, in double precision on the
CPU, except where a GPU is named (RTX 5070 Ti, driver 621.14).

| Piece | File |
|---|---|
| Translation, bounds, tables, reference integrals (C++, double) | `indra/newview/llatmosphericlight.h` (`LLScatter`, end of the file) |
| GLSL twin (not registered, no viewer shader includes it) | `indra/newview/app_settings/shaders/class1/windlight/atmosphereCommonF.glsl` (after `physicalAtmosphereF.glsl`) |
| Test (CI, no GPU) | `indra/newview/tests/llatmosphericscatter_test.cpp` |
| GPU vectors | `scripts/light/gen_atmosphere_scatter_vectors.py` writes `indra/newview/tests/llatmosphere_scatter_vectors.inc` |
| Library check (the 768 shipped skies, no GPU) | `scripts/light/check_atmosphere_library.py` and `atmosphere_library_check.cpp` |

## What is translated, and how it is bounded

AT0 (`LLAtmosphere::fromLegacy`) already turns a sky's extinction into physical values. `LLScatter::fromLegacy` adds the scattering. Every
value is brought into its bound (clamped, never switched to the old model), each bound is continuous, and each sets a bit in
`scatter_clamped`.

| Value | From | Bound | Bit |
|---|---|---|---|
| Rayleigh scattering | albedo `blue_horizon / Default's` per channel, x the Rayleigh extinction | albedo in [0, 1] | `SCLAMP_RAYLEIGH_ALBEDO` |
| Aerosol and slab scattering | albedo `0.9 haze_horizon / Default's`, x the aerosol (and slab) extinction | albedo in [0, 0.95] | `SCLAMP_AEROSOL_ALBEDO` |
| Multiple-scattering scale | `ambient / Default's` per channel | [0, 4] | `SCLAMP_MS_SCALE` |
| Multiple-scattering series | Hillaire's `1 / (1 - f_ms)` | `f_ms` <= 0.9: at most 10x (the study allowed 1000x, a whiteout in thick fog) | (in the texel) |
| Aerosol phase | the creator's glow lobe `(glow.x max(1 - cos, 0.001))^glow.z + 0.25`, normalised over the sphere (closed-form integral) | glow.x <= 0 or glow.z outside [-3, 0.5]: Cornette-Shanks with the Mie anisotropy; the phase <= 1e4 | `SCLAMP_GLOW_LOBE`, `SCLAMP_PHASE_PEAK` |
| E_sun | `3 x scale x lin(source)`, the irradiance `pbrBaseLight` gives a sunlit surface | source in [0, 1], scale in [0, 4] | `SCLAMP_SOURCE`, `SCLAMP_SUNLIGHT_SCALE` |
| E_moon | E_sun x AT1's gain (moon_brightness x compensation; 2.5e-6 x mb while the sun is up) | gain <= 1/4 | `SCLAMP_MOON_GAIN` |
| Ground albedo (multiple scattering, the ground below the horizon) | 0.2 | fixed | - |

Notes on the bounds:

- The glow lobe switching to Cornette-Shanks is the one bound that is not continuous: it is a switch between two physical phases (the
  plan's choice), not a clamp. The glow lobe's own peak cannot reach 1e4: with `1 - cos` clamped at 0.001 it is at most
  1 / (2 pi 0.001) = 159 per steradian (and Cornette-Shanks at g = 0.95 is about 64). The cap is a guard, exercised in the test with a
  lower value.
- The aerosol albedo bound is reached by many skies (the library check below): `haze_horizon` is often above Default's 0.19.
- Gamma gets no factor: it stays where it already acts on the whole frame.

## Tables

All tables are built from the C++ in the tests and by the GPU passes in AT2-1.

- **Transmittance T(r, mu)**, 256 x 64: built from `physical_transmittance` (AT0's twin, cut at the haze slab and every kink). The study's
  40-step midpoint march misses the 0.25 km slab on grazing rays (|dT| to 0.13). The table holds the physical part only. A light multiplies
  the visibility of its disc and the excess factor of the capped columns (`light_T`).
  - Parameterisation: u is the distance to the top of the atmosphere between its minimum and the horizon's (Bruneton and Neyret); **v = sqrt(rho / H)**, not
    rho / H. With rho / H one texel is 14 km of horizon distance, which is 0.015 km of altitude at the ground, and a bilinear lookup in
    dense fog is off by 6e-3; with the root it is off by 1.4e-3 (all embedded skies <= 1.4e-3, most <= 8e-4).
  - A texel's ray has mu at least 1e-6 above the horizon's, so the table covers the lit side only; the visibility term cuts the rest.
- **Multiple scattering Psi(r, mu_s)**, 32 x 32: `ms_texel`, 8 x 8 directions, 20 steps, per unit irradiance.
- **Sky irradiance E(h, mu_s)**, 64 x 16 (x = mu_s, y = sqrt(h / top)): `sky_irradiance_texel`, 8 x 8 cosine-weighted directions, 16 quadratic steps, single
  scattering with the true phase plus Psi. For the clouds (AT2-4) and the probes. Within 0.6 % of a 64 x 64 x 512 integral on Default.

## The march

`atmosphere_ap` (GLSL `atmosphereAP`) is the one march of the froxels, the far table, the cloud trace and the vectors. It is split where the
ray leaves the 0.25 km haze slab (a uniform march skips the slab: off by up to 99 % at 40 km in fog), quadratic beyond it, and 40 % of the
steps (5 of 12) go to the part up to the slab's top. When the observer is inside the slab that part is spaced with exponent 1.25 (uniform
is 1.7 % in dense fog, quadratic 4 %). The two numbers (`AP_SLAB_STEP_SHARE`, `AP_INSIDE_POWER`) were tuned on Default, Foggy, Sunset,
Bangko 1 and Default at night, and are the same in the GLSL.

## Measurements

### R-ACC (restated: a converged reference)

The reference is the same march at 4096 steps (a uniform 64-step march is itself off by up to 99 %). Max relative error of the in-scatter, per
channel, relative to the larger of the reference and 1e-3 of its largest channel; the clouds' domain (end altitude <= 12 km, elevation >= 0),
elevations 0 to 90 degrees x azimuths 0, 90, 180; T from the table (so the march is isolated); the moon is AT1's (E_moon = E_sun x mb x
compensation, opposite the sun).

| Sky | 2 km | 10 km | 40 km | 8 steps (for comparison) |
|---|---|---|---|---|
| Default (sun 65.9) | 0.27 % | 0.68 % | 0.29 % | 1.3 / 1.5 / 0.7 % |
| Foggy (sun 90) | 0.13 % | 0.54 % | 0.54 % | 9.9 / 9.8 / 2.4 % |
| Sunset (sun 4.3) | 0.21 % | 0.45 % | 0.30 % | 10.7 / 6.8 / 5.4 % |
| Sunset, twilight (sun -8, moon 8 deg, gain 0.0625) | 0.19 % | 0.46 % | 0.27 % | 8.9 / 6.2 / 5.5 % |
| Default, night (sun -30, moon 30 deg) | 0.25 % | 0.60 % | 0.27 % | 1.3 / 1.3 / 0.7 % |

12 steps meet 1 % (worst 0.68 %); 8 steps do not. The survey's 8-step Default figure (2.1 % at 10 km) was with a different step distribution; the
moon at AT1's gain changes nothing measurable (by day it is 2.5e-6 of the sun).

### Against the GPU

`gen_atmosphere_scatter_vectors.py` runs `atmosphereCommonF.glsl` on 14 random atmospheres (700 vectors: the parameterisation both ways, the table texels,
the phase functions, the medium, the light transmittance, `atmosphereAP` at 6, 12 and 24 steps, the multiple-scattering texel, the sky irradiance texel and
the sky including the ground). The light transmittance is the exact `pa_transmittance` and the multiple scattering a smooth stand-in, so the vectors hold the
arithmetic, not a table. Worst relative |C++ - GLSL|: parameterisation 1e-3, texels 5e-4, phase and medium 3e-7, light T 5e-4, multiple-scattering texel 3e-4, sky
irradiance 4e-5, sky 5e-4, **the march 2.3e-3** (gate 4e-3).

The march's 2e-3 is float precision, not logic: positions at the planet's radius (6360 km) have 0.5 m resolution, and the slab's density changes over
190 m. AT2-1 should keep marching positions relative to the surface (altitude, not radius) if the GPU tables are to be held to better than 3e-3.
`gen_atmosphere_scatter_vectors.py --check` regenerates and compares (1e-3).

### Sun disc and sky irradiance

- The sun's colour from the table (`light_T`: table x disc x excess) is AT1's CPU transmittance within **0.04 %** above 2 degrees on every embedded sky,
  where T > 0.01 (a light rig's sun, with an optical depth over 4.6, is below that: bilinear interpolation of T is relatively inaccurate there, and it is dark).
- The sky irradiance on a horizontal surface, Default at 65.9 degrees: E_sky = 0.136, 0.178, 0.238 per unit sun; the study's *capture irradiance*
  lum(E_sky E_sun) / pi = **0.0963** (the study measured 0.096, today's probe sky 0.115). A regression number, not a gate (the test accepts 0.085 to 0.11).

### The library (768 shipped skies)

`python scripts/light/check_atmosphere_library.py [--acc] [--quick]` translates every sky, samples the sky radiance (sun at the file's elevation and at 5, 30, 60,
90 and -30 degrees with AT1's moon, 42 directions each, 24 steps) and reports. The measurable "no whiteout" is that outside 10 degrees of the light no radiance
exceeds the white-card limit lum(E_light) / pi.

Result on the 768 skies (`--acc`):

- Bounds hit: Rayleigh albedo on 295 skies, aerosol albedo on 402, the glow falling back to Cornette-Shanks on 15; the multiple-scattering scale, the
  phase peak, the sunlight scale, the moon's gain, the source and the anisotropy on none; nothing non-finite. The largest multiple-scattering amplification
  is 10.0 (the bound; "[TOR] FOGGY - Golden glow").
- **No whiteout: 5 skies of 768 exceed the card, by at most 1.26x** (Raylanta 1.26x at sun 18, 18 degrees from the sun; Awesome atmo 2 and 3 1.09x and 1.08x at
  14.8 degrees; Surreal - Summer 1.16x with the moon at -12 degrees; Arrakissed 2 1.02x). Widening the zone to 15 degrees leaves one (Raylanta). They are steep
  glow lobes in the aureole, not the multiple scattering (a multiple-scattering scale bound of 2 changes nothing; a lower aerosol albedo bound or f_ms bound
  helps a little, 0.9 gives 7 of the first 9) and not the phase peak (capping the phase at 3 changes nothing). The survey's literal limit E_sun T_sun / pi
  cannot hold in twilight: the sky is 12.8x brighter than the sun's *ground-level* card there, because the scattering happens in air the sun reaches
  that the ground does not; the limit used is E_light / pi. The script exits 1 while any sky exceeds it. A decision for the owner (below).
- An early version of the check found 9 skies, up to 1.52x, which were a bug and are fixed: an observer exactly on the ground looking below its horizon
  took the far side of the chord through the planet as the ground (`ground_distance`). The observer is at the ground then (distance 0).
- **The 12-step march meets 1 % on 653 of 768 skies; 115 do not** (error relative to the larger of the reference and 1 % of the white card; worst 91 % /
  101 % / 101 % at 2 / 10 / 40 km on "Grit & shame" and "Anime - 6AM"). The common case is a sun on or within a degree of the horizon in haze ("6AM" skies have a sun
  elevation of 0): the sun's transmittance along the ray changes from nothing to something over a few hundred metres of altitude and 12 steps cannot resolve it
  (Anime - 6AM, sun at 0 degrees, ray 2 degrees up, 10 km: 12 steps 0.55, 1024 steps 0.27, 8192 steps 0.27). Default, Foggy and Sunset, the owner's three, and
  Bangko 1 pass at 12 steps. AT2-3 has to decide how the froxel pass spends steps near a horizon sun (more steps there, or steps placed by the gradient of the
  sun's transmittance).


## Tests

`llatmosphericscatter_test.cpp` (CI step in `.github/actions/ci-checks`; `-O2` 8 s, `-O0` 21 s):

1. Phase functions integrate to 1 over the sphere on 11 embedded skies; the glow lobe's closed-form integral is continuous across glow.z = -1.
2. The translation of Default (albedos 1 and 0.9, multiple-scattering scale 1, E_sun = 3 lin(source), no clamp) and the invariants of every embedded sky.
3. Bounds: sweeps of blue_horizon, haze_horizon and ambient across their bounds are continuous, end on the bound and set the bit; the sunlight scale and the moon's gain
   are bounded; the glow falls back to Cornette-Shanks outside its range; the phase never exceeds its cap.
4. Hostile input (NaN, infinities, negative, 1e30 in each field, 40 cases) stays finite and bounded.
5. The transmittance table: the parameterisation round-trips, texels equal `physical_transmittance`, lookups are within 2e-3, the sun's colour is AT1's within 1 % above 2 degrees.
6. Multiple scattering: the series amplifies by at most 10 in thick fog; Default's table is smooth.
7. The sky irradiance texel against a converged one (0.6 %) and the regression number.
8. No whiteout on the embedded skies (largest ratio 0.34); the observer on the ground looking below its horizon is at the ground.
9. The GPU vectors.
10. R-ACC (above).

## Open points

- Float precision of the march on the GPU (above).
- The library's whiteouts and the aerial-perspective errors on extreme skies (above).
- The T table at the horizon is bilinear in T; a light rig's near-zero sun colour is inexact there (dark, below 0.01).
- Not in AT2-0: the passes that fill the tables (AT2-1), the sky in the lighting pass (AT2-2), the froxels and `atmosphereApply` (AT2-3), the far-AP table and
  `atmosphereAPFar` (AT2-4); the cloud shadow map's mean and the night sky's extinction.
