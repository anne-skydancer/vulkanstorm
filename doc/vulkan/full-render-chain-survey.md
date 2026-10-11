# Full OpenGL reference render-chain survey

The dependency-ordered implementation and acceptance plan is
[full-parity-roadmap.md](full-parity-roadmap.md).

Survey date: 2026-10-11. Work checkout: `codex/vulkan-world-parity`, based on
`vkstorm-vulkan`. Paths below are repository-relative. Source symbols identify
the relevant implementation; line numbers refer to the surveyed checkout and
will move as integration proceeds.

## Acceptance boundary

The deliverable is full supported rendering and viewer feature parity with the
OpenGL reference: the complete world, self and other avatars, connected UI,
interactions, services and Firestorm bridge. Native Vulkan is a peer backend.
GL/Zink is a separate GL backend, not a Vulkan fallback. Rendering some geometry,
passing an isolated pixel test or improving FPS does not satisfy this boundary.

Authenticated live native frames have now been observed by the user, at about
14 FPS. This establishes first live rendering, not visual parity, performance
acceptance or complete-world qualification. The existing native renderer is a
small forward renderer with diffuse/tint, simple sun/ambient shading, fullbright,
fixed alpha cutoff, sorted blending and depth. Terrain uses one detail texture
per patch; sky is a procedural gradient; water is a flat translucent surface.
CPU avatar integration is partial. All missing paths below remain required.

## Reference execution and resource dependencies

The main reference frame in `indra/newview/llviewerdisplay.cpp` executes:

1. Dynamic textures/previews (`:812`, `LLViewerDynamicTexture::updateAllInstances`
   in `lldynamictexture.cpp:184`), hero/mirror probes (`:831`).
2. Geometry creation, partition changes, rebuilds and resource updates (`:865`;
   `pipeline.cpp:2093`, `:2994`, `:2881`).
3. Main camera cull (`:902`, `LLPipeline::updateCull`, `pipeline.cpp:2634`),
   `stateSort` (`:3281`) and `postSort` (`:3743`).
4. Sun/spot shadow views and avatar impostors (`:923`; `generateSunShadow`,
   `pipeline.cpp:11119`; `generateImpostor`, `:12004`).
5. Optional depth prepass, then opaque/masked deferred geometry into MRT/shared
   depth (`:1102`, `:1130`; `renderGeomDeferred`, `pipeline.cpp:4213`).
6. Deferred sun/shadow/SSAO light map and optional blur, atmosphere/indirect
   shading, point lights and projectors (`renderDeferredLighting`, `:9674`).
7. Post-deferred pools, water exclusion/haze/atmospheric insertion points,
   transparent, emissive and fullbright geometry (`:10153`;
   `renderGeomPostDeferred`, `:4352`).
8. Previous scene/matrix preservation (`:10159`), then HDR/LDR finalization and
   postprocessing (`llviewerdisplay.cpp:1624`; `pipeline.cpp:9191`).
9. World HUD elements, HUD attachments, selections and UI
   (`llviewerdisplay.cpp:1631`, `:1639`, `:1656`, `:1674`), then presentation.

Pool and pass ordering in `lldrawpool.h:57` and `:139` includes sky, water
exclusion, WindLight sky, simple, fullbright, bump, legacy materials, GLTF PBR,
terrain, grass, GLTF mask, trees, alpha mask, fullbright mask, avatars, control
avatars/animesh, glow, pre-water alpha, void water, water and post-water alpha.
The pass enumeration includes rigged and nonrigged simple/fullbright/shiny/bump,
normal/specular combinations, alpha/mask/emissive, glow and GLTF variants.
An ordered list of native triangles cannot replace this classification.

The native resource graph must provide G-buffer/shared depth, shadow targets,
auxiliary probe/mirror views, scene/depth scratch, HDR scene, exposure and SSR
history, glow and postprocessing ping-pong targets, with explicit transitions,
resize/history invalidation and GPU-completion retirement.

