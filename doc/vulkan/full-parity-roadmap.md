# Native Vulkan full-parity implementation roadmap

Date: 12 October 2026. Source baseline:
[full render-chain survey](full-render-chain-survey.md).

This is the execution plan for the expanded world and connected-viewer mandate.
It refines the world stages of [viewer-integration-plan.md](viewer-integration-plan.md)
and [qualification-roadmap.md](qualification-roadmap.md); it does not replace the
existing insertion catalog or erase earlier qualification limits.

## Outcome and starting point

Deliver a functioning connected viewer whose supported rendering, UI and services
match the OpenGL reference. Include self/other avatars, all supported geometry,
materials, environment/day cycle, lighting, shadows, water, transparency, HDR,
post-processing, auxiliary views, interactions and Firestorm bridge. Startup,
persistent texture-cache behavior, and the real login/logout progress screens
are explicit parity requirements, not implied by connected-frame rendering.

Native Vulkan and OpenGL remain peer backends. GL/Zink is a GL backend, not a
Vulkan fallback. Production Vulkan uses normal machine-driver discovery; pinned
software devices provide correctness evidence without requiring AMD/NVIDIA hosts.

The current experimental viewer produces authenticated native frames. Primitive,
terrain, mesh, CPU avatar/attachment, camera and service work is partial. The
user's approximately 14 FPS observation is a starting measurement, not a target
or parity result. Existing source tests and isolated renderer readbacks do not
establish complete connected-viewer behavior. No phase below is fully accepted
by that evidence alone.

Continue development on `vkstorm-vulkan` in the existing
`C:\Dev\vulkanstorm\worktrees\vkstorm-vulkan` checkout. The user-requested
`codex/vulkan-world-parity` branch supplies the initial progress work integrated
into it; do not proliferate per-fix branches or builds. The canonical build is
`build-vc170-64`, `RelWithDebInfo`,
fully staged through Autobuild without an installer. Preserve an executable in
use for a user test. Production release CI and `latest` are unchanged.

## Dependency order

| Phase | Deliverable | Prerequisites |
|---|---|---|
| R0 | Traceable acceptance inventory and reproducible baseline | Survey |
| R1 | Native scene/service owners and shared asset/resource contracts | R0 |
| R2 | Complete geometry, avatar and appearance producers | R1 |
| R3 | Materials, texture streaming and runtime settings controllers | R1; extend alongside R2 |
| R4 | Production render graph, visibility, linear HDR and G-buffer | R1, core R3 contracts; incremental R2 producers |
| R5 | EEP/day cycle, atmosphere and deferred lighting | R3, R4 |
| R6 | Shadows, probes, mirrors and dependent views | R2, R4, R5 |
| R7 | Water, transparency, particles and world media composition | R2–R6; media service owners from R1/R3 |
| R8 | Exposure, tone mapping, history and full post-processing | R4–R7 |
| R9 | Auxiliary rendering, interaction and connected-service closure | Start with R1/R2; final rendering qualification after R8 |
| R10 | Whole-viewer parity, stability and performance acceptance | All preceding phases |

This is a dependency graph, not a requirement to finish every geometry variant
before building the render graph. Establish shared contracts first, then extend
the production frame with vertical integrations. Services/UI and their native
controllers proceed with the owner they depend on, not as an afterthought after
graphics. Qualification accompanies implementation; checkpoints do not turn
remaining authorized work into permission handoffs.

## R0 — Acceptance inventory and baseline

- Give every survey row, draw-pool/pass variant, rendering setting listener and
  ordinary UI/service route a stable acceptance ID. Record its reference symbol,
  native owner, dependencies, implementation state, evidence and remaining gap.
  Classify variants separately, including rigged, masked, underwater and HUD.
- Preserve the distinction between implemented, source-qualified,
  software-runtime-qualified and authenticated-runtime-qualified. A compiled
  path or enabled menu entry is not an accepted feature.
- Establish reproducible assets, camera/settings/environment snapshots, fixed
  clocks/seeds and simulator replay. Capture separate software OpenGL reference
  output and Vulkan pass/readback output. Record revisions and capabilities.
- Measure scene/service update, packet construction, skinning, upload bytes,
  draw/binding counts, CPU frame time, GPU time where available, and presentation.

Acceptance: no survey requirement is unassigned; the same production viewer
scene can be replayed with reproducible inputs and retained comparison evidence.
Record tolerances and performance methodology before accepting results.

