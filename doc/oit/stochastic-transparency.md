# Stochastic transparency

Vulkanstorm's order-independent transparency (OIT) method since 2026-11-05: depth-based
stochastic transparency, which replaced PPLL. The viewer has two alpha methods,
`RenderAlphaSortMethod` 0 (sorted alpha, the per-object order of the Linden viewer) and 1
(stochastic transparency, the default).

## Provenance

- The algorithm: Enderton, Sintorn, Shirley and Luebke, "Stochastic Transparency", I3D 2010,
  section 3.4 (the anti-aliased depth-based form), with the paper's footnote 1 for more samples
  than a pass holds.
- The design and its constants come from the owner's own implementation in the Aurora Viewer,
  branch `feat/alpha-oit` of github.com/anne-skydancer/Aurora-Viewer (the commits by Anne
  Skydancer since `90f68a8`, notably `e5950ced` the method, `9424d986` the passes at their own
  sample count and the alpha-only shading, `887dafa4` the glow, `2692cbfd` every anti-aliasing
  mode, `1e32ccb5`/`e5950ced` the face identity, and `64e09ebb` with the measurements that
  replaced the per-pixel lists). Their author brings that work into Vulkanstorm under its dual
  licence (LGPL-2.1-or-later or GPL-3.0-or-later). What was taken: the pass structure, the mask
  table generator (`oit_stochastic.rs` `mask_table`), the mask seeding (`object.wgsl`
  `st_mask`, `st_sub_mask`), the face identity hash (`types.rs` `oit_tie`), the composite and
  glow resolve (`oit_stochastic.wgsl`), the single-sample depth spread
  (`oit_stochastic_expand.wgsl` `fs_expand_ss`). No other Aurora code (GPL-3.0-only, by other
  authors) was used: the OpenGL resources, the passes in `LLDrawPoolAlpha`, the shader variants
  and the polygon offsets are written for this renderer.
- The decision: the owner, 2026-11-05, "stoch and ppll don't go well together. got to be 1 or
  the other. stoch is less expensive in my experience with aurora, and comparable quality with
  the algorithm used there."

## The method in this viewer

The deferred scene is single-sampled (FXAA or SMAA run after it), so the passes over the blended
faces run on their own multisampled targets, as Aurora does without MSAA:

| Target | Format | Use |
|---|---|---|
| accumulation | RGBA16F, P samples | alpha x colour and alpha, additive |
| transparency | RG16F, P samples | red: prod(1 - alpha), cleared to 1; green: the faces' glow |
| stochastic depth | DEPTH_COMPONENT24, P samples | the opaque depth spread, then the faces' stochastic depth |
| mask table | R32UI, 2048 x (S + 1) | row r: 2048 random r-subsets of the S sample bits |

P, the samples of one pass, is the most the device takes for the three formats up to 8 (NVIDIA
621.01 honours only the low 4 bits of a texture-fetched `gl_SampleMask` at 16 samples). S, the
samples in all, is `RenderAlphaStochasticSamples` (4, 8 or 16); S / P iterations run the depth
and accumulation passes with the next P bits of the same mask. Memory: 16 B per sample, 128 B per
pixel at P = 8 (253 MiB at 1920x1080, 450 MiB at 2560x1440), allocated at the render resolution
while the method is selected and freed otherwise; nothing grows with the depth of the scene.

`LLDrawPoolAlpha::renderStochastic` (`pipeline.cpp` holds the targets, the spread and the
composite):

1. the GLTF scene into the scene depth (as before), the targets cleared, the opaque depth spread
   to every sample (`alphaStochasticExpandF.glsl`);
2. **total alpha**: every captured draw through its program's alpha-only variant into the red
   of the transparency target, blend ZERO, ONE_MINUS_SRC_ALPHA, against the spread depth;
3. per iteration: **stochastic depth**, the alpha-only variants writing `gl_SampleMask` with
   R = floor(alpha S + xi) samples (`alphaStochasticF.glsl`), depth only, LEQUAL; then the
   **accumulation**, the ordinary programs (full shading, early depth test kept) blended
   SRC_ALPHA, ONE / ONE, ONE where the face is at or in front of the stochastic depth, and the
   glowing faces' glow (`emissiveF.glsl`, `pbrglowF.glsl`, which now write the glow in green as
   well) into the green of the transparency target; the depth spread again before the next
   iteration;
