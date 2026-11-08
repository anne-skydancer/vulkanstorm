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
samples in all, is `RenderAlphaStochasticSamples`, a debug setting that takes 4, 8 or 16; 8 is the default on every line and there is no user-facing choice (see Settings); S / P iterations run the depth
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
simple-alpha variant takes no shadow sample. The legacy material variant does so whenever its
uniforms rule out glare (no environment intensity, no glossiness) or its diffuse is opaque; a
material that can glare (shiny glass, whose reflections raise the shaded alpha above a clear
diffuse) runs the full shading in those passes and reports the shaded alpha, so its reflections
keep their coverage. In those two passes a variant, once
fully bound in a frame, is bound without the light function, shadow maps and reflection probes,
and the materials' normal and specular maps are not bound, unless the face's material can glare:
then the variant shades in full and is given what the shaded pass is given (the light function,
shadow maps and probes, and the face's own normal map, whose alpha scales the glossiness, and
specular map, whose alpha scales the environment intensity), so that its alpha is the shaded
pass's alpha. The ordinary programs are unchanged, so sorted alpha draws as before.

Until 1.0.1.81943 those two maps were never bound in the alpha-only passes, so a glaring material
read the maps another face had left on those units: a near-invisible blended layer whose own maps
rule out glare (a mesh body's tattoo or applier layer with a glossy material) got coverage there
but none in the accumulation, and the composite dimmed the deferred-lit skin under it, a
shadow-like band with a hard edge at the layer's outline (fixed 2026-11-05;
`scripts/perf/test_alpha_stochastic.py` runs materialF.glsl itself both ways).

The coverage mask of a fragment is seeded by its pixel, its face's identity
(`LLDrawInfo::mStochasticSeed`: the object's UUID and texture entry hashed, the same on every
launch) and its triangle (`gl_PrimitiveID`), so the noise is fixed in screen space and
uncorrelated between faces and between the triangles of one face.

Depth: the spread gives each sample the opaque surface's depth at that sample (the pixel
centre's depth extended along the surface's screen-space gradient, taken from the neighbour on
the same side of any depth edge), and a face's depth is the sample's own, so a face meets the
opaque surface sample for sample, as sorted alpha meets it at the pixel centre. The passes pull
the faces forward by a 256th of their depth slope and one depth unit (`glPolygonOffset(-1/256,
-1)`), the rounding of the spread and the rasteriser, so that a face lying on an opaque surface
(a decal) keeps every sample; the accumulation takes one depth unit more and the glow (other
vertex shaders) one more again, so that a face always passes at the samples it won.

Until 1.0.1.81943 the spread gave every sample the pixel centre's depth and the passes pulled the
faces forward by a whole slope unit (`glPolygonOffset(-1, -1)`). That let a blended face up to one
pixel's depth step behind an opaque surface show through it, where sorted alpha (and Aurora, whose
passes have no depth bias) hide it, for instance a blended layer just inside a surface it should
stay under (fixed 2026-11-05; `scripts/perf/test_alpha_stochastic.py` checks faces a third and a
sixteenth of a pixel behind sloped planes, and that the old passes show them on every pixel). The spread now runs once
per sample (it reads `gl_SamplePosition`): 0.26 ms instead of 0.03 ms per spread at 2560x1440 with
8 samples on an RTX 5070 Ti (NVIDIA 621.01), run once at S = 4 or 8 and twice at S = 16.

## Temporal accumulation (`RenderAlphaStochasticTemporal`, always on)

The accumulation is part of the method, not a user choice: it is not in Preferences. The debug setting
exists for debugging only, and is not persisted (see Settings).

8 samples in one run of the passes, with the noise averaged over the frames, instead of 16 samples in
two runs: the second run of the stochastic depth and accumulation is gone. The design and the weights
are the author's Aurora Viewer's (feat/alpha-oit, `oit_stochastic.wgsl` `reprojected` and `cs_resolve`).

- The masks' seeds change every frame (`st_frame` in the seed hash, only with the accumulation on; the
  face identities stay stable, so layers keep their roles).
- `alphaStochasticResolveF.glsl`, one full-screen pass after the accumulation: this frame's accumulated
  colour (alpha x colour, alpha) averaged over the samples, blended 1/8 with the last frame's blended
  colour taken where this pixel's nearest transparent surface was (the minimum of the stochastic depth
  over the samples), moved by the camera alone (this frame's clip space to the last frame's, from the
  matrices the pipeline keeps itself, `mAlphaSTLastModelView` and `mAlphaSTLastProjection`) and clamped
  to the 3 x 3 minimum and maximum of this frame, as a TAA's history is. The result is the next frame's
  history and the composite's accumulated colour; the alpha correction there is this frame's total alpha
  over its alpha, so an alpha change is never ghosted. The glow keeps the current frame's accumulation.
