# World rendering and service parity progress

Work branch: `codex/vulkan-world-parity`, based on `vkstorm-vulkan` at
`54e765952e`. This is an experimental progress branch. Full supported rendering
and feature parity remains the acceptance target, and is **not achieved** by this
first geometry increment. OpenGL remains the behavioral reference; no GL/Zink
fallback is introduced.

The existing checkout and canonical RelWithDebInfo build remain at
`C:\Dev\vulkanstorm\worktrees\vkstorm-vulkan\build-vc170-64`. The checkout is
currently on the progress branch. Its staged executable is an experimental
build, not a qualified replacement for the previous connected UI build.

## Survey of production ownership

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
| Terrain/sky/environment | Not admitted | LayerData decoding and terrain meshes, EEP/sky, environment history, interpolation |
| Deferred lighting, shadows, probes/mirrors | First geometry uses explicit simple sun/ambient shader inputs | Exact lighting model and attachments, local/projector lights, cascades, probes and dependent views |
| Water/transparency/post-processing | Straight-alpha sorted draw substrate only | Water/haze/exclusion, particles, glow, PPLL and independent path, exposure/SSR/DoF/AA and temporal resets |
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
