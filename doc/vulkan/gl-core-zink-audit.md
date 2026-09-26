# OpenGL Core modernization and Mesa/Zink audit

> Current implementation contract: Windows/Linux OpenGL **>=4.3 Core**.
> Both OIT paths remain for now. Depth peeling will be deprecated before it is
> retired; PPLL is the long-term sole OIT target. Its current 4.4-dependent calls
> need a qualified 4.3 implementation before that transition is complete.


Reader reference: [acronyms and terminology](rendering-audit-glossary.md).

Date: 2026-09-26. Viewer source: `50871c432f` (`master`).
Status: static audit and implementation plan; no performance claim or renderer change.

See the [targeted rendering-pipeline audit](rendering-pipeline-targeted-audit.md)
for the proposed Core-only contract, prioritized correctness findings and the
CPU-to-GPU mesh work inventory.

## Scope and evidence

Three tracks: establish the lowest useful Core-profile baseline while preserving
viewer functionality, reduce unnecessary work through the GL/Mesa/Zink stack,
and offload mesh preparation and render decisions to GPU compute where beneficial.
Shared GPU provisioning between rendering/mesh compute and OpenCL J2C decode is
a cross-cutting requirement, initially targeting 60:40 under contention.
Platform decision: Windows/Linux only; macOS compatibility is dropped.
The 4.1 proposal is superseded by a 4.3 Core minimum, making compute, SSBOs and
multi-draw indirect baseline capabilities. Keep both OIT methods during transition.
PPLL is the long-term sole OIT target; deprecate depth peeling before retirement.

Read alongside `opengl_modernization_strategy.md`, `gl_compute_mesh.md`,
`../depth_peel_portability.md`, `../ci_release_parity.md` and the historical
`reverse-engineering/backend-contracts.md`. Historical reference reports and draft
policy are not proof of current implementation. This audit does not change native
Vulkan's governing contracts or amend the old modernization draft implicitly.

Viewer manifest pins Mesa `00e42c51b10d8e0769489156fa414f111897d515`,
26.3.0-devel, through `anne-skydancer/3p-mesazink`. The matching local Mesa source
at `C:/Dev/mz-src` was inspected, including its source marker and Meson build options.
The other checkout at `C:/Dev/3p-mesazink/mesa-src` has a different source marker
(`3e2092a295...`) and is not the audited baseline. Package build scripts and three
patches were also read from the package repository. A local source marker and
build options do not independently prove a distributed DLL's provenance.

The viewer audit covers context creation, capability gates, shader version emission,
buffer/texture submission, compute LOD, transparency, occlusion, legacy debug
rendering, and runtime selection. The Mesa audit samples the relevant frontend,
descriptor, draw-state and presentation paths; it is not an exhaustive audit of
all Mesa internals. No viewer sessions or controlled frame-time captures were run.

## Capability baseline

| Feature or implementation | Current requirement/evidence | Plan |
|---|---|---|
| Ordinary rendering, HDR and depth peeling | `pipeline.cpp` HDR checks above 4.05; depth-peel helper requires 4.1 | Qualify rendering/HDR/depth peeling on 4.3 Core; deprecate peeling before later retirement |
| Mesh compute LOD / indirect submission | `llcomputelod.cpp::enabled`, `initialize`; GLSL 430 in `meshLODC.glsl` | Require capability at the 4.3 floor; expand coverage incrementally, keeping direct draws for ineligible cases |
| PPLL | `pipeline.cpp::allocateAlphaOITBuffers`, `beginAlphaOITCapture`; `glBufferStorage`, `glClearTexImage`; shader gate in `llshadermgr.cpp` | Long-term sole OIT target; keep the current 4.4 gate until a 4.3 path is qualified |
| Anisotropic filtering | `llgl.cpp::initExtensions`: GL 4.6 or `GL_EXT_texture_filter_anisotropic` | Explicit extension capability below 4.6; not guaranteed by bare 4.1 |
| Texture compression and vendor memory telemetry | Format/extension-dependent | Inventory and qualify separately; Core version alone cannot guarantee every facility |
| 4.5/4.6 function loading | Many entry points are loaded; search found no calls to the loader's 4.5/4.6 function set outside the loader/header | Do not raise the baseline merely because symbols are loaded |

