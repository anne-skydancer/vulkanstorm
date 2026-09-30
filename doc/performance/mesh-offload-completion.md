# Mesh offload completion

> Historical implementation: the CPU-to-GPU offload described below has been
> retired on the CPU restoration branch. See [current restoration and validation](cpu-rendering-restoration.md).
> Earlier AMD results do not establish NVIDIA performance gains.

Branch: `codex/mesh-offload-completion`, based on `vkstorm-devel` with the
previous mesh pipeline and Zink correctness checkpoints carried forward.
Worktree: `worktrees/mesh-offload-completion`. Do not relocate this worktree
without explicit user authorization.

## Scope and completion rule

Item 1 covers stable mesh geometry, GPU transforms and compatible batching
across static, moving, legacy, PBR, rigged, alpha, HUD, media and selected meshes.
The CPU owns only what it must; remaining rendering arithmetic belongs on the
GPU wherever it can be offloaded. Existing implementation boundaries do not
justify retaining CPU execution. Item 2's additional visibility policy, avatar
morphs and flexible-object deformation are separate workstreams.

CPU source geometry needed by picking, editing, mesh decoding and physics is
distinct from generated rendering buffers. Keeping that source does not justify
maintaining a second CPU implementation of rendering transforms or packing.
Rendering must not require a synchronous readback of generated attributes,
indices, visibility or commands.

## Geometry implementation in progress

`LLMeshGeometry` consumes bounded, immutable GPU snapshots of authored mesh
attributes. Snapshots retain their source volume identity and revision; geometry
replacement and weight scrubbing cannot reuse an old snapshot. A 128 MiB cache
evicts least-recently-used snapshots, and pending jobs retain ownership until
their dispatch. The pending queue is bounded independently at 2,048 jobs.

The existing face/material code provides per-face metadata and destination ranges.
Compute writes the ordinary render buffers, preserving their existing shader,
coordinate-space, alpha ordering, texture/media and selection consumers. This
path covers mesh faces independently of the narrower resident-LOD admission
policy. It also prepares alternate resident LODs.

Implemented operations, still requiring integrated qualification:

- Position, normal and tangent transformation, including rigged bind shapes,
  mirrored/nonuniform transforms, texture-index bits and tangent handedness.
- Planar/default UV generation, texture animation matrices, material-channel
  transforms, selection texture transforms and legacy emboss offsets.
- Skin-weight copying and packed color/emissive expansion.
- U16 index rebasing, including odd boundaries shared with adjacent faces.
- GPU-to-GPU resident attribute copies and index rebasing during refinement.

Draw-buffer binding flushes pending producers. Host writes complete before GPU
publication; barriers cover later vertex/index fetches and buffer copies. No
CPU rendering shadow is generated. Existing authored `LLVolume` data remains
available to independent CPU consumers.

## Batching and submission

Ordinary mesh draws gather only adjacent records with matching buffer, material,
texture matrices, blend state and skin palette. GPU compute expands their stable
range metadata into cached indirect commands. Alpha gathering retains the original
order and group boundaries; emissive replay retains every gathered draw.

Resident rigged faces now use shared page allocations. Compatible faces from the
same avatar and skin palette can consume GPU-selected LOD commands in one MDI call.
Rigged gathering preserves the prior avatar LOD policy and does not test deformed
skins against static local-space bounds. Separate state or palette bindings still
require separate submissions. This reduces possible host binds/draw calls; it does
not reduce the number of vertices skinned or triangles rasterized. The frame-time
benefit has not yet been measured.

## Validation completed, 2026-09-29

- Autobuild-configured RelWithDebInfo viewer built successfully with the Release
  feature/dependency configuration, checked by `ci_release_config.py --development`.
- Ran the copy-only viewer manifest after linking. The staged named executable
  matches `vulkanstorm-bin.exe` by SHA-256, and includes runtime libraries, plugins,
  shaders and assets. No installer was generated.
