<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# AT2-F: the creator's filter, measured over the shipped library and the built-in skies

Stage AT2-F of redesign R (decided by the owner on 2026-11-17; see the decision page and `doc/light/atmosphere-study.md`, section 8):
"physics sets the light, the creator sets the look". Each EEP sky is a **filter** on the physical sky (AT2-0's `LLScatter`,
`llatmosphericlight.h`, with decision D1's march). The model has two parts:

- **The visible sky** (the dome, and the haze on distant objects) is exactly today's look, W, including its brightness relative to the
  sun. Creator intent wins over the white-card rule here.
- **The cast light** (reflection probes, shade) is physical in level, k x W, with the creator's tint. k is the scalar that brings W's
  horizontal irradiance to the physical sky's. Where physics casts no light (k = 0), the cast light is today's ambient.

The filter lives in the sky tables (the sky-view table, the aerial perspective, the probes), never in a post pass, so it is in every
photo path: the snapshot, high-resolution and tiled snapshots, 360 snapshots, and saved and uploaded images. Every stage that draws the
sky checks capture against screen. "Grade" below is the filter's multiplier, G = W / P (the physical sky P times G is W).

The **built-in skies are filters too**: the code default sky (`LLSettingsSky::defaults()`, `llsettingssky.cpp:883`, the `ENV_DEFAULT`
sky and the default day cycle) and the five menu skies of World > Environment (`LLEnvironment::KNOWN_SKY_*`, `llenvironment.cpp:824-828`):
Sunrise, Midday, Midday (Legacy), Sunset and Midnight. They are measured as named rows, outside the library figures (section "Built-in
skies"). AT2-F runs before AT2-1 and reports how close the filter comes to each preset; it does not decide whether R goes ahead.

Offline, CPU only, no viewer change: `scripts/light/at2f_creator_grade.py` (driver) and `scripts/light/at2f_creator_grade.cpp`; the GPU
anchor values come from `scripts/light/at2f_anchor_gpu.py`. Raw rows: `doc/light/atmosphere/at2f-2026-11-17.tsv`; the run's summary:
`doc/light/atmosphere/at2f-2026-11-17.txt`. Rerun on `codex/atmosphere-refresh-2026-11-16` after the review of the first run (55a948fe33).

## What is compared

- **Today's look (W), per draw path.** A C++ twin of `class1/deferred/skyV.glsl` (the Vulkanstorm L3 version) with `skyF.glsl`'s x2
  and clamp 5, then the lighting pass of the sky's own path (`llsettingsvo.cpp:845-865`):

  | Path | Sky | `sky_hdr_scale` | `final_scale` | Tone curve |
  |---|---|---|---|---|
  | classic | no `reflection_probe_ambiance` key: all 768 library skies, the cached Midday (Legacy) and Midnight | 1 (2 only with `RenderSkyAutoAdjustLegacy`, default off) | 1.1 (`softenLightF.glsl`) | none (`tonemap_mix` 0) |
  | PBR | key present, nonzero | 2 sqrt(gamma) | 1 | ACES |
  | PBR, ambiance 0 | key present and 0: the code default (`llsettingssky.cpp:941`) | 1 | 1 | ACES |

  The display used for dE is the path's own (classic: the clamp only; the others: ACES Hill; both sRGB, 8 bit, exposure 1). **The path can
  flip mid-blend**: `llsettingssky.cpp:609` copies the destination's `mCanAutoAdjust` onto the blended sky at once while
  `mReflectionProbeAmbiance` interpolates, so a blend between a classic sky and a PBR sky changes path (and the nonzero-ambiance test)
  partway. The filter must use each frame's own path. The first run applied the PBR path's 2 sqrt(gamma) to every sky, which
  doubled W for the classic library: k, the "x N" and the white-card ratios below are recomputed on the real path.
  The twin sets `sun_moon_blend` to the viewer's band weight (`smoothstep(0.3, -0.3, elevation + dip)`, `llatmosphericlight.h`, dip
  0.14 degrees for the bench's 20 m camera): it is above 0 for 50 of the 612 measured skies (sun elevation below about 0.16 degrees,
  largest 0.168). The first run had it at 0 for every sun elevation of 0 or more.
- **Anchored to the GPU bench** (`--anchor`, `at2f_anchor_gpu.py`; the bench draws the PBR path whatever the sky). W matches the bench
  at the zenith, the horizon (2 degrees up, 90 degrees from the light) and a near-sun pixel (14.75 degrees of azimuth, at the sun's
  elevation) to four decimals for six skies: Default (0.2786, 1.7111, 0.8024), Sunset (0.1421, 0.4221, 2.0457), [TOR] MIDDAY - Salmon
  (sunlight 1.05, 7.7388, 4.5206, 83.7561), [TOR] MIDDAY - Pleasantrie (sunlight 1.47, cloud shadow 1.0, 0.0757, 0.9377, 0.2566),
  Alchemy Immortalis - Fog Lifting (sun at 7.6 degrees, 0.2257, 1.9343, 2.7539) and Foggy (sun at the zenith, 85.932, 1.492, 85.932).
- **The physical sky (P).** `LLScatter::fromLegacy` with the same inputs as `atmosphere_library_check.cpp`, its transmittance,
  multiple-scattering and irradiance tables built, sampled with the slab-split march at 64 steps (D1), at 20 m above the ground. The
  ground term of `sky_radiance` is not evaluated: every direction measured is above the horizon from a camera above the ground (the
  first run's branch for it was dead).
  P is anchored to the bench's physical figures for the same pixels. The bench's physical sky is its own Python mapping (study 4.2),
  not `fromLegacy`, so the two agree only where the mappings do. Default, the mapping's calibration sky: zenith 0.0802 (bench 0.0782),
  horizon 0.1541 (0.1573), near-sun 0.1343 (0.1265), irradiance/pi 0.0977 (0.0962): within 6 %, so k's unit is the bench's. Elsewhere they
  differ, by the model and not by units: Sunset 0.09x the bench's irradiance, Salmon 1.38x, Pleasantrie 0.37x, Fog Lifting 0.86x, Foggy
  2.38x (the bench prints irradiance / pi).
- **The energy factor k.** The scalar that brings W's horizontal irradiance (luminance, cosine-weighted upper hemisphere) to P's. It is
  0 when either is 0. The cast light is k x W; the visible sky is W.
- **The filter tables' grid.** The sky-view table's own grid (Hillaire 2020: 192 x 108, latitude mapped as v = 0.5 + 0.5 sign(l)
  sqrt(|l| / (pi/2))), its upper 54 rows and the 97 columns from the sun's azimuth to the opposite one (both skies are symmetric about
  the sun's vertical plane). The rows are 90 t^2 spaced (about 2.7 degrees at 60 degrees, about 3.3 near the zenith); 1.875 degrees is
  the azimuth step only.
- **The display and dE.** CIE76 dE, weighted by solid angle (1 x 5 degree cells, 3240 per sky). dE 2 is barely visible; above 10 is
  clearly different. These are not comparable one for one with the bench's 13.6 for Default: the bench weights by panorama pixel and
  compares against its Python mapping, not `fromLegacy`.

Scope: the 768 library skies (the legacy library XML, not the built-ins) at each file's own sun elevation. 150 have the sun below the
horizon (night and twilight) and are not measured here: skyV's moon branch and AT1's moon gain need their own pass (AT2-F pass 2).
6 have `max_y` 0 and are undefined (the dome collapses to a point and has no direction; the first run's twin divided by zero and
poisoned its medians). 612 are measured.

## Results (612 skies)

| Measure | Median | p90 | Max | Within dE 2 |
|---|---|---|---|---|
| Physics alone, best luminance gain | 18.62 | 46.65 | 136.80 | 53 |
| Physics alone, best gain and per-channel tint | 8.27 | 16.98 | 100.00 | 67 |
| Filter table, product: mean over the sky | 0.05 | 0.10 | 0.29 | 612 |
| Filter table, product: worst cell | 2.31 | 10.28 | 38.26 | 285 |
| Filter table, ratio (linear): mean | 0.03 | 1.57 | 100.00 | 553 |
| Filter table, ratio (linear): worst cell | 1.90 | 23.77 | 157.30 | 313 |
| Filter table, ratio (log): mean | 0.03 | 1.06 | 100.00 | 553 |
| Filter table, ratio (log): worst cell | 1.25 | 16.38 | 157.30 | 370 |
| Physical sky view, its own table error, mean | 0.03 | 0.06 | 0.83 | 612 |
| Physical sky view, its own table error, worst cell | 1.48 | 6.05 | 18.06 | 346 |

### F1. What the sky-view table holds: the product, not the ratio

The first run's "graded sky view" figure compared W with bilinear(W): it measured the table's sampling and said nothing about the
filter. The run now measures both structures against W at the cell centres (a filter is either stored as the product or as a ratio):

- **Product** (the table stores P x G = W per texel; bilinear in linear). This is the structure the sky-view table has.
- **Ratio** (the table stores G = W / P; G is interpolated, in linear or in log, then multiplied by the exact P at the sample).

Over the sky as a whole the product is within dE 0.05 of today's look (median), against 0.03 for the physical sky's own table error;
none of the 612 has a mean above 2. The **ratio** has the same mean in the median sky (0.03) and is better at the median worst cell
(1.90 against 2.31), because P at the sample is exact there and G is smooth. It fails in the tails: p90 of the worst cell 23.8 (linear)
and 16.4 (log) against 10.3 for the product, a maximum of 157 against 38, and 82 skies with a worst cell above dE 10 (73 in log)
against 65 for the product. In 59 skies the mean is above dE 2 (27 above 10, 7 of them with k = 0): where P is near 0 in a channel, or
0, G explodes and the interpolated ratio times P cannot reproduce W. The worst ratio cells are in [TOR] NIGHT - Twilight rider (157),
[TOR] NIGHT - That spells moon 4 (156), [TOR] SCIFI - Rezzable Hallucinogen (155), [TOR] SCIFI - Lydian dinosaurs (154) and
[TOR] NIGHT - That spells moon 1 and 2 (153, 152); "Phototools- Me Mine Light" has a worst cell of 82 as a ratio against 2.0 as a
product. The ratio's measurement gives it the exact P at the sample; with P through its own table too it would be worse.

**The implementation must store the product, today's look per texel, in the sky-view table.** That is the same filter by construction,
and it avoids the ratio blowing up where P is near 0 in a channel. The cast light (probes, shade) takes k x W from the same table.

The product's own worst cells are near the sun: of the 327 skies whose worst cell is above dE 2, 90 have it inside the 2.56 degree zone
where skyV's `haze_glow = max(1 - dot, 0.001)` is flat (acos(0.999)), 183 between 2.56 and 10 degrees, and 54 beyond 10 degrees;
192 skies have a worst cell above dE 5, against 91 for the physical sky alone. So the flat zone is not the whole story: more than half
of the cells near the sun lie beyond it. The candidates are the `(1 - dot)^glow.z` cusp and the aerosol peak, which a table with rows
2.7 to 3.3 degrees apart and columns 1.875 degrees apart cannot hold. Remedy, planned for **AT2-2** (it is the sky draw): evaluate the
near-sun glow analytically in the sky pass (one `pow`, the same expression), or give the sky view a finer band around the sun.

### F2. Physics alone does not reach the creator's look

With today's mapping (`fromLegacy`), 53 of 612 skies (9 %) are within dE 2 of the creator's sky after the best exposure, and 67 (11 %)
after the best exposure and tint. Pure rebasing (the alternative the owner set aside on 2026-11-17) would therefore change the look of
about nine skies in ten. The physics-alone figures are higher than the first run's because the classic display has no tone curve and
the first run's medians were poisoned by the undefined skies (their values on the 613 finite skies of that run were median 16.96 and
7.86). A fitted mapping might get closer and has not been tried (proposed next stage, AT2-F2).

### F3. Energy: the creator's skies hold more light than physics gives them

k is the factor that takes the creator's sky to the physical sky's light on the ground, on each sky's own draw path. All 612 library
skies draw classic. For the 577 with k > 0: median k 0.0722 (p10 0.0036, p90 0.4504), so the creator's sky holds a median **x13.8**
the physical light of the same sun (p10 x2.2, p90 x275); 339 skies have k below 0.1, 94 below 0.01, 26 below 0.001. The library's Default
is k 0.3138 (x3.2); Foggy 0.0284 (x35); Sunset 0.0517 (x19). (The first run's median 0.0328 and "x30" used the PBR path's doubled W.)

**The 41 skies with k = 0 or undefined** (of 618 day skies), classified from the inputs. The flags overlap: 21 have sunlight 0, 10 have
`density_multiplier` 0, 6 have `max_y` 0, and 10 have none of the three. Partitioned by what the physical and today's skies show:

| Skies | What |
|---|---|
| 6 | `max_y` 0: undefined, no direction; not measured |
| 20 | today's sky is black too (W = 0): 7 with `density_multiplier` 0, 2 with that and no sunlight, 6 with no sunlight, 5 with `blue_horizon` and `haze_horizon` both 0 (Lunar Morning 7, White Fire Sky 01-03, [TOR] HORROR - Asian red). Nothing to filter; the cast light is today's, also black |
| 15 | today's sky is lit and the physical sky is black: 10 with no sunlight (the creator lit the sky with `ambient` alone) and 5 with sunlight where the mapping leaves no scattering (4 of the 5 have `blue_horizon` 0: Phototools- B/W Light 11, [TOR] BIG SUN - Eats night, [TOR] SCIFI - Conciergist, [TOR] SPECIAL - Blacklight; [TOR] SCIFI - Scanning aliens has `haze_horizon` 0) |

The first run's split "21 with no sunlight, 7 with no atmosphere, 13 by design" did not partition the 41, and its "by design" claim
for the 13 is not shown by this data: the 15 above are the real "W > 0, P = 0" cases, and for them (and for the 10 with no sunlight)
the filter has nothing to multiply, so the cast light is today's ambient, as decided. The first run's Conciergist figures (red column
0.0041, ozone 0.0049) are not reproduced here and are dropped.

**Owner decision (2026-11-17).** Each EEP sky is a filter on the physical sky and the result for the visible sky is the same as today,
including its brightness relative to the sun. The cast light stays physical in level with the creator's tint (decision D4's deeper shade
stands); where the physical sky casts no light, the cast light is today's, from the creator's ambient.

