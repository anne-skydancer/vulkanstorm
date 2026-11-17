<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# AT2 qualification, phase 1: upstream's sky pass against the filtered physical sky (offline)

Owner decision (2026-11-17): "today's look would be the sky under PR #543 rules, after AT1 and AT2 land and EEP applied as filters on top of
the physical sky." #543 (merged: `doc/light/atmosphere-study.md` section 8 and `doc/light/at2f-creator-grade.md`) is the study of those rules.
This phase asks, offline and on the CPU only: **does the filtered physical sky reproduce upstream Firestorm's sky pass, the reference creators
tuned against?** No viewer code or build is involved. Phase 2 (in the viewer) is listed at the end.

Upstream is `upstream/master` (FirestormViewer/phoenix-firestorm, `bd03bafe69`). Our `skyV.glsl` and `skyF.glsl` have drifted from it (L3's
handover band, the night-sky port, and the haze evaluated per dome vertex in our skyV where upstream's skyF evaluates it per pixel), and the
AT2-F twin was built from OUR skyV; so the reference here is rebuilt from upstream, and our deliberate differences are reported on their own
(section "Known differences") and never counted as filter failures.

Files: `scripts/light/at2_qual.cpp` (the twins and the measurement), `scripts/light/at2_qual.py` (driver, summary, panoramas),
`scripts/light/at2_qual_anchor_gpu.py` (GPU reference values). Results: `doc/light/atmosphere/qual/at2-qual-phase1-rows.tsv` (every sky),
`at2-qual-phase1-summary.txt`, `anchor-gpu.tsv`, and 24 panorama PNGs (`pano-*.png`, 675 kB in all).

## Method, with the technique behind each part

- **The reference U: a C++ twin of upstream's sky pass.** `skyV.glsl` only passes the position on; `skyF.glsl` is the whole sky, per pixel
  (the Windlight / EEP haze model of Linden Lab: off-axis sunlight attenuation, blue and haze weights, haze glow `(1 - dot)^glow.z`, the
  cloud-shadow "below cloud" blend, x2 and clamp 5). Then the lighting pass for a sky pixel (`class3/deferred/softenLightF.glsl`,
  `GBUFFER_FLAG_SKIP_ATMOS`): `srgb_to_linear`, x `sky_hdr_scale`, x `final_scale` 1.1 in classic mode, `clampHDRRange` (0..11.2), with the
  path logic of upstream's `llsettingsvo.cpp`:

  | Path | Sky | `sky_hdr_scale` | `final_scale` | Display |
  |---|---|---|---|---|
  | classic | no `reflection_probe_ambiance` key: all 768 library skies, cached Midday (Legacy), Midnight | 1 | 1.1 | clamp only |
  | PBR | key present, nonzero | 2 sqrt(gamma) | 1 | ACES Hill |
  | PBR, ambiance 0 | key present and 0: the code default | 1 | 1 | ACES Hill |

  Upstream's day / night branch is replicated: `sun_up_factor` (sun by day; by night the moon, opposite the sun in a library sky, so the
  light is at the sun's depression above the horizon), `moonlight_color x 0.7` (the moon's colour is the sun's), and `sun_moon_glow_factor`
  (1 by day; by night `moon_brightness x 0.25`, and below 1 the glow is dropped entirely: the library skies carry no `moon_brightness`, so
  the default 0.5 applies and night has no glow). There is no handover band upstream: the sun is up when its elevation is 0 or more. So all
  759 measurable skies, night included, are measured; none is skipped. Rainbow and halo are 0 (moisture and ice levels are 0), no clouds, no stars.
- **Ours O: the filtered physical sky, as #543 plans it.** The sky-view table is Hillaire's (Hillaire 2020, "A Scalable and Production Ready Sky
  and Atmosphere Rendering Technique", Computer Graphics Forum 39(4)): 192 x 108, latitude mapped `v = 0.5 + 0.5 sign(l) sqrt(|l| / (pi/2))`;
  the 54 upper rows and the 97 columns from the light's azimuth to the opposite one are used (both skies are symmetric about the light's
  vertical plane), the AT2-F layout. It stores the **product** (physical x filter = reference) per texel and is sampled bilinearly. Because the
  table holds the product, **the additive part where the physical sky is 0 is inside it**: the visible-sky error is the table's sampling error
  of the reference, and the physical sky P does not enter it. P (`LLScatter::fromLegacy`, slab-split march at 64 steps, camera 20 m) is
  computed on a coarse grid only to say whether a sky is "hard for physics" (a colour channel starved or P exactly 0 over most of the sky).
  The additive term proper is the aerial perspective's concern (AT2-3, #543 F5), not measured here.
