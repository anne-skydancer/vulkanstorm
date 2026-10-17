# R3 reboot checkpoint â€” 17 October 2026

Work continues on `vkstorm-vulkan` in
`C:\Dev\vulkanstorm\worktrees\vkstorm-vulkan`. R3 is **not complete**.
Commit and push completed checkpoints, then continue. Keep all viewer tests
headless/background. Preserve any user-run viewer and its profile.

## Saved work

All production changes and source pins are committed and pushed through
`fcc79d27f9` (imported-scene qualification). Relevant checkpoints:

- `07379bcd5e` / `826953839e`: scoped cache body error/size diagnostics.
- `f90614e9fa` / `24dc6ba22d`: continue disk reads when demand increases during
  an outstanding bounded read, with actual cache-state regression.
- `78441d6c45` / `a3ddce4148`: reference texture-pressure controller and separate
  GPU/CPU/native-admission accounting; UI/exact/full-resolution protection.
- `9273a72e4e` / `8dd2e3a3c3`: asynchronous imported glTF image reads/decoding,
  bounded admission, shared decoder/scaling, progressive preferences and
  retirement guards.
- `a725cc6d3a` / `fcc79d27f9`: actual five-channel skinned/animated imported
  scene qualification, independent GPU pixels and mandatory archived inputs.

`r3-production-audit.md` records implementation and qualification limits.

## Passed evidence

Canonical staged source `a3ddce4148` passed all nine cache cases in 170.1 s:
`.tmp/r3-pressure-cache-matrix/results.json`. Warm, read-only and resize
performed zero texture HTTP requests. Cold includes shared settings,
local/remote materials, legacy/bump and real CEF media contracts.

Actual concurrent primary/read-only sibling ownership also passed:
`.tmp/r3-pressure-cache-instances/results.json`.

Four earlier read-only restarts at `826953839e` passed with zero texture HTTP:
`.tmp/r3-cache-body-repeat`. These repetitions do not establish the cause of
historical intermittent misses. The expanded-demand state-machine defect is
independently reproduced and corrected; avoid claiming unproven attribution.

Focused regressions pass: native cache operation/demand/decoder bounds,
1,000-frame comparison against the actual extracted OpenGL pressure policy,
actual native admission-pressure recovery, shared settings, image ownership,
actual asynchronous glTF decode/preparation retirement and index reuse,
imported-image evidence rejection, session replay (30), evidence packaging (10).
Changed viewer translation units compiled. This is not platform qualification.

## Interrupted operation and immediate next action

The canonical `RelWithDebInfo` rebuild for `fcc79d27f9` was interrupted for the
requested reboot. Its compilation/link had progressed into asset staging;
provenance completion, staging validation and the new actual imported-image
replay have **not** been established. The orchestration and its build children
were stopped; no diagnostic viewer was launched by that interrupted command.
Build log: `.tmp/r3-imported-images-build.log`.

After reboot, inspect git/process state, then rebuild and fully stage in place:

```powershell
python scripts/vulkan_parity_provenance.py begin build-vc170-64
cmake --build build-vc170-64 --config RelWithDebInfo --parallel 3
python scripts/vulkan_parity_provenance.py finish build-vc170-64
python scripts/check_vulkan_ci_staging.py build-vc170-64
python scripts/run_vulkan_session_replay.py --build-directory build-vc170-64 --runtime C:/Dev/vulkanstorm/.tmp/software-vulkan-runtime/runtime.json --harness-evidence .tmp/r3-bump-gpu-contracts/results.json --evidence .tmp/r3-imported-images-qualified --r3-image-contracts --headless
```

Use a fresh evidence directory if an attempted replay already created results.
Do not edit tracked source during provenance builds, source-pin gates or
source-qualified replay. On failure inspect actual viewer/result artifacts;
retain strict pixel, network and lifecycle oracles. The provenance manifest is
`build-vc170-64/parity-build-provenance.json`.