## Geometry and simulation

| System and reference seams | Reusable inputs/CPU work | Required native ownership and output | Qualification |
|---|---|---|---|
| Standard primitives/linksets: `LLVolume::generate`, `llmath/llvolume.cpp:2051`, face generation `:2798`; `LLVOVolume::updateGeometry`, `llvovolume.cpp:2276`; `LLVolumeGeometryManager::genDrawInfo`, `:6604` | Volume params, all TE channels, object updates, tessellation and parent transforms | Persistent object/face generations; adaptive LOD, correct rebuild causes, link transforms, complete face/material classification and batching | All profiles, paths, cuts, hollow, twist, shear/taper; linked transforms; replacement/delete/local-ID reuse against the same CPU reference meshes |
| Mesh assets/LOD: `LLMeshRepoThread::fetchMeshHeader`, `llmeshrepository.cpp:2017`; `fetchMeshLOD`, `:2105`; `LLVolume::unpackVolumeFaces`, `llvolume.cpp:2311/2327`; `loadMesh`, `llmeshrepository.cpp:4441`; `notifyMeshLoaded`, `:4922` | Mesh header/LOD decoding, faces, UVs, tangents, bounds and cache information | Asset subscribers independent of `LLVOVolume`; retained CPU and GPU generations, actual LOD policy, material faces and upload retirement | Real assets with several LODs, seams/material faces, missing/replaced assets, camera transitions and cancellation |
| Sculpt: `LLVolume::sculpt`, `llvolume.cpp:3205`; map vertices `:3050`; `LLVOVolume::updateSculptTexture`, `llvovolume.cpp:1371`, `sculpt`, `:1470` | Decoded raw sculpt maps, tessellation and topology flags | Raw/discard-generation-driven rebuild without GL texture creation; mirror/invert and all topologies | Sphere/torus/cylinder/plane, mirror/invert, degenerate maps, progressive resolution and replacement |
| Flexible: `LLVolumeImplFlexible::doFlexibleUpdate`, `llflexibleobject.cpp:429`; rebuild `:732`; relative transform `:906` | Simulation/path deformation, forces and wind | Extract simulation state and deformed volume from GL drawable rebuild scheduling; account for time and attachment transforms | Fixed-time wind/force fixtures, attached/unattached cases, crossing/reset and deformed mesh comparison |
| Self/other system avatars: `LLAvatarAppearance`, `LLPolyMesh`, `LLCharacter`; CPU skinning `LLViewerJointMesh::updateGeometry`, `llviewerjointmesh.cpp:456`; avatar deferred/post/shadow pools `lldrawpoolavatar.cpp:224/264/358/671` | Skeleton, visual-param morphs, animation controller and CPU skinning | Every body/eye/hair/skirt branch; baked channels/masks, projected LOD, actual locomotion/sitting/hover/ground, specialized motions, safe asset callbacks | Shape/bake fixtures, exact pose/frame, self/other identity, sitting/hover/ground, shadow/alpha branches |
| Rigged attachments/animesh: `LLSkinningUtil::initSkinningMatrixPalette`, `llskinningutil.cpp:128`; scrub weights `:207`; rigging info `:324`; `LLVOVolume::updateRiggedVolume`, `llvovolume.cpp:5174`; `LLControlAvatar::updateVolumeGeom`, `llcontrolavatar.cpp:283`, animations `:562` | Joint/skin metadata, bind matrices, weights and animation assets | Native palette adapter, joint overrides/scales, animated bounds/pick geometry; control-avatar lifecycle; inventory/object attachment identities; HUD attachment projection; all rigged passes | Weighted multi-joint assets, overrides, attached versus animesh instances, LOD transitions, HUD projection and animation combinations |
| Terrain: `LLDrawPoolTerrain::renderDeferred`, `lldrawpoolterrain.cpp:145`; legacy four-texture `:244`; PBR `:351`; shadow `:187` | Patch inverse DCT, heightfield, texture ranges/starts and surface normals | Four-way smooth splatting, four corner controls, tangent/normal semantics, GLTF/PBR terrain, neighboring/variable regions and LOD | Known heights/slopes, corner controls, blend weights/materials, patch and region joins |
| Trees/grass: `LLVOTree::updateGeometry`, `llvotree.cpp:502`; `LLVOGrass::updateGeometry`, `llvograss.cpp:416`; tree pool `lldrawpooltree.cpp:65/128`; grass `lldrawpoolsimple.cpp:137` | Species, procedural geometry, seeds, wind and billboard/LOD choices | Species assets, native simulation/geometry owners, masks, billboards, wind and shadows | Species coverage, fixed-time wind, distance transitions and masked shadows |
| Particles/effects: groups `llviewerpartsim.cpp:276`, simulation `:717`; geometry `llvopartgroup.cpp:230` | Emitters, source/target state and deterministic simulation | Ribbons/beams/billboards, targeting/death, texture animation, blend equations, glow and bounds, scene sound/effect owners | Seeded bursts, ribbons, targeting, source retirement and every blend mode; stock residual path alongside PPLL |

