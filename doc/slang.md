<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Slang shaders

The viewer's shaders move from hand-written GLSL to Slang (owner decisions D1 to D5, 2026-11-18, D1 replaced by decision A on
2026-11-19; the
study is in `reports/slang-shaders-2026-11-18/`). Slang is a **build tool**: the GLSL it writes is committed and is what
the OpenGL viewer compiles. No Slang compiler or library is in the viewer or the installers. The same sources later
compile to SPIR-V for the Vulkan line.

This page describes the groundwork (stage S1), the pilot (S2, the interface family and the binding model), and the
conversions of objects, effects, post-processing and G-buffer writers.

## Decisions in force

- **D1 (decision A, 2026-11-19)** The PR with the loader, the quiet families and the shared position module lands on
  `vkstorm-devel` first. Each hot family (alpha and material blend, stochastic transparency and casters, `shadowUtil`,
  `shadowStochasticUtil` and the depth programs, deferred lights and BRDF, softenLight, probes, SSR, sky, atmosphere, water)
  then converts in its **own PR against devel**, once its light and shadow rebuild stage has settled. Devel carries a mixed
  tree meanwhile. The switchover is complete when the last hot family lands: `invariant` goes on for every generated program,
  the hand-written-GLSL loader path of converted programs (`VKS_SLANG_PILOT`) is removed, and the position baseline holds only
  the permanent exemptions (see "End state"). A PR branch is rebased on devel, never merged with it.
- **Drafts** Keep all Slang conversion PRs in draft until every shader in scope has been converted.
- **Pin** The latest stable Slang release (`scripts/shaders/slang_pin.json`: 2026.22), fetched with a checksum, bumped only
  on purpose, in a PR of its own. Each bump regenerates every program and re-runs the bench and the visual A/B.
- **D3** The Slang output is taken **as it is**: no hand fix to match today's GLSL bit for bit. Tiny float differences and
  new program checksums are accepted. The image test is a sanity check. **Exception:** positions, depth and coverage agree
  exactly between passes; the shared position module guarantees it.
- **D4** At the switchover the C++ side moves to Slang's binding model: uniform blocks from reflection, explicit bindings,
  no restore step back to loose uniforms. The generator therefore keeps Slang's `block_GlobalParams_0` block, its
  `layout(binding)` samplers and `layout(location)` varyings. Its per-frame CPU cost is measured in the pilot first; if it
  costs real CPU per frame the stage stops and options are reported.
- **D5** GLSL-flavoured Slang: `import glsl`, `vec`/`mat` types and `*`. Entry points use struct inputs and outputs with
  `[[vk::location(N)]]`, so both stages of a program compile together and share one uniform block.

## Layout

| Path | What |
|---|---|
| `indra/newview/slang/` | Slang sources (not staged, not hashed), `program_list.json`, the position module |
| `indra/newview/app_settings/shaders/generated/` | generated GLSL, one file per program, permutation and stage, and `programs.json` |
| `scripts/shaders/slang_pin.json` | the pinned release: version, archive URL and SHA-256 per platform |
| `scripts/shaders/slang_fetch.py` | fetches the pin into the tool cache (outside the tree), checks SHA-256 and `slangc -v` |
| `scripts/shaders/slang_gen.py` | the generator; `--check` is the drift check |
| `scripts/shaders/slang_check.py` | CI checks: manifest, interface, position gate, licence headers, glslang compile and link |
| `scripts/shaders/ab_harness.py` | visual A/B harness |
| `scripts/shaders/fixtures/` | a small program that exercises the pipeline in tests |
| `indra/llrender/llgeneratedshader.h` | the loader's source format (header-only, tested on its own) |

## The workflow

```
python scripts/shaders/slang_fetch.py        once: the pinned slangc into the tool cache
python scripts/shaders/slang_gen.py          regenerate (parallel; about 0.15 s per stage)
python scripts/shaders/slang_check.py all    manifest, interface, position gate, glslang
```

Edit the `.slang` files only. A conflict in a generated file is resolved by regenerating, never by hand.

