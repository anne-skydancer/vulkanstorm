# Multisampled deferred shading (MSAA 2x, 4x, 8x): study

Status: study with offline measurements, 2026-11-05. Branch `codex/msaa-study` from
`vkstorm-devel` (b8c0e63eba). No viewer code is changed. The OpenGL renderer is the subject.
Notes for the later Vulkan line are in section 8.

Labels used below: **[verified]** means read in source or measured by the bench in this
branch. **[inferred]** means reasoned from those, not tested. **[survey]** means read in the
devel tree by a read-only survey at b8c0e63eba; line numbers are approximate and should be
re-checked when the code is written.

## 1. Recommendation

Build MSAA as **design A with edge classification**:

- The G-buffer, its depth (D24 becomes D24S8, same size) and the lit HDR scene colour are
  multisampled.
- A full-screen classification pass marks complex pixels in the stencil.
- Lighting shades simple pixels once and writes that value to all of their samples. It
  shades complex pixels per sample.
- The forward passes (water, sorted alpha, the stochastic transparency composite, haze)
  draw into the multisampled scene, so their edges are antialiased too.
- A tonemap-weighted resolve (Karis/Lottes) then produces the single-sample HDR image that
  today's post chain consumes unchanged. A one-sample depth resolve feeds SSAO, SSR, depth
  of field and the present pass.

Offer 2x and 4x everywhere it is supported, and 8x only on high-memory GPU classes. Expose
it as its own setting `RenderMSAASamples` (0, 2, 4, 8), next to the existing
FXAA/SMAA choice. The existing settings then need no migration, and SMAA can still run on
the resolved image.

**Memory, from the format arithmetic** [verified against `GL_NVX_gpu_memory_info`, which
reads 3 to 10 % higher]:

| Resolution | MSAA | G-buffer + depth | Scene colour | Resolve targets | Total | Added over 1x |
|---|---|---|---|---|---|---|
| 1080p | 1x | 55 MiB | 16 MiB | 0 | 71 MiB | 0 |
| 1080p | 2x | 111 MiB | 32 MiB | 24 MiB | 166 MiB | +95 MiB |
| 1080p | 4x | 221 MiB | 63 MiB | 24 MiB | 308 MiB | +237 MiB |
| 1080p | 8x | 443 MiB | 127 MiB | 24 MiB | 593 MiB | +522 MiB |
| 1440p | 1x | 98 MiB | 28 MiB | 0 | 127 MiB | 0 |
| 1440p | 2x | 197 MiB | 56 MiB | 42 MiB | 295 MiB | +169 MiB |
| 1440p | 4x | 394 MiB | 112 MiB | 42 MiB | 548 MiB | +422 MiB |
| 1440p | 8x | 788 MiB | 225 MiB | 42 MiB | 1055 MiB | +928 MiB |

- G-buffer is RGBA8 + RGBA8 + RGBA16 + RGB16F (padded to 8 B) + D24S8: 28 B per sample.
  The scene colour is RGBA16F. The resolve targets are an RGBA16F colour and a D24 depth.
- Stochastic transparency adds a transmittance target at the scene's sample count, RG16F,
  4 B per sample: 1440p 2x/4x/8x +28/56/112 MiB.
- The measured figures are 1080p 1x/2x/4x/8x: 78/182/321/616 MiB; 1440p: 139/324/578/1110 MiB.

**Scene-pass GPU cost, measured** [verified]:

- Hardware: RTX 5070 Ti, NVIDIA 621.01, OpenGL 4.6, bench `scripts/perf/bench_deferred_msaa.py`,
  median of 20 frames, third run (after the review fixes).
- What is timed: G-buffer fill + classification + lighting + tonemap-weighted resolve +
  depth resolve. The lighting stand-in is "heavy" (sun + 24 point lights GGX + 2 probe
  lookups; 0.40 ms per pixel-shaded 1440p frame).
- The scenes:
  - "open" has a few large triangles: 1 to 2 % complex pixels.
  - "dense" is a built-up region: 3 to 7 %.
  - "foliage" has 40 000 thin triangles: 17 to 42 %. It is the worst case.

| Resolution | Scene | 1x | 2x | 4x | 8x |
|---|---|---|---|---|---|
| 1080p | open | 0.44 | 0.54 (+0.10) | 0.66 (+0.22) | 1.08 (+0.64) |
| 1080p | dense | 0.48 | 0.67 (+0.19) | 0.94 (+0.46) | 2.11 (+1.63) |
| 1080p | foliage | 0.54 | 1.10 (+0.56) | 1.83 (+1.29) | 4.40 (+3.86) |
| 1440p | open | 0.80 | 0.95 (+0.15) | 1.14 (+0.34) | 1.82 (+1.02) |
| 1440p | dense | 0.82 | 1.13 (+0.31) | 1.73 (+0.91) | 3.73 (+2.91) |
| 1440p | foliage | 0.88 | 1.79 (+0.91) | 3.28 (+2.40) | 9.61 (+8.73) |

What these numbers say [verified unless marked]:

- **Lighting cost follows the number of shaded samples.** Per-sample shading everywhere
  costs N times a single shade (1440p heavy: 0.40 → 0.85 / 1.69 to 1.72 / 3.44 ms). With
  classification, the open and dense scenes come back to 0.43 to 0.81 ms at every sample
  count.
- **The G-buffer fill dominates at 8x on dense geometry.** The fill is 0.39 → 0.67 ms
  (open) but 0.48 → 6.4 ms (foliage) at 1440p. It also varies most from run to run: over
  three runs 0.92 to 1.24 ms (1440p dense 4x) and 2.4 to 4.1 ms (1080p foliage 8x). The bench's fill shader is trivial, so the viewer's real G-buffer shaders add their
  own cost at 1x. The MSAA increment (raster, depth and ROP work and bandwidth per sample)
  is the part this table measures [inferred: the increment carries over].