There are three different questions:

1. Proposed Windows/Linux architecture minimum: **4.3 Core**, to require compute,
   SSBOs and multi-draw indirect. The earlier 4.1 candidate is no longer a target.
2. Lowest Core version retaining the present PPLL and compute implementations:
   **4.4**, with format, storage-size and other resource limits still checked.
3. A version-only promise of every optional feature, including anisotropy without
   extension dependencies: 4.6 is relevant, but still does not replace resource,
   compression-format and platform capability checks.

Request the highest qualified Core context and enforce the proposed 4.3 minimum.
Both OIT methods remain during transition. The current PPLL path is gated at
4.4; qualify a 4.3 implementation before retiring depth peeling. Direct geometry
handling remains useful for incomplete compute coverage, diagnostics and recovery,
not for preserving a non-compute hardware tier. This is policy for implementation;
the current source still contains the older context negotiation and fallbacks.

### Core-profile blockers and cleanup

* Windows: `llwindowwin32.cpp::createSharedContext` clamps requests to 3.0–4.6,
  chooses core/compatibility from a setting, decrements versions, then permits
  legacy `wglCreateContext`. The main context also uses this helper. Replace with
  an explicit supported-version ladder and an enforced Core minimum; preserve
  bootstrap-context requirements separately from the final rendering context.
* Linux SDL2: `llwindowsdl2.cpp::createContext` sets framebuffer attributes but
  does not request a Core profile/version. Specify both before window creation;
  verify shared upload contexts and reconstruction after window changes.
* `settings_to_globals` reads `RenderGLContextCoreProfile` on Windows/Linux;
  its XML default is false. Reconcile saved settings and feature-table masks with
  the new contract instead of only changing this default.
* `llfeaturemanager.cpp` rewrites Intel GL/GLSL versions to 3.3/3.20 on a broad
  pre-4.6 condition. Separate reported capabilities from driver workarounds;
  qualify affected drivers rather than keeping synthetic version mutation.
* Reachable physics-shape debug drawing in
  `llspatialpartition.cpp::renderPhysicsShape` uses `glVertexPointer` and CPU
  index memory in the convex-prim branch. Convert this to vertex/index buffers
  and generic attributes before declaring Core support complete. Normal-world
  captures alone will miss this path.
* `llshadermgr.cpp` emits GLSL 140/150/330/400/420 and separately enables depth
  peel/PPLL. Simplify only unreachable tiers after startup enforcement. Audit
  extension directives and feature permutations; a global version edit is not
  sufficient. The compute shader already has its own explicit GLSL 430 header.
* `LLGLState::resetTextureStates` still contains client-active-texture legacy
  calls, but this search found only its declaration/definition. `LLGLSSpecular`
  likewise has no discovered consumers. `LLPostProcess` is initialized at startup,
  but no direct `gPostProcess->apply` call was found. These are removal candidates
  needing final call/reference checks, not demonstrated per-frame costs.
* `ENABLE_GL_WORK_QUEUE` is explicitly zero; pipeline `checkReferences` bodies
  contain `#if 0`. Removing these improves maintainability, not frame rate.

## Viewer-to-Zink findings

The stack is viewer GL calls → Mesa frontend/state tracker → Gallium/Zink → Vulkan
driver. It is not one Vulkan call per GL call. Attribute costs to the appropriate
layer and distinguish CPU work, GPU work, queue waits and presentation pacing.

