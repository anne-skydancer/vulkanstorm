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
  replaced the per-pixel lists). Their author brings that work into Vulkanstorm under
  GPL-3.0-or-later (the dual LGPL licence of that work was dropped on 2026-11-08). What was taken: the pass structure, the mask
  table generator (`oit_stochastic.rs` `mask_table`), the mask seeding (`object.wgsl`
  `st_mask`, `st_sub_mask`), the face identity hash (`types.rs` `oit_tie`), the composite and
  glow resolve (`oit_stochastic.wgsl`), the single-sample depth spread
  (`oit_stochastic_expand.wgsl` `fs_expand_ss`). No other Aurora code (GPL-3.0-only, by other
  authors) was used, other than PR 3663 (next bullet): the OpenGL resources, the passes in `LLDrawPoolAlpha`, the shader variants
  and the polygon offsets are written for this renderer.
- The 2026-11-14 upgrade (the dither, the NaN guard and the grain-measuring resolve; S stays 8, owner decision 2026-11-15, see Measured): the samples are the
  multisample samples of the stochastic targets with the coverage through the hardware sample mask, as Enderton et al.
  2010 describe it (S = the samples of the multisampled targets); the grain-measuring resolve (the grain of a pixel as the
  standard error of its samples' colours, the frame weight from it, the last frame left out where the total alpha or the far
  layer's position differ, the clamp box widened by the grain, the smooth of the leftover grain) and the set of guards against
  values that are not finite are taken from Aurora Viewer PR 3663 (Gaalrauch, also credited Rohacan Hirons, GPL-3.0-or-later,
  merged 2026-11-14; `oit_stochastic.wgsl` `cs_resolve`, `cs_temporal`, `cs_smooth`, `st_nan`, `st_sample`, `st_through`), mapped
  onto this renderer's fragment passes (`alphaStochasticCurrentF.glsl`, `alphaStochasticResolveF.glsl`,
  `stochasticResolveUtilF.glsl`, `alphaStochasticCompositeF.glsl`, which holds the smooth); the notices of the ported functions and constants stay in those
  files. The blue-noise tile (void and cluster, R. Ulichney, "The void-and-cluster method for dither array
  generation", SPIE 1993) and the dither are the Aurora Viewer's since 2026-11-18 (stage T1 of the Aurora port: PR 3789,
  `object.wgsl` `st_mask` and `oit_stochastic.rs` `void_and_cluster`, Anne Skydancer, GPL-3.0-or-later): the tile bit for bit
  (`scripts/perf/gen_stochastic_blue_noise.py` is a port of its integer generator, `llstochasticbluenoise.h`, SHA-256 pinned in the
  unit test) and xi in 24-bit fixed point, with the golden-ratio step of the frame as a Kronecker sequence (one integer
  multiply). Where this renderer differs from the PR: the history keeps the four-vector (alpha x colour, alpha)
  with the alpha correction made after the blend, not the corrected colour (offline the corrected colour made the low-frequency error
  twice as large, measured offline at S = 4); the total alpha is taken at every stochastic sample, not at one; the history is read with a Catmull-Rom
  filter, not bilinear; and the smooth's reach is 0.5 grains, not 2 (below).
- The decision: the owner, 2026-11-05, "stoch and ppll don't go well together. got to be 1 or
  the other. stoch is less expensive in my experience with aurora, and comparable quality with
  the algorithm used there."

## The stochastic engine

The sampling and the temporal resolve are shared with the producers that will use them (the Stochastic
shadow method, RenderShadowMethod 2, later): GLSL objects `stochasticSampleF.glsl` (hash, per-frame seed,
mask lookup in the transparency's S = 8 table with the blue-noise tile, `stoch_masks`) and `stochasticResolveUtilF.glsl` (reprojection with the
last frame's matrices, Catmull-Rom (transparency) and depth-checked bilinear (shadow) history reads, the 3 x 3 clamp, the 1/8 blend,
`stoch_reproj`, `stoch_weight`, `stoch_history_valid`; stage-neutral). The sampling object is compiled once
and attached to the programs that sample (`STOCH_SAMPLE_OBJECT`, `LLShaderMgr::attachShaderFeatures`); the
resolve util is attached as a fragment object (`STOCH_RESOLVE`) to the transparency's blend and composite programs and to the shadows'
resolve (the shadows' compute resolve, A/B only, compiles it as a compute object; the transparency's compute resolve is retired). The producer defines `vec4 stoch_current(ivec2)` and its `main()`. In C++, `llstochastichistory.h`:
`LLStochasticEngine` (the mask table, counted textures) and `LLStochasticHistory`, one instance per producer
(`allocate`, `release`, `beginFrame`, `seedFrame`, `historyUsed`, `setResolveUniforms`, `endFrame`,
`invalidate`).

## The method in this viewer

The deferred scene is single-sampled (FXAA or SMAA run after it), so the passes over the blended
faces run on their own multisampled targets, as Aurora does without MSAA:

| Target | Format | Use |
|---|---|---|
| accumulation | RGBA16F, 8 samples (S = 8) | alpha x colour and alpha, additive |
| transparency | RG16F, 8 samples | red: prod(1 - alpha), cleared to 1; green: the faces' glow |
| stochastic depth | DEPTH_COMPONENT24, 8 samples | the opaque depth spread, then the faces' stochastic depth |
| mask table | R32UI, 2048 x (S + 3) | row r: 2048 random r-subsets of the S sample bits; the last two rows: the 64 x 64 blue-noise tile (ranks 0 to 4095) |

