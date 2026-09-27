# In-world particle stress test

Companion script: `scripts/perf/particle-stress-test.lsl`.
A standalone LSL workload generator; no viewer instrumentation, CPU fallback or
viewer preference is added. It has not yet been compiled or run in-world.

## Build the test object

1. Use a dedicated copy of a rezzed, nonphysical object in a build area where you
   can run the test. The script overwrites particle systems and child transforms.
2. Create one root/controller prim and 16 small child prims. Name each child
   **PS_EMITTER** (case-sensitive). Link them, selecting the controller last so
   it becomes root. Do not add emitter scripts to the children.
3. Put the LSL script in the root and compile with Mono. No particle texture asset
   is needed: it uses the built-in blank texture for easily visible quads.
4. Keep the root upright. Children are placed in a 4-by-4 grid two metres above
   it: 4 m spacing in spread mode, 0.15 m in overlap mode. Ribbon sources also
   move in 0.75 m circles. Use small children (e.g. 0.1 m cubes), and leave room
   for the grid. Child transforms are deliberately not restored after stopping.
5. Frame the entire grid and its surrounding particles. Keep that camera position
   fixed throughout comparisons. Stay in range of owner chat commands (20 m).

Commands are owner-only, on channel 73043. With multiple copies, they will all
respond if within chat range: use only one test linkset at a time.

```text
/73043 run billboard spread
/73043 run velocity spread
/73043 run ribbon spread
/73043 run glow overlap
/73043 run churn spread
/73043 status
/73043 stop
/73043 help
```

Either layout works with every mode. Commands during a run do not replace it;
stop first. Touching the controller also stops the run. A new run starts with a
10-second drain. It then uses 1, 4, 8 and 16 emitters, each with 15 seconds of
warm-up, 30 seconds of measurement and 10 seconds of drain: about **230 seconds**
per run. Simulator scheduling can extend this. Keep other scripted objects from
altering these children during testing. Relinking or ownership changes abort.

Stopping clears the emitter properties; already living particles need time to
expire. Wait 10 seconds. Resetting the script also clears the named emitters.
Do not simply delete the script to stop a particle system: the emitter is a prim
property. Sources have a finite 60-second lifetime as a backstop; a viewer can
start that lifetime when it encounters the source, so this is not a global clock
or a substitute for clearing it. Extreme timer delay aborts the run as invalid.

## Workloads

| Mode | What it exercises | Nominal live particles per emitter |
| --- | --- | ---: |
| billboard | Ordinary lit billboards, color/scale/alpha interpolation | 1,280 |
| velocity | Velocity-aligned elongated quads | 1,280 |
| ribbon | Moving source, connected segments, endpoint aging | 400 |
| glow | Fullbright, larger transparent quads and glow | 1,280 |
| churn | One-second lifetime, eight times the billboard birth rate | 1,280 |

Billboards request 8 particles per 0.05 seconds with an 8-second lifetime.
Churn requests 64 per 0.05 seconds with a 1-second lifetime. Ribbons request one
per 0.02 seconds with an 8-second lifetime, avoiding coincident burst endpoints.
Totals at 16 emitters are nominally 20,480 and 6,400 respectively. These are
**requested steady-state estimates**, not rendered counts or guaranteed rates.
Viewer caps, visibility, source scheduling, low FPS and competing particle
systems affect actual populations. Different modes are not equal-cost workloads.

Spread mode reduces overlap between sources; it does not eliminate overdraw.
Overlap mode increases it. Ribbon motion also causes simulator object updates;
it is not a pure geometry microbenchmark. Churn changes particle spatial extent
as well as turnover. The script fixes parameters and load stages, but it cannot
seed the viewer's random emission or guarantee frame-identical particles.
Wind, target tracking, HUD particles, custom blending and source deletion are not
covered by this initial script. Ribbon drain covers aging, not every lifecycle
path. Do not treat this as complete renderer qualification.

## Compare builds

1. Record executable version/commit, GPU, driver, **actual renderer** (native GL
   or Mesa/Zink), viewport resolution, graphics settings, particle cap and OIT
   mode. Keep these fixed between baseline and compute builds. Do not change
   renderer at the same time as the feature being compared.
2. Prefer a quiet region with little unrelated particle activity. Let textures
   finish loading. Record avatars, background workload and any J2C decoding.
3. Take a no-emitter baseline after draining. Then run one mode/layout pair.
4. The script emits timestamped `PSBENCH` owner-chat records with run ID, mode,
   layout, active emitters and phase. Capture viewer frame times only between
   `MEASURE_BEGIN` and `MEASURE_END`. LSL cannot measure viewer FPS, GPU time or
   actual live/drawn particle counts; collect available counts and timings from
   viewer statistics or an external profiling tool. Mark unavailable counts as
   unknown, never substitute the nominal request count.
5. Repeat at least three times per build, alternating build order when practical.
   Compare median and p95 frame time from real frame samples. If only displayed
   FPS is available, record its range and label the result observational; do not
   derive frame-time percentiles from a few FPS readings.
6. Repeat separately for native OpenGL and Mesa/Zink. Keep screenshots of the
   same stage and look for missing quads, orientation errors, broken ribbons,
   wrong color/glow, flicker and stutter during drain. If the particle cap is
   reached, label the result capped; higher requested loads may no longer produce
   more live particles. Move the camera only in a separate visual test.

Suggested result columns:

```text
build,renderer,driver,mode,layout,emitters,run,particle_cap,observed_particles,
median_ms,p95_ms,visual_notes,background_activity
```

No automatic FPS logging or results file is claimed here. Owner-chat timestamps
mark script phases; network delivery and viewer frame timing are asynchronous.
Allow margin at window edges when aligning external recordings.

## API references and validation

- [Second Life particle API](https://wiki.secondlife.com/wiki/LlParticleSystem):
  linked emitters, particle flags, ribbon behavior and parameter definitions.
- [Linked primitive parameters](https://wiki.secondlife.com/wiki/LlSetLinkPrimitiveParamsFast):
  child placement through local position/rotation and link targets.

Source was checked against documented API names and reviewed for command
validation, owner filtering, stage progression and emitter cleanup. An LSL
compiler/simulator is not available locally: the first in-world compile and run
are still required. No viewer rebuild is needed to use this script.

## Vulkanstorm population cap

The audited fork currently sets both `LL_MAX_PARTICLE_COUNT` and the simulator's
`MAX_PART_COUNT` to **8,192** (`llviewerpartsim.h/.cpp`). Adaptive admission can
reduce the realized population further. The normal-mode 8- and 16-emitter stages
request more than this hard cap, so they probe saturation/source competition;
they cannot establish scaling to 10,240 or 20,480 actual particles in this build.
Do not raise the viewer cap or change admission policy between comparison runs.
