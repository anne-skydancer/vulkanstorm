<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Particle CPU bench kit (local OpenSim)

Dev tooling, nothing in the viewer. A reproducible particle load for a local OpenSim region, used by the particle CPU
rework (stage 0 measurement, then the "after" runs of every later stage) and reusable for other crowd or particle
scenes (multicore S1, alpha-crowd). The kit is the LSL in `lsl/` plus `tracy_particles_report.py`. The viewer side of
stage 0 is a temporary Tracy probe on `codex/particles-s0-probe` (never merged); the zones and plots it adds are
listed in section 6.

OpenSim facts below were checked against the OpenSim master sources; confirm them on your 0.9.3.x. The `--grid` form
and the in-world behaviour (NPC attachments, sitting on keyframed objects) have not been run here.

Commands that redirect output (`>`) must run in Git Bash, cmd or PowerShell 7: PowerShell 5.1 writes UTF-16 files.
The report script reads `utf-8-sig`.

## 1. The bench viewer

Build from the probe branch (worktree `work\particles-s0`), configuration `RelWithDebInfoFS_open` (OpenSim on: the
Release configuration cannot log in to a local OpenSim grid), with Tracy, LTO and AVX2 (local Windows builds default
AVX2 off, CI turns it on, so pass it):

```bash
autobuild configure -A 64 -c RelWithDebInfoFS_open -- --tracy --lto --avx2 "-DLL_TESTS:BOOL=FALSE"
autobuild build -A 64 -c RelWithDebInfoFS_open --no-configure
# build-vc180-64/CMakeCache.txt must say OPENSIM=ON, USE_TRACY=ON, USE_AVX2_OPTIMIZATION=ON, USE_LTO=ON
```

Launch with a scratch profile, so no saved credentials exist (the only login is the local OpenSim user, typed by hand):

```powershell
$exe = '<worktree>\build-vc180-64\newview\RelWithDebInfo\vulkanstorm-bin.exe'
$env:APPDATA      = '<repo>\.captures\particles-profile\Roaming'
$env:LOCALAPPDATA = '<repo>\.captures\particles-profile\Local'
Set-Location (Split-Path $exe); & $exe --grid 127.0.0.1:9000
```

(`--grid 127.0.0.1:9000` reaches the grid-info fetch of the local region; otherwise add `http://127.0.0.1:9000/` under
Preferences > OpenSim.)

Settings for every run, written down with the capture: vsync off, frame limiter off, one window size and field of view
(the particle distance throttle depends on the camera, mostly near the rain), default graphics preset,
`RenderAttachedParticles` on, `RenderMaxPartCount` 4096. The 8192 run changes only `RenderMaxPartCount` to 8192 at the
same preset.

## 2. The OpenSim region (one time)

1. **Install** the current OpenSim 0.9.3.x binary release (needs the .NET 8 runtime), e.g. to `C:\opensim-bench`.
2. **Configure** standalone mode (the default architecture of `Standalone.ini`): copy `bin/OpenSim.ini.example` to
   `OpenSim.ini` and `bin/config-include/StandaloneCommon.ini.example` to `StandaloneCommon.ini`. NPCs are enabled by
   default (`[NPC] Enabled`), and the shipped `osslDefaultEnable.ini` already sets `AllowOSFunctions = true`; the
   `osNpc*` functions are allowed for the estate owner and estate managers by default. So: run all the scripts as the
   estate owner and no OSSL edit should be needed. Confirm on your version (the only OSSL file that ships as an
   example is `osslEnable.ini.example`; if a function is refused, the script owner prints a permission error).
3. **First start:** run `OpenSim.exe`, create the region `ParticleBench` at 1000,1000, port 9000, external host
   127.0.0.1, and the estate owner. Then `create user Bench Owner` (or use the estate owner).
4. **Log in** with the bench viewer (section 1).
5. **Build the kit** from `lsl/` (new script in a prim, paste the file). The plaza centre is the origin of the
   geometry below; the camera seat is 20 m from it on the -x side.

   | Object | Script | Notes |
   |---|---|---|
   | Controller post, near the seat | `controller.lsl` | `/7 <scene>` |
   | 4 firework posts, 12 m high, around a 30 m plaza | `emitter-fire.lsl` | names `Fire 0` .. `Fire 3` |
   | 8 ribbon movers on the plaza | `emitter-ribbon.lsl` | physics off; keyframed circle R 4 m, 8 s; names `Ribbon 0` .. `Ribbon 7` |
   | 2 rain emitters at 25 m | `emitter-rain.lsl` | rotation `<180,0,0>`; description `follow` on one of them; names `Rain 0`, `Rain 1` |
   | 1 fountain on the ground | `emitter-fountain.lsl` | |
   | 2 target posts | `emitter-target.lsl` | description of A: UUID of B; of B: `<UUID of A> linear` (an empty description warns) |
   | Camera seat | `camera-seat.lsl` | 20 m from the plaza centre, -x side, unrotated (looks along +x) |
   | Camera dolly | `camera-dolly.lsl` | geometry in the script header and section 4 |
   | NPC spawner at the plaza centre | `npc-spawner.lsl` | an animation named `bench-dance` in its inventory |

   Put 4 different textures in every emitter's inventory. The emitter picks one by a static index (the last digit of
   its name when it sits in the world, the owner key when it is worn), so it never changes during a run; the
   differences split the draw batches, which matters for the draw-info and draw-submission numbers.