`program_list.json` names each program: its `source`, its `entries` (stage to entry point; vertex, fragment or compute),
an optional qualification `family`, and optional `permutations`, an object of axes to value lists. Every combination is one program id
(`name__AXIS0__OTHER1`) compiled with `-DAXIS=0 -DOTHER=1`. Permutations are for inputs that change a declaration. Settings
and device values are **hooks**.

slangc prints every entry point of one compile to standard output (its `-o` per entry point does not work for GLSL in
2026.22), each starting with its own `#version` line; the generator splits on those lines. Compiling both stages together is
what gives them one shared uniform block, which the driver requires.

## Hooks (settings and device values as run-time macros)

A Slang function whose GLSL case is `__intrinsic_asm "(NAME)"` becomes `((NAME))` in the output. The name must be listed in
`program_list.json` under `hooks`; the generator fails on an unlisted one and records the hooks each file uses in its
header line `// hooks: A B` and in `programs.json`. The loader defines each as the program's define of that name, or 0.

## The loader path

A program is generated when its shader files start with `generated/`. For such a program `LLGLSLShader::createShader` reads each
stage with `LLShaderMgr::loadGeneratedShaderFile` and hands the driver, in one piece:

```
#version 460
#define <hook> <value>        one line per hook in the file's "// hooks:" line
<the whole file>
```

There is no 4,096-piece limit, no `attachShaderFeatures`, no class search, no shader-level retry and no fallback to
hand-written GLSL. A stage that does not compile fails the program with the driver's log. A program that mixes generated
and hand-written stages is refused. The format is `LLGeneratedShader::assemble` and, for the checks, `assemble_source()` in
`slang_check.py`; both are tested against one literal.

## The binding model on the C++ side (pilot S2, D4)

A generated program is bound the way slangc writes it; nothing is restored to loose uniforms.

- **Uniforms are one std140 block**, `block_GlobalParams_0`, shared by both stages and pinned at binding 5
  (`slangc -fvk-bind-globals <n> 0`, n = the viewer's UBO count, `LLGLSLShader::UniformBlock`), above the viewer's own UBO bindings; `slang_check.py
  interface` fails a block below that, and the loader refuses a program whose block binding is below the viewer's UBO count
  (a new viewer UBO needs the generated tree regenerated, which the `slang-shaders` CI job reports as drift). After the link `LLGLSLShader::mapGeneratedUniforms()` reads the block's size and binding
  from the driver and the members' byte offsets from the manifest, makes one `GL_DYNAMIC_DRAW` buffer per program and a CPU copy
  of it (`mBlockData`). `mUniform` and `mUniformMap` hold the **member's byte offset** where a loose program holds its location,
  so every existing `uniform*()` call keeps working: the setters end in `slUniform*()`, which calls `glUniform*` for a hand-written
  program and writes the block copy (compared with `memcmp`, so an unchanged value stays clean) for a generated one. std140 rules
  are applied there: an array of scalars or vectors has a 16 byte stride, a matrix is its columns on 16 byte boundaries.
- **One upload per draw.** `LLGLSLShader::flushBlock()` runs in the draw calls (`LLVertexBuffer::drawRange`, `drawRangeFast`,
  `drawArrays`, the downscale draw in `llimagegl.cpp`): if the bound program's block is dirty it is sent with one
  `glNamedBufferSubData`. `bind()` rebinds the program's buffer at the block binding when another generated program changed it.
  Code that calls `glUniform*` itself with `getUniformLocation()` (the materials pool does) cannot be used on a generated program.
- **Samplers have explicit units** (`layout(binding = N)`, N from 0, declaration order): `mTexture[]` takes the unit from the
  manifest, `mUniform[]` stays -1, so `uniform1i(sampler, unit)` is a no-op and `bindTexture()` binds the declared unit. Declare
  the sampler the viewer binds first (`diffuseMap`) first.
- **Vertex attributes** come from the manifest's vertex `inputs` (location is the reserved slot): `mapGeneratedAttributes()` links
  without `glBindAttribLocation` and builds `mAttribute` and `mAttributeMask` from them. A vertex input the stage does not read is
  not declared, so the mask is the one `glGetAttribLocation` gave the hand-written program.
- **Hooks.** A hook takes the program's define, else the viewer-wide `sGlobalDefines` value, else 0 (a debug log line names a
  hook with no value). The binary-cache key (`LLGLSLShader::hash()`) includes the text of each generated file.
