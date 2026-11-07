<!-- SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later -->
# AMD FidelityFX Super Resolution (FSR 3.1) in the future Vulkan viewer: feasibility study

Status: study, 2026-11-07. Branch `codex/fsr-study` from `vkstorm-devel` (b2ffddcf48). No viewer
code changes. Nothing was measured on a GPU: every cost below is either AMD's published figure or
arithmetic on it.

Scope, as decided by the owner:

- FSR 3.1 belongs in the **Vulkan viewer** that the Vulkan line will build after the Rust port
  (`doc/vulkan/direction-2026-10-29.md`).
- **OpenGL is out of scope**, because FSR 3.1 has no OpenGL backend (§1).
- The OpenGL renderer is used here only as the logic baseline: which passes exist, and where motion
  and jitter would have to be produced.
- FSR 1 and FSR 4 appear only as short comparisons (§9).

Labels:

- **[verified]**: read in this tree at b2ffddcf48, or in the cited source.
- **[source]**: stated by AMD or another cited page. It was read through a fetch tool, so it is
  second-hand wording.
- **[inferred]**: reasoned from the above, not tested.
- **[unverified]**: not confirmed; it has to be checked before a design depends on it.

Line numbers refer to `vkstorm-devel` b2ffddcf48.

## 1. Can FSR be used in OpenGL? (short answer)

| Question | Answer | Basis |
|---|---|---|
| FSR 3.1 on OpenGL, officially | **No.** | The ffx-api has DX12 and Vulkan backends only. The SDK 1.1 notes say "only the native DirectX 12 and Vulkan backends are available through this API", and SDK 2.4.0 ships `_dx12` and `_vk` DLLs only [source: GPUOpen SDK 1.1 notes, SDK 2.4.0 README and `Kits/FidelityFX/signedbin`]. |
| FSR 3.1 on OpenGL, unofficially | **No known working route found.** Two routes are possible in principle: GL–Vulkan interop (`EXT_external_objects`; Mesa radeonsi, iris and zink implement it [source]; Windows driver support [unverified]), or a port of the MIT FSR 3.1 shaders, which SDK 1.1.4 also publishes as Vulkan-flavoured GLSL, to GL 4.5 compute. Either is feasible in principle; neither was sized, as decided. | The search was not exhaustive. |
| FSR 1 on OpenGL | **Yes.** | `ffx_fsr1.h` and `ffx_a.h` support GLSL through `A_GLSL` and are MIT-licensed [source: GPUOpen-Effects/FidelityFX-FSR]. The viewer already ships AMD's portability header ("[A] SHADER PORTABILITY 1.20210629", from the FSR sample) inside `CASF.glsl` (`indra/newview/app_settings/shaders/class1/deferred/CASF.glsl:46-71`) [verified]. |

## 2. Recommendation

1. **Plan FSR 3.1 as the Vulkan viewer's temporal upscaler and antialiaser.** Design its
   prerequisites into the new renderer from the start, and turn the upscaler on in stages (§10).
   - The prerequisites are motion vectors for everything that moves (including skinned avatars),
     a jittered projection, reactive and transparency masks, a render-resolution/display-resolution
     split of the post chain, and the exposure.
   - They are most of the work: about 25 to 40 of the 35 to 60 days (§8).
   - They pay off beyond FSR. The same motion vectors let the stochastic transparency's temporal
     pass reproject moving hair, which it cannot do today (§5.4).

2. **Build FSR 3.1 from AMD's MIT source; do not ship the signed SDK 2.x DLLs.**
   - Linking the SDK 2.x DLLs fails this project's licence rule (§7).
   - They also offer nothing for Linux: SDK 2.4.0 ships only Windows `.dll` files [source].
   - The source route is FidelityFX SDK **v1.1.4**, `sdk/` under MIT. It has the FSR 3.1 upscaler
     shaders (`sdk/include/FidelityFX/gpu/fsr3upscaler/`, with GLSL and HLSL callback headers) and
     the Vulkan backend source (`sdk/src/backends/vk`) [source].
   - The cost of this route: we stay on the 3.1.3/3.1.4 code unless AMD publishes the 3.1.5 and
     3.1.6 fixes as source. The driver's FSR 4 override is also lost, since it needs AMD's signed
     DLL [inferred]. Both are acceptable.

3. **Integrate through the Vulkan backend on the raw `VkDevice`.**
   - With wgpu (favoured), access goes through wgpu-hal: `Device::as_hal::<Vulkan>`, the raw image
     handles, and the command buffer.
   - The fallback, if hal interop proves brittle, is a small wgpu-native FFX backend that runs the
     FSR shaders as wgpu compute passes.
   - A raw-Vulkan renderer would need neither and is the simplest host (§6).

4. **Frame generation: not now.**
   - Assess it as a separate decision once the upscaler ships (§4).
   - It is the only FSR part that helps a CPU-bound SL frame.
   - But it wants a base of about 60 fps that SL often lacks, adds about one base frame of latency,
     and must own the swapchain. That fits a wgpu renderer poorly.

5. **Expected benefit is conditional (§3).**
   - In GPU-bound views (1440p/4K, shadows, PCSS, local lights, SSR, water, stochastic
     transparency), the Quality mode is expected to give about 1.3 to 1.6x the frame rate.
   - In CPU- or draw-call-bound views (crowded clubs, shopping events), the gain is about zero: the
     upscaler then costs 0.3 to 1.2 ms for nothing.
   - The setting has to be the user's choice, defaulting to off. Native AA (1.0x) is offered as an
     antialiasing mode in its own right.

