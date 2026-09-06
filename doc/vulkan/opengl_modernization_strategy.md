# OpenGL modernization strategy — AVX2 floor, 4.3 core, no-Apple

**Status:** DRAFT for review (2026-09-06)
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

The plan rests on three platform decisions:

1. **AVX2 is the hardware floor.** The default build is `ReleaseFS_AVX2`
   (`scripts/configure_firestorm.sh` defaults `WANTS_AVX2=TRUE`;
   `USE_AVX2_OPTIMIZATION=ON` → `/arch:AVX2`). `--no-avx2` remains available as an
   opt-out.
2. **OpenGL 4.3 core is the GL floor.** Every AVX2-capable CPU (Haswell 2013 /
   Excavator 2015 onward) pairs with a driver providing GL 4.3+. A `--no-avx2`
   binary is still a 4.3+ binary — the floor is unconditional.
3. **No Apple support.** The project has no use for the viewer on Apple silicon;
   all `LL_DARWIN` code, Objective-C++ sources, and the macOS CI leg are compiled
   out.

The rationale for coupling 1 and 2: the GPUs that need GL < 4.3 fallbacks
(pre-Haswell Intel HD 4X00/5X00) cannot execute AVX2 and therefore cannot run the
default build at all. The legacy paths are not "degraded but running" safety nets
on the supported hardware matrix — they are unreachable. This document deletes
them rather than shipping them dead.

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
- **Legacy fallbacks are gated, not the floor.** Where old-hardware fallback code
  is retained for the non-AVX2 escape hatch, it is compiled only when the AVX2
  flag is off — "compiled in for older processors, compiled out otherwise."
- **OpenGL is the product, not the oracle-for-something-else.** With the Vulkan
  migration deferred, there is no external consumer holding GL's output to a
  fixed contract during this work. The goal of each phase is a *better* OpenGL
  pipeline, and regression is measured against the viewer's own behavior (visual
  correctness, frame time), not against a frozen capture set for another backend.
  A capture baseline is still recorded at the start of Phase 5 purely as a
  self-consistency check, and it doubles as the seed corpus for the future Vulkan
  diff harness.
- **Each phase is independently mergeable** and leaves the tree green on the
  supported configs (Windows/Linux, AVX2 and `--no-avx2`).

## The compile-out mechanism

`USE_AVX2_OPTIMIZATION` is already a source-visible compile definition
(`indra/CMakeLists.txt`). Because the 4.3 floor applies to **both** AVX2 and
`--no-avx2` builds, the flag does not select the floor — it selects whether the
sub-4.3 *runtime fallback code* is present. To express that intent directly,
derive a single definition rather than piggybacking on a CPU flag:

```cmake
# indra/CMakeLists.txt (near the AVX2 option block)
if (NOT USE_AVX2_OPTIMIZATION)
  add_compile_definitions(LL_GL_LEGACY_FALLBACK)
endif()
```

Guards then read as intent:

```cpp
#ifdef LL_GL_LEGACY_FALLBACK
    // sub-4.3 compat paths, retained only for the --no-avx2 escape hatch
#else
    // 4.3+ core path (default)
#endif
```

Apple code is not toggled by a flag — it is **deleted** (sources, CMake
conditionals, CI leg). The `LL_DARWIN` macro simply ceases to be defined because
the toolchain blocks that set it are removed.

## Phases

### Phase 1 — Build/CI seam (no behavior change)

- Add the `LL_GL_LEGACY_FALLBACK` derived definition as above.
- Remove `macos-15` from the `build_matrix` in
  `.github/workflows/build_viewer.yml`; delete
  `.github/workflows/build_sl_mac_only.yml`.
- Remove `--no-avx2` references from docs where they implied a sub-4.3 target;
  document that `--no-avx2` still requires GL 4.3.
- Outcome: Windows + Linux build green; no functional change yet.

### Phase 2 — GL floor to 4.3 core

Context creation:

- `indra/llwindow/llwindowwin32.cpp` (~line 1965): `mMaxGLVersion =
  llclamp(mMaxGLVersion, 3.f, 4.6f)` → floor at `4.3f`; line ~1974 drop the
  `WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB` branch (core only). Guard the
  compat branch with `LL_GL_LEGACY_FALLBACK` if it must survive for `--no-avx2`;
  otherwise delete.
- `indra/llwindow/llwindowsdl2.cpp` (Linux, SDL): set
  `SDL_GL_CONTEXT_PROFILE_CORE` + `SDL_GL_CONTEXT_MAJOR/MINOR_VERSION = 4.3` at
  context creation (currently only color/depth/stencil attributes are set).

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

Outcome: viewer refuses to start below GL 4.3 core; legacy runtime paths compiled
out on AVX2, present only under `LL_GL_LEGACY_FALLBACK`.

### Phase 3 — Apple excision

- Delete the 8 Objective-C++ sources in `indra/llwindow`
  (`llwindowmacosx-objc.mm`, `llopenglview-objc.mm`, `llwindowmacosx.cpp/.h`,
  `llkeyboardmacosx.cpp/.h`, `llappdelegate-objc.h`, `llopenglview-objc.h`,
  `llpreeditor.h` Cocoa bits) and `indra/mac_crash_logger`.