### F4. The white-card rule on the visible sky and on the cast light

The rule of 2026-11-11: outside 20 degrees of the sun a sky radiance of at most lum(E_sun) / pi; from 10 to 20 degrees at most 2x; inside
10 degrees excluded. Two counts, because the owner's decision splits the sky in two:

| Quantity | Over the rule | Aureole 2x allowance only (not already counted) | Largest outside the aureole / inside |
|---|---|---|---|
| **W, the visible sky** (the reach of "creator intent wins"), all 612 | 367 | 44 | 5701 / 18050 |
| W, the 577 skies with k > 0 | 364 | 44 | the same |
| **k x W, the cast light** (the physical-in-level light), the 577 skies with k > 0 | 33 | 24 | 4.18 / 3.62 |

So the white-card rule, if it applied to the visible sky, would clip 364 of the 577 skies with k > 0 on today's look (Default's W is
1.89x the card outside the aureole; the first run's 466 and 3.4x used the PBR path's doubled W). Under the decision it does not: it bounds the physical sky and
the cast light only. On the cast light, 33 skies (5 %) are over the rule, up to 4.18x the card outside the aureole ([TOR] SCIFI - Big
pink) and 3.62x inside it (Places Las Legunas); a further 24 (the first run said 29; 5 of those were already among the 33) use only
the aureole's 2x allowance. The full list is in the run's summary.

