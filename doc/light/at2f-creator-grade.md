<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# AT2-F: the creator grade, measured over the shipped library

Stage AT2-F of redesign R (decided by the owner on 2026-11-17; see the decision page and `doc/light/atmosphere-study.md`): "physics
sets the light, the creator sets the look". The drawn sky is the physical sky (AT2-0's `LLScatter`, `llatmosphericlight.h`) multiplied
by a per-preset grade, which reproduces the creator's Windlight gradient and colour while the sky's light on a horizontal surface stays
at the physical value. The owner accepted that the drawn sky is then not pure Hillaire output, to protect creator intent. AT2-F runs
before AT2-1 and reports how close the graded sky comes to each preset; it does not decide whether R goes ahead.

Offline, CPU only, no viewer change: `scripts/light/at2f_creator_grade.py` (driver) and `scripts/light/at2f_creator_grade.cpp`.
Raw rows: `doc/light/atmosphere/at2f-2026-11-17.tsv`; the run's summary: `doc/light/atmosphere/at2f-2026-11-17.txt`. Base:
`codex/atmosphere-refresh-2026-11-16` at 55a948fe33.

## What is compared

- **The creator's look (W).** A C++ twin of `class1/deferred/skyV.glsl` (the Vulkanstorm L3 version, day branch) with `skyF.glsl`'s
  x2 and clamp 5 and the PBR path's decode x `sky_hdr_scale` (2 sqrt(gamma)), with the uniforms exactly as the GPU bench sets them
  (`bench_atmosphere.py`, `set_today_uniforms`). It is anchored to that bench's figures at the bench's own pixel directions
  (`--anchor`): Default zenith 0.2786 (bench 0.279) and horizon 1.7111 (1.711); Sunset 0.1421 (0.142) and 0.4221 (0.422).
- **The physical sky (P).** `LLScatter::fromLegacy` with the same inputs as `atmosphere_library_check.cpp`, its transmittance,
  multiple-scattering and irradiance tables built, sampled with the slab-split march at 64 steps (study decision D1, recommended),
  at 20 m above the ground.
- **The energy factor k.** The scalar that brings W's horizontal irradiance (luminance, cosine-weighted upper hemisphere) to P's.
  The graded sky is k W wherever the grade is evaluated.
- **The grade's grid.** The sky-view table's own grid (Hillaire 2020: 192 x 108, latitude mapped as v = 0.5 + 0.5 sign(l)
  sqrt(|l| / (pi/2))), its upper 54 rows and the 97 columns from the sun's azimuth to the opposite one (both skies are symmetric
  about the sun's vertical plane). The table holds P x G = k W per texel, so the drawn sky is the creator's sky through the table's
  bilinear filter.
- **The display.** The GPU bench's transform (ACES Hill, sRGB, 8 bit) and CIE76 dE, weighted by solid angle (1 x 5 degree cells,
  3240 per sky). dE 2 is barely visible; above 10 is clearly different. These dE are not comparable one for one with the bench's
  13.6 for Default: the bench weights by panorama pixel and compares against its Python mapping, not `fromLegacy`.

Scope: the 768 shipped Windlight skies at each file's own sun elevation. 150 have the sun below the horizon (night and twilight) and are
not measured here: skyV's moon branch and AT1's moon gain need their own pass. 618 are measured.

## Results (618 skies)

| Measure | Median | p90 | Max | Within dE 2 |
|---|---|---|---|---|
| Physics alone, best luminance gain | 9.01 | 37.83 | 127.38 | 66 |
| Physics alone, best gain and per-channel tint | 7.54 | 15.18 | 80.28 | 76 |
| Graded sky view, mean over the sky | 0.05 | 0.06 | 0.25 | 613 |
| Graded sky view, worst cell | 1.15 | 5.60 | 36.15 | 406 |
| Physical sky view, its own table error, mean | 0.03 | 0.07 | 0.57 | 613 |
| Physical sky view, its own table error, worst cell | 1.11 | 3.91 | 15.01 | 428 |

### F1. The grade reproduces the creator's sky in the dome and the probes

Over the sky as a whole the graded sky view is within dE 0.05 (median) of the creator's sky, against 0.03 for the physical sky's own
table error. The grade adds no error of its own beyond the sky-view table's sampling.

The worst cells are near the sun. Of the 212 skies whose worst cell is above dE 2, 178 have it within 10 degrees of the sun (72 within
3 degrees); 79 skies have a worst cell above dE 5, against 26 for the physical sky alone. The cause is in skyV: `haze_glow =
max(1 - dot, 0.001)` holds the glow flat within acos(0.999) = 2.56 degrees of the sun, a kink that a 1.875 degree table cannot hold.
Remedy for AT2-1: evaluate the creator's near-sun glow analytically in the sky pass (one `pow`, the same expression), or give the sky
view a finer band around the sun.

