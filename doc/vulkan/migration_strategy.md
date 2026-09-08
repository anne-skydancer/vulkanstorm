# OpenGL-to-Vulkan migration strategy

**Status:** DRAFT for review (2026-09-06)  
**Baseline:** `master` @ `bf7d58111b`  
**Related:** [design_overview.md](design_overview.md) · [phase1_bootstrap.md](phase1_bootstrap.md) · [phase3_v2_ui_plan.md](phase3_v2_ui_plan.md) · [phase3_v2_m0_design.md](phase3_v2_m0_design.md) · [capability_probe_design.md](capability_probe_design.md) · [shared_assets.md](shared_assets.md)

## Guiding principle

> Development is now governed by the [native Vulkan invariants](native_vulkan_invariants.md),
> derived from the [approved reverse engineering](reverse-engineering/README.md).
> They preserve the result-equivalence principle below while defining its review
> gates and precedence. In particular, exact comparison applies to established
> exact contracts; floating-point/image tolerances require reference evidence,
> not a blanket assumption that every attachment is cross-driver byte-exact.
> This historical strategy does not override the separate OpenGL modernization
> roadmap or establish current implementation/validation status.

> **Every load-bearing feature of the rendering pipeline must yield an equivalent _result_, not necessarily equivalent _code_.**

This is the migration's single governing rule, established 2026-09-06. It generalizes the UI plan's §0 rule ("follow the results, not the logic") to the entire pipeline.

What it means in practice:

- The OpenGL pipeline is the **reference for results** — the pixels, the per-attachment contents, the pass ordering, the observable frame — stated independently of how GL's code produces them.
- Vulkan code is judged by whether its **output matches the contract**, never by whether it structurally resembles the GL implementation. A Vulkan pass may use different algorithms, different internal representations, different pass decomposition, and different math — *if and only if* the observable result is equivalent within the documented tolerance for that feature.
- "Load-bearing" is the qualifier that scopes the rule: if downstream passes, other features, or the user's eyes depend on a value, that value is a contract. Internal scaffolding that leaves no observable trace is free to differ completely.
- The rule is a **license for Vulkan-native design**, not a mandate to mimic GL. It is why we may replace GL's shared-context texture threads with a transfer queue, FBO stacks with a render graph, immediate-mode state with PSOs, and per-frame re-upload with ring buffers — none of those are results. And it is why G-buffer packing, blend modes, painter's order, and HDR range semantics *are* results: they cross pass boundaries and are consumed downstream.

The consequence that disciplines everything below: **equivalence is proven at the seam where a result is consumed, by comparison against the GL reference — not asserted from code structure.** Where a result is exact-valued (attachment contents, blend output, clip rects), the tolerance is byte-exact; where it is perceptual (final lit frame), the tolerance is documented and perceptual/numeric.

## Executive recommendation

Continue the current **greenfield, backend-exclusive Vulkan** direction. Do not translate `LLRender` call-for-call and do not introduce a low-level RHI implemented by both OpenGL and Vulkan. The OpenGL pipeline should remain the immutable correctness and performance reference while Vulkan gains a frame-oriented world renderer.

The next major investment should not be a world draw-pool port yet. First harden the current Vulkan frame/resource model, because the UI implementation already exposes two synchronization patterns that will become release blockers when world-scale streaming is added:

1. **Shared mutable UI vertex storage across two frames in flight.** `LLVKUI2D` owns one mapped vertex buffer and resets its append offset every frame; `LLVKContext` waits only on the current frame slot's fence. With `kFramesInFlight = 2`, frame N+1 may rewrite storage while frame N is still reading it.
2. **Synchronous in-place texture updates before the frame fence wait.** Media and font-atlas updates submit a copy and wait for that copy, but do not prove that the image is no longer sampled by earlier in-flight frames.

Both are straightforward to fix with per-frame/ring-buffer regions and a retire queue, but the world migration should inherit the corrected model rather than replicate the hazard.

## Current OpenGL survey

### Architecture