- **Windows and Linux output.** The generated tree was written on Windows; the CI drift check (`slang-shaders`) runs on Linux and
  passes on the pilot head, so the two agree. If they ever differ, fix the generator, not the check.

## The pilot (S2): the interface family and the quiet families

Programs are registered by `generatedStages()` in `llviewershadermgr.cpp` (`VKS_SLANG_PILOT=0` selects the hand-written GLSL for the A/B; both
the switch and the hand-written GLSL path of the converted programs go when the last hot family lands). Converted: ui, alphamask, benchmark, clip, copy
(+ depth), glowcombine, glowcombineFXAA, gaussian, reflectionmip, solidcolor, onetexturefilter, twotexturecompare, occlusion
(+ cube), highlight normals and specular, pathfinding (with and without normals), radianceGen (probes and hero), irradianceGen.
Left hand-written for now: `normaldebug` (it has a geometry stage) and `pbrTerrainBake`. The programs with a skinned variant moved with the skinning module (below).

## The position module

Every program takes its position (skinning included) from **one shared module**, `indra/newview/slang/position.slang`:

| Function | Use |
|---|---|
| `vkPositionClip(mvp, p)` | static prims, attachments, UI and full-screen quads |
| `vkPositionNdc(p)` | points already in -1..1 |
| `vkSkinObject(palette, weight4)` | the rigged object's skin: four joints of the 110-matrix palette (`VK_MAX_JOINTS`, checked against `LL_MAX_JOINTS_PER_MESH_OBJECT` by the tests) |
| `vkPositionRigged(projection, modelview_skin, p, eye)` | a rigged point: `projection * (modelview * skin * p)`, with the eye-space position out for normals and varyings |

A program declares its own `matrixPalette` uniform and passes it in, so a program that does not skin carries no palette in its block.

**The gate.** `slang_check.py position` is armed: the module must exist whatever programs there are. It fails when any other `.slang` file
writes `gl_Position`, returns `SV_Position` straight from a function, or assigns an `SV_Position` field from anything but a call of the
module's `vkPosition*` functions (whole, with plain `=`); when a program's vertex source does not import the module or never calls it; when a
generated vertex stage does not write `gl_Position` exactly once; and when the module makes the position invariant and a stage does not say so.
The generated text cannot show where the value came from (slangc copies the field to `gl_Position`), so the source check is the guard.
The hand-written GLSL that still writes `gl_Position` is the list `scripts/shaders/glsl_position_baseline.txt`, which **only shrinks**: a new
`.glsl` file that writes it fails, and so does a listed file that no longer does. The switchover empties it.

