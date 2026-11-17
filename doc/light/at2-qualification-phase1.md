<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# AT2 qualification, phase 1: can a table hold upstream's sky? (offline)

Owner decision (2026-11-17): "today's look would be the sky under PR #543 rules, after AT1 and AT2 land and EEP applied as filters on top of
the physical sky." The owner's rule for the design: "the simplest path with best performance", and simplicity is never traded for a worse result.
This phase measures, offline and on the CPU only, **whether a table in the sky pass can hold upstream Firestorm's sky pass** (the reference creators
tuned against), in several table variants. The physical sky P never enters the comparison: a table that stores the product (physical x filter =
reference) holds the additive part where P = 0 as well, so the question is purely one of tabulating upstream's sky. **The worst cell is the
criterion.** The mean dE has no discriminating power (largest mean in any variant: 0.28), so mean <= 2 is reported only as a sanity check, with a
view-weighted mean (elevation 0 to 40 degrees, where the eye looks) beside it. No viewer code or build is involved; phase 2 (in the viewer) is
listed at the end.

**Scope.** The legacy library: 768 skies, all classic (no `reflection_probe_ambiance` key). PBR skies with a nonzero ambiance have no rows here;
the code default (`LLSettingsSky::defaults()`) is the only PBR-ambiance-0 sky, and the cached Midday (Legacy) and Midnight are classic. 9 skies have
`max_y` 0 and are undefined (see Anchor); 759 are measured: 612 day, 147 night.

## Relation to #543 (merged into vkstorm-devel)

#543 (`doc/light/atmosphere-study.md` section 8, `doc/light/at2f-creator-grade.md`) took our `skyV.glsl` as the reference ("today's look W").
**This document amends that, by the owner's decision of 2026-11-17: the reference is upstream's sky pass.** Our shaders have drifted from upstream's
(L3's handover band, the night-sky port), and AT2-F's W twin was built from OUR skyV. Two numbers differ in kind from AT2-F's: the cells here are 0.5 x 2.5
degrees (AT2-F: 1 x 5), and the display chain is upstream's own (below); AT2-F's committed run (`at2f-2026-11-17.*`) was measured with the older display and is not
regenerated (the shared `display_lab` in `at2f_creator_grade.cpp` now has the corrected chain, which moves only its code-default row).

Per the owner: **for the upstream reference, follow upstream; for our model, follow our model.** The reference keeps upstream's instant day / night switch;
ours keeps the L3 blend. The band differences are accepted, not failures (the owner: it looks better), and are listed apart.

## Method, with the technique behind each part

- **The reference U: a C++ twin of upstream's sky pass** (`upstream/master` `bd03bafe69`). `skyV.glsl` only passes the position on; `skyF.glsl` is the
  whole sky, per pixel (Linden Lab's Windlight / EEP haze model: off-axis sunlight attenuation, blue and haze weights, haze glow `(1 - dot)^glow.z`, the cloud-shadow
  "below cloud" blend, x2 and clamp 5). Then the lighting pass for a sky pixel (`class3/deferred/softenLightF.glsl`, `GBUFFER_FLAG_SKIP_ATMOS`): `srgb_to_linear`,
  x `sky_hdr_scale`, x `final_scale` 1.1 in classic mode, `clampHDRRange` 0..11.2, with the path logic of upstream's `llsettingsvo.cpp`. Upstream's day / night branch:
  `sun_up_factor` (the sun when its elevation is 0 or more, else the moon, opposite the sun in a library sky), `moonlight_color x 0.7`, and `sun_moon_glow_factor` (1 by
  day; by night `moon_brightness x 0.25`, below 1 the glow is dropped: the library has no `moon_brightness`, default 0.5, so night has no glow). There is no band upstream.
- **The display chain is upstream's** (`pipeline.cpp` 7885-7915, `postDeferredTonemap.glsl`, `llsettingsvo.cpp:868`): a sky with `reflection_probe_ambiance` 0
  (every classic sky, set at `llsettingssky.cpp:1177-1181`, and the code default) takes the NO_POST path plus `legacyGamma`: clamp 0..1, `linear_to_srgb`, then
  `c = 1 - (1 - c)^gamma` with the sky's own gamma. Only a nonzero ambiance takes the ACES tone map (Hill's fit of the RRT and ODT; exposure 1). 8-bit sRGB, then
  CIE76 dE (CIE 15), **weighted by solid angle** over 0.5 x 2.5 degree cells (180 x 72 above the horizon, azimuth 0 to 180 by symmetry). The first draft of this phase used
  no gamma and ACES for the code default; its figures are superseded (the table below gives old and new).
