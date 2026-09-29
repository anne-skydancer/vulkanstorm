# Mesa/Zink: verified findings and implementation plan

Revised 2026-09-29 after source review. The original audit targeted viewer
`9a387f65b6`; the findings below were checked against current master
`89db50a109` and local Mesa source pinned at
`00e42c51b10d8e0769489156fa414f111897d515` with the package patches.
The relevant feature-table and hero-manager code is unchanged between those
viewer revisions. No viewer was built or benchmarked for this revision.

This document replaces the original audit's performance estimates and ordering.
**Verified** means supported by the inspected source. **Recorded** means reported
by existing measurement notes, not reproduced here. **Candidate** means a proposed
change whose correctness or benefit must still be demonstrated. Package release
binary hashes have not been independently reproduced in this review.

## Conclusions

- There are actionable hero-probe correctness and duplication defects. Fix these
  before restructuring rendering or estimating speedups.
- HDR hero intermediates and destination textures have different formats. Matching
  them is a credible way to eliminate format-converting blits, subject to visual
  validation and preservation of the non-HDR path.
- The recorded native/Zink baseline used mirrors off and VSync off. Hero scene
  rendering and WGL's nonzero-interval sleep do not explain that baseline.
- Zink classification needs improvement for reliable diagnostics and deliberate
  workaround selection. It does not establish different texture-upload settings:
  Windows `list all` and `list AMD` both enable multithreaded texture uploads.
- Reliable Mesa configuration and artifact assembly are immediate engineering
  work. They prevent stale packages; they are not direct FPS optimizations.
- Neither the CPU/GPU bottleneck nor the contribution of presentation has been
  established. No percentage speedup or native/Zink parity is promised.

## 1. Evidence and corrections

Source paths below are relative to this viewer repository unless prefixed with
`Mesa:` or `Package:`. The latter refer to `C:/Dev/mz-src` and
`C:/Dev/3p-mesazink` respectively. Function names are the durable references;
line numbers from the earlier audit can drift.

### Verified findings

| ID | Finding and source | Consequence |
|---|---|---|
| F1 | `LLHeroProbeManager::generateRadiance()` in `indra/newview/llheroprobemanager.cpp` writes `mMipChain.size()/4` output levels. At resolution 1024 that is levels 0-1. `updateUniforms()` supplies `heroMipCount=10`; `class3/deferred/reflectionProbeF.glsl::tapHeroProbe()` requests `(1-glossiness)*heroMipCount`. | Unwritten output levels can contribute. Glossiness 0.85 requests LOD 1.5 with a nonzero hero weight. This is a correctness defect; its visible manifestation has not been reproduced. |
| F2 | `initReflectionMaps()` inserts the same default probe twice on initial allocation. `LLPipeline::doOcclusion()` has two blocks calling the hero manager. | Redundant traversal, GL state work and potentially query polling. `LLReflectionMap::doOcclusion()` can return without issuing a query, so a fixed number of saved submissions is not guaranteed. |
| F3 | Hero `mMipChain` uses RGBA16F with HDR enabled; `LLCubeMapArray::allocate()` uses R11F_G11F_B10F for the three-component HDR cube. Mesa `zink_blit.c::try_copy_region()` and `blit_native()` reject the relevant conversion/mask combination. | The format-converting path requires a blitter fallback. Matching formats removes this obstacle to fast copies; actual command selection and savings need capture evidence. |
| F4 | The hero cube allocation reserves four cubes; the inspected path uses output index 0 and scratch index 3. | Approximately 128 MiB at 1024, or 512 MiB at 2048, for the full HDR cube mip allocation alone. Two-cube storage could roughly halve that allocation. These are calculated texel sizes, not measured VRAM residency. |
| F5 | `indra/llrender/llgl.cpp` classifies vendor strings; Mesa can become `MISC`, bypassing native AMD/NVIDIA branches. Linux also checks the renderer for Intel. `llappviewer.cpp` logs a requested Zink backend even after a Windows preload fallback. | Separate requested backend, loaded provider and actual context identity. Do not automatically apply native driver workarounds to Zink. |
| F6 | `Package: build.py::build_mesa()` runs Meson setup only if `build.ninja` is absent. `assemble()` copies only when the destination timestamp is older. | Changed requested options or newer stale destination files can survive a build/package run. |
| F7 | `Mesa: zink_kopper.c::zink_kopper_set_present_mode_for_interval()` forces IMMEDIATE on Windows. `stw_framebuffer.c::wait_swap_interval()` uses a 1.75 multiplier, and its caller skips it for interval zero. | Genuine VSync/pacing work, but not an explanation for an interval-zero benchmark. |
| F8 | `Mesa: meson.build` disables Mesa's shader cache on Windows. `zink_screen.c` disables EDS2, dependent dynamic-state features and push descriptors for the proprietary AMD path. | Existing limitations/workarounds, not permission to enable features blindly. This does not mean all driver or in-memory caching is absent. |

