<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Projector shadow pool: baseline and budget (stage S0)

The projector shadow pool replaces today's two view-driven projector shadow slots with one persistent shadow map per projector
(spec: `reports/projector-pool-2026-11-13/design-final.md`, sections 3 to 9). Every stage reports its cost against the budget
and keeps a running total. This file holds the measurement procedure and the tables. **Every table cell below, except the offline Table 5 (P2b), is empty on
purpose: S0's offline parts (guards, counters, scripts) are in the PR; the numbers come from the owner's capture session and are
written in then.** Nothing here is measured yet, and no value in this file may be filled in from an estimate.

## Budget

Everything new for light and shadow, together, at the default tier, costs at most the Stochastic frame of build 1.0.1.82178
(windows-latest at `8a797c1d61`, Shadow method Stochastic) + 5 % (AGENTS.md). The spec estimates the allowance at about 0.4 ms
at an 8 ms frame [spec estimate, not measured]. Each pool stage reports its own cost and the running total.

## What S0 added (offline)

| Part | Where |
|---|---|
| Legacy guard regions `spot-map-size`, `spot-map-compare`, `spot-slots`, `spot-render`, `spot-rank` (the spec's four, with the map region split in two because its two spans are not contiguous), reference texts from upstream Firestorm 7.2.5 | `indra/newview/pipeline.cpp`, `scripts/shadows/legacy_reference/spot-*.cpp.txt`, `scripts/shadows/check_legacy_shadows.py` (run by `.github/actions/ci-checks`) |
| Pass scopes for the sun pass, the projector pass and the fullscreen-light pass (`VKS_GL_STATS`) | `pipeline.cpp`: `sun program`, `projectors`, `fullscreen lights` under `deferred lighting` |
| LEAP API `ProjectorShadows.stats` (devel only): slots occupied, light-set counts within 16 / 32 / 64 m / reach, pool fields (all empty until P2) | `indra/newview/llprojectorshadowstatsapi.cpp`, `LLPipeline::getProjectorShadowStats` |
| `perf_check.py --no-glstats`: frame rate only, nothing collecting; records `ProjectorShadows.stats` per preset | `scripts/inworld/perf_check.py` |
| `projector_orbit_check.py` skeleton: O-1 implemented against the stats reply, O-2, O-4 and O-6 reported NOT RUN until P3 / P2 | `scripts/inworld/projector_orbit_check.py`, `scripts/inworld/scenarios/projector_orbit.json` |

The names `ProjectorShadowStats`, `llprojectorshadowstatsapi` and the `VKS_GL_STATS` hooks are in the forbidden list of
`scripts/tests/check_release_hooks.py`; an integration into `vkstorm-release` leaves these edits out (the pass-scope lines, the
accessor in `pipeline.h` / `pipeline.cpp`, the two `llprojectorshadowstatsapi` files and their CMake lines).

## Procedure (owner session)

Build: a development viewer with `-DVKS_GL_STATS=ON`, RelWithDebInfo, staged. Close other programs; vsync and the frame
limiter off (`perf_check.py` warns when they are on). Launch with `--leap` as `scripts/inworld/README.md` describes; log in by
hand. Scenes: **ground room**, **skybox**, **outdoors**, **projector club** (the owner's places; record the region and
position of each in table 1).

1. **Frame, nothing collecting** (the figure the budget is measured against):
   `python scripts/inworld/perf_check.py --no-glstats --scenario <scenario> --set RenderShadowMethod=2 --set RenderShadowDetail=2`
   Record `fps_stats_off` (median of the probes, with the spread) per scene into table 1. Convert to ms as 1000 / fps.
2. **Slot renders and sun pass with 0, 1 and 2 slots** (the credit the pool earns): in each scene arrange 0, 1 and 2 projectors
   in view (or none lit; read the number off the API: `ProjectorShadows.stats` `slots.occupied` is the count of slot renders per
   frame) and collect a window: `python scripts/inworld/perf_check.py --scenario <scenario> --only current --frames 240`.
   In the window (`window_*.json`, `analyze_perf_check.py`) read, per slot count:
   - view `spot shadow 0` and `spot shadow 1`: CPU ms and GPU ms (one slot render each, with casters). S0 does not
     measure the CPU of an empty `renderShadow` item (spec section 6, avatar items): those views contain casters. P4 must
     measure it (a slot render of a lamp whose cone holds no casters);
   - view `main`, pass `deferred lighting/sun program`: GPU ms (the sun pass including the two slot marches);
   - passes `deferred lighting/projectors` and `deferred lighting/fullscreen lights`: GPU ms (the lookup side);
   - draws and triangles of the two spot views.
   CPU ms and GPU ms of a scope are inclusive; counters are exclusive (`doc/gl/instrumentation.md`).
3. **Projectors within reach** (decides D20, the 8192 atlas tier): at each place call `ProjectorShadows.stats` (the script
   records it per preset; the viewer also logs it under `ProjectorShadows`) and write `lights.eligible_within_*` into table 3.
4. **Tracy** (optional, if a Tracy build is at hand): zones `projectors` and `fullscreen lights` at 1080p and 1440p.

## Tables (to fill at the owner session)

Machine, driver, resolution, build id, date: ______ (none recorded yet).

**Table 1. The Stochastic frame on the devel parity build (`-DVKS_GL_STATS=ON`, hooks compiled in and idle), nothing collecting**

This is not the 82178 binary: `--no-glstats` still calls `GLSubmitStats.status` for the frame count, so the hooks must be
in the build. The hook cost is read from `hook_bench` (run `perf_check.py` once without `--no-glstats`; `--no-glstats` skips
`selfBench`). If the 82178 figure is needed on the 82178 binary itself, take it by another method and record it beside this one.

| Scene | Place (region, position) | Resolution | fps | ms | Spread of the probes |
|---|---|---|---|---|---|
| Ground room | | | | | |
| Skybox | | | | | |
| Outdoors | | | | | |
| Projector club | | | | | |

Allowance (frame + 5 %): ground room ___ ms, skybox ___ ms, outdoors ___ ms, club ___ ms.

**Table 2. Slot renders and sun pass by slot count** (mean per frame over the window)

| Scene | Slots | `spot shadow 0` CPU / GPU ms | `spot shadow 1` CPU / GPU ms | `sun program` GPU ms | `projectors` GPU ms | `fullscreen lights` GPU ms | Spot-view draws / triangles |
|---|---|---|---|---|---|---|---|
| Ground room | 0 | | | | | | |
| Ground room | 1 | | | | | | |
| Ground room | 2 | | | | | | |
| Skybox | 0 / 1 / 2 | | | | | | |
| Outdoors | 0 / 1 / 2 | | | | | | |
| Projector club | 0 / 1 / 2 | | | | | | |

The pool's credit is the two slot renders plus the slot marches in the sun pass (spot-view CPU and GPU, and the difference of
`sun program` between 0 and 2 slots); the pool's added cost is the lookup in `projectors` and the cost of tile renders and
copies. Both sides are read from this table; the spec's estimates (0.3 to 0.5 ms CPU per slot render, 0.23 to 0.38 ms per
covered Mpx per slot march) are estimates until it is filled.