- No history on the first frame, after a resize, when the setting or the method is switched, or when the
  frame before did not run the method (the history stamp is the frame counter).
- Memory: two full-size RGBA16F colours (the history and the latest), 16 bytes a pixel: 33 MB at 1920 x
  1080, 59 MB at 2560 x 1440, allocated only while the setting is on. The passes' own targets are as
  before.
- Not reprojected: the surfaces' own motion (an avatar's hair in motion, an animated texture): there the
  history ghosts, limited by the clamp.

Offline (`test_alpha_stochastic.py`, three layers of alpha 0.2, 0.4, 0.5, rmse/255 against exact, NVIDIA
and Mesa Zink): 8 samples 5.77, 16 samples 3.74, 8 samples accumulated 5.91 at frame 1, 2.22 at frame 8,
1.60 at frame 16, 1.56 at frame 40; the image changes by 0.95 between frames, against 6.99 for 8 re-seeded
samples; after a 3 pixel camera move the columns at the edges of the layers read 1.30 with the history
reprojected and 2.69 without; after an alpha change from 0.2/0.4/0.5 to 0.05/0.1/0.1 the first frame reads
1.01 (0.81 without the history).

Hair-like random strips (10 layers, alpha 0.2 to 0.6, rmse/255 against sorted alpha at every sample; plain
8 samples 4.7 to 5.4, plain 16 samples 3.5 to 4.0): a camera pan of 1 or 4 pixels a frame, history
reprojected, 2.9 and 2.5; surfaces moving 0.25, 0.5, 1 and 4 pixels a frame, not reprojected, 10.9, 13.1,
14.8 and 15.1: the history ghosts there, three times the plain 8 samples. A variance clip of the history
(mean plus or minus 0.75 deviations inside the minimum and maximum) gives 11.2 at 2 pixels a frame against
15.6 for the minimum and maximum, and slows the convergence.