### F5. The haze on objects cannot be graded by a ratio alone

The filter reaches the haze on objects by multiplying the aerial perspective's in-scatter, so there it acts as a ratio, W / P. Where
the physical sky has almost no light in a channel the ratio explodes: 68 skies are colour-starved (a channel below 1e-3 of the
strongest over most of the sky, P not 0) and for 35 more P is exactly 0 over most of the sky (the first run did not count those).
The 1e-3 is a chosen threshold, not derived (about 10 stops below the strongest channel); so is the first run's 32x bound (5 stops)
on the ratio, which has no derivation either. The share of W's light that a ratio bounded at B times the sky's median W / P cannot
carry (`ap_remainder`; it uses W / P without k, since k scales W and the median alike) depends on B, so the sensitivity is reported:

| Bound B | Median | p90 | Skies above 1 % | Above 10 % |
|---|---|---|---|---|
| 8x | 0.0016 | 0.744 | 244 | 162 |
| 32x | 0.0000 | 0.571 | 147 | 107 |
| 128x | 0.0000 | 0.188 | 91 | 71 |

The aerial perspective therefore needs a ratio and an additive term (two tables), or another approach; the sky-view table does not
(F1: it stores the product). The size of the filter: over the 575 skies where W and P are both lit, the range of G in luminance is
x11.6 (median), x273 (p90), up to x7.9e6; in 27 skies more than 10 % of the sky has W lit and P not, so no ratio exists there.
The largest per-channel tint of G, over the channels above 1e-3 of the luminance in both W and P (the first run's 1e12 and 4e7
figures were floor artefacts of dividing by 1e-12), is median x3.4, p90 x29, max x1760. Not yet measured: the filtered haze against
today's object haze (`calcAtmosphericVarsLinear`), which is a different formula from the dome (AT2-F pass 2).