- `LLRender` is the dominant legacy submission surface: global/thread-local state, CPU matrix stacks, fixed blend/depth/cull state, texture units, immediate-mode `begin`/vertex/`end`, and deferred `flush()` batching. `extern thread_local LLRender gGL` makes the implicit context pervasive.
- `LLVertexBuffer` owns GL vertex/index buffer handles and exposes GL-oriented mapping, draw, and attribute-layout operations. The disabled `ENABLE_GL_WORK_QUEUE` block shows a prior attempt at threaded GL work, but the active model is effectively main-thread upload/submission.
- `LLImageGL` owns GL texture handles, discard levels, upload/update/readback, and statistics. Optional threaded texture/media work uses a shared GL context (`LLImageGLThread`) plus fence/flush and main-thread handoff.
- `LLGLSLShader` owns GL program objects, uniform locations, sampler channels, matrix/light hashes, profiling queries, and GLTF permutations.
- `LLRenderTarget` is an FBO/attachment stack. It is the cleanest conceptual seam: allocation, binding, clear, flush, and attachment readback map naturally to Vulkan attachments, but the implementation and callers still expose GL handles and global FBO state.
- `LLPipeline` drives scene generation and rendering: dynamic texture updates, camera/environment/HUD updates, geometry creation/update, culling, state sorting, shadow generation, deferred geometry, deferred lighting, post-processing, UI, then swap.

### Current per-frame GL flow

In `display()` the main world path is approximately:

1. Window resize and state validation.
2. Picking, dynamic textures, camera/environment/HUD updates.
3. Geometry creation/update and `gPipeline.updateGL()`.
4. Culling (`gPipeline.updateCull`) and image/decode updates.
5. `gPipeline.stateSort`, sky update, shadow generation.
6. Deferred G-buffer geometry (`renderGeomDeferred`) into `deferredScreen`.
7. Deferred lighting and post/finalization.
8. HUD, 3D UI, 2D UI, debug text.
9. `swapBuffers()`.

### Migration surface size

Current repository scan:

- `newview` has **417 raw `gl*` call sites** across 39 C++ files. The largest owners are `pipeline.cpp` (100), `lldrawpoolterrain.cpp` (61), `llviewerwindow.cpp` (49), `llspatialpartition.cpp` (24), and `llviewerdisplay.cpp` (22).
- The viewer ships **225 GLSL files** under `newview/app_settings/shaders`, concentrated in deferred, avatar, object, lighting, environment, interface, and post-processing families.
- The raw calls include direct clears, viewport/scissor changes, polygon state, texture parameters, readbacks, and occlusion queries in addition to work nominally going through `LLRender`.

This is too coupled for a wrapper-only migration. The correct seam is higher: preserve scene production/culling/material data, then have Vulkan render passes consume backend-neutral frame work items.

## Current Vulkan survey

### What is already built

- `llvulkan` owns instance/device/queue/swapchain setup through `LLVKContext`, with volk, VMA, Vulkan 1.3, dynamic rendering, validation-layer logging, and a bandwidth probe.
- `LLVKSession` binds the Vulkan backend to a GL-free viewer window, pumps frames, handles resize, and shuts down before window destruction.
- The greenfield UI renderer walks readable `LLView` state and emits into `LLVKUI2D`; it deliberately does not execute GL-coupled `draw()` implementations.
- `LLVKUI2D` batches UI geometry by blend/texture/scissor/topology and records draws into the current command buffer.
- Vulkan-native image and text paths exist (`LLVKUIImage`, `LLVKText`), plus a deterministic UI scene and raw-RGBA capture harness for GL/Vulkan diffs.
- The backend selector, restart flow, Vulkan availability probe, GPU-facts handoff, and About-panel integration are present.
- Current scope is Windows-only; non-Windows `LLVKSession` methods are stubs.

### Important boundary caveat

The runtime path is intentionally GL-free, but the module boundary is not yet physically clean: `llvulkan/CMakeLists.txt` still links `llrender` and `llui`, and `llvktext.h` exposes `LLFontGL` types/static state. That is acceptable as a tactical bridge, but the world plan should replace this with neutral font/style descriptors so `llvulkan` can be built and reasoned about independently.

## Findings that should gate world work

### P0 — Correctness/synchronization

