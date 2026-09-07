# OpenGL modernization strategy — evidence first, 4.3 core, throughput

**Status:** DRAFT amended for review (2026-09-07)
**Baseline:** `master` @ `658470c`
**Related:** [migration_strategy.md](migration_strategy.md) · [design_overview.md](design_overview.md) · [phase1_bootstrap.md](phase1_bootstrap.md)

## Purpose

This document sets the modernization plan for the **OpenGL backend**, which is
the active pipeline. **The Vulkan migration is deferred until the OpenGL path is
modern and performant.** OpenGL is the shipping renderer, the reference for
results, and the supported backend for the supported platforms; the work below is
what makes it worthy of being the baseline the eventual Vulkan port is measured
against. The existing Vulkan bring-up (`indra/llvulkan`, the phase docs) is not
abandoned — it is paused, and resumes once this plan's phases are done.

The plan rests on two platform decisions and one scope boundary:

1. **AVX2 is the hardware floor.** The default build is `ReleaseFS_AVX2`
   (`scripts/configure_firestorm.sh` defaults `WANTS_AVX2=TRUE`;
   `USE_AVX2_OPTIMIZATION=ON` → `/arch:AVX2`). `--no-avx2` remains available as an
   opt-out.
2. **OpenGL 4.3 core is the GL floor.** This is an independent GPU/driver
   capability contract, not a consequence of AVX2 CPU support. A `--no-avx2`
   binary is still a 4.3+ binary — the floor is unconditional.
3. **Windows and Linux are the qualification scope.** Apple is outside the
   supported matrix, but wholesale Apple source excision is not part of renderer
   modernization. CI/packaging policy may be handled separately when needed.

## Why 4.3 and not 4.1

Raising the floor only to 4.1 (the macOS ceiling) is possible but misaligned:
Apple is being dropped, and no AVX2 machine needs to be *held* to 4.1. The GL 4.3
threshold is where the pipeline stops being "modernized legacy" and gains the
primitives that genuinely improve throughput and that share a data model with the
Vulkan backend:

- **Compute shaders** — post-processing, texture compression/transcode, particle
  sim, occlusion culling.
- **Shader Storage Buffer Objects (SSBOs)** — structured scene/light/material
  data without texture-unit gymnastics.
- **Multi-draw-indirect (MDI)** — CPU-side draw-call reduction for the draw pools.
- **Immutable texture storage** (`glTexStorage*`) — driver can pick optimal
  layouts; required for the bindless/PBO work.
- **Explicit attribute location** — removes the link-time name-binding table.

`glBufferStorage` + `GL_MAP_PERSISTENT_BIT` is 4.4, and DSA is 4.5; both remain
**runtime feature gates** (`gGLManager.mGLVersion >= 4.4f` etc.), which the
existing staircase already expresses. The floor raise does not remove the 4.4+
gates; it removes everything *below* 4.3.

## Guiding rules

- **The floor is unconditional.** GL 4.3 is required to start; there is no
  runtime detection-and-degrade below it. `--no-avx2` does not lower the floor.
- **CPU and GPU floors are independent.** `USE_AVX2_OPTIMIZATION` controls CPU
  code generation only. It must not select OpenGL context behavior or retain
  sub-4.3 rendering paths.
- **Evidence comes first.** Automated captures and frame metrics are established
  before cleanup or functional changes and gate every subsequent phase.
- **OpenGL is the product, not the oracle-for-something-else.** With the Vulkan
  migration deferred, there is no external consumer holding GL's output to a
  fixed contract during this work. The goal of each phase is a *better* OpenGL
  pipeline. Regression is measured against the automated Phase 0 baseline for
  the viewer's own observable behavior and performance. That same evidence
  corpus later becomes the seed for Vulkan comparison; Vulkan does not constrain
  how the OpenGL implementation is modernized.
- **Each phase is independently mergeable** and leaves the tree green on the
  supported configs (Windows/Linux, AVX2 and `--no-avx2`).

## The capability contract

The viewer requests an OpenGL 4.3 core context and refuses startup below that
floor with an actionable diagnostic containing the selected GPU, driver, and
reported GL version. The contract applies equally to AVX2 and `--no-avx2`
builds. No `LL_GL_LEGACY_FALLBACK` definition is introduced: once startup
requires 4.3, sub-4.3 rendering paths are unreachable in every supported build.

Context enforcement lands before old feature gates are deleted. This preserves
a reviewable qualification point: first prove the new contract on AMD and
NVIDIA under Windows and Linux, then remove code the contract makes unreachable.

## Phases

### Phase 0 — Automated baseline captures and frame metrics

This phase is the prerequisite for all cleanup and modernization work. Extend
the existing deterministic UI/capture tooling into a repeatable corpus covering:

- login and representative UI states;
- avatars, terrain, water, shadows, alpha/OIT, particles and HUDs;
- PBR materials, reflection probes, deferred lighting and post-processing.

Every run records the source revision, build configuration, GPU/driver identity,
window and graphics settings, CPU and GPU frame time, draw count, buffer-upload
bytes, texture-upload bytes, and shader compilation time. Captures and metrics
must be machine-readable and comparable with a previous accepted run.

