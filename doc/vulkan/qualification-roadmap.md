# Qualification and implementation roadmap

Current mandate: DiligentCore is the user-selected GHI, with pinned dependency
preparation on `vkstorm-vulkan`. Library-selection comparisons below describe the
historical audit, not a requirement to choose again. These responsibility-level
designs do not establish a complete insertion catalog. The
[current insertion audit](diligent-insertion-catalog.md) must cover every necessary
point and keep unresolved in-scope paths as acceptance blockers. Runtime parity
and platform/GPU qualification remain separate from source coverage.

## First deliverable for vkstorm-vulkan

The current user mandate supersedes the historical library-selection and rollout
plan below. DiligentCore is the selected GHI candidate; its dependency preparation
is on `vkstorm-vulkan`. Selection and dependency preparation do not establish
runtime suitability or a working renderer.

Vulkan is a peer rendering backend to OpenGL. OpenGL fallback is not achievable
for this deliverable and must not be promised as startup, device-loss or recovery
behavior. An existing OpenGL viewer may supply reference images in a separate
run, but Vulkan execution must own its rendering resources and presentation.
GL/Zink is a GL backend created for AMD GPUs affected by rendering regressions
caused by bugs in the AMD OpenGL ICD. It is not a Vulkan fallback.
If Vulkan initialization or required capabilities fail, report the cause and
stop the Vulkan session without attempting OpenGL rendering.

The first executable milestone is a minimal native-Vulkan UI and in-world chat
interface, with **no world rendering**. In-world means a connected session with
working chat; it does not require a rendered scene or world-space chat bubbles.
Use a clear background behind the UI. Preserve the CPU/session services needed
for authentication, connection, incoming messages and outgoing chat.

| Requirement | Acceptance evidence |
|---|---|
| Native Vulkan through DiligentCore | Device, surface/swapchain, UI resources, draw submission and presentation use the pinned DiligentCore Vulkan backend. No GL context, GL draw/upload callback or Zink translation is needed by the milestone's execution path. |
| Minimal usable UI | Login/connection status, chat transcript and input render correctly. Verify fonts, UI textures, alpha blending, clipping, draw order, keyboard focus, typing and scrolling, including Unicode text. |
| Connected chat | Log in, join a session, send and receive nearby chat, and display disconnection/errors. Record observed behavior; a mock transcript alone does not qualify. |
| No world rendering | World geometry, avatars, terrain, sky, water, shadows, probes and world postprocessing are not submitted. World-dependent previews and auxiliary renders are deferred. Trace startup and connected-session callbacks so disabling the world draw does not leave hidden GL dependencies. |
| Correct lifecycle | Exercise resize, minimize/restore, swapchain recreation, resource upload/retirement and shutdown. Required Vulkan validation and synchronization validation have zero unexplained errors. A device failure reports a clear failure; there is no OpenGL recovery contract. |
| Reproducible delivery | Use Autobuild and a fully staged RelWithDebInfo viewer without an installer, including GHI/runtime libraries, shaders, plugins and UI assets. Record source/dependency revisions, OS/GPU/driver, commands and passed/failed/untested cases for Windows and Linux. Claim only the configurations actually tested. |

Implement the UI/session slice before world parity work. Scope the insertion
audit and qualification to every dependency reachable by that slice; unresolved
in-scope paths remain blockers. World shader, PPLL, bake, media, preview and full
scene parity gates below apply to later deliverables unless required by the
minimal UI/chat path. A successful first milestone establishes native UI/chat
correctness, not full viewer parity or a performance improvement.

Implementation-plan item 1 (current-source reconciliation and UI/chat
initialization, callback and teardown audit) has its
[acceptance record and gate contracts](milestone1-source-acceptance.md).
The accepted source/design boundary permits a connected session while deferring
world graphics; it does not forbid world entry. The native backend, adapters and
gate enforcement still require implementation and runtime qualification.

The following sections retain the historical full-renderer assessment. Any
OpenGL fallback or renewed library-selection language there is superseded by
this mandate. Independent functional alternatives for optional utilities and
Vulkan algorithms are separate from an OpenGL backend fallback.

Baseline: `1a490c3cb7ed60124169bf4bf6ad61a6ae1eeec5`. Target: Windows/Linux Vulkan. This is a proposed plan; none of the experiments, builds, captures or measurements below has been performed in this audit. Qualification must precede library commitment and renderer implementation.