4. **composite** (`alphaStochasticCompositeF.glsl`) over the scene: the scene dimmed by the
   pixel's transparency (its glow in alpha dimmed alike), the accumulated colour added times the
   alpha correction (total alpha over accumulated alpha); the faces' glow, corrected the same
   way, added to the scene alpha;
5. as with PPLL: the rigged faces into the scene depth, then the residual draws (particles,
   custom blend functions) in the stock path, then the depth-of-field depth pass.

The alpha-only variants (`LLGLSLShader::mStochasticVariant`, built with `ALPHA_STOCHASTIC` by
`make_stochastic_variant` in `llviewershadermgr.cpp`) exist for the alpha programs the pool
binds: simple alpha, fullbright alpha, the blended legacy materials and PBR alpha, static and
rigged. Each returns as soon as its alpha is known, before any texture other than the base map is
fetched (Aurora's `st_material_alpha`, commit `9424d986`: the driver did not drop the other
fetches by itself): base alpha, the alpha-mask cutoff and the vertex alpha, nothing else. The
simple-alpha variant takes no shadow sample; the material variant leaves out the glare that an
environment map or a highlight adds to the shaded alpha (the accumulation keeps it, and the alpha
correction scales the colour to the variants' total alpha). In those two passes a variant, once
fully bound in a frame, is bound without the light function, shadow maps and reflection probes,
and the materials' normal and specular maps are not bound. The ordinary programs are unchanged, so sorted alpha draws as before.

The coverage mask of a fragment is seeded by its pixel, its face's identity
(`LLDrawInfo::mStochasticSeed`: the object's UUID and texture entry hashed, the same on every
launch) and its triangle (`gl_PrimitiveID`), so the noise is fixed in screen space and
uncorrelated between faces and between the triangles of one face.

Depth: the opaque depth is the pixel centre's on every sample, a face's depth the sample's own.
A face lying on an opaque surface (a decal) would lose half its samples, so the passes pull the
faces forward by one depth-slope unit (`glPolygonOffset(-1, -1)`); the accumulation takes one
depth unit more and the glow (other vertex shaders) one more again, so that a face always passes
at the samples it won.

## What takes the method, and what does not

Exactly the draws PPLL captured, with the same exclusions:

| Draw | Method |
|---|---|
| alpha-blended faces of the main view, post-water pool: simple, fullbright, legacy materials, PBR/glTF blend, rigged and static | stochastic transparency |
| their glow | accumulated with them (PPLL re-added it unsorted after its resolve) |
| particles, faces with a custom blend function | stock path after the composite (as PPLL) |
| pre-water alpha, mirrors and hero probes, water reflections, cube snapshots, HUDs, impostors | sorted alpha (as PPLL) |
| selection highlight ("show transparent" and the selection outline) | stock path after the composite (as PPLL) |
| depth of field depth | stock pass after everything (as PPLL) |

## Settings

| Setting | Values | Default |
|---|---|---|
| `RenderAlphaSortMethod` | 0 sorted alpha, 1 stochastic transparency | 1 |
| `RenderAlphaStochasticSamples` | 4, 8, 16 (values in between take the next) | 16 |
| `RenderAlphaStochasticAvailable` | runtime flag, not persisted | |

Preferences › Graphics › Rendering 2: "Alpha rendering method" (Sorted alpha / Stochastic
transparency) and "Samples per pixel" (4, 8, 16). Users who had chosen PPLL (1) get stochastic
transparency; `RenderAlphaOITNodesPerPixel`, `RenderAlphaOITMemoryMB`, `RenderAlphaOITStats`
and `RenderAlphaPPLLAvailable` are gone (a stale value in a settings file is ignored). Neither
the method nor its samples are in the feature tables or the graphics presets, as PPLL's were
not. 16 samples follow Aurora's default, which stays 16 there until the owner decides. Aurora's
findings: S = 8 (one run, one accumulation) costs what OIT off does on its hair fly-through,
with a faint speckle on dark hair cards and more grain on broad glass; S = 16 runs the
accumulation twice. The bench below finds the same here (S = 8 0.73-0.81x sorted alpha, S = 16
1.39-1.47x).

Requirements: Windows or Linux, OpenGL 4.0 (GLSL 4.00 for `gl_SampleMask` and
`gl_PrimitiveID`, multisample textures), at least 2 samples for the three formats, and every
alpha-only variant built. Otherwise, or when the targets cannot be allocated, sorted alpha runs
(logged once).

## Tests and measurements

- `scripts/perf/test_alpha_stochastic.py`: offscreen, the viewer's shaders with the viewer's pass
  structure: the mask table, every covered sample against a Python model of the mask (16 in two
  passes of 8, 8, 4), the total alpha, visibility and composite, an opaque occluder, the glow,
  a decal on a sloped plane with and without the offset. NVIDIA 621.01 and Mesa Zink
  (`GALLIUM_DRIVER=zink`, `--opengl <Mesa opengl32.dll>`).
- `scripts/perf/bench_alpha_oit.py`, implementation `st`: the viewer's shaders and passes on the
  bench's synthetic layers, beside the round-2 study's variants (`stoch+...`, `stoch1`, `hybrid`),
  against `direct` (sorted alpha) and `slots` (PPLL, from git ref `a5290e8c81`), GPU time only;
  `OIT_BENCH_SHADE=heavy` adds a proxy of the alpha programs' shading cost to every
  implementation's shaded pass.

### Against sorted alpha, with shading (the comparison that matters)

Sorted alpha (method 0, "OIT off") shades every blended fragment once. The stochastic
accumulation shades only the fragments at or in front of the stochastic depth (the early depth
test stays on), and the alpha-only passes shade for alpha alone, so the figures depend on what a
fragment costs. The bench cannot run the viewer's alpha programs (their lighting, probes and
atmospherics need the whole viewer); `OIT_BENCH_SHADE=heavy` gives the shaded pass of every
implementation (`direct`, which is sorted alpha; the PPLL capture; the accumulation) a proxy of
that cost: diffuse, normal and specular fetches, a 4-tap shadow, eight scattered probe lookups,
seven point lights and fog. It does not cover draw calls: each blended draw is issued once by
sorted alpha, five times by stochastic transparency at S = 16 (total alpha, two depth and two
accumulation runs) and three times at S = 8, CPU time the bench does not see.