6. **Bling attachment:** a small prim with `emitter-bling.lsl` and 4 textures, worn. Say `/8 save`, then (for scenes A
   and D only) `/8 spawn` (30 NPCs).
7. **Save the region:** in the OpenSim console `save oar C:/opensim-bench/particles-bench.oar` (reload with
   `load oar`). NPCs are probably not part of an OAR: say `/8 spawn` again after a load. Keep the OAR off-tree.

NPCs: `/8 spawn` refuses while NPCs from the spawner exist (OpenSim limits a region to 40 by default, 30 are used);
`/8 remove` removes them; they are also removed on script reset and region restart. If the spawner script was reset
while NPCs existed, restart the region. Spawn NPCs only for scenes A and D and remove them (`/8 remove`) for B and C:
they wear the bling and alpha clothing, so they would load the other scenes. If the bling script does not run on NPC
attachments, the particle system (a prim property) stays on: then scene A is always on and B and C are not clean, so
use a bling-free appearance for them.

## 3. Scenes

The controller relays `/7 <name>` over channel -74250 with `llRegionSay`. Any other word (`stop`) stops every
emitter, so scenes are exclusive; `all` starts every emitter.

Particle demand is burst count / burst rate x particle age per emitter. Burst counts are at most 255 (a U8 on the wire
and in OpenSim: 350 would arrive as 94). These are estimates; the real number is `parts_max` and `parts` in the probe
log, which also depends on the frame rate (see below).

| Scene | Command | Contents | Demand (estimate) |
|---|---|---|---|
| A crowd bling | `/7 bling` | 30 NPCs with a bling attachment (explode, 4 per 0.1 s, 1.2 s life, emissive, glow): 48 each | 1440 |
| B fireworks | `/7 fire` | 4 posts, 255 particles every 1.1 s, 2.5 s life, gravity, streaks (non-uniform scale + follow velocity): about 580 each | 2320, peaky |
| C ribbons | `/7 ribbon` | 8 ribbon emitters on keyframed circles: 150 each | 1200 |
| D mixed at cap | `/7 all` | A + B + C, plus rain (2 emitters, cone, wind, streaks: 3000 each), fountain (bounce, 1000), 2 target posts (target-pos and target-linear, 400 each) | about 12.8k wanted: saturates 4096 and 8192 |

Every update branch is exercised: wind, bounce, target, linear, follow-source (rain, one emitter), ribbon, emissive,
glow, interpolation, uniform and non-uniform groups.

**Spawn rate depends on the frame rate.** The viewer drops the fractional spawn remainder per update and the burst
rate is quantised on the wire, so a different frame rate gives a different particle count. Compare a before and an
after build only at equal mean `parts` (the report gives `parts`), and read the `*_per_1000` columns (ms per 1000
particles) next to the per-frame milliseconds.

## 4. Camera

- **Still:** sit on the camera seat (click it). Its offsets fix the view. Press Esc once if the camera moved.
- **Moving:** sit on the dolly. It is the same seat on a lateral keyframed leg: placed 20 m from the plaza centre on the
  -x side and 10 m to the -y side of the centre line, it travels 20 m along +y in 15 s and back (ping-pong). The plaza
  stays in the frustum at 20 to 22.4 m distance (+-12 %), seen from -27 to +27 degrees to the side. The leg restarts from
  its placed start when the script is reset or the object rezzed. Fallback if sitting on a keyframed object does not
  work in your OpenSim: Develop > Recorder (`LLAgentPilot`), record a 60 s path once, then Loop Playback.

## 5. Capture

Tracy 0.11.1 tools for Windows (matching the 0.11.1 client): `tracy-capture.exe`, `tracy-csvexport.exe` (the flags `-u`,
`-f` and the columns used below are those of 0.11.1; `tracy-capture` has no `--help`, run it without arguments for its
usage), and the GUI for looking by eye (`tracy-profiler.exe` is staged next to the viewer).

For each capture:

1. Run `/7 <scene>`; sit; wait 20 s for steady state (the `part count` plot plateaus).
2. Note the wall-clock start time (UTC) of the capture, then
   `tracy-capture.exe -a 127.0.0.1 -o S0-<scene>-<still|moving>.tracy -s 60 -f`
3. `tracy-csvexport.exe S0-....tracy > S0-....csv`
   and `tracy-csvexport.exe -u -f "df MainLoop" S0-....tracy > S0-...-frames.csv`
4. Keep the viewer log (`<scratch APPDATA>\<viewer folder>\logs\*.log`, e.g.
   `.captures\particles-profile\Roaming\Vulkanstorm_x64\logs`): the probe writes a timestamped `ParticleProbe:` line every
   second.