The shared architecture/default functional path stays independent and supports NVIDIA/AMD/Intel. Vendor-origin supporting utilities, including NVIDIA/AMD and candidate AMD VMA, are eligible when qualified with independent functional fallback. Optional NVIDIA-only NVRHI still requires a complete equivalent independent path and isolated adapter dependencies. No universal vendor-controlled renderer is approved. Hardware support, utility support and architecture governance are separate checks.

## First decision: qualify the backend boundary

Use [both concrete framework designs](framework-comparison.md) and the [73-row paired matrix](framework-contract-matrix.csv). Run the same distinguishing cases on DiligentCore and bgfx; no shader triangle or presumed winner settles the decision.

The recommended architecture is the viewer-owned graph and resource contracts in [architecture.md](architecture.md). DiligentCore is the leading mature GHI candidate to qualify, with bgfx comparator; no library is adopted. Bespoke Vulkan machinery is infeasible under current capacity and is not the fallback. Pin immutable framework, SDK and shader-tool revisions before experiments. Existing GL/Zink is a separate GL backend and may supply reference behavior in a separate run; it is not a Vulkan fallback.

| Gate | Representative experiment and acceptance evidence | Decision addressed |
|---|---|---|
| Q0 Governance and utility scope | Produce a pinned module/build manifest: core ownership/control, utility provenance, licenses, required/optional modules, nested shader helpers/tools and selected targets. Independent default architecture is mandatory. Vendor-origin helpers are eligible; document actual integration, supported capabilities and independent functional fallback. Diligent packaged FSR/GPUOpen content is not automatically integrated support. | U-D1: architecture eligibility and qualified utility scope before adoption. |
| Q1 Shader ABI | Compile representative legacy material, PBR, rigged, HUD, terrain, water and post shaders to SPIR-V. Reflect all blocks, vertex attributes, texture/sampler bindings and outputs against CPU producers, including packed texture indices. Demonstrate exact layout and resource binding without ambient GL state. | U-D1/U-D3: reject or adapt a GHI whose descriptor schema cannot express this ABI. |
| Q2 Transparency | Run detached-depth rejection, fragment R32_UINT head atomics, bounded node allocation, explicit capture→resolve barriers, depth replay and residual particle/custom-blend/glow passes. Compare overflow and layer truncation with GL, including GPU delay and multiple flight slots. | U-D1: fragment storage support and caller-visible synchronization; U-D6: actual PPLL budget, not an assumed per-frame allocation. |
| Q3 Views and formats | Render main MRT, shadow comparisons, cube faces/mips, hero mirror, preview, local avatar bake and tiled capture. Query each actual format/usage/filter/blend combination. Test winding, clip depth, Y orientation and color encoding using the coordinate contract. | U-D2: depth 24 versus supported depth fallback and RGB→RGBA substitutions; U-D4: history and partial-probe behavior. |
| Q4 Publication | Delay GPU completion while replacing texture/mesh/shader generations, resizing browser media and cancelling uploads. Trace ownership through worker staging, GPU completion and render visibility. Readback must match synchronous bake/depth/capture consumer semantics without use-after-free or stale bytes. | U-D5: plugin producer protocol, completion, cancellation and blocking compatibility. |
| Q5 WSI and teardown | Exercise Windows and Linux supported window systems, resize/minimize/restore/DPI, swapchain recreation, pending captures, shutdown with uploads, device loss and explicit failure reporting without OpenGL fallback. Prove present semaphore reuse and retirement separately from graphics completion. | U-D1: framework/platform integration feasibility. |
| Q6 Optional NVRHI equivalence | First complete and qualify the mature common GHI path on NVIDIA/AMD/Intel. Prove identical shared frontend/graph contracts, full feature/quality/fallback and auxiliary/UI/media/capture/recovery coverage for optional NVIDIA NVRHI; isolate build/package/load dependencies. Compare both paths on the same NVIDIA devices under sustained workloads while maintaining AMD/Intel qualification. Show tangible benefit exceeding duplicate adapter/ABI/binding/PSO/lifetime/WSI/test costs; preserve NVIDIA's common GHI choice. Comparable performance goals do not mean identical FPS across GPUs. | U-D1: optional adapter only if equivalence, independence and measured value hold; otherwise omit it. |

