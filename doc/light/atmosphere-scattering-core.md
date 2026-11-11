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
`scatter_clamped`. Non-finite input counts as 0 (NaN) or as the largest finite value of its sign (infinity), then the bound applies.

| Value | From | Bound | Bit |
|---|---|---|---|
| Rayleigh scattering | albedo `blue_horizon / Default's` per channel, x the Rayleigh extinction | albedo in [0, 1] | `SCLAMP_RAYLEIGH_ALBEDO` |
| Aerosol and slab scattering | albedo `0.9 haze_horizon / Default's`, x the aerosol (and slab) extinction | albedo in [0, 0.95] | `SCLAMP_AEROSOL_ALBEDO` |
| Multiple-scattering scale | `ambient / Default's` per channel | [0, 4] | `SCLAMP_MS_SCALE` |
| Multiple-scattering series | Hillaire's `1 / (1 - f_ms)` | `f_ms` <= 0.6, so scale x series <= 4 / 0.4 = **10** | (in the texel) |
| Aerosol phase | the creator's glow lobe `(glow.x max(1 - cos, 0.001))^glow.z + 0.25`, normalised over the sphere (closed-form integral) | glow.x in [0.01, 1000] and glow.z in [-3, 0.5], a continuous clamp; the phase <= 1e4 | `SCLAMP_GLOW_LOBE`, `SCLAMP_PHASE_PEAK` |
| E_sun | `3 x scale x lin(source)`, the irradiance `pbrBaseLight` gives a sunlit surface | source in [0, 1], scale in [0, 4] | `SCLAMP_SOURCE`, `SCLAMP_SUNLIGHT_SCALE` |
| E_moon | E_sun x AT1's gain (moon_brightness x compensation; 2.5e-6 x mb while the sun is up) | gain <= 1/4 | `SCLAMP_MOON_GAIN` |
| Ground albedo (multiple scattering, the ground below the horizon) | 0.2 | fixed | - |

E_sun and E_moon live in `lights.E[0]` and `lights.E[1]` only (`E_sun()` and `E_moon()` read them).

Notes on the bounds:

- **The gain bound.** A multiple-scattering scale of 4 with a series of up to 10 allowed a gain of 40. A legal synthetic sky (every value at its bound)
  reached 1.36x the white card across the open sky with those two (the test reproduces it); with the product at most 10 it reaches 0.75x. The library's
  largest ambient / Default's is 2.86 ("[TOR] FOGGY - Das fog"), so no shipped sky is on the scale bound; the series bound is reached only by a very thick fog.
- **The glow lobe.** The first design switched to Cornette-Shanks outside glow.z in [-3, 0.5]. That was a discontinuity: it is now a clamp into
  the nearest usable lobe (glow.x floored at 0.01), so the phase moves continuously and monotonically across the bound (tested across glow.z = -3 and the glow.x
  floor). Cornette-Shanks remains the phase of an atmosphere built by hand without a lobe. A glow lobe is at most 1 / (2 pi 0.001) = 159 per steradian
  (the `1 - cos` is clamped at 0.001; Cornette-Shanks at g = 0.95 is about 64), so the cap of 1e4 is a guard, tested with a lower value. A lobe with glow.z > 0 peaks
  backwards; the cap looks at both ends (`phase_peak`).
- The aerosol albedo bound is reached by many skies (the library check): `haze_horizon` is often above Default's 0.19.
- Gamma gets no factor: it stays where it already acts on the whole frame.

## Tables and the altitude