## Materials and texture resources

| System and reference seams | Required native implementation | Qualification |
|---|---|---|
| Legacy diffuse/bump/shiny/normal/specular: `LLDrawPoolBump`, `lldrawpoolbump.cpp:544/598`; `LLDrawPoolMaterials::renderDeferred`, `lldrawpoolmaterials.cpp:105` | Complete shader permutation and face classification; tangent basis; normal/specular/generated bump maps; shiny/environment intensity; independent UVs; exact alpha mode/cutoff/fullbright | Material permutation chart under controlled lighting, all UV transforms, normal handedness and alpha modes |
| GLTF/PBR: override messages/queue `llgltfmateriallist.cpp:171/269`, lookup `:620`; opaque/emissive `lldrawpoolpbropaque.cpp:53/76`; `deferred/pbropaque*`, `pbralpha*`, `pbrglow*` | CPU material assets and overrides attached to native objects; sRGB base color versus linear ORM/normal/emissive; factors/transforms, alpha/double-sided, rigged variants, BRDF/probes | Metal/dielectric roughness chart, independent UVs, override/replacement, alpha/double-sided and rigging |
| Streaming/color/sampling: texture statistics `llviewertexture.cpp:1803`, LOD stats `:3107`; creation `:1639`; uploads/mips/formats `llimagegl.cpp:774/871/1319` | World mip/discard priorities, semantic formats/sRGB, swizzles, compressed format policy, anisotropy/wrap/clamp/filter variants, raw ownership, cache/VRAM budget and eviction | Mip/discard transitions, wrap/clamp/anisotropy, color-transfer ramps, normal/ORM preservation, replacement and pressure |

The current one-texture packet and UI RGBA8 provider cannot express the reference
material schema. Expand geometry attributes, material/resource descriptions,
samplers, persistent generations and pass classification before independently
adding shader effects. Do not misrepresent absent mesh/sculpt/flexible resources
as ordinary undeformed prims.

## Environment, lighting and dependent views

