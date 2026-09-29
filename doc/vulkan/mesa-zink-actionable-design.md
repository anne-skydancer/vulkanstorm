# Mesa/Zink immediate work: engineering design

Date: 2026-09-29. Implementation baseline: `vkstorm-devel` at `bc8a55c558`.
Status: implemented on `codex/zink-actionable-fixes`; package CI qualification
passed. In-world visual qualification remains pending. No performance claim is made. This document specifies the
four immediate work packages from the revised Mesa/Zink audit. The development
checkout was clean before adding this plan; its hero-probe defects match the
reviewed master code. No master history is to be merged into development.

## Scope and delivery

Develop viewer changes on `codex/zink-actionable-fixes`, based on the then-current
`vkstorm-devel`; integrate the qualified feature back into `vkstorm-devel`.
This is a proposed implementation branch, not one created by this planning task.
Build and stage through Autobuild: RelWithDebInfo, development channel, Tracy and
viewer LTO enabled, matching Release dependencies/features, no installer.

Package changes require a separate branch in `C:/Dev/3p-mesazink`, provisionally
`codex/zink-package-reliability`. Its output is consumed by the viewer through
Autobuild only after qualification. Keep the Mesa source pin and three patches
unchanged while repairing build reliability. Updating Mesa or removing patches
is outside these four items.

No debug opt-in, increased OpenGL requirement, CPU rendering replacement for
existing shaders, speculative presentation change, or format optimization is
part of this implementation. Ordinary reflections remain available during hero
warmup. CPU responsibilities remain scheduling, readiness bookkeeping, GL
submission and existing culling; shading/filtering stays on the GPU.

## 1. Hero mip validity and resource readiness

### LOD contract

Files: `llheroprobemanager.{h,cpp}`, `llreflectionmapmanager.cpp`,
`app_settings/shaders/class3/deferred/reflectionProbeF.glsl` and a small pure
layout helper with a focused test in the existing test infrastructure.

Keep the existing artistic mapping: `lod = (1 - glossiness) * S`, where `S` is
the current scratch-chain level count. For supported power-of-two resolution R,
S is log2(R): levels 0 through S-1 are generated, ending at 2x2. The scratch
sampler's radiance filter is explicitly limited to S-1; the allocated 1x1 tail
must not become a new input accidentally.

Hero weight is nonzero only above glossiness 0.75. Define:

```
scratchLevels = S
outputMaxLevel = min(S - 1, ceil(S / 4.0))
outputLevels = outputMaxLevel + 1
```

At 1024 this generates levels 0-3 instead of 0-1. Preserve `heroMipCount=S` as
the existing sampling scale. Use the same count calculation in CPU generation
and the shader's final LOD bound; test their boundary agreement. Reject invalid
resource dimensions through existing settings/allocation handling; do not apply
the power-of-two formula silently to arbitrary dimensions.

In `tapHeroProbe()`:

1. Return before any hero texture access if `heroProbeCount <= 0`.
2. Compute the existing spatial/glossiness weight. Return if it is zero; a
   zero-weight mix does not make an undefined texture sample safe.
3. Clamp the requested LOD into [0, outputMaxLevel], then sample and blend.

Use the existing `heroProbeCount` field as the readiness flag, preserving the
uniform layout. Audit `LLReflectionMapManager::updateUniforms()` forwarding and
all draw paths so zero/one reaches the uploaded UBO before consumers execute.
Keep a valid texture/sampler binding wherever required by GL validation even
when the shader bypasses sampling. Do not lower GL_TEXTURE_MAX_LEVEL on the
shared scratch/output texture.

### State and lifetime

Proposed manager fields: `mValidScratchFaces` (six-bit mask), `mOutputReady`
(bool), selected-probe identity, and resource-generation identity. Initialize
these and `mHeroData` explicitly. Proposed helpers:
`invalidateHeroContents()`, `hasReadyOutput()` and `computeHeroMipLayout()`.
They must not perform GL readback.