F1 also requires checking resource initialization: allocation uses null texel data,
and staggered face updates precede radiance generation. Do not assume every scratch
face is valid after allocation, reset or changing the selected probe. This is an
implementation dependency to resolve, not a separately measured visual defect.

### Corrections to the original audit

| Earlier assertion | Corrected assessment |
|---|---|
| Missing AMD classification proves different texture-upload settings. | Incorrect deduction. `featuretable.txt` enables `RenderGLMultiThreadedTextures` in both `list all` and `list AMD`. Compare actual effective settings. |
| WGL's CPU sleep is a leading fix for the recorded uncapped gap. | Unsupported: the recorded run had VSync off, and the sleep is conditional on a nonzero interval. |
| Six clears imply six blocking swapchain acquisitions. | Incorrect. `zink_kopper.c` returns immediately when the required image is already acquired/acquiring. A call into acquisition handling is not necessarily a new acquisition or a wait. |
| Forty-two copies imply 84 extra render-pass boundaries. | Not established from source counts. Batching, deferred clears and driver state determine actual boundaries. |
| Mirrors off means absolutely no hero-related GL work. | Too broad. `update()` and `renderProbes()` return early, but the duplicate pipeline occlusion setup is not guarded by `RenderMirrors`. Check retained probe state and toggle/reset behavior. Hero scene/filter optimization still does not explain the mirrors-off baseline. |
| All proposed changes are neutral or beneficial on native GL. | Unproven. Format precision, filtering, resource hazards and temporal updates need visual and timing checks on both backends. |
| A vkcube result uniquely attributes Composed Flip to AMD or Zink. | It narrows hypotheses. Window state, compositor policy and application differences prevent that attribution from one comparison. |
| Upstream `3fe13b1c074` is the historical viewer crash fix; drop all null guards. | The commit exists and is absent from the pin; it guards failed Vulkan pipelines. Matching the historical crash and proving each older guard redundant are separate tasks. |
| Hero optimizations remove most of the penalty or preserve a fixed mirrors-off performance ratio. | Neither follows from the available evidence. CPU/GPU overlap and the limiting stage can change. |

### Conditional work counts

For resolution 1024 and update rate 2, when an eligible, non-occluded hero probe
actually renders, the current loops update three faces, perform six blur draws,
30 mip-chain draws and 12 radiance draws, and issue 42 cube copies. These describe
the current implementation before F1 is fixed. They are not unconditional
per-frame costs or a predicted saving. Scene/shadow work depends on the active
rendering configuration. The update-rate setting selects 6/rate faces, using rates
1, 2, 3 or 6; its description should reflect that.

The clears in `LLViewerWindow::cubeSnapshot()` and the cube-display path are
candidates for removal, not yet proven redundant. Trace framebuffer ownership and
later color/depth consumers before deleting them.

### Package provenance and maintenance

Local source markers identify `mz-src` as the pinned revision and
`3p-mesazink/mesa-src` as `3e2092a295...`. This establishes different local source
states, not which released archive contains which DLLs. Record archive and payload
hashes before making byte-identity claims or removing old artifacts.

Retain the MSVC release and WGL loader-initialization patches while validating
package changes. The loader patch initializes optional kopper data and the initial
swap interval; do not claim it has no possible effect on AMD. Retain null guards
until a call-path review establishes their redundancy. Evaluate the upstream
null-pipeline fix separately rather than rebasing Mesa as part of build hygiene.