## R1 — Ownership, lifecycle and shared contracts

- Establish explicit account, region, object, face, asset, material, avatar,
  control-avatar, attachment, inventory/task and interaction identities.
  Separate reusable CPU algorithms/services from GL resource ownership.
- Support neighbour circuits, host-qualified local IDs, variable-sized regions,
  region transforms and floating origin. Preserve scene continuity on crossing
  while retiring genuinely stale owners and callbacks.
- Implement motion timestamps, interpolation, acceleration/angular motion,
  selection freezes and correction policy; retain authoritative movement.
- Define native geometry attributes, material descriptions, image semantics,
  samplers, buffers, generation IDs and capability policy once for all producers.
- Provide bounded asset requests/cache/streaming, cancellation, error states,
  local asset replacement, GPU-completion retirement and budget accounting.
- Establish ordinary startup and shutdown service sequencing from
  `LLAppViewer` and `LLStartUp`: settings/per-account initialization, cache
  workers, login authentication, circuit/capability establishment, required
  inventory/appearance startup, failure/cancellation/retry and logout cleanup.
  Preserve shared CPU startup behavior without invoking GL-only initialization.
  Progress events must describe the real operation state, not a parallel mock.

Acceptance: production message/asset replay survives replacement, local-ID reuse,
neighbours, crossing, teleport, logout/relogin and shutdown without stale
callbacks, leaks or GL rendering calls. Unknown resources never masquerade as
correctly rendered substitute primitives.

## R2 — Geometry, avatars and appearance

- Complete prim/linkset tessellation and adaptive LOD; mesh face/LOD/skin data;
  sculpt topology/mirror/invert; flexible simulation; trees/grass and wind.
- Add GLTF scene assets independently of GLTF surface materials: referenced
  buffers, node hierarchy/transforms, accessors, animation, skins and picking.
- Complete self/other system-avatar body branches, skeleton/morph/bake masks,
  specialized motions, sitting/ground/hover, cloth/physics and visibility policy.
- Complete rigged/fitted attachments, joint overrides/scales and restoration,
  animated bounds, HUD attachments and independently owned animesh.
- Wire COF/wearables, attachment rez/detach queues, local compositor/bakes,
  animation/gesture/AO and applicable script controls to these owners.
- Publish geometry by changed generation, not by unconditional per-frame
  rebuild; skin/update animated data only as required while preserving pose.

Acceptance: deterministic geometry/pose comparisons cover every producer and
LOD, including topology replacement and attachment lifetimes. Ordinary inventory
appearance actions affect the real connected avatar. Qualify animated selection
bounds as well as visible triangles. All material/shadow classifications remain
tracked for subsequent phases.

## R3 — Materials, textures and settings

- Complete legacy diffuse/bump/shiny/normal/specular/fullbright/emissive variants,
  tangent conventions and per-channel UV transforms.
- Implement PBR asset/override ownership, factors, color spaces, normal/ORM/
  emissive channels, alpha modes, double-sided and rigged variants; terrain PBR.
- Implement TE/PBR texture animation, progressive mip/discard demand, sampler
  variants, anisotropy, compression policy, replacement/eviction and local reload.
- Integrate persistent texture-cache reads/writes and worker pumping with native
  fetch/decode subscribers using `LLTextureCache` and the existing fetch/cache
  policy. Preserve cache location/size/clear controls, validity/version checks,
  partial data/discard semantics, corruption recovery, read-only/multiple-instance
  behavior and orderly flush/shutdown. Distinguish disk cache, decoded CPU cache
  and GPU residency; a bounded in-memory image map is not disk-cache parity.
  Verify identity/validation policy when accounts or grids change.
- Add dynamic media image generations with plugin stride/format/origin and
  dirty-region updates; share the resource contract with world media and previews.
- Inventory the listeners in `llviewercontrol.cpp` and implement native settings
  controllers. Each later phase adds its controllers when its production effect
  is implemented; do not re-enable GL callbacks wholesale.

Acceptance: material/UV/color/sampling charts and real replacement/streaming
cases match the reference semantics. Preferences cause observable native state
or resource changes and persist correctly. A saved value with an inert callback
fails acceptance. BRDF/probe-dependent appearance is finally accepted in R5/R6.
Cold/warm-cache runs and process restart prove persistent reuse and equivalent
pixels. Corrupt/truncated entries recover through the reference fetch policy;
clear/resize/read-only, account/grid transitions and shutdown with pending I/O
preserve correctness and bounded memory, with recorded cache/network counters.