1. **Per-frame mutable-buffer race**: one UI vertex buffer is reused while two frames can be in flight. Introduce one buffer/region per frame slot, or a frame-indexed ring large enough for the worst UI frame. Retire overflow buffers only after the fence/timeline value that used them.
2. **Texture update/read hazard and stalls**: update font atlases/media/dynamic images through a transfer ring and a resource-retire queue keyed to frame completion. If an image may be sampled by an earlier frame, double-buffer it or defer its update. Do not call `vkDeviceWaitIdle` or a fresh fence wait per texture in the frame hot path.
3. **No post-window fallback**: if `LLVKSession::start()` fails after a Vulkan-native/no-GL window has been created, startup logs a warning but the window cannot present. Add a controlled recovery path (recreate the window as GL, or terminate with an actionable notification); do not expose a permanently black session.

### P1 — Engineering quality and observability

4. Add reproducible shader builds. Compiled SPIR-V is packaged, but there is no visible CMake rule that proves `ui2d.vert/.frag` and the committed `.spv` files are in sync. Build with glslc/glslang when available, validate with `spirv-val`, and fail CI on source/binary drift.
5. Add a persistent `VkPipelineCache`, keyed/serialized safely, before world shader variants arrive. The 2D pipelines currently pass no cache to `vkCreateGraphicsPipelines`.
6. Add debug names and frame/pass markers (`VK_EXT_debug_utils`), persistent timestamp query pools, and Tracy/vendor-profiler ranges. Current diagnostics are mostly logs and ad-hoc counters.
7. Adopt synchronization2-style dependency descriptions for new world code rather than expanding legacy `VkImageMemoryBarrier` call sites. Keep the current UI path unchanged until its resource hazards are fixed and covered by tests.
8. Honor vsync/present settings. Swapchain creation currently chooses FIFO unconditionally.
9. Add finite timeout/watchdog behavior around frame fences and acquire, plus explicit `VK_ERROR_DEVICE_LOST` handling.

### P2 — Scaling and portability

10. Introduce lifetime-aware descriptor allocation. The UI pool is fixed at 4,096 sets and descriptors are not individually freed; this is adequate for static UI but not world streaming.
11. Split platform surface/session support for SDL2/Linux and MoltenVK/macOS only after Windows world parity has a stable test harness. Avoid spreading an unproven world architecture across platforms too early.
12. Keep Mesa/Zink as a comparison bridge, not as the migration architecture. It is useful for compatibility and baseline experiments but does not remove the need for a native Vulkan renderer.

## Target Vulkan architecture

### Governing boundaries

- **Share CPU-side facts and production data**: scene graph, culling result, camera/environment state, material metadata, decoded pixel data, font registry, colors, and image declarations.
- **Do not share GPU objects or implicit render state**: textures, buffers, pipelines, descriptors, render targets, and transient attachments are Vulkan-owned.
- **Choose the backend once at the process/window seam.** Do not interop GL and Vulkan in one window for the main migration path.
- **OpenGL remains the reference.** Changes to GL should be limited to harness hooks and neutral data extraction; avoid behavior perturbation.

These boundaries are the architectural expression of the guiding principle. They separate *what may differ* (all GPU-side mechanism: objects, state model, threading, memory) from *what must not differ* (the results consumed downstream). "Do not share GPU objects" is not dogma — it follows from the principle: a GPU object's layout and lifetime are not a result, so each backend owns the representation that suits it. "Share CPU-side facts" likewise: the scene graph, culling result, and decoded pixels are inputs to the result, so diverging them would diverge the output before rendering even begins.

### Proposed world-frame model

