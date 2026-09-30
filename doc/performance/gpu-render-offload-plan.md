# GPU rendering offload: approved development scope

> Historical implementation: the CPU-to-GPU offload described below has been
> retired on the CPU restoration branch. See [current restoration and validation](cpu-rendering-restoration.md).
> Earlier AMD results do not establish NVIDIA performance gains.

> Current contract (2026-09-27 clarification): the Windows/Linux viewer baseline
> remains **OpenGL 4.3 Core**. The advanced resident particle path uses OpenGL
> 4.5 / GLSL 4.50 plus interlock, bindless textures, shader draw parameters and
> indirect draw counts **when available**. Missing advanced capabilities select
> CPU simulation/ordering/submission automatically, without a user toggle or
> viewer startup rejection. Supported systems use the GPU path; they do not run
> duplicate CPU simulation. Particle rendering remains outside PPLL/depth peeling.

Date: 2026-09-27.
Branch: `codex/gpu-render-offload`.
Base: `vkstorm-devel` at `1b1cea8b6552181bdc0b362770196c41e5f4a9c3`.

Current implementation: geometry expansion and the live resident simulation,
allocation, ordering and alpha-submission integration are implemented. The
integrated viewer pixel test passes on native AMD and Mesa/Zink. Logged-in scene
and frame-time acceptance, plus Linux compilation, remain outstanding. See [current implementation and validation](gpu-particle-pipeline.md).

## Scope and sequence

The approved items are:

1. Stable mesh geometry, GPU transforms and compatible batching.
2. Per-view compute visibility feeding indirect draws.
3. Particle geometry expansion on the GPU.
4. Particle simulation on the GPU.

Implementation begins with item 3, then item 4, before items 1 and 2.
The approved architecture replaces CPU execution of each migrated rendering
operation completely. There is no opt-in setting or small-workload CPU threshold. Hardware lacking
the advanced path capabilities uses the CPU implementation automatically. Partial implementations are development work, not
an acceptable completed feature. Qualification must cover every supported mode
before integration. On-demand CPU picking is a separate intersection consumer;
CPU simulation remains for capability fallback; item 4 replaces it on capable systems.
Flexible-object deformation and avatar morph offload are not approved in this
workstream. The AMD native-OpenGL applier quirk is a separate investigation.

Windows/Linux OpenGL >=4.3 Core remains the contract. Depth peeling remains
supported alongside PPLL. Development instrumentation stays off master.

## Initial particle audit

`indra/newview/llvopartgroup.cpp`:

- `LLVOPartGroup::updateGeometry` creates/configures a face per particle, performs
  size/distance admission, updates texture demand, and handles parcel-media textures.
- `getGeometry(const LLViewerPart&, ...)` constructs four vertices on the CPU.
  Ordinary billboards use camera-facing axes, with optional velocity alignment.
  Ribbons use parent/source position, axis and width instead.
- The second `getGeometry` overload duplicates color and glow across vertices;
  ribbon endpoints can have different colors/glow. Lit particles receive a
  camera-derived normal.
- `LLParticlePartition::getGeometry` distance-sorts faces and merges adjacent
  compatible ranges by texture, fullbright, glow and blend factors.
- CPU geometry also serves particle intersection queries. Rendering migration
  must preserve picking or provide equivalent on-demand CPU geometry.
- HUD particles have their own camera-position contract.

`indra/newview/llviewerpartsim.cpp`:

- `LLViewerPartGroup::updateParticles` integrates velocity/acceleration, source
  following, target motion, wind, bounce, color/scale/glow and age.
- It also invokes CPU callbacks, removes expired particles, transfers particles
  between spatial groups and destroys empty viewer objects.
- Agent-origin shifts and source removal mutate particle state on the CPU.

Consequently item 4 is not a mechanical replacement of the integration loop.
Reading every simulated position back each frame to retain these consumers would
undermine the proposed offload. GPU state ownership, visibility and ordering are
part of its design and acceptance criteria.