The method needs 8 samples in one pass: the GL 4.6 base takes 8 for the colour, the depth and the `GL_MAX_SAMPLES` limits (NVIDIA
621.01 honours only the low 4 bits of a texture-fetched `gl_SampleMask` at 16 samples, so a pass is never wider than 8). S, the
samples in all, is 8 (`LLStochasticEngine::TRANSPARENCY_SAMPLES`; the Stochastic shadows' ray count `SAMPLES` is 8 too). It was
tried at 4 on 2026-11-14 and set back to 8 on 2026-11-15 (see Measured: at 4 the upgrade was statically grainier than the
viewer before it, at 8 it is better in every scene). A device that takes fewer than 8 samples cannot run it:
`allocateAlphaStochastic` leaves stochastic transparency off and sorted alpha is drawn (owner decision, 2026-11-15: the viewer is GL 4.6 based, no
multi-pass fallback). There are no iterations: the lanes of one pass are never summed across passes. (S = 16 as two passes of 8 was dropped from the harness on 2026-11-15 with the multi-pass path; the mask table itself still has an S = 16 generator.) Memory: 16 B per sample, 128 B per pixel at 8 samples (253 MiB at 1920x1080, 450 MiB at 2560x1440), allocated at the render
resolution while the method is selected and freed otherwise; nothing grows with the depth of the scene.

`LLDrawPoolAlpha::renderStochastic` (`pipeline.cpp` holds the targets, the spread and the
composite):

1. the targets cleared, the opaque depth spread to every sample (`alphaStochasticExpandF.glsl`);
   the GLTF scene's blended assets join passes 2 to 4 with the faces (see below), not the scene depth;
2. **total alpha**: every captured draw through its program's alpha-only variant into the red
   of the transparency target, blend ZERO, ONE_MINUS_SRC_ALPHA, against the spread depth;
3. **stochastic depth**, the alpha-only variants writing `gl_SampleMask` with
   R = floor(alpha S + xi) samples (`alphaStochasticF.glsl`), depth only, LEQUAL; then the
   **accumulation**, the ordinary programs (full shading, early depth test kept) blended
   SRC_ALPHA, ONE / ONE, ONE where the face is at or in front of the stochastic depth, and the
   glowing faces' glow (`emissiveF.glsl`, `pbrglowF.glsl`, which now write the glow in green as
   well) into the green of the transparency target;
4. **temporal resolve** (`resolveAlphaStochasticTemporal`, two fragment passes: `alphaStochasticCurrentF.glsl`,
   `alphaStochasticResolveF.glsl`; the compute `alphaStochasticResolveC.glsl` is retired, the Vulkan renderer
   takes the compute steps, doc/vulkan/stochastic-transparency-grain-resolve.md): this frame's accumulation averaged over the samples and
   its grain measured, with the mask of where anything was drawn, then blended with the last frame's into the history by that grain, under the mask (see Temporal accumulation); the smooth is in the composite;
5. **composite** (`alphaStochasticCompositeF.glsl`) over the scene: the scene dimmed by the
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

The GLTF scene's blended variants (`pbrmetallicroughnessF.glsl` with `ALPHA_BLEND`, lit and unlit,
single and multi-UV, static and rigged: eight) have alpha-only variants of their own
(`gGLTFAlphaStochasticProgram`, built by `make_gltf_variant`), drawn by `GLTFSceneManager::render`
in the first two passes (total alpha and stochastic depth; the accumulation uses the ordinary variant, as for prims). A GLTF blended asset behaves exactly like a blended prim face: wherever
the faces contribute depth, the asset does the same through a depth-only, colour-masked draw of its
alpha-only variant in its mode 3 (every sample; fragments under the cut discarded): the depth of
field pass (the faces' non-rigged draw, with the same 0.33 cut) and the rigged depth restore before
the residual (the faces' rigged draw, the same minimum alpha). Where the faces contribute nothing
(the rigged draw of the depth of field pass), neither does the asset. The GLTF variants do not gate
the engine: a GLTF alpha-only variant that does not build fails the GLTF shader set like a failed
base variant (`RenderCanUseGLTFPBROpaqueShaders` off, the GLTF blended set not drawn, logged) and
leaves the prims' transparency ready. The raw `glBindTexture` calls of `GLTFSceneManager::bindTexture`
do not go through `LLTexUnit`: the mask table is bound after resynchronising the active unit and
forgetting the unit's cached texture, and the active unit is restored after each GLTF draw.

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
8 samples on an RTX 5070 Ti (NVIDIA 621.01), run once at S = 8 and twice at S = 16.

## Temporal accumulation (`RenderAlphaStochasticTemporal`, always on)

The accumulation is part of the method, not a user choice: it is not in Preferences. The debug setting
exists for debugging only, and is not persisted (see Settings).

8 samples in one run of the passes (S = 8; 4 for a day, 2026-11-14 to 15), with the noise measured per pixel and averaged over the frames.
The design is the author's Aurora Viewer's (`oit_stochastic.wgsl`) with the grain-measuring resolve of Aurora PR 3663 (see Provenance).
The masks' seeds change every frame (`st_frame`, only with the accumulation on; the face identities stay stable, so layers keep
their roles). Two fragment passes (`resolveAlphaStochasticTemporal`) and the smooth in the composite. OpenGL compute dispatches stall the frame in motion, so the
compute resolve `alphaStochasticResolveC.glsl` is retired for the transparency (the Stochastic shadows keep theirs for A/B tests), and the Vulkan renderer takes the
compute steps (doc/vulkan/stochastic-transparency-grain-resolve.md).

1. **Measure** (`alphaStochasticCurrentF.glsl`, two RGBA16F targets, 16 B/px, as the RGBA32F target they replace, and an R8 mask, 1 B/px): per pixel, this
   frame's accumulation (alpha x colour, alpha) averaged over the samples once; the **grain** (the standard error of the pixel's
   colour: the spread of the samples' colours over the root of their effective count, times the accumulated alpha; 0 where one layer
   covers the pixel, since every sample then holds its colour); the total alpha; and the nearest and farthest stochastic depth, stored as one minus
   the window depth (a 16-bit float is fine near 0, not near 1). NaN samples count for nothing, infinite ones clamp to 65504, a NaN transparency hides nothing.

   **The mask** (the third target, R8): 1 where anything transparent was drawn at the pixel (a non-zero lane of the accumulation, a total alpha, a glow, a grain), 0 where every lane is empty.
   The measure is full screen (it is the pass that finds the mask, and the blend reads its output at the neighbours, which must exist), and only
   what follows it is masked (below).