The set (about 30 minutes):

- scenes A, B, C, D, each still and moving: 8 captures;
- D-still again: the noise check (the two D-still runs must agree within 3 % in `mean_frame_ms` and `particle_cpu_ms`);
- D-still at `RenderMaxPartCount` 8192: 1 capture;
- a baseline: NPCs present, `/7 stop`, still camera, 60 s. Its `alpha_push_ms` is subtracted from the scene rows (see
  below).

Report one row per capture. The log lines are selected by timestamp, so a session log with earlier captures is fine:

```powershell
py -I tracy_particles_report.py --name S0-D-still --zones S0-D-still.csv --frames S0-D-still-frames.csv `
   --log <viewer log> --log-from 2026-11-13T04:39:26Z --log-seconds 60 --csv report.csv
```

Record the window size and field of view with each capture.

## 6. What to read

Probe zones (added by the probe branch):

| Name | Kind | Covers |
|---|---|---|
| `part put` | Tracy zone | `LLViewerPartSim::put`, the first-fit group scan; counters `part puts`, `part put scanned` |
| `part sort` | Tracy zone | the face sort in `LLParticlePartition::getGeometry`; `part faces` |
| `part fill us` | plot + log `fill_us` | CPU vertex fill: the per-face `getGeometry` quad build (billboard maths, colours, glow), summed per frame |
| `part drawinfo us` | plot + log `drawinfo_us` | batching and `LLDrawInfo` creation per face, summed per frame; `part drawinfos` counts the infos created |
| `part upload` | Tracy zone | vertex upload: the flush of the particle vertex buffers (`glBufferSubData`), only the particle buffers; plots `part upload us`, `part upload bytes`, `part upload buffers` |

Fill and draw-info are interleaved per face, so they are accumulated clock ticks (TSC) instead of a zone per face; a
zone per face would distort what it measures. The four TSC reads per face cost about 0.1 ms/frame at 4096 faces and are
inside `fill_ms` and `drawinfo_ms`: treat them as upper bounds. The same probe is on the "after" build, so the
before/after difference is not affected. `PARTICLE_PROBE_FACE_ZONES` in the probe's `llparticleprobe.h` adds per-face
zones for a look in the Tracy GUI (not for numbers).

Report columns: `upload_zone_ms` is the Tracy zone, `upload_ms` is the log value (a cross-check); `update_ms` =
`simulate_ms` - `put_ms`; `particle_cpu_ms` = `simulate_ms` + `updateGeometry_ms` + `rebuildGeom_ms` (NaN, with a warning,
if any of those zones is missing); `alpha_push_ms` is the zone `ra - push batch`.

`alpha_push_ms` wraps every alpha batch of the frame, NPC hair and clothing included, not only particles: it is an upper
bound for draw submission. Subtract the same column of the baseline capture (NPCs present, emitters stopped) before
reading it as the particles' share.

Other existing zones: `Simulate Particles` (spawn and update, `put` is nested inside), `LLVOPartGroup::updateGeometry`
(face update), `LLParticlePartition::rebuildGeom` and `::getGeometry` (`rebuildGeom - getGeometry` is the geometry
count), `glBufferSubData` (all buffers, context only), `df MainLoop` (the frame).

### Ranking rules

The survey's registered rules (section 9.7 of the particle survey, fixed before the run): first-fit if `part put` is
0.15 ms/frame or more in any scene; draw-info recycling at `part drawinfo` 0.1 ms or more; sort keys at `part sort` 0.15 ms
or more; the pooled layout if `pooled_rule_ms` (update + face update + geometry) is 1.0 ms or more in D at 4096; if
`alpha_push_ms` (baseline subtracted) is 50 % or more of the particle total, the exact rule caps the gain.

### The GPU instancing decision (fill against upload)

Particles draw as a quad of four vertices each (position, normal, colour, emissive, texture coordinates) that the CPU
fills and uploads every rebuild. Instancing would move the per-vertex expansion to the GPU and upload one record per
particle. Read `fill_ms` against `upload_zone_ms` and `upload_bytes`, per scene at 4096 and 8192: fill is what
instancing shrinks on the CPU, upload is what it shrinks on the bus. Both against `particle_cpu_ms` and
`mean_frame_ms`. `alpha_push_ms` is not changed by instancing by itself.

**Proposal, owner to confirm before the run** (this threshold is not one of the registered rules): `fill_upload_ms`
(fill + upload zone) of 1.0 ms/frame or more in D at 4096, or 2.0 ms at 8192, makes an instancing study worthwhile;
below 0.3 ms it does not; between 0.3 and 1.0 ms the numbers are reported and the owner decides.

## Files

- `lsl/`: the emitters (`emitter-*.lsl`), `controller.lsl`, `camera-seat.lsl`, `camera-dolly.lsl`, `npc-spawner.lsl`
  (OSSL, OpenSim only).
- `tracy_particles_report.py` builds the report row from the two csvexport files and the viewer log.