- **The resolve is cheap.** The tonemap-weighted resolve costs the same as
  `glBlitFramebuffer` at 2x/4x (0.02 to 0.07 ms) and up to 0.04 ms more at 8x (up to
  0.14 ms at 1440p). The depth resolve costs 0.02 to 0.05 ms.
- **Classification costs 0.02 to 0.09 / 0.08 to 0.14 / 0.27 to 0.49 ms at 2x / 4x / 8x.**
  Comparing only depth and normal, without albedo, saves 20 to 35 %, but it misses material boundaries between coplanar faces and is not validated, so the
  recommendation keeps the full rule (section 4.2).
- **The viewer's real lighting is heavier than the stand-in.** Soften with probes,
  atmospherics and shadows, plus up to 256 local lights, makes the per-sample-everywhere
  design worse and classification better than shown here [inferred].

- **Design B (lighting resolved inside the lighting shader) is competitive in this sum at
  8x on simple scenes.** It skips the standalone classification and the colour resolve
  (1440p open heavy 8x: B-edge 1.84 against A-edge 1.82 ms; light lighting: 1.24 against
  1.61 ms). It is still not recommended: the forward passes (water, alpha, haze,
  transparency) stay aliased, and the in-shader average is a box resolve of HDR (section 3,
  lesson 2).

**Why not 8x by default:** +0.9 GB at 1440p, and 2 to 6 ms on dense and foliage-heavy
views from the fill alone. 8x stays an opt-in for high-memory GPU classes.

**Zink check** [verified]: Mesa 26.3.0-devel Zink on the same GPU.
- `GL_MAX_*_SAMPLES` is 8, and every G-buffer format takes 2/4/8 (NVIDIA: up to 32).
- All 60 correctness checks pass.
- The scene-pass sums are mostly within 15 % of NVIDIA's OpenGL driver, and up to about
  25 % higher at 8x (1440p open heavy 8x: 2.26 against 1.82 ms). The exception is the blit
  resolve, which the timer query does not capture under Zink (0.002 ms), so it is not
  comparable.

## 2. Inventory of the current frame and what MSAA needs per pass

Sources: [survey] of `pipeline.cpp` (`allocateScreenBufferInternal` ~913-1090,
`renderDeferredLighting` ~9772-10120, `renderFinalize` ~9257-9440), `llrendertarget.cpp`,
`lldrawpoolwater.cpp`, `lldrawpoolalpha.cpp`, `llreflectionmapmanager.cpp`,
`llheroprobemanager.cpp`, `llviewerdisplay.cpp` and `llviewerwindow.cpp`.

### Baseline facts [survey]

- `LLRenderTarget` has no sample count. Every attachment is `GL_TEXTURE_2D`, and depth is
  a `GL_DEPTH_COMPONENT24` texture without stencil.
- There is no `glBlitFramebuffer` in `llrendertarget.cpp`, and no stencil use in the frame.
- The window asks for 0 samples (`llviewerwindow.cpp` ~2030).
- `RenderFSAASamples` is only the FXAA/SMAA quality (0 to 3).

### Targets and passes

"MS" in the table means multisampled at the scene's N samples.