### F2. Physics alone does not reach the creator's look

With today's mapping (`fromLegacy`), 66 of 618 skies (11 %) are within dE 2 of the creator's sky after the best exposure, and 76 (12 %)
after the best exposure and tint. Pure rebasing (the alternative the owner set aside on 2026-11-17) would therefore change the look of
almost nine skies in ten. A fitted mapping might get closer and has not been tried (proposed next stage, AT2-F2).

### F3. Energy: the creator's skies hold far more light than physics gives them

k is the factor that takes the creator's sky to the physical sky's light on the ground. Median 0.033: the creator's sky is about 30x
the physical sky lit by the same sun (Default 0.17, about 6x). 453 skies (73 %) are below 0.1, 178 (29 %) below 0.01, 79 (13 %)
below 0.001, and 41 (7 %) are exactly 0: the physical model gives them no sky light at all, so under R as decided their drawn sky is
black. The 41 split into:

- 21 with no sunlight (`sunlight_color` 0): the creator lit the sky with `ambient` alone;
- 7 with no atmosphere (`density_multiplier` 0);
- 13 where the mapping leaves no scattering, by design: `blue_horizon` 0 sets the Rayleigh albedo to 0, and
  `LLAtmosphere::fromLegacy` keeps each channel's column exactly as today's sunlight reaches the ground at noon before fitting Earth's
  ozone and a grey aerosol inside it; when ozone fills a thin channel's whole column (CLAMP_OZONE), a grey aerosol has no room in any
  channel. The creator's sunlight wins over the sky's scattering. Example: `[TOR] SCIFI - Conciergist` (its red column is 0.0041,
  Earth's ozone alone would be 0.0049 in red; the aerosol extinction is 0).

**Owner decision (2026-11-17).** The principle is creator intent for every EEP, with each EEP sky a filter on the physical sky
whose result is the same as today. The sky you see keeps today's look, including its brightness relative to the sun, and creator
intent wins over the white-card rule (F4). The light the sky casts stays physical in level with the creator's tint (decision D4's
deeper shade stands); where the physical sky casts no light, as for these 41 skies, the cast light is today's, from the creator's
ambient. The mapping behind the 13 agrees with that principle and stays as it is.

### F4. The white-card rule clips 33 graded skies

On the graded sky, 33 skies (5 %) break the rule of 2026-11-11: outside the aureole up to 4.18x the card (`[TOR] SCIFI - Big pink`),
inside it up to 3.62x (`Places Las Legunas`). A further 29 use the aureole's 2x allowance. The owner's decision of 2026-11-17 keeps today's look for them: under the
filter rule creator intent wins, and the white-card rule bounds the physical sky and the light it casts. The full list is in the run's summary.

### F5. The haze on objects cannot be graded by a ratio alone

The grade reaches the haze on objects by multiplying the aerial perspective's in-scatter, so it acts there as a ratio, W / P. Where the
physical sky has almost no light in a channel the ratio explodes: 68 skies are colour-starved (a channel below 1e-3 of the strongest
over most of the sky). Bounding the ratio at 32x the sky's median grade, the share of the graded sky's light the ratio cannot carry
is above 1 % for 148 skies (24 %) and above 10 % for 106. The aerial perspective therefore needs a ratio and an additive term (two
tables), or another approach; the sky view does not (F1). Not yet measured: the graded haze against today's object haze
(`calcAtmosphericVarsLinear`), which is a different formula from the dome.

### F6. What the grade does to shade on walls

The grade holds the light on flat ground at the physical value (decision D4's 16 % deepening stands), but redistributes it. A vertical
wall facing the sun gets 1.33x the physical sky light (median; p10 0.73, p90 2.68); a wall facing away 0.98x (p10 0.08, p90 1.98).

## Not covered yet

- The 150 night and twilight skies.
- Day cycles (keyframe blending of the grade).
- The haze on objects against today's object haze (F5).
- A fitted mapping (AT2-F2), which would shrink the grade and might move F2, F3 and F5.
- GPU cost: computing the grade is the old sky formula on the sky-view grid once per keyframe; AT2-3 times it.

## Reproduce

    python scripts/light/at2f_creator_grade.py --anchor      # the twin against the GPU bench
    python scripts/light/at2f_creator_grade.py --quick       # every 8th sky, about 30 s
    python scripts/light/at2f_creator_grade.py --out rows.tsv  # all 768 skies, about 3.5 minutes
