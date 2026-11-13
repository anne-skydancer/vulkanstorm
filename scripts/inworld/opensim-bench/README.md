<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Particle CPU bench kit (local OpenSim)

Dev tooling, nothing in the viewer. A reproducible particle load for a local OpenSim region, used by the particle CPU
rework (stage 0 measurement, then the "after" runs of every later stage) and reusable for other crowd or particle
scenes (multicore S1, alpha-crowd). The kit is the LSL in `lsl/` plus `tracy_particles_report.py`. The viewer side of
stage 0 is a temporary Tracy probe on `codex/particles-s0-probe` (never merged); the zones and plots it adds are
listed below.

Everything marked *unverified* below was written from memory of OpenSim 0.9.3.x and Tracy 0.11.1 and has not been run
here: check it against the `.ini` comments and `--help` of the versions you install.

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

(*unverified:* `--grid 127.0.0.1:9000`; otherwise add `http://127.0.0.1:9000/` under Preferences > OpenSim.)

Settings for every run, written down in the capture notes: vsync off, frame limiter off, one window size (e.g. maximised
2560x1440), default graphics preset, `RenderAttachedParticles` on, `RenderMaxPartCount` 4096 (one extra run at 8192,
see section 5).

## 2. The OpenSim region (one time)

1. **Install** the current OpenSim 0.9.3.x binary release (needs the .NET 8 runtime), e.g. to `C:\opensim-bench`.
2. **Configure** standalone mode: copy `bin/OpenSim.ini.example` to `OpenSim.ini` and
   `bin/config-include/StandaloneCommon.ini.example` to `StandaloneCommon.ini`. In `OpenSim.ini` set `[NPC] Enabled = true`
   and keep `[XEngine] Enabled = true`. In `config-include/osslEnable.ini` set `AllowOSFunctions = true` and allow, for
   the estate owner: `osNpcCreate`, `osNpcRemove`, `osNpcPlayAnimation`, `osAgentSaveAppearance`.
3. **First start:** run `OpenSim.exe`, create the region `ParticleBench` at 1000,1000, port 9000, external host
   127.0.0.1, and the estate owner. Then `create user Bench Owner` (or use the estate owner).
4. **Log in** with the bench viewer (section 1).
5. **Build the kit** from `lsl/` (new script in a prim, paste the file):

   | Object | Script | Notes |
   |---|---|---|
   | Controller post, near the seat | `controller.lsl` | `/7 <scene>` |
   | 4 firework posts, 12 m high, around a 30 m plaza | `emitter-fire.lsl` | |
   | 8 ribbon movers on the plaza | `emitter-ribbon.lsl` | physics off; keyframed circle R 4 m, 8 s |
   | 2 rain emitters at 25 m | `emitter-rain.lsl` | rotation `<180,0,0>`; description `follow` on one of them |
   | 1 fountain on the ground | `emitter-fountain.lsl` | |
   | 2 target posts | `emitter-target.lsl` | description of A: UUID of B; of B: `<UUID of A> linear` |
   | Camera seat | `camera-seat.lsl` | about 20 m from the plaza centre |
   | Camera dolly | `camera-dolly.lsl` | same seat on a 30 m ping-pong line, 15 s each way |
   | NPC spawner | `npc-spawner.lsl` | an animation named `bench-dance` in its inventory |

   Put 4 different textures in every emitter's inventory (the emitter picks one by position, stable across runs): the
   texture differences split the draw batches, which matters for the draw-info and draw-submission numbers.
6. **Bling attachment:** a small prim with `emitter-bling.lsl` and 4 textures, worn. Say `/8 save`, then `/8 spawn`
   (30 NPCs). If the script does not run on NPC attachments, start the bling once while worn, before `/8 save`: the
   particle system is a prim property and persists without the script, so scene A is then always on.
7. **Save the region:** in the OpenSim console `save oar C:/opensim-bench/particles-bench.oar` (reload with
   `load oar`). Keep the OAR off-tree.

## 3. Scenes

The controller relays `/7 <name>` over channel -74250 with `llRegionSay`. Any other word stops every emitter, so scenes
are exclusive; `all` starts every emitter.

| Scene | Command | Contents | Particle demand |
|---|---|---|---|
| A crowd bling | `/7 bling` | 30 NPCs with a bling attachment (explode, 3 per 0.08 s, 1.2 s life, emissive, glow) | about 1350 |
| B fireworks | `/7 fire` | 4 posts, 350 particles every 1.5 s, 2.5 s life, gravity, streaks (non-uniform scale + follow velocity) | about 2300, peaky |
| C ribbons | `/7 ribbon` | 8 ribbon emitters on keyframed circles | about 1200 |
| D mixed at cap | `/7 all` | A + B + C, plus rain (cone, wind, streaks), fountain (bounce), target posts (target-pos, target-linear) | about 8900 wanted: saturates 4096 and nearly 8192 |

