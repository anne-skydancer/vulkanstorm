# World rendering and service parity progress

Execution order and phase acceptance are defined in
[full-parity-roadmap.md](full-parity-roadmap.md). The progress observations below
do not establish acceptance of those phases.

The full source-backed inventory, pass order, resource/ownership dependencies
and qualification oracles are in
[full-render-chain-survey.md](full-render-chain-survey.md). That inventory includes
geometry/rigging, materials, EEP/day cycles, lighting/dependent views, HDR,
transparency, postprocessing, auxiliary rendering, UI interactions and services.
The user has observed first authenticated native frames at approximately 14 FPS.
This is first live rendering evidence, not parity or performance acceptance.

Development branch: `vkstorm-vulkan`. Initial progress originated on the
user-requested `codex/vulkan-world-parity`, based on `vkstorm-vulkan` at
`54e765952e`, and is integrated into the development branch. Full supported rendering
and feature parity remains the acceptance target, and is **not achieved** by this
first geometry increment. OpenGL remains the behavioral reference; no GL/Zink
fallback is introduced.

The existing checkout and canonical RelWithDebInfo build remain at
`C:\Dev\vulkanstorm\worktrees\vkstorm-vulkan\build-vc170-64`. The checkout is
on `vkstorm-vulkan`. Its staged executable is an experimental
build, not a qualified replacement for the previous connected UI build.

## Survey of production ownership

The table below records the first primitive increment. The later mesh/avatar,
camera and resource-reuse increments are described at the end of this document;
use the full survey and roadmap for current acceptance requirements.

The native frame loop bypasses the normal startup/idle scene pipeline. Before
this increment, `VSNativeSession` admitted account/chat traffic but rejected all
object/terrain updates. It owns authenticated simulator location and capabilities;
it does not create `LLViewerRegion`, `LLWorld`, `LLViewerObject`, `LLDrawable`,
avatar, camera, environment or spatial-audio owners. The OpenGL renderer's
`LLDrawInfo`/vertex buffers and draw pools carry GL resource ownership. Calling
`gPipeline` or removing UI admission gates cannot implement Vulkan parity.

| Reference production seam | Current native implementation | Remaining implementation |
|---|---|---|
| `LLViewerObjectList::processObjectUpdate`, `LLViewerObject::processUpdateMessage`, `LLVOVolume::processUpdateMessage` | Authenticated current-region native primitive model, full/compressed/terse/cache-miss/kill traffic | Multi-region scene, object cache equivalence, interpolation, spatial indexing, property/interaction ownership |
| `LLVolume`, `LLVolumeMessage`, `LLPrimitive::unpackTEMessage` | Reuses CPU primitive tessellation, shape decoding and texture-entry parser | Adaptive LOD, sculpt/flexible assets, mesh assets and geometry budgets |
| `LLFace`, `LLDrawInfo`, `LLVertexBuffer`, draw pools | Immutable Diligent indexed meshes, per-draw constants, depth and sorted-alpha substrate | Material variant/reflection registry, legacy/PBR/glTF semantics, exact alpha/mask/cull/material ordering |
| `LLViewerTextureList`, `LLTextureFetch`, `LLGLTexture` | Uses current native CPU image service and immutable image generations for first primitive textures | World texture streaming, mip/LOD priorities, encoding, sampler variants, cache/VRAM budgets |
| `LLAgentCamera`, tools, selection/picking | Fixed +X region camera at authenticated agent position plus eye height | Ordinary camera and movement controls, picking, selection, editing, HUD world content |
| Avatar/appearance/animation/bake owners | Not admitted | Skeleton/palette publication, rigged meshes, bakes, attachment ownership, gestures/AO/animation |
| Terrain/sky/environment | Bounded LayerData decoder, viewer CPU inverse DCT, native terrain meshes and default sky gradient | Variable/neighbor regions, four-way splatting, EEP/atmosphere/environment history and interpolation |
| Deferred lighting, shadows, probes/mirrors | First geometry uses explicit simple sun/ambient shader inputs | Exact lighting model and attachments, local/projector lights, cascades, probes and dependent views |
| Water/transparency/post-processing | Region water-height surface and straight-alpha sorted draw substrate | Animated water, environment inputs, reflection/refraction/underwater/haze/exclusion, particles, glow, PPLL and independent path, exposure/SSR/DoF/AA and temporal resets |
| Auxiliary passes | Not admitted | Previews, local bakes, impostors/maps, snapshots, debug/query/selection consumers |
| SimulatorFeatures / `LFSimFeatureHandler` | Bounded authenticated current-region fetch, shared parser and CPU signals | Actual network interoperability and scene-dependent feature consumers |
| Marketplace idle services | Existing importer and notification queues now pumped while connected | Authenticated runtime confirmation |
| Voice clients/channels | Initialization exists, provisioning/signalling still lacks native region context | Voice capability grants, current-region context, WebRTC ICE/STUN/signalling and Vivox lifecycle, cancellation and spatial owners |
| Firestorm bridge | Account retirement exists; scene-owner gate remains | Real avatar/attachment/script ownership; bridge service provisioning and interaction |
| Inventory appearance actions | Account inventory services remain present | Wearables, attachment rez, animation/gesture playback and scene operations |