| Target / pass | Today [survey] | Under MSAA (design A) |
|---|---|---|
| G-buffer `mRT->deferredScreen` | RGBA8 diffuse, RGBA8 spec/ORM, RGBA16 normal (`encodeNormal` xy, env, flag in w; RGB10_A2 without HDR), RGB16F emissive, D24 | **MS**, D24 becomes D24S8 (stencil for the classification). The flag in normal.w must never be averaged. Only the colour and depth are resolved, never the G-buffer. |
| Depth pre-pass (`RenderDepthPrePass`, off by default) | depth only | MS (same depth) |
| Sun shadow maps `mRT->shadow[0..3]`, spot `mSpotShadow[0..1]` | depth only | **Nothing.** Light-space maps, not screen space. |
| Sun / SSAO light map `mRT->deferredLight` (`sunLightSSAOF`, R shadow, G AO, BA spot shadows) + `blurLightF` | full screen, reads depth and normal by `texture()` | **Per pixel** from the resolved depth and sample-0 normal (unchanged shaders, rebound inputs). Risk: halo where a silhouette's samples take the other surface's shadow and AO. The bilateral blur already mixes across edges at 1x [inferred]. Option: re-evaluate the sun shadow per sample on complex pixels only. |
| Soften / sun / IBL / sky (`class3/deferred/softenLightF`) → `mRT->screen` | one full-screen pass, `texture()` point reads of the G-buffer, probes sampled inside | **Classified:** per pixel on simple pixels (stencil 0, writes all samples), per sample on complex ones (stencil 1, `gl_SampleID`). `getGBuffer`/`getDepth` take a sample index (`texelFetch` on `sampler2DMS`). |
| Local point / spot lights (cube volumes with depth test; full screen when the camera is inside; `multiPointLightF`, `multiSpotLightF`) | additive, one draw per light | Same classification. Each light draws twice (stencil 0 per pixel, stencil 1 per sample), or once at sample rate inside small volumes. Choose by measurement in stage 2. The depth test against the MS depth is per sample. |
| Reflection probes sampled in soften | cube array, per pixel | Per sample on complex pixels (inside soften). Probe captures stay single-sample (below). |
| SSR (`RenderScreenSpaceReflections`, off by default; `copyScreenSpaceReflections` → `mSceneMap`) | copy of the colour and depth for the next frame | Copy from the **resolved** colour and depth (single-sample `mSceneMap`, unchanged). |
| Water exclusion mask `mWaterExclusionMask` | R8 + own depth | Single-sample is enough (a mask read by haze) [inferred]. |
| Atmospherics haze (`doAtmospherics`) | `gCopyDepthProgram` copies depth into `mWaterDis` to avoid reading the attached depth, then full-screen blend | Read the **resolved depth** (haze is low frequency). The full-screen blend into the MS scene writes all samples. The depth copy is no longer needed. |
| Water haze (`doWaterHaze`), water pool (`waterF`: `screenTex` refraction = `mWaterDis` colour copy, depth-difference fog) | colour and depth copy, forward mesh with depth test | Refraction source: a **box resolve** of the MS scene into `mWaterDis` (one blit, 0.02 to 0.1 ms). Fog depth: the resolved depth. The water mesh rasterises into the MS scene with an MS depth test, **so water edges are antialiased**. |
| Sorted alpha (legacy forward path), fullbright, glow pools | forward into `screen` | Into the MS scene: **edges antialiased for free** (per-pixel shading, per-sample coverage and depth). |
| Stochastic transparency (#321) | own MS targets at P ≤ 8 samples, composite per pixel | Total alpha and composite at the scene's N, accumulation at P ≥ N (section 6). |
| PPLL (devel default today; removed by #321) | per-pixel image arrays | **Not supported with MSAA.** MSAA requires #321 to have landed, or falls back to sorted alpha. |
| Glow (screen alpha → `generateGlow`) | alpha of `screen` | Resolved as the plain sample mean beside the weighted colour (glow is linear). |
| Luminance / exposure (`generateLuminance`, `generateExposure`) | reads `screen` | Reads the resolved colour. Unchanged. |
| Tonemap / gamma (`postDeferredTonemap`, `postDeferredGammaCorrect`) | `screen` → `mPostPingMap` RGBA8 | Reads the resolved colour. Unchanged. The resolve is weighted so that the tonemap of the average is close to the average of tonemaps (section 4.3). |
| CAS, glow combine, DoF (`cofF` reads depth), vignette, snapshot frame, RLV effects | single-sample LDR chain | Unchanged on resolved inputs. DoF reads the resolved depth. |
| FXAA / SMAA (`applyFXAA`, `applySMAA`; FXAA writes `gl_FragDepth` from the G-buffer depth) | `RenderFSAAType` 1/2 | Unchanged on the resolved image. Any `gl_FragDepth` copy reads the resolved depth (section 4.5). |
| Present (`postDeferredNoDoFF` to the back buffer, writes `gl_FragDepth` for HUD and UI depth tests) | reads the G-buffer depth | Reads the resolved depth. |
| HUD attachments, UI, `mUIScreen` | forward into the default framebuffer after `renderFinalize` | **Nothing** in stage 1. The HUD is not antialiased (it is today neither). A later option is a HUD-only MS target. |
| Reflection probe captures (`mAuxillaryRT`, `gCubeSnapshot`, `ProbeResolution*4` "16x supersampled", blurred) | full deferred path into the aux RT pack | **Single-sample always.** The allocation must skip MSAA when `mRT != &mMainRT`. |
| Hero probes / mirrors (`mHeroProbeRT`, `RenderMirrors` off by default) | full deferred path at hero resolution, not supersampled | Single-sample in stages 1 to 3. Candidate for MSAA in stage 4, because mirror edges are visible [inferred]. |
| Avatar impostors (`generateImpostor`, own G-buffer) | G-buffer without lighting, drawn later as quads | **Single-sample** (filtered quads). |
| Snapshots (`rawSnapshot`: reallocates the screen buffers at snapshot size, or tiles) | full `display()` per tile | MSAA at snapshot size if it fits the budget, else fewer samples or off. Check `GL_MAX_SAMPLES` and memory, not only `gGLMaxTextureSize`. |
| Material preview (`llgltfmaterialpreviewmgr` forces `RenderFSAAType` 0) | own target | Force MSAA off as well. |

## 3. What the old Linden implementation teaches

**Correction to the brief.** MSAA was not removed in the 7.0 PBR rewrite. Linden's
multisampled deferred shading existed for about four months in 2011 and was gone by
viewer 3.2.4. The 6.x viewers already had no MSAA in deferred: `RenderFSAASamples > 1`
only meant "FXAA on", and the window asked for 0 samples. [verified in the local
Firestorm clone `phoenix-firestorm` history; all commits by Dave Parks, LGPL-2.1]

### Timeline

| Commit | Date | Change |
|---|---|---|
| `4353eeb928` | 2011-05-24 | SH-469 "GL_ARB_texture_multisample support". It adds the `*MSF.glsl` shaders. |
| `9eea451a82` | 2011-05-25 | Consistent sample counts between shaders and targets. |
| `2dd8ce53e4` | 2011-08-11 | SH-2242 "FXAA support instead of unreliable multisample textures (done here because it's a smaller change than integrating glVertexAttrib with FSAA pipe)". |
| `7ee10ae1de` | 2011-08-27 | SH-2242 "Remove old multisample shaders…", −1605 lines. First tag that contains it: 3.2.4-beta1. |
| `f558d86578` | 2011-09-30 | SH-2244 "Don't use multisample texture (ever)…". It removes `glTexImage2DMultisample` from `llrendertarget.cpp`. |

### How it worked (at `7ee10ae1de^`)

- Shader variants existed for soften, sun, sun + SSAO, point and spot lights (single and
  multi), blur and the post passes, all named `*MSF.glsl`. The sample count was a define,
  set by `mDefinitions["samples"]`.
- Every pass looped over all samples with `texelFetch` on `sampler2DMS` and wrote **the
  average into a single-sample target**: the full lighting ran per sample, per pixel, on
  every pixel.
- There was **no edge detection and no stencil.** `mEdgeMap` served depth of field only.
- Samples were capped at 4 (`getNumFBOFSAASamples`: min of the colour and depth texture
  limits and 4).
- Stencil was disabled on MS targets ("no support for multisampled stencil targets yet").
- ATI was forced to 0 samples.
- A failed allocation halved the samples.
- The G-buffer was 8-bit RGBA, with no HDR.

### Lessons

1. **Shade per sample only where it matters.** The 2011 design is the bench's `b_all`. The
   bench measures its lighting at the full N-times cost (1440p heavy: 0.84 / 1.69 /
   3.39 ms at 2x / 4x / 8x against 0.40 ms single-sample) [verified]. Linden named
   unreliable drivers, not cost, as the reason for dropping it [verified]. The cost alone
   would have made 4x hard on 2011 hardware [inferred].
2. **Do not resolve lighting inside the lighting shader** (design B). It is simple and
   saves the MS scene colour, but the forward passes (water, alpha, haze) then draw into a
   single-sample target against a resolved depth. Their edges stay aliased, and depth-tested
   forward geometry needs a single-sample depth that disagrees with the samples at
   silhouettes [inferred]. Design B with an in-shader edge branch (`b_edge`) also lights more
   slowly than design A with stencil classification (1440p dense heavy 4x: 1.20 ms,
   classification included, against 0.60 + 0.14 ms) [verified]. The branch diverges in a
   warp. In the scene-pass sum B can still come out level at 8x on simple scenes, because
   it needs no colour resolve (section 1).
3. **The driver problems of 2011 are gone.** Both NVIDIA 621.01 and Zink create every
   G-buffer format at 2, 4 and 8 samples, including D24S8 with stencil, and all FBOs are
   complete [verified]. The sample-count halving fallback is still worth keeping for
   allocation failures.
4. **Keep the flag and packed channels out of any averaging.** Linden's average of `bloom`
   and specular was unweighted. The current G-buffer packs a gbuffer flag into normal.w
   (two bits under RGB10_A2), so only lit colour may be resolved [verified packing,
   survey].
5. **A dedicated setting beats reusing `RenderFSAASamples`.** That name meant a sample
   count before 2011 and a quality level since 2024 (`958afaa7a7`, SMAA). Saved settings
   from both eras exist [verified].

## 4. Design options and trade-offs

### 4.1 Where lighting runs per sample

| Option | Lighting work | MS scene colour | Forward edges antialiased | Measured (1440p heavy, dense, 4x) |
|---|---|---|---|---|
| A-all: sample-rate shading everywhere (`gl_SampleID`) | N × | yes | yes | lighting 1.72 ms, scene 2.72 ms |
| **A-edge: stencil classification, per pixel on simple and per sample on complex pixels** | 1 × + edges × (N − 1) + classification | yes | yes | lighting 0.60 ms + classification 0.14 ms, scene 1.73 ms |
| B-all: per-pixel loop over samples, average out (Linden 2011) | N × | no | no | lighting 1.69 ms, scene 2.64 ms |
| B-edge: per-pixel loop only on complex pixels (branch) | 1 × + edges, divergent | no | no | lighting 1.20 ms (classification included), scene 2.15 ms |
| C: compute-tiled classification (Lauritzen 2010: per-tile lists of complex pixels, shaded in the same dispatch) | like A-edge, without the stencil and quad overhead | yes (via image store) | yes | not measured |

[verified measurements; C inferred from Lauritzen 2010]

**Two-pass stencil overhead.** The second pass in A-edge runs at sample rate on scattered
pixels, and its quads are poorly packed. Where complex pixels exceed about 20 % (the
foliage scene), A-edge is no better than A-all: 1440p foliage heavy 2x lighting 0.80 against
0.85 ms, light 2x 0.31 against 0.23 ms, before the 0.09 ms classification [verified].

**Adaptive choice.** The classification's `GL_SAMPLES_PASSED` count of the previous frame
gives the edge fraction for free. Above a measured threshold, the frame can skip the split
and shade every sample [inferred; stage 1 measures the threshold in the viewer].

**Option C.** It is the standard answer for many local lights (Lauritzen's tiled deferred).
It would also suit the Vulkan line (section 8). It is a larger change to a pipeline that has
no compute lighting today, so it is not recommended for the OpenGL line now.

### 4.2 Edge classification rule

- A pixel is complex if any sample differs from sample 0 in:
  - window depth by more than 2⁻⁹ in the bench (the viewer should compare linear depth
    relative to distance);
  - encoded normal by more than 0.02 (about 4.5°);
  - gbuffer flag (any difference);
  - albedo by more than 4/255.
- MSAA evaluates attributes once per pixel and depth per sample. Samples of one triangle
  therefore differ only in depth, within the tolerance [verified: model and GLSL,
  `scripts/tests/test_msaa_bench_model.py`].
- Missed edges cost little. A-edge against A-all over a 256² window at 1080p is at most
  4.7 steps (8-bit, tonemapped) on single foliage pixels, with RMSE ≤ 0.04, and ≤ 0.06
  steps in the open and dense scenes [verified, checks in the bench].
- The depth-and-normal-only rule is 20 to 35 % cheaper, but it treats coplanar faces with
  different materials (same depth, normal and flag, different albedo) as simple. A-edge then
  writes sample 0's colour to every sample and the material boundary stays aliased. The
  bench's image checks use the full rule, so they do not validate the cheaper one.
  **The recommendation keeps the full rule** (depth, normal, flag, albedo), and stage 1
  also tests whether ORM or emissive differences need comparing. The cheaper rule is
  adopted only if an image test of coplanar material boundaries shows the loss acceptable
  [verified that the checks use the full rule; the quality loss is not measured].
- Reading the depth attachment while writing the stencil is a feedback loop in the GL spec.
  The viewer already samples the attached depth in its full-screen passes with depth writes
  off. The bench's stencil written that way passes the image checks on NVIDIA and Zink
  [verified]. Linden used a separate copy for the same reason (`mDeferredDepth`).

### 4.3 Resolve before or after tone mapping

- A box resolve of HDR samples lets one bright sample (sun glint, emissive edge) dominate
  the pixel, so the edge loses its antialiasing after tone mapping.
- The usual fix is to resolve "after tonemap": weight each sample by
  1 / (1 + max(r, g, b)) and renormalise. This is Karis 2013 (weight 1/(1 + luma)) and
  Lottes 2016 (max3, reversible).
- It equals tonemap → average → inverse tonemap exactly when every sample's largest channel
  is the same channel, and comes close otherwise [verified: unit test].
- The bench checks the GPU resolve against the CPU model on a per-sample pattern with one
  sample at 40× brightness: maximum relative error ≤ 8.6·10⁻⁴ at 2/4/8x, on NVIDIA and
  Zink [verified].
- It costs the same as the blit at 2x/4x [verified].
- The alpha channel carries glow, which is linear, so it is resolved as the plain sample
  mean. Weighting it by brightness would suppress the glow of bright samples: one
  (40, 40, 40, α 1) sample among three dark α 0 samples would resolve to α 0.01 instead
  of 0.25 [verified: unit test].
- Recommended: always the weighted resolve for colour, with the current exposure
  multiplied into the weight. This leaves the HDR post chain (luminance, exposure, tonemap) unchanged.

### 4.4 Depth for the post chain

- One single-sample depth, resolved by a full-screen `gl_FragDepth = sample 0` pass right
  after the G-buffer: 0.02 to 0.05 ms [verified].
- Use the nearest sample (min) instead if depth of field shows halos [inferred].
- SSAO, haze, water fog, depth of field, the SSR copy, FXAA's depth write and the present
  pass read it.
- It also removes the haze passes' two full-screen depth copies into `mWaterDis` today. They
  read the resolved texture, while the MS depth stays attached for depth tests.

### 4.5 MSAA with FXAA or SMAA

- MSAA does not touch alpha-tested cutouts (G-buffer `discard`), shader and specular
  aliasing, or texture aliasing. FXAA and SMAA on the resolved image still help there, at
  their existing cost.
- SMAA's own MSAA modes (S2x, 4x) run edge detection per sample (Jimenez et al. 2012). They
  need per-sample SMAA passes and a 2x sample pattern, so they are out of scope [inferred].
- Plain SMAA 1x after the MSAA resolve is the simple, supported combination.
- Recommended: keep the two controls independent and let presets choose.

### 4.6 Alpha-masked surfaces (foliage, hair)

- Alpha-mask materials discard in the G-buffer pass. Under MSAA the cutout edge stays
  aliased, because the discard is per pixel.
- Alpha-to-coverage, or a shader-written `gl_SampleMask` from the alpha with dithering, as
  the stochastic transparency does, gives those edges N coverage levels.
- This is likely the largest visible gain in Second Life content (trees, grass, hair)
  [inferred]. It is a stage 4 item.
- A2C uses draw buffer 0's alpha, which in the G-buffer is the "unshadowed" weight. A
  shader-written mask avoids that conflict [survey].

## 5. Memory and cost

The headline tables are in section 1. Measured detail [verified], all 1440p heavy, ms:

| Pass | 1x | 2x | 4x | 8x |
|---|---|---|---|---|
| G-buffer fill, open / dense / foliage | 0.39 / 0.42 / 0.48 | 0.42 / 0.54 / 0.84 | 0.47 / 0.92 / 1.64 | 0.67 / 2.27 / 6.45 |
| Classification (full rule / depth + normal) | - | 0.04-0.09 / 0.03-0.07 | 0.14 / 0.10-0.11 | 0.45-0.49 / 0.31-0.33 |
| Lighting per pixel (today) | 0.40 | - | - | - |
| Lighting A-all | - | 0.85 | 1.69-1.72 | 3.44 |
| Lighting A-edge, open / dense / foliage | - | 0.43 / 0.48 / 0.80 | 0.46 / 0.60 / 1.41 | 0.52 / 0.81 / 2.52 |
| Resolve, tonemap-weighted (after A-edge) | - | 0.03 | 0.04-0.07 | 0.11-0.14 |
| Resolve, blit (after A-edge) | - | 0.03 | 0.04-0.07 | 0.07-0.12 |
| Depth resolve | - | 0.03 | 0.03 | 0.04-0.05 |

For the light lighting stand-in (sun + 4 lights, 0.10 ms per pixel at 1440p) the lighting
part of the comparison narrows, and the fill dominates. The 1080p table and the light
rows are in the bench output (section 9).

### GL limits [verified]

| Limit | NVIDIA 621.01 | Zink (Mesa 26.3.0-devel) |
|---|---|---|
| `GL_MAX_SAMPLES` | 32 | 8 |
| `GL_MAX_COLOR_TEXTURE_SAMPLES` | 32 | 8 |
| `GL_MAX_DEPTH_TEXTURE_SAMPLES` | 32 | 8 |
| `GL_MAX_INTEGER_SAMPLES` | 32 | 8 |
| `GL_MAX_DRAW_BUFFERS` | 8 | 8 |
| `glGetInternalformativ(GL_TEXTURE_2D_MULTISAMPLE, GL_SAMPLES)` for RGBA8, RGB10_A2, RGBA16, RGB16F, RGBA16F, R11F_G11F_B10F, RG16F, R8, R16F, R32UI, D24, D24S8, D32F | 2, 4, 8, 16, 32 | 2, 4, 8 |

### Known NVIDIA quirk [survey, documented on #321]

- A texture-fetched `gl_SampleMask` honours only the low 4 bits at 16 samples.
- The scene MSAA here never writes `gl_SampleMask` at 16, so it does not apply.
- The stochastic passes keep their cap of 8 (`ST_MAX_PASS_SAMPLES`).
- Stage 4's alpha-mask coverage must also stay at 8 or below, or use a computed (not
  fetched) mask.

## 6. Integration with stochastic transparency (#321, `codex/alpha-stochastic-2`)

Today [survey of `d2110f7af2`]:

- All stochastic targets are `GL_TEXTURE_2D_MULTISAMPLE` at P pass samples, P ≤ 8, the
  largest of 8/4/2 that is ≤ S and within the device limits, on one FBO `mAlphaSTFBO`:
  - `mAlphaSTAccum` RGBA16F;
  - `mAlphaSTTrans` RG16F (prod(1 − α), glow);
  - `mAlphaSTDepth` D24.
- `spreadAlphaStochasticDepth` copies the single-sample scene depth to every pass sample
  with a full-screen `gl_FragDepth` pass.
- `compositeAlphaStochastic` averages trans and accum over the P samples per pixel and
  blends into the single-sample scene.

Under scene MSAA with N samples (the owner's Aurora design, `oit_stochastic.rs`
`pass_samples` / `trans_samples`, `fs_composite` per `sample_index`) [inferred design, Aurora
read for design only]:

1. **Pass samples P = max(N, P today).** S/P iterations as now.
2. **Total alpha at the scene's N.**
   - `mAlphaSTTrans` gets N samples and its own FBO, with the **scene's MS depth** attached
     read-only (depth test, no writes).
   - The spread pass for it disappears.
   - Glow moves out of Trans green into a P-sample R16F target, or Accum's FBO, because
     Trans and Accum no longer share a sample count when P > N.
3. **Stochastic depth init.**
   - When P = N, `glCopyImageSubData` of the scene's MS depth replaces
     `alphaStochasticExpandF`, but only if `mAlphaSTDepth` is allocated in the scene's format.
     The scene depth becomes D24S8 while `mAlphaSTDepth` is D24 today, and the two are not
     copy-compatible (`GL_INVALID_OPERATION`). Either allocate `mAlphaSTDepth` as D24S8 on
     this path (same 4 B per sample), or keep the per-sample shader copy.
   - When P > N, `alphaStochasticExpandF` reads `sampler2DMS` and writes, per pass sample,
     the nearest scene sample's depth.
   - The nearest sample comes from the actual positions (`glGetMultisamplefv(GL_SAMPLE_POSITION)`).
     Aurora assumes the standard pattern instead.
   - Repeat this before each iteration, as now.
4. **Composite per scene sample, before the scene resolve.**
   - First box-resolve Accum (P → 1) with one blit, so that the composite does not loop over
     P per scene sample.
   - The composite then runs at sample rate into the MS scene. Each scene sample is dimmed
     by its own `texelFetch(trans, px, gl_SampleID)`, and the single per-pixel
     accumulated colour times the correction is added.
   - Transparent-over-opaque edges and the edges of transparent surfaces are thereby
     antialiased, and pass through the tonemap-weighted resolve with everything else.
5. **Single-sample path otherwise.** N = 1 keeps #321's code exactly as it is.
6. **Polygon offsets.**
   - The decal offsets (−1, −1..−3) were reasoned against the pixel-centre spread depth.
   - With the scene's own per-sample depth the reasoning changes. Re-run
     `test_alpha_stochastic.py`'s decal and slope cases at N = 2/4/8.
7. **Changes, by place:**
   - `allocateAlphaStochastic`: N, the split FBOs and the reuse check.
   - `beginAlphaStochastic` / `spreadAlphaStochasticDepth`: the copy (into a D24S8
     `mAlphaSTDepth`) or the per-sample expand.
   - `alphaStochasticExpandF.glsl`: `sampler2DMS`.
   - `alphaStochasticCompositeF.glsl`: a per-sample variant.
   - `compositeAlphaStochastic`: the Accum blit and sample rate.
   - `lldrawpoolalpha.cpp::renderStochastic`: the FBO for the total alpha phase.
   - Tests: `test_alpha_stochastic.py` MS-scene cases.
8. **Memory and cost.**
   - Trans at N adds 4 B × N per pixel: 1440p 4x +56 MiB, while Trans no longer needs P
     samples.
   - Aurora's measurement, for scale: "The cost under SMAA is that of MSAA 4x (p95 1600 x
     900 14.5 against off 10.3, 1278 x 1360 24.2 against 15.5, 2560 x 1440 29.6 against
     20.0)". This is the hair fly-through with 400 cards, S = 16 in passes of 8, Vulkan on
     the same GPU (Aurora `TASKS.md` ~line 1217).
   - Aurora also notes that with the passes at the scene's 4x, "S = 16 [runs] in two runs
     instead of four".
   - That is: with scene MSAA, the total alpha and the composite cost about what they cost
     without it, because they run at the samples that already exist [Aurora measurement,
     not re-measured here].

## 7. Settings, UI, feature tables and fallbacks

**New setting `RenderMSAASamples`** (U32: 0 off, 2, 4, 8; default 0):

- Persisted like the other render settings and added to `graphic_preset_controls.xml`, so
  presets save it.
- Presets saved before it exist do not list it and keep the user's value, as other
  later-added controls do [inferred from how presets apply listed controls only].
- A change triggers `handleReleaseGLBufferChanged`, as `RenderFSAAType` does
  (`llviewercontrol.cpp` ~1319) [survey].

**`RenderFSAAType` and `RenderFSAASamples` stay as they are**, as the post-process AA and its
quality. No migration is needed, so saved settings and presets cannot break.

- Pre-2011 settings files with `RenderFSAASamples` 2/4/8 are already clamped to quality 3
  today, and stay that way.
- The rejected alternative is one combobox (Disabled / FXAA / SMAA / MSAA 2x / 4x / 8x).
  It would need a migration of `RenderFSAAType`, would forbid MSAA + SMAA, and would change
  what presets mean.

**Preferences › Graphics** (`panel_preferences_graphics1.xml` ~1040-1100,
`floater_preferences_graphics_advanced.xml` ~410/445, `floater_phototools.xml` ~3440/3476)
[survey]:

- A third combobox, "Multisampling: Off / 2x / 4x / 8x", beside "Antialiasing" and
  "Antialiasing quality".
- Entries above `min(GL_MAX_COLOR_TEXTURE_SAMPLES, GL_MAX_DEPTH_TEXTURE_SAMPLES,
  GL_MAX_SAMPLES)` and above the feature-table ceiling are disabled.
- A tooltip states the memory cost at the current window size, from the arithmetic in
  `msaa_bench_model.msaa_memory`.

**Feature tables** (`featuretable*.txt`, value = ceiling) [inferred]:

| GPU class | Ceiling |
|---|---|
| All-features list | `RenderMSAASamples 1 8` |
| Low classes and `class0` | 0 |
| Middle classes | 2 |
| High classes | 4 |
| Top class | 8 (≥ 12 GB video memory) |

The recommended value stays 0 until the owner's in-world check, then 4 on the top class.
A feature-table change re-applies recommended settings once (`doc/graphics_settings_persistence.md`),
so land the table entry together with another table change [inferred].

**Fallbacks:**

- If allocation fails, halve the samples and retry, then 0, logged once. The Linden code
  did the same.
- Not with MSAA:
  - probe, hero-probe and impostor packs (`mRT != &mMainRT`);
  - `gCubeSnapshot`;
  - the material preview;
  - PPLL (falls back to sorted alpha until #321 removes it).
- Snapshots above the window size: keep the samples only while the memory arithmetic fits
  a budget (for example 25 % of free video memory, where `GL_NVX_gpu_memory_info` or
  `GL_ATI_meminfo` exists). Otherwise use fewer samples for the snapshot.

## 8. Notes for the Vulkan line

The OpenGL behaviour stays the reference, and an equivalence test of the OpenGL-free logic
(classification rule, resolve weights, memory arithmetic in `msaa_bench_model.py`) can be
shared [inferred throughout this section].

- **Stencil classification while sampling depth.** In Vulkan this is legal with separate
  depth and stencil layouts (`DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL`, core 1.1).
- **In wgpu** (the favoured base for the own renderer) a texture cannot be sampled and
  written as stencil in one pass. The wgpu port would classify into a separate edge mask,
  or use the compute-tiled option C, which is the natural fit there.
- **Sample counts.** Vulkan exposes per-format sample counts (Aurora reports 2/4/8, no 16, on
  this GPU). wgpu guarantees 1 and 4. 2 and 8 depend on adapter-specific format features.
  The UI must take its sample list from the device, as the OpenGL design takes it from
  `glGetInternalformativ`.
- **Depth resolve.** `VK_KHR_depth_stencil_resolve` (core 1.2) can resolve sample 0, min or
  max in the render pass. wgpu has no depth resolve, so the shader resolve of section 4.4
  is the portable form.
- **Sample-rate shading.** `sampleRateShading` and `SampleId` (Vulkan), and
  `@builtin(sample_index)` (wgpu), mirror `gl_SampleID` directly.
- **Colour resolve.** Render-pass resolve attachments do a box resolve only. The
  tonemap-weighted resolve stays a shader pass on every backend.

## 9. Bench and tests

- `scripts/perf/bench_deferred_msaa.py`: offscreen GL bench in the style of
  `bench_alpha_oit.py`, with no viewer launch.
  - It prints the limits and the per-format sample counts, then times every pass per
    resolution, sample count, scene and lighting weight, and prints the summary table of
    section 1.
  - Checks (exit 1 on failure):
    - FBO completeness;
    - A-edge against A-all;
    - B-all and B-edge against A-all + box;
    - the tonemap-weighted resolve against the CPU model.
  - Environment: `MSAA_BENCH_RES`, `MSAA_BENCH_SAMPLES`, `MSAA_BENCH_SCENES`,
    `MSAA_BENCH_LIGHTS`, `MSAA_BENCH_FRAMES`, `MSAA_BENCH_JSON`. Use `--opengl <Mesa
    opengl32.dll>` with `GALLIUM_DRIVER=zink` for Zink.
- `scripts/perf/msaa_bench_model.py`: the CPU side: layout and memory arithmetic, scenes,
  normal encoding, edge rule, box and tonemap-weighted resolves, frame estimate.
- `scripts/tests/test_msaa_bench_model.py`: its unittests (21).

Reproduce (Windows, with TEMP and TMP pointing to the Defender-excluded `.fixture-tmp`):

```
python scripts/tests/test_msaa_bench_model.py
python scripts/perf/bench_deferred_msaa.py
GALLIUM_DRIVER=zink MSAA_BENCH_FRAMES=10 python scripts/perf/bench_deferred_msaa.py --opengl H:\Dev\3p-mesazink\build\bin\release\opengl32.dll
```

Bench limits:

- The scenes are synthetic. Their edge fractions bracket, but do not measure, Second Life
  views.
- The G-buffer fill shader is trivial.
- The lighting is a stand-in, not `softenLightF`.
- Local lights, shadows, SSAO and water are not in the bench.
- Each frame is a single pass sequence, not a full viewer frame.
- The G-buffer fill at high sample counts on dense geometry varied by up to 1.7× between
  runs. The other passes varied by less than 10 %.

## 10. Staged plan with gates

Each stage lands as a feature PR from `vkstorm-devel` (`codex/msaa-*`), built
`RelWithDebInfo` with the release viewer's dependencies, and is integrated toward release
through its own integration PR. The owner's in-world check comes once, after release, on
the installed viewer.

**Stage 0: plumbing, about 4 to 6 days.**
- `LLRenderTarget` gets a sample count: `glTexStorage2DMultisample` for colour and
  `DEPTH24_STENCIL8`, `shareDepthBuffer`, `resize`, `bindTexture` on
  `GL_TEXTURE_2D_MULTISAMPLE`, `swapFBORefs`, and a `resolveTo()` blit helper.
- The `RenderMSAASamples` setting and its listener, the allocation fallback, and the
  exclusions (non-main packs, cube snapshot, preview).
- *Gate:* with MSAA = 0, the frame is bit-identical to today, by an offscreen image test and
  a G-buffer visualisation capture. A unit test covers the clamp and fallback logic (CPU
  side).

**Stage 1: G-buffer, classification, soften, resolve; post chain on resolved inputs; behind
the setting. About 8 to 12 days.**
- The MS G-buffer and scene colour.
- The classification pass with the full rule (depth, normal, flag, albedo). The cheaper
  depth-and-normal rule only after an image test of coplanar material boundaries.
- `softenLightF` with a sample index (`getGBuffer` taking a sample index); two draws.
- The depth resolve after the G-buffer.
- The tonemap-weighted resolve before `renderFinalize`.
- Luminance, exposure, tonemap, glow, DoF, FXAA/SMAA, present and SSR rebound to the
  resolved targets.
- Local lights at sample rate everywhere first (correct, simple).
- *Tests:* an offscreen image test in the style of `test_alpha_stochastic.py` that renders
  the viewer's own soften and resolve shaders on a synthetic G-buffer at N = 2/4/8. It
  checks:
  - simple pixels equal to the 1x result;
  - complex pixels equal to the per-sample reference;
  - the resolve against `msaa_bench_model`.
- *Measure:* viewer GPU timers (`RenderDebugGLTimers` or the existing profile CSV, with
  `scripts/perf/profile_cmp.py`) at 1080p and 1440p on three saved camera positions (open
  sim, city, forest) at 0/2/4/8 samples. Record the edge fraction from the classification's
  sample query.
- *Gate:* 4x at 1440p costs ≤ 2.5 ms over 1x on the city view; no new GL errors; the Zink
  run passes.

**Stage 2: local lights, light map, haze, water, sorted alpha. About 6 to 8 days.**
- Classified local lights: two draws per volume, or sample rate inside small volumes,
  chosen by measurement.
- The sun/SSAO light map per pixel from the resolved inputs.
- Haze and water haze reading the resolved depth; the depth copies removed.
- The water refraction from a box resolve.
- Sorted alpha, fullbright and glow into the MS scene.
- *Tests:* a water-edge image test and an alpha-edge image test. The edges must show
  intermediate coverage values; today they are binary.
- *Measure:* the local-light cost with 64 and 256 lights at 4x.
- *Gate:* no silhouette halo above a threshold in a shadow-edge test.

**Stage 3: stochastic transparency at scene samples (after #321 has landed). About 4 to 6
days.**
- Section 6's changes.
- *Tests:* `test_alpha_stochastic.py` MS-scene cases (exact cases, decal and slope);
  `bench_alpha_oit.py` with an MS scene.
- *Gate:* stochastic total cost at N = 4 ≤ its N = 1 cost + 10 %, and the transparent edges
  antialiased.

**Stage 4: alpha-mask coverage, mirrors, snapshots, presets and feature tables. About 5 to
8 days.**
- Shader-written coverage for alpha-masked materials (≤ 8 samples).
- Optional MSAA for hero probes (mirrors).
- The snapshot budget.
- Feature-table ceilings, preset entries, the Preferences combobox and tooltip, and the
  user documentation.
- *Gate:* a foliage view image test showing coverage on cutouts; snapshots at 2× the window
  size with 4x complete within budget.

**Total: about 27 to 40 working days**, plus the in-world check after release. The risk sits
in stages 1 and 2: the number of shaders that read the G-buffer through `texture()` (soften,
the light shaders, SSAO, haze, water, CoF), and the silhouette behaviour of the per-pixel
light map.

## 11. Sources

- Andrew Lauritzen, "Deferred Rendering for Current and Future Rendering Pipelines",
  SIGGRAPH 2010 Beyond Programmable Shading:
  https://www.intel.com/content/www/us/en/developer/articles/technical/deferred-rendering-for-current-and-future-rendering-pipelines.html
  (per-pixel shading of single-surface pixels, per-sample shading elsewhere, tiled).
- Nicolas Thibieroz, "Deferred Shading with Multisampling Anti-Aliasing in DirectX 10",
  ShaderX7, 2009 (stencil-restricted per-sample shading of edge pixels). Summarised at
  https://diaryofagraphicsprogrammer.blogspot.com/2009/06/multisample-anti-aliasing.html
  and https://www.realtimerendering.com/blog/shaderx7/. The chapter itself was not read.
- Brian Karis, "Tone mapping", 2013: https://graphicrants.blogspot.com/2013/12/tone-mapping.html
  (weight 1/(1 + luma), equivalent to tonemap, average, inverse).
- Timothy Lottes, "Optimized Reversible Tonemapper for Resolve", GPUOpen, 2016:
  https://gpuopen.com/learn/optimized-reversible-tonemapper-for-resolve/ (max3 form used
  here).
- Matt Pettineo, "Experimenting with Reconstruction Filters for MSAA Resolve":
  https://therealmjp.github.io/posts/msaa-resolve-filters/ and "A Quick Overview of MSAA":
  https://therealmjp.github.io/posts/msaa-overview/
- Jorge Jimenez et al., "SMAA: Enhanced Subpixel Morphological Antialiasing", Eurographics
  2012: https://www.iryoku.com/smaa/ (S2x, 4x modes).
- Khronos, `ARB_texture_multisample`, `ARB_sample_shading`:
  https://registry.khronos.org/OpenGL/extensions/ARB/ARB_texture_multisample.txt,
  https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sample_shading.txt
- Linden Lab / Firestorm history (LGPL-2.1), local clone `phoenix-firestorm`, commits
  `4353eeb928`, `2dd8ce53e4`, `7ee10ae1de`, `f558d86578`, `958afaa7a7`. Upstream:
  https://github.com/secondlife/viewer
- Vulkanstorm #321 (`codex/alpha-stochastic-2`, `d2110f7af2`), `doc/oit/stochastic-transparency.md`
  on that branch. Aurora `origin/feat/alpha-oit` `5ec4bd8c`, `oit_stochastic.rs` and
  `TASKS.md`, the owner's design, read for design only.