### F6. What the cast light does to shade on walls

The cast light holds the light on flat ground at the physical value (decision D4's deepening stands), but redistributes it. A vertical
wall facing the sun gets 1.37x the physical sky light (median over the 577 skies with k > 0; p10 0.94, p90 2.79); a wall facing away
1.02x (p10 0.36, p90 2.06).

## Built-in skies

Named rows, outside the library medians. Same tool and same dE; k is on each sky's own path.

| Sky | Source | Sun | Path | k (x N) | Physics alone, gain / tint | Product, worst cell | Ratio (lin / log), worst cell | White card on W / k x W (outside / aureole) |
|---|---|---|---|---|---|---|---|---|
| Code default | `LLSettingsSky::defaults()` through the getters' fallbacks (`llsettingssky.cpp:1228-1234`: no `legacy_haze`, ambient 0.25) | 80 degrees (position 0) | PBR, ambiance 0 | 1.0946 (x0.9) | 18.1 / 11.7 | 7.3 at 2.5 degrees | 4.4 / 5.2 | 0.445 / 0.075; 0.487 / 0.082 |
| Midday (Legacy) | local asset cache, `6c83e853-e7f8-cad7-8ee6-5f31c453721c` | 90 degrees | classic | 0.3274 (x3.1) | 19.9 / 16.5 | 6.3 at 2.5 degrees | 3.4 / 4.2 | 1.823 / 0.449; 0.597 / 0.147 |
| Midnight | local asset cache, `8a01b97a-cb20-c1ea-ac63-f7ea84ad0090` | -90 degrees | classic | not measured: a night sky, pass 2 | | | | |
| Sunrise, Midday, Sunset | not in the local cache | | | need a one-off fetch in a later owner session; nothing was fetched for this study | | | | |

- The code default's k is above 1: on its path (`sky_hdr_scale` 1, no `final_scale` 1.1) its visible sky is dimmer than the physical sky
  lit by its sun, so k x W is brighter than the sky drawn. The first auditor's figure for it, k 0.547 and gain 1.75, was the PBR
  path's doubled W; the real path gives 1.09 and gain 0.877.
- The library's "Default" (k 0.3138) is the Midday (Legacy) look (k 0.3274 as the cache holds it), not the code default.
- The built-in skies' own sun elevations: the code default's sun is at 80 degrees by `defaults(position = 0)`; the cached skies' sun
  rotations point straight up (Midday (Legacy)) and straight down (Midnight). Both cached assets are read through a small LLSD notation
  reader in the driver; no network access.

## Limits of the twin and the tool

- **Camera height.** The bench and the twin put the Windlight dome at camera height 0 (`camPosLocal` 0) and the physical sky at 20 m;
  the viewer's camera is at the avatar's height, and the dome is placed with the camera. The sky's horizon dip and the haze change with it.
- **Dome convention.** The twin's dome is the bench's `dir x 15000` with the `max_y` clamp and the below-horizon plane at 32000 m; the
  viewer's dome mesh is the same shape but is not rendered here.
- Exposure is 1 and the sky's moon colour is the sun's (the bench: `moonlight_color` = `sunlight_color`); the band's swing is the default
  0.3 degrees and its dip comes from 20 m. A sky's own moon colour changes the 0.7 handover only inside the band.
- The tone curve is ACES Hill on the non-classic paths (`RenderTonemapType` default is not read); sRGB, 8 bit.
- Sun at the horizon or below: the 150 night and twilight skies are not measured (pass 2); the white card uses the physical sun's
  irradiance at the ground.

## Not covered yet (AT2-F pass 2, before AT2-3)

- The 150 night and twilight skies and the moon branch; Midnight.
- Sunrise, Midday and Sunset of the menu (a one-off fetch).
- Day cycles (keyframe blending of the filter, including a path flip mid-blend).
- The haze on objects against today's object haze (F5).
- A fitted mapping (AT2-F2), which would shrink the filter and might move F2, F3 and F5.

## Where each piece lands

- **AT2-1:** the filter tables, built per preset load and per keyframe (not in section 6's per-frame 0.07-0.11 ms of the study), with
  the C++ twin of skyV and its vectors; the sky-view table stores the product (F1).
- **AT2-2:** the sky and the probes drawn from them; the near-sun glow evaluated analytically (F1); the capture-path checks (every
  photo path against the screen).
- **AT2-F pass 2**, then **AT2-3** (the aerial perspective with its additive term, F5), **AT2-4** and **AT2-5**, which keeps the
  Windlight formula as the filter's reference.
- GPU cost of computing the filter: the old sky formula on the sky-view grid once per preset load and keyframe; AT2-3 times it.

## Reproduce

    python scripts/light/at2f_creator_grade.py --anchor       # the twin against the GPU bench (reference values from at2f_anchor_gpu.py)
    python scripts/light/at2f_creator_grade.py --quick        # every 8th library sky by name, with --builtins about 50 s
    python scripts/light/at2f_creator_grade.py --builtins --out rows.tsv   # all 768 skies and the built-ins, about 6 minutes
    python scripts/light/at2f_creator_grade.py --from rows.tsv --builtins  # summarise a saved run without measuring again
    python scripts/light/at2f_anchor_gpu.py                   # the anchor's reference values; needs a GPU, with no viewer or build running