| Finding | Evidence and likely impact | Change/validation |
|---|---|---|
| Repeated texture binds | `llrender.cpp::LLTexUnit::bindFast` always calls `glActiveTexture` and `glBindTexture` | Cache active unit and binding with correct invalidation for deletion, shared contexts, media and raw GL callers; measure frontend/descriptor updates |
| Shared contexts weaken Mesa's repeated-bind shortcut | Mesa `src/mesa/main/texobj.c::bind_texture_object`, around 1624: early return requires `ctx->Shared->RefCount == 1` | Viewer upload contexts make this especially relevant. Do not remove shared-resource visibility semantics to gain a shortcut |
| One-command MDI | `llcomputelod.cpp::drawLOD` calls `drawIndirect(..., slot, 1)` | Existing GPU LOD does not batch CPU draw submission. Group compatible opaque draws; preserve material, geometry and skin ownership |
| State queries around compute | `llcomputelod.cpp::Bindings` queries program, generic SSBO and binding/start/size for five slots: 17 queries per construction | Give the compute pass explicit state ownership and restore from authoritative state; verify cache coherence. Queries are not all GPU readbacks |
| Upload segmentation overlaps bytes | `llvertexbuffer.cpp::flush_vbo` uses inclusive `end`, `tend=min(i+65536,end)`, size `tend-i+1`, and advances by 65536 | Each nonfinal chunk overlaps one byte with the next. Fix inclusive-range arithmetic; separately benchmark coarser uploads rather than assuming 64 KiB chunks are optimal |
| CPU mirror plus GPU copy | `copyResidentRange` performs CPU memcpy and GPU attribute copies, then rebases indices on CPU | Not automatically redundant: CPU mirror remains consumed by the buffer design. Separate persistent GPU-owned buffers from CPU-owned mutable buffers before removing copies |
| Full font atlas upload | `llfontfreetype.cpp` uploads atlas width/height for each inserted glyph; `llimagegl.cpp::setSubImage` redirects matching full-size updates to `setImage` | Upload dirty glyph rectangles or batch updates; verify row stride, atlas growth, alpha bookkeeping and rendered text |
| Duplicate hero-probe path | `pipeline.cpp::doOcclusion` has two consecutive blocks with the same condition, each calling `mHeroProbeManager.doOcclusion()` | Remove redundant traversal/state changes after checking scheduling. Probe query readiness means this is not necessarily two GPU draws per probe |
| Upload synchronization policy | `llimagegl.cpp::syncToMainThread`: NVIDIA worker uses a blocking client wait; other path uses two flushes around a fence and a main-context server wait | Track waits/flushes per uploaded texture and batch handoffs. Retain a valid shared-context publication fence; do not simply delete waits |
| Occlusion result timeout | `llvieweroctree.cpp::checkOcclusion` eventually requests `GL_QUERY_RESULT` without availability | Investigate conservative deferral and bounded polling; preserve visibility history, asset demand and per-camera rules |

Existing optimizations must be preserved: `LLRender::syncMatrices` has matrix
hashes and inverse/normal caches; it does not blindly invert every matrix on every
draw. Mesa/Zink also tracks dirty state and pipeline changes. An application-level
redundancy is evidence of extra frontend work, not proof of extra GPU commands.

## Mesa/Zink-specific findings

### Build and runtime selection

The Windows package is release, Zink-only, with LLVM, bundled Vulkan drivers,
EGL/GLX/GLES disabled. Linux builds a private GLVND GLX provider and matching
Gallium library. There is no obvious collection of unused hardware drivers to
strip. The inspected matching Windows build has optimization level 3,
`b_ndebug=if-release`, and **LTO disabled**. LTO is a benchmark candidate, not a
promised gain; compare identical sources/toolchains and retain diagnostic symbols
separately. Inspect distributed build provenance before changing package policy.

Windows `selectGLBackend` preloads the WGL implementation and preserves existing
driver-selection environment values. Linux `lllinuxzink.h` explicitly selects its
private GLX vendor, Zink and SDL X11 (not EGL). These paths differ intentionally;
verify the actual selected driver, GPU and environment in each run, including
fallback to system GL. Startup selection is not a per-draw translation layer.

Package README says a loader patch was incorporated without changing its package
version/Mesa revision. Record archive checksum plus patch digest and build options;
`26.3.0-devel` by itself is not sufficient benchmark identity.

### Descriptor and submission policy

Pinned `zink_screen.c` selects `db` automatically when descriptor-buffer requirements
are met, with explicit driver exceptions; otherwise it chooses `lazy`. Forcing
`ZINK_DESCRIPTORS=db` can fail initialization where prerequisites are absent.
Changing `auto` to `lazy` is not a generic modernization fix. Compare modes only on
qualified hardware and record the actual selected mode.