The SimulatorFeatures fetch uses weak session ownership, account/region epochs,
two bounded 15-second attempts, cancellation and reset on crossing/logout.
The shared parser preserves OpenGL scene-limit behavior and omits those scene
side effects in the native account path. Marketplace updates do not instantiate
new scene owners. Gesture pumping requires both an existing manager and a real
agent avatar; that condition is not met by this first primitive model.

## First production geometry increment

`VSVulkanContext` owns `VSWorldRenderer` and `VSWorldScene` and draws geometry
before ordered UI in the normal connected viewer. Independent depth generations
resize with the presentation target; all meshes, images, constants, bindings and
depth attachments remain retained through GPU fence completion. Shutdown waits
for idle before releasing scene/renderer resources. Crossing/account reset drops
the old CPU scene; pending GPU generations remain retained by submissions.

Primitive updates use the shared CPU volume and texture-entry decoders. Parenting
is bounded and cycle checked. Position/scale/rotation and vertex/index inputs are
validated. Compressed optional strings are checked for bounded termination before
the legacy data packer is called. Cached and unknown terse objects request full
updates with queue, batch, rate and retry bounds. Mesh/sculpt/flexible objects and
attachments are explicitly omitted rather than displayed as ordinary primitives.
Neighbor-region traffic remains denied. This is not a complete simulator scene.

The camera keeps viewer region axes (+X forward, +Y left, +Z up). Its projection
uses GL clip-depth convention, converted exactly once in the world vertex shader
to Vulkan zero-to-one depth. Normal transforms account for nonuniform scale.
Texture-entry transforms preserve ordinary per-face scale/rotation/offset inputs;
world mip, encoding, alpha-mode and material parity still require later work.

## Qualification and remaining work

Local headless SwiftShader execution of the **actual production renderer** passes
indexed drawing, depth occlusion, straight-alpha blending, four texture sampling
oracles, near-plane clipping and geometry-generation retirement, with core and
synchronization validation and no windows. Seven PPM artifacts and diagnostics
are retained in `.tmp/world-contracts-evidence`. This offscreen result does not
qualify native presentation or authenticated streamed-world behavior.

Production-extracted headless tests cover packet bounds, little-endian floats,
camera depth/axes, authenticated host/phase admission, feature parsing and stale
account/region cancellation, and CPU service ownership. The dedicated workflow
runs these tests and adds the world renderer contract to its existing Windows
SwiftShader/Linux SwiftShader/Linux Lavapipe matrix. Missing world images fail
the job; previous images are removed before each run. The production baseline,
release publication and `latest` are unaffected.

The next integration dependencies are streamed primitive/runtime evidence and
camera/movement ownership; terrain/environment; world asset/material services;
avatar/appearance/attachments; lighting and dependent views; water/transparency/
post-processing; then all auxiliary/interaction services. Voice needs its own
renderer-independent authenticated region context, not a fabricated GL region.
Each opened route needs matching init/tick/reset/teardown and native viewer
qualification. Full parity cannot be inferred from an allowlist, a build, or this
geometry contract test. Live world, voice and bridge acceptance remain untested.

## Ground, sky and water increment