- **The table.** Hillaire's sky-view layout (Hillaire 2020, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", Computer Graphics Forum 39(4)):
  192 x 108, latitude mapped `v = 0.5 + 0.5 sign(l) sqrt(|l| / (pi/2))`; the 54 upper rows and the 97 columns from the light's azimuth to the opposite one
  (the AT2-F layout), sampled bilinearly. Variants:

  | | Stores | Per pixel |
  |---|---|---|
  | **a** | the product, final linear (the #543 / AT2-F structure) | bilinear |
  | **b** | the coded value `min(2 pre, 5)` (what skyF writes, still sRGB-encoded) | bilinear, then `srgb_to_linear` x hdr |
  | **c** | two tables: A = the pre-clamp colour at `haze_glow` 0 and B = its slope in `haze_glow` (skyF's colour is exactly linear in `haze_glow`) | bilinear A and B, `A + B haze_glow` with the exact glow (one `pow`), x2, clamp 5, `srgb_to_linear`; no zone, no blend |
  | **d (ns5, ns10, ns20)** | variant a plus an N-degree zone with variant c's glow, blended out by 1.5 N degrees (smoothstep) | for comparison only |

  A **self-check** asserts `A + B haze_glow == pre` at every table node and at sample cells of every sky: the largest relative error is 1.4e-7 (B is a finite
  difference, so a sky with a huge haze_glow loses about 1e-8 to rounding; the limit is 1e-6, and a run above it stops).

## Anchor: the upstream twin against upstream's own shader on the GPU

`scripts/light/at2_qual_anchor_gpu.py` reads upstream's `skyF.glsl` and `srgbF.glsl` at the pinned `bd03bafe69195460df8023ae9f9487a9031ae010` with `git show` into a scratch
folder, compiles them in the bench's hidden OpenGL 4.6 context (NVIDIA 621.14, RTX 5070 Ti) and draws one pixel per direction with the uniforms upstream's `llsettingsvo.cpp`
sets; the header of `anchor-gpu.tsv` records the SHA, GPU and driver. `scripts/light/at2_qual.py --anchor` compares the twin to it (no GPU needed to rerun). **11 skies,
7 directions each (77): zenith; the horizon, 2 degrees up at 90 degrees from the light; 0.5 degrees up (below 2 degrees); mid sky; 14.75 degrees from the light at its elevation;
10 degrees from the light; and 0.4 degrees from the light's core. Largest difference 4e-6; 77 of 77 match to 4 decimals.** The skies: Default, Sunset, Foggy (light at the
zenith), [TOR] MIDDAY - Salmon (sunlight 1.05), [TOR] MIDDAY - Pleasantrie (sunlight 1.47), Alchemy Immortalis - Fog Lifting, **two real night skies** ((SS) Atmos 02:00 2, sun
at -60 degrees, and A-12AM, sun at -90), **two skies with glow.z > 0** (Phototools- Yellow Stars 01, glow.z 10, and Phototools- rara Fall Home Light, 0.4) and one with `max_y` 0.
9 of the 77 directions saturate at 5.0; the other 68 do not (the first draft's anchors were mostly saturated or low-information; the "night" sky it used was a day sky at 34 degrees).

**`max_y` 0** (9 library skies): `rel_pos` collapses to a point, `normalize(0)` is NaN in the shader, and `clampHDRRange` (softenLightF) maps NaN to 0. The GPU (upstream's
skyF, [TOR] SPECIAL - Puur) gives 0 in every direction. The twin outputs black for them; they are listed, not counted as measured skies.

The lighting pass's own arithmetic (x `sky_hdr_scale`, x 1.1, clamp 11.2) and the display chain are plain arithmetic, checked by reading upstream's code, not on the GPU.

## Results (759 skies)

Worst-cell passes, old (first draft: no gamma, ACES for the code default) to new (upstream's display chain), over the 759 with the sign rule at the horizon:

| Variant | Worst <= 5: old | **new** | Worst <= 2: old | **new** | Mean <= 2 (new) | Worst cell median / p90 / max (new) | View-weighted mean (el 0-40) median / p90 / max (new) |
|---|---|---|---|---|---|---|---|
| a: product table | 559 | **608** | 425 | **444** | 759 | 1.34 / 9.97 / 85.8 | 0.046 / 0.127 / 0.273 |
| b: coded-domain table | | **634** | | **477** | 759 | 1.06 / 6.92 / 84.2 | 0.035 / 0.079 / 0.220 |
| **c: whole-sky A/B** | | **749** | | **735** | 759 | 0.82 / 1.16 / 84.2 | 0.028 / 0.064 / 0.210 |
| ns5 | 676 | **691** | 582 | **596** | 759 | 0.98 / 4.47 / 85.8 | 0.044 / 0.122 / 0.272 |
| ns10 | 730 | **732** | 679 | **678** | 759 | 0.86 / 2.03 / 85.8 | 0.039 / 0.116 / 0.269 |
| ns20 | 751 | **747** | 725 | **717** | 759 | 0.84 / 1.36 / 85.8 | 0.035 / 0.103 / 0.269 |

Away from the horizon (701 skies; the 58 with the sun at the horizon are below): worst <= 5 / <= 2: a 560 / 410, b 584 / 438, **c 696 / 691**, ns5 639 / 555, ns10 679 / 635, ns20 694 / 674.

### Recommendation: variant c

**Variant c is the simplest variant with the best result**: two tables, one `pow` per pixel, the sky's own arithmetic after the lookup, no zone, no blend, no special case.
749 of 759 pass worst-cell dE 5 (735 pass dE 2), against 747 / 717 for the best zone variant (ns20), which needs c's glow evaluation anyway plus the zone, the blend and a
third table read. The diagnosis (corrected from the first draft): the gain does **not** come from the clamp-5 edge. It comes from **interpolating in the coded domain** (a to b:
608 to 634 within dE 5) **plus the exact glow** (b to c: 634 to 749); the glow cusp `(1 - dot)^glow.z` is what a table cannot hold, and the A / B split evaluates it exactly. There is
no "clamp after the lookup" proposal: variant c already clamps per pixel, after the lookup, because it stores the pre-clamp colour.

**What remains with variant c (10 of 759):** the five away from the horizon (Surreal - Flirt (Paulina) 84.2, Surreal - Brazil (Paulina) 44.0, [TOR] DUSK - Fairytale glow 10.0,
[TOR] SCIFI - Ramshagguld 2 7.9, Ramshagguld 1 7.3) all have **gamma 0.07 to 0.54** (the library median is 1.01): `legacyGamma`, `1 - (1 - c)^gamma`, is infinitely steep as a
bright channel approaches 1, so a tiny error in a bright, low-gamma sky is amplified. The other five are skies with the sun at 0 degrees in the day branch (next section). The legacyGamma
display is part of the reference, so this is real; variant a has the same limit.

### The 58 skies with the sun at the horizon (|elevation| <= 0.0011 degrees)

For these the day / night branch is float rounding: upstream decides with the float32 quaternion's z, and a sun at 0.0000 or -0.0003 degrees flips it (**numeric limit: below about
0.002 degrees the branch is not determined by the data**). They are measured in both branches (day: the sun up, glow on; night: the moon, no glow) and listed apart; the library has
nothing between 0.0011 and 0.36 degrees, so L3's band (0 to about 0.17 degrees) is only seen at its outer edge. Worst-cell passes, of 58:

| Variant | Day branch <= 5 / <= 2 | Night branch <= 5 / <= 2 |
|---|---|---|
| a | 46 / 31 | 58 / 57 |
| b | 48 / 36 | 58 / 58 |
| c | 52 / 42 | 58 / 58 |

Night is trivial (no glow). The day branch fails in six skies for variant c: Places Beach Cay 50.4, Anime - 6AM 27.1, [TOR] SCIFI - Rainbowtek 21.8, Bryn Oh's Rusted Gears 12.2 (gamma 0.2),
[TOR] SCIFI - Grit & shame 8.6, [TOR] FOGGY - Das fog 6.3 (the glow core at the horizon rows, 1.3 to 1.5 degrees from the light at elevation 0.2 to 0.8). The headline counts above use the sign
rule (the sun at -0.0003 is night, and Anime - 6AM, at -0.0000, is night too, so it passes); under it variant c passes 749 and fails ten: the five away from the horizon, and five of the day-branch failures above (Places Beach Cay, Rainbowtek, Bryn Oh's Rusted Gears, Grit & shame, Das fog).

### Day-cycle blending: rebuild the tables, do not blend them

M is a sky with two keyframes' parameters lerped (the viewer's `LLSettingsSky::blend` lerps the colours and scalars; the sun's elevation is lerped here), t = 0.25, 0.5, 0.75, over five pairs
(A-6AM to A-12PM, A-12PM to A-6PM, A-6PM to A-12AM, A-12AM to A-6AM, Default to Sunset; 15 blends). The reference is upstream's formula at M. Worst cell (dE):

| Method | Median | Max | Within dE 5 |
|---|---|---|---|
| lerp of the two a tables | 66.8 | 114.9 | 0 of 15 |
| lerp of the A and B tables, with M's exact glow | 30.0 | 39.0 | 0 of 15 |
| **M's own c tables (rebuilt)** | **0.8** | **1.2** | **15 of 15** |

The sky is nonlinear in its parameters (the sunlight's `exp(-atten / elevation)`, the density's `exp`), so a lerp of keyframe tables is not the lerp of the parameters: **AT2 must rebuild the tables
for the blended sky** (per blend step, not per frame if the blend is slow; table build cost is the AT2-1 measurement) rather than blend tables.

### Built-ins

| Sky | Path | Sun | a: mean / worst | b | c | ns20 |
|---|---|---|---|---|---|---|
| Code default (`LLSettingsSky::defaults()`) | PBR, ambiance 0 (classic display with legacyGamma) | 80 deg | 0.07 / 4.8 | 0.05 / 5.9 | **0.03 / 1.0** | 0.05 / 1.0 |
| Midday (Legacy), cache 6c83e853 | classic | 90 deg | 0.08 / 7.9 | 0.09 / 10.4 | **0.05 / 0.8** | 0.05 / 0.8 |
| Midnight, cache 8a01b97a | classic | -90 deg | 0 / 0 (black at 8 bit) | | | |

Sunrise, Midday and Sunset of the Environment menu are not in the local cache: **pending a one-off fetch** (an owner session).

## Geometry: the table must be indexed by the shader's `rel_pos` direction

The viewer does not draw the bench's dome. `lldrawpoolwlsky.cpp:303-308` and `llvowlsky.cpp` draw a **pi/8 cap of radius R = 15000 seen from y = 0.96 R**, and skyF adds **50 m** to `rel_pos` in y; the
vertex is at a distance rho from the camera that runs from 600 m at the zenith to about 4200 m at the horizon (the bench used 15000 m for every direction). The sky depends on the direction of `rel_pos`
only (the `max_y` clamp fixes its length), so **the AT2-2 table must be indexed by `normalize(rel_pos)` computed as skyF computes it, not by the screen's view direction**: on the same view directions,
the bench's sky differs from the viewer dome's by a mean dE median 1.20 (p90 2.17, max 8.58), 105 of 701 skies above mean 2, worst cell above dE 5 in 442. The rim of the cap is about 4.9 degrees below the
horizon and what is below is untested here (the ground hides most of it); the below-horizon plane at 32000 m is not measured.

## Known differences: our current shaders against upstream (accepted, never counted above)

Same cells, our current twin (the AT2-F twin of our skyV/skyF, with the L3 handover band, `smoothstep(0.3, -0.3, elevation + dip)`, `llatmosphericlight.h`) against the upstream twin. The owner accepts the
band: for the upstream reference follow upstream; for our model follow our model; it looks better.

| Group | Skies | Differ by more than dE 0.5 | Worst-cell dE |
|---|---|---|---|
| Band weight 0 (sun above about 0.17 degrees) | 562 | 0 | 0 |
| Band weight 1 (night) | 139 | 0 | 0 |
| 0 < weight < 1 (the 58 skies with the sun at the horizon) | 58 | 51 | median 14.7 over the 51 (11.8 over the 58), max 82.6 |

The band is the **only** numeric difference of the day and night branches. Not numerically measured and therefore only listed: the night sky (stars, Milky Way), the moon's halo and rainbow (zero here:
moisture and ice levels are 0), and `RenderSkyAutoAdjustLegacy` (default off).

**Per-vertex haze is the Firestorm 7.2.5 inheritance, not our drift.** Release and devel evaluate the haze per dome vertex in `skyV.glsl` and interpolate it; upstream moved the evaluation to the pixel in
`b9727dd841` (in canary and Firestorm 7.3.0 beta). With the real dome tessellation (`WLSkyDetail` 64: 64 stacks x 128 slices, `calcPhi`, interpolating the vertex colours across the triangles the strip draws)
and the viewer's geometry on both sides, **today's release / devel look against upstream's per-pixel sky** (701 skies away from the horizon) is: mean dE median 0.06 (max 0.40), worst cell <= 5 in 450
(sun on a slice vertex) or 487 (half a slice off) of 701, <= 2 in 344 / 349, worst cell median 2.0, max 98.9. AT2-2 draws the sky from the table, which removes it.

## Limits

- Camera at the dome's origin, 15000 m radius for the bench cells; the viewer's dome geometry is modelled for the comparison above (not rendered). Above the horizon only.
- Exposure 1, 8-bit sRGB: the night skies (Midnight: brightest channel 0.003) are below 1/255 over most of the sky, so dE hides relative error there; an exposure sweep is phase 2's.
- No clouds, no stars, no sun disc, no rainbow, no halo; the sky pass only. The cast light (k x W), the haze on objects (the additive term, #543 F5) and the probes are not tested.
- The moon of the cached Midnight is taken opposite the sun; its own rotation and brightness are not read. Sunrise, Midday and Sunset (menu) are not cached: pending a fetch.
- A sky blended across the day / night switch in the middle of a lerp is measured (A-6PM to A-12AM); a path that flips mid-blend is not (all measured skies are classic).
- dE thresholds 2 and 5 are the owner's task figures; CIE76 over-reports in saturated blues relative to CIEDE2000 (not used).

## Own ideas (labelled)

- The **A / B decomposition** (skyF's pre-clamp colour is linear in `haze_glow`, so two tables give the exact glow with one `pow`) is ours, not from the literature; so is the zone variant d.
  Variant b (coded-domain table) is the standard practice of interpolating in the encoding a shader writes.
- Everything else uses published or in-repo techniques: Hillaire 2020 (sky-view table layout), the Linden Lab Windlight haze model, Hill's ACES fit, CIE 15 (CIE76).

## Phase 2 (in the viewer) must check

1. Upstream Firestorm against ours in the same world with the same sky (LEAP / the Environment menu), screenshots in every photo path: snapshot, high-resolution and tiled snapshots, 360 snapshots, saved and uploaded images.
2. Each draw path: classic, PBR with ambiance (ACES), PBR ambiance 0; a blend that flips the path mid-way.
3. The anchor skies, the ten that fail with variant c (low-gamma skies and the sun-at-0 day branch), and the built-ins; the sun at 0 degrees; the clamped skies.
4. The glow at several screen scales and in motion; the table indexed by `rel_pos`'s direction (geometry section); AT2-2's gate: variant c's cost against the zone variants.
5. The blend: the table rebuilt per blend step; its cost and any visible step.
6. Release / devel per-vertex against upstream per-pixel on the real dome; exposure sweep over the night skies; the horizon at the avatar's height; Sunrise, Midday and Sunset once fetched.

## Reproduce

    git remote add upstream https://github.com/FirestormViewer/phoenix-firestorm.git ; git fetch upstream     # once; the anchor pins bd03bafe69
    python scripts/light/at2_qual.py --anchor                                   # twin against the GPU reference (no GPU needed); needs g++ on PATH
    python scripts/light/at2_qual_anchor_gpu.py > doc/light/atmosphere/qual/anchor-gpu.tsv    # the reference values; needs a GPU, no viewer or build running
    python scripts/light/at2_qual.py --out rows.tsv --panos > summary.txt       # all 768 skies and the built-ins, blending and dome; about 40 s; add --physical for the starved labels (about 50 s more)
    python scripts/light/at2_qual.py --from rows.tsv                            # summarise a saved run

Set `TEMP` and `TMP` to the repository's `.fixture-tmp` folder. Files: `scripts/light/at2_qual.cpp` (the twins; includes `at2f_creator_grade.cpp` with its `main` renamed, so the two tools share one
display and one physical sky), `at2_qual.py`, `at2_qual_anchor_gpu.py`; results in `doc/light/atmosphere/qual/`.

## Panoramas

`doc/light/atmosphere/qual/pano-*.png`: 24 skies (the 10 worst for variant c, the named and the built-in skies, a few typical ones and a night sky; identical images are written once: the cached
Midday (Legacy) is the library's Midday). Each strip is 1536 x 128: **upstream | ours (variant c) | dE heat map**, each panel 512 x 128 with the azimuth from the light 180 degrees at the left edge to 180 at the right
(the light is at the centre of the panel) and the elevation from 0 (bottom) to 90 degrees (top). The heat scale (`pano-legend-heat-scale.png`): black dE 0, blue 2, yellow 5, red 10, white 20 or more; the white
ticks above the bar are dE 2, 5, 10 and 20.