## P1: Particle geometry (item 3)

Cover ordinary, velocity-aligned, ribbon and HUD particles, including single-face
groups. Retain CPU simulation and existing ordering/admission policy until their
separately sequenced migrations, while proving GPU-generated geometry parity.

1. Define a compact per-particle render record for position, scale, velocity/axis, ribbon
   endpoint data, mode flags and packed color/glow, with explicit CPU/GLSL layout checks.
2. Expand queued records together in a bounded GPU scratch arena, then copy each
   group into its existing planar vertex buffer. This
   replaces the originally proposed instanced expansion for the first milestone:
   it preserves all existing alpha/fullbright/emissive shader consumers and draw
   submission. Do not assume gl_DrawID or newer multi-draw counts.
3. Preserve sorted order by batching only compatible consecutive runs. Preserve
   texture, blend factors, glow, lighting, clipping and camera-space semantics.
   Do not send particles to PPLL merely because their geometry moved to the GPU.
4. Keep CPU ownership of births, expiry, texture demand and picking initially.
   Qualify every particle type on the required GPU path; do not retain a CPU
   rendering implementation.
5. Cover velocity alignment, ribbons and HUD geometry with explicit parity tests;
   define finite behavior for degenerate camera/velocity vectors.
6. Bound staging memory and handle buffer reuse, context teardown, shader reload,
   group destruction and origin changes. No synchronous GPU readback for drawing.

Record CPU geometry time, particle counts by mode, expanded versus
compact upload bytes, draw calls, GPU time and frame-time distributions. Account
for retained CPU face/sort work; do not claim that billboard offload removes it.

## CPU/GPU boundary and remaining dependencies

The scope of item 3 is rendering geometry construction: billboard/ribbon vertices,
velocity alignment, normals, UVs and repeated color/glow attributes. All supported
particle modes use that GPU implementation. Remaining CPU work is explicit:

| Step | Current owner and reason | Required by host architecture, or deferred? |
| --- | --- | --- |
| Object/UUID lookup, attachment/wrist transforms, network source/target changes | CPU publishes data from viewer objects and region messages | Host integration; these objects and protocols are not shader resources |
| Texture/media ownership and GL submission | CPU manages viewer references, media substitution and driver calls | Host integration; shaders cannot invoke these viewer APIs |
| Birth admission, random emission policy and lifetime bookkeeping | Existing source/simulation code | Retained for item 4; GPU emission/expiry is possible, not inherently CPU-only |
| Motion, wind sampling, targeting, bounce and appearance interpolation | Existing per-particle simulation loop | Item 4 offload; no duplicate CPU integration or per-frame result readback is acceptable |
| Beam/spiral/chat callbacks | Existing callback functions | The three actual behaviors are arithmetic and can move to GPU after host-resolved source/target data is published |
| Spatial-group migration, per-particle culling, sorting and draw runs | CPU consumers of current position/scale | Architectural dependencies to replace before GPU-owned simulation can drive rendering without readback |
| Picking | CPU ray/quad intersection on demand | Explicitly outside geometry rendering; GPU picking is possible, so this is retained scope, not a claim it must remain CPU forever |

The source audit found three nonempty callbacks in `llviewerpartsource.cpp`:
spiral and chat use the same age/phase position formula; beam interpolates between
host-resolved source/wrist and target positions. Their function-pointer dispatch
is not a reason to retain CPU particle arithmetic. `LLWind::getVelocity` samples
a CPU-side 16x16 region field, with special edge behavior. The GPU implementation
must receive that field and perform the sampling, while receipt/decompression of
region data remains host work. Preserve
callback-before-integration order and the callbacks' use of the previous age.

The current CPU loop also performs pointer-based ribbon repair, source removal,
particle counters, invisible-group time skipping and origin shifts. These need
explicit commands, stable handles/generations and lifecycle tests. Merely moving
position integration to a shader and reading positions back to satisfy the old
consumers does not fulfill item 4.