Cost: the resolve pass alone, 0.5 to 0.7 ms at 1920 x 1080 and 1.7 to 2.0 ms at 2560 x 1440 on a machine
under load (about the composite's own cost); the second run of the passes it replaces is, in the shaded
bench above, 2.6 to 3.3 ms at 1080p and 4.8 to 6.7 ms at 1440p.
## Edge pixels: attributes at the centroid (fixed 2026-11-05)

Symptom (release 1.0.1.81947, rigged mesh hair close up in a dim interior): a fine regular lattice
of thin, slightly redder lines over the hair, one line along every quad of the mesh ("i can see the
matrix"). Its cause is not the noise. The passes draw into multisampled targets, and a pixel that a
triangle covers in part is shaded once for that triangle, with every varying interpolated at the
pixel centre unless it is declared `centroid`. When the centre lies outside the triangle the value is
extrapolated. Sorted alpha is single-sampled and only shades a pixel whose centre is inside, so
there the texture coordinates never leave the triangle. Hair cards whose quads have UV islands of
their own (the UV jumps at every quad edge) therefore showed the texels beyond each island along
every quad edge, in the stochastic passes only. The alpha programs' fragment inputs (`alphaF`,
`fullbrightF`, `pbralphaF`, `materialF`, `emissiveF`, `pbrglowF`) are now `centroid in`; for a
single-sampled target that changes nothing, for the passes' samples the attributes are taken inside
the triangle. `scripts/perf/test_alpha_stochastic.py` draws three layers of 8-pixel quads with UV
islands (grey inside, red outside): sorted alpha shows the outside on 0 of 2048 pixels, the
stochastic passes without `centroid` on 273, 664 and 686 pixels (S = 4, 8, 16), with it on 0 (NVIDIA
621.01 and Mesa Zink alike, the vertex shaders' outputs staying as they are).

The cause was found by elimination first, with offline experiments on the masks (next section): no
structure in the noise, at any S, even on regular quad lattices, as long as every fragment has its
own (face seed, triangle) pair. The lattice in the screenshot has the rectangular cells of the
quads, no diagonals, and a reddish tone, which is the signature of extrapolated texels.

## Noise: what the paper recommends, what we do, what was measured

Reference: the journal version, Enderton, Sintorn, Shirley and Luebke, "Stochastic Transparency",
IEEE TVCG 17(8), 2011 (section numbers below are its). The method here is its depth-based,
anti-aliased form (3.4, 3.5), kept.

| Recommendation | Here |
|---|---|
| 3.1 stratified masks: R = floor(alpha S + xi) samples, one S-choose-R subset | done (a table of 2048 random R-subsets per R) |
| 3.1 dither R across a tile for further stratification | done per pixel with a white xi; a 64 x 64 blue-noise tile for xi measured: no gain (below), not adopted |
| 3.3 alpha correction, total alpha over accumulated alpha | done (composite); a single layer is exact (test) |
| 3.4 steps 1-5: opaque depth, total alpha, stochastic depth only, accumulation by the stochastic depth, composite | done; deviations: the opaque depth is spread over the samples (the scene is single-sampled), and the composite dims the scene by the pixel's mean transparency, not per sample (differs only on background edges inside a pixel) |
| 3.5, footnotes 3 and 4: more than 8 samples by iterating, one stratified mask split across the passes | done (S = 16 is two passes of 8 of one 16-bit mask) |
| 5 mask table of about 2047 masks | done (2048) |
| 6 temporal coherence: seed by pixel and primitive id | done (pixel, face identity, triangle); the noise field is fixed in screen space |
| 6 uneven noise at edges; alpha correction makes one-layer regions noise free | known properties |
| 6 post processing: cross-bilateral filter on the total alpha | measured, rejected (below) |
| 6 adaptive sampling, importance sampling for dark backgrounds | not adopted: S is a setting |
| 6 mask selection: a pair of random S-choose-R masks does reasonably well | done; masks of fragments that differ in (face seed, triangle) are independent |

Measurements (offline, the viewer's own shaders and pass structure on NVIDIA 621.01, 256 x 256,
hair-like stacks of N = 5, 10, 20 layers of alpha 0.2 to 0.6 in one draw call, so every triangle has
its own id; a lattice of 8-pixel quads shifted per layer, or random thin strips; the reference is
sorted alpha at every sample; the colour error is in 1/255 of full scale, "lp" is the rms after a
Gaussian low-pass of sigma 1.5 pixels, the closer measure of what the eye sees):

| Layout | S | N = 5 rms / lp | N = 10 rms / lp | N = 20 rms / lp |
|---|---|---|---|---|
| quads | 4 | 9.3 / 2.3 | 9.6 / 2.9 | 9.3 / 2.5 |
| quads | 8 | 5.8 / 1.3 | 6.9 / 1.7 | 6.4 / 1.5 |
| quads | 16 | 3.7 / 0.8 | 4.9 / 1.1 | 4.4 / 0.9 |
| strips | 4 | 6.5 / 1.7 | 8.5 / 2.0 | 10.9 / 2.7 |
| strips | 8 | 3.7 / 1.4 | 5.3 / 1.6 | 7.5 / 2.1 |
| strips | 16 | 2.9 / 1.4 | 4.0 / 1.5 | 5.6 / 1.9 |

- No spectral peak in the noise: the error's power spectrum has a maximum of 7 to 13 times its mean
  for the quads (white noise of the same size: 12), at every S and N. The strips are lower in
  frequency (up to 55 times at S = 16, N = 5, where the error is a few grey levels).
- An independent per-fragment hash of (pixel, face seed, triangle), in place of the shared pixel
  field rotated per triangle that `alphaStochasticF.glsl` uses, changes nothing: rms and lp within
  0.5 %. The same with a 64 x 64 blue-noise tile (void and cluster, sigma 1.5) for xi, shifted per
  triangle: within 1.5 % at every S and N. The error is dominated by the overlap of the masks of
  different layers, which no per-pixel pattern controls (paper, section 6: "it would seem difficult
  to improve on that significantly"). Rejected: a texture and a binding for nothing.
- Masks that coincide (the same subset on two layers over their overlap) make the layers occlude each
  other instead of blending: a bias of 27 to 44 grey levels and spectral peaks 100 to 600 times the
  mean on a quad lattice. This does happen: the subset of the mask table was the column of the pixel's
  value plus the layer's rotation, so two layers whose rotations met on one of the 2048 columns (about one
  pair in a thousand, many pairs among hundreds of hair cards) shared a subset over their whole overlap,
  and the layers behind them saw too few covered samples. The subset is now hashed from the seed and the
  pixel (the Aurora Viewer's fix of 2026-11-06, the author's own); the test puts a pair of colliding seeds
  over a third layer: the back layer reads 0.219 where the exact 0.125, now 0.120 to 0.124 (S = 4, 8, 16).
- A cross-bilateral filter guided by the total alpha (radius 1 or 2, sigma_alpha 0.02 to 0.1): on a
  layout whose colour varies only where the total alpha does, it cuts the rms by 60 % but the part
  of the noise the eye sees (lp) by 5 %; on random strips, where the colour has detail that the
  total alpha does not carry, it blurs: the rms goes from 5.3 to 4.8 at radius 1 and up to 5.9 to 6.6
  at radius 2, where the blur of the exact image alone is 5.5. Rejected, as the paper says (6).
- The bias of alpha correction on deep stacks (3.3, 4.1) is 0.5 to 3.5 grey levels at N = 10 to 20
  and S = 4 to 16 (the "bias" of the same runs): the price of its noise reduction, as the paper
  finds.
- S is the lever: the rms falls with S (quads, N = 10: 9.6, 6.9, 4.9 at S = 4, 8, 16), at the cost
  of the bench below.

## Aurora Viewer's implementation (the author's, feat/alpha-oit)

Compared point by point (`crates/aurora-render/src/oit_stochastic.rs`, `shaders/object.wgsl`
`st_mask`, `st_pixel_values`, `st_sub_mask`; all of it by Anne Skydancer): the same `st_hash` (PCG),
the same seed (face identity, triangle), the same rotation of two per-pixel values (the dither of R
and the column of the table), the same table (2048 columns, splitmix64 and a partial Fisher-Yates),
the same S = 16 in passes of at most 8 of one mask, the same alpha correction and composite, and the
same default (white pixel values, triangle in the seed, no filter). Aurora has the options measured
above, off by default and not changed by what was found here: `seed=r2|blue` (R2 or a blue-noise
tile), `noprim`, `filter=<r>[:<sigma>]` (cross-bilateral), `temporal`, `naive`, `nocorr`. So nothing
in the mask logic separates the two. What may separate them is the shading of the edge pixels: WGSL
`@location` inputs are interpolated at the pixel centre as GLSL's are without `centroid`; whether a
Vulkan driver takes them at the centre or inside the triangle is left to the implementation, and
Aurora's hair may simply not hit it (not tested). Nothing was ported from Aurora in this fix; the
passes and masks come from it as before.

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
| `RenderAlphaStochasticSamples` | debug only, not persisted: 4, 8, 16 (values in between take the next) | 8 |
| `RenderAlphaStochasticTemporal` | debug only, not persisted: accumulate over the frames | TRUE |
| `RenderAlphaStochasticAvailable` | runtime flag, not persisted | |

Preferences › Graphics › Rendering 2: "Alpha rendering method" (Sorted alpha / Stochastic
transparency) only. Stochastic transparency is the same on every line (owner decision, 2026-11-08):
8 samples with the temporal accumulation, and no other settings. The two settings above are
debug settings with Persist 0: the settings loader (`LLControlGroup::loadFromFile`) does not load a
value for a non-persisted control from a user settings file, and does not write one back, so a
value an older build saved (16 samples, accumulation off) is ignored and dropped at the next
save. Only `--set` or the debug settings floater change them, for the session. Users who had chosen PPLL (1) get stochastic
transparency; `RenderAlphaOITNodesPerPixel`, `RenderAlphaOITMemoryMB`, `RenderAlphaOITStats`
and `RenderAlphaPPLLAvailable` are gone (a stale value in a settings file is ignored). Neither
the method nor its samples are in the feature tables or the graphics presets, as PPLL's were
not. Aurora's default is 16 samples; here 8 with the accumulation is fixed. Aurora's
findings: S = 8 (one run, one accumulation) costs what OIT off does on its hair fly-through,
with a faint speckle on dark hair cards and more grain on broad glass; S = 16 runs the
accumulation twice. The bench below finds the same here (S = 8 0.75-0.80x sorted alpha, S = 16
1.38-1.46x).

Requirements: Windows or Linux, OpenGL 4.0 (GLSL 4.00 for `gl_SampleMask` and
`gl_PrimitiveID`, multisample textures), at least 2 samples for the three formats, and every
alpha-only variant built. Otherwise, or when the targets cannot be allocated, sorted alpha runs
(logged once).

## Tests and measurements

- `scripts/perf/test_alpha_stochastic.py`: offscreen, the viewer's shaders with the viewer's pass
  structure: the mask table, every covered sample against a Python model of the mask (16 in two
  passes of 8, 8, 4), the total alpha, visibility and composite, an opaque occluder, the glow,
  a decal on a sloped plane with and without the offset, the edge pixels of quads with UV islands
  (sorted alpha, then the passes without and with `centroid`). NVIDIA 621.01 and Mesa Zink
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
bench's 19 scenes (with `deep` and `occl` from the round-2 bench). Sorted alpha draws each layer
at its depth against the opaque depth (LEQUAL, no depth writes), as the viewer does:

| Scene | Sorted alpha | PPLL | S = 16 | S = 8 |
|---|---|---|---|---|
| 1080p full, 8 layers | 2.60 | 2.88 | 5.68 | 3.07 |
| 1080p full, 32 layers | 10.36 | 11.78 | 10.76 | 5.87 |
| 1080p sparse, 24 layers | 2.07 | 2.21 | 2.89 | 1.72 |
| 1080p foliage (hair-like cards), 32 layers | 6.33 | 7.79 | 7.61 | 4.30 |
| 1080p occluded, 8 layers | 2.21 | 2.82 | 5.99 | 2.70 |
| 1080p, sum of 19 scenes | 86.0 | 96.6 | 118.8 (1.38x) | 65.3 (0.75x) |
| 1440p full, 8 layers | 4.63 | 5.15 | 10.38 | 5.54 |
| 1440p full, 32 layers | 18.42 | 21.03 | 19.92 | 10.76 |
| 1440p sparse, 24 layers | 3.68 | 3.96 | 5.03 | 3.17 |
| 1440p foliage, 32 layers | 11.16 | 13.82 | 15.00 | 8.32 |
| 1440p occluded, 8 layers | 3.77 | 4.99 | 9.29 | 4.95 |
| 1440p, sum of 19 scenes | 149.7 | 172.0 | 218.4 (1.46x) | 119.6 (0.80x) |

In some 19-scene runs one scene of one implementation reads much higher than in every other run
and alone (st on "full, 8 layers" about +1.5 ms, its depth and composite passes doubled; sorted
alpha once on "occluded" +8.5 ms): a placement effect of the process, not of the method. For
each implementation the table takes a run without it (1080p S = 16: st from one run, sorted
alpha and PPLL from the other); the ratios divide sums from runs without it.

Summed, S = 8 costs less than sorted alpha (most where the layers are many; at 8 full-screen
layers a little more), as Aurora found on its hair fly-through; S = 16 runs the shaded
accumulation twice and costs about 1.4-1.5x. The split per pass, total alpha / depth /
accumulation / composite, ms:

| Scene | S = 16 | S = 8 |
|---|---|---|
| 1080p full, 8 layers | 0.24 / 1.34 / 3.92 / 0.19 | 0.24 / 0.66 / 1.98 / 0.19 |
| 1080p foliage, 32 layers | 0.72 / 2.46 / 4.24 / 0.19 | 0.71 / 1.29 / 2.12 / 0.18 |
| 1440p full, 8 layers | 0.42 / 2.67 / 6.95 / 0.35 | 0.42 / 1.30 / 3.47 / 0.35 |
| 1440p foliage, 32 layers | 1.27 / 5.95 / 7.45 / 0.33 | 1.27 / 3.04 / 3.70 / 0.30 |

The bench has no glowing layers; in the viewer the glow pass runs only for groups with a glowing
blended face, and the glow composite only when one was drawn.

The alpha-only shading of the total alpha and depth passes, before (`OIT_BENCH_ST_ALPHAONLY=0`:
those passes shade the full proxy, its alpha depending on the whole result so nothing is dropped)
and after, sum of 19 scenes: 1080p S = 16 339.4 to 118.8 ms, S = 8 216.8 to 65.3; 1440p S = 16
603.0 to 218.4, S = 8 378.9 to 119.6. The depth pass writes the sample mask, so it has no early
depth test and full shading there costs every layer at every pixel. This is the cost of shading
those passes in full, not of what a given driver would have left in; the variants' early return
before the extra fetches is what makes the "after" figures hold on any driver. The CPU-side
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

¹ Measured alone (in the 19-scene run 4.87 and 5.03, the placement effect above); the sums are
those runs'.

Mesa Zink on the same GPU, 1080p, S = 16: 3.4x PPLL summed. In short: on trivially shaded scenes
stochastic transparency costs more GPU time than PPLL's fixed slots (4.0x at S = 16, 2.1-2.2x at
S = 8), least where PPLL overflows its 8 slots (foliage, 1.6x at S = 16); it is noisier than PPLL
where PPLL is exact (up to 8 layers: RMSE 4.0 against 0.7 steps of 8 bits at S = 16) and better
where PPLL overflows (32 layers: 3.9 against 12.2; foliage: 1.0 against 11.2).