1. `LLVKFrame` / frame context: owns frame index, command allocator, timeline value, per-frame uniform/vertex/index ring, timestamp queries, and retire lists.
2. `LLVKTransferQueue`: asynchronous staging ring, upload batches, texture/buffer residency transitions, and transfer-to-graphics ownership synchronization. Use a dedicated transfer queue where beneficial, with explicit ownership transfers.
3. `LLVKResourceCache`: static geometry/textures, streaming resources, and frame-graph transient attachments. VMA remains the allocator; add budget queries and per-class accounting.
4. `LLVKShaderLibrary`: shader manifest, offline GLSL→SPIR-V build, reflection data, descriptor layouts, specialization variants, and cache validation.
5. `LLVKPipelineCache`: deterministic pipeline-state keys for blend/depth/stencil/cull/topology/formats/sample count, backed by a persistent disk cache.
6. `LLVKRenderGraph`: explicit pass inputs/outputs, layout transitions, load/store behavior, transient-resource aliasing, and synchronization. Dynamic rendering is a good fit; the graph supplies the missing dependency discipline. **The graph's core topology is the deferred frame** (G-buffer fill → deferred lighting → post-deferred alpha/water → post chain → UI composite), not a generic forward path — see the revised phasing below.
7. `LLVKSceneRenderer`: converts culling/state-sort results into pass work items and secondary-command-recording jobs. Start single-threaded; parallelize only after deterministic output and stable resource ownership.
8. `LLVKGBuffer` + deferred-lighting pass: the structural center of the world renderer. The G-buffer attachment set (diffuse/albedo, normal, specular/emissive, PBR channels, depth) is sized and laid out once in Phase C and never treated as an optional add-on, because every downstream pass in the GL reference (deferred lighting, alpha, water, reflections, post FX, UI composite) consumes it.

## Migration map

| OpenGL concept | Vulkan successor |
|---|---|
| `LLRender` immediate calls and matrix stacks | Frame work items plus per-pass camera/material constants; no one-to-one state wrapper |
| `LLGLState`, RAII enable/disable | Immutable PSO state plus explicit dynamic viewport/scissor/depth state |
| `LLVertexBuffer` | Static device-local geometry plus per-frame ring buffers for dynamic streams |
| `LLImageGL` / texture name | Versioned `LLVKTexture` handles, staging ring, explicit layouts, residency/retire tracking |
| GL shared-context texture thread | CPU decode remains threaded; Vulkan transfer queue performs uploads |
| `LLGLSLShader` program/uniform maps | SPIR-V module + reflection manifest + descriptor layouts + pipeline variants |
| `LLRenderTarget` FBO stack | Render-graph attachments/pass nodes; physical image views allocated by resource cache |
| `glClear`, `glViewport`, raw state in `newview` | Pass declarations and command recording owned by `llvulkan` |
| Occlusion/query calls | `VkQueryPool` with delayed, non-blocking result consumption |
| Fixed pipeline remnants (`glTexGen`, `glPolygonMode`) | Shader-generated coordinates and PSO rasterization state |
| `swapBuffers` | acquire → graph execution → submit → present, with per-image sync |

## Phased plan

### Phase A — Harden the current Vulkan foundation

Exit criteria:

- Per-frame buffer and texture hazards eliminated and covered by synchronization validation.
- Startup/session failure cannot leave a black, unrecoverable window.
- SPIR-V source/build/package drift is CI-detectable.
- Debug markers, persistent GPU timestamps, memory-budget counters, and upload/submit counters exist.
- Current UI capture harness remains byte-stable; GL reference unchanged.

### Phase B — Establish the world data contract and Vulkan core

Build the resource, transfer, descriptor, pipeline-cache, and render-graph layers. At the same time, define a compact backend-neutral draw item produced from the existing cull/state-sort result: geometry handle, material/shader variant, texture handles, transform, sort key, pass mask, and render-state key.

**Phase B must also deliver the G-buffer renderer itself** — the `LLVKGBuffer` attachment allocation and the MRT (multiple-render-target) fill pipeline — because it is shared infrastructure, not content. In Vulkan, "render to 4 attachments with depth" is pipeline construction work (attachment formats baked into the PSO, descriptor layouts for the lighting pass that will read them, layout transitions between fill and readback). If Phase B's exit test is a single-attachment color pass, every Phase-C draw-pool port starts by rebuilding its pipelines. The G-buffer machinery must exist and be proven before the first content port begins.

The Phase B exit test is a **content-free G-buffer equivalence test** — not a "trivial" stand-in with invented formats, which would validate a skeleton that is not the real one. The contract is feature-equivalence with the GL G-buffer at the **format, layout, pass-topology, and shader level**. Only the *geometry* is trivial; the *shaders are not*.

### The shaders are the contract

It is not sufficient for the Vulkan pipeline to accept the same formats. The G-buffer's meaning is defined by the shader code that packs and unpacks it. The canonical example is the normal encoding, which lives in `class1/deferred/globalF.glsl:46-61` and is consumed by every G-buffer writer and reader:

```glsl
vec4 encodeNormal(vec3 n, float env, float gbuffer_flag) {
    float f = sqrt(8 * n.z + 8);
    return vec4(n.xy / f + 0.5, env, gbuffer_flag);   // stereographic projection
}
vec4 decodeNormal(vec4 norm) {
    vec2 fenc = norm.xy*4-2;  float f = dot(fenc,fenc);
    float g = sqrt(1-f/4);
    return vec4(fenc*g, 1-f/2, ...);                  // exact inverse
}
```

This stereographic (not octahedral) packing is what makes `norm.xy` fit the attachment while `norm.z` is reconstructed in the reader. The .b channel carries `envIntensity`, the .w channel carries `gbufferFlag` (`GBUFFER_FLAG_HAS_ATMOS` etc.) — see `getGBuffer` in `gbufferUtil.glsl:36-59`. If the Vulkan writer uses a different packing, every lighting/shadow/reflection pass downstream reads garbage that no attachment readback would flag as wrong, because the *format* is still valid.

Therefore the rule for all Vulkan world shaders, starting in Phase B — stated per the guiding principle, as a *result* requirement rather than a code requirement:

**The packed values a Vulkan shader writes to the G-buffer must equal the packed values the GL shader writes for the same inputs, within the attachment format's representable precision.** The `encodeNormal`/`decodeNormal` round-trip, the `gbufferFlag` bit assignments, and the channel semantics (normal.xy, envIntensity.z, flag.w) are results — downstream readers depend on their exact values. The GLSL that produces them is not.

This permits two legitimate implementations, both compliant:
- **Shared utility library** (preferred where it fits): `globalF.glsl`, `gbufferUtil.glsl`, etc. `#include`d into both pipelines so the math has one source of truth. Lowest divergence risk.
- **Independent Vulkan implementation** (permitted where the GL formulation is awkward in Vulkan): a different but provably equivalent encoding is acceptable *if* it produces the same packed bytes for the same inputs and the same decoded normal after the round-trip. The equivalence is proven by the encode→store→read→decode round-trip test against the GL reference, not by code inspection.

What is **not** negotiable either way: the decoded result and the cross-pass channel contract. A Vulkan shader may not adopt a "better" normal encoding, a different flag assignment, or a different HDR range clamp, because those are consumed by other passes and would break equivalence at the seam. The binding model (descriptor sets vs uniforms, push constants, `gl_FragCoord`/texture-unit conventions) is *not* a result and is free to be Vulkan-native.

The G-buffer contract is derived from the GL source of truth, `addDeferredAttachments` (pipeline.cpp:381) and `allocateScreenBufferInternal` (pipeline.cpp:978):

| Slot | GL format | GL semantic | Vulkan equivalent |
|---|---|---|---|
| Attachment 0 (from `deferredScreen.allocate`) | `GL_RGBA` | diffuse/albedo | `VK_FORMAT_R8G8B8A8_UNORM` |
| Attachment 1 | `GL_RGBA` | specular OR PBR ORM | `VK_FORMAT_R8G8B8A8_UNORM` |
| Attachment 2 | `GL_RGBA16` (HDR) / `GL_RGB10_A2` (non-HDR) | stereographic-packed normal (xy) + envIntensity (z) + gbufferFlag (w) | `VK_FORMAT_R16G16B16A16_UNORM` / `VK_FORMAT_A2B10G10R10_UNORM_PACK32` |
| Attachment 3 (if `RenderEnableEmissiveBuffer`) | `GL_RGB16F` (HDR) / `GL_RGB` (non-HDR) | PBR emissive / env intensity | `VK_FORMAT_R16G16B16_SFLOAT` / `VK_FORMAT_R8G8B8_UNORM` |
| Depth | shared with `screen` (`shareDepthBuffer`) | scene depth | dedicated `D32_SFLOAT` attachment shared with the HDR scene target |
| HDR scene target | `GL_RGBA16F` | lit accumulation | `VK_FORMAT_R16G16B16A16_SFLOAT` |

The test must therefore be:

- **Real attachment set**: the full table above, at a fixed 512×512 extent — including the non-HDR variant (`RGB10_A2` normals) as a second pipeline permutation, since the GL reference supports both and the formats differ structurally.
- **Real graph topology**: G-buffer fill → deferred lighting (fullscreen, reads all G-buffer attachments via descriptors) → tonemap → present, with the real layout transitions (COLOR_ATTACHMENT → SHADER_READ_ONLY → back per frame).
- **Real shaders**: the fill shader calls the *ported* `encodeNormal` (shared-library, bit-equivalent) on a known normal and writes the packed result to attachment 2's real location; the lighting shader calls the ported `decodeNormal`/`getGBuffer` and reconstructs it. The round-trip encode→store→read→decode is validated against the GL reference value to within the attachment format's quantization — proving the packing survives the real format (this is exactly what catches e.g. `RGB10_A2` destroying the low bits of the stereographic encoding, or a sign/layout error that a constants-only test would never see).
- **Trivial content only in the geometry**: two screen-space triangles with hardcoded vertices, no draw pools, no scene graph, no textures, no camera/culling/materials.
- **Byte-exact readback**: every attachment is read back and compared against the value the *GL shader path* produces for the same inputs (not merely against host-computed constants), proving every channel of every attachment survives fill → transition → read intact and that the Vulkan shader math matches the GL shader math through the real storage format.

"Feature-equivalent" here means: any GL draw call that writes the G-buffer could be re-targeted at this Vulkan pipeline — same ported shader code, same inputs — and produce numerically identical attachment contents, and any GL reader (`getGBuffer`) could consume the result identically. What is deliberately *not* yet equivalent is scene-level shading breadth (multiple light types, shadows, sky, water) — those are validated content-side in Phase C/D. What must not differ is the shader-level data contract: packing, channel semantics, flags, and conversions.

Exit criteria: the G-buffer equivalence test passes byte-exactly on both HDR and non-HDR attachment permutations; the fill→light→tonemap→present graph validates clean; attachments survive resize and repeated frames; GL output unchanged.

### Phase C — Deferred core world pipeline (structural, not optional)

**Correction (2026-09-06): deferred rendering is the skeleton of the world renderer, not a Phase-E add-on.** The GL reference's world path IS the deferred pipeline: `deferredScreen` G-buffer fill (`renderGeomDeferred`) → deferred lighting (`renderDeferredLighting`) → post-deferred alpha/water (`renderGeomPostDeferred`) → post chain (`renderFinalize`) → UI composite. A forward-first world milestone would render pixels no setting combination of the reference produces, could not be visually diffed against it, and would be thrown away. Deferred therefore lands here, in the first world phase.

Port in this order:

1. **G-buffer pass**: static opaque geometry writing G-buffer values through the MRT machinery proven in Phase B. Camera matrices from the real viewer camera. Shaders ported family-by-family from `app_settings/shaders/deferred/` (start: `gbufferUtil`, terrain, simple/fullbright). The attachment set, descriptor layouts, and graph topology are *not* new work here — Phase B delivered them.
2. **Deferred lighting pass**: fullscreen light accumulation reading the G-buffer (start: sun/ambient; the GL `deferred/sunLightF` family). Output to the HDR scene target (`GL_RGBA16F` equivalent).
3. **Sky/void water** in their GL pass positions (sky renders into the deferred frame, not as a forward backdrop).
4. **Post-deferred alpha**: alpha-masked and alpha-blended geometry with GL-compatible ordering (the `renderGeomPostDeferred` position), sampling scene depth from the G-buffer.
5. **Minimal post chain**: exposure/tonemap/gamma (the `postDeferred*` family subset needed to reach the swapchain with GL-matching output) — the full FX chain lands in Phase D, but the gamma/tonemap tail is required here or no pixel comparison is meaningful.
6. **UI composited above the world** (the existing Vulkan 2D pass renders on top).

Exit criteria: captured GL/Vulkan scene pairs match under documented tolerances **for the full deferred frame** (G-buffer fill through tonemap); world startup, login, resize, teleport, and texture streaming are stable; per-pass GPU timestamps recorded; performance baseline recorded.

### Phase D — Feature-family parity

Expand by shader/pass family rather than by file:

- Full material set: normal/specular (bump), PBR/glTF metallic-roughness.
- Rigged avatars and matrix palettes.
- Trees/grass/impostors.
- Water, environment/atmospherics, and reflection probes.
- Shadow passes (sun cascades, spot).
- Full post-processing chain: SSAO, DoF, bloom/glow, FXAA/SMAA, haze/underwater.
- Advanced transparency (PPLL and depth peel), occlusion queries, picking/readbacks, mirrors/hero probes.

Exit criteria per family: deterministic visual comparison, feature-specific stress scene, memory budget, and no new validation errors. Transient attachment aliasing is permitted where the graph proves lifetimes do not overlap.

### Phase E — Optimization, platforms, and rollout

Only after correctness is stable:

- Parallelize pass/work-item recording with per-thread command pools.
- Add descriptor indexing/bindless texture arrays where device support and content scale justify it.
- Add multi-draw indirect and draw compaction for large pools.
- Tune descriptor reuse, barriers, transfer batching, and pipeline warm-up.
- Port SDL2/Linux and MoltenVK/macOS.
- Move Vulkan from developer setting to opt-in beta, then default only after telemetry shows parity.

## Correctness gates

The gates below are the guiding principle made executable: each one compares a *result* against the GL reference, at the seam where that result is consumed.

1. **Build gates**: shader compile + `spirv-val`; no GL headers/API calls in `llvulkan`; CI verifies packaged SPIR-V.
2. **Validation gates**: standard, synchronization, and GPU-assisted validation in internal runs; zero new errors is the merge bar.
3. **Result gates**: for each load-bearing feature, define the seam where its result is consumed and compare against the GL reference there. Exact-valued results (G-buffer attachment contents, blend output, clip/scissor rects, painter's order) are compared byte-exactly; perceptual results (final lit frame) use documented numeric/perceptual tolerances. The existing UI byte-exact harness and the Phase B G-buffer round-trip are the first two instances of this gate.
4. **State-transition gates**: login/logout, teleport, resize/minimize/maximize, display-scale changes, window occlusion, device loss, low VRAM, shader reload, and snapshot capture.
5. **Reference isolation**: every phase proves the GL path is unchanged, and the Vulkan path never depends on a GL context.

## Performance plan

Measure before optimizing. Establish GL and Vulkan baselines for:

- login UI;
- empty region;
- representative residential region;
- avatar-heavy region;
- texture-storm/teleport;
- post-processing and shadow presets;
- snapshot and resize.

Track p50/p95/p99 CPU frame time, render-thread time, queue submit time, GPU pass time, present wait, upload bytes, descriptor updates, pipeline creations, swapchain recreations, VMA heap usage, and dropped/late frames.

Initial release target should be **correctness with no meaningful regression** (for example, Vulkan p95 within 5% of GL on the agreed scenes). Optimization targets should be set from the baseline; likely wins are lower driver overhead, fewer texture-state transitions, batched uploads, pipeline-cache warm-up, parallel command recording, and bindless material resources. Do not accept a large Vulkan performance win if it relies on missing visual features or weaker correctness.

## Recommended immediate backlog

1. Fix UI vertex-buffer frame ownership and texture-update hazards.
2. Add controlled recovery for Vulkan session startup/runtime failure.
3. Add shader build/validation/packaging consistency checks.
4. Add persistent pipeline cache and debug names/timestamp instrumentation.
5. Extend validation runs to synchronization and GPU-assisted modes.
6. Define the world draw-item contract and render-graph skeleton.
7. Create a deterministic world capture/replay harness before porting draw pools.
8. Prototype the **deferred core** first — G-buffer fill + sun/ambient deferred lighting + tonemap tail with terrain/static opaque content; defer avatars, alpha OIT, probes, and the full post chain until the resource model is proven.

## External guidance used

- Khronos Vulkan Guide: synchronization, memory allocation, threading, pipeline caching, profiling.
- Khronos synchronization examples.
- NVIDIA Vulkan dos and don'ts.
- Khronos OpenGL/Vulkan interoperability sample. Interop is useful as a reference implementation, but it is not recommended as the viewer's main strategy because the current design intentionally uses backend-exclusive windows and already discovered the native-window ownership constraint.