## R4 — Render graph, visibility and HDR foundation

- Replace direct swapchain-only world composition with explicit pass resources,
  formats, transitions, shared depth, view contexts and retirement.
- Provide linear floating-point scene storage, G-buffer attachments and optional
  depth prepass. Preserve values above one; define output transfer and UI
  composition so color conversion happens in the correct reference order.
- Implement bounds/spatial partition visibility, cull/state/pass classification,
  instancing/batching where compatible, and asynchronous per-view occlusion.
- Provide common dependent-view execution for shadows, probes, mirrors,
  previews, snapshots and impostors; reset history on discontinuities.

Acceptance: production-viewer pass readbacks verify encodings/depth/HDR values;
resize/minimize/restore and account/scene changes preserve resource lifetimes.
Missing query results do not falsely cull geometry. Core and synchronization
validation failures fail CI. HDR lighting does not imply HDR10 display output.

## R5 — Environment and lighting

- Adapt shared environment settings to native snapshots: region/parcel/local
  precedence, altitude tracks, day length/offset, asset transitions and blending.
- Port analytic WindLight/EEP sky, sun/moon, clouds/scroll, stars and depth haze.
- Implement reference deferred sun/moon/ambient/local/projector lighting,
  material BRDF/LUTs, light limits/culling and transparent-lighting inputs.

Acceptance: frozen sunrise/noon/sunset/night and altitude/precedence transitions
match the reference; controlled light/material charts exercise attenuation,
projectors and camera-inside cases. Placeholder gradients are removed from the
accepted production path. No atmospheric scattering-LUT system is invented
as a supposed reference requirement.

## R6 — Shadows and dependent reflection views

- Implement sun cascades/spot views, caster classification, depth comparison,
  bias/filtering and every masked/rigged/tree/PBR caster variant.
- Implement probe cube faces/arrays, irradiance/radiance filtering, influence
  blending, dynamic scheduling and hero probes.
- Implement planar mirrors with reflected camera, clip plane, recursion policy
  and view-specific visibility/history.

Acceptance: map/face/mip readbacks and composite scenes verify cascade seams,
moving/skinned casters, masked edges, probe orientation/blending and mirror
clipping. Dependent views execute through the production graph without unsafe
resource feedback or treating their camera state as the main view.

## R7 — Water, transparency, effects and media

- Implement EEP water normals/waves, Fresnel/fog, scratch scene/depth,
  reflection/refraction, exclusion, haze, underwater and void-water variants.
- Preserve pre/post-water alpha ordering, masks, emissive/glow/depth behavior,
  sorted alpha and supported PPLL with bounded allocation, barriers, overflow
  and residual paths. Feature-capability policy must preserve supported behavior.
- Integrate particle/ribbon/beam simulation, textures, blend modes and glow.
- Complete media-on-a-prim permissions/navigation/synchronization, per-face
  image substitution and picked UV/input routing; parcel browser UI alone is
  insufficient. Wire scene sounds/effects to native spatial owners.

Acceptance: intersecting surfaces, additive/residual particles, water crossings,
PPLL overflow and media resize/input behave correctly in production scenes;
depth, exclusion and composited pixels have independent oracles.

## R8 — HDR finalization and post-processing

- Implement luminance reduction, exposure current/history, fixed-time adaptation,
  tone mapping, gamma/color grading and actual reference LUT dependencies.
- Implement SSR scene/depth/matrix history and SSAO/blur; handle first frame,
  disocclusion, offscreen samples, teleport, resize and settings changes.
- Implement glow, DoF/focus policy, FXAA/SMAA and lookup assets, CAS, RLVa sphere,
  vignette, snapshot framing, visualization modes and final noise/depth/overlays.
- Preserve the survey's verified settings-dependent finalization order. Ensure
  world HDR processing does not inadvertently recolor or blur ordinary UI.

Acceptance: floating-point/pass readbacks and composed scenes verify exposure
adaptation, depth reconstruction, edge/focus/glow fixtures, effect combinations
and history resets. Qualify the reference FXAA/SMAA paths; integrated TAA was
not found and is not silently added as a parity requirement.

## R9 — Auxiliary rendering and connected feature closure

Begin these routes as soon as their R1/R2 owners exist; use the common R4 graph
and complete their visual qualification after R8.

- Finish camera collision/smoothing, pick/selection/edit/touch/context-menu
  routes, property/parcel controls, HUD text/icons/attachments and handles.