| System and reference seams | Inputs and required native execution | Qualification |
|---|---|---|
| EEP/day cycle: `LLEnvironment::updateEnvironment`, `llenvironment.cpp:1600`; camera `:1759`; cloud scroll `:1801`; region `:1978`, parcel `:2046`; day progression/init/animation/transitions `:2796/2859/2912/2979`; GL uniform adapter `:1840` | Actual region/parcel/local precedence, environment assets, altitude tracks, day length/offset, blending/transitions and injected settings; renderer-independent snapshots of shared CPU settings | Frozen sunrise/noon/sunset/night, altitude tracks, precedence, teleport transition and texture crossfades |
| Analytic atmosphere, sky/sun/moon/clouds/stars: `LLVOSky::updateDirections/updateSky`, `llvosky.cpp:656/679`, heavenly geometry `:1105`; dome/stars `llvowlsky.cpp:145/338/513`; haze/stars/clouds/sun-moon/deferred `lldrawpoolwlsky.cpp:143/219/290/354/471`; WindLight and `class3/deferred/hazeF.glsl` | Reference analytic atmosphere and depth haze, actual dome/cloud/star/celestial geometry, EEP values, cloud scroll and blended texture generations | Same environment/time/camera reference images, disc orientation, clouds, stars and haze/depth |
| Visibility/G-buffer: `updateCull/occlusion/stateSort/postSort`, `pipeline.cpp:2634/2778/3281/3743`; screen allocation/shared depth `:897/973`; deferred attachments `:381` | Native bounds/spatial partitions/visibility and batching; diffuse and ORM/specular MRT, normals RGBA16 or RGB10_A2, optional emissive RGB16F/RGB, shared depth; screen scene RGBA16F | Attachment encodings, masked depth, cull identities, resize/reset and explicit access validation |
| Sun/moon/local/projector lights: nearby lights `pipeline.cpp:6162`; deferred `:9674`; sun/SSAO `:9729`; soften/atmosphere `:9824`; local `:9879`; projectors `:9997`; fullscreen `:10039`; setup `:10332` | EEP directions/colors, actual object light/projector inputs, BRDF, falloff/scale, light limits, camera-inside volumes; opaque and transparent lighting | Per-light controlled scenes, ranges/cones, projector orientation, limits/cull, transparent surfaces |
| Sun/spot shadows: `generateSunShadow`, `pipeline.cpp:11119`; `renderShadow/renderGeomShadow`, `:10686/4494`; map bind `:9393`; shadow/avatar/tree/PBR shader variants | Cascades/spot fitting and caster cull; matrices/depth targets, compare samplers, bias/PCF; masked, rigged, tree and PBR caster behavior | Shadow maps and final edges, cascade seams, spot shadows, moving cameras/lights and mask/rigging |
| Probes/irradiance/radiance/mirrors: manager update `llreflectionmapmanager.cpp:206`; face `:773`; neighbors `:1044`; uniforms `:1084`; cubemap face `llreflectionmap.cpp:52`; hero/mirror `llheroprobemanager.cpp:76/242/313/437` | Dependent deferred views; six faces, cube arrays/mips and filtering; influence/blend data and UBO ABI; reflected camera/clip plane and recursion rules | Face orientation, filtered mip/irradiance samples, influence/blend, dynamic schedules, mirror clipping/recursion |
| Water/void water/haze/exclusion: `LLDrawPoolWater::renderPostDeferred`, `lldrawpoolwater.cpp:142`; exclusion `pipeline.cpp:10321`; water haze `:10237`; underwater insertion `:4352` | EEP normals/waves/fresnel/fog and blend; scratch scene/depth (`mWaterDis`), exclusion mask, reflections/deferred inputs; above/underwater variants and void surfaces | Above/below transitions, submerged alpha, exclusion, normal crossfade, shoreline/void boundaries and reflection/refraction |

Accuracy boundaries: the reference atmosphere is analytic WindLight/EEP. This
survey found no dedicated transmittance/scattering-LUT pipeline to port. The
actual renderer LUTs are the legacy specular and PBR BRDF resources below.

## Transparency, HDR and postprocessing