| Event/state | Action |
|---|---|
| Allocation, reset, context loss or resolution/HDR change | Clear mask/readiness and publish heroProbeCount=0 immediately. Rebuild compatible resources through existing reset handling. |
| Selected probe changes, disappears, dies, or scene/teleport reset occurs | Invalidate contents; clear obsolete probe association/occlusion state. Establish the new probe association and bounds before rendering its first face. |
| Face update completes successfully | Mark its bit only after all scratch mip commands for that face have been issued. Return success from the update helper if required to propagate failures. |
| Mask is not 0x3f | Keep regular reflection shading; skip radiance generation that could sample missing faces. |
| Mask becomes 0x3f | Generate all outputLevels for all six faces. Mark output ready only after successful command submission for the full output. |
| Steady state | Preserve staggered updates and regenerate the full output as today. Ordinary camera movement does not invalidate the mask each frame. |
| Mirrors disabled | Publish zero and invalidate contents; do not rely solely on early returns that leave old metadata intact. |

Commands remain ordered on the rendering GL context; readiness means initialized
commands precede consumers, not that the CPU waits for GPU completion. No new
fence/readback is required for this ownership model. If implementation discovers
cross-context writers, stop and specify their synchronization explicitly.

Retain normal 6/rate scheduling during initial warmup: at most rate eligible
render frames to seed six faces (1, 2, 3 or 6). No startup six-face burst is
required. Stale occlusion state must not permanently prevent a new probe warming;
reset that state on identity change and permit initialization to progress.
Do not invalidate on every camera translation: that would prevent convergence.
Temporal freshness improvements are separate from initialized-data correctness.

### Validation

Test layout boundaries for every supported resolution and fractional LOD around
0.75/0.85/1.0 glossiness. Test no exposure before six valid faces, invalidation,
failed update, same-probe progression and probe replacement. Exercise all rates,
HDR/non-HDR, mirror toggles, teleport, relog and shader/context reload. Check the
actual UBO values and capture that only generated mips contribute. Compare native
GL and Zink images, especially roughness transitions and cube seams.

Expect additional radiance work: at 1024, four rather than two output levels
means 24 radiance face draws/copies rather than 12 in the current design. Measure
that correctness cost separately; do not promise this fix alone improves FPS.

## 2. Default-probe uniqueness and one occlusion pass

Files: `llheroprobemanager.cpp`, `pipeline.cpp`, `app_settings/settings.xml`.

Retain insertion in the default-probe creation block; remove the later
unconditional insertion. Repeated initialization must update the existing
probe, and cleanup/reset must produce exactly one new default entry. Check the
invariant at the end of successful initialization in development diagnostics.

Keep the first shared ordinary/hero occlusion block in `LLPipeline::doOcclusion()`.
Remove the second duplicate block and call the hero manager in the first only
when mirrors and a valid active hero candidate are present. Keep ordinary probe
occlusion independent of that condition. Align manager-level gating with item 1,
without requiring ready output before a new candidate may initialize.

Do not alter `LLReflectionMap::doOcclusion()` globally: ordinary probes use it.
Preserve its nonblocking availability check. Only add a hero-scoped frame stamp
if call-site verification shows multiple legitimate invocations remain. Reset
such a stamp with the resource generation to avoid stale suppression.

Correct the update-rate description to state 6/rate faces and clamping to
1/2/3/6. Validate repeated allocation/reset, mirrors disabled/enabled, ordinary
probe visibility and occlusion transitions. Capture queries to demonstrate the
actual removed work; an inside-radius early return can mean no query existed.

## 3. Requested provider versus actual context identity

Files: `llappviewer.{h,cpp}`, Windows/Linux application implementations,
`llgl.{h,cpp}`, and window swap-interval reporting where required.

Keep `selectGLBackend()` as a void virtual hook and store its result in an
application-owned record. This avoids changing every platform override solely
for diagnostics. Proposed fields:

```
requestedBackend: existing preference, normalized for reporting
provider: Unknown | SystemGL | BundledMesa
selectionOutcome: NotAttempted | Selected | Fallback | Failed
providerPath, fallbackReason
actualDriver: Unknown | Zink | OtherGL
GL vendor, renderer, version
underlyingGPU: optional identity + evidence source
swapInterval: requested + optional observed value + setter success
```