RTX 5070 Ti, NVIDIA 621.01, GPU ms per frame, 20-frame medians, `OIT_BENCH_SHADE=heavy`, the
bench's 19 scenes (with `deep` and `occl` from the round-2 bench):

| Scene | Sorted alpha | PPLL | S = 16 | S = 8 |
|---|---|---|---|---|
| 1080p full, 8 layers | 2.64 | 2.97 | 5.80 | 3.11 |
| 1080p full, 32 layers | 10.57 | 12.13 | 10.83 | 5.87 |
| 1080p sparse, 24 layers | 2.04 | 2.27 | 2.92 | 1.73 |
| 1080p foliage (hair-like cards), 32 layers | 6.37 | 7.96 | 7.68 | 4.32 |
| 1080p occluded, 8 layers | 2.65 | 2.89 | 5.12 | 2.75 |
| 1080p, sum of 19 scenes | 85.9 | 99.2 | 119.3 (1.39x) | 65.7 (0.73x) |
| 1440p full, 8 layers | 4.57 | 5.16 | 10.41 | 5.57 ¹ |
| 1440p full, 32 layers | 18.51 | 21.14 | 21.11 | 10.77 |
| 1440p sparse, 24 layers | 3.54 | 3.98 | 5.02 | 2.97 |
| 1440p foliage, 32 layers | 11.01 | 13.83 | 15.04 | 8.33 |
| 1440p occluded, 8 layers | 4.57 | 5.00 | 9.29 | 4.96 |
| 1440p, sum of 19 scenes | 149.7 | 173.2 | 220.7 (1.47x) | 120.8 (0.81x) ¹ |