2. **Blend** (`alphaStochasticResolveF.glsl` with the engine's `stochasticResolveUtilF.glsl`): this frame's weight is `st_frame_weight` of the grain
   (1 where the colour is sure: sharp, no history; down to the engine's 1/8 where it is grainy; QUIET 0.004 of the colour, 0.05 floor). The last frame's
   colour is read where the nearest layer was, moved by the camera alone, with the Catmull-Rom filter (unchanged), and left out
   where its total alpha differs from this frame's (from 0.004, all of it at 0.016) or where the farthest layer (of the 3 x 3) has slid more than 1/4 px (all of it at 3/4 px) past
   the nearest since. The history is clamped to the 3 x 3 box of this frame, widened by one grain of the pixel.
   The history keeps this engine's format, the four-vector (alpha x colour, alpha); the alpha correction (this frame's total alpha over the
   blended alpha) is made after the blend, so that an alpha change is never ghosted (the PR stores the corrected colour; offline that made the
   low-frequency error twice as large, measured offline at S = 4). Beside it, ping-pong with it, an RG16F texture: the last frame's total alpha (the PR's), and the part of the last frame left out (so that the smooth need not recompute it; the PR's R16F plus a recomputation).
3. **Smooth** (in `alphaStochasticCompositeF.glsl`, from the history, the aux target and the part left out; it was a pass of its own, `alphaStochasticSmoothF.glsl`, until the mask): what grain the blend leaves
   (the pixel's grain times sqrt(w / (2 - w)), all of it where the history was left out) is averaged with the neighbours of a like total alpha (within 0.004) and a
   colour within `ST_GRAIN_REACH` times that grain, radius 1 then 2; pixels with no grain leave early. The composite draws it (already
   alpha corrected, rounded to half floats as the smooth's RGBA16F target stored it, so the image is the one the separate pass gave); the history is the unsmoothed blend, as before; the total alpha that dims the scene is still the mean over the samples at the composite. **Deviation from the PR**: its
   reach is 2; here 0.5. At 2 the smooth blurred textured layers (the pan scenes, T3 static: rmse/255 3.4 against 1.5 without the smooth); at
   0.5 it does not (1.48), and it still averages a grainy pixel that has no history. The HDR range needed no other constant (values are clamped to 65504).

The total alpha is still taken at every stochastic sample (not at one sample, as the PR does). The glow keeps the current frame's accumulation (the composite's glow draw reads the accumulation samples).

Dither: the pixel's xi of R = floor(alpha S + xi) is a 64 x 64 blue-noise tile (void and cluster; its ranks are in two spare rows of the mask table, one more `texelFetch`, no new binding),
plus a per-face offset, plus the frame's golden-ratio step (frame x 2^32 / phi mod 2^32, one multiply); the subset of the covered samples is drawn afresh per face, triangle, pixel and
frame from the table. Never a subset fixed over the frames, nor one rotation stride for all layers (measured 1.8 to 1.9 times worse).
Since 2026-11-18 (stage T1) the tile is the Aurora Viewer's and xi is its integer form, `stoch_xi24`: `((2 rank + 1) 2048 + (seed >> 8) + (frame x 0x9e3779b9 >> 8)) mod 2^24`, over 2^24.
The whole sum is integer modulo 2^24, so every stratum stays exact at any frame number (the float form it replaces rounded the sum of three values in [0, 1) before `fract`), the face's offset is the top 24 bits of its seed (no extra hash),
and the unit test holds the Aurora Viewer's properties for it: the mean of R over the tile is alpha S within one stratum for any rotation, eight consecutive
frames sum to 8 alpha S within 1 (max error 1.00, rms 0.38, against 4.00 and 1.14 for a fresh hash every frame), two layers share a subset at 3 of 4096 pixels
(chance: 2), and every 8 x 8 block of the tile is balanced (sd of the block means 0.0105, white noise 0.037). The grain pattern changes; its statistics do not
(the A/B below was measured with the previous tile).

NaN guard (PR 3663's set, with a bit test of the exponent, not `isnan`): a fragment with a non-finite alpha is discarded in the alpha-only passes; the measuring pass and the
composite zero NaN samples and clamp infinities; the Catmull-Rom taps skip non-finite texels, the 3 x 3 box skips non-finite values, and a blend that is
not finite takes this frame's value; so the history never holds one. The harness injects NaN and infinite colours, NaN and infinite alphas and a NaN in one channel for six frames and checks the image, both history textures,
the measuring targets and the pixels far from the injection (`test_alpha_stochastic.py`, "NaN guard").

No history on the first frame, after a resize, when the setting or the method is switched, or when the frame before did not run the method (the history stamp is the frame counter).
If the history cannot be made, the frames draw with their 8 samples, the setting stays on and the allocation is tried again (1 s, doubling to 30 s).

**Where the mask is 0** (the tile skip: scattered transparency costs by what it covers). The blend writes (0, 0, 0, 0) and (total 0, part left out 1) for such a pixel at once, which is exactly what it
would have written (an empty accumulation takes its first early return), and the composite discards it at once, which is what it would have done (no total alpha, no glow). The history is
written for every pixel, because the two history textures alternate and a pixel left unwritten would keep the frame before last. Neighbour reads (the 3 x 3 box, the smooth's radius 2, the Catmull-Rom taps) find the same values as before:
the measure wrote this frame's aux everywhere and the blend wrote the history everywhere, so no widening of the mask is needed. The mask is per pixel, not per tile: a warp whose pixels all leave runs only a texture fetch.
This was chosen over a stencil mark because the composite's target (`mRT->screen`) shares its depth buffer with the deferred screen and has no stencil, and a stencil buffer attached to the history framebuffers
alone would leave the composite without it; one mechanism for both passes is the simpler. Exactness is tested (below).

Memory (B/px, from the allocations; the viewer logs it): the S = 8 targets 128 (accumulation 64, transparency 32, depth 32), the history 2 x (8 + 4) = 24, the measuring targets 17 (two RGBA16F and the R8 mask; the smooth drew over the first, so it never had a target of its own):
**169**, against 160 before (128 targets + 16 history + 16 averaged samples in RGBA32F): +9 B/px. At 1920 x 1080 that is 334 MiB against 316 MiB, at 2560 x 1440 594 MiB against 563 MiB. All of it is counted in the viewer's texture memory at its real size.

The history is read with a Catmull-Rom filter at the exact reprojected position (`stoch_history_catmull_rom`, 4 x 4 texels, Karis 2014 and Jimenez 2016, unchanged), bit-identical to the nearest read for whole-pixel offsets.
Not reprojected: the surfaces' own motion; there the history ghosts, limited by the clamp.

### Measured against the viewer before the upgrade (offline, the viewer's shaders)

`scripts/perf/test_alpha_stochastic.py` (the harness's pan scenes at 256 x 256, 40 frames, rmse/255 against sorted alpha at the same position; RTX 5070 Ti, NVIDIA 621.14; before = the shaders of git ref `1f38b56b50`: S = 8, hashed dither, the 1/8 resolve; after = this tree, S = 8, one pass of 8 samples; the last column is the first version of the upgrade at S = 4, kept for the record):

| scene | motion | plain 8 samples (before) | before: S = 8 + history | plain 8 samples (new dither) | after: S = 8 + grain resolve | the same at S = 4 (dropped 2026-11-15) |
|---|---|---|---|---|---|---|
| T3 (3 textured layers) | static | 3.63 | 1.03 | 3.73 | **0.79** | 1.28 |
| T3 | 0.5 px a frame, reprojected | 3.63 | 2.17 | 3.74 | **2.11** | 2.27 |
| T3 | 2.5 px | 3.62 | 2.16 | 3.73 | **2.11** | 2.38 |
| Q10 (10 textured layers) | static | 5.47 | 1.74 | 5.50 | **1.51** | 2.18 |
| Q10 | 0.5 px | 5.48 | 3.45 | 5.50 | **3.41** | 3.58 |
| Q10 | 2.5 px | 5.48 | 3.44 | 5.49 | **3.39** | 3.58 |
| S10 (10 layers of hair strips) | static | 4.96 | 1.70 | 4.96 | **1.32** | 2.98 |
| S10 | 0.5 px | 4.94 | 6.35 | 4.95 | **4.98** | 6.35 |
| S10 | 2.5 px | 4.95 | 6.60 | 4.95 | **5.14** | 6.51 |
| T3 / Q10 / S10 | surfaces moving 0.5 px, not reprojected | 3.63 / 5.48 / 4.94 | 14.8 / 17.5 / 17.2 | 3.74 / 5.50 / 4.95 | **15.5 / 18.1 / 9.7** | 15.5 / 18.2 / 10.9 |
| T3 / Q10 / S10 | surfaces moving 2 px, not reprojected | 3.63 / 5.48 / 4.95 | 16.0 / 19.0 / 19.3 | 3.73 / 5.49 / 4.96 | **18.3 / 19.4 / 9.8** | not run |

**Reading.** S is 8 (owner decision, 2026-11-15, after the S = 4 column showed the first version statically grainier than the viewer before the upgrade: T3 +24 %, Q10 +25 %, hair +75 %).
At S = 8 the grain resolve beats the viewer before the upgrade in every static and reprojected-pan row: static T3 -23 %, Q10 -13 %, hair -22 %; a 0.5 px pan -3 %, -1 %, -22 %; 2.5 px -2 %, -1 %, -22 %.
The exceptions are the surfaces that move without the camera (the history is not reprojected and ghosts, limited by the clamp): the textured layers ghost slightly more than before (0.5 px: T3 +4 %, Q10 +3 %; 2 px: T3 +15 %, Q10 +2 %), the hair far less (-44 % and -49 %).
Hair on a fractional pan (S10) is not better than its plain 8 samples in rmse (4.98 against 4.95; the viewer before the upgrade 6.35): the history's smear costs what its noise saving gains there, so the new resolve only removes the extra smear.
The first frame and the frames where the history is left out (a total alpha change, a far layer sliding) are smoothed: after an
alpha change at frame 24 the first frame reads 0.45 against 0.75 without history. The smooth's reach was cut from the PR's 2 to 0.5 (reach 2: T3 static 3.41).
The harness checks that one layer has zero grain and takes no history, that two layers have grain, and that the history is left out when the total alpha changes (1.00 of the pixels) or the far layer slides 2 px (0.90; 0 at 0 and 0.1 px).
The gain is the resolve, not the dither: the plain 8 samples with the new dither are as noisy as before (T3 3.73 against 3.63, Q10 5.50 against 5.48: not a gain); the dither alone is measured in the A/B below (static convergence only).

**The mask and the folded smooth are exact.** `test_alpha_stochastic.py` runs the new passes and the passes of git `c121dc100c` (unmasked blend, a smooth of its own) side by side over two sequences of 22 frames, static and with a 1.3, 0.7 px pan: nothing, transparency appearing, moving, scattered (hair in the middle, a window in a corner, an alpha 0 face with only a glow, a face too faint to move the half-float transparency), gone the next frame, back elsewhere, everything, nothing. Every frame the image, both history textures, both of the last total alpha and part left out, and the aux target are equal bit for bit (`np.array_equal`). The test has teeth: without the glow clause in the mask the glow-only face fails it.

The farthest depth (review round): the first version took the farthest stochastic depth over all samples. A sample no layer covers still holds the opaque depth (the spread), so a pane in front of a
wall had the wall as its "far layer", and any camera translation that slid the wall 3/4 px past the pane (a few pixels a frame at a few metres) dropped the history of all the glass. The measuring pass now reads the scene depth
(`depthMap`, one more `texelFetch`) and takes the farthest depth only over the samples whose depth is in front of it. This raised the static quality (the numbers above) and keeps the history under translation where the layers
move together; the harness has no translating walk-through of a partly covering layer over a distant wall (its reprojection is a pan, or a shift linear in depth between the layers), so that case is unmeasured in motion: in-world check.

NaN injection against the old pipeline (a NaN colour over 16 of 256 columns for 6 frames, then 33 clean frames): no value that is not finite in the image either before or after for this layout (the old clamp of the history
to the 3 x 3 box happened to drop it from the image), but the old pipeline's accumulation and history held 4096 and 8192 non-finite values after the injected frames and the upgrade held none; the upgrade holds at every stage for the harness's six injection forms.
The measuring pass zeroes a NaN and clamps an infinity before the blend, so the guards in the Catmull-Rom taps and the 3 x 3 box cannot be reached through this pipeline; they are defence in depth for a producer that does not measure first (and the Stochastic shadows' resolve, which shares `stochasticResolveUtilF.glsl`, now bit-tests its 16 taps too: identical for finite data, one test per tap more).

### Dither A/B (offline, 2026-11-15)

Question: what do the blue-noise tile and the golden-ratio step of the frame, the dither that shipped with the upgrade, gain over a hashed dither? Measured offline, with the viewer's shaders, S = 8, the shipping resolve, NaN guard and tile mask. Hardware for every number [M]: GL 4.6.0 NVIDIA 621.14, NVIDIA GeForce RTX 5070 Ti.

The script that ran it (`ab_stochastic_dither.py`) is **not in the repository**: it is session tooling, kept off-tree. The method is stated here so it can be redone.

- **Harness.** The shared blocks of `scripts/perf/test_alpha_stochastic.py` (the pan scenes, the sorted-alpha reference, the temporal history and the grain resolve), run at 256 x 144. The engine's `stochasticSampleF.glsl` is source-patched per variant (the script asserts that every patch changed the source); each variant is its own program, and all other passes are the shipping ones. The reference is sorted alpha at the same position.
- **Variants.**
  - **a**: white everywhere. The dither xi is a hash of face seed, pixel and frame; the mask subset is hashed as in b. This is the hashed dither the viewer used before the upgrade.
  - **b0**: the blue-noise tile and the per-face offset, with a white (hashed) per-frame offset instead of the golden step.
  - **b**: the shipping dither: blue tile, face offset and the golden-ratio step of the frame (`frame * 0x9e3779b9`); the mask subset is hashed afresh every frame.
  - **c**: b, and a golden step on the subset too: the subset column is a per-fragment start (hash of face seed and pixel, constant over frames) plus `frame * 1265` mod 2048 (1265 is about 2048 times the golden fraction), instead of a fresh hash.
- **Scenes.** T3 (3 textured layers), Q10 (10 textured layers), S10 (10 layers of hair strips), SC (scattered: the first six layers of S10 cut to a central rectangle of 40 % x 55 % of the frame, and the first two layers of Q10, shifted 0.01 in depth, as a window in the top-right corner; the layers and the reference are drawn only inside their rectangles).
- **Runs.** Each scene 6 times (other layer seeds, offset 17 per run, and another frame base, 1000 per run), 40 frames after a history reset. A run's figure is the mean of frames 33 to 40 (the converged state); the tables give mean +- sd over the 6 runs. The same runs feed all four variants, so the differences are paired per run. Cases: static; pan 0.5, 0 px and 1.3, 0.7 px a frame (reprojected); surfaces moving 0.5 and 2 px a frame (not reprojected: ghosting).
- **Metrics.** rmse/255 of the resolved image against the sorted reference, and the same after a 1.5 px Gaussian (the low-pass error, closer to what the eye sees). Convergence is the error at frames 1, 2, 4, 8 and 16 after the reset, static (the low-pass error at frames 1, 4 and 16).

Resolved rmse/255, static, mean of frames 33-40 (lower is better):

| scene | a | b0 | b (ships) | c |
|---|---|---|---|---|
| T3 | 0.948 +- 0.005 | 0.903 +- 0.045 | 0.795 +- 0.006 | 0.792 +- 0.006 |
| Q10 | 1.554 +- 0.007 | 1.541 +- 0.015 | 1.520 +- 0.010 | 1.517 +- 0.008 |
| S10 | 1.348 +- 0.005 | 1.342 +- 0.012 | 1.321 +- 0.005 | 1.316 +- 0.008 |
| SC | 0.660 +- 0.009 | 0.657 +- 0.006 | 0.644 +- 0.008 | 0.646 +- 0.005 |

Low-pass error/255, static, frames 33-40:

| scene | a | b0 | b (ships) | c |
|---|---|---|---|---|
| T3 | 0.187 +- 0.003 | 0.163 +- 0.004 | 0.161 +- 0.005 | 0.160 +- 0.005 |
| Q10 | 0.304 +- 0.004 | 0.299 +- 0.003 | 0.296 +- 0.003 | 0.296 +- 0.004 |
| S10 | 0.266 +- 0.004 | 0.263 +- 0.001 | 0.259 +- 0.002 | 0.259 +- 0.005 |
| SC | 0.136 +- 0.005 | 0.134 +- 0.001 | 0.135 +- 0.004 | 0.135 +- 0.002 |

Resolved rmse/255, pan 0.5, 0 px (frames 33-40):

| scene | a | b0 | b (ships) | c |
|---|---|---|---|---|
| T3 | 2.137 +- 0.004 | 2.136 +- 0.006 | 2.117 +- 0.012 | 2.117 +- 0.009 |
| Q10 | 3.410 +- 0.006 | 3.409 +- 0.011 | 3.403 +- 0.007 | 3.408 +- 0.008 |
| S10 | 5.046 +- 0.005 | 5.047 +- 0.006 | 5.045 +- 0.004 | 5.046 +- 0.006 |
| SC | 2.395 +- 0.006 | 2.394 +- 0.005 | 2.391 +- 0.007 | 2.395 +- 0.008 |

Resolved rmse/255, pan 1.3, 0.7 px (frames 33-40):

| scene | a | b0 | b (ships) | c |
|---|---|---|---|---|
| T3 | 2.390 +- 0.004 | 2.386 +- 0.009 | 2.389 +- 0.006 | 2.388 +- 0.005 |
| Q10 | 3.527 +- 0.007 | 3.526 +- 0.009 | 3.522 +- 0.009 | 3.525 +- 0.009 |
| S10 | 4.781 +- 0.005 | 4.777 +- 0.007 | 4.778 +- 0.005 | 4.776 +- 0.003 |
| SC | 2.170 +- 0.006 | 2.166 +- 0.009 | 2.167 +- 0.008 | 2.165 +- 0.004 |

The low-pass error under both pans differs by at most 0.01 between variants (T3, a / b0 / b / c: 0.464 / 0.456 / 0.456 / 0.456 at 0.5, 0 px; 0.546 / 0.539 / 0.539 / 0.540 at 1.3, 0.7 px). The ghosting of surfaces moving 0.5 and 2 px without the camera is the same in the four variants to within 0.015 in every scene (rmse, T3 a / b0 / b / c: 15.446 / 15.445 / 15.435 / 15.435 at 0.5 px, 18.313 / 18.325 / 18.312 / 18.313 at 2 px).

Convergence after a history reset, static, resolved rmse/255 (a / b0 / b / c):

| scene | frame 1 | frame 2 | frame 4 | frame 8 | frame 16 |
|---|---|---|---|---|---|
| T3 | 3.807 / 3.768 / 3.784 / 3.788 | 3.260 / 3.200 / 3.173 / 3.175 | 2.629 / 2.592 / 2.525 / 2.527 | 1.758 / 1.709 / 1.578 / 1.579 | 1.081 / 1.064 / 0.915 / 0.920 |
| Q10 | 5.384 / 5.383 / 5.376 / 5.366 | 5.327 / 5.326 / 5.304 / 5.301 | 4.497 / 4.509 / 4.491 / 4.486 | 3.088 / 3.091 / 3.066 / 3.065 | 1.846 / 1.839 / 1.809 / 1.814 |
| S10 | 4.955 / 4.941 / 4.953 / 4.940 | 4.608 / 4.581 / 4.589 / 4.565 | 3.814 / 3.782 / 3.778 / 3.760 | 2.587 / 2.575 / 2.546 / 2.547 | 1.569 / 1.562 / 1.539 / 1.545 |
| SC | 2.246 / 2.243 / 2.248 / 2.261 | 2.103 / 2.080 / 2.086 / 2.084 | 1.743 / 1.720 / 1.723 / 1.730 | 1.191 / 1.185 / 1.173 / 1.179 | 0.755 / 0.749 / 0.735 / 0.744 |

The spread of b0 on T3 is the widest (+-0.134 at frame 8, +-0.117 at frame 16); b is +-0.010 and +-0.004. The low-pass error at frame 16, a / b0 / b / c: T3 0.210 / 0.180 / 0.176 / 0.179, Q10 0.372 / 0.359 / 0.359 / 0.362, S10 0.312 / 0.306 / 0.302 / 0.303, SC 0.151 / 0.149 / 0.148 / 0.152.

Paired per-run differences of the resolved rmse/255 (negative: the first is better; mean +- sd of the difference):

| difference | T3 | Q10 | S10 | SC |
|---|---|---|---|---|
| b - a, static, frames 33-40 | -0.153 +- 0.009 | -0.034 +- 0.013 | -0.027 +- 0.008 | -0.016 +- 0.006 |
| b - b0, static, frames 33-40 | -0.108 +- 0.045 | -0.021 +- 0.016 | -0.021 +- 0.009 | -0.013 +- 0.011 |
| c - b, static, frames 33-40 | -0.003 +- 0.004 | -0.003 +- 0.009 | -0.005 +- 0.009 | +0.002 +- 0.009 |
| b - a, frame 16 after the reset | -0.166 +- 0.006 | -0.038 +- 0.015 | -0.030 +- 0.008 | -0.020 +- 0.009 |
| c - b, frame 16 after the reset | +0.005 +- 0.005 | +0.005 +- 0.010 | +0.006 +- 0.012 | +0.009 +- 0.016 |
| b - a, pan 0.5, 0 px | -0.020 +- 0.012 | -0.007 +- 0.011 | -0.001 +- 0.004 | -0.003 +- 0.008 |
| b - a, moving 0.5 px | -0.011 +- 0.003 | -0.001 +- 0.008 | -0.006 +- 0.011 | -0.000 +- 0.004 |

**Reading.**

- **(b) is kept.** It ships. Static, it beats the hashed dither (a) in every scene: T3 0.948 to 0.795 (-16 %), Q10 -2 %, S10 -2 %, SC -2 %; on T3 the paired difference is many times its spread. The convergence gain shows from frame 4 on: T3 at frame 16 reads 1.081 for a and 0.915 for b, **-15.4 %** (paired -0.166 +- 0.006), against 1.064 +- 0.117 for b0.
- **The blue tile alone (b0) is the weaker half.** The tile with a white per-frame offset gains little over a (T3 0.903 against 0.948, with a spread of 0.045 that the golden step cuts to 0.006): the golden step is what makes the frames of the history cover the tile evenly, so the gain needs both.
- **(c) gives no gain over (b).** A golden step on the subset as well changes nothing the paired runs resolve: static c - b is within one sd in every scene (T3 -0.003 +- 0.004), at frame 16 it is slightly positive (+0.005 to +0.009), and under the pans and the moving cases it is within 0.005. The subset stays hashed afresh every frame, as shipped.
- **Where the dither helps: static and slow convergence, most on the textured layers.** Under pans and for surfaces that move, the four variants are equal within noise (the history is reprojected off the tile's pixel grid, so the tile's structure does not survive); hair (S10) and the scattered scene (SC) gain 1 to 2 % static. The 2026-11-06 finding that a tile alone gains nothing stands for moving images; what the tile with the step adds is the static case.
- Limits: 256 x 144, 6 runs, one GPU, an error against sorted alpha and not an in-world judgement. The script's timing mode (b against c, interleaved) was not run for this section, since c is not adopted; no cost of the dither is claimed here.

### GPU time per pass (2560 x 1440, `scripts/perf/bench_stochastic_resolve.py`, GL_TIME_ELAPSED)

Per pass the median of 68 timed frames of a run (80 frames, the first 12 not timed), and of three runs the **lowest** median per pass (the GPU's clock state moves a run by tens of percent; the minimum is the repeatable one). The total is the sum of those pass times (alpha, depth, accumulation, measure, blend, smooth where it is a pass, composite). shipping = git ref `1f38b56b50` (S = 8, before the upgrade), c121 = `c121dc100c` (the upgrade before the mask: unmasked blend, a smooth pass of its own), now = this tree. The harness composite draws the colour and the glow pass; the viewer draws the glow only when a glowing face is drawn. RTX 5070 Ti, NVIDIA 621.14. ms:

| scene | shipping | c121 | now | now - shipping | now - c121 |
|---|---|---|---|---|---|
| T3 static (full screen) | 1.897 | 2.310 | 2.275 | +0.378 | -0.035 |
| T3 pan 1.3, 0.7 | 1.910 | 2.380 | 2.344 | +0.434 | -0.037 |
| Q10 static | 4.464 | 4.877 | 4.837 | +0.373 | -0.039 |
| Q10 pan | 4.472 | 4.945 | 4.902 | +0.430 | -0.043 |
| S10 static (hair) | 1.640 | 2.162 | 1.812 | +0.172 | -0.350 |
| S10 pan | 1.765 | 2.306 | 1.964 | +0.198 | -0.343 |
| SC static (hair in the centre, a window in a corner, about 30 % covered) | 1.071 | 1.566 | 1.133 | +0.063 | -0.433 |
| SC pan | 1.077 | 1.580 | 1.151 | +0.075 | -0.429 |

The budget was the Stochastic frame of 8.6 ms + 5 % = +0.43 ms over the shipping S = 8 (the former budget, retired 2026-11-16; the budget is now the fps floor, see AGENTS.md; the numbers below were taken under it). The full-screen scenes sit at +0.37 to +0.43 (T3 pan +0.434 and Q10 pan +0.430 are on the line, 0.004 over and 0 over); hair is +0.17 to +0.20 and the scattered scene +0.06 to +0.08, all inside it.
Per pass, Q10 static, shipping / c121 / now: alpha 0.437 each, depth 1.361 / 1.387 / 1.387, accumulation 1.627 / 1.629 / 1.629, measure 0.361 / 0.532 / 0.527, blend 0.252 / 0.139 / 0.148, smooth - / 0.263 / folded, composite 0.425 / 0.490 / 0.710 (the composite now holds the smooth: 0.753 for the two in c121). Folding the smooth saves 0.04 on a full-screen scene (one fewer full-screen pass, its reads done in the composite's draw); the gain of the mask is where the transparency is not: composite 0.469 + 0.101 in c121 against 0.223 on hair, 0.466 + 0.051 against 0.083 on the scattered scene. The measuring pass stays full screen (it finds the mask), so it is the floor of the cost on a scene that covers little: 0.44 ms at 1440p.
The mask costs nothing in the measure: the third target (R8) is within noise of the two-target version (0.470 against 0.471 on T3). Memory per pixel of the transparency's targets, history and measuring targets: shipping 160 B, c121 168 B, now 169 B (+1: the mask; the smooth drew over the first measuring target, so folding it frees nothing).
The offline accumulation shades with a light stand-in, not the viewer's alpha programs, so the saving or cost in the viewer is not measured here.

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
| 3.1 dither R across a tile for further stratification | done since 2026-11-14: a 64 x 64 void-and-cluster blue-noise tile (two spare rows of the mask table), a per-face offset and the golden-ratio step of the frame; the 2026-11-06 measurement of a tile without the step found no gain (below), the temporal sequence is what the upgrade adds; since 2026-11-18 the Aurora Viewer's tile and its 24-bit integer xi |
| 3.3 alpha correction, total alpha over accumulated alpha | done (composite); a single layer is exact (test) |
| 3.4 steps 1-5: opaque depth, total alpha, stochastic depth only, accumulation by the stochastic depth, composite | done; deviations: the opaque depth is spread over the samples (the scene is single-sampled), and the composite dims the scene by the pixel's mean transparency, not per sample (differs only on background edges inside a pixel) |
| 3.5, footnotes 3 and 4: more than 8 samples by iterating, one stratified mask split across the passes | S = 16 two-pass reference, dropped 2026-11-15 with the multi-pass path (the shader's iteration shift and the harness cases are gone); the viewer runs S = 8 in one pass and needs a device that takes 8 (the GL 4.6 base) |
| 5 mask table of about 2047 masks | done (2048) |
| 6 temporal coherence: seed by pixel and primitive id | done (pixel, face identity, triangle); the noise field is fixed in screen space |
| 6 uneven noise at edges; alpha correction makes one-layer regions noise free | known properties |
| 6 post processing: cross-bilateral filter on the total alpha | measured, rejected (below); the 2026-11-14 smooth averages by the measured grain and the total alpha instead (Temporal accumulation) |
| 6 adaptive sampling, importance sampling for dark backgrounds | not adopted: the grain resolve measures the grain per pixel instead (S is fixed at 4 for the transparency) |
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
  to improve on that significantly"). Rejected then: a texture and a binding for nothing. (Superseded 2026-11-14: the tile now lives in two spare rows of the mask table that is bound anyway, so there is no new binding and one `texelFetch`. The gain is still not shown: the 2026-11-06 measurement stands, and no A/B of the tile plus the golden-ratio step against the hashed dither was run on S = 4 with the grain resolve; the harness result above cannot attribute any gain to them.
The golden-ratio step is the same for every layer, the face's offset (its seed) differs; the owner's rule against equal strides concerned the mask subsets, which stay hashed per face, triangle, pixel and frame.)
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
| depth of field depth | stock pass after everything (as PPLL), skipped when the engine was selected and drew nothing; the GLTF blended assets add their non-rigged depth with the same cut |

## Settings

| Setting | Values | Default |
|---|---|---|
| `RenderAlphaSortMethod` | 0 sorted alpha, 1 stochastic transparency | 1 |
| `RenderAlphaStochasticTemporal` | debug only, not persisted: accumulate over the frames | TRUE |

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
not. Aurora's default is 16 samples; here 8 with the accumulation is fixed (4 was tried on 2026-11-14 and dropped on 2026-11-15). Aurora's
findings: S = 8 (one run, one accumulation) costs what OIT off does on its hair fly-through,
with a faint speckle on dark hair cards and more grain on broad glass; S = 16 runs the
accumulation twice. The bench below finds the same here (S = 8 0.75-0.80x sorted alpha, S = 16
1.38-1.46x).

Requirements: Windows or Linux, the OpenGL 4.6 floor (the method itself uses GLSL 4.00 for `gl_SampleMask` and `gl_PrimitiveID`, multisample
textures; the Stochastic shadows' compute resolve, kept for A/B tests, also needs GL 4.3, GLSL 4.30: `imageStore`, `glMemoryBarrier`), at least
8 samples for the three formats, and every prim alpha program's alpha-only variant built. A device without 8 samples per pass for the colour, depth and `GL_MAX_SAMPLES` formats, has
sorted alpha: a capability gate (`sRenderAlphaStochasticSupported`; the OpenGL 4.6 floor is enforced at start-up). On every device the fragment resolve runs (169 bytes a pixel on a device that takes 8 samples in a pass, 160 before 2026-11-14). A device that has the capability but whose targets or alpha-only shaders are not ready (a failed
allocation, a resize, a shader set that did not build) does not switch method (owner decision Q8, 2026-11-13: stochastic methods never fall back, replacing the
2026-11-12 exception): the failure is logged, `RenderAlphaSortMethod` and `RenderShadowMethod` stay as selected, the shadows
are not changed, and the blended faces are drawn by the plain alpha pass frame by frame (not dropped) while the engine is
not ready. A failed allocation is tried again (every frame loop: 1 s, doubling to 30 s) and a shader set at the next shader
reload, and stochastic transparency runs again by itself when they succeed. There is no notice.
A history or temporal resolve that fails only drops the accumulation: each frame keeps its 8 samples. A snapshot larger than the window is taken in tiles
if the large buffer cannot have the targets, and fails with a message if the window's cannot either.

## Tests and measurements

- `scripts/perf/bench_stochastic_resolve.py`: GPU time per pass of the viewer before the 2026-11-14 upgrade (git ref `STOCH_OLD_REF`, default `1f38b56b50`), of the upgrade before the mask (`STOCH_REF_REF`, `c121dc100c`) and of this tree, on the pan scenes at `STOCH_TEST_SIZE` (the harness blocks of `test_alpha_stochastic.py` run in it).
- `indra/newview/tests/llstochastichistory_test.cpp`: the engine's GL-free rules and the transparency's table (`TRANSPARENCY_TABLE_HASH`, the blue-noise tile a permutation of 0 to 4095 and blue, the Aurora Viewer's by SHA-256), and C++ twins of the dither and the mask lookup with the Aurora Viewer's properties and golden values (computed from its formulas on its own tile file).
- `scripts/perf/test_alpha_stochastic.py`: offscreen (`STOCH_TEST_SIZE=70x37` partial edges), the viewer's shaders with the viewer's pass
  structure: the mask table, every covered sample against a Python model of the mask (8 and 4, each in
  one pass), the total alpha, visibility and composite, an opaque occluder, the glow,
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
