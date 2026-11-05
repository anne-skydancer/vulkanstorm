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
rigged. Each returns as soon as its output alpha is known; the material variant shades fully
only when glare can raise the alpha (an environment map or a glossy highlight on a translucent
diffuse alpha). The ordinary programs are unchanged, so sorted alpha draws as before.

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
not. 16 samples follow Aurora's default (its finding: S = 8 shows a faint speckle on dark hair
cards, S = 16 less); 8 halves the cost of the passes over the faces.

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
  bench's synthetic full-screen layers against `slots` (PPLL, from git ref `a5290e8c81`). It
  measures GPU time with a trivial fragment shader: it leaves out shading cost, draw calls and
  skinning, which the real alpha programs add once per accumulation pass (twice at S = 16) and,
  for the alpha-only variants, at a fraction of the cost.

Bench, RTX 5070 Ti, NVIDIA 621.01, GPU ms per frame (20-frame medians), PPLL = `slots`:

| Scene | 1080p PPLL | 1080p S=16 | 1080p S=8 | 1440p PPLL | 1440p S=16 | 1440p S=8 |
|---|---|---|---|---|---|---|
| full, 8 layers | 0.43 | 3.35 | 1.84 | 0.78 | 6.24 | 3.40 |
| full, 32 layers | 1.85 | 9.47 | 5.12 | 3.47 | 17.72 | 9.53 |
| sparse, 24 layers | 0.38 | 2.50 | 1.49 | 0.67 | 4.39 | 2.62 |
| foliage, 32 layers | 4.05 | 6.83 | 3.86 | 8.30 | 13.62 | 7.58 |
| sum of 17 scenes | 21.7 | 84.7 | 47.2 | 44.0 | 157.4 | 86.6 |

Mesa Zink on the same GPU, 1080p, S = 16: 4.8x PPLL summed. In short: on these scenes stochastic transparency costs more GPU time than
PPLL's fixed slots (summed over the 17 scenes, 3.9x at 1080p and 3.6x at 1440p at S = 16; 2.2x and
2.0x at S = 8), least where PPLL overflows its 8 slots (foliage, 1.6x to 2.2x at S = 16); it
is noisier than PPLL where PPLL is exact (up to 8 layers: RMSE 4.0 against 0.7 steps of 8 bits
at S = 16) and better where PPLL overflows (32 layers: 3.9 against 12.2; foliage: 1.0 against
11.2).