6. **Optional, owner's call:** FSR 1 on today's OpenGL viewer, as an opt-in improvement of the
   existing `RenderResolutionMultiplier` path, which currently upscales bilinearly. It is small
   (3 to 5 days) and independent of the Vulkan line (§9.1). It is not part of the FSR 3.1 plan.

## 3. Performance expectations for SL

### 3.1 What scales with render resolution, and what does not

Pixel count at the FSR 3.1 modes [source: the FSR 3.1.6 manual table, §5.1]:

| Mode | Scale per axis | Pixels shaded |
|---|---|---|
| Native AA | 1.0 | 100 % |
| Quality | 1.5 | 44 % |
| Balanced | 1.7 | 35 % |
| Performance | 2.0 | 25 % |
| Ultra Performance | 3.0 | 11 % |

Upscaling reduces only the **per-pixel** work. In the OpenGL frame (logic baseline) that is:

- the G-buffer fill (fragment part);
- sun/SSAO and the light map;
- soften and local lights (`renderDeferredLighting`, `pipeline.cpp:9796`);
- SSR, haze, water;
- the stochastic transparency's per-sample passes;
- tonemap and post.

The bench in the MSAA study measured the fill and the lighting stand-ins at 0.4 to 0.5 ms each at
1440p on light scenes (`doc/msaa/deferred-msaa-study.md` §5). In heavy real scenes these costs are
several milliseconds.

It does **not** reduce:

- CPU work: culling, the object and avatar update, the draw-call submission, texture decode;
- vertex work: avatars with 0.5 to 1 M triangles of mesh body, hair and clothes, and their skinning;
- the sun and spot **shadow maps**, which are light-space and independent of screen resolution;
- the reflection probe captures (`gCubeSnapshot`, their own resolution).

### 3.2 Model (arithmetic on AMD's figures, not a measurement)

The FSR 2.2 pass cost from AMD's README, DX12 [source]:

| GPU | 1080p Quality | 1440p Quality | 4K Quality |
|---|---|---|---|
| RX 6800 XT | 0.3 ms | 0.5 ms | 1.2 ms |
| RX 5700 XT | 0.6 ms | 1.1 ms | 2.4 ms |
| RX 7900 XTX | 0.2 ms | 0.3 ms | 0.7 ms |

FSR 3.1 is described as slightly slower than 3.0 [source]. No ms table is published for 3.1.6;
assume the FSR 2.2 figures plus about 10 % [inferred].

Let G be the GPU frame time, P the part of it proportional to pixels, U the upscaler cost, M the
motion-vector and mask cost (estimated 0.1 to 0.3 ms at 1440p [inferred]), and s the scale. Then:

- the new GPU time is G − P·(1 − 1/s²) + U + M;
- the frame time is max(CPU, GPU).

| Scene (illustrative) | CPU | GPU native | P | Quality, GPU | Frame rate |
|---|---|---|---|---|---|
| Open sim, 4K, shadows + PCSS + SSR, RX 6800 XT | 9 ms | 25 ms | 16 ms | 25 − 8.9 + 1.3 + 0.3 = 17.7 ms | 40 → 56 fps (1.4x) |
| Same scene at 1440p | 9 ms | 14 ms | 8 ms | 14 − 4.4 + 0.6 + 0.2 = 10.4 ms | 71 → 96 fps (1.35x) |
| Club, 40 avatars, 1440p | 30 ms | 18 ms | 7 ms | 14.4 ms | 33 → 33 fps (no gain) |
| iGPU laptop, 1080p, shadows off | 14 ms | 30 ms | 22 ms | 30 − 12.2 + 1.5 + 0.4 = 19.7 ms | 33 → 51 fps (1.5x) [cost on Intel unverified] |

So upscaling helps exactly where the GPU is the limit, and most at high display resolution and on
weaker GPUs. In SL that is typically:

- outdoor and landscape views;
- photography setups with everything on;
- 4K monitors;
- integrated GPUs.

It does nothing for the crowded, CPU-bound scenes where SL is slowest. The viewer's frame-time
statistics should tell the user which case they are in [inferred].

### 3.3 Memory

FSR 3.1.6 working set on an RX 9070 XT, DX12, from AMD [source]:

| Output | Quality | Balanced | Performance | Ultra Performance |
|---|---|---|---|---|
| 1080p | 75 MB | 65 MB | 61 MB | 45 MB |
| 1440p | 134 MB | 118 MB | 101 MB | 58 MB |
| 4K | 292 MB | 256 MB | 226 MB | 176 MB |

The added renderer targets are:

- a motion-vector target (RG16F: 4 B per render pixel, 6.5 MB at a 2560 x 1440 output in Quality,
  which renders 1707 x 960);
- the two R8 masks;
- the display-resolution post targets.

The render-resolution targets shrink by the same 1/s². At Quality the saving on the G-buffer, scene
and stochastic transparency targets roughly offsets FSR's working set [inferred].

### 3.4 Quality risks with SL content