| System and reference seams | Required resources, execution and ordering | Qualification |
|---|---|---|
| Alpha/masks/PPLL: `LLDrawPoolAlpha::renderPostDeferred`, `lldrawpoolalpha.cpp:142`; eligibility `:200`; render `:750`; allocate/begin/end/composite `pipeline.cpp:7986/8112/8158/8171` | Face alpha/blend classification, pre/post-water splits and clipping; sorted groups, rigged masks, emissive and depth replay; PPLL R32UI head image, node SSBO and atomic allocator; barriers and overflow/residual policy | Intersections and insertion-order invariance; exact sorted fallback, overflow, additive/particle residual, glow occlusion and DoF depth |
| HDR/exposure/tonemap/gamma/LUT: `createLUTBuffers`, `pipeline.cpp:1585`; exposure buffers `:1672`; luminance `:7690`; exposure `:7737`; tonemap `:7852`; gamma `:7921`; `genbrdflutF.glsl` | RGBA16F linear scene; legacy R16F specular and 512² RG16F BRDF; 256² R16F luminance mip reduction; current/history 1² R16F exposure; adaptation/reset, tonemap choices and display transfer | Float scene/LUT/luminance/exposure readbacks, values above 1, fixed-dt dark/bright adaptation, gamma and tonemap charts |
| SSR/SSAO/history: SSR scene copy `pipeline.cpp:7949`; previous matrices `:10159`; `class3/deferred/screenSpaceRefl*`; SSAO map/blur `:9729/9756` | Previous scene/depth/matrices, first-frame/reset rules, reflection raymarch/fallback, depth reconstruction, AO radius/bias/noise and separable blur | Fixed camera sequences/frozen noise, disocclusion/offscreen, depth reconstruction, teleport/resize reset and reference AO |
| Glow/DoF/FXAA/SMAA/CAS/post-effects: glow `pipeline.cpp:8252`; CAS `:8362`; FXAA `:8416`; SMAA generation/apply `:8502/8620`; glow combine `:8704`; vignette `:8726`; snapshot frame `:8773`; DoF `:8968`; finalize `:9191` | Ping-pong targets, depth/focus, SMAA lookup textures; exact settings-dependent ordering; RLVa sphere, vignette, screenshot frame, visualizations and final screen/noise/depth/guides | Impulse, depth and edge fixtures plus composed images across settings, edit/probe DoF exceptions, resize/zero-size and history reset |

Verified finalize dependencies: SSR history → luminance/exposure → tonemap →
optional CAS/gamma, or gamma without that branch → glow generation/combine →
optional DoF → FXAA or SMAA → RLVa sphere → vignette → snapshot framing →
visualization override → final screen/noise/depth → guides/focus/physics overlays.
Port these as an explicit graph rather than unrelated effects on the swapchain.

HDR settings describe HDR lighting/postprocessing; they do not establish HDR10
OS/swapchain output support. No integrated TAA was found. SMAA library T2x
comments are not evidence of an enabled temporal-AA viewer path.

## Auxiliary rendering and feature owners