¹ Measured alone. In the 19-scene run this one scene reads 7.15 ms at 1440p S = 8 (its depth and
composite passes about twice as long; twice reproduced in that sequence, never alone), which the
sum includes; without it the sum is about 119.2 (0.80x). The trivially shaded runs show the same
on the same scene in some configurations.

Summed, S = 8 costs less than sorted alpha (most where the layers are many; at 8 full-screen
layers about the same or a little more), as Aurora found on its hair fly-through; S = 16 runs the
shaded accumulation twice and costs about 1.4-1.5x. The split per pass, total alpha / depth /
accumulation / composite, ms:

| Scene | S = 16 | S = 8 |
|---|---|---|
| 1080p full, 8 layers | 0.24 / 1.34 / 4.04 / 0.19 | 0.24 / 0.66 / 2.02 / 0.19 |
| 1080p foliage, 32 layers | 0.72 / 2.48 / 4.29 / 0.19 | 0.72 / 1.29 / 2.14 / 0.17 |
| 1440p full, 8 layers | 0.42 / 2.67 / 6.97 / 0.34 | 0.42 / 1.30 / 3.51 / 0.35 ¹ |
| 1440p foliage, 32 layers | 1.28 / 5.96 / 7.47 / 0.33 | 1.27 / 3.05 / 3.71 / 0.30 |

The bench has no glowing layers; in the viewer the glow pass runs only for groups with a glowing
blended face, and the glow composite only when one was drawn.

The alpha-only shading of the total alpha and depth passes, before (`OIT_BENCH_ST_ALPHAONLY=0`:
the proxy's full cost in those passes) and after, sum of 19 scenes: 1080p S = 16 126.2 to 119.3
ms, S = 8 71.0 to 65.7; 1440p S = 16 227.9 to 220.7, S = 8 128.7 to 120.8 (3 to 7 %). This is the
cost of shading those passes in full, not of what a given driver would have left in. The CPU-side
reductions (no light, shadow, probe or material-map binds in the alpha-only passes) cannot be
measured offline.

Not done, Aurora's listed next step: one accumulation testing both runs' depths in the shader,
which would halve the accumulation at S = 16. Here that means every alpha program reading both
stochastic depths per sample in its fragment shader, giving up the early depth test the
accumulation relies on; it is left for a later change.

### Against PPLL, trivially shaded

With a trivial fragment shader (`OIT_BENCH_SHADE` unset), PPLL = `slots`, 19 scenes:

| Scene | 1080p PPLL | 1080p S=16 | 1080p S=8 | 1440p PPLL | 1440p S=16 | 1440p S=8 |
|---|---|---|---|---|---|---|
| full, 8 layers | 0.44 | 3.37 ¹ | 1.84 | 0.77 | 6.30 | 3.42 ¹ |
| full, 32 layers | 1.86 | 9.57 | 5.16 | 3.55 | 17.89 | 9.61 |
| sparse, 24 layers | 0.38 | 2.50 | 1.49 | 0.67 | 4.39 | 2.63 |
| foliage, 32 layers | 4.42 | 6.89 | 3.87 | 8.42 | 13.73 | 7.63 |
| sum of 19 scenes | 23.2 | 92.7 (4.0x) | 50.0 (2.2x) | 44.9 | 178.1 (4.0x) | 96.9 (2.1x) |

¹ Measured alone (in the 19-scene run 4.87 and 5.03, the placement effect above); the sums are the
19-scene runs'.

Mesa Zink on the same GPU, 1080p, S = 16: 3.4x PPLL summed. In short: on trivially shaded scenes
stochastic transparency costs more GPU time than PPLL's fixed slots (4.0x at S = 16, 2.1-2.2x at
S = 8), least where PPLL overflows its 8 slots (foliage, 1.6x at S = 16); it is noisier than PPLL
where PPLL is exact (up to 8 layers: RMSE 4.0 against 0.7 steps of 8 bits at S = 16) and better
where PPLL overflows (32 layers: 3.9 against 12.2; foliage: 1.0 against 11.2).