The ordinary connected frame now receives authenticated current-simulator
`LayerData` during connection and while connected. A bounded bit reader validates
16/32-sample patch streams before calling the same CPU inverse DCT as the OpenGL
viewer. Truncated/invalid packets are rejected atomically. Received height patches
produce native indexed meshes with neighbor edge samples and terrain normals;
missing terrain is not filled with invented ground. Scene reset removes terrain
and water on crossing/logout. Submitted GPU generations retain their usual fence
ownership. The current heightfield is limited to a standard 256m region.

`RegionHandshake` supplies real water height, four terrain detail textures and
height metadata. The first terrain material selects one detail texture per patch
by height. It is not the reference's four-way splatting/PBR terrain material.
Water is a translucent depth-tested region surface at the advertised height;
terrain above it remains visible. A native gradient sky draws first without
writing depth. Its colors are default procedural colors, not EEP data. Full
atmospheric shading, animated water, reflections/refraction and underwater views
remain required by the complete rendering-chain acceptance target.

The production renderer passes headless sky-gradient orientation and background/
opaque/alpha depth-order pixel comparisons on Windows SwiftShader with validation.
Nine images and diagnostics are retained in `.tmp/terrain-contracts-evidence`.
The CPU fixture runs the actual established inverse DCT and verifies a known
height, chunked bit order, signed coefficients, truncation, invalid sizes/IDs,
standard/extended patch IDs and atomic rejection. The executable has been rebuilt
and complete runtime/assets staging checked in the canonical RelWithDebInfo tree.
Live ground/water streaming still requires authenticated runtime evidence.

The overall job remains **the complete rendering chain, all connected UI and
Firestorm bridge integration with no gaps**. These surface passes are progress
increments and do not satisfy that complete acceptance boundary.

## Required bridge ownership closure

The bridge is part of full feature acceptance. Its native guard must only be
replaced after real service owners exist; a primitive mesh does not supply an
avatar or a worn attachment. The surveyed production dependency order is:

1. Preserve authenticated avatar/attachment identities, attachment points,
   inventory-item identities and parent transforms with account/region lifetime.
2. Provide actual self-avatar/attachment services, COF/wearable ownership and the
   attachment queue, including attach/detach notifications.
3. Receive task-inventory replies and asset-transfer completion with listener
   cancellation. Adapt bridge prim mutation and script-save/upload operations.
4. Start/reuse/create the bridge at the real ownership-readiness transition,
   replace blanket native rejection, and route authenticated owner chat through
   bridge processing before translation and transcript publication.
5. Qualify creation, reuse, forced detach/reattach, script installation, crossing,
   logout/reconnect, and the UI actions using bridge services.

These are implementation dependencies, not an alternative reduced acceptance
criterion. The remaining scene chain includes world assets/materials, ordinary
camera/movement, avatars/animation/attachments, terrain/environment parity,
lighting/shadows/dependent views, water/post-processing, and every auxiliary pass
and connected UI consumer. The native path remains incomplete until those owners
and routes are working in the viewer.

## Current integration under qualification

The native session now sends configured movement intent and camera/body/head
basis through ordinary AgentUpdate snapshots. Actual self-avatar updates feed
the authoritative position; the camera controller does not fabricate movement.
Orbit/pan/roll/zoom and first-person projection feed the native frame. Focus,
freeze, region/account changes and logout retire held intent. Camera collision,
picking targets, smoothing, joystick and cursor-locked mouselook remain required.

Native CPU avatar owners reuse skeleton/morph/motion and body mesh data for self
and others, retain real attachment bone transforms, and decode five-channel bake
masks through the CPU image service. Region time dilation and actual viewport
projection inform animation/LOD. Mesh assets now use shared binary-header,
compressed face and skin-metadata decoders behind account/region epochs and
bounded requests/cache. Native attachment skinning consumes actual weights and
inverse-bind/joint matrices. This does not complete joint override/scale policy,
animesh, HUD attachments, COF/wearable/local compositor or bridge ownership.
Real mesh runtime and avatar parity still require qualification.

Completed world draws recycle uniform buffers and descriptor bindings only
after fence completion. Adjacent compatible UI draws batch without reordering
paint operations or crossing image-generation, scissor or pipeline boundaries.
The production headless renderer verifies changed constants after reuse and UI
overlap/clipping/generation pixels. Counters distinguish draw, binding/uniform
allocation and upload cost. They do not establish an FPS improvement on the
user's live scene, or fill missing rendering passes.