| System and reference seams | Required integration | Qualification |
|---|---|---|
| World/HUD picks and selection: world/HUD/particle intersections `pipeline.cpp:7200/7376/7158`; `pickImmediate/cursorIntersect`, `llviewerwindow.cpp:5629/5674`; HUD projection `llviewerdisplay.cpp:1384/1476`; HUD text/icons `llhudtext.cpp:123/748`, `llhudicon.cpp:83`; selection/highlights `llviewerwindow.cpp:5284`, `pipeline.cpp:4153` | Real object/face identity, native mesh/rigged intersections, HUD projection, touch/edit/context-menu services, outlines/beacons/handles and render masks | Picked UUID/face/UV/normal and transparency/rigged/particle flags; screen/world/HUD mapping, selection and edit actions |
| Snapshots/previews/bakes/maps/impostors: `rawSnapshot`, `llviewerwindow.cpp:6295`; image/avatar/sculpt previews `llfloaterimagepreview.cpp:1011/1215`; model preview `llmodelpreview.cpp:4307`; dynamic textures `lldynamictexture.cpp:184`; impostors `pipeline.cpp:12004`; layer render/masks `llappearance/lltexlayer.cpp:364/481/1030/1299`; minimap `llnetmap.cpp:310/1374/1471` | Production offscreen views and shared CPU owners; tile/rebuild/readback, encoding and UI-image publication; local compositor/morph masks, impostors and map object/parcel overlays | Actual preview widgets, snapshot dimensions/orientation/UI/HUD/depth modes, bakes and masks, impostor rotation/alpha, minimap data |
| Appearance/inventory/actions | COF/wearable owner, attachment queue, gestures/AO/animation and script controls; native exclusions in `llinventorybridge.cpp:7674/7875/8040/8456` must be replaced by functioning owners | Wear/change/detach/play/reset through ordinary UI, self/other identity, crossing and logout cancellation |
| Firestorm bridge | Self-avatar/attachment readiness, inventory↔object identity, task-inventory replies, asset transfer/listeners, prim mutations/script installation and owner-chat processing; `fslslbridge.cpp:61`, creation `:826–928` | Create/reuse, forced detach/reattach, script upload, handshake, bridge-dependent UI actions, crossing/logout/reconnect |
| Voice and spatial services | Native account authorization/provisioning, region capabilities and simulator feature selection; adapt `llvoicewebrtc.cpp:532–543/2729/2788/3419` legacy avatar/region/world assumptions; nonspatial calls must not inherit spatial-only requirements | Account login/logout, peer/group calls, spatial position, ICE/STUN/signaling, provider selection and stale callbacks |
| Remaining connected UI/services | Inventory scene operations, scene sounds/effects, blacklist/derender, nearby objects/media, camera/selection, parcel/object properties and controls must have corresponding owners | Exercise ordinary controls and their network/output effects, including unavailable and error cases; enabling menu entries alone is insufficient |

## Implementation and qualification discipline

The common prerequisite is coherent native scene/asset/service ownership, not
one adapter per visual symptom. Object, face, region, avatar/control-avatar,
attachment, image/material, inventory/task and interaction identities need
matching init/tick/reset/teardown. Preserve shared CPU decoders/settings/services
where possible; separate GL resources and lifetime assumptions explicitly.

Performance accompanies this work. Measure simulation/services, scene packet
construction, skinning/tessellation, upload bytes, UI collection, draw/binding
counts, GPU pass cost and presentation separately. The original per-draw constant
and SRB allocations and per-glyph UI draws are architectural costs, but reducing
them cannot establish missing material, environment or postprocess behavior.

Software Vulkan qualification can exercise the complete chain without AMD/NVIDIA
hosts: fixed-clock/seed production scene replay, real assets, pass readbacks and
resource/history contracts on SwiftShader and Lavapipe. Add an independent
software OpenGL reference run with identical assets/settings/camera/environment.
Use independent numerical/pass oracles as well as final-image comparison; golden
screenshots or standalone triangles alone cannot qualify viewer wiring. Keep
normal device discovery for actual machines and vendor drivers.

Acceptance requires functioning production viewer routes and complete composite
output, with measured performance and explicitly reported runtime limits. A pass
name, settings allowlist, compiled shader or harness result is not completion.

## Second-pass completeness findings

A second independent source pass identified the following requirements. These
are part of full parity, not optional additions to the first visible scene.