- **Near-sun treatment (AT2-2's plan; own design, see below).** Within N degrees of the light the sky pass evaluates the glow analytically (one
  `pow`, upstream's expression) instead of reading it from the table. Two extra tables A and B hold skyF's pre-clamp colour at `haze_glow = 0`
  and its slope in `haze_glow` (the colour is exactly linear in it); the pass computes `A + B x haze_glow(exact)`, then x2, clamp 5 and the
  lighting pass's arithmetic per pixel, and blends to the product table between N and 1.5 N degrees (smoothstep). Reported for N = 5, 10, 20.
- **The comparison.** 0.5 x 2.5 degree cells over the visible hemisphere (180 x 72, azimuth 0 to 180 by symmetry), in the display each path
  really uses (classic: the clamp only; PBR paths: ACES Hill, from the viewer's tone-map code; sRGB, 8 bit, exposure 1), CIE76 dE (CIE 15), **weighted
  by solid angle** (cos of elevation). Mean and worst cell per sky; the worst cell's elevation and its angle from the light. Pass / fail at dE 2
  (barely visible) and dE 5.
- Camera at the dome's origin; the table and dome are above the horizon only (the ground hides the rest in the viewer).

## Anchor: the upstream twin against upstream's own shader on the GPU

`scripts/light/at2_qual_anchor_gpu.py` reads upstream's `skyF.glsl` and `srgbF.glsl` with `git show upstream/master:...` into a scratch folder
(not the repository), compiles them in the bench's hidden OpenGL 4.6 context (RTX 5070 Ti) and draws one pixel per direction with the
uniforms upstream's `llsettingsvo.cpp` sets. `scripts/light/at2_qual.py --anchor` compares the twin to it (reference values committed in
`anchor-gpu.tsv`; no GPU needed to rerun the comparison). Seven skies, five directions each (zenith, horizon at 2 degrees and 90 degrees from the
light, mid sky, 14.75 degrees from the light at its elevation, 0.4 degrees above the light's core): Default, Sunset, Foggy (light at the zenith, output
saturated), [TOR] MIDDAY - Salmon (sunlight 1.05), [TOR] MIDDAY - Pleasantrie (sunlight 1.47), Alchemy Immortalis - Fog Lifting, and
[TOR] NIGHT - That spells moon 1 (the moon branch). **35 of 35 directions match to 4 decimals; the largest difference is 4e-6.** What the GPU
does not cover is the lighting pass's own arithmetic (x `sky_hdr_scale`, x 1.1, clamp 11.2), which is a plain scale checked by reading
upstream's `softenLightF.glsl` and `llsettingsvo.cpp`, and the three paths' choice.

## Results (759 skies measured: 612 day, 147 night)

The 768 library skies: 9 have `max_y` 0 and are undefined (the dome collapses to a point), 759 are measured. All 759 draw classic.
Built-ins are separate rows below. Medians, p90 and max are over the skies; "pass" counts skies.

| Variant | Mean dE median / p90 / max | Mean dE <= 2 | Mean dE <= 5 | Worst-cell dE median / p90 / max | Worst <= 2 | Worst <= 5 | Mean <= 2 and worst <= 5 |
|---|---|---|---|---|---|---|---|
| Table only | 0.043 / 0.097 / 0.284 | 759 | 759 | 1.39 / 10.30 / 74.1 | 425 | 559 | 559 |
| + near-sun 5 deg | 0.041 / 0.095 / 0.284 | 759 | 759 | 0.98 / 5.48 / 74.1 | 582 | 676 | 676 |
| + near-sun 10 deg | 0.037 / 0.086 / 0.284 | 759 | 759 | 0.89 / 2.08 / 51.1 | 679 | 730 | 730 |
| + near-sun 20 deg | 0.032 / 0.075 / 0.284 | 759 | 759 | 0.86 / 1.35 / 41.3 | 725 | 751 | 751 |

Day only (612): worst-cell median / p90 / max 2.50 / 11.6 / 74.1 table only; 0.91 / 2.45 / 51.1 with 10 degrees; 0.87 / 1.44 / 41.3 with 20
degrees (worst <= 5: 412, 583, 604). The 147 night skies are all but trivial: worst cell median 0.79, max 3.4, 144 of 147 within dE 2 in every
variant (the near-sun treatment does nothing there: no glow). Night skies are dark, so the 8-bit display hides relative errors (limit below).

**Headline.** Every sky passes at mean dE 2: the product table reproduces upstream's sky as a whole to dE 0.04 (median). **At worst-cell dE 5, 559
of 759 pass with the table alone and 730 with the near-sun treatment at 10 degrees (751 at 20).** At worst-cell dE 2: 425, 679 and 725.

### Why the table alone fails: the glow, and where it clips

200 skies exceed dE 5 somewhere with the table alone: the worst cell lies within 2.56 degrees of the light (the flat zone of `haze_glow`) in 43,
2.56 to 10 degrees in 128, 10 to 20 degrees in 24, beyond 20 degrees in 5. Causes by the worst cell's place: near the sun 117, near the sun at the
horizon 27, near the sun with a starved physical channel 22 (P's starvation is an association, not a cause here: the product table does not
use P), mid sky 20, others 14. The glow `(1 - dot)^glow.z` is a cusp that 2.7 to 3.3 degree rows and 1.875 degree columns cannot hold.
Treatment at 5 degrees leaves 83; at 10, 29; at 20, 8.

### Failures that remain with the near-sun treatment at 10 degrees (29 skies; the list is in the rows file)

Most remaining misses at 10 degrees are **the clamp-5 edge of the glow**: skyF clamps the pre-lighting colour at 5, and for strong glows the
clipped disc reaches 15 to 25 degrees from the light; its edge is a step that the table cannot hold and that the analytic zone (10 degrees,
blended out by 15) cuts across. That is why 20 degrees fixes most of them (Cornfield 11.0 to 0.87, Precision blue 7.4 to 1.5, Las Legunas 7.0
to 0.8, Red city 5.5 to 1.2). The five worst at 10 degrees: [TOR] BIG SUN - Raylanta (51.1 at 24.3 degrees from the light, elevation 40: the
clipped disc's edge beyond the zone; still 31.5 at 20 degrees), Places Beach Cay (41.3 at 8.8 degrees, elevation 0.75: sun exactly at the horizon,
a starved physical sky, clipped), Phototools- Yellow Stars 01 (25.3 at 58.7 degrees, elevation 78: **53 % of the sky is clamped at 5**, so the
edge of the clipped area is anywhere, not near the sun), [TOR] SCIFI - Rainbowtek (17.0 at 1.5 degrees, elevation 0.75: sun at the horizon, the glow core on the horizon rows) and [TOR] SUNSET - Buccaneers of the coast (12.1 at 16.3 degrees, elevation 1.2,
the glow's edge at the horizon). With 20 degrees, 8 remain: Raylanta, Beach Cay, Yellow Stars 01, Slips through walls 1 (7.4, 35 % clamped),
Grit & shame, Das fog (74 % clamped), Trilogy Rain 02 (62 % clamped) and Rainbowtek: clipped-region edges, or the sun at the horizon.
**Path:** all failing skies draw classic. Four of the 8 remaining at 20 degrees have the sun at exactly 0 degrees (Beach Cay, Rainbowtek,
Grit & shame, Das fog): their worst cell is at elevation 0.75 inside the analytic zone, so the error is in A and B themselves (still read bilinearly
from the table, where the horizon rows change steeply with elevation), not in the glow; the near-sun treatment does not help there.

### What the near-sun treatment buys (for AT2-2)

It moves worst-cell dE 5 passes from 559 to 676 (5 degrees), 730 (10) and 751 (20) of 759, and dE 2 passes from 425 to 582, 679 and 725; it
also lowers the mean slightly (median 0.043 to 0.032) because the cusp also dominates the mean near the sun. It costs two more RGB tables (A, B) in the
sky pass, one `pow`, a `mix` and the post-clamp arithmetic per pixel within N and 1.5 N degrees of the light (a small disc of the screen), and an
extra blend. **Recommendation: 20 degrees**, since the saturated glow's edge, not the cusp alone, sets the width; 10 degrees is the knee for
cost if the pass is tight. The remaining 8 are skies whose clamped region is large (clamp-5 areas away from the sun); for them the table cannot
hold the step, and the question for AT2-2 is whether the clamp should be applied after the table lookup (store the unclamped pre-colour and clamp
per pixel), which is exact and cheaper than widening N. That variant is not measured here (own idea, below).

### Built-in skies (named rows, outside the library figures)

| Sky | Path | Sun | Table only: mean / worst (at, elevation) | Near-sun 10: mean / worst | Near-sun 20: mean / worst |
|---|---|---|---|---|---|
| Code default (`LLSettingsSky::defaults()`) | PBR, ambiance 0 | 80 deg | 0.07 / 6.2 (2.8 deg, 80) | 0.05 / 1.0 | 0.05 / 1.0 |
| Midday (Legacy), cache 6c83e853 | classic | 90 deg | 0.08 / 7.9 (2.8 deg, 87) | 0.06 / 0.8 | 0.05 / 0.8 |
| Midnight, cache 8a01b97a | classic | -90 deg | 0.00 / 0.0 (black sky, brightest channel 0.003) | 0.00 / 0.0 | 0.00 / 0.0 |

Sunrise, Midday and Sunset of the Environment menu are not in the local cache: **pending a one-off fetch** (an owner session).

## Known differences: our current shaders against upstream (never counted above)

The same cells, our current twin (the AT2-F twin of our skyV/skyF with the L3 handover band, extended to the night; band weight
`smoothstep(0.3, -0.3, elevation + dip)`, `llatmosphericlight.h`) against the upstream twin:

| Group | Skies | Differ by more than dE 0.5 | Worst-cell dE median / max |
|---|---|---|---|
| Band weight 0 (sun above about 0.17 degrees) | 562 | 0 | 0 / 0 |
| Band weight 1 (night) | 139 | 0 | 0 / 0 |
| 0 < weight < 1 (sun within about 0.17 degrees of the horizon) | 58 | 51 | 13.1 / 75.6 |

So the L3 handover band is the **only** numeric difference in the day branch and the night branch: 58 skies whose sun sits at, or a
hair below, the horizon (upstream flips at once to the moon's 0.7 light and no glow when the elevation goes below 0; ours blends over about 0.17
degrees, so at `sun_angle` -0.000 the two look unlike): [TOR] SUNSET - Verdancy (mean 46.6), Orac - Drawing underground comic (mean 33),
[TOR] SCIFI - Grit & shame (13.7), and others. These are intended (the band is L3's design), not filter failures.
Not numerically measured here and therefore only listed: (1) the **night sky** (stars, Milky Way; off for Firestorm's random stars), (2) the
**moon's halo and rainbow** (L3 adds the moon's; zero here as moisture and ice are 0), (3) **our skyV evaluates the haze per dome vertex** and
interpolates it, where upstream's skyF evaluates it per pixel (the difference depends on the dome mesh and is largest at the sun's glow cusp;
AT2-2 draws the sky from the table, so it removes it, but "today's look" in the viewer includes it), (4) `RenderSkyAutoAdjustLegacy`
(default off) changes the classic path; not modelled.

## Limits

- **Camera height and dome convention**: the dome at the origin (camera height 0), 15000 m radius, `max_y` clamp and the plane at 32000 m below
  as the bench; the viewer's dome mesh is not rendered here. The physical sky is at 20 m, used only for the diagnostics.
- **Exposure 1, 8-bit sRGB**: the night skies (and Midnight, brightest channel 0.003) are below 1/255 over most of the sky, so dE hides relative
  error there; an exposure sweep (or a log-luminance metric) belongs to phase 2.
- **No clouds, no stars, no sun disc, no rainbow, no halo**; the sky pass only.
- **Above the horizon only**; the cells at the horizon row (elevation 0.25) are the lowest.
- **Moon**: library skies have their moon opposite the sun and `moon_brightness` 0.5; the cached Midnight's moon is taken opposite the sun,
  its own moon rotation and brightness are not read.
- **Sunrise, Midday, Sunset** (built-in menu skies) are not cached: pending a fetch. The 9 `max_y` 0 skies are undefined and are listed in the
  rows file.
- The product table is the **visible sky** only; the cast light (k x W), the haze on objects (the additive term; #543 F5) and the probes are not
  tested here.
- dE 2 / 5 thresholds are the owner's task figures; CIE76 over-reports in saturated blues relative to CIEDE2000 (not used here).

## Own ideas (labelled)

- The **A / B decomposition** for the near-sun treatment (skyF's pre-clamp colour is linear in `haze_glow`, so two tables give the exact glow with
  one `pow`) and its smoothstep blend between N and 1.5 N degrees are ours, not from the literature.
- **Clamp after the lookup** (store the pre-clamp colour; apply x2, clamp 5, `srgb_to_linear` per pixel) would remove the clamp-edge failures
  at their source; proposed for AT2-2 to test, not measured.

## Phase 2 (in the viewer) must check

1. Upstream Firestorm against ours in the same world, with the same sky applied by LEAP / the Environment menu, comparing screenshots in every
   photo path: the snapshot, high-resolution and tiled snapshots, 360 snapshots, saved and uploaded images (the filter is in every path).
2. Each draw path: classic (library), PBR, PBR with ambiance 0 (code default), including a **blend that flips the path mid-way**.
3. The seven skies of the anchor, the five worst of this run (Raylanta, Beach Cay, Yellow Stars 01, Rainbowtek, Buccaneers of the coast) and the
   three built-ins; the sun near 0 degrees (the band), near the zenith, and the clamped skies.
4. The sun's glow at several screen scales and in motion (no shimmer from the table, no seam at N and 1.5 N degrees); AT2-2's gate for the
   near-sun width (20 degrees recommended) and the clamp-after-lookup variant.
5. The per-vertex evaluation of our current skyV against upstream's per-pixel (known difference 3), measured with the dome mesh.
6. Exposure sweep over the night skies; the horizon (camera at the avatar's height); Sunrise, Midday and Sunset once fetched.

## Reproduce

    python scripts/light/at2_qual.py --anchor                                   # twin against the GPU reference (no GPU needed)
    python scripts/light/at2_qual_anchor_gpu.py                                 # the reference values; needs a GPU, no viewer or build running
    python scripts/light/at2_qual.py --out rows.tsv --panos > summary.txt       # all 768 skies and the built-ins, about 1 minute; panoramas to doc/light/atmosphere/qual/
    python scripts/light/at2_qual.py --from rows.tsv                            # summarise a saved run

Set `TEMP` and `TMP` to the repository's `.fixture-tmp` folder.