| Q7 Utility outcome | For each concrete NVIDIA/AMD/common utility, verify available module/version/API integration and actual capabilities. Compare fair quality/resolution/settings and sustained CPU/GPU/memory/reliability outcomes with a vendor-neutral implementation. Select supported devices by capability, not vendor ID alone; some helpers may work across vendors. Test independent fallback on unsupported devices, including Intel. VMA requires allocation/lifetime/fragmentation stress against eligible library allocation alternatives, not a mandated handcrafted allocator or postprocessing image comparison. | U-D1/U-D6: adopt qualified useful utilities without changing baseline functional parity; no fabricated benefits. |

| Q8 Paired API integration | Compare actual Diligent GLSL/SPIR-V resource-name remapping and bgfx shaderc/container/uniform conversion; PPLL explicit UAV transitions versus real bgfx pass boundaries; partial cube/shared depth; media publication; fence map versus frame-flush synchronous bake; native window/WSI/loss; fully staged Autobuild packages. Use the identical baseline contracts, quality and failure cases for both. Record required escapes/upstream changes and whether adaptation remains bounded. | Candidate selection follows comparative evidence; if both need handcrafted backend machinery, implementation remains blocked by feasibility. |


A triangle demo passes none of these gates. A blocked gate is evidence to revise the library choice or interface, rather than silently omit a viewer feature. Use Vulkan validation and synchronization validation with zero unexplained errors, plus GPU capture inspection of resource transitions. Validation alone does not prove visual or temporal parity. [Khronos validation guidance](https://docs.vulkan.org/guide/latest/validation_overview.html) and [synchronization guidance](https://docs.vulkan.org/guide/latest/synchronization.html) supply the relevant tooling and dependency rules.

An eligible core alternative must pass Q0 before adoption; qualified vendor-origin utilities are evaluated separately under Q7. An optional NVRHI adapter additionally must pass Q6. Confirm provenance/license/build compatibility of retained postprocessing helpers, qualify parity and provide independent functional alternatives where support is unavailable. Existing source remains unchanged as audit evidence.

The optional NVIDIA adapter is a separate work package after WP6 independent-baseline qualification. Its existence cannot be used to defer AMD/Intel features or qualification. This design request implements no adapter. If the common GHI requires pervasive native escapes or a handcrafted RHI, reject that candidate; if none qualifies, report feasibility blockage rather than implement bespoke by assumption.

## Source-analysis work before parity implementation

The inventories are lexical source censuses, not realized execution manifests. Close these gaps before a backend is described as behaviorally complete:

1. Generate a reachable shader-program/variant manifest from class fallback, feature injection, settings and capability branches. Reconcile every dynamic uniform/sampler setter with reflected fields; enumerate complete linked library closure.
2. Reconcile each raw GL state mutation in the candidate inventory with packet/pass state. Trace specialized debug, selection, terrain historical paths and optional render masks; classify unreachable paths with evidence.
3. Trace history initialization/reset and cube partial-publication behavior across resize, teleport, environment changes and cancellation. Preserve observed behavior initially.
4. Inspect plugin-side media buffer ownership/dirty-region protocol, every active preview subclass, local avatar layer-mask arithmetic, glTF extension routes and mesh/cache failure branches. Document each producer's failure and cancellation contract.
5. Confirm the compiled window/platform variants, shared-context failure behavior, resource recovery and staging rules. Existing macOS paths remain historical inventory, outside the Windows/Linux target.

The detailed gap lists remain in [source-audit.md](source-audit.md), [resources-platform.md](resources-platform.md), [scene-ui-auxiliary.md](scene-ui-auxiliary.md) and [shader-contracts.md](shader-contracts.md). Resolving a source gap is distinct from demonstrating runtime correctness.

## Dependency-ordered implementation work packages

| Package | Deliverable and dependency | Exit evidence |
|---|---|---|
| WP0 | Backend qualification governance gate Q0 and paired experiments Q1–Q5/Q8 on a feature branch from current `vkstorm-devel`; no shipping renderer commitment. | Pinned revisions, reproducible experiments, actual limitations, revised option decision. |
| WP1 | Extract semantic packet/resource/view contracts while retaining CPU scene/asset/UI behavior. Keep existing GL/Zink separately selectable as reference; no complete port to the framework's GL backend is required. | State completeness checks and reference comparisons for active/conditional views; no hidden GL callback in GHI packets. |
| WP2 | Integrate qualified library device/WSI, allocation, descriptor/PSO, command/barrier/lifetime infrastructure; adapt viewer upload/readback and shader ABI. Depends on WP0/WP1. | Resource stress, device/WSI failure tests, measured memory and validated lifecycle. |
| WP3 | UI, fonts, media and offscreen preview/bake/capture paths. | Startup/login/disconnected behavior, local-avatar composite bytes, text clipping, media dirty regions, capture orientation. |
| WP4 | Opaque world, legacy/PBR/terrain/avatar/sky, shadows and probes through the shared graph. | Material/view parity matrix below, complete shader route manifest, partial-probe/history semantics. |
| WP5 | Deferred lighting, water/exclusion/haze, sorted transparency, then optional PPLL and postprocessing. | Exact blend/mask/order contracts, controlled numerical and visual comparisons, feature fallback evidence. |
| WP6 | Full staging, Windows/Linux qualification and independent review before integration PRs. | Autobuild staged RelWithDebInfo viewer and Release binaries run with all runtime dependencies/shaders/assets. No executable-only completion claim. |
| WP7 | Optional batching, parallel recording, compute skinning/lighting/post, descriptor indexing, async queues or reverse-Z, as separate qualified changes. | Each optimization has a measured bottleneck and parity/performance evidence; no assumption that a new API is faster. |

Do not merge release/development branches together or import legacy master. Follow feature-branch PR policy. Development capture/profiling hooks must remain outside release; run `scripts/tests/check_release_hooks.py` for release changes. Audit documents require no viewer build, and no build has been run here.

## Parity and measurement matrix

Compare identical assets, settings, camera, resolution and deterministic scene inputs where possible. Record driver/OS/GPU/CPU, source/dependency hashes and realized capabilities. Include NVIDIA, AMD and Intel Windows/Linux configurations supported by the project; establish actual qualification hardware before claiming support.

| Area | Required cases |
|---|---|
| Materials and geometry | Legacy diffuse/fullbright/masks/bump/shiny, material masks, PBR/GLTF opaque/masked/unlit/double-sided, rigged/control avatars, missing assets/palettes, terrain layers/paint/parcel overlay, foliage and dynamic geometry. |
| Views | Main/HUD, sun/spot shadows, low-detail/default/realtime probes, mirrors at all rates, impostors, previews/appearance hints, map, local bake, startup/disconnected, depth sampling and tiled snapshots. |
| Composition | Above/below water crossings, exclusion/haze order, sorted pre/post-water alpha, PPLL normal/overflow/truncation/budget fallback, particles/custom blend, glow A, HDR off/on, exposure history, SSR, DoF, every AA tier, RLVa/debug/selection overlays. |
| Lifetime | Resize/teleport/settings changes during upload/capture, media resize/seek/exit, eviction/LOD replacement, shader reload, GPU delay, memory pressure, device loss and shutdown. |

Use controlled pixel/numerical comparisons with documented tolerance per pass; disclose justified precision differences rather than hide them in a generous global tolerance. Compare capture bytes where exactness is contractual, and animated temporal sequences where a still image misses history or scheduling behavior. Inspect depth, normals, mixed encoding and glow separately from final RGB.

Measure CPU frame/recording/submission time, GPU pass and total time, p50/p95/p99 frame time and variance over repeated runs, memory residency/peak/upload/readback, PSO compilation stalls and asset latency. Separate cold and warm runs and presentation pacing. Set acceptance thresholds from a measured GL reference and user goals **before** claiming improvement; this audit supplies no numbers. Account for extra RGBA channels, history targets, probe scratch, bounded PPLL nodes and flight-slot duplication. One graphics queue and bounded descriptors are the initial design; expand only when measured benefit exceeds complexity and memory cost.

The final qualification record must list passed, failed and untested cases, hardware scope, open source gaps and fallback behavior. Only then can a feature PR claim implementation completeness or a release claim performance/support.

Optional utility qualification uses the verified providers in the paired report: FSR1 alpha-channel handling and placement, DLSS motion/jitter/history/input creation, build/runtime availability and native state containment. These are separate changes after common-path parity; no utility benchmark or integration was performed.