The new fixture is `scripts/vulkan_r3_gltf_fixture.py`. The actual viewer path is
`vsr3gltfreplay.cpp`, called before the existing world-material tests. It checks
five independent 1024x512 PNG channels at 512x256, forced full resolution,
limited again and replaced assets, with full GPU readbacks, material/sampler
contracts and object/account retirement. This runtime path remains untested.

## Required remaining R3 work

- Complete native four-layer legacy/PBR terrain material acquisition, blending,
  UV/detail/triplanar/paint inputs and their settings controllers. Current
  `VSWorldScene::Impl::surfaces` still selects one detail map per patch and uses
  UI demand; this is a genuine production gap.
- Complete actual pressure-transition GPU/resource evidence and minimized scene
  image-owner rebinding/retirement. The new controller alone does not make
  minimized retained scene generations release or downshift their textures.
- Qualify account/grid cache identity and shutdown with real pending I/O against
  the shared reference policy; do not invent a new cache namespace.
- Complete imported-image runtime checks, any discovered corrections and archive
  verification. Then rerun the full cache/concurrent matrix on the final source.
- Audit the remaining R3 acceptance, then qualify Windows plus Linux SwiftShader
  and Lavapipe. R4/R5/R6 ownership must not disguise missing R3 production paths.

The exact OpenGL reference implementation remains the engineering guide.
Keep BRDF/probe-dependent final appearance assigned to R5/R6 as documented;
source pins or passing subsets do not certify full runtime parity.

## Local helpers

Ignored `.tmp/repin_r3_materials.py` remaps only `HEAD^` to `HEAD`. After a new
source commit, run it, then `check_diligent_insertions.py --write --accept` and
`check_milestone1_boundaries.py`; commit the pins and push before continuing.
New `vstexturememory.h` is classified I05; `vsr3gltfreplay.*` is I25.
Do not rerun older `.tmp/apply_*` scripts: several contain superseded candidates.
Standalone harness evidence `.tmp/r3-bump-gpu-contracts/results.json` remains
usable for image-only changes; shader/ABI changes need fresh harness evidence.

## Live-login crash reported 2026-10-17

The user reports the local staged Vulkan viewer crashes immediately after live
login. Windows Application event 1000 at 02:25:33 records an access violation
(0xc0000005), module RVA 0x150f867, in the canonical build-vc170-64 executable.
The matching binary/PDB were subsequently rebuilt; this RVA must not be
symbolized against the new PDB as if it identified the old crash. No matching
minidump or retained login log was found. The standard log was rotated by the
Release viewer, which remains running and must be preserved.

The current canonical stage, rebuilt at 10:57 from c0f1d0e290, passes the
headless connected-session replay using AnsaStorm Modern in 16.7 seconds
(.tmp/user-login-crash-headless). This is diagnostic evidence, not proof of
authenticated login or of fixing the reported crash. Current source differs
from that binary by the Linux codec include correction and inventory pins.

Ignored .tmp/capture-vulkan-login.cmd provides a manual next-live-test launcher,
using .tmp/debug_live_login.exe. It captures symbolized unhandled exception
stacks and login-crash.dmp in .tmp/live-login-capture. A deliberate standalone
exception verified both stack capture and MiniDumpWriteDump. It has not been
used to launch or log in the user's viewer. Dumps/logs are private diagnostic
files and must not be uploaded automatically. Do not mistake the separate
R3 imported-glTF diagnostic fixture rejection for this live-login failure.

The latest Linux source gate also detected stale discovery references in the
R0 ledger after recent R3 changes. Regeneration updates only source-reference
metadata; all runtime/authenticated qualification flags remain false.

### Captured live-login failure and correction