| Content | Risk | Mitigation |
|---|---|---|
| Alpha-blended hair, eyelashes | ghosting, smearing (no reliable motion vectors) | transparency and composition mask, plus reactive by alpha; skinned motion vectors for the masked parts |
| Alpha-masked foliage and hair | shimmer is reduced (FSR's strength) | negative mip bias (§5.2) |
| Text on prims, signs | blur if textures stream lower or the mip bias is missing | mip bias; texture-streaming priority taken at display resolution (§5.2) |
| Media on a prim (video, web), animated textures (`llSetTextureAnim`) | smearing; the content changes without geometric motion | reactive mask for faces with texture animation or media |
| Particles (CPU billboards, `LLVOPartGroup::getGeometry`, `llvopartgroup.cpp:460`) | trails | reactive by alpha; no motion vectors in stage 1 |
| Flexi prims (CPU-deformed, `LLVolumeImplFlexible::doFlexibleUpdate`, `llflexibleobject.cpp:429`) | slight ghosting | previous vertex positions, or reactive |
| Water (animated normals, refraction) | swimming detail | camera motion vectors from depth; FSR 3.1 shading-change detection; a mild reactive value [inferred] |
| Teleport, region crossing, camera snap, snapshot | history from another view | `reset` on each, which is a cheap signal the viewer already has |
| HUD attachments, UI, name tags, hover text | none | drawn after the upscale at display resolution, unjittered (today's order, §5.5) |

## 4. Frame generation (FSR 3.1 frame interpolation), assessed separately

What it is [source: FSR 3.1 release post; frame-interpolation manual]:

- Since 3.1 it is separate from the upscaler.
- A "prepare" dispatch takes depth, motion vectors, jitter, the motion-vector scale, the frame time
  delta, camera near/far, the vertical FOV and camera information.
- The interpolation itself runs in a proxy swapchain (`ffxCreateContextDescFrameGeneration`,
  `ffxDispatchDescFrameGenerationPrepare`, `ffxConfigureDescFrameGeneration`) that presents the real
  and the generated frames.
- The UI is handled through a present callback (preferred), a UI texture, or a HUD-less colour.
- AMD recommends a base of about 60 fps and VRR (FreeSync).
- The Vulkan swapchain variant "differs from DX12 and needs extra data from the game" [source]. The
  exact queue and extension requirements are [unverified].
- The driver-paced FSR Frame Generation (Swapchain) 4.0.0 needs RX 9000 [source].

For SL:

- **Pro.** It is the only FSR component that raises the displayed frame rate when the CPU is the
  limit, which is SL's common case.
- **Con: low base frame rates.** Crowded SL scenes run at 25 to 40 fps, well under the recommended
  base. Interpolation errors are larger and shown longer, and the added latency is about one base
  frame: about 33 ms at 30 fps [inferred, standard for interpolation].
- **Con: input.** Camera orbiting, building and typing in-world are latency-sensitive, so it must
  turn off in build mode and in mouselook [inferred].
- **Con: the UI.** SL's UI is large and changes constantly: chat, floaters, name tags, the
  mini-map. It must be drawn into a separate target or through the present callback. Today it is
  drawn straight onto the back buffer after `renderFinalize` (`llviewerdisplay.cpp:1640-1660`)
  [verified]. The Vulkan renderer should keep the UI in its own pass anyway.
- **Con: the swapchain.** The proxy swapchain must own the `VkSwapchainKHR`. wgpu owns its surface
  and swapchain, so frame generation under wgpu means a raw-Vulkan present path beside wgpu: the
  hardest integration point in the whole study [inferred].
- **Con: Linux.** Wayland and X11 swapchain behaviour under the proxy is [unverified]. AMD ships no
  Linux binaries.

Verdict: do not plan it with the upscaler. Revisit after stage 4 (§10) with measured base frame
rates. If adopted, it is user opt-in only, off in build mode and mouselook, and its own decision.
Estimate: 15 to 25 days on top of the upscaler, plus the raw present path [inferred].

## 5. Prerequisites to design into the Vulkan renderer

The OpenGL renderer is the logic baseline throughout: it shows where each input would come from.

### 5.1 FSR 3.1 inputs [source: the FSR 3.1.6 upscaler manual]

**Colour.** Render resolution, linear. Use `FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE` for HDR input.
Placement: after the opaque and transparent scene, before the tonemap.

**Depth.**

- Render resolution, single float. D32S8 is read as R32F.
- Flags: `FFX_UPSCALE_ENABLE_DEPTH_INVERTED` and `..._DEPTH_INFINITE`.
- The new renderer should use reversed-Z with an infinite far plane: better precision over SL's
  draw distances, and FSR supports it directly [inferred].

**Motion vectors.**

- Two floats per pixel, at render resolution by default.
- In pixels, current position to previous position. `motionVectorScale` converts NDC vectors.
- Without jitter, unless `FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION` is set.
- Every input except the motion vectors is jittered.

**Jitter.**

- Halton(2,3) offsets in pixels, from `ffxQueryDescUpscaleGetJitterOffset`.
- The phase count comes from `QueryDescUpscaleGetJitterPhaseCount`: 18, 23, 32 and 72 for Quality,
  Balanced, Performance and Ultra Performance. In general it is ceil(8·s²).
- Projection offset: x = 2·jx / renderW, y = −2·jy / renderH.

**Exposure.** A 1x1 texture, or `FFX_UPSCALE_ENABLE_AUTO_EXPOSURE`, plus a pre-exposure value.

**Masks (optional, R8).**

- A reactive mask (about 0.9 at most). A helper, `ffxDispatchDescUpscaleGenerateReactiveMask`,
  derives one from the colour before and after transparents.
- A transparency and composition mask.

**Per-frame fields.** `reset`, `frameTimeDelta` (ms), the camera near/far and vertical FOV
[near/far/FOV on the upscaler dispatch: unverified], and the RCAS sharpness [unverified on the
3.1.6 page].

**Modes and mip bias.**

| Mode | Scale | Mip bias |
|---|---|---|
| Quality | 1.5 | −1.58 |
| Balanced | 1.7 | −1.76 |
| Performance | 2.0 | −2.0 |
| Ultra Performance | 3.0 | −2.58 |

In general the bias is log2(render / display) − 1.

**Hardware.** Typed UAV loads and `R16G16B16A16_UNORM` [source]. The Vulkan device features and
extensions (fp16, subgroup operations) are [unverified]; they must be checked in the stage 1 spike.

### 5.2 Render scale and jittered projection

**Baseline [verified].**

- There are two render-scale debug settings:
  - `RenderResolutionDivisor` (U32, default 1, `settings.xml:13222`);
  - `RenderResolutionMultiplier` (F32, default 1.0, `settings.xml:13235`; a Catznip patch).
- They are applied in `resizeScreenTexture` (`pipeline.cpp:825-859`) and in the allocation
  (`pipeline.cpp:951-975`). All scene and post targets are then allocated at the reduced size,
  including:
  - the G-buffer and `screen` (`pipeline.cpp:983-989`);
  - the post ping-pong maps (`:1062-1063`);
  - FXAA/SMAA (`:1035-1040`).
- The present pass then draws the reduced `sourceBuffer` over the whole world view with a
  full-screen triangle (`pipeline.cpp:9412-9423`). That is a bilinear upscale *after* every post
  effect.
- Neither setting is in Preferences. Only `RenderCASSharpness` and `RenderFSAAType` are (in
  `panel_preferences_graphics1.xml:1050` and `:1674`).

**Vulkan renderer design.**

- Keep two explicit sizes, render and display. Each target belongs to one of them (§5.5).
- One scale source is fed by the upscaler mode. A custom ratio is kept as a debug setting, taking
  over `RenderResolutionMultiplier`.

**Jitter.**

- The jitter goes into the main camera's projection only. Today's equivalent of that hook is the
  sub-pixel tiling translation that high-resolution snapshots already apply to the projection
  (`llviewercamera.cpp:364-385`) [verified].
- Unjittered copies must be kept for:
  - frustum culling and occlusion;
  - the sun shadow cascade fit, which is fitted to the camera frustum, so jitter would make the
    cascades shimmer [inferred];
  - picking and the UI's 3D projections (`LLViewerCamera::projectPosAgentToScreen`);
  - the reflection-probe and hero-probe captures, which must never be jittered;
  - the motion vectors.

**Mip bias.**

- The bias goes on the scene's material samplers only: not on UI, shadow-map or LUT samplers.
- **SL-specific:** texture streaming chooses the discard level from the on-screen pixel area. That
  area must be computed at **display** resolution. Otherwise a lower render resolution would also
  fetch lower-resolution textures, and text on prims would blur twice [inferred; to be checked in
  `LLViewerTextureList` priority code when the line opens].

### 5.3 Motion vectors

**Baseline [verified].** The OpenGL renderer has no motion-vector target. The G-buffer is diffuse,
ORM, normal and an optional emissive (`addDeferredAttachments`, `pipeline.cpp:383-414`). Its only
motion knowledge is camera-only:

- `gGLLastModelView` and `gGLLastProjection`, saved at the end of the 3D scene
  (`pipeline.cpp:10308-10317`);
- `gGLDeltaModelView`, built at the start of `renderGeomDeferred` (`pipeline.cpp:4257-4271`) and
  used by SSR (`screenSpaceReflUtil.glsl:34-35`, `:82-83`).

On open PR #349, the stochastic transparency's temporal accumulation (`alphaStochasticResolveF.glsl`
on `codex/stoch-temporal-2`) reprojects by camera motion alone. It documents ghosting on moving hair
(rmse 10.9 to 15.1 against 4.7 to 5.4 without history) [verified, from that branch's doc].

What each content class needs, sized for a renderer designed for it:

| Content | Motion source | Work [inferred] |
|---|---|---|
| Static prims, terrain, linksets at rest | camera only: a full-screen pass from depth and the previous unjittered view-projection (about 0.1 ms at 1440p). Pixels the geometry pass did not write fall back to it. | 1 to 2 days |
| Sky, clouds, sun/moon, stars (depth at far) | camera rotation only, from the same pass; cloud drift is slow enough to ignore | included |
| Water plane | camera from depth; animated normals and refraction handled by FSR's shading-change detection, plus a small reactive value | 0.5 day |
| Moving prims (physics, keyframed motion, `llTargetOmega` spin, interpolated object updates), attachments on moving avatars | the previous model matrix per draw (or per instance), kept beside the current one; the G-buffer pass writes the screen-space difference into an RG16F attachment | 3 to 5 days |
| Rigged mesh, avatars, animesh (GPU skinning: `matrixPalette`, uploaded per draw by `LLRenderPass::uploadMatrixPalette`, `lldrawpool.cpp:651-722`) | the previous joint palette per skinned draw. **Recommended for the Vulkan renderer:** skin in a compute pass once per frame into a position buffer and keep the last frame's buffer. Motion vectors are then a subtraction, and the shadow and depth passes reuse the skinned positions instead of skinning again. | 5 to 10 days (compute skinning is a renderer decision in its own right) |
| Flexi prims | the previous CPU vertex buffer (double-buffered), or reactive | 1 to 2 days |
| Particles, avatar impostors | none; reactive by alpha (impostors pop on refresh: `reset` is too strong, so reactive) | in the masks |
| Animated textures, media faces | none; reactive or transparency and composition mask per face | in the masks |
| Alpha (sorted, stochastic transparency) | the opaque surface's motion stays in the target; the transparent layers are marked in the masks; see §5.4 | in the masks |
| HUD attachments, UI | none: drawn after the upscale | 0 |

Cost when on: one RG16F attachment in the geometry pass (+4 B per render pixel of bandwidth), the
previous matrices or positions, and one camera-motion pass. With compute skinning, avatar-heavy
scenes may even save vertex work, because the shadow passes stop skinning separately [inferred].

### 5.4 Reactive and transparency/composition masks; stochastic transparency

Both masks are R8 at render resolution. They are written by:

- sorted alpha and particles: reactive = their alpha, scaled; this is AMD's guidance for alpha
  without motion vectors;
- glow and emissive fullbright (glow sources): a small reactive value, so bloom sources do not
  lag;
- texture-animated and media faces: transparency and composition = 1;
- the stochastic transparency: discussed below.

The masks can be produced by the passes themselves (an extra R8 output) or, as a first cut, by AMD's
generator, which compares the colour before and after the transparent passes (one dispatch).

**Stochastic transparency.** Today it runs at 8 samples, with its own camera-only temporal
accumulation on PR #349 (the resolve costs 0.5 to 0.7 ms at 1080p [verified, from that branch's
doc]). Under FSR 3.1 there are two histories. The design choices are:

- **(A) Keep its own resolve before FSR**, with the per-object motion vectors replacing the
  camera-only reprojection, which fixes its hair ghosting. Mark its pixels with a transparency and
  composition value proportional to the layer alpha. This is the simplest option.
- **(B) Drop its own history when FSR is on.** Feed FSR the 8-sample image, re-seeded every frame,
  and let FSR's accumulation average the noise. This saves the 0.5 to 2 ms resolve. The risk is
  that FSR treats the noise as instability and does not accumulate it [inferred]. This needs an
  image test.
- Either way, the stochastic transparency's `mAlphaSTLastProjection` must be the **unjittered**
  projection, or the jitter has to be included consistently [inferred].

Choose by an offline image test in the style of `scripts/perf/test_alpha_stochastic.py` in stage 3.

### 5.5 Exposure and place in the post chain

**Baseline order in `renderFinalize` (`pipeline.cpp:9266-9455`) [verified].** All of it runs at
render resolution; the present then stretches the result.

1. SSR copy (`:9291`)
2. Luminance → exposure (`:9293-9295`; `generateExposure` `:7768`, writing `mExposureMap`, 1x1
   R16F, `:1699`)
3. Tonemap (`:9300`, `tonemap` `:7883`)
4. CAS (`:9305`, `applyCAS` `:8437`)
5. Glow (`generateGlow` `:8327`) and the glow combine (`:9320`)
6. DoF (`:9333`)
7. FXAA/SMAA (`:9339-9345`)
8. RLV sphere, vignette, snapshot frame
9. Present with noise (`:9412-9423`)

