<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Projector shadow pool: baseline and budget (stage S0)

The projector shadow pool replaces today's two view-driven projector shadow slots with one persistent shadow map per projector
(spec: `reports/projector-pool-2026-11-13/design-final.md`, sections 3 to 9). Every stage reports its cost against the budget
and keeps a running total. This file holds the measurement procedure and the tables. **Every table cell below is empty on
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
   - view `spot shadow 0` and `spot shadow 1`: CPU ms and GPU ms (one slot render each; the empty-cull overhead of an
     avatar-less render is the cost of an "empty" item the spec asks about for the avatar layer);
   - view `main`, pass `deferred lighting/sun program`: GPU ms (the sun pass including the two slot marches);
   - passes `deferred lighting/projectors` and `deferred lighting/fullscreen lights`: GPU ms (the lookup side);
   - draws and triangles of the two spot views.
   CPU ms and GPU ms of a scope are inclusive; counters are exclusive (`doc/gl/instrumentation.md`).
3. **Projectors within reach** (decides D20, the 8192 atlas tier): at each place call `ProjectorShadows.stats` (the script
   records it per preset; the viewer also logs it under `ProjectorShadows`) and write `lights.eligible_within_*` into table 3.
4. **Tracy** (optional, if a Tracy build is at hand): zones `projectors` and `fullscreen lights` at 1080p and 1440p.

## Tables (to fill at the owner session)

Machine, driver, resolution, build id, date: ______ (none recorded yet).

**Table 1. The 82178 Stochastic frame, nothing collecting**

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

| Place | Nearby lights | Spotlights | Eligible | Within 16 m | Within 32 m | Within 64 m | Within reach (min(far clip, 128 m)) |
|---|---|---|---|---|---|---|---|
| | | | | | | | |

Rule: the 8192 tier is added only if the owner's places push the pool level to 2 or more (spec D20).

**Table 4. Running total against the budget** (one row per stage, filled by that stage's PR)

| Stage | Measured? | Frame cost change (ms) | Running total (ms) | Allowance (ms) | Within? |
|---|---|---|---|---|---|
| S0 (offline parts) | no code that draws; the new pass scopes cost one pointer test with the option off and are absent without `VKS_GL_STATS` | 0 | 0 | | |
| P2 | | | | | |
| P3 | | | | | |
| P4 | | | | | |
| P5 | | | | | |

Label every cost "measured" (with scene, resolution, driver) or "estimate".