| System and verified reference seams | Native integration requirement and current gap | Acceptance evidence |
|---|---|---|
| GLTF scene assets, distinct from PBR surface materials: `llviewerobject.cpp:8004`, `llvovolume.cpp:1356`, `GLTFSceneManager::addGLTFObject`, `gltfscenemanager.cpp:362/382/430`; rendering `:578/597/638`, opaque dispatch `lldrawpoolpbropaque.cpp:59` | Separate GLTF asset and referenced `AT_GLTF_BIN` buffer ownership; node hierarchy, asset-to-agent transforms, multi-UV/material/double-sided/unlit/rigged variants. The native legacy LL mesh decoder does not cover this. | Real multi-node scenes, embedded/external buffers, interleaved/sparse accessors, material assignment, skinning and animation; exact selected node/primitive identity. |
| GLTF CPU animation and picking: `gltf/asset.cpp:95/106/123/240`, `gltf/animation.cpp:84`, `gltf/primitive.cpp:635/735`, `gltfscenemanager.cpp:843/935`; GL resource seams `gltf/asset.cpp:134/191/624`, `gltf/animation.cpp:402/455`, `gltf/primitive.cpp:540` | Preserve CPU scene evaluation and intersections while replacing GL node/material/skin buffers and geometry lifetimes. Sharing current asset classes without separating their GL upload/destruction paths is insufficient. | Frozen animation poses, exact picked UV/normal, all shader variants, replacement, account reset and safe destruction. |
| Animated TE/PBR texture matrices: `llvovolume.cpp:402/610/729/812`, `LLViewerTextureAnim::animateTextures`, `llviewertextureanim.cpp:78`, `setup_texture_matrix`, `lldrawpool.cpp:758` | Per-object/face animation state and clock; frame atlas, smooth/loop/reverse/ping-pong, rotation/scale/translation, stopped final TE state and PBR matrix semantics. Current static TE transforms are insufficient. | Deterministic times for every mode, one/all-face targets, atlas orientation, stopped/replaced animation and batching. |
| Media-on-a-prim: `LLViewerMedia::updateMediaImpl`, `llviewermedia.cpp:306`; volume state `llvovolume.cpp:2787`; `preMediaTexUpdate/doMediaTexUpdate/updateMediaImage`, `llviewermedia.cpp:3120/3170/3213`; focus `llviewermediafocus.cpp:87/404/455` | Dynamic native world image generations, dirty rectangles, plugin format/stride/origin, per-face substitution, permissions, navigation and picked-surface input. Native guard `llviewermedia.cpp:3073` skips the GL texture path; the native parcel browser does not substitute for MOAP. | Deterministic media frames, partial updates, resize/shrink, aspect/orientation, pointer/key input and synchronized scripted media state. |
| Object motion interpolation: `LLViewerObject::idleUpdate/interpolateLinearMotion`, `llviewerobject.cpp:2538/2568` | Timestamped velocity/acceleration/angular motion, time dilation, attachment/selection policy, correction and bounded extrapolation. Native storage of velocity/omega alone does not advance objects between simulator packets. | Fixed-dt trajectories and rotations between sparse updates; correction, selection freeze, deletion and crossing. |
| Neighbour simulators and variable regions: `LLWorld::addRegion/removeRegion/updateRegions/updateOffset`, `llworld.cpp:524/681/1125/1515`; size messages `llviewermessage.cpp:3772/4216`; surface/parcel/neighbours `llviewerregion.cpp:674–705/2068–2164` | Host/handle/generation-keyed visible-region owners and enabled neighbour circuits; per-host local IDs, transforms, terrain seams, dimensions, water/environment/parcels and floating origin. Native session retains dimensions but the scene heightfield/water extent remain 256 m, admission is current-host-only and crossing resets the scene. | Standard/larger regions, duplicate local IDs on different hosts, adjacent objects and terrain/water seams, origin shifts, crossing/teleport and stale packets. |
| Avatar cloth/physics/visibility: `lldrawpoolavatar.cpp:906`; `llvoavatar.cpp:3793/2758/4714/9770/12220`; shadow exclusions `lldrawpoolavatar.cpp:393`, dependent animesh visibility `:859` | Specialized CPU motions and cloth inputs, complexity/impostor/jelly/invisible policy, self mouselook visibility and consistent attachments/shadows. Skeleton/keyframe support alone is incomplete. | Known cloth/physics frames, complexity limits, policy transitions, camera modes and dependent shadow/attachment behavior. |
| Local asset reload: `LLLocalGLTFMaterial::updateSelf`, `lllocalgltfmaterials.cpp:123/364/498`; `fslocalmeshimportgltf.cpp`; `GLTFSceneManager::load`, `gltfscenemanager.cpp:318` | Native texture/material/mesh subscribers must honor local identity and replacement generations, including changes to dimensions/topology, without forcing UUID server fetch or retaining the first upload. | Live file modification/reload, deletion/error, topology/size changes and consistent world/preview updates. |
| Asynchronous occlusion and timers: `LLOcclusionCullingGroup::checkOcclusion/doOcclusion`, `llvieweroctree.cpp:1107/1134/1146/1172/1220/1239/1281`; avatar timers `llvoavatar.cpp:13193–13213` | Per-view query ownership, readiness and invalidation; conservative visibility while results are absent; optional timestamps without synchronous stalls. The transform-feedback block `pipeline.cpp:3860` is commented out and is not an active required pass. | Query lifecycle, visibility under missing/stale results, resize/reset; measured performance separately from software correctness. |
| Physics/build/debug/service overlays: `llspatialpartition.cpp:2183/2291/2330/2348`; physics/debug `pipeline.cpp:4965/5026`; beacons/tracker/HUD `:3995/4083/4100`; parcel lines `llworld.cpp:1212`; GLTF debug `gltfscenemanager.cpp:1063` | Renderer-independent physics meshes, bounds/edges/wireframes/selection handles, parcel/telehub/tracker owners and native overlay depth/projection. These are functioning editor/user controls, not just ordinary scene triangles. | Exact geometry and selected targets for each supported overlay, projection/depth and production toggle effects. |