`render_ui` then draws the HUD and the UI (`llviewerdisplay.cpp:1640-1660`).

**Vulkan renderer order with FSR 3.1.**

Render resolution (jittered):

- G-buffer and motion vectors;
- lighting, SSAO, SSR;
- water, haze, sorted alpha, stochastic transparency;
- masks;
- luminance → exposure.

Then the **FSR 3.1 upscale**, which takes:

- the HDR linear colour;
- depth, motion vectors and masks;
- the exposure texture: the viewer's own `mExposureMap` equivalent. Use it rather than FSR's auto
  exposure, so the tonemap and the upscaler agree [inferred].

Display resolution (unjittered):

- tonemap;
- glow combine (the glow itself can stay a low-resolution blur generated at render resolution; it
  is 512 wide today, `pipeline.cpp:1485`);
- DoF (reads the render-resolution depth by UV: low frequency, acceptable [inferred]);
- vignette, RLV sphere, snapshot frame;
- film-grain noise last;
- present;
- HUD attachments and UI.

This is AMD's "post A before, post B after" split [source]. SL's order already puts the tonemap
before glow, DoF and AA, so the split is clean. When FSR is on, **FXAA, SMAA and CAS are off**:
FSR antialiases and RCAS sharpens. The CAS sharpness slider can drive RCAS [inferred].

### 5.6 Interactions with shadows, probes, SSR and PCSS

These follow from §5.2 [inferred]:

- **Shadow maps**: not jittered (light space). The cascade fit uses the unjittered frustum.
  - PCSS: its per-pixel sampling noise is integrated by FSR over frames, which is a quality gain.
    The noise should then vary per frame. Today it is fixed in screen space; the PCSS study chose
    that for stability without TAA.