- Strip `#if LL_DARWIN` / `#ifdef LL_DARWIN` blocks: concentrated in `newview`
  (42 files), `llcommon` (20), `llrender` (10), `llwindow` (9), `llfilesystem`
  (5), `media_plugins` (5), `llplugin` (4). 106 files total reference the macro.
- Remove `DARWIN`/`APPLE` conditionals from `indra/CMakeLists.txt` and
  `indra/cmake/*.cmake`; remove the macOS legs from `configure_firestorm.sh` /
  `build.sh`.
- Outcome: a Windows/Linux-only tree; `LL_DARWIN` no longer defined anywhere.

### Phase 4 — Render-stack cruft sweep

Remove the dead weight the survey identified in the rendering path:

- **Dead (`#if 0`) scaffolding** — `LLPipeline::checkReferences` ×3 and the
  orphaned `check_references(LLSpatialGroup*, LLDrawInfo*)` helper in
  `indra/newview/pipeline.cpp` (~2452–2545).
- **Superseded experiment** — `ENABLE_GL_WORK_QUEUE` (`#define ... 0`) and the
  ~50-line `GLWorkQueue` class + call sites in
  `indra/llrender/llvertexbuffer.cpp` (74–116, 970, 1000); the threaded-GL
  submission model is superseded by the main-thread model.
- **Shader-class tiering** — all `mShaderLevel` classes are hardcoded constants
  in `indra/newview/llviewershadermgr.cpp` (613–636); the class2/class3 directory
  tiering (10 + 19 files) is selection theater. Merge to a flat set and collapse
  the level indirection.
- **Stale comments / fixed-function archaeology** — `fxaaF.glsl` `#version 120`
  header history; residual fixed-function globals (`gGLModelView`,
  `gGLProjection`, `gGLLastModelView` in `llrender.cpp:55`) behind the
  immediate-mode façade.

### Phase 4b — Codebase-wide hygiene (dead / unresponsive code)

An addition to the OpenGL modernization scope: extend the dead-code removal
beyond the render stack to the whole `indra/` tree, to lighten the codebase and
make it more maintainable. This is a **hygiene-only pass — zero behavior
change**, separable from the functional work and landable in reviewable batches.

**Scope rule: comments are free and stay.** This phase removes *dead code*, not
commentary. `//` comments — including commented-out code that documents a
trade-off, a reference implementation, or a known-fragile path — are left alone.
Only code the compiler never sees (`#if 0`) or that nothing calls (dead symbols,
dead flags) is in scope.

Measured residue at `@658470c` (so the sweep is tracked against a baseline):

- **`#if 0` dead blocks: 172 sites across 89 files**, concentrated in `newview`
  (86), `llmessage` (9), `llcommon` (8), `llmath` (5), `llcorehttp` (4),
  `llrender` (4). Unconditionally compiled out; deletion is provably
  behavior-preserving. This is the primary target.
- **`#if 0 && …` debug switches: 5 sites** (`llfile.cpp:286`, `llmemory.h:137`,
  `llgl.cpp:1083`, `lldrawable.cpp:1460`, `llviewerpartsim.cpp:634`). Hand-toggled
  debug scaffolding — review individually: delete if the debugging need is stale,
  or convert to a proper named toggle if still useful.
- **`DEPRECATED` (386) / `OBSOLETE` (57) markers** — audit whether the flagged
  API still has callers; where a deprecated symbol has zero live call sites,
  remove the symbol and its declaration together.
- **Unresponsive / superseded feature flags** — feature-table rows shipped
  disabled (87 of 169 in `featuretable.txt`) whose backing code path is itself
  dead, and hardcoded-constant "dynamic" selectors (the `mShaderLevel` pattern).
  Remove the flag and the dead branch together.

Method and guardrails:

- **Order: `#if 0` → deprecated-symbol → dead flags.** Deleting `#if 0` blocks
  first shrinks the surface the later passes must reason about.
- **Batch by library/directory** (`llcommon`, `llrender`, `newview`, …) in
  separate commits, so each is reviewable and bisectable.
- **Compile-gate every batch** on both AVX2 and `--no-avx2`; run the existing
  `indra/test` and `integration_tests` suites. A hygiene pass that changes
  behavior is a bug.

### Phase 5 — Throughput modernization (the actual payoff)

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

Because there is no second backend consuming GL's output during this work, the
correctness bar is the viewer's own observable behavior. Record a capture set
(deterministic UI scene + representative world frames) from the pre-change
pipeline at the start of Phase 2, and re-run it after each Phase 2 and Phase 5
change to confirm visual output is unchanged within tolerance and frame time has
not regressed. This capture set is retained: when the deferred Vulkan migration
resumes, it becomes the seed corpus for the GL↔Vulkan diff harness described in
[migration_strategy.md](migration_strategy.md).

## Out of scope

- **The Vulkan migration** — deferred until this plan is complete; tracked by the
  `phase*` docs and resumed afterward.
- Any runtime below GL 4.3, and Apple platforms.

Note: dead-code removal is **in scope** codebase-wide (Phase 4b), not limited to
the render path. Comments — including commented-out code — are preserved.