Every update branch is exercised: wind, bounce, target, linear, follow-source (rain, one emitter), ribbon, emissive,
glow, interpolation, uniform and non-uniform groups.

## 4. Camera

- **Still:** sit on the camera seat. Its offsets fix the view. Press Esc once if the camera moved.
- **Moving:** sit on the dolly. It is the same seat on a keyframed line, so the camera distance, the group culling
  rate and the group transfers change continuously. Fallback if sitting on a keyframed object does not work in your
  OpenSim: Develop > Recorder (`LLAgentPilot`), record a 60 s path once, then Loop Playback.

## 5. Capture

Tracy 0.11.1 tools for Windows (matching the 0.11.1 client): `tracy-capture.exe`, `tracy-csvexport.exe`, and the GUI
for looking by eye. Check the flags with `--help` (*unverified:* `-a`, `-o`, `-s`, `-f` for capture; `-u`, `-f` for
csvexport).

For each capture:

1. Run `/7 <scene>`; sit; wait 20 s for steady state (the `part count` plot plateaus).
2. `tracy-capture.exe -a 127.0.0.1 -o S0-<scene>-<still|moving>.tracy -s 60 -f`
3. `tracy-csvexport.exe S0-....tracy > S0-....csv`
   and `tracy-csvexport.exe -u -f "df MainLoop" S0-....tracy > S0-...-frames.csv`
4. Keep the viewer log (scratch `LOCALAPPDATA` logs folder): the probe writes a `ParticleProbe:` line every second.

The set (about 25 minutes):

- scenes A, B, C, D, each still and moving: 8 captures;
- D-still again: the noise check (the two D-still runs must agree within 3 %);
- D-still at `RenderMaxPartCount` 8192 (Ultra preset): 1 capture.

Report one row per capture:

```powershell
py -I tracy_particles_report.py --name S0-D-still --zones S0-D-still.csv --frames S0-D-still-frames.csv `
   --log <viewer log> --log-tail 60 --csv report.csv
```

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
zone per face would distort what it measures. `PARTICLE_PROBE_FACE_ZONES` in the probe's `llparticleprobe.h` adds
per-face zones for a look in the Tracy GUI (not for numbers).

Existing zones, read as they are: `Simulate Particles` (spawn and update), `LLVOPartGroup::updateGeometry` (face
update), `LLParticlePartition::rebuildGeom` and `::getGeometry` (`rebuildGeom - getGeometry` is the geometry count),
`ra - push batch` (draw submission, ms and calls per frame), `glBufferSubData` (all buffers, context only),
`df MainLoop` (the frame).

Plots and log fields: `part count`, `part groups`, `part puts`, `part put scanned` (groups tested per put = scanned /
puts), `part groups created` / `deleted`, `part sgroups rebuilt`, `part faces`, `part drawinfos`.

### The GPU instancing decision (fill against upload)

Particles draw as a quad of four vertices each (position, normal, colour, emissive, texture coordinates) that the CPU
fills and uploads every rebuild. Instancing would move the per-vertex expansion to the GPU and upload one record per
particle. Read, per scene and at 4096 and 8192:

- `fill_ms` against `upload_ms` and `upload_bytes` (per frame). Fill is what instancing shrinks on the CPU; upload is
  what it shrinks on the bus. The ratio says which side pays.
- Both against the whole particle CPU (`particle_cpu_ms`, a report row) and the frame (`mean_frame_ms`).
- `ra - push batch` is draw submission and instancing does not change it by itself (the draw count stays the same
  unless the batching changes): if it dominates, the gain is capped.
- A first rule of thumb, to be confirmed with the owner: fill + upload at or above 1.0 ms/frame in scene D at 4096 (or
  2.0 ms at 8192) makes an instancing study worthwhile; below 0.3 ms it does not.

The other ranking rules of the particle rework (first-fit if `part put` is 0.15 ms/frame or more in any scene,
draw-info recycling at 0.1 ms or more, sort keys at 0.15 ms or more, the pooled layout at 1.0 ms or more of update +
face update + geometry in D at 4096) are applied to the same table.

## Files

- `lsl/`: the emitters (`emitter-*.lsl`), `controller.lsl`, `camera-seat.lsl`, `camera-dolly.lsl`, `npc-spawner.lsl`
  (OSSL, OpenSim only).
- `tracy_particles_report.py` builds the report row from the two csvexport files and the viewer log.