Altitudes are never formed as `|x| - Rg`: at the planet's radius a float has 0.5 m of resolution and the haze slab is 250 m thick. The march carries the observer's
height h0 and takes the altitude of the point at path length t as `q / (r + Rg)` with `q = h0 (2 Rg + h0) + 2 b t + t^2` and `b = (Rg + h0) d.z`; the
sphere intersections (the top, the ground, the slab's top) are quadratics in altitudes solved without cancellation (`quad_roots`). The tables are indexed by the
altitude. This took the C++ vs GLSL error of the march from 2.3e-3 to 1e-5.

- **Transmittance T(h, mu)**, 256 x 64: built from `physical_transmittance` (AT0's twin, cut at the haze slab and every kink). The study's
  40-step midpoint march misses the 0.25 km slab on grazing rays (|dT| to 0.13). The table holds the physical part only; a light multiplies
  the visibility of its disc and the excess factor of the capped columns (`light_T`).
  - u is the distance to the top of the atmosphere between its minimum and the horizon's (Bruneton and Neyret); **v = sqrt(rho / H)**, not rho / H. With rho / H one
    texel is 14 km of horizon distance, which is 0.015 km of altitude at the ground, and a bilinear lookup in dense fog is off by 6e-3; with the root it is off by 1.4e-3.
  - A texel's ray has mu at least 1e-6 above the horizon's, so the table covers the lit side only; the visibility term cuts the rest.
- **Multiple scattering Psi(h, mu_s)**, 32 x 32: `ms_texel`, 8 x 8 directions, 20 **quadratic** steps (the chord to the top of a near-horizontal ray is a thousand
  kilometres, and a uniform step of it 50 km: that was a 10 % error), per unit irradiance. A ray from the ground below the horizon ends at the ground at once
  (`ground_distance`; it used to run through the planet: f_ms 0.81 against 0.34 a micrometre up). Within 3 % of a 48 x 48 x 400 texel (relative to the larger of the value and 0.01).
- **Sky irradiance E(h, mu_s)**, 64 x 16 (x = mu_s, y = sqrt(h / top)): `sky_irradiance_texel`, 8 x 8 cosine-weighted directions, 16 quadratic steps, single
  scattering with the true phase plus Psi. For the clouds (AT2-4) and the probes. Within 0.6 % of a 64 x 64 x 512 integral on Default.
- Psi and E are built from the T **table** (AT2-1 builds them that way: `build_ms_lut` and `build_e_lut` with `t_lut` bound), the vectors from the exact transmittance.

## The march

`atmosphere_ap` (GLSL `atmosphereAP`) is the one march of the froxels, the far table, the cloud trace and the vectors.

- It is split where the ray leaves the 0.25 km haze slab (a uniform march skips the slab: off by up to 99 % at 40 km in fog) into two segments, or is one segment when
  it never crosses the slab's top.
- In each segment the steps are spaced as `(i / n)^power`, power 1.2 in the first segment and 1.1 in the second. They crowd to the start, except in a **thin** segment
  (optical depth < 2) whose far end is more than twice as dense (a ray descending into the slab): there they crowd to the end. In a thick segment the radiance comes from where
  the light still is, the start (a rule "crowd to the lower end" put the steps at the wrong end of a ray that stays in the slab: 1.07 % at 0.5 degrees below the horizon).
- The steps are shared between the two segments by the radiance each can contribute: with w1 = 1 - e^-tau1 and w2 = e^-tau1 (1 - e^-tau2) the first segment gets
  `(w1 + 0.2) / (w1 + w2 + 0.4)` of them, at least 2 and at most steps - 2 (tau from Simpson's rule on the extinction, three medium evaluations per segment).
- The earlier schemes: half the steps to the slab, uniform inside it, was 1.3 % in Foggy at 2 degrees; 40 % of the steps and exponent 1.25 inside was 2.14 % on Bangko 1 grazing and 1.75 % in Foggy at
  0.3 km looking 2 degrees down. The constants were scanned on Default, Foggy, Sunset, Bangko 1, Sunset in twilight and Default at night, all heights and elevations of the domain below.

## Measurements

### R-ACC (restated: a converged reference)

Max relative error of the in-scatter, per channel, relative to the larger of the reference and 1e-3 of its largest channel. The domain: observer heights 0, 0.002, 0.1, 0.3, 2, 4 and 6 km;
elevations -2, -0.5, 5, 10, 30, 90 degrees and every 0.1 degree of 0.1 to 3; azimuths 0 and 135; distances 2, 10 and 40 km; rays that end above the ground and below 12 km (the clouds' domain);
1444 cases per sky. The reference is the same march at 256 steps (checked against 4096 on every 100th case: < 0.001 %). T from the table (so the march is isolated). The moon is AT1's
(E_moon = E_sun x mb x compensation, opposite the sun).

| Sky | worst | at 2 / 10 / 40 km | grazing 0.1-3 deg | 8 steps at 2 / 10 / 40 km |
|---|---|---|---|---|
| Default (sun 65.9) | 0.45 % (h 0.1, el 90, 10 km) | 0.04 / 0.45 / 0.21 % | 0.08 % | 0.4 / 1.5 / 0.5 % |
| Foggy (sun 90) | 0.39 % | 0.10 / 0.39 / 0.33 % | 0.33 % | 0.5 / 0.5 / 0.9 % |
| Sunset (sun 4.3) | 0.44 % | 0.18 / 0.35 / 0.44 % | 0.29 % | 1.6 / 4.1 / 1.3 % |
| Bangko 1 (sun 1.8) | 0.74 % (h 0.3, el -2, 2 km) | 0.74 / 0.41 / 0.48 % | 0.48 % | 2.4 / 2.4 / 9.7 % |
| Sunset, twilight (sun -8, moon 8) | 0.49 % | 0.21 / 0.40 / 0.49 % | 0.21 % | 1.5 / 4.0 / 1.0 % |
| Default, night (sun -30, moon 30) | 0.39 % | 0.04 / 0.28 / 0.39 % | 0.16 % | 0.4 / 1.0 / 0.8 % |

**12 steps meet 1 % over this domain (worst 0.74 %)**; 8 steps do not (9.7 % on Bangko 1). The claim is limited to this domain.

Beyond it: on the 768 shipped skies (sun at the file's elevation, observer on the ground, elevations 0 to 90, the clouds' domain; error relative to the larger of the reference and 1 % of the white card)
**130 skies exceed 1 % at 12 steps** (129 of them not light rigs), 18 at 24 steps and 5 at 48 steps (`check_atmosphere_library.py --acc`; the worst 12-step errors are 12 / 80 / 36 % at 2 / 10 / 40 km on "Grit & shame",
"Phototools- Lo Light 02" and "[AnaLu] Studio Light (Gillian)"). The common case is a sun on or near the horizon in haze, or a very dense ground fog. This is open for AT2-3.

### Against the GPU

`gen_atmosphere_scatter_vectors.py` runs `atmosphereCommonF.glsl` on 14 random atmospheres (770 vectors: the parameterisation both ways, the table texels, the phase functions, the medium,
the light transmittance, `atmosphereAP` at 6, 12 and 24 steps (the first four at h = 0 and 0.002 km, looking 0.2 to 6 degrees up), the multiple-scattering texel (once on the ground), the sky irradiance texel and
the sky including the ground (the first two from the ground)). The light transmittance is the exact `pa_transmittance` and the multiple scattering a smooth stand-in, so the vectors hold the
arithmetic, not a table. Worst relative |C++ - GLSL|: parameterisation 4e-7, texel 1e-6, phase and medium 2e-6, light T 1e-6, march **1e-5**, multiple-scattering texel 1.5e-6, sky irradiance 4e-6,
sky 7e-6; the test holds all of them to 1e-4. (The march was 2.3e-3 with positions taken at the planet's radius.)

The `.inc` also holds the SHA-256 of `physicalAtmosphereF.glsl` and `atmosphereCommonF.glsl` (line ends normalised); the test recomputes them, so a changed shader fails CI until the vectors are regenerated
(`gen_atmosphere_scatter_vectors.py`, then `--check`, which also compares the numbers to the committed ones).

### Sun disc and sky irradiance

- The sun's colour from the table (`light_T`: table x disc x excess) is AT1's CPU transmittance within **0.04 %** above 2 degrees on every embedded sky,
  where T > 0.01 (a light rig's sun, with an optical depth over 4.6, is below that: bilinear interpolation of T is relatively inaccurate there, and it is dark).
- The sky irradiance on a horizontal surface, Default at 65.9 degrees: E_sky = 0.137, 0.178, 0.238 per unit sun; the study's *capture irradiance*
  lum(E_sky E_sun) / pi = **0.0965** (the study measured 0.096, today's probe sky 0.115). A reported value; the test accepts 0.05 to 0.2.

### The library (768 shipped skies)

`python scripts/light/check_atmosphere_library.py [--acc] [--quick]` translates every sky, samples the sky radiance (sun at the file's elevation and at 5, 30, 60,
90 and -30 degrees with AT1's moon, 42 directions each, 24 steps) and reports. The measurable "no whiteout" is that outside 10 degrees of the light no radiance
exceeds the white-card limit lum(E_light) / pi.

**Owner decision (2026-11-11): the hazy skies whose aureole exceeds the card are accepted as physical.** The aureole is 10 to 20 degrees from the light, and may exceed the card by at most 2x; beyond 20 degrees the rule is strict.
The script fails on any other exceedance and on any aureole exceedance by a sky outside its expected set. The expected set, on the 768 skies:

| Sky | Where | Ratio |
|---|---|---|
| [TOR] SCIFI - Arrakissed 2 | sun -21, 11.2 degrees from the moon | 1.02x |
| [TOR] SCIFI - Awesome atmo 2 | sun 14.8, 14.8 degrees from the sun | 1.09x |
| [TOR] SCIFI - Awesome atmo 3 | sun 14.8, 14.8 degrees from the sun | 1.09x |
| Surreal - Summer (Paulina) | sun -12, 12.2 degrees from the moon | 1.16x |

Other results: bounds hit on Rayleigh albedo 295 skies, aerosol albedo 402, the glow clamp 15, the others none; nothing non-finite; the largest multiple-scattering amplification of the library is 2.5.
The survey's literal limit E_sun T_sun / pi cannot hold in twilight (the sky is up to 7x the ground-level card there: the scattering happens in air the sun reaches that the ground does not); the limit used is E_light / pi.
`--quick` (every 8th sky, 10 s, deterministic) runs in CI. Earlier runs found 9 skies up to 1.52x, which were a bug (an observer exactly on the ground looking below its horizon took the far side of the chord through the planet as the ground), and, with the first gain bound, 5 skies up to 1.26x.

## Tests

`llatmosphericscatter_test.cpp` (CI step in `.github/actions/ci-checks`; `-O2` 13 s, `-O0` 28 s):

1. Phase functions integrate to 1 over the sphere on 11 embedded skies.
2. The translation of Default (albedos 1 and 0.9, scale 1, E_sun = 3 lin(source), no clamp) and the invariants of every embedded sky.
3. Bounds: sweeps of blue_horizon, haze_horizon and ambient across their bounds are continuous, end on the bound and set the bit; the sunlight scale and the moon's gain are bounded.
4. The aerosol phase: glow.z across -3, glow.x across its floor, glow.z beyond 0.5 and infinities clamp continuously and set the bit exactly; the backward peak; the cap.
5. Hostile input: NaN, infinities, negative and 1e30 in each field (40 cases); the marches and texels with hostile directions, heights, distances, step counts, light directions and light counts, tables bound or not (484 marches); scattering never exceeds extinction.
6. The transmittance table: round trip, texels, lookups within 2e-3, the sun's colour; `ground_distance` (on the ground below the horizon is 0, upward -1, 1e-6 km up a few millimetres, the dip of the horizon).
7. Multiple scattering: f_ms in thick fog, continuity down to the ground (h = 0 vs 1e-6 km), a smooth table, within 3 % of a converged texel.
8. The sky irradiance texel against a converged one (0.6 %); the capture irradiance.
9. No whiteout: the embedded skies (largest ratio 0.34); a synthetic sky at every bound (0.75, and 1.36 with the first bounds: the test can fail); the light rigs stay dark (0.1 and 0.33 % of Default's sky).
10. The GPU vectors (1e-4) and the shaders' SHA-256.
11. R-ACC (above), with the reference's own convergence checked.

## Open points

- The aerial-perspective step placement is tuned on six skies and meets 1 % on that domain, not on the library (130 skies at 12 steps, 18 at 24, 5 at 48).
- The T table is bilinear in T; a light rig's near-zero sun colour is inexact there (1.3 %; dark, below 0.01).
- `ms_override` (the test hook for the vectors' stand-in multiple scattering) is still a member of `LLScatter`.
- Not in AT2-0: the passes that fill the tables (AT2-1), the sky in the lighting pass (AT2-2), the froxels and `atmosphereApply` (AT2-3), the far-AP table and
  `atmosphereAPFar` (AT2-4); the cloud shadow map's mean and the night sky's extinction.