- **Reflection probes and hero probes**: captured with an unjittered camera, at their own
  resolution. Unaffected.
- **SSR**: reads last frame's colour through the camera delta (`screenSpaceReflUtil.glsl:82-83`).
  The jitter adds a sub-pixel error that FSR then averages, which is harmless. Better: reproject
  with the motion vectors.
- **SSAO**: jitter plus FSR accumulation reduces its noise. A rotating kernel per frame is then
  worthwhile.

## 6. Integration on the Vulkan line: wgpu versus raw Vulkan

Source and build:

- Vendor FidelityFX SDK v1.1.4 `sdk/` (MIT) as an Autobuild package `3p-fidelityfx`. It contains
  the FSR 3.1 upscaler effect, the Vulkan backend and the shaders.
- The shaders are compiled to SPIR-V at package build. The SDK has GLSL callback headers for Vulkan
  [source].
- SDK 1.1.4 lists CMake, Visual Studio 2019, the Windows SDK and Vulkan SDK 1.3.250 as
  prerequisites, with Windows build scripts [source]. **A Linux build of the backend is
  [unverified]**. It is the first spike of stage 1, and the largest single risk for the
  Windows + Linux target.
- Use the per-effect API with the Vulkan backend from source, linked statically, not the ffx-api
  loader, which requires AMD's signed DLL [source: SDK 1.1 notes]. Name check at implementation
  time: `ffxFsr3UpscalerContextCreate`, `ffxGetInterfaceVK` [unverified].

Three hosts:

| Host | How | Pros | Cons | Effort for the hookup [inferred] |
|---|---|---|---|---|
| **Raw Vulkan renderer** | the FFX Vulkan backend gets the `VkDevice`, `VkPhysicalDevice`, `vkGetDeviceProcAddr` and a command buffer; resources are `VkImage`s with their layouts | direct; this is the SDK's own model | the renderer is a larger undertaking than wgpu (not this study's decision) | 3 to 5 days |
| **wgpu + wgpu-hal interop** (favoured renderer) | `Device::as_hal::<Vulkan>` (unsafe; wgpu 30.0.1 [source]) for the raw device; the raw `VkImage` of each wgpu texture; the raw command buffer from the encoder; FSR's required extensions and features enabled at device creation (create the device from hal, or let wgpu open it with a callback) | keeps wgpu everywhere else | (1) wgpu does not know about FSR's own barriers and layouts, so state must be restored or declared; (2) the exact hal accessors for textures and command encoders change between wgpu versions [unverified for wgpu 30]; (3) the FFX backend allocates its own resources on the device | 5 to 8 days, plus a pin on the wgpu version |
| **wgpu-native FFX backend** (fallback) | implement the backend interface (create a resource, dispatch a compute job, copy) on wgpu; run FSR's SPIR-V through wgpu's SPIR-V passthrough, or translate it with naga | no raw-handle hazards; portable across wgpu backends | most work; naga may not accept FSR's fp16/subgroup SPIR-V [unverified], so passthrough would be required | 10 to 15 days |

Recommendation: prototype the wgpu-hal route in stage 1 against the wgpu version the renderer
pins. Fall back to the wgpu-native backend if barrier handling proves fragile. Frame generation
(§4) would additionally need a raw swapchain, so a wgpu renderer that later wants it should keep
the present path replaceable [inferred].

## 7. Licence

**SDK v1.1.4 `sdk/` (FSR 3.1 source, Vulkan backend)** [source]:

- MIT, "Copyright (C) 2024 Advanced Micro Devices, Inc."
- Compatible with LGPL-2.1 and GPL-3.0.
- Attribution: keep the copyright and permission notice in the source and in the distribution.
  That means an entry in `doc/licensing.md` (the audit table, beside the other MIT rows) and the
  viewer's third-party notices.
- Files we write around it carry the dual SPDX header.

**SDK 2.x (2.4.0, FSR 3.1.6)** [source: `docs/license.md`]:

- Two texts:
  - a primary AMD licence: binary form only, no reverse engineering, notices reproduced;
  - an MIT-style text for an explicit list of files. That list includes the `signedbin` DLLs,
    according to the fetched summary; read the file yourself before relying on that.
- Either way, the effect code ships as binaries without source.
- Shipping the viewer under **GPL-3.0** with a dynamically linked library that has no Corresponding
  Source is not compatible with GPL-3 §6 (it is not a System Library) [inferred, not legal advice].
- The LGPL-2.1 side would permit it, but this project's rule is that a new linked library must be
  compatible with **both** (`AGENTS.md`, `doc/licensing.md`). **So: no SDK 2.x DLLs.**

**FSR 4**: delivered only through the signed SDK 2.x DLLs, or the driver override. The same
objection applies (§9.2).

**Existing note.** `CASF.glsl` already carries AMD's MIT notice (`CASF.glsl:52-71`) [verified].
`doc/licensing.md` has no row for it [verified by grep]. Adding a row for FidelityFX (CAS now, FSR
later) is worth doing with the first FSR work.

## 8. Effort summary (FSR 3.1 upscaler on the Vulkan line) [inferred]

These figures assume the renderer itself exists and that the items are designed in, not
retrofitted.