## P2: Particle simulation (item 4)

Begin only after the GPU geometry consumer is validated.

1. Define stable particle/source handles with generations and bounded GPU pools.
   Publish births, source/target updates and source kills as bounded command batches.
2. Start with ordinary particles whose integration and interpolation have no CPU
   callback dependency. Preserve exact lifetime and interpolation policies.
3. Choose emitter/group ownership and conservative bounds that do not require
   synchronous current-position readback. Include gravity, acceleration, target
   motion, scale growth and origin shifts in the bounds contract.
4. Preserve ordered alpha through GPU ordering within the required rendering
   scope, or restrict admission where equivalent ordering can be demonstrated.
   PPLL is not a blanket substitute for custom blending or retained depth peeling.
5. Use bounded delayed completion feedback for bookkeeping where safe, without
   allowing dead/stale slots to draw or be reused prematurely. Prove count/limit
   behavior and source/group lifecycle invariants across delayed feedback.
6. Extend wind, callbacks, ribbons and special source types only after defining
   their data dependencies. These modes are completion requirements, not reasons
   to keep supported systems on CPU simulation; capability fallback remains supported.

Measure the entire path, including command upload, sorting, bounds maintenance,
feedback and CPU work that remains. Reject an implementation whose readback or
additional GPU cost erases its frame-time benefit.

## P3/P4: Mesh infrastructure and visibility (items 1 and 2)

The existing resident LOD path already generates commands on the GPU, but
`LLComputeMesh::drawLOD` submits one command per call. Build stable shared geometry
ranges and compatible opaque batches first. Move transform-only changes to compact
metadata updates instead of repeating CPU attribute conversion.

Then add conservative per-view compute visibility, initially with fixed command
capacity and zero-count rejected commands. Preserve world-camera LOD/streaming
policy separately from shadow/probe visibility. Keep picking, missing/authored-empty
LOD semantics, ownership generations and direct-path eligibility correct.

## Qualification and integration

- Use meaningful real-GL parity tests against current CPU geometry and simulation;
  validate winding, UVs, color, glow, lighting, velocity alignment and lifecycle.
- Include mixed particle-mode groups, custom blends, HUDs, ribbons, source
  deletion, picking, cold login, teleport, shader reload and origin changes.
- Benchmark particle-heavy and ordinary scenes, with decode idle and busy. Measure
  p50/p95/p99 frame times and GPU memory as well as CPU time removed.
- Rendering compute belongs to the rendering side of the soft 60:40 rendering/
  OpenCL-decode policy. Protect frame deadlines, permit idle-capacity borrowing,
  and account for whether both APIs actually use the same GPU.
- Build through Autobuild using RelWithDebInfo and Release-equivalent features,
  no installer, with full runtime staging. A successful compile is not runtime
  visual qualification.
- Review and integrate through vkstorm-devel. Audit development-only hooks before
  any later master integration and run scripts/tests/check_release_hooks.py there.

## Status

The item-3 development implementation uses compute for every rendered particle:
ordinary and velocity-aligned billboards, ribbons (parent/source/detached), and
HUD particles. It has no feature toggle, minimum-workload threshold or CPU
rendering fallback. CPU vertex/color/glow generation and its shared alpha-object
interface have been removed; grass retains its own typed geometry interface.
CPU intersection reconstruction remains explicitly named `getPickingGeometry`.
Degenerate view/velocity bases use finite billboard directions in both implementations.

The input record is 80 bytes per particle plus 64 bytes per queued group.
One compute dispatch handles a batch; GPU-to-GPU copies publish the existing draw
buffers without changing face indices, draw order or materials. UVs are generated
alongside the other attributes so each complete buffer copy contains valid data.
The GPU now writes/copies 192 bytes of attributes per live particle, plus buffer
padding/unused capacity, versus the previous 160-byte per-frame CPU attribute
upload (its static UVs were initialized separately). Less host upload does not
mean less total GPU traffic.