- Isolated startup on native AMD OpenGL and packaged Mesa/Zink reached rendering
  and exited normally through `QuitAfterSeconds=25`. Both processes returned zero;
  neither used the normal profile or logged in.
- Actual geometry shader: 103,104 transform checks per backend, packed index
  boundary/rebase checks, packed color/glow, weights, planar/default/emboss UVs,
  active rotation, matrix precedence, indirect commands and untouched guards.
- Existing GPU LOD, copying, per-view gathering and transform rendering tests pass
  on both drivers. Added shared-page rigged MDI versus separate draws: identical
  blended color and depth in two joint poses.
- Production-code regression fixtures pass for geometry queue/cache/reload,
  page leases and rigged compatibility, color/depth submission, transforms,
  registration, resumable preparation, mesh/skin arrival order and texture
  publication.

Staged viewer: `build-vc170-64/newview/RelWithDebInfo/Vulkanstorm-RelWithDebInfo.exe`.
Native driver: AMD 26.9.1.260826. Packaged Mesa: 26.3.0-devel, git `4c18bbc637`.
Local build/startup logs are under `.tmp/`; they are not release artifacts.

## Open completion gates

- Audit source mutation and queued-job lifecycle, shader/context reload,
  allocation failure, and metadata-only rebuilds.
- Validate the actual face integration and all attribute modes, including
  texture changes while position data remains GPU-owned.
- Audit specialized draw-pool consumers for any remaining compatible submission
  groups outside the common geometry, alpha and emissive paths.
- Validate native GL and Mesa/Zink, including in-world category coverage,
  selection, teleport/origin shifts and resource pressure.
- Measure removed CPU work, upload/dispatch overhead and frame-time tails.

This document is a work record, not a completion or performance claim.

## In-world validation in progress

User logged the staged Zink viewer into Isle of Repose. Live logs confirm resident
mesh publication and rigged indirect drawing, with no mesh publication/dispatch
errors and no pending preparation/resource waits after loading. These counters do
not establish visual correctness or a batching/frame-time improvement.

The user reproduced the improper glass behavior in the baseline viewer with legacy
alpha selected, confirming it predates this offload work. The exact commit of that
user-tested baseline is not established. `RenderAlphaSortMethod=1` (PPLL) resolves
the symptom in the user's test; this is a workaround, not a legacy-alpha fix.
Desktop inspection is unavailable (Computer Use native pipe missing); visual checks
currently depend on the user's observations. Legacy material/bump batch integration
and alpha allocation reuse have since been built and staged, but have not received
in-world qualification. Item 1 remains incomplete.

The current priority is correcting legacy alpha. A candidate correction restores
deferred shader texture state after each group's glow replay. Previously only the
program was rebound, leaving shared texture units changed by glow; the next alpha
group skips shader setup when it uses the same program. The production handoff
regression fixture reproduces stale bindings with the old code and passes with the
correction. This establishes a state-restoration defect, not yet the cause of the
recorded glass artifact. A legacy-alpha in-world retest is still required.

The user confirmed that the affected glass uses PBR materials. The candidate was
built and fully staged with matching executable hashes; Release feature parity
and isolated native OpenGL/Zink startup checks passed (exit zero on both).
Shader-channel logging confirmed the actual PBR/glow sampler layouts, but did
not establish that the observed glass symptom comes from the glow handoff.
Do not treat this candidate or the synthetic state regression as visual proof.

In-world retest: the user reports the artifact is mitigated, not fully resolved.
The candidate ran alongside the baseline with Zink and legacy alpha explicitly
set to zero. Preserve the partial result; legacy-alpha correctness remains open.

The user clarified that the remaining artifact resembles the original, is fainter,
and still depends on camera movement. The test viewer was closed; concurrent
sessions should be kept short.