| Item | Days |
|---|---|
| Render and display resolution split, post chain order, the FSR on/off path | 3 to 5 |
| Jittered projection with unjittered culling, shadows and probes; mip bias; streaming at display resolution | 2 to 4 |
| Camera motion pass; per-draw previous transforms | 4 to 7 |
| Skinned motion (previous palette or compute skinning) | 5 to 10 |
| Flexi, particles, texture animation, media: masks and reactive values | 3 to 5 |
| Stochastic transparency under FSR (A or B, with the image test) | 2 to 4 |
| Exposure hookup, reset signals (teleport, region crossing, snapshot, camera snap) | 1 to 2 |
| Package `3p-fidelityfx` from SDK 1.1.4: Windows and **Linux** builds, shader compilation | 4 to 8 (Linux risk) |
| FSR context, dispatch, resize, modes; wgpu-hal glue | 5 to 8 |
| Bench scene set, image tests, GPU timers | 4 to 6 |
| **Total** | **33 to 59** |

Of this, the prerequisites (motion, jitter, masks, post split) are 20 to 37 days. They also improve
the stochastic transparency's temporal pass, and would serve a TAA or another upscaler. That makes
them valuable even if FSR were later replaced.

Frame generation, if adopted later: 15 to 25 more days, plus a raw present path (§4).

## 9. Short comparisons

### 9.1 FSR 1 (EASU + RCAS)

- Spatial and single-frame, with no motion vectors or history [source].
- The input should be the antialiased image in a perceptual space: after the tonemap and the AA,
  before noise and UI.
- Modes 1.3x, 1.5x, 1.7x and 2.0x. MIT, with a GLSL path.
- Cost [source]: at most 0.4 ms at 4K on an RX 6800 XT or RTX 3080; at most 0.2 ms at 1440p.
- Quality is clearly below FSR 3.1: no added detail, no AA of its own. It needs FXAA or SMAA first.

As a cheap first stage on **today's OpenGL viewer** (owner's call; not part of the Vulkan plan):

- It would replace the bilinear stretch of the existing `RenderResolutionMultiplier` path:
  - EASU between the AA pass and the present (`pipeline.cpp:9345` / `:9412`), into a target at
    display resolution;
  - RCAS from the existing CAS slider;
  - vignette, snapshot frame and noise moved to display resolution.
- Opt-in; OpenGL output is unchanged at a scale of 1.0.
- 3 to 5 days, plus an image test.
- Gate: at most 0.3 ms at 1440p on the reference GPU; bit-identical at a scale of 1.0.

### 9.2 FSR 4 (ML)

- **Hardware** [source]: RX 9000 (RDNA 4) since SDK 2.0. RX 7000 (RDNA 3) since 4.1.1 (SDK 2.3.0,
  an INT8 model per press reports). RX 6000 (RDNA 2) listed for 4.1.2 in SDK 2.4.0. The press
  disagrees on whether RDNA 2 arrives now or in early 2027.
- **Vendors**: AMD only; NVIDIA and Intel are not supported [unverified, no statement found]. On
  unsupported hardware the API falls back to FSR 3.1.5 [source].
- **API**: DX12 and Vulkan (Vulkan parity in SDK 2.4.0) [source].
- **Delivery**: signed DLLs only, so the licence rule excludes it (§7).
- Not recommended. The FSR 3.1 integration keeps the same inputs, so nothing would be lost if this
  changed [inferred].

## 10. Staged plan for when the Vulkan line opens

Ground rules:

- Each stage is a feature PR on the Vulkan line (`codex/fsr-*` from `vkstorm-vulkan`), built
  `RelWithDebInfo`.
- The owner's in-world check comes once, after release.
- Every gate includes: **with the upscaler off, no frame-time cost and no image change** against
  the renderer without the stage.