**The AMD decal fix.** A tattoo or applier is drawn over the body by another program than the body, so the two positions must agree bit for bit.
Two things give that. The skinned position is one text in each program's single compilation unit (the hand-written skinning was a separate shader
object, which GLSL 4.60 section 4.8.1 gives no guarantee across). And every vertex program declares `invariant gl_Position;`, which the module
emits through a prelude (`VK_POSITION_INVARIANT`; 0 removes it from every program at once. It is 0 until the switchover: with hand-written programs still drawing over generated ones, the invariant position moved the depth of the rigged bump by up to 2 ULP on NVIDIA against them (`ab_harness.py programs`, the `depth (info)` rows), and 0 makes it agree exactly. The switchover sets it to 1 for every program together). Slang's GLSL front end rejects `invariant
gl_Position;` as source, and its `precise` is dropped from the GLSL whenever the temporary folds away, so the prelude is the one way that holds.
`invariant` makes the driver multiply then add where it would fuse them: depth moves by a few steps on NVIDIA (invariance-audit.md 3.4), and the
hand-written programs still outside the tree keep their fused form until the switchover, so the interim tree mixes the two. Not yet measured on AMD.

The system avatar's own skin (`avatarV.glsl`, the 45-row eye-space palette) joins the module with its program, which waits for the forward
lighting and atmospherics.

## Objects family (S3, first part)

Converted: bump (static and rigged), impostor, object preview (static and rigged), physics preview, draw colour, and the programs that
waited for the skin: highlight (static and rigged), debug (static and rigged), the skinned occlusion. `VKS_SLANG_PILOT=0` selects their
GLSL, as for the interface family. `make_rigged_variant(..., generated_id)` names the `HAS_SKIN` 1 program. The IS_AMD_CARD static
references of the old palette arrays (a loose-uniform storage workaround) have no meaning for a block member and are gone. `mirrorclip.slang`
is the one import for the mirror clip (it adds `mirror_flag` and `clipPlane` to the block of a program that uses it).

Not converted, and why: `avatarV/F`, `simpleNoColorV` and `simpleF` call the forward lighting (`calcLighting`) and `calcAtmospherics`
(windlight), which belong to the lighting and atmosphere families. `eyeballV/F` were never registered and are deleted.
The block upload sends the dirty byte range, not the block, because the 110-matrix palette makes a rigged program's block 5.4 KB.

## Quiet families (S3, second part): effects, post, G-buffer

Converted: glow (post), glow extract (HAS_NOISE 0 and 1, RenderGlowNoise), vignette, snapshot frame, normal-map generation,
BRDF LUT generation (the MIT notice of the original stays in the source), and the G-buffer programs that need nothing from the hot families:
the deferred bump (static and rigged), impostor, tree and the avatar eyes. Each G-buffer program has a `HAS_EMISSIVE` axis
(RenderEnableEmissiveBuffer): its output struct has a fourth target only then, so `emissiveId()` in `llviewershadermgr.cpp` picks
`name__HAS_EMISSIVE0` or `1`. `gbuffer.slang` now carries `encodeNormal` (the packing of `globalF.glsl`) and the `HAS_ATMOS` flag.

Not converted, and why:

| Programs | Reason |
|---|---|
| forward lighting (`avatarV/F`, `simpleNoColorV`, `class1/lighting`) | the forward avatar and no-colour alpha-mask programs (`gAvatarProgram`, `gObjectAlphaMaskNoColorProgram`) call `calcAtmospherics` (windlight, atmosphere is a hot family); nothing else uses the forward lighting |
| every deferred world program that calls `calcAtmospherics` (fullbright, emissive, terrain, alpha) and `rlv` (`getPositionWithDepth`, `deferredUtil`) | atmosphere and `deferredUtil` are hot |
| diffuse, diffuse alpha mask, non-indexed variants | the texture-index lookup (`diffuseLookup`, `passTextureIndex`) is composed at run time from the device's texture unit count; needs its own design (a hook or a permutation per count) |
| `pbropaque`, `pbrglow`, `pbrterrain`, `pbrTerrainBake` | the glTF texture-transform and terrain-mix utilities; one module for the glTF family, with glTF |
| glTF (`pbrmetallicroughness`) | uses the lighting, shadows and probes (hot) and was changed by hotfixes on 2026-11-17 and 2026-11-18 |
| environment (`water*`, `underWater`, `srgbF`) | water and atmosphere are hot; `srgbF` is a helper object |
| `normaldebug` | has a geometry stage |
| SMAA, FXAA, CAS | third-party code, stays hand-written (planner) |

The pilot's radianceGen, irradianceGen, copy-depth and alphamask conversions stay (the owner asked for the radiance filter A/B first);
like every converted program they are regenerated at the switchover from the settled code.

## Deferred post-processing

Luminance, exposure (with and without history), gamma correction (sRGB and legacy), and tone mapping use generated
programs. The six tone-mapping permutations combine three gamma modes (none, sRGB, legacy) with post-processing on
or off. The ACES and Khronos Neutral curves, exposure, and blend factor remain uniform-driven in the post-processing
permutations. All four programs share the full-screen vertex stage in `postfullscreen.slang`, which takes its position
from `position.slang`.

The A/B harness exercises luminance across G-buffer flags, exposure from a mip chain with and without the history texture,
and both tone-mapping curves on HDR inputs. The hand-written full-screen vertex and fragment helpers still serve the
GLSL comparison path and other passes that link them.

The depth-of-field passes also use the shared full-screen stage. The circle-of-confusion pass receives `inv_proj`
through matrix synchronization. The blur and bypass import the deferred HDR clamp, including its treatment of
NaN and infinity. The bypass copies sampled depth exactly with or without the final display noise.
Run `ab_harness.py programs --family depth-of-field --strict-depth` to compare the five variants.

## The test gaps of the pilot, closed

- **std140 packing** (`LLGeneratedShader::putArray` and `putMatrix` in `llgeneratedshader.h`, used by `LLGLSLShader::blockPutArray` and
  `blockPutMatrix`): `tests/llgeneratedshader_test.cpp` checks the transposed mat4, mat3 (three columns of three floats, the fourth untouched),
  mat2, the mat3x4 palette with and without transpose, an offset inside the block, and the 16 byte stride of float, vec3, vec4 and int arrays.
- **The manifest parse** (`LLGeneratedShader::parseManifest` in `llgeneratedmanifest.h`, used by `LLShaderMgr::getGeneratedInterface`):
  `tests/llgeneratedmanifest_test.cpp` runs it on small literals (broken JSON, a missing key, a stage without its interface) and on the committed
  `programs.json` (members ordered and never overlapping, unique texture bindings and input locations). It runs in Check PR (`slang-shaders`).
- **The manifest against the driver**: `ab_harness.py manifest` links every generated program and compares the block name and binding, each
  member's byte offset (`GL_UNIFORM_OFFSET`), the member set, each sampler's unit and the vertex input locations with `programs.json`. The harness
  self-test proves it catches a wrong offset, unit and input location. Needs a GPU, so it is run by hand with the A/B.
- **Texture units**: `slang_check.py interface` fails a program whose units are not 0..n-1 in a row or whose `diffuseMap` is not unit 0.
- The block name is the one in `kBlockName` (`test_slang_pipeline.py`, on the committed tree and on the generator's output).

## Visual A/B of every converted program

`python scripts/shaders/ab_harness.py programs [--only TEXT]` (`scripts/shaders/ab_cases.py`) draws each converted program with the
hand-written GLSL and the generated one on a fixed grid mesh, from uniforms and textures filled by type and name, into float targets
(up to four colour attachments and depth), and compares them. The G-buffer programs also run with the mirror clip on, and with the emissive target.
`radiance --program radianceGen__PROBE_FILTER_SAMPLES4` runs the 4-sample hero filter. Results on the NVIDIA 621.14 driver are in the PR.

## CI

`Check PR` job `slang-shaders` (Linux): fetch the pin, run `scripts/tests/test_slang_pipeline.py`, then
`slang_gen.py --check --strict` (generated output drift) and `slang_check.py all` (manifest, interface from reflection,
position gate, and every stage compiled and every program linked with glslang as GLSL 4.60 for OpenGL, once with every hook 0
and once with every hook 1). glslang is not a driver; the visual A/B and the viewer's own shader load are the driver checks.
The `ci-checks` action also runs the loader-format test (with the std140 packing fixtures) and the tool tests that need no compiler; `slang-shaders` also
builds and runs the manifest parse test.

## Visual A/B harness

`python scripts/shaders/ab_harness.py radiance` renders the hand-written radiance filter and its generated program from the
same fixed inputs (a procedural environment cube array; three faces, four mip levels, two layers), reads both back as float32 and
compares them. `--selftest` proves the harness: identity gives zero differing values, one changed constant is caught, and the
generated-program path (uniforms packed into a std140 block from introspection, a sampler with `layout(binding)`) renders
the fixture program bit for bit like a loose-uniform twin. `python scripts/shaders/ab_harness.py skin` draws a wavy skinned grid with the hand-written `bumpV.glsl` plus `objectSkinV.glsl` and with the generated bump
programs (rigged and static) over four palettes and view angles, and compares the eye position per pixel. Exit code
2 means the candidate is not generated yet.

## Licence

Slang is Apache-2.0 WITH LLVM-exception, a build tool, not shipped (row in `doc/licensing.md`). A `.slang` file converted from a Linden Lab or
Firestorm GLSL shader keeps that shader's `$LicenseInfo` header (LGPL-2.1) unchanged, in comment syntax, with its `@file` line
naming the GLSL file it comes from; a converted file whose original had only a third-party notice (the exo post shaders,
Firestorm's snapshot frame) keeps that notice. New files (the shared position and G-buffer modules, the scripts, the fixture)
carry `SPDX-License-Identifier: GPL-3.0-or-later` and the Vulkanstorm copyright line. The generator re-emits the source's header
at the top of every generated file (never edit those by hand), and `slang_check.py licence` fails CI when any `.slang` source or
generated file has neither a `$LicenseInfo` block nor an SPDX line.

## Not touched

The hot families stay hand-written GLSL while their rebuild stages run: alpha and material blend, stochastic transparency and
casters, `shadowUtil`, `shadowStochasticUtil` and the depth programs, deferred lights and BRDF, `softenLight`, probes, SSR,
sky, atmosphere and water.

## Mixed tree: rules until the last hot family lands

- **A hotfix to a converted program is made in its `.slang` source** (and the generated GLSL regenerated). The hand-written GLSL
  of a converted program stays only as the A/B oracle (`VKS_SLANG_PILOT=0`); an edit to it alone changes nothing in the running
  viewer. The same holds for helpers duplicated into a Slang module (`gbuffer.slang`'s packing, `mirrorClip`, the skin): change
  both sides, the token check in `test_gbuffer_module_packs_like_the_hand_written_shaders` guards the G-buffer flags only.
- **Locations of generated programs are byte offsets.** `getUniformLocation` returns a block member's offset (0 is valid, a sampler
  returns -1). No code may test a location for 0, or a sampler's presence, on a generated program. `pipeline.cpp`'s
  `!shader.getUniformLocation(MODELVIEW_MATRIX)` in `bindDeferredShader` and the `DIFFUSE_MAP != -1` tests in `lldrawpoolavatar.cpp`
  must be changed in the PR that converts those programs (the converted programs here are all hand-written at those sites).
- **Draw and dispatch sites flush the block.** Every `glDraw*` goes through `LLVertexBuffer` or `LLImageGL`, which call
  `LLGLSLShader::flushBlock()`; a compute dispatch calls it itself (`pipeline.cpp`, the stochastic shadow resolve).
- **Gate for the first hot family:** run the skin/position A/B and the 3,000-draw, 128-byte upload benchmark on the local
  qualification GPU (NVIDIA GeForce RTX 5070 Ti). `VK_POSITION_INVARIANT` is off until the last hot family lands.

## Hot-family qualification on Windows

The RTX 5070 Ti is the qualification GPU for every hot-family conversion. Save the OpenGL renderer, driver, and comparison
results with the feature revision. Conclusions apply to the measured GPU and driver. The RX 9070 XT decal behavior remains
a separate cross-vendor check.

Run the shader pipeline tests, strict generation drift check and `slang_check.py all` for each conversion. Use the local
driver to check the linked manifest and run the shared position and upload tests before the first hot family:

```powershell
python scripts/shaders/ab_harness.py manifest --expect-renderer 'RTX 5070 Ti' --report manifest.json
python scripts/shaders/ab_harness.py skin --expect-renderer 'RTX 5070 Ti' --report skin.json
python scripts/shaders/ab_harness.py programs --only deferred-bump --strict-depth --expect-renderer 'RTX 5070 Ti' --report depth.json
python scripts/perf/bench_palette_block.py --small-uploads --expect-renderer 'RTX 5070 Ti'
```

Set the `family` of each converted program in `program_list.json` and add legacy/candidate cases for its permutations. For
example, the deferred post family can be qualified with the same command used by later conversions:

```powershell
python scripts/shaders/ab_harness.py programs --family deferred-post --strict-depth --expect-renderer 'RTX 5070 Ti' --report deferred-post.json
```

Family selection requires A/B coverage for every registered permutation. Cases that write depth or depend on depth ordering
compare it exactly. For alpha tests, casters and overlays, compare coverage exactly as well. Add the inputs and runtime hook
values that exercise the family. For a compute resolve, also dispatch both implementations with the same input and compare
the outputs and history over successive frames.

| Family | Cases to exercise |
|---|---|
| Alpha, material blend, and stochastic transparency | Static and rigged cutouts, per-sample coverage, opaque occlusion, and seeded history with disocclusion |
| Shadows and depth | Exact caster depth and masks, cascade and projector edges, static and skinned casters, and fragment/compute resolve parity |
| Deferred lights, BRDF, and softenLight | Legacy and PBR branches, roughness extremes, point and projector lights, and light-volume boundaries |
| Probes and SSR | Both probe filters, mip and layer selection, SSR hits and misses, and history and depth rejection |
| Sky and atmosphere | Legacy and physical sky, sun and moon coverage, LUT outputs, and clouds in still and moving views |
| Water | Above and below the waterline, reflection and refraction, and shoreline and clip-plane coverage |
| glTF and world surfaces | Texture transforms and texture-count permutations, terrain layer weights, and alpha-mask coverage |
| Avatars | Native avatar and rigged-object palettes, lighting and atmospherics, and overlay/decal coverage and depth |

Use the staged viewer for driver startup checks. The owner's in-world Release check is the frame-level gate for rendering
changes. For light and shadow work, compare the legacy and generated paths at the same scene and default tier, and report
the pass cost and frame rate against the standing and moving floors in `AGENTS.md`.

## End state: every remaining program has a place

| Program | Family | Converts in |
|---|---|---|
| alpha, material blend (`alpha*`, `pbralpha*`, `materialV/F`), `avatarAlphaShadow`, `pbrShadowAlphaMask` | alpha / blend | the alpha family PR |
| stochastic transparency and casters, shadow, shadowStochastic and depth programs (`avatarShadow`, tree shadows, `pbrShadow*`), `normgen` | stochastic / shadow | the shadow family PR |
| deferred lights, BRDF (`genbrdflut`), `blurLight`, softenLight | lights | the lights PR |
| `cloudsV`, `cloudsVolComposite`, `moonV`, sky and atmosphere helpers | sky / atmosphere | the sky PR |
| `water*`, `underWater`, `srgbF` | water | the water PR |
| probes, SSR | probes | the probes PR |
| glTF (`pbrmetallicroughness`, `pbropaque`, `pbrglow`, `pbrterrain`, `pbrTerrainBake`) | glTF | the glTF PR (needs lighting, shadows and probes) |
| indexed-texture diffuse family (`diffuse*`, `diffuseNoColor`, `emissive`, `fullbright*`, `bump*` non-skin variants), `terrain`, `rlv` | world surfaces | the surface PR (texture-index lookup design first) |
| forward `avatarV/F`, `simpleNoColor`, `class1/lighting`, deferred `avatarV`, `avatarEyesV`, `impostorV` | avatar | the avatar PR |
| `normaldebug` (geometry stage) | debug | stays hand-written unless Slang geometry output is accepted |
| SMAA, FXAA, CAS vertex and fragment stages, `errorV` | third-party or fallback | **permanent exemptions** |

At the end `scripts/shaders/glsl_position_baseline.txt` is exactly the permanent exemptions (the SMAA vertex stages, `errorV`
and, if kept, `normaldebug`), and `slang_check.py position` fails any other hand-written GLSL that writes `gl_Position`. "Invariant on
for every program" means every generated program (`VK_POSITION_INVARIANT 1`); an exempted program keeps its own declaration,
and SMAA and FXAA are post-process passes with no depth contract. The hand-written loader path is removed for every program that
has a generated twin; the exempted programs keep loading their GLSL.