Populate selection state before GL imports; finalize actual-driver identity only
after a context exists. Recognize a Zink renderer signature independently of the
Mesa preload/preference; native Linux system Mesa can itself be Zink. Preserve raw
strings. Do not interpret every Mesa context as Zink, or infer the physical GPU
from an unrelated Vulkan enumeration. Unknown identity is a valid result.

Add `mIsZink` to the GL manager if downstream code needs it; leave `mIsAMD`,
`mIsNVIDIA`, existing feature tables and quirks unchanged. No new Zink feature
mask is needed for diagnostics alone.

Replace the later preference-based backend assertion with one consolidated
context report. Record upload-threading actually initialized, mirror/HDR state,
and requested versus observed swap interval after settings are applied. Existing
WGL setters currently discard the result: capture success and query interval
where supported; record unknown when querying is unavailable. Use the platform's
actual Linux window API equivalently. An observed interval is not proof of the
Vulkan present mode underneath it.

A context recreation emits a fresh report; normal settings changes may emit a
bounded change event, never per-frame logging. Preserve Linux environment restore
behavior and Windows loader behavior in this item. Do not add automatic fallback
on context-creation failure merely because preload fallback is already supported.

Test system GL, bundled Zink, missing/incomplete runtime, failed preload, a Mesa
non-Zink override, Zink through system GL, unknown renderer strings and context
recreation. No contradictory 'Zink running' message may survive native fallback.
No feature setting may change just because diagnostics were introduced.

## 4. Mesa configuration, assembly and publication

Files in the package repository: `build.py`, `build_linux.py`, shared metadata
helper, `autobuild.xml`, tests and CI workflows. Windows currently skips existing
Meson configuration and uses timestamp-based copying. Linux already uses
`--reconfigure` and unconditional copying; preserve those behaviors.

### Configuration and provenance

Separate the source/patch marker from build configuration. Leave source checkouts
intact when options change. Represent requested platform options in one function,
with the pinned source and ordered patch hashes plus toolchain identity forming
the build identity. Use a new managed build directory for incompatible compiler,
architecture or source identity; reconfigure compatible directories with explicit
requested options before every compile. Invoke Meson compile consistently so the
Visual Studio environment is available on repeated Windows builds.

Introspect effective Meson options after configuration and fail on disagreement
for controlled options. Record compiler/version, architecture, Meson/Ninja versions,
source revision, patch hashes and requested/effective options in provenance.
Single-source the package version; expose it to workflow archive naming. Give the
new build-recipe output a distinct package revision even though Mesa's pin stays
unchanged. Do not enable unsupported Mesa LTO as part of this work.

### Assembly transaction

Build into a new, owned assembly directory for each successful invocation. Copy
all required files unconditionally, then perform platform relocation and validate
the inventory. Windows must include both DLLs and the license. Linux must include
its GLX/Gallium libraries and license with correct SONAME, NEEDED and $ORIGIN paths.

Hash Windows payloads against the build outputs. Linux uses patchelf, so record
both pre-relocation source hashes and final packaged hashes: requiring equality
between those two would be incorrect. Write provenance and a completion marker
only after validation. Package only this invocation's completed assembly. An old
build directory or package-results file must never count as current success.

Do not recursively remove arbitrary user output trees. Publish by selecting the
validated assembly explicitly; if Autobuild requires a fixed build directory,
use an owned managed payload subdirectory plus a current-generation manifest and
verified switch. Keep the last good package identifiable on failure, but never
upload it as the result of the failed invocation.

### CI and promotion

Use a single workflow with Windows and Linux build/package jobs sharing the same
package identity. Preserve the existing Linux software-Vulkan Zink smoke test and
label it accordingly. Windows CI checks architecture, inventory, dependencies and
archive/provenance; a suitable GPU run separately qualifies actual Zink rendering.
Hosted CI packaging success must not be described as an AMD rendering test.

A promotion job depends on both platform jobs and verifies their source/patch and
recipe identity. Publish immutable assets with hashes only after both succeed.
Viewer Autobuild metadata changes are a separate commit after package validation.
Do not repurpose the viewer's `latest` tag for a dependency package; its existing
successful Windows/Linux viewer CI policy remains unchanged.

