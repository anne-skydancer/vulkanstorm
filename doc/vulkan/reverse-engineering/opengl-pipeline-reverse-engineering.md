# OpenGL rendering pipeline: behavioral specification for a native Vulkan rewrite

## Baseline and evidence

- Repository: `anne-skydancer/vulkanstorm`.
- Starting point: local `origin/master`, **59108e15a1f8f94d2da7c674d937d19f5cf9450d**, "Merge pull request #39 from anne-skydancer/fix-chat-console-markdown".
- `HEAD` matched that commit and the working tree was clean when analysis began. The remote-tracking ref was not refreshed; this identifies the exact baseline rather than claiming it is the latest remote commit.
- Method: static, end-to-end source tracing of frame orchestration, scene submission, shader producers/consumers, auxiliary views, resource management and the existing Vulkan backend. No source changes or runtime rendering experiments were made.
- Evidence-backed behavior is distinguished below from **proposed architecture** and **validation still required**. This is not a claim of measured pixel parity.
- [Immutable baseline on GitHub](https://github.com/anne-skydancer/vulkanstorm/tree/59108e15a1f8f94d2da7c674d937d19f5cf9450d).

Detailed companion investigations:

1. [Scene submission, geometry, draw pools and transparency](scene-submission.md).
2. [Shader ABI, materials, lighting and color mathematics](shader-contracts.md).
3. [Resource lifetime, GL assumptions and existing Vulkan backend](backend-contracts.md).
4. [Probe scheduling, environment, picking, snapshots and UI](auxiliary-views.md).

Source links below refer to the pinned GitHub commit. The companion investigations provide the deeper implementation evidence behind the cross-system conclusions. Investigation-time worktree and validation statements are historical; see the [publication notes](README.md) and the governing [development invariants](../native_vulkan_invariants.md).

## 1. Executive finding

**The renderer is a view-dependent, temporal hybrid renderer, not a sequence of interchangeable OpenGL calls.**

Its output is governed by:

1. **Which representation is available and selected:** streaming textures, generated geometry, LOD, rigged attachments, avatar impostors and visibility policy.
2. **Which material interpretation applies:** legacy diffuse/fullbright/shiny/materials and glTF PBR coexist. Their G-buffer values and shader branches are not interchangeable.
3. **Where a surface belongs in composition:** opaque/masked geometry, pre-water transparency, water, atmospheric haze, post-water transparency, glow, world overlays, HUD and UI have distinct contracts.
4. **Which view is rendering:** main camera, shadow, probe, mirror, impostor, preview and snapshot share machinery but deliberately use different rules.
5. **What survives from earlier frames:** exposure, SSR scene/depth, camera transforms, probe contents, spotlight selection/fades, occlusion and streamed asset state affect current output.

A native Vulkan rewrite should preserve those decisions and equations while replacing GL's implicit state and resource ownership with explicit scene data, view descriptions, material variants, pass dependencies and GPU lifetime management.

Do not start by reimplementing `gGL`, translating every `bindTarget`, or preserving pool classes as Vulkan command recorders. Conversely, replacing this system with a generic physically based renderer would also fail parity: several legacy and Firestorm-specific visual rules are intentional inputs to the observed result.

### 1.1 Existing Vulkan is not the reference renderer in another API

There are three backend choices in this revision:

- **OpenGL:** the world renderer analyzed here. Application initialization forces deferred rendering; leftover nondeferred branches do not establish a supported alternative world pipeline.
- **Zink:** the same OpenGL renderer running through Mesa's GL-on-Vulkan implementation. Its world-rendering capability is not native renderer completion.
- **Native Vulkan:** Windows-only, owns a GL-free window and presentation, reconstructs UI from widget/CPU state, and currently clears/presents after login rather than rendering the world.

The native pipeline family has one RGBA8 sampled 2D texture, 2D vertices, an orthographic push-constant matrix, four blend modes and two topologies, with no world G-buffer/depth/shadow/probe pipeline. There is no native GL-image-sharing bridge to extend. See the [backend investigation](backend-contracts.md) and the [native display early return][display].

The existing backend offers useful window, loader and UI groundwork, but should not be treated as a production-ready resource substrate. The backend investigation identifies source-level frame-buffer reuse/retirement, noncoherent memory, capture usage/ownership, descriptor capacity and WSI/capability risks. These were not changed or runtime-tested here. In particular, the current capture path must be validated/repaired before treating it as a trustworthy parity oracle.

## 2. The actual main-frame execution

### 2.1 Frame preparation and CPU/GPU overlap

The central [display entrypoint][display] has early exits for resize, inactive/minimized windows, headless operation and startup. Some inactive-window work remains important: rebuilding pool bookkeeping, retiring HUD effects, returning empty picks and advancing texture maintenance.

For an active logged-in GL frame, the relevant order is:

| Order | Operation | Why the position matters |
|---|---|---|
| 1 | Check settings, perform pending picks, update texture statistics and frame counters | Picking is not a late final-color operation. Simulation/application state is part of the render input. |
| 2 | Update dynamic textures; restore main viewport | Dynamic textures can use framebuffer resources before the main scene. |
| 3 | Update/render hero probes and mirrors, when enabled and not a snapshot | These are rendered before the main camera's ordinary scene work. Do not blindly schedule them after presentation. |
| 4 | Set camera, update environment, HUD effects and geometry queues, run GL updates | Builds the state from which the following views derive their draws. Geometry work has a frame-time budget. |
| 5 | Cull main view, with underwater state and temporarily reduced occlusion use when depth is invalid | Visibility is view-specific and temporally qualified. |
| 6 | Resize targets if requested; generate sun/spot shadows; update avatar impostors | These auxiliary renders temporarily change camera, targets and draw lists. |
| 7 | Update images/material lifetime | Explicitly positioned to give earlier occlusion queries time to complete; can invalidate material/texture classification. |
| 8 | State-sort visible data, rebuild pools if requested, update sky | Final draw lists are constructed after texture updates, not just after simulation. |
| 9 | Clear G-buffer; optional restricted depth prepass; draw deferred pools | The prepass is only simple/fullbright/shiny prim passes, not a universal opaque prepass. |
| 10 | Deferred lighting and ordered post-deferred geometry | Produces the scene color/glow buffer, not the final display image. |
| 11 | Scene-monitor capture, finalization, HUD/UI, swap, release frame references | The capture point is not synonymous with final UI-composited presentation. |
| 12 | Back in the application loop, update ordinary reflection probes, then snapshot floaters | Ordinary probe updates are **after display**, unlike hero/mirror updates. Their new products generally affect the next displayed frame, but subsequent snapshot rerenders can see them immediately. |

Evidence: [preparation and geometry updates][display-prep], [cull/shadows/impostors/image updates/state sort][display-cull], [G-buffer through presentation][display-draw], [UI composition][ui].

The enclosing application-loop boundary is documented in the [auxiliary-view investigation](auxiliary-views.md), with the after-display call at [llappviewer.cpp:1785][app-display].

The `for_snapshot` parameter and global `gSnapshot` are not interchangeable. `display` records `gSnapshot` first and then clears the local `for_snapshot` in deferred mode to make sky rendering work. Reconstruct snapshot behavior from its callers, not from the parameter's name.

### 2.2 Canonical dependency graph

This graph is a logical description, not a proposed one-command-buffer-per-box design.

```text
simulation / animation / environment / asset residency
          |
          +--> dynamic textures, bakes, previews
          +--> hero / mirror views --> updated hero products
          |
          +--> main-view cull and representation selection
                     |
                     +--> shadow receiver fitting / caster lists --> shadow maps
                     +--> avatar impostor refresh --> impostor G-buffer textures
                     |
                 image updates -> draw classification / sorting
                     |
                 opaque + masked G-buffer / depth
                     |
                 shadow/SSAO visibility map [optional]
                     |      `--> two-pass blur when SSAO is enabled
                     |
                 base lighting + environment / probes
                     |
                 additive point and projected spot lighting
                     |
                 ordered forward/material + water/haze/alpha composition
                     |
                 scene HDR RGB + glow alpha
                     |
           +---------+----------------------+
           |                                |
        SSR history copy                luminance -> exposure history
           |                                |
           |                           tone map -> CAS/gamma
           |                           or legacy gamma-only branch
           |                                |
           |                          glow extraction/blur/combine
           |                                |
           |                           DoF -> FXAA or SMAA
           |                                |
           |                          RLVa/vignette/snapshot effects
           |                                |
           |                          final noise + depth output
           |                                |
           |                           world/HUD/UI composition
           |                                |
           `----> subsequent frame        present
                                            |
                                   ordinary probe update/filter
                                            |
                                   snapshot floater updates
                                            |
                                   subsequent displayed frame
```

The graph omits deliberate debug overrides and uses named logical images where GL reuses scratch targets. It must not be interpreted as putting all transparency on one side of water or all atmosphere after all geometry.

## 3. Render-target and data contracts

### 3.1 Logical resources versus current allocation

The current [target packs][target-pack] contain scene color, G-buffer, a lighting/scratch target and four sun-shadow targets. Main, auxiliary probe and hero-probe packs are separate; `mRT` is a mutable pointer selecting one. A native renderer needs an explicit view/resource context instead.

| Logical resource | Current storage / behavior | Native parity requirement |
|---|---|---|
| G-buffer attachment 0 | `GL_RGBA`; base diffuse/albedo plus material-dependent alpha | Preserve material-dependent encoding, not just four float names. |
| G-buffer attachment 1 | `GL_RGBA`; legacy specular or PBR ORM | The material discriminator controls interpretation. |
| G-buffer attachment 2 | HDR configuration: `GL_RGBA16` normalized, **not FP16**. Non-HDR: `GL_RGB10_A2` | Encoded normal and flags require their actual precision and thresholds. |
| Optional attachment 3 | HDR: `GL_RGB16F`; non-HDR: `GL_RGB`; gated by emissive-buffer setting | Match fallback when absent, not merely the highest-quality path. |
| Opaque scene depth | `DEPTH_COMPONENT24`, shared by G-buffer and scene target; main geometry uses `LEQUAL` | Model one logical depth resource and its read/write transitions. |
| Scene color | Allocated `GL_RGBA16F` even when HDR postprocessing is off | HDR switch is not simply the scene image's format. Alpha carries glow, not final coverage. |
| Lighting visibility / scratch | Conditional target; HDR-dependent format; reused for SSAO/shadows and later postprocessing | Separate logical lifetimes before considering memory aliasing. |
| Water exclusion | `R8` plus its **own depth** | Multiple water planes/exclusion volumes cannot use only main opaque depth. |
| Water displacement/scratch | Color + depth, screen-sized | Includes depth copies for haze; water also needs its own scene sampling contract. |
| SSR history | Scene color + depth, allocated when SSR enabled | History must outlive current lighting reads and be replaced only at its producer point. |
| Post ping/pong | `GL_RGBA` display-space intermediates | Do not move all post effects to linear HDR and expect the same result. |
| Glow | Three targets, width 512 and setting-derived height up to 512; format chosen separately by `RenderGlowHDR` | Glow resolution, precision and alpha channel semantics are independent of viewport. |
| Sun shadows | Four depth targets per pack; aligned setting-scaled dimensions | Shadow resolution and filter behavior affect visible results. |
| Spot shadows | Two shared square depth targets outside cube allocations | Separate temporal light ownership from the light list. |
| PPLL transparency | `R32UI` head image, packed-node storage buffer, allocator counter; main-view resolution | Preserve bounded capture/resolve and degradation semantics if this mode is offered. |
| Depth-peeling selection | Two `R32F` ping-pong targets sharing opaque depth | These hold selected depths as color; they are not two ordinary depth attachments. |

Evidence: [attachment formats][attachments], [screen allocation and shared depth][allocation], [shadow allocation][shadow-allocation], [glow allocation][glow-allocation], [transparency resources][oit].

For exact per-channel encodings, texture color spaces and shader interpretation, use the [shader contract investigation](shader-contracts.md).

### 3.2 Buffer validity is not merely allocation

The main G-buffer clears all attachments/depth, then renders selected pools. The scene target is cleared to zero including alpha before deferred base lighting; the code explicitly warns that failing to zero alpha accumulates glow against sky. Post-deferred blending modifies RGB and glow under different rules.

The lighting target may be absent. [Deferred binding][deferred-bind] uses a white image when there is no complete light target, representing neutral visibility. Texture filtering is also contractual: G-buffer fields use point sampling/clamp, while history, shadow and post filters differ.

Target allocation applies render-resolution divisor/multiplier, so requested pack dimensions and physical image dimensions need not agree. UI/world rectangles, raw/scaled viewport dimensions and snapshot tile dimensions must be represented separately.

### 3.3 Streaming-to-GPU publication is a renderer boundary

[Texture-list updates][texture-updates] budget work across fast-cache loading, dispatch to fetch/decode workers, creation of decoded textures and loaded callbacks. Teleport state can clear/defer requests. Decode priority depends on visible face demand and discard bias, not only asset identity.

[Texture creation scheduling][texture-publish] either posts creation to the image worker and completion back to the main thread, or queues creation for main-thread processing. Completion can invalidate sculpt geometry and then release raw CPU pixels. [The main creation queue][texture-create-queue] skips redundant uploads when existing residency already satisfies the desired discard level and performs GPU downscaling under memory pressure.

Therefore native upload jobs need an explicit ownership/publication protocol: retain decoded data until upload consumption is complete, publish the new image version at the correct application boundary, preserve completion/rebuild callbacks, and retire old images after GPU use. Do not assume every resident texture retains raw CPU pixels that the Vulkan renderer can query later. This is independent of whether uploads use a dedicated transfer queue.

### 3.4 The G-buffer is a material-tagged union

The [shader investigation](shader-contracts.md) traces both writers and readers:

| Field | Legacy material | SL PBR material |
|---|---|---|
| Base RGB | sRGB-valued texture RGB multiplied by vertex RGB before decode | Texture decoded to linear, then multiplied by vertex RGB |
| Base A | Fullbright/emissive factor, not coverage | Zero |
| Parameters RGB | sRGB-valued specular color | Linear occlusion / perceptual roughness / metallic |
| Parameters A | Glossiness, including normal-texture alpha modulation | Zero for ordinary PBR objects |
| Normal RG | Packed view-space unit normal | Same |
| Normal B | Environment intensity, including specular-texture alpha modulation | Zero |
| Normal A | Atmospheric legacy category | PBR category |
| Optional emissive RGB | Zero for ordinary legacy material writer | Linear emissive |

Sky/HDRI and simple shiny geometry have additional payload rules; the table is not a universal sky/simple-material decoder. The actual lighting accessor does not automatically linearize these fields; the material branch performs the necessary decode.

Consequences:

- An sRGB attachment view over the entire base/parameter image is not equivalent: adjacent legacy and PBR pixels store different representations.
- The normal encoding is `RG = n.xy / sqrt(8*n.z+8) + 0.5`, not octahedral encoding. Decoding uses `q=4*RG-2`, `f=dot(q,q)`, `n.xy=q*sqrt(1-f/4)`, `n.z=1-f/2`.
- View position reconstruction uses inverse projection of `(2*u-1, 2*v-1, 2*depth-1, 1)`, divided by W; view-forward Z is negative.
- Optional emissive changes sky writers/readers as well as PBR materials. It must be a consistent graph/shader configuration.

Source anchors: [G-buffer accessor][gbuffer-access], [normal packing][normal-pack], [PBR writer][pbr-writer], [base-light material branches][base-light].

## 4. Scene visibility and submission policy

[Culling][cull] visits enabled region spatial partitions, bridge partitions and the object-cache tree; sky is injected separately. It installs a water clip plane when appropriate. Mirrors explicitly bypass that water clipping rule.

[State sorting][state-sort] consumes available occlusion results, marks occluded groups for future tests, exposes visible drawables, updates distance/LOD under the main camera conditions and rebuilds visible meshes. [Post sorting][post-sort]:

- Rebuilds eligible geometry outside cube snapshots.
- Filters occluded or excessive-surface-area groups.
- Collects per-pass draw records from each spatial group's draw map.
- Builds world-alpha and rigged-alpha lists separately.
- Propagates avatar/attachment depth and render-order metadata.
- Completes delayed mesh packing.
- Establishes legacy alpha ordering first; the post-water consumer can replace it with interleaved ordering.

Thus "draw all meshes from a scene graph" does not reproduce the source. Material routing, alpha modes, representation changes, avatar budgets and streamed texture metadata must survive as backend-neutral policy.

Occlusion is delayed and conservative rather than a synchronous query per object, although group-query timeout handling can eventually force a blocking result read. Queries are issued during deferred pool traversal at the grass boundary after earlier opaque work, and probes/object-cache groups have their own participation. Cube views, HUD and shadow generation disable main-view occlusion. Invalid main depth temporarily reduces its use. A Vulkan HZB implementation is a possible optimization, **not an established equivalent** without visibility/temporal testing.

Evidence: [query issuance][occlusion], [deferred traversal][deferred-pools], [scene submission investigation](scene-submission.md).

### 4.1 Geometry and material routing invariants

- Active moving linksets use local spatial bridges; once the bridge passes its outer test the internal tree is marked visible as a unit. This differs from a fresh child-level frustum test.
- Static geometry is region-relative, active geometry can be root/parent-relative, scale is baked rather than necessarily present in the drawable transform, and rigged submissions use skinning rather than ordinary model matrices.
- Volume LOD has four levels, distance/FOV/bounds policy, staggered update eligibility and hysteresis. HUDs and rigged attachments use special rules. Probe/shadow jobs must not independently force all scene LOD decisions.
- Classification can emit base, post-bump, glow and exclusion contributions from one face. It is not one surface to one draw.
- Initial [texture-entry pool selection][pool-selection] is only coarse classification: TE alpha below 0.999, image component count (with a media exception), legacy alpha mode, bump/shiny and PBR presence all participate. Later volume pass emission refines/overrides it. Texture decode metadata therefore affects routing; a GPU material enum alone is insufficient.
- Legacy automatic alpha masking is distinct from an explicit material MASK. It excludes PBR, rigged geometry and several alpha/glow/HUD cases. Cutoff values also differ by grass, legacy material, PBR and avatar path.
- Rigged alpha preserves authored face order; interleaving orders avatar ensembles rather than all triangles.
- SL PBR base-color factors are baked into vertex colors; its binder supplies per-channel texture fallbacks and overrides non-MASK alpha cutoff to -1. Reapplying a base-color factor or a generic alpha threshold in Vulkan changes results.

### 4.2 Special geometry is part of the world renderer

Terrain blends four legacy textures or PBR material slots using the selected height/noise/paint policy; its UV direction and per-material transforms are not simply ordinary mesh UVs. Built-in avatar eyes/body and rigged mesh attachments take different submission routes. Old shiny and emboss bump remain active, while the old sky pool is empty.

The active WL sky renders haze, celestial bodies, stars and clouds with capture exceptions. Moon depth occludes stars locally, then the pool clears depth so later world/haze behavior can distinguish untouched sky. Cube captures omit stars and omit clouds during irradiance capture. Do not assume an early generic depth prepass remains intact through sky rendering.

Modern water is double-sided post-deferred geometry, samples a scene/depth copy and exclusion mask, and does not use ordinary fixed-function alpha blending for its surface composition. The alpha-only/invisiprim compatibility path now contributes water exclusion, not the historical erasure of arbitrary world pixels. Full details and source evidence are in the [scene report](scene-submission.md).

### 4.3 Native glTF scene assets are dormant, SL PBR is active

The separate imported/native glTF scene path contains loaders, animation, node/joint/material buffers, CPU picking and pool consumers. However, [its GPU buffer upload and render-list population are compile-disabled][gltf-disabled] at this baseline. The consumers skip empty/null draw data; it is not a second fully active world-material pipeline. This does **not** disable glTF PBR materials on ordinary Second Life prims/meshes.

The companion reports document latent native glTF shader/CPU contracts for future work, but enabling that dormant path is feature restoration beyond current visual parity. Do not misclassify its unsorted alpha, repeated material-factor arithmetic or incomplete deformation/picking behavior as image requirements of the active SL PBR renderer.

## 5. Lighting, shadows and atmosphere

### 5.1 Deferred lighting is multiple policies

[Deferred lighting][lighting] first transforms sun and moon into the current view. If SSAO is enabled for a non-cube view or shadows are enabled, a fullscreen sun pass generates the visibility/light map. Two-pass light-map blur runs under the SSAO condition, not unconditionally for every shadow configuration.

Base sunlight/environment shading is gated by `RenderDeferredAtmospheric`; the name understates its role. Point and spot lighting are then additive:

- Candidates come from the managed nearby-light set, including distance/fade policy.
- `RenderLocalLightCount` caps iteration; disabled attachment lights, tiny radii, near-black colors and out-of-frustum bounds are excluded.
- Coverage radius is scaled by 1.5.
- Camera-outside volumes use box coverage; camera-inside point lights are grouped into fullscreen shader variants; projected spotlights have separate paths.
- Cubemap captures use an additional local-light scale and omit local lights at low probe detail.

A clustered light implementation could replace the box/fullscreen mechanism, but it must reproduce the selected light set, falloff, projection, space conversions and fade behavior. Do not substitute an arbitrary physical inverse-square law.

**Important opaque/forward asymmetry:** [nearby-light collection][nearby-lights] is called in post-deferred traversal, after opaque deferred local lighting. Deferred lighting therefore uses the previously maintained candidate set; forward geometry uses the subsequently updated one. [Forward light setup][forward-lights] fills at most six local slots (2 through 7) and multiplies their colors by fade. The deferred local-light loop instead reads each volume's linear color directly, without multiplying by that fade. It can process up to the larger configured count. Do not "fix" these differences by feeding all surfaces one newly computed, uniformly faded clustered-light list and call it parity. Fade increments also occur in the setup routine, which has multiple callsites, rather than a guaranteed once-per-frame state update.

### 5.2 Shadow map placement is part of appearance

[Sun/spot shadow generation][shadow-generation] chooses sun or moon, temporarily restores first-person avatar attachment visibility when required, disables main-view occlusion and builds independent caster lists.

Directional shadows are not just four fixed orthographic cascades:

- Receiver point-cloud extents determine near/far limits.
- Split exponent varies with light/camera orientation; first split receives transition padding.
- Split frusta overlap; no-receiver/disabled splits are cleared.
- The fitting code attempts a perspective-warped light projection and falls back to orthographic based on fit/error/FOV conditions.
- Cube snapshots stretch the split distances and render only two directional splits.
- Main shadow depth rendering uses `LESS` with depth clamping for directional views; texture comparison uses `LEQUAL`.
- Opaque, masked, blended-alpha, rigged and glTF casters have distinct submissions.

Evidence: [split computation][shadow-splits], [projection fit][shadow-fit], [caster rendering][shadow-casters].

Only two spot-shadow owners are retained. [Ownership transitions][spot-history] fade in/out using frame interval and camera velocity. Lighting selects target spotlight candidates after the current frame's shadow generation, so selection is temporal. A rewrite that chooses new shadow owners immediately in the same frame changes that behavior.

### 5.3 Water and haze create composition boundaries

[Post-deferred orchestration][post-deferred] inserts operations at pool-type boundaries:

- Generate water exclusion at the exclusion boundary.
- Apply water haze just before pre-water alpha.
- Apply atmosphere just before post-water alpha when above water.
- When underwater, move atmosphere before the water pass.
- Skip atmosphere/water haze on HUDs and low-detail probe captures; the haze helpers also skip impostors.

The alpha pools select/clip fragments for the appropriate side of water; this is not merely sorting every transparent draw by distance.

[Haze composition][haze] copies depth to scratch before sampling it while the scene depth remains attached. Its blend equation is `RGBout = haze.rgb + RGBdst * haze.a`; destination glow alpha is multiplied by haze alpha. This is transmittance composition, **not ordinary source-over alpha**. Above-water water haze renders water patch geometry; underwater it is fullscreen. Water exclusion supplies another input.

These are physical/logical dependencies to preserve even if Vulkan changes image layout or pass grouping.

### 5.4 Numeric shading requirements, not generic "PBR"

The following is a compact cross-pass contract; detailed equations and citations are in the [shader report](shader-contracts.md):

| Area | Observed rule that a generic replacement would miss |
|---|---|
| PBR material split | Dielectric F0 = 0.04; diffuse = baseColor * (1-F0) * (1-metallic); specular = mix(F0, baseColor, metallic) |
| Punctual PBR | GGX/Schlick/Smith-style terms; perceptual roughness floor 8/255; roughness is squared for the microfacet parameter |
| IBL | LUT uses `(NdotV, 1-roughness)`; material AO multiplies both diffuse irradiance and specular radiance contributions |
| Local attenuation | `2 * (1-clamp((distance/radius + falloff)/(1+falloff),0,1))^2`, not inverse-square |
| Local PBR gain | Deferred point/spot gain 3.25 versus forward helper gain 3.0, in addition to the CPU list/fade differences |
| Legacy lighting | Separate gloss LUT, Fresnel/geometry terms and environment mixes; sun diffuse uses `min(NdotL, shadow)` rather than their product |
| SSAO | Modulates irradiance with a color-effect transform; not a final-image multiply and not the same as material AO |
| Visibility-map channels | R directional shadow, G ambient visibility, B/A the two projected shadow slots |
| Classic sky | Changes ambient/sun transfers, light gains and probe irradiance behavior for both legacy and PBR materials |
| HDR bound | `clampHDRRange` repairs Inf/NaN and clamps to 11.2 where called; later additive passes mean 11.2 is not a global scene ceiling |
| Water | Distinct Fresnel/refraction/fog formula, special probe selection and final [0,1] clamp even in the HDR scene |
| Transparent shadows | PBR BLEND shadows use thresholds and deterministic stripe/dither, not fractional transmission |

Turning off probes does not mean black/no IBL: the lower-class helper samples the legacy environment cube. SSR participates inside reflection shading and uses prior scene/depth; it is suppressed for cube captures, and PBR has a high-gloss participation threshold. These fallback/selection rules are part of the required image.

## 6. Transparency is a family of reference behaviors

The serialized [transparency settings][alpha-settings] select:

| Mode | Reference behavior |
|---|---|
| 0, default | Sorted alpha; main post-water path can interleave world and avatar ensembles according to its additional setting and eligibility. |
| 1 | Bounded per-pixel linked-list capture/resolve, when supported and valid. |
| 2 | Bounded depth peeling, when supported and valid, with layer and CPU submission-time limits. |

These alternatives are specifically main-view post-water algorithms, not global replacements for pre-water alpha, probes, HUDs and shadows.

Ordinary alpha is also not uniformly depth-read-only: the stock rigged path writes depth, and pre-water alpha has additional write behavior. Its destination alpha blend attenuates the glow already behind it, followed by separate additive glow replay. PPLL/peeling disable shared-depth writes during capture/selection, but **only PPLL explicitly replays standard rigged depth afterward**. Successful peeling has no equivalent replay; residual custom-blend rigged draws can still write depth. Particles and custom non-source-over blends remain in a residual stock phase.

PPLL heads start at `0xFFFFFFFF`; an allocator reset precedes capture. Nodes store packed half-float color, depth and next pointer. After capture, storage writes must be visible to resolve. Resolve tests opaque depth, exact-sorts a bounded subset and blends premultiplied coverage into RGB while preserving glow alpha. It traverses at most 256 nodes, exact-sorts up to the configured limit capped at 32, and collapses remaining traversed visible fragments into an approximate coverage/transmittance tail. The exact subset is the first visible list entries, not globally nearest N. Exhausted global storage deliberately falls through to legacy source-over rendering instead of making surfaces disappear.

Depth selection uses `MAX` blending into `R32F`, cleared to zero, with ping-pong access to the previous selected depth. This is a far-to-near selection mechanism; replacing it with the usual near-to-far peel algorithm requires rederiving composition and tail behavior. After bounded exact layers it renders a legacy-ordered nearer tail. Teleports/slow frames can trigger a temporary peeling cooldown.

Defaults include 8 nodes/pixel, 512 MiB node budget, 24 exact-sort slots per pixel, 8 peel layers and 4 ms CPU submission budget. Despite its setting comment, the pixel-layer setting does not cap total captured fragments per pixel. Device/storage clamps and shader bounds further constrain effective behavior; see the [scene](scene-submission.md) and [shader](shader-contracts.md) investigations rather than treating those defaults as guarantees.

**Native implication:** retain an explicit alpha policy and its failure/overflow contract. Weighted blended OIT is not an equivalent replacement. Faster CPU submission can itself change how many peel layers fit the time budget, making deterministic comparison require controlled settings.

## 7. Postprocessing and frame history

### 7.1 Verified order

[Finalization][finalization] executes:

1. If HDR postprocessing is enabled and supported:
   - Copy current scene color/depth into SSR history when SSR is enabled.
   - Generate luminance.
   - Copy previous exposure aside and compute new exposure.
   - Tone map.
   - If CAS is enabled/complete, tone map without gamma into scratch, then CAS performs gamma conversion; otherwise tone mapping performs gamma itself.
2. Otherwise apply the gamma-only branch.
3. Extract and blur glow **from the post-tone-map/gamma image**, then combine glow.
4. Apply DoF when settings/edit-mode permit.
5. Apply FXAA, or generate/apply SMAA.
6. Run RLVa sphere effects, vignette and snapshot-frame effects when active.
7. Optionally replace output with a debug-buffer visualization.
8. Draw final screen pass adding noise, **also writing depth** with `ALWAYS`.
9. Draw snapshot guides/focus/physics overlays as enabled.
10. Compose HUD/world UI and 2D UI.

"Bloom before tone mapping", a common modern design, is not this reference order. FXAA/SMAA are not geometry MSAA. UI must not accidentally inherit exposure, DoF, AA or world fog.

Tone mapping selects Khronos Neutral or ACES Hill with a configurable mix; the presence of other utility functions does not make them active operators. Exposure samples a luminance mip rather than a histogram. FXAA preparation stores luma in alpha using the source's unusual blue coefficient 0.144. DoF instead repurposes alpha as signed circle-of-confusion encoded around 0.5. A generic persistent "scene alpha" interpretation is invalid after these boundaries.

### 7.2 Temporal dependencies to represent explicitly

| State | Producer / consumer relationship | Required treatment |
|---|---|---|
| SSR scene/depth | Read by scene shaders before finalization overwrites it; current-to-previous transforms accompany it | At least logical previous/current versions; preserve camera/projection pairing. |
| Exposure | Finalization copies retained exposure aside, computes adapted exposure, then tone mapping reads the new result | Preserve adaptation history separately from the current result. Binding an exposure sampler in a generic deferred helper does not establish an active scene-shader read. |
| Glow feeding luminance | Luminance samples retained `mGlow[1]` before this frame regenerates glow | Preserve the previous glow input, in addition to previous exposure. |
| Probe contents | Scheduled capture/filter/publication, not a complete rebuild each displayed frame | Track readiness, age, update phase and ownership. |
| Spotlight owners/fades | Current shadow generation consumes prior selected targets; lighting selects future targets | Preserve delayed assignment and transition state. |
| Nearby local lights | Opaque lighting precedes candidate-list refresh; forward slots are refreshed/fade-scaled afterward | Preserve opaque versus forward light-count, color and timing differences. |
| Occlusion | Earlier query results inform lists; new queries update future decisions | Do not block the entire frame awaiting fresh results. |
| Asset residency and LOD | Time-budgeted image/geometry work changes available representation | Golden tests must freeze or fully settle these inputs. |

[Exposure generation][exposure] uses frame interval, random sample offsets, sky HDR settings and previous exposure. [Luminance generation][luminance] binds retained `mGlow[1]` before this frame's later glow generation, and the [luminance shader][luminance-shader] actually adds that sample to scene RGB. This is an active previous-glow dependency, not an unused sampler binding and not the optional PBR emissive G-buffer attachment.

The [SSR copy][ssr-copy] occurs inside the HDR branch, not merely whenever the SSR setting is true. Allocation does not prove that the history is updated in every configuration.

## 8. Multiple view families, not one global camera

| View | Important divergence from main world |
|---|---|
| Reflection probe face | Independent cull/sort/shadows; no main-camera occlusion reads/writes; no final tone mapping/UI; quality-dependent local lights/haze. |
| Hero/mirror | Separate target pack and visibility/projection policy; rendered before ordinary main scene; mirror water-clip exemption. |
| Shadow | Depth-only/material-specific caster evaluation; own cull result and projection; no main-view occlusion. |
| Avatar impostor | Cached multi-attachment surface representation; selective geometry, alpha-depth replay and generated coverage mask. |
| Preview/profile | Different avatar/attachment selection, no ordinary impostor update semantics. |
| HUD attachments | Separate orthographic-like matrices and cull; post-deferred-only rendering after main postprocessing; no main occlusion or atmosphere. |
| Snapshot | Tile/zoom and UI inclusion controls; global versus local snapshot flags; snapshot-specific effect scaling and readback. |
| Startup/disconnected | UI/background path without normal main-world rendering. |

[Cube display][cube-display] does environment, cull, shadows, sorting, G-buffer and deferred lighting, but deliberately does not call finalization. [HUD attachment rendering][hud] creates its own camera, masks world geometry, disables occlusion and invokes only post-deferred traversal. [Impostor generation][impostor] stores G-buffer attachments, not merely a finished RGBA billboard: ordinary generation renders geometry, replays post-deferred depth without altering color, then creates a coverage mask from depth. Its dimensions are view-dependent powers of two capped at 512.

Native view contexts should replace shared mutable `mRT`, global camera ID, `sCull`, matrix stacks and render flags. View contexts must specify representation/LOD policy, clipping, light/probe inputs, history access and output encoding. Sharing immutable meshes/materials is appropriate; sharing main-camera visibility/history indiscriminately is not.

### 8.1 Probe updates are persistent transactions

Ordinary probes normally capture one face per manager update: six faces to generate irradiance, then another six to generate radiance. Filtering occurs at the six-face boundary; already-complete probes retain usable resident products while scratch faces refresh. Realtime dynamic probes add a separate six-face update whose irradiance/radiance phase alternates. Default probe slot zero is an environment fallback, not an ordinary nearby object.

Hero mirrors use a reflected-eye cubemap, not a single planar reflection texture. Their update-rate setting staggers face refreshes and regenerates radiance from a mixture of new and retained faces; this is not the ordinary twelve-face transaction.

GPU storage identity and per-view shader identity differ: a resident cube index persists while packed probe indices, depth buckets, neighbor lists and box transforms are rebuilt for the camera. A Vulkan descriptor table must not confuse these identities. Capture freezes environment animation time but still rebuilds view-dependent environment uniforms.

### 8.2 Picking and screenshots are different systems

Normal picks use CPU ray/geometry intersections with HUD priority, transparency/rigging selectors and queued input callbacks. They do not require a GPU object-ID pass. Occlusion queries and selection-overlay rendering are separate concerns.

Screenshots instead rerender selected view(s), can resize targets or tile/crop, then read back color/depth while suppressing presentation. World labels, 3D HUD attachments and the widget tree use different depth and viewport rules. In particular, UI-like glyph drawing does not mean world labels ignore scene depth.

## 9. Proposed native Vulkan architecture

This section is a design derived from the evidence, **not a claim that the baseline already implements it**.

### 9.1 Backend-neutral renderer inputs

Produce an immutable frame snapshot containing:

- Region-relative transforms and origin epoch.
- Mesh handles, selected LOD, vertex layout, index ranges and geometry versions.
- Instance data and skin palette handles/versions.
- Material model, alpha policy/cutoff, texture handles/transforms, sampler semantics, double-sidedness, emissive/glow and compatibility flags.
- Light candidates and their current fade/selection state.
- Environment state plus published reflection/irradiance products.
- Per-view camera/projection/viewport, clip planes, render inclusion policy and temporal references.
- Explicit HUD/UI draw data and dynamic texture/bake outputs.

Do not make Vulkan understand GL texture names, ambient global `gGL` state or a draw callback that changes unrelated viewer state during command recording.

### 9.2 Separate policy from mechanism

| Keep as behavior/policy | Replace as execution mechanism |
|---|---|
| Asset selection, LOD thresholds and fallback texture rules | GL buffer pools, immediate-mode emulation and object names |
| Material classification and compatibility math | Runtime GL shader binding and implicit uniform updates |
| Water/haze/alpha ordering, glow semantics | Mutable blend/color/depth state leaking between pools |
| Shadow receiver fitting and light/probe selection | FBO binding order and texture-unit bindings |
| Probe cadence and history validity | Shared global camera/target switches |
| HUD/UI order, clipping and text sampling | GL matrix stacks and UI emitters |

Opaque draws can use pipeline/material sorting and instancing/indirect submission where equivalence is demonstrated. Transparent draws retain semantic ordering or the selected OIT algorithm. Clustered lighting, compute skinning, HZB culling and parallel probe rendering are later optimizations, not prerequisites for a correct first native implementation.

### 9.3 Suggested logical graph and shader ABI

Use typed logical resources: `SurfaceBase`, `SurfaceParameters`, `NormalAndClass`, `Emissive`, `SceneDepth`, `LightVisibility`, `SceneRadiance`, `GlowMask`, `WaterMask`, `SSRHistory`, `ExposureHistory`, `ProbeProducts`, `DisplayColor`, `DisplayDepth`.

Initially retaining reference G-buffer encodings minimizes simultaneous changes. Splitting glow into a separate image or changing packing is possible only if every producer, blend equation and consumer is updated together and precision differences are accepted/tested.

Group descriptors by stable lifetime, for example frame/view, material textures, instance/skin data and pass-local images. Use explicit variant keys for material model, alpha mode, skinning, double-sidedness, view family and quality/compatibility features. Reflect/validate block layouts rather than copying OpenGL uniform locations or assuming existing C++ packing is the ABI.

The actual GL shader input is assembled from source files, C++-injected declarations/macros, linked feature objects and permutations. The injected material categories are floating values `0.0`, `0.34`, `0.67`, `1.0`, tested within `0.1`; despite their names they are not bitwise-composable flags. Shader class fallback can load a lower-class file while retaining the requested logical class. Porting individual raw GLSL files therefore misses compilation inputs and can choose the wrong algorithm.

Likewise, the native UI's runtime SPIR-V files are checked in and loaded from packaged compiled shaders; changing a GLSL source alone does not establish that its runtime binary changed. A native world-shader build needs reproducible compilation, variant manifests and ABI validation.

### 9.4 Synchronization and lifetime requirements

| Dependency | Vulkan-side responsibility |
|---|---|
| Texture/mesh upload -> shader/vertex/index reads | Transfer completion, memory visibility, layout transition and queue ownership when needed |
| G-buffer/depth writes -> deferred sampling | Correct attachment-write to sampled-read dependencies; avoid illegal sampled-depth/write feedback |
| Lighting -> water's sampled scene copy -> water blend | Explicit image version/copy; never sample an attachment being overwritten without a deliberately supported feedback design |
| Haze depth sampling while scene depth is attached | Read-only depth arrangement or explicit depth snapshot matching the source's semantics |
| PPLL initialization -> capture -> resolve | Transfer/storage/atomic visibility and buffer lifetime; Vulkan atomics in storage replace the GL atomic-counter binding model |
| Probe face capture -> mip/filter -> published sampling | Per-face/per-mip dependencies and publication version |
| Scene/exposure history read -> replacement | Separate versions or ordered access; no premature alias/recycle across in-flight frames |
| Final display -> HUD/world overlays | Preserve intended display depth and independent HUD depth policy |
| Readback/capture -> CPU consumption | Completion tracking and explicit row/orientation/format handling |
| Resize/resource replacement | Retire old resources only after every referencing submission completes |

Start with one graphics queue and a simple, auditable frame graph. Add transfer/compute concurrency after parity; GL's command order currently supplies many dependencies implicitly.

### 9.5 Coordinate and color conventions

Document one explicit boundary for:

- Agent/world-relative to view space and origin rebasing.
- GL-style clip depth to Vulkan clip depth; framebuffer Y orientation.
- Winding/culling under reflection and viewport inversion.
- Depth reconstruction, SSR reprojection, shadow comparison and HUD/snapshot projection.
- Texture decode, material arithmetic, encoded G-buffer values, linear scene lighting, display-space postprocessing and swapchain transfer function.

Do not independently flip Y or gamma in every shader. Do not introduce reverse-Z at the same time as the first parity implementation: it changes all depth comparisons, reconstruction, shadow bias, peel order and readbacks and should be validated as a separate change.

## 10. Parity requirements and verification plan

### 10.1 Acceptance contracts

| ID | Must remain equivalent |
|---|---|
| R01 | Selected geometry, visibility, material routing and alpha cutoff for each view |
| R02 | Legacy/PBR material interpretation, texture color spaces, tangent basis and skinning |
| R03 | G-buffer fields, discriminator/flag values and effective precision |
| R04 | Light selection/fades, direct/IBL equations and shadow fitting/filtering |
| R05 | Water exclusion, clipping, above/below-water haze and transparency order |
| R06 | Sorted/interleaved/PPLL/peel behavior, bounds and fallback semantics |
| R07 | Glow alpha, exposure history, SSR history and exact postprocess order |
| R08 | Probe/hero update cadence, filtered products and publication readiness |
| R09 | Main/probe/shadow/impostor/HUD/snapshot camera and history isolation |
| R10 | UI/HUD placement, clips, alpha, fonts, media, overlays and capture orientation |
| R11 | Resize, minimized/startup/disconnected transitions and in-flight resource safety |

### 10.2 Capture strategy

First establish a deterministic GL reference at the pinned revision, with fixed assets, camera, environment, settings, viewport, time/animation and random seeds. Wait for streaming, probe convergence, exposure settling and geometry rebuilds. Record enabled shader variants and actual target sizes/formats rather than only a graphics-preset name.

Then compare stages, not only final screenshots:

1. Visible instance/material/pass inventory.
2. Opaque depth and each G-buffer channel.
3. Shadow maps and visibility/SSAO map.
4. Base lighting and local-light accumulation.
5. Probe radiance/irradiance products by face and mip.
6. Before/after water, haze, each alpha boundary and OIT resolve.
7. Scene RGB and glow alpha.
8. Luminance and exposure over a frame sequence.
9. Tone-map/CAS, glow, DoF, AA and final world output.
10. HUD/UI composite and readback.

Use exact categorical checks for material flags, inclusion sets, pass order and resource formats. Establish numeric tolerances for decoded depth/normal/radiance and image differences from repeated GL captures before setting acceptance thresholds; no numeric threshold has been measured in this investigation. Cross-driver pixel equality is not assumed.

### 10.3 Minimum scene/transition matrix

- Legacy diffuse, fullbright, shiny, normal/specular materials alongside glTF opaque/masked/blended, emissive and double-sided surfaces.
- Rigged bodies/hair plus unrigged attachments and world alpha at intersecting depths; muted/silhouette/impostor avatar states.
- Alpha mode 0 with interleaving on/off; modes 1/2; intentionally saturated PPLL storage and limited peel budget.
- Above-water, underwater and camera-crossing-water cases; intersecting water/exclusion volumes and transparency on both sides.
- Day/night, legacy sky adjustment, HDR on/off, emissive target on/off, dynamic exposure transitions.
- Shadow off/sun/sun+spot; moving camera, changing spotlight owners, first-person attachments and empty receiver splits.
- Probes disabled/low/high detail; new/dirty/moving probes, mirror views, camera entering/leaving influence volumes.
- SSR moving camera and resize/history reset; DoF edit-mode gates; FXAA/SMAA/no AA; glow/CAS off/on.
- HUD zoom/clipping, world labels, particles, UI scale, media textures, startup/disconnected/minimized/restored windows.
- High-resolution tiled snapshots, UI excluded/included, depth readback, dynamic texture previews and avatar bakes.

### 10.4 Recommended implementation sequence

1. Add deterministic stage capture and frame/view/material inventories without changing GL behavior.
2. Define backend-neutral scene/material/view packets and asset handles.
3. Implement native uploads, opaque/masked geometry, G-buffer and depth parity.
4. Implement legacy/PBR lighting, shadows and environment/probe products.
5. Implement ordered water/haze and reference sorted/interleaved transparency.
6. Implement HDR/history/postprocessing, then remaining transparency modes.
7. Complete auxiliary views, impostors, HUD/UI, snapshots and transition behavior.
8. Optimize only after per-stage and temporal parity tests pass.

Features not yet implemented should be explicitly unavailable rather than silently taking a visually unrelated path.

## 11. Verification status and limits

This report recovers the governing contracts and execution structure from the pinned source. It does not demonstrate a working Vulkan world renderer, validate GPU-driver behavior, measure performance, or certify visual equality.

No build/test run is needed to validate unchanged source as a documentation deliverable. Source-location and artifact integrity checks accompany the report; actual visual verification requires running the viewer with representative assets and capturing intermediate GPU outputs.

Delivery checks passed: all five reports exist and name the pinned revision; companion links resolve; all 46 primary source anchors point to existing files and valid line numbers; no unresolved primary reference definitions were found. Final `HEAD` and `origin/master` still match the pinned commit, `git diff origin/master` is empty, and the repository worktree remains clean. These integrity checks do not substitute for semantic review or runtime image comparison.

Important open measurements include: material precision sensitivity, depth/shadow bias equivalence across APIs, stochastic exposure/noise baselines, probe convergence timing, alpha overflow appearance, driver-specific texture/filter behavior and the full set of dynamic UI widget paths.

### Defined quirks versus undefined or dormant code

Preserve source-defined visual quirks as the initial reference, then change them deliberately. Do not promise to reproduce undefined values: the normal decoder leaves W unwritten, while a standalone SSR shader reads it as a tag; the primary G-buffer accessor reads the raw tag correctly. DoF's extra alpha traversal has explicit color suppression, but its inherited depth writes and per-material cutoff are not proven by the nearby depth-only comment. Hero filtered versus advertised mip counts also need runtime coverage. Dormant native glTF arithmetic and disabled draw-data production are enabling-time concerns, not active world-image parity requirements.

The implementation goal is now expressible as **matching the renderer's scene decisions, view policies, shading contracts and temporal composition**, not matching its OpenGL call trace.

[display]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llviewerdisplay.cpp#L1457
[display-prep]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llviewerdisplay.cpp#L1818
[display-cull]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llviewerdisplay.cpp#L1897
[display-draw]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llviewerdisplay.cpp#L2094
[ui]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llviewerdisplay.cpp#L2609
[target-pack]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.h#L740
[attachments]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L381
[allocation]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L909
[shadow-allocation]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L1107
[glow-allocation]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L1473
[oit]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L8049
[deferred-bind]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L9542
[cull]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L2660
[state-sort]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L3317
[post-sort]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L3794
[occlusion]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L2810
[deferred-pools]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L4276
[lighting]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L9778
[shadow-generation]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L11223
[shadow-splits]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L11395
[shadow-fit]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L11562
[shadow-casters]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L10790
[spot-history]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L11826
[post-deferred]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L4415
[haze]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L10276
[alpha-settings]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/app_settings/settings.xml#L11862
[finalization]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L9295
[exposure]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L7800
[luminance]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L7753
[ssr-copy]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L8012
[cube-display]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llviewerdisplay.cpp#L2283
[hud]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llviewerdisplay.cpp#L2395
[impostor]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L12108
[app-display]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llappviewer.cpp#L1785
[nearby-lights]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L6225
[forward-lights]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L6500
[texture-updates]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llviewertexturelist.cpp#L840
[texture-publish]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llviewertexture.cpp#L1623
[texture-create-queue]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/llviewertexturelist.cpp#L1117
[pool-selection]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/pipeline.cpp#L1952
[luminance-shader]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/app_settings/shaders/class1/deferred/luminanceF.glsl#L63
[gbuffer-access]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/app_settings/shaders/class1/deferred/gbufferUtil.glsl#L26
[normal-pack]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/app_settings/shaders/class1/deferred/globalF.glsl#L46
[pbr-writer]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/app_settings/shaders/class1/deferred/pbropaqueF.glsl#L68
[base-light]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/app_settings/shaders/class3/deferred/softenLightF.glsl#L167
[gltf-disabled]: https://github.com/anne-skydancer/vulkanstorm/blob/59108e15a1f8f94d2da7c674d937d19f5cf9450d/indra/newview/gltf/asset.cpp#L623