Second candidate: cache the ordered volume-face bounds and check whether a camera
turn reverses any adjacent depth keys. The previous invalidation checked only the
camera-to-group-center direction and a large angular threshold, missing turns in
place. The new check uses the same size-biased depth rule as face sorting, supports
bridge-local cameras, and refreshes keys at rebuild time. Unchanged directions use
a cached result; changed directions that preserve order do not request a rebuild.
This remains a CPU metadata check for the existing CPU-owned ordered draw list;
it does not move geometry production back to the CPU or implement GPU alpha sorting.
Tests cover small turns, stable order, size bias, common translations, empty/single
face groups and a double-precision depth oracle. Particle alpha integration passes.
Visual validation and performance measurement of this second candidate are pending.
The second candidate built and staged successfully; native OpenGL and Zink startup
checks both exited zero. Executable SHA-256:
`a8135cec085c04109bcf359314b54251474faa459f5f606fb0b102fc45382c8d`.

The user's second recording (`Screen Recording 2026-09-29 143613.mp4`, 27 seconds)
still shows camera-dependent transitions between bluish/cloudy and clearer panes.
The second candidate is therefore not a complete visual fix. Whole-face sorting
tests do not establish ordering or state correctness within a draw. RenderDoc is
installed; `.tmp/launch-alpha-capture.py` prepares a manual, untimed Zink/legacy
capture session so the remaining defect can be investigated from actual draws.

Latest user validation (2026-09-29): "Second candidate functions well with legacy
alpha. No issues that I can see." This supersedes the earlier unresolved visual
assessment for the second candidate: the user now reports a successful visual
check. This does not establish exhaustive scene coverage or complete item 1.
Further capture work is unnecessary unless the symptom returns. Work remains
paused following the user's shutdown request; no integration is implied.

## Broader automated validation resumed (2026-09-29)

The user authorized broader validation and requested automated checks for now.
No viewer was running. This session identifies NVIDIA GeForce RTX 5070 Ti,
native OpenGL 4.6 NVIDIA 617.14, and packaged Mesa/Zink 26.3.0-devel
`4c18bbc637` over the NVIDIA Vulkan driver. Earlier AMD results remain historical.

Added production-producer observer tests covering 80 source revisions under the
128 MiB cache budget, LRU reuse, both 2,048-job overflow entry points, 300 indirect
packets against the 256-packet bound, regeneration after eviction, and context
loss with outstanding callbacks on two destinations. These pass. The observer
does not emulate GPU memory or reference-counted object lifetime.

Added real-driver tests for 20 successive UV/color rebuilds and neighboring CPU
uploads while preserving resident position/normal/tangent bytes and guard ranges.
These and the 103,104 transform checks pass on native NVIDIA and Zink. This tests
the production shader and upload sequencing, not the complete LLFace integration.

The broader rendering harness lacked SSBO barriers before repeated writes in its
LOD and gather cases. Matching the viewer's existing synchronization fixes native
NVIDIA failures. The complete native rendering suite then passes, including
resident LOD, animated rigged draws, refinement copies, visibility gathering,
local-space transforms and ordered shared-page rigged MDI.

**Unresolved:** the Zink rendering suite fails the local-space transform pixel
comparison: the resident draw produces no fragments while the baked comparison
draw does. An isolated transform-only reproduction also fails. Buffer readback
contains the correct matrix, GL reports 14 texture-buffer texels and no error,
but a diagnostic vertex texture fetch of the identity column returns zero.
Constant slot selection, buffer subdata, reattaching the texture buffer, another
texture unit and a larger allocation do not resolve it. This is not yet attributed
to viewer code, Mesa, or the NVIDIA Vulkan driver. No production workaround or
viewer rebuild was made during this validation pass.

Evidence: `.tmp/broader-*-render.log`, `.tmp/broader-*-attributes.log`,
`.tmp/broader-validation-results.json` and `.tmp/repro-mesh-transform.py`.
In-world category coverage and comparative frame-time measurements remain open.