The release-hook checker passed during review. Its scope does not prove the
absence of every conceivable development hook. Historical documents, standalone
tools and unapplied patches are not runtime overhead. Preserve useful evidence;
index or mark historical material instead of moving it between branches merely
for performance cleanup. Unused includes, old headless code, duplicate backend
parsing and staging names are maintenance work, outside the critical path.

## 2. Implementation plan

This is an implementation specification, not a claim that the work is completed.
Each numbered item should be independently reviewable and committed with its
validation results. Start viewer implementation on a feature branch based on
`vkstorm-devel`, auditing that branch's current code before applying these findings.
Keep `master` and `vkstorm-devel` separate; use a reviewed PR for production changes,
excluding development-only instrumentation. Mesa package changes belong in the
separate `3p-mesazink` repository and receive their own commits and release identity.

### P0 — Do now: correctness and duplicated work

#### P0.1 — Define and enforce valid hero-probe mip sampling

Scope: `llheroprobemanager.{h,cpp}`, `reflectionProbeF.glsl` and, if required,
the hero uniform layout and radiance shader interface.

1. Establish one explicit contract for the sampling LOD scale, maximum usable
   output LOD, generated output-level count and scratch filtering levels.
   Derive counts from the configured resolution; do not hard-code 1024.
2. Preserve the existing glossiness-to-LOD mapping initially. Generate every
   output level needed by its nonzero hero contribution, including the upper
   neighbor used by fractional LOD filtering. At 1024, the current mapping needs
   output levels through 3 to cover values approaching LOD 2.5. Confirm this
   against the actual sampler state before implementing the count calculation.
3. Explicitly bound shader reads to valid generated output data. Do not simply
   lower the shared cube texture's maximum mip level: scratch and output currently
   occupy the same texture and the radiance filter needs deeper scratch levels.
   A new roughness mapping is a separate visual change, not the default fix.
4. Track scratch-face readiness across allocation/reset/probe changes. Until a
   complete valid source and output are ready, retain ordinary reflection-probe
   shading rather than expose undefined hero data. Invalidate readiness when its
   resource or probe identity changes; avoid synchronous GPU readback.
5. Keep CPU responsibilities limited to counts, readiness and scheduling. Scene
   rendering, filtering and radiance generation remain GPU work.

Acceptance: every contributing output sample addresses initialized data at each
supported resolution/rate; startup, relog, teleport, probe switching, mirror
on/off and resolution changes show no undefined flashes or missing ordinary
reflections. Check smooth roughness transitions, HDR and non-HDR on native GL and
Zink. Add a focused test of count/LOD boundaries. More generated levels can cost
extra GPU time: record that correctness cost before subsequent optimization.

#### P0.2 — Remove duplication and make mirror gating explicit

Scope: `llheroprobemanager.cpp`, `pipeline.cpp`, and the update-rate description.

- Keep exactly one default-probe insertion and one hero-occlusion scheduling site;
  retain the ordinary reflection-manager occlusion pass.
- Ensure initialization, repeated initialization and reset preserve uniqueness.
- Guard hero-only work when mirrors are disabled; remove the duplicate GL setup
  block without removing state needed by ordinary probe occlusion.
- Preserve nonblocking availability checks. If multiple legitimate callers can
  still poll a newly issued query in the same frame, add a scoped frame guard;
  do not change the shared query machinery without evidence it is needed.
- Correct the setting description to explain faces per frame and the valid rates.

Acceptance: one default entry through reset cycles; at most one intended hero
occlusion pass; ordinary probe culling unchanged; mirrors re-enable correctly.
Observe actual query submissions when assessing the gain rather than assuming a
fixed saving. Use existing diagnostics or development-only capture instrumentation.

### P1 — Do: trustworthy backend identity and Mesa builds

#### P1.1 — Record the backend that actually runs

Scope: `llappviewerwin32.cpp`, corresponding application declarations,
`llappviewer.cpp`, `llgl.{h,cpp}`, and Linux backend initialization as needed.