`LLVertexBuffer::setBuffer` invokes a pending producer before vertex consumption.
The queue retains destination buffers, captures each group's camera/normal,
restores program/SSBO/copy bindings, and clears producer callbacks on completion
or context teardown. It is capped at 16,384 records and 16 MiB of scratch output;
reaching either bound flushes GPU work. A repeated destination is copied in queue
order, so its latest rebuild wins. CPU staging vectors retain bounded capacity.
CPU simulation, sorting, admission and material batching remain at this stage.

`scripts/perf/test_particle_compute.py` exercises the production GLSL on real
OpenGL: 546,816 geometry/attribute checks passed on native AMD and packaged
Mesa/Zink, including all particle modes, zero/camera-parallel velocity, coincident
and vertical views, packed endpoint colors/glow, dispatch tails, padding and buffer
reuse. It compares mathematical reference geometry; it is not a rendered visual
comparison or a claim of bit-identical legacy SSE arithmetic.
`scripts/tests/test_particle_compute.py` compiles the production dispatcher against
fake viewer/GL dependencies and checks records, world/HUD camera selection flags,
ribbon parent/source/detached data, small/debug groups, binding restoration,
allocation-failure handling, malformed-input rollback, forced capacity flushes,
duplicate destinations and cleanup of pending work. Neither substitutes for viewer visual,
lifecycle and performance qualification.

The compute shader loads with the other required viewer shaders; load failure
uses the existing fatal required-shader policy. Runtime allocation/dispatch/copy
failure is logged and a required pre-draw flush failure is fatal, preventing stale
geometry consumption. Invalid queue admission omits the affected group with a
diagnostic. Neither is a supported degraded mode; either fails qualification. Shader/context reload
resets the compute service.

Item 4 remains gated on geometry qualification and the simulation ownership work
above. Mesh items 1 and 2 follow it; they are not implemented by this milestone.

### Build and startup validation (2026-09-27)

- Windows Autobuild `RelWithDebInfoOS` build succeeded, with OpenSim off,
  OpenJPEG/KDU-off, Mesa/Zink enabled, AVX2/LTO enabled and installer packaging off.
- Re-ran `copy_w_viewer_manifest` after linking. The staged
  `build-vc170-64/newview/RelWithDebInfo/Vulkanstorm-Release.exe` matches the linked
  executable by SHA-256; the staged compute shader matches its source as well.
  CEF, media plugins, voice, OpenJPEG, Mesa and viewer assets are present.
- An isolated native-AMD startup loaded the required particle compute shader,
  reached `STATE_LOGIN_WAIT`, quit after 20 seconds and exited with code 0.
  Temporary APPDATA/LOCALAPPDATA and disabled automatic login kept this separate
  from the user's normal viewer session. The smoke log is at
  `.tmp/particle-smoke/roaming/Vulkanstorm_x64/logs/Vulkanstorm.log` in this worktree.
- No in-world particle visual/performance qualification or Linux build has been
  completed. Startup success does not exercise a particle scene, teleport,
  source deletion, shader reload or context recovery.

### Focused performance qualification after batching

`python scripts/perf/benchmark_particle_compute.py` compiles the production C++
dispatcher against minimal viewer objects and a real OpenGL driver. Its CPU
reference reproduces ordinary billboard expansion with legacy approximate
reciprocal-square-root arithmetic and planar uploads. Both paths consume the
attributes with the same offscreen point draws. Draw submission is outside the
geometry CPU timer. Validation reads back results before timing; production
rendering never reads the generated buffers back.

The fixture excludes real viewer face sorting, color conversion, buffer-pool
bookkeeping, alpha blending, decode contention and the rest of a viewer frame.
It is a diagnostic, not an FPS benchmark or complete qualification. The drained
mode waits outside the CPU timer; STREAM mode queues 120 iterations, reads GPU
timestamps only after completion, and can expose driver backpressure. Neither
models the viewer's full workload or frame pacing.