**Table 3. Projectors within reach (for D20, the 8192 atlas tier)**

The eligibility test on colour is approximate: it tests the light's colour unscaled, while the draw loop multiplies by `light_scale` (1 outside reflection probes), so the counts equal the drawn lights in practice. Lights that are fading out and the selected light (`dist` 0) fall into every band.

| Place | Nearby lights | Spotlights | Eligible | Within 16 m | Within 32 m | Within 64 m | Within reach (min(far clip, 128 m)) |
|---|---|---|---|---|---|---|---|
| | | | | | | | |

Rule (spec D20): the 8192 tier was to be added only if the owner's places push the pool level to 2 or more. P2b adds it on the owner's
in-world report of the P2 build (several lamps, every tile shrinking) instead of waiting for this table: chosen from 11 GB of
dedicated video memory (`NVX_gpu_memory_info` or `WGL_AMD_gpu_association`), within `GL_MAX_TEXTURE_SIZE`. The table still records
how many lamps the owner's places have. Its 512 MiB counts in the viewer's texture budget (about 4.9 GB on a 12 GB card, so +384 MiB over
4096 is 7.8 % of it, 5.2 % on 24 GB): watch the texture discard bias on 12 GB cards.

**Table 4. Running total against the budget** (one row per stage, filled by that stage's PR)

| Stage | Measured? | Frame cost change (ms) | Running total (ms) | Allowance (ms) | Within? |
|---|---|---|---|---|---|
| S0 (offline parts) | not measured (no draw work added; the new pass scopes cost one pointer test with the option off and are absent without `VKS_GL_STATS`) | not measured | not measured | | |
| P2 | | | | | |
| P2b | offline per-pixel path only (table 5); in-world full-frame cost: not measured, owed at the S0 capture session; gates any release integration | see table 5 | not measured | | |
| P2c | estimate, not measured in-world. The tile timer is CPU only; each 2048 tile also pays a GPU clear, render and a 16 MB copy (S to L) that no number here includes | up to +0.35 ms CPU (fixed cap; about 0.28 ms for four lamps at the 0.07 ms of 2026-11-14), GPU not measured | not measured | about 0.4 ms (+5 % of an assumed 8 ms frame) | not shown; the Legacy-cost guarantee is dropped until P3 |
| P3 | | | | | |
| P4 | | | | | |
| P5 | | | | | |

**Table 5. P2b: the changed per-pixel path, offline (measured)**

`python scripts/shadows/projector_pool_g1.py --time-pass` on an NVIDIA GeForce RTX 5070 Ti (driver 621.14, OpenGL 4.6), 2560x1440, every
pixel a receiver inside the projector's frustum (the worst case: a lamp that covers the whole screen), median of 9 x 20 draws,
2026-11-14. It compiles stSpotGradient through sampleProjectorShadow from the working file (after) and from
`origin/codex/projector-pool-p2` (before; its kernel cap is `max(1, 10 N / N_base)`, N_base 2048), stubbing only
`shadowReceiverGeometry` (identically on both sides). The matrix is built from real poses (the cube, the strip and the bulb of the
columns; fov 1.5, radius 10 m), receivers are hashed inside the frustum from the near plane to 1.5 x the radius, and each tile is
ray-cast from a floor and eight boxes with the same matrix. Emitter maximum 15 cm. "Filter off": the filter returns 1.0 at
once (its inputs kept live), which isolates the arithmetic of the per-pixel derivation (matrix setup, light size, radius cap) and the
receiver. Run-to-run noise of the same shader: at most 1.5 %. Penumbra pixels: the share of receivers with a visibility strictly between 0
and 1. kmax: the share of receivers by the larger of the two per-axis radius caps (texels) after the change.

| atlas | projector | N | before ms | after ms | delta | filter off: before ms | after ms | delta | penumbra pixels before / after | kmax 1 / (1,4] / (4,8] / (8,12) / 12 |
|---|---|---|---|---|---|---|---|---|---|---|
| 4096 | cube 0.5 m | 64 | 0.506 | 0.504 | -0.4 % | 0.050 | 0.050 | +0.6 % | 6.2 % / 6.2 % | 93% / 6% / 0% / 0% / 0% |
| 4096 | cube 0.5 m | 256 | 0.501 | 0.411 | -17.9 % | 0.050 | 0.050 | +0.7 % | 1.9 % / 2.0 % | 66% / 27% / 5% / 1% / 1% |
| 4096 | cube 0.5 m | 1024 | 0.760 | 0.711 | -6.4 % | 0.049 | 0.049 | +0.1 % | 1.3 % / 1.0 % | 0% / 66% / 18% / 6% / 10% |
| 4096 | cube 0.5 m | 2048 | 0.789 | 0.790 | +0.2 % | 0.049 | 0.050 | +0.4 % | 1.1 % / 0.7 % | 0% / 27% / 39% / 13% / 21% |
| 4096 | strip 5 x 0.2 m | 64 | 0.510 | 0.504 | -1.3 % | 0.050 | 0.050 | +0.9 % | 14.6 % / 14.6 % | 92% / 7% / 0% / 0% / 0% |
| 4096 | strip 5 x 0.2 m | 256 | 0.410 | 0.404 | -1.6 % | 0.050 | 0.050 | +0.6 % | 7.2 % / 7.1 % | 65% / 27% / 6% / 1% / 1% |
| 4096 | strip 5 x 0.2 m | 1024 | 0.602 | 0.571 | -5.1 % | 0.050 | 0.050 | +1.4 % | 5.0 % / 5.0 % | 26% / 40% / 18% / 5% / 12% |
| 4096 | strip 5 x 0.2 m | 2048 | 0.610 | 0.675 | +10.6 % | 0.049 | 0.050 | +0.7 % | 4.8 % / 4.6 % | 26% / 11% / 28% / 13% / 22% |
| 4096 | bulb 0.05 m | 64 | 0.510 | 0.508 | -0.3 % | 0.050 | 0.050 | +0.8 % | 6.3 % / 6.3 % | 92% / 7% / 1% / 0% / 0% |
| 4096 | bulb 0.05 m | 256 | 0.505 | 0.418 | -17.2 % | 0.049 | 0.050 | +1.1 % | 1.8 % / 1.8 % | 65% / 27% / 5% / 2% / 2% |
| 4096 | bulb 0.05 m | 1024 | 0.765 | 0.709 | -7.3 % | 0.049 | 0.050 | +0.2 % | 1.2 % / 1.0 % | 1% / 64% / 18% / 5% / 12% |
| 4096 | bulb 0.05 m | 2048 | 0.787 | 0.792 | +0.6 % | 0.049 | 0.049 | +0.6 % | 1.2 % / 0.7 % | 1% / 25% / 39% / 13% / 22% |
| 8192 | cube 0.5 m | 64 | 0.506 | 0.504 | -0.4 % | 0.050 | 0.050 | +0.8 % | 6.2 % / 6.2 % | 93% / 6% / 0% / 0% / 0% |
| 8192 | cube 0.5 m | 256 | 0.496 | 0.406 | -18.1 % | 0.049 | 0.050 | +0.5 % | 1.9 % / 2.0 % | 66% / 27% / 5% / 1% / 1% |
| 8192 | cube 0.5 m | 1024 | 0.779 | 0.725 | -7.0 % | 0.049 | 0.050 | +1.2 % | 1.3 % / 1.0 % | 0% / 66% / 18% / 6% / 10% |
| 8192 | cube 0.5 m | 2048 | 0.794 | 0.775 | -2.3 % | 0.049 | 0.050 | +1.0 % | 1.1 % / 0.7 % | 0% / 27% / 39% / 13% / 21% |
| 8192 | strip 5 x 0.2 m | 64 | 0.512 | 0.504 | -1.5 % | 0.050 | 0.051 | +0.8 % | 14.6 % / 14.6 % | 92% / 7% / 0% / 0% / 0% |
| 8192 | strip 5 x 0.2 m | 256 | 0.411 | 0.404 | -1.9 % | 0.050 | 0.051 | +0.9 % | 7.2 % / 7.1 % | 65% / 27% / 6% / 1% / 1% |
| 8192 | strip 5 x 0.2 m | 1024 | 0.586 | 0.543 | -7.3 % | 0.050 | 0.050 | +0.9 % | 5.0 % / 5.0 % | 26% / 40% / 18% / 5% / 12% |
| 8192 | strip 5 x 0.2 m | 2048 | 0.616 | 0.677 | +9.9 % | 0.050 | 0.050 | +0.2 % | 4.8 % / 4.6 % | 26% / 11% / 28% / 13% / 22% |
| 8192 | bulb 0.05 m | 64 | 0.510 | 0.508 | -0.4 % | 0.050 | 0.050 | +1.1 % | 6.3 % / 6.3 % | 92% / 7% / 1% / 0% / 0% |
| 8192 | bulb 0.05 m | 256 | 0.501 | 0.416 | -17.0 % | 0.049 | 0.050 | +0.7 % | 1.8 % / 1.8 % | 65% / 27% / 5% / 2% / 2% |
| 8192 | bulb 0.05 m | 1024 | 0.775 | 0.724 | -6.6 % | 0.049 | 0.049 | +0.4 % | 1.2 % / 1.0 % | 1% / 64% / 18% / 5% / 12% |
| 8192 | bulb 0.05 m | 2048 | 0.784 | 0.771 | -1.7 % | 0.049 | 0.050 | +0.6 % | 1.2 % / 0.7 % | 1% / 25% / 39% / 13% / 22% |

Reading: the arithmetic of the derivation costs +0.0003 to +0.0007 ms per full-screen pass (the filter-off columns: about +0.5 to +1.4 % of
0.05 ms, within the noise). With the filter on, the change is neutral or faster at N = 64 to 1024 (cap at or under 12 texels and
mostly under the old 10 or 5), because the radius caps of a shrunk tile are now in metres and no longer 1 to 10 texels of a tile
that is smaller. It is **slower where the cap now reaches 12 texels at a full-size tile**: the strip projector at N = 2048 is
**+10 %** (0.61 to 0.68 ms, +0.06 ms per full-screen pass; 22 % of its receivers are at the 12-texel cap, which the old rule capped at 10),
the cube and the bulb at N = 2048 are +0.2 to +0.6 % at atlas 4096 and -2.3 to -1.7 % at 8192. The atlas size changes the
timings by under 3 %. These are full-screen worst cases of one projector; a real frame shades only the pixels inside each frustum.
In-world full-frame cost: not measured, owed at the S0 capture session; gates any release integration.

**Table 6. P2c: scheduler and ambiance (estimates, nothing measured in-world yet)**

| Item | Value | Label |
|---|---|---|
| Static cap (CPU, tile renders per frame) | 0.35 ms fixed (4.4 % of an 8 ms frame; was 2 x the 0.07 ms average = 0.14 ms) | design constant |
| Tile renders per frame, 4 lamps at the P2b session's 0.07 ms | 4 (all) instead of about 1.2; the CPU cost rises from about 0.085 ms to 0.28 ms a frame | estimate from the 2026-11-14 session's numbers |
| Staleness of the oldest tile | at most ceil(N / tiles per frame) frames (24 frames seen in P2b) | by construction, unit test T7; read `pool.max_stale_frames` |
| Ambiance shadowed: the per-pixel function `sampleProjectorShadow` | unchanged, so `--time-pass` does not apply (it times that function) | not applicable |
| Ambiance shadowed: lookups on back-facing pixels in the cone | extra lookups, one per such pixel for lamps with `proj_ambiance` > 0; each costs what Table 5 shows per full-screen pass, scaled by the share of the screen those pixels cover | estimate |
| Running total of the light and shadow budget | the tile renders now take up to 0.35 ms CPU of the about 0.4 ms (+5 % of an assumed 8 ms frame) allowed; the timer is CPU only, GPU side (clear, render, 16 MB copy per 2048 tile) not measured | estimate, to be read in-world |
| Cold start | a never-drawn record is predicted at most 0.0875 ms (a quarter of the cap), so about four tiles a frame after login or a teleport | by construction, unit test T7 |

Label every cost "measured" (with scene, resolution, driver) or "estimate".