- Carry requested backend, loaded GL provider and fallback reason separately.
  After context creation, log actual GL vendor, renderer and version once.
- Detect Zink from the actual context identity, not just the saved preference or
  successful Mesa preload. Environment overrides can select another driver.
- Keep translation-backend identity separate from native vendor flags. Record the
  underlying GPU only when its identity is reliable; use unknown otherwise. Do
  not mistake the first independently enumerated Vulkan device for Mesa's device.
- Record effective upload-threading, mirror, HDR and swap-interval settings needed
  for comparisons. Distinguish requested interval from a successfully applied or
  queried interval. Avoid per-frame logging.
- Preserve current feature behavior. Introduce a Zink-specific feature-table rule
  only when an independently justified rule exists.

Acceptance: native, Zink, missing-runtime fallback and environment-override cases
report the actual provider/context without contradictory backend messages. Verify
Windows and Linux behavior; do not silently change user settings or vendor quirks.

#### P1.2 — Make Mesa configuration and package assembly reliable

Scope: `Package: build.py`, package metadata and Windows/Linux CI workflows.
Can proceed independently of P0 and P1.1. Keep the current Mesa pin and patches for
this step so build reliability is not mixed with a driver update.

- Reconfigure an existing compatible Meson build with the requested options on
  every build invocation, or compare a complete configuration fingerprint first.
  Include source/patch identity, build options and toolchain identity; incompatible
  changes require a fresh build directory rather than reusing old object files.
- Copy expected DLLs and licenses unconditionally after a successful build and
  fail if any required artifact is missing. Never package after a failed build.
- Emit provenance containing source revision, patch hashes, effective options,
  compiler/tool versions and payload hashes. Single-source package version data.
- Add a Windows build/package job alongside Linux. Check archive contents and
  licenses; use a Windows Zink smoke test on a suitable GPU runner when available,
  clearly distinguishing packaging success from rendering validation.
- Test changed options, a newer stale destination DLL, missing artifacts, failed
  compilation and repeated unchanged builds. These are the failure modes to cover.
- Give new package contents a distinct release identity; update the viewer's pin
  only after both platform packages pass their checks. Do not overwrite an old
  release asset or delete local source/build trees as part of this change.

Acceptance: requested and effective configuration agree; packaged payload hashes
match the successful build outputs even when old destination timestamps are newer;
failed builds cannot publish packages. Inventory/license checks pass on both
platforms. Windows runtime qualification must be explicit if CI lacks a GPU.

### P2 — Validate and implement targeted optimizations

Begin after P0 establishes the intended output. Use separate commits/measurements
for each item; P0's new mip counts supersede the earlier 42-copy estimate.

| Order | Work and dependencies | Acceptance |
|---|---|---|
| P2.1 | Match hero intermediate formats to their destination, following the ordinary probe path where appropriate. Preserve HDR/non-HDR behavior; inspect alpha consumers and precision before choosing formats. | Capture confirms conversion blits disappear on the intended path; bright/dim reflections, roughness transitions and cube seams remain correct on both backends. |
| P2.2 | Trace cube-snapshot framebuffer bindings and depth/color lifetimes; remove only clears whose results are unused. Inspect ordinary probes as well because they run with mirrors off. | No depth/culling/UI regression; fewer unnecessary clear/render-pass operations. Measure actual acquire waits independently. |
| P2.3 | Replace radiance level-zero rendering with a six-layer image copy if the shader operation is equivalent. Verify cube orientation, dimensions, compatible formats, clamping and destination offsets. | Image comparison demonstrates equivalence; capture shows removed draws/copies. Retain the shader path until equivalence is established. |
| P2.4 | Replace four-cube allocation with explicit output/scratch indices for two cubes, after auditing all users. Do not combine this with direct rendering into layers yet. | No stale hard-coded indices; reset/switch behavior passes; calculated allocation and observed resource sizes confirm the reduction. |

No optimization becomes a permanent user debug opt-in. Once qualified, it replaces
the redundant path for applicable configurations; retain only necessary capability
or format compatibility handling. Do not raise the viewer's GL contract for these
changes.

### P3 — Deferred engineering and experiments

