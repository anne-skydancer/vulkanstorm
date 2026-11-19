<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026, Anne Skydancer (Vulkanstorm) -->
# Slang shaders

The viewer's shaders move from hand-written GLSL to Slang in **one switchover** (owner decisions D1 to D5, 2026-11-18; the
study is in `reports/slang-shaders-2026-11-18/`). Slang is a **build tool**: the GLSL it writes is committed and is what
the OpenGL viewer compiles. No Slang compiler or library is in the viewer or the installers. The same sources later
compile to SPIR-V for the Vulkan line.

This page describes the groundwork (stage S1) and the pilot (S2, the interface family and the binding model).

## Decisions in force

- **D1** One switchover, no mixed tree in any line. All conversion work is on `codex/slang-switchover`; its PR against
  `vkstorm-devel` stays a draft until the light and shadow rebuild settles. The branch is rebased on devel, never merged
  with it.
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
| `scripts/shaders/slang_check.py` | CI checks: manifest, interface, position gate, glslang compile and link |
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

`program_list.json` names each program: its `source`, its `entries` (stage to entry point; vertex, fragment or compute), and
optional `permutations`, an object of axes to value lists. Every combination is one program id
(`name__AXIS0_OTHER1`) compiled with `-DAXIS=0 -DOTHER=1`. Permutations are for inputs that change a declaration. Settings
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

- **Uniforms are one std140 block**, `block_GlobalParams_0`, shared by both stages and pinned at binding 4
  (`slangc -fvk-bind-globals 4 0`), above the viewer's own UBO bindings 0 to 3 (`LLGLSLShader::UniformBlock`); `slang_check.py
  interface` fails a block below that. After the link `LLGLSLShader::mapGeneratedUniforms()` reads the block's size and binding
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
- **Windows and Linux output.** The generated tree was written on Windows; the CI drift check runs on Linux. Run the job
  once before the tree grows; if the two differ, fix the generator, not the check.

## The pilot (S2): the interface family

Programs are registered by `interfaceStages()` in `llviewershadermgr.cpp` (`VKS_SLANG_PILOT=0` selects the hand-written GLSL for the A/B; both
the switch and the GLSL of the converted programs go when the pilot is accepted). Converted: ui, alphamask, benchmark, clip, copy
(+ depth), glowcombine, glowcombineFXAA, gaussian, reflectionmip, solidcolor, onetexturefilter, twotexturecompare, occlusion
(+ cube), highlight normals and specular, pathfinding (with and without normals), radianceGen (probes and hero), irradianceGen.
Left hand-written for now: the programs with a skinned variant built from the same files (`gHighlightProgram`, `gDebugProgram`,
`gSkinnedOcclusionProgram`, `normaldebug`, which has a geometry stage) wait for the skinning module (S3), and `pbrTerrainBake`.

## The position module

Every program takes its position (skinning included) from **one shared module**, written before the families convert. The
gate in `slang_check.py position` fails when any `.slang` file other than the module writes `gl_Position`, returns
`SV_Position` straight from a function, or assigns an `SV_Position` field from anything but a call of the module's
`vkPosition*` functions (whole, with plain `=`); when a program's vertex source does not import the module or never calls
it; or when a generated vertex stage does not write `gl_Position` exactly once. The generated text cannot show where the
value came from (slangc copies the field to `gl_Position`), so the source check is the guard. The gate is wired and **allowed empty**: it passes while no program has a vertex stage, and fails once one does if
`indra/newview/slang/position.slang` is missing. The AMD decal `invariant`/`precise` goes into that module when needed.

## CI

`Check PR` job `slang-shaders` (Linux): fetch the pin, run `scripts/tests/test_slang_pipeline.py`, then
`slang_gen.py --check --strict` (generated output drift) and `slang_check.py all` (manifest, interface from reflection,
position gate, and every stage compiled and every program linked with glslang as GLSL 4.60 for OpenGL, once with every hook 0
and once with every hook 1). glslang is not a driver; the visual A/B and the viewer's own shader load are the driver checks.
The `ci-checks` action also runs the loader-format test and the tool tests that need no compiler.

## Visual A/B harness

`python scripts/shaders/ab_harness.py radiance` renders the hand-written radiance filter and its generated program from the
same fixed inputs (a procedural environment cube array; three faces, four mip levels, two layers), reads both back as float32 and
compares them. `--selftest` proves the harness: identity gives zero differing values, one changed constant is caught, and the
generated-program path (uniforms packed into a std140 block from introspection, a sampler with `layout(binding)`) renders
the fixture program bit for bit like a loose-uniform twin. Windows and NVIDIA now; the RX 9070 XT run comes later. Exit code
2 means the candidate is not generated yet.

## Licence

Slang is Apache-2.0 WITH LLVM-exception, a build tool, not shipped (row in `doc/licensing.md`). Generated files carry the licence
lines of their source. New files carry `SPDX-License-Identifier: GPL-3.0-or-later`.

## Not touched

The hot families stay hand-written GLSL while their rebuild stages run: alpha and material blend, stochastic transparency and
casters, `shadowUtil`, `shadowStochasticUtil` and the depth programs, deferred lights and BRDF, `softenLight`, probes, SSR,
sky, atmosphere and water.