Establish AMD and NVIDIA Windows baselines first, then the corresponding Linux
baselines. A later phase does not pass merely because it builds: its capture
comparison must remain within the documented tolerance and its frame metrics
must show no unexplained regression.

### Phase 1 — Renderer-only proven-dead hygiene

- Remove `ENABLE_GL_WORK_QUEUE` (`#define ... 0`) and its disabled `GLWorkQueue`
  implementation and guarded call sites in `indra/llrender/llvertexbuffer.cpp`.
- Remove the dead `LLPipeline::checkReferences` blocks and orphaned
  `check_references(LLSpatialGroup*, LLDrawInfo*)` helper in
  `indra/newview/pipeline.cpp`.
- Restrict this phase to renderer code that is demonstrably excluded from every
  build or has no live callers. Each removal is a separate, buildable,
  bisectable change and must pass the Phase 0 evidence gate.
- Outcome: less inert renderer machinery with no observable behavior change.

### Phase 2 — Explicit GL 4.3 core startup contract

Context creation:

- `indra/llwindow/llwindowwin32.cpp` (~line 1965): `mMaxGLVersion =
  llclamp(mMaxGLVersion, 3.f, 4.6f)` → floor at `4.3f`; line ~1974 drop the
  `WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB` branch (core only).
- `indra/llwindow/llwindowsdl2.cpp` (Linux, SDL): set
  `SDL_GL_CONTEXT_PROFILE_CORE` + `SDL_GL_CONTEXT_MAJOR/MINOR_VERSION = 4.3` at
  context creation (currently only color/depth/stencil attributes are set).

Startup validation:

- Reject a context reporting less than OpenGL 4.3 before renderer
  initialization.
- Report the selected GPU, driver version and reported OpenGL version in the
  failure notification and log.
- Qualify AMD and NVIDIA under Windows and Linux before deleting old feature
  gates. AVX2 and `--no-avx2` builds exercise the same GL contract.

### Phase 3 — Post-contract simplification

Feature detection (`indra/llrender/llgl.cpp`): collapse the staircase. Every
`mGLVersion < N` gate with `N < 4.3` is unreachable and deleted; keep the
`4.39f/4.49f/4.59f` runtime gates.

Shader emission (`indra/llrender/llshadermgr.cpp` ~578–622): delete
`#version 140/150/330` emission and the GLSL 1.30 `precision mediump` shims; on a
4.3+ driver emit `#version 430` (or retain `400/420` tiers as an internal
optimization — decision at implementation time). All 225 `.glsl` sources carry no
`#version` and are emitted at 400+ today, so no shader-source rewrite is needed.

Feature manager (`indra/newview/llfeaturemanager.cpp` ~824): delete the Intel
synthetic-3.3 downgrade and its 3.33f dual-application guard; the target class
cannot run the binary. `TARGET_GL_VERSION` collapses to a no-op on the supported
matrix.

Other sub-4.3 gates: `indra/newview/llglsandbox.cpp`, `pipeline.cpp`,
`llreflectionmapmanager.cpp` — delete the `< 3.x / < 4.0x` branches.

Outcome: one qualified 4.3-core renderer contract, a smaller feature-detection
surface, and no sub-4.3 shader prologues or runtime degradation paths.

### Phase 4 — Throughput modernization (the actual payoff)

With the floor at 4.3 core, the high-leverage work, in dependency order:

1. **Immutable texture storage + PBO async upload** in
   `indra/llrender/llimagegl.cpp` — removes `glTexImage2D` re-specification
   stalls; resolve the `sGLCoreProfile` rectangle-texture fallbacks.
2. **Persistent-mapped vertex/index buffers** (`glBufferStorage` +
   `GL_MAP_PERSISTENT_BIT`, runtime-gated ≥4.4) in `llvertexbuffer.cpp` —
   ring-buffer offsets kill per-frame map/unmap stalls.
3. **Draw-call reduction** — instancing / multi-draw-indirect across
   `pipeline.cpp` (100 raw `gl*` calls), `lldrawpoolterrain.cpp` (61),
   `llviewerwindow.cpp` (49); leverage the existing `gPipeline.stateSort`.
4. **Compute post-processing** — move bloom/DoF/FXAA and texture transcode to
   compute where profitable; fold `RenderGlow` into an always-on post chain.
5. **SSBO scene/light/material data** — restructure scene/light/material data
   into structured buffers. This is done for GL's own benefit first; it happens
   to also be the layout a future Vulkan path would consume, but that is a
   byproduct, not a driver.

## Regression discipline

The Phase 0 capture and metric corpus gates every later phase. Re-run it after
each independently mergeable change to confirm visual output is unchanged within
tolerance and frame time has not regressed. Retain every accepted result with its
source/build/GPU identity. When Vulkan migration resumes, this corpus becomes the
seed for the GL↔Vulkan diff harness described in
[migration_strategy.md](migration_strategy.md).

## Out of scope

- **The Vulkan migration** — deferred until this plan is complete; tracked by the
  `phase*` docs and resumed afterward.
- Any runtime below GL 4.3, and Apple platforms.
- Wholesale Apple source excision; unsupported-platform source and CI policy are
  handled separately from renderer modernization.
- Codebase-wide dead-code or deprecated-symbol sweeps outside the renderer-only
  Phase 1 targets.
- Flattening shader classes or removing the `mShaderLevel` fallback mechanism.