Representative median geometry CPU submission times in microseconds, with 8,192
particles (native AMD 26.9.1 and packaged Mesa 26.3-devel/Zink on RX 9070 XT):

| Driver / groups | Drained CPU reference | Drained compute | STREAM CPU reference | STREAM compute |
| --- | ---: | ---: | ---: | ---: |
| Native / 1 | 105.8 | 66.0 | 109.5 | 59.6 |
| Native / 8 | 107.3 | 65.8 | 102.6 | 139.0 |
| Native / 64 | 105.6 | 68.4 | 100.7 | 100.5 |
| Native / 512 | 132.1 | 107.3 | 153.5 | 81.0 |
| Zink / 1 | 117.1 | 77.2 | 409.6 | 137.7 |
| Zink / 8 | 120.0 | 89.2 | 426.6 | 154.4 |
| Zink / 64 | 121.0 | 83.0 | 145.4 | 305.2 |
| Zink / 512 | 193.1 | 108.9 | 514.4 | 256.3 |

One-particle groups have higher compute submission overhead. Drained completion
latency under Zink is highly variable (including multi-millisecond cases); it is
not equivalent to measured shader execution time. Native STREAM / 1 also showed
a large completion tail despite lower median submission time. These observations
and the STREAM regressions keep the performance gate open: do not claim a general
CPU/frame-time improvement, or promote this as qualified, from the favorable
rows alone. If remaining overhead needs work, improve GPU batching/staging rather
than restore a CPU threshold or user toggle.

Raw local logs: `.tmp/particle-benchmark-native-draw.log` and
`.tmp/particle-benchmark-mesa-draw.log`. Earlier no-draw diagnostics were useful
for finding per-group dispatch overhead but are not the comparison above.

### Initial user in-world observation (2026-09-27)

The user tested the development viewer in a region with high particle density
and reported that visuals looked correct and responsiveness felt acceptable.
They observed approximately 11 FPS, compared with approximately 8 FPS previously
using Mesa/Zink in that region: about 37.5% higher FPS, or nominal frame time
falling from 125 ms to 91 ms (about 27% lower).

This is an informal user observation, not a controlled same-scene A/B benchmark.
The current session's renderer has not been confirmed, and identical camera,
settings, scene population, cache state and decode activity have not been
established. Do not attribute the whole difference to particle geometry compute
or to avoiding Mesa/Zink. This supplies initial in-world visual feedback;
performance attribution, broader visual/lifecycle coverage and Linux validation
remain open. At that geometry-only milestone, particle simulation, sorting and
spatial grouping still ran on the CPU; the resident integration below supersedes
that implementation boundary.

### Resident simulation, sorting, grouping and rendering (2026-09-27)

The live consumers are connected. Capable GPUs receive births and source snapshots,
then perform simulation, spatial grouping, conservative culling, depth sorting,
geometry expansion and ordered alpha submission. The CPU does not maintain a
parallel live-particle simulation or download per-frame draw counts. Unsupported
advanced capabilities select the existing CPU path automatically; the viewer
baseline remains OpenGL 4.3 Core.

The staged RelWithDebInfoOS development viewer (build 82004) includes the executable,
runtime libraries, media plugins and assets. Its branded executable matches the
linked binary. No installer was generated. Native AMD and Mesa/Zink pass the
integrated viewer color/depth/glow/interval fixture and the production dispatcher,
texture and blend tests. GL 4.3 and missing-interlock startup checks select CPU
fallback without rejecting startup.

See [the resident pipeline document](gpu-particle-pipeline.md) for the exact CPU
boundary, driver compatibility fix, deliberate scheduling/admission differences,
and validation evidence. Logged-in visual and frame-time acceptance and a Linux
build remain outstanding. Mesh items 1 and 2 remain later roadmap work; they are
not implemented by this particle migration.