Threaded submission is already enabled conditionally on CPU count/GALLIUM_THREAD;
`ZINK_DEBUG=flushsync` disables it. `zink_draw.cpp` uses dirty-state and batch-change
conditions to avoid some repeated pipeline/dynamic-state work. Do not add another
uncoordinated state cache inside Zink before demonstrating a missed optimization.

### Presentation and patches

The WGL loader patch zero-initializes Kopper metadata and propagates the effective
swap interval. It retains alpha-capable surfaces. Do not remove it as cruft: it
corrects initialization. Measure actual present mode and frame pacing before
trying an opaque-surface or swapchain policy change.

`zink_kopper.c::kopper_present` includes an implicit-sync fence wait guarded by a
driver workaround **and a non-Win32 condition**. It cannot explain a universal
Windows tax. Other waits relate to present fences, retirement and swapchain
recreation; classify their conditions instead of counting wait calls.

The null-shader/program guards and cooperative-matrix build patch are correctness
and build fixes, not proven hot-path overhead. Audit failure propagation under
fault injection separately; no recommendation here to remove them.

Viewer vendor heuristics (`llgl.cpp`, `llfeaturemanager.cpp`, texture handoff)
mix hardware/vendor strings and driver policy. Under Zink, record Mesa identity
and underlying Vulkan ICD separately. Workarounds justified for a vendor's native
GL driver should not automatically be enabled or disabled solely by a Zink vendor
string. No specific wrong classification is claimed without a runtime identity.

## Mesh offload through OpenGL compute

Added scope: minimize CPU work in mesh rendering, using the existing resident
compute infrastructure as the starting point. Optimize end-to-end frame time,
not the percentage of code executing on the GPU. Rasterization and ordinary
rigged-mesh skinning already execute on GPU shaders; they are not new CPU offloads.

### Existing boundary

`llcomputelod.cpp::dispatch` runs shared-avatar LOD selection followed by per-mesh
indirect-command generation. `meshLODC.glsl` selects authored resident ranges;
it does not perform frustum culling or generate simplified geometry. Streaming
demand is copied at most every 250 ms and consumed through completed fences.
Rendering does not wait for that feedback. Retain this separation.

`advanceJob` still allocates and packs geometry on the CPU, maintains per-face
resident buffers, reuses attributes with GPU copies plus CPU mirrors, and handles
16-bit vertex-offset constraints. `LLRenderPass::drawGeometry` is still reached
through the ordinary draw-info submission path. `drawLOD` binds a resident buffer
and submits one command. Thus GPU LOD selection has not eliminated the surrounding
CPU scene traversal, material/state handling or per-face draw submission.

Current eligibility excludes selected/HUD objects, GLTF scene assets and several
moving-object cases; rigged meshes have avatar/skin readiness conditions. Face
preparation adds further material/layout restrictions. Expand these categories
deliberately, with invalidation and fallback tests, rather than removing guards.

### Offload sequence

| Stage | GPU responsibility | Required CPU/data changes and complications |
|---|---|---|
| M1: Persistent scene/geometry data | Consume resident mesh ranges, transforms, bounds, material and skin references | Stable handles with generation counters; compatible geometry arenas and explicit ownership; bounded uploads and retirement. Per-face buffers and per-draw texture binds currently limit batching |
| M2: Visibility plus LOD | Frustum-test resident candidates, choose LOD, write per-view indirect commands | Keep coarse CPU spatial traversal initially. Generate visibility independently for world, shadow and probe views; do not reuse main-camera visibility for shadows. GPU-resident transforms/bounds must remain current |
| M3: Batched draw submission | Consume command lists directly, with no CPU visibility/count readback | Group by compatible program, vertex layout/index type, material bindings and render state. Use fixed-capacity command lists with zero-count culled entries on GL 4.3; compact into a cleared tail if profitable. Indirect-count support is an optional later capability |
| M4: Broader mesh coverage | Apply transforms and per-object data to moving/static legacy and PBR meshes | Audit coordinate spaces, origin shifts, animated UVs, media, editing and material changes. Keep an explicit CPU fallback for unsupported cases, allocation pressure and debugging |
| M5: Geometry preparation/deformation | Parallel attribute conversion, selected rebuild math, morph/deformation work | Move only operations shown costly by captures. Often a vertex shader can apply a transform without an intermediate compute-written buffer. Preserve CPU picking/collision consumers; avoid synchronous GPU readback |
| M6: Occlusion and advanced scheduling | Optional depth hierarchy, conservative visibility and command compaction | Establish temporal/camera-cut rules and conservative bounds, including skinned motion. Compare benefit against depth-pyramid, dispatch and synchronization costs; retain visibility history needed by CPU services |