- Direct layer rendering and separate scratch/output textures: potentially remove
  copies, but first design attachment lifetimes and avoid framebuffer/texture
  feedback. Use explicit resource boundaries rather than assuming barriers cure
  illegal feedback.
- Face selection, clip planes and shadow reuse: prove reflected-view coverage and
  shadow validity before reducing work. No percentage saving is established.
- Radiance updates for only changed faces: account for cross-face filtering and
  source mip dependencies; no unconditional half-work claim.
- Windows VSync: separate feature work. Query supported present modes, use FIFO
  where appropriate, and use FIFO_LATEST_READY only with required support enabled.
  Handle interval changes, swapchain recreation and removal of duplicate CPU pacing.
  Validate tearing, latency and frame pacing; this is not the uncapped baseline fix.
- Presentation experiments: collect wait/queue evidence before varying swapchain
  image counts, usage flags or fullscreen-exclusive behavior. vkcube is comparative
  evidence, not proof of driver blame.
- Mesa null-pipeline fix: inspect/backport and qualify independently. Do not remove
  existing guards or rebase Mesa wholesale without reviewing the affected paths.
- Shader cache, descriptor features and Mesa LTO: pursue only with evidence of the
  relevant bottleneck and a supported configuration. Viewer development LTO remains
  enabled; that is separate from enabling Mesa's allow-broken-lto option.
- Loader search-path cleanup, environment policy, license staging, old guards and
  backend parsing: separate maintenance changes with targeted platform checks.
  Resetting DLL search state must account for delayed dependency loading.

## 3. Validation and integration gates

Before behavioral changes, preserve the baseline commit, package payload hashes,
GPU/driver identity and effective settings. Follow the existing
[measurement rules](../mesa-zink-performance-archive-review.md) and retain the
[recorded observations](../mesa-zink-inworld-observations.md) as historical evidence.
The matched Midday records were about 34.04 native versus 20.09 Zink presents/s;
they are not a universal backend ratio or a freshly measured result.

For each rendering change, compare native GL and Zink in both mirrors-off and
mirrors-on scenes. Hold camera, draw distance, resolution, HDR, shadows, upload
settings, VSync and frame limiting constant; separate warm steady-state runs from
loading/compilation. Record repeated frame-time distributions, CPU preparation
and submission time, GPU scene/filter time, and waits. Read GPU timestamps
asynchronously from completed frames so measurement does not introduce a new stall.
Gallium's Zink render-pass counter requires appropriate driver instrumentation; it
is not a portable OpenGL query token.

Shaders execute on the GPU; culling, scheduling and submission include CPU work.
Only paired timings can identify the limiting stage. Report visual correctness,
operation/resource changes and measured timings separately. Reject visual
regressions even when a benchmark improves.

Build viewer candidates through Autobuild as fully staged RelWithDebInfo viewers,
with the development channel, Tracy and viewer LTO enabled, otherwise matching the
Release dependency/feature configuration. Include libraries, plugins, shaders and
assets; generate no installer. Keep new profiling probes in development only.
Before a master PR, audit the diff for inherited development infrastructure and run
`scripts/tests/check_release_hooks.py`. Do not merge master into vkstorm-devel.

Completion order: **P0.1, P0.2, P1.1, P1.2**, then P2 changes individually.
P1.2 can run in parallel in its separate repository. Stop an individual speculative
optimization if validation fails; continue independent correctness/reliability work.

## References

- [Hero manager](../../indra/newview/llheroprobemanager.cpp)
- [Hero sampling shader](../../indra/newview/app_settings/shaders/class3/deferred/reflectionProbeF.glsl)
- [Radiance generation shader](../../indra/newview/app_settings/shaders/class1/interface/radianceGenF.glsl)
- [Cube-array allocation](../../indra/llrender/llcubemaparray.cpp)
- [Pipeline occlusion](../../indra/newview/pipeline.cpp)
- [Probe query handling](../../indra/newview/llreflectionmap.cpp)
- [Windows feature table](../../indra/newview/featuretable.txt)
- [Khronos: FIFO latest-ready capability](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR.html)
- [Microsoft: presentation and Independent Flip](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)