Native weighted attachment deformation is now implemented through
`VSWorldAvatar::skinRiggedFace` and the scene owner, using real skin metadata,
bind matrices and CPU joint palettes. It remains under qualification. This does
not establish joint overrides/restoration, fitted mesh/global scale, animesh,
HUD projection, complete materials/shadows or appearance/inventory lifecycle.
The current rigged path rebuilds geometry every frame and needs separate upload,
retirement and performance qualification.

## Settings-to-native-controller inventory

`settings_setup_listeners` in `llviewercontrol.cpp:1315` registers production
rendering callbacks at `:1320–1378`. Native callback wrappers at `:1295/1307`
bypass nearly all of these, except logging throttle and SDL IME. Thus a visible
control and a persisted setting can still have no native rendering effect.
Re-enabling the legacy wrappers wholesale would invoke GL resources and legacy
world owners; native controllers must explicitly apply the same semantics.

| Controller family | Required native effect | Qualification |
|---|---|---|
| Terrain, PBR scale/detail, object/avatar/tree/flexible LOD, octrees | Scene geometry/material rebuilds, projection-aware LOD, partition invalidation and asset demand | Change ordinary Preferences controls; assert actual geometry/material/visibility changes and persisted restart behavior. |
| HDR, exposure, tonemap/LUT, glow, DoF, AA, CAS | Shader/permutation and pass selection, target allocation, history reset, output color policy | Change controls during rendering; compare affected pass outputs and final image, including disabled/unsupported policy. |
| Shadows, probes, mirrors, SSR, SSAO | Dependent view schedules, resolution/quality/resource changes, culling and history invalidation | Assert target/view/pass changes, bounded lifetime and actual visual effects under fixed scenes. |
| Anisotropy, texture budgets and cache/streaming policy | Sampler generations, mip demand, budget accounting and safe eviction | Verify sampler state and mip selection, memory pressure/replacement, and no stale descriptors. |
| Resolution, VSync and presentation | Swapchain/viewport/targets, camera aspect and history updates; supported present mode | Resize/minimize/restore and in-session setting changes, recorded capability fallback. |
| Particles, cloth, avatar complexity/impostors | Simulation budgets, visibility/deformation and dependent geometry/shadow behavior | Actual owner/output changes at deterministic clocks, account reset and transitions. |

An exhaustive implementation inventory must classify every registered rendering
listener and ordinary UI route as implemented, unavailable with a real reason,
or pending with an owner and test. Saving values or enabling menu entries is
not wiring. Capability constraints must be reflected consistently in UI and
runtime state.