M1–M3 are the first coherent deliverable: resident opaque meshes culled and selected
on GPU, submitted in materially fewer CPU draw calls. Adding compute culling while
retaining every existing per-face call may save GPU work but does not remove the
main submission cost. Start with opaque meshes; extend alpha/masked/shadow paths
only after their ordering, coverage and pass contracts are documented.

Per-draw identity needs a defined shader interface. Do not assume `gl_DrawID` is
available on a 4.4 baseline; use suitable instance attributes/base-instance data or
feature-gate a shader-draw-parameters path. MDI batches share bound geometry and
state. Arbitrary textures are not made batchable merely by placing material IDs in
an SSBO; compatible texture groups/arrays or another qualified binding strategy
are required. Bindless is not a portable baseline requirement.

Compute skinning is a separate experiment: skin once and reuse across multiple
passes versus current vertex-shader skinning. Its gains are reuse of existing GPU
work, not CPU offload. Account for extra skinned-buffer memory, bandwidth, different
LOD use across views, and synchronization before enabling it. Mesh simplification
on GPU is also separate from selecting existing authored LODs and requires its own
quality/topology evaluation.

### Invariants and acceptance

* Require compute capability at the 4.3 Core baseline and expand mesh coverage
  incrementally. Keep depth peeling and the current gated PPLL path for now;
  deprecate peeling before later retirement. Define PPLL allocation-failure and
  node-overflow behavior explicitly. Ordinary UI/effect blending needs its own
  consumer audit; see the targeted audit's PPLL consolidation section.
* Define producer/consumer barriers for SSBO, vertex/index and indirect-command
  accesses. Ring/slot reuse must not overwrite in-flight data. Keep all render
  decisions on GPU; use delayed bounded feedback for streaming and CPU policy.
* Preserve authored missing/empty/aliased LOD handling, shared-avatar decisions,
  material readiness, deletion/slot reuse, teleport/origin shifts, shader reload,
  and context teardown. Existing mesh tests cover several of these and should be
  extended rather than replaced with isolated shader-only demonstrations.
* Keep CPU geometry required for picking/editing/physics, or explicitly design an
  equivalent asynchronous consumer. Removing all CPU geometry is not a prerequisite
  for GPU-driven rendering. Avoid making the existing CPU buffer mirror stale.
* Enforce resident and temporary memory budgets, including old/new geometry during
  replacement and optional skinned buffers. Do not trade lower CPU time for chronic
  GPU memory pressure or uploads that dominate the saved work.
* Measure direct/indirect CPU submission count, visible/culled mesh count, rebuild
  time, metadata/geometry upload bytes, GPU pass time, memory and frame tails.
  Compare native GL and Zink for small as well as crowded scenes. GPU-bound scenes
  may lose performance from additional compute even when CPU time falls.

## Shared GPU provisioning: rendering and OpenCL decode

Requested starting policy: **60% rendering / 40% J2C decode**, adjustable based on
code capabilities and measured behavior. Interpret this as a soft service-budget
target when both have queued work on the same physical GPU, not a partition of
compute units, memory, worker threads or a guaranteed driver timeslice. Rendering
includes mesh compute, rasterization, shadows/probes and postprocessing. Charge
associated transfers and texture publication once under a documented accounting
convention; their cost must not disappear between the two budgets.

### Existing controls and their limits

The local package source submodule at
`C:/Dev/3p-openjpeg/openjpeg`, revision
`7dcd298d4b9e854c2cdbbc946e06900abd80a660`, provides evidence for an initial design;
match it to the packaged SOURCE_REVISION/archive before implementation. In
`src/lib/openjp2/opencl_backend.c`:

* `initialize` reads `OPJ_OPENCL_WORKERS`, default 4, bounded by MAX_WORKERS=8.
  Once initialized, changing the environment is not a dynamic scheduler interface.
* `acquire_worker` admits jobs against free slots and a 64 MiB pooled-buffer budget,
  reclaims idle buffers and waits with the lock released. Active buffers remain
  protected. Worker availability and memory are existing admission constraints.
* Each worker has an OpenCL command queue. Profiling is optionally enabled through
  `OPJ_OPENCL_PROFILE`; event durations are available for instrumentation. Current
  queue creation does not request a priority or throttle hint.
* Decode reads results back with a blocking read and finishes its queue during
  cleanup. The public decode operation is not a resumable frame-budgeted job.
  Admission cannot preempt kernels already submitted.

Separately, the viewer chooses ImageDecode CPU thread count from core count and
`FSImageDecodeThreads` (`llappviewer.cpp`). CPU threads, OpenCL workers and GPU
occupancy are different quantities. A 3:2 worker ratio does not produce 60:40 GPU
service. Existing mesh preparation milliseconds are CPU scheduling limits, also
not a GPU share.

### Proposed controller

1. **Identify contention.** Match GL/Zink's physical GPU to the OpenCL device using
   stable device identity where available. Decode auto-selection can choose another
   GPU. Separate-device execution needs memory/transfer and CPU limits, not an
   artificial single-GPU 60:40 constraint.
2. **Observe without blocking rendering.** Collect delayed GL timer-query results,
   completed CL event durations, outstanding jobs/estimated cost, transfer bytes,
   decoded-image backlog and frame p95/p99. Use durations, not raw cross-API
   timestamps, unless clocks are explicitly correlated. Overlapping kernel times
   cannot simply be added into an exact utilization percentage.
3. **Control work before submission.** Add a thread-safe codec admission/statistics
   interface for dynamic active-job limits and estimated-cost credits. Keep a
   viewer-side priority queue; defer jobs instead of occupying every CPU decode
   worker waiting for GPU permission. Preserve the current codec API/fallback when
   the optional scheduling interface is absent. Integrate with `acquire_worker`
   rather than racing its memory/worker bookkeeping.
4. **Use weighted credits over a rolling interval.** Start with 60:40 weights and
   reconcile estimated decode costs with completed-event measurements. Bound the
   amount of outstanding GPU decode work so a burst cannot overwhelm a frame.
   Admit only bounded mesh-preparation work beyond essential rendering work too.
   Carry limited credit, not unlimited idle-time credit that permits a later burst.
5. **Protect frame latency, retain decode progress.** Reduce decode admission when
   frame deadlines are missed; increase it gradually when headroom returns. Apply
   smoothing/hysteresis and a starvation bound. Prioritize visible missing/coarse
   textures and age queued requests. Do not automatically respond to throttling by
   moving all work to CPU and creating a new CPU bottleneck.
6. **Borrow unused capacity.** When decode is idle, rendering can consume the GPU.
   When rendering has headroom, decoding may exceed its nominal 40%. Do not reserve
   capacity that remains unused. Treat 60:40 as an initial tuning objective, with
   frame-time and texture-readiness outcomes taking precedence.

If a single admitted tile/kernel lasts too long, worker throttling alone cannot
protect frame latency. Audit safe code-block/kernel batch boundaries and add finer
submission granularity in the codec where needed, preserving wavelet dependencies,
buffer lifetime, error fallback and pixel correctness. Also bound completed decoded
images awaiting upload; accelerating decode into an unbounded upload backlog is not
an improvement. Budget memory and transfer pressure independently of compute time.