**Stage 0: design in the renderer (part of the renderer's own design, not FSR work).**

- Two resolutions, the post chain split of §5.5, a projection-jitter hook, unjittered matrices for
  culling, shadows and probes, a motion-vector attachment slot, and the compute-skinning decision.
- *Gate:* with no upscaler, the extra attachment and pass are not allocated, and the frame time
  equals the build without them (within noise, 3 runs).

**Stage 1: licence and package spike.**

- Build FSR 3.1 (SDK 1.1.4 MIT source, Vulkan backend) on Windows and Linux into `3p-fidelityfx`.
- Run a headless dispatch on synthetic inputs on NVIDIA, AMD and Intel (Mesa on Linux).
- Prototype the wgpu-hal hookup.
- Add the `doc/licensing.md` row.
- *Gate:* both platforms build, and the dispatch passes validation layers on all three vendors. If
  Linux fails, the line decides between the wgpu-native backend (§6) and Windows-only (not
  recommended).

**Stage 2: camera motion, jitter, Native AA (scale 1.0).**

- Static scenes only. Replaces FXAA and SMAA as an AA option.
- *Gate:* an offscreen image test against a supersampled reference shows less error than SMAA on
  the static bench views. The upscaler pass costs no more than AMD's table plus 10 %, and the
  camera motion pass no more than 0.2 ms at 1440p.

**Stage 3: per-object and skinned motion, masks, stochastic transparency choice.**

- *Gate:*
  - The ghosting image tests (moving avatar, blended hair, particles, texture animation) stay within
    a set error of the no-history reference.
  - The G-buffer and skinning cost on the 40-avatar bench rises at most 5 %, or falls with compute
    skinning.
  - The stochastic transparency's moving-hair rmse is below #349's 10.9.

**Stage 4: render-scale modes (Quality, Balanced, Performance), mip bias, streaming at display
resolution.**

- *Gate (the performance gate):*
  - On the GPU-bound bench views at 1440p and 4K, Quality reaches at least 1.3x the frame rate of
    native.
  - On the CPU-bound crowd view, the cost is at most 0.3 ms over native when on. Off is free, which
    is the default.
  - Text on prims stays legible in the texture-detail image test.

**Stage 5 (separate decision): frame generation** (§4), only if stage 4's measurements show common
base frame rates of 60 fps or more on target hardware.

## 11. Settings and Preferences (a proposal; the owner decides)

Owner rule: no Preferences toggles that were not asked for; structural parts are debug settings
only.

**Debug settings (all structural):**

| Setting | Values |
|---|---|
| `RenderUpscaler` | 0 off (default), 1 FSR 3.1 |
| `RenderUpscaleMode` | 0 Native AA, 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance |
| `RenderUpscaleCustomScale` | takes over `RenderResolutionMultiplier` |
| `RenderUpscaleSharpness` | default: the CAS value |
| `RenderUpscaleDebugView` | FSR's debug view |
| `RenderUpscaleReactiveScale` | debug only |

The motion-vector target, the jitter and the masks have no settings: they follow `RenderUpscaler`.

**Preferences, if the owner asks for it.** A single "Upscaling" choice, Off or FSR 3.1 with the
mode, in Graphics beside the antialiasing choice. Two placements are possible:

- extend the existing antialiasing drop-down (`RenderFSAAType` in
  `panel_preferences_graphics1.xml:1050`) with an "FSR 3.1 Native AA" entry, plus a separate
  render-scale mode list; or
- a single new drop-down.

This study proposes nothing in Preferences until stage 4 has passed its gate.

## 12. Sources

- FidelityFX SDK repository, releases and v2.4.0 (FSR upscaler 3.1.6, Frame Generation 3.1.6,
  Vulkan parity), README component table:
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK,
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.4.0
- SDK 2.x signed binaries (`_dx12` and `_vk` DLLs, no Linux binaries):
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/main/Kits/FidelityFX/signedbin
- SDK 2.x licence (primary AMD binary-only text plus an MIT-style list):
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/license.md
- SDK v1.1.4 (MIT `sdk/LICENSE.txt`; `sdk/src/backends/vk`;
  `sdk/include/FidelityFX/gpu/fsr3upscaler/` with GLSL and HLSL callbacks):
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v1.1.4,
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v1.1.4
- SDK 1.1 "what's new" (ffx-api, signed DLL, DX12 and Vulkan backends only):
  https://gpuopen.com/manuals/fsr_sdk/whats-new/version_1_1/
- FSR 3.1.6 upscaler manual (inputs, flags, jitter, modes, memory, placement):
  https://gpuopen.com/manuals/fidelityfx_sdk2/techniques/super-resolution-upscaler/
- ffx-api: https://gpuopen.com/manuals/fidelityfx_sdk2/getting-started/ffx-api/
- Frame interpolation manual:
  https://gpuopen.com/manuals/fidelityfx_sdk2/techniques/frame-interpolation/
- FSR 3.1 release post (decoupled frame generation, Vulkan, GPU support, 60 fps advice):
  https://gpuopen.com/learn/amd_fsr_3_1_release/
- FSR 2.2 README (pass cost and memory tables, MIT):
  https://github.com/GPUOpen-Effects/FidelityFX-FSR2
- FSR 1 (EASU and RCAS, GLSL via `A_GLSL`, MIT, cost bounds): https://gpuopen.com/fidelityfx-superresolution/,
  https://github.com/GPUOpen-Effects/FidelityFX-FSR
- FSR 4 hardware: SDK v2.4.0 release notes (above);
  https://www.gamingonlinux.com/2025/08/amd-fidelityfx-super-resolution-4-fsr-4-now-available-for-all-developers-with-fidelityfx-sdk-2-0;
  https://guru3d.com/story/amd-has-expanded-fsr-41-support-to-radeon-rx-7000-gpus/
- GL–Vulkan interop: https://registry.khronos.org/OpenGL/extensions/EXT/EXT_external_objects.txt;
  Mesa radeonsi https://www.phoronix.com/news/RadeonSI-Semaphores-V2; iris
  https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/4337
- wgpu `Device::as_hal`, `create_texture_from_hal` (wgpu 30.0.1): https://docs.rs/wgpu/latest/wgpu/struct.Device.html
- In this repository: `indra/newview/pipeline.cpp`, `llviewercamera.cpp`, `llviewerdisplay.cpp`,
  `lldrawpool.cpp`, `llvopartgroup.cpp`, `llflexibleobject.cpp`, `app_settings/settings.xml`,
  `app_settings/shaders/class1/deferred/CASF.glsl`, `class3/deferred/screenSpaceReflUtil.glsl`;
  `doc/msaa/deferred-msaa-study.md`; `doc/oit/stochastic-transparency.md`; PR #349
  (`codex/stoch-temporal-2`); `doc/vulkan/direction-2026-10-29.md`.