The user's manual capture at 12:10 local time produced a matching stack and
minidump in .tmp/live-login-capture. VSWorldScene::Impl::textureCost queried
VSUIRenderer::imageExtent on an empty remote image before GPU publication;
the access violation occurs in imageExtent, called from scene complexity
accounting during present. The scene now keeps the reference formula's base
256 cost while dimensions are unknown, then computes the resolution cost as
soon as the immutable image is published. The renderer/fetch upload pipeline
and OpenGL behavior are unchanged. An equivalent unguarded extent query in
local glTF image limit handling is guarded as well.

Both compiled production-source regressions failed on the empty image before
the guards, then pass with them: missing/pending, publication, replacement
and retirement for scene cost; pending, limited and full-resolution states
for embedded image limits. Matching live-login acceptance still needs a user
retry of the rebuilt canonical executable; a headless replay alone cannot
prove the real account no longer crashes. Preserve private dump/log evidence.

## Startup/shutdown reference audit and terrain regression report

The user confirms live login now succeeds, but reports ground appearing before
the null-image fix and absent afterward, plus low FPS. Treat this as an open
visual regression report, not accepted terrain behavior or a proven causal
attribution. The exact c0f1d0e290-to-6efe1ebca9 viewer delta contains only two
null-image guards and the Linux codec include. Preserve these crash fixes while
tracing ground reception, mesh publication, image readiness and draw submission.

The startup audit found the Release login layout's setTargetDim replaced with
reshape, native completion hiding the progress view instead of using the shared
fade, connection progress resetting below authentication, and logout hiding its
progress before application teardown. Restore the Release layout and shared
localized progress/fade/cursor completion. Native presentation owns vsync; the
shared completion method must skip SDL's GL-only vsync call. Keep logout visible
on quit and use shared gLogoutMaxTime (normally six seconds); return-to-login
still hides the logout overlay. These corrections are not a declaration of full
startup/shutdown parity: final snapshots, account/cache persistence and pending
upload shutdown sequencing still require production work and qualification.

System WPR CPU capture was refused (0xc5585011). Do not infer an FPS bottleneck
from source inspection. Opt-in VS_VULKAN_FRAME_PROFILE counters in diagnostic
builds record average packet/world/UI/present CPU times, observed frame cadence,
submitted terrain packets, and current-region terrain mesh/detail-image counts
every two seconds. No synthetic replacement ground is introduced. The ignored
manual capture launcher enables these counters for the next user login; no
profiling is enabled by default. This is CPU submission timing, not GPU timing.

The first actual replay with the shared fade reached connected UI and correctly
kept the progress overlay's wait cursor. The old replay immediately attempted
editor hover underneath that overlay. Update the replay driver to wait for the
real shared fade/input release on initial login and relogin before exercising
connected controls; keep the hover assertions and all pixel oracles unchanged.

## Missing-ground production correction

The terrain surface producer discarded each received/published patch whenever
its detail image was absent. This differs from LLDrawPoolTerrain using
LLTexUnit::bind -> LLViewerTexture::bindDefaultImage: the OpenGL peer keeps
geometry visible with the 128x128 0x7f default image created by
LLViewerTextureManager::init while the real image is pending. Transpose that
local default image in VSUIResources and keep the real terrain mesh in the draw
packets. If even that small upload is refused, use the renderer's existing
white binding with the same grey tint and retry; do not invent terrain heights.
The actual detail publication automatically replaces the default. Remote image
readiness remains false while using the default; full terrain-material parity
and the causal attribution to the null-handle crash correction remain separate.

A compiled regression executing the actual surfaces method failed before the
change, then passes pending, loaded, erased, allocation-refused and no-geometry
cases. The new diagnostic terrain replay sends encoded standard LayerData
through the real session, verifies the produced mesh's Vulkan pixels while its
network image is deliberately withheld, releases a lossless J2C through the
actual HTTP/fetch/cache path, and verifies the textured and erased-publication
frames. The runner and evidence packager independently reject missing/wrong
pixels. CI's existing R2 replay enables this focused regression with
--terrain-contracts; it is not a substitute for completing R3 terrain materials.