OpenCL priority/throttle extensions are optional hints and do not guarantee a
cross-API allocation against OpenGL or Zink's Vulkan queues. They can supplement
admission control after driver testing, not implement the 60:40 contract by
themselves. See [Khronos priority-hints specification](https://registry.khronos.org/OpenCL/specs/unified/refpages/man/html/cl_khr_priority_hints.html).

### Qualification

Compare uncontrolled baseline, conservative fixed decode concurrency, and adaptive
60:40 policy. Test warm regions, cold login/teleports, crowded mesh scenes with
texture arrivals, low VRAM, large textures, CPU decode fallback, multiple GPUs,
and native GL versus Zink. Record frame tails and missed deadlines, time to visible
texture readiness, decode throughput, queue age, memory and transfer bytes. Require
bounded queues, continued progress, no deadlocks and decoder correctness. Publish
observed service estimates rather than claiming an enforced hardware percentage.

## Modernization sequence and acceptance

1. **Freeze benchmark identity and capture baseline.** Source SHA, archive hashes,
   Mesa revision/patch digest/build options, GL version/profile, GPU/ICD, effective
   saved settings plus feature masks, window resolution, vsync, presentation mode,
   driver env vars. Use identical Release-equivalent builds; diagnostic builds are
   separate and must not be mixed into timing comparisons.
2. **Qualify Windows/Linux 4.3 Core.** Exercise ordinary rendering, depth peeling and PPLL,
   picking, convex physics visualization, HUD/UI/text, terrain, avatars, media,
   snapshots and shared-context uploads. Fix reachable compatibility calls first.
   Test compute and PPLL coverage, allocation failure and fragment-list overflow.
3. **Enforce the selected Core contract.** Main/shared/recreated contexts on both
   platforms, diagnostic rejection below minimum, saved-setting reconciliation,
   explicit capability records. Replace synthetic-version mutation with targeted
   workarounds. Then remove proven unreachable compatibility/GLSL branches.
4. **Take small measured wins.** Correct upload overlap; consolidate duplicate
   hero-probe work; dirty-rectangle glyph updates; explicit state ownership and
   safe bind elision. These require focused correctness checks and captures.
5. **Restructure throughput where captures justify it.** Separate transient upload
   rings from persistent geometry, batch texture publication, then batch compatible
   opaque submissions. Compute and MDI are baseline capabilities; buffer storage remains feature-gated.
   Persistent mapping remains an implementation choice. Implement mesh stages
   M1–M3 as the first GPU-offload milestone, then expand coverage and geometry work
   according to measurements. Keep transparency order, skin palettes and CPU
   picking consumers explicit.
6. **Coordinate rendering and decode provisioning.** Instrument contention and add
   bounded admission before increasing mesh-compute load. Trial the adaptive 60:40
   service target against fixed-concurrency controls and frame/texture latency.
7. **Tune the shipped Mesa package.** A/B descriptor policy, pipeline compilation
   and LTO on the pinned source; retain only reproducible wins. Change underlying
   Zink algorithms only after traces identify a driver-side missed optimization.

Measure CPU frame time, GPU frame time, p50/p95/p99, render-thread and upload-thread
waits, calls/uploads per frame, descriptor updates, Vulkan queue submissions,
pipeline compilations/cache hits, and VRAM. Include warm steady state, cold shader
cache, texture arrivals/teleports, UI glyph insertion, dense geometry, mirrors,
PPLL under high depth complexity, and window resize. First uncapped for attribution, then normal
presentation settings for user-visible latency. Compare native GL and bundled Zink
on the same GPU and repeat across supported AMD/NVIDIA/Intel configurations.

Decoder work competes for resources too: record the installed OpenJPEG package and
whether its OpenCL path is actually selected, rather than assuming every decode
runs on GPU. Keep texture-loading workload equivalent between backend captures.

Performance acceptance: improvement in the affected phase without a regression in
frame-tail latency, correctness, GPU time or memory on the agreed matrix. No fixed
percentage estimate is justified by this static audit. Open questions are actual
hotspot weights, end-to-end 4.3 Core qualification, distributed Mesa build provenance,
and platform/driver-specific presentation cost.

## References

* Viewer locations are relative to `indra/` and the source revision above.
* Mesa paths refer to the pinned revision above, not current upstream main.
* [Package source and patches](https://github.com/anne-skydancer/3p-mesazink)
* [Mesa Zink documentation](https://docs.mesa3d.org/drivers/zink.html)
* [Khronos OpenGL registry](https://registry.khronos.org/OpenGL/index_gl.php)