- Implement snapshots, image/avatar/model/sculpt previews, dynamic textures,
  local bakes, impostors, map/parcel overlays and physics/build/debug views.
- Complete Firestorm bridge creation/reuse, inventory↔attachment mapping,
  task-inventory/asset/script transfer, authenticated handshake and dependent
  actions. A generic chat or HTTP adapter cannot substitute for the bridge.
- Complete native voice authorization/provider lifecycle, peer/group calls and
  spatial positioning; retain music/media, statistics/FPS, notifications,
  local chat/IM/group IM and all supported skins.
- Complete production startup and login/logout progress presentation using the
  existing XUI/`LLProgressView` semantics and real operation events. Match status
  text, messages, percentages, backgrounds/logos, supported intro/MOTD behavior,
  fades, cancellation/quit controls and modal input ownership. Retain the native
  frame/presentation resources until the logout screen has finished its work;
  a synthetic diagnostic screen or abrupt window closure is insufficient.
- Match ordinary login behavior: saved account/grid/start-location options,
  credentials and supported authentication/consent flows, failure/retry,
  account initialization and transition into the actual connected viewer.
  Startup settings, cache/audio/media/service readiness and first-run/error
  dialogs remain part of acceptance, not just the login panel's appearance.
- Close ordinary connected UI and runtime settings routes against the R0 ledger,
  including local assets, derender/blacklist, marketplace and applicable RLVa
  effects. Disabled controls need the same legitimate capability/account reason
  as the reference, not a missing native owner.

Acceptance: ordinary controls invoke real owners and produce the expected
network/service/rendering effects. Automated replay covers both success and
failure/cancellation; authenticated cases retain explicit evidence. All-skin
visual/input checks run headlessly or without focus theft. Menu enablement and
test-only dialogs do not close production UI acceptance.
Exercise successful and failed/cancelled authentication, retry, slow startup,
first-run configuration, pending cache/service work, normal logout and its
timeout/error path. Compare actual login/logout progress frames, text, input
behavior and lifecycle events against the reference across skins/localizations.

## R10 — Integrated acceptance and delivery

- Run the complete staged viewer on Windows/Linux software Vulkan CI using
  pinned SwiftShader/Lavapipe, decisive validation and native presentation tests.
  Tests must execute production routes, not only standalone harness copies.
- Compare against separate software OpenGL runs using matching real assets,
  settings/camera/time. Use numerical/geometry/pass oracles as well as final
  images; declare tolerances for filtering/rounding rather than hiding defects.
- Exercise connected sessions, crossing/teleport, reconnect, appearance changes,
  media/voice/bridge, resize/minimize, memory pressure, failure and long-run
  teardown. Preserve revisions, capabilities, library paths, logs, readbacks,
  failures and build/staging manifests.
- Include cold/warm-cache startup, persisted restart, real login progress and
  real logout progress in the end-to-end viewer matrix. Capture authentication
  and lifecycle evidence without recording credentials or authentication tokens.
- Track frame-time distributions, allocations, upload rates, bounded memory and
  frame pacing for representative scenes. Compare CPU/algorithmic cost with the
  reference under controlled software conditions; do not label software timings
  as physical-GPU FPS parity or require unavailable vendor hosts.
- Review all remaining ledger entries and insertion-catalog witnesses, integrate
  the progress branch into `vkstorm-vulkan`, and publish a reviewable staged
  development executable with precise implementation and qualification status.

Acceptance: every supported survey/setting/UI route has a functioning production
implementation and its required evidence; no unexplained missing pass or inert
control remains. Software correctness, authenticated interoperability and
physical-driver/performance evidence are reported separately. Unavailable vendor
hardware remains an explicit qualification limit, not a blocker for software
implementation or a claim of vendor qualification.

## Execution and reporting rules

Implement production wiring before treating tests as progress toward missing
functionality. Add meaningful qualification as each integrated path becomes
available; preserve connected UI regressions throughout. Performance is part of
every phase, particularly geometry reuse, streaming, skinning, batching and
dependent views, rather than an FPS-only diversion or a final cleanup task.

Maintain the R0 ledger and link each completed entry to its source change and
evidence. Update this roadmap if source review discovers another supported path.
Phases organize continuous work; none is a reason to stop authorized development
before its requested scope is fulfilled. Do not invent calendar estimates before
the owner extractions and first full vertical integration are measured.