Tests inject a command runner and temporary directories: changed option reaches
reconfigure; unchanged run remains correct; incompatible toolchain selects a new
build directory; newer stale destination cannot survive; missing file or compile
failure blocks assembly completion; Linux final hashes reflect relocation;
platform jobs cannot promote mismatched provenance. These tests cover real
failure modes rather than exact command formatting.

## Commit and acceptance sequence

| Commit | Target | Deliverable / gate |
|---|---|---|
| 1 | Viewer feature branch | Mip contract and readiness, forwarding and shader guard; boundary/lifecycle tests. |
| 2 | Viewer feature branch | Unique default probe, one occlusion pass, gating and setting text. |
| 3 | Viewer feature branch | Backend record and actual-context/settings report; platform fallback tests. |
| 4 | Package branch | Shared version/provenance and Windows reconfiguration fix; Linux compatibility retained. |
| 5 | Package branch | Validated assembly, failure tests and dual-platform CI/promotion. |
| 6 | Viewer feature branch | Qualified immutable package pin, if package publication has completed. |
| 7 | vkstorm-devel integration | Reviewed feature integration; fully staged RelWithDebInfo viewer and validation record. |

Commits 4-5 can proceed independently of 1-3. Do not block the viewer correctness
fixes on a dependency release: first validate them with the existing pin, then
qualify the replacement separately. Checkpoints are commit boundaries, not
requests to pause between already-authorized implementation steps.

For viewer qualification, hold effective settings and scene/camera constant and
compare native/Zink with mirrors on/off. Record CPU and asynchronous GPU timing,
frame-time distributions and image correctness. Separate initial warmup from
steady-state results. A compile alone is insufficient: stage DLLs/plugins/assets/
shaders and run the visual/lifecycle matrix. Record unavailable hardware checks
as pending rather than passed. Keep profiling-only changes in development.

The production PR is a later integration step: audit inherited development hooks,
run `scripts/tests/check_release_hooks.py`, and select only production changes.
Never merge master into vkstorm-devel to reconcile this work.


## Implementation record (2026-09-29)

Viewer work packages are committed on `codex/zink-actionable-fixes`, based on
`bc8a55c558`. The three main commits are `beebf757a0` (mip/readiness), `d5f0335f51`
(duplicate work), and `2dc6989a00` (actual backend/settings reports). No master
history has been merged. Unknown Vulkan device identity remains explicitly unknown.
The minimum accepted debug resolution is 4: the radiance filter divides by its
maximum scratch LOD, so a one-level scratch chain is not valid. Normal UI choices
(256 through 2048) are unaffected.

The production layout/readiness helper passed compiled C++ tests; hero, hero+SSR
and ordinary probe shader variants compiled under GLSL 4.30 using the production
REF_SAMPLE_COUNT=32. RelWithDebInfo linked successfully with Tracy/LTO, and full
manifest staging was verified against the linked executable and shader hashes.
Autobuild configuration explicitly disables INSTALL_PROPRIETARY/HAVOK/USE_KDU,
matching this fork's Release setup rather than Autobuild's generic defaults.

Package work is on `3p-mesazink:codex/zink-package-reliability`. Sixteen tests cover
configuration mismatch, incompatible toolchain isolation, stale destinations,
failure markers, relocation, exact archives and platform identity. CI additionally
builds both platforms and runs Linux's software-Vulkan smoke check. Initial Windows
qualification exposed missing Flex and Meson's omission of subproject builtin
options from introspection; parser generators are now installed, and static zlib
is checked through generated targets. Package commit `1ed0490` passed Windows and Linux build/package jobs, Linux's
software-Vulkan Zink smoke test, and exact archive/shared-identity verification in
[CI run 36532755739](https://github.com/anne-skydancer/3p-mesazink/actions/runs/36532755739).
Publication was skipped by design; no replacement package has been published or
pinned in the viewer. The test viewer retains the existing Mesa dependency.

Pending runtime acceptance: mirrors at multiple roughnesses, first activation,
probe switching, resolution/HDR changes, relog/teleport and mirror toggles on native
GL and Zink; actual fallback reports; measured CPU/GPU/frame-time comparisons.
Compile/staging success is not a claim that these visual checks have passed.
