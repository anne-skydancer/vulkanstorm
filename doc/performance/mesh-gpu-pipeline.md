# Resident mesh pipeline: design and acceptance

> Historical implementation: the CPU-to-GPU offload described below has been
> retired on the CPU restoration branch. See [current restoration and validation](cpu-rendering-restoration.md).
> Earlier AMD results do not establish NVIDIA performance gains.

Development base: `vkstorm-devel` at `7805689030`. Master is not merged into this
branch. Windows/Linux OpenGL 4.3 remains the baseline.

Master integration uses a separate branch based on `master` at `9c4756ce4b`,
porting only the feature checkpoints and associated texture/particle fixes.
Development profiling/capture infrastructure is excluded, including resident
geometry memory probes; production residency-budget accounting remains intact.
The page regression validates that production accounting directly. The release
hook check, mesh page/registration/submission/transform and resumed-preparation
tests, texture publication regression, and Mesa/Zink mesh/particle texture GPU
tests pass on this integration branch. Full viewer builds and live-session
results below refer to the development branch; the integration branch still
needs platform build validation.

## Destination

Eligible unrigged opaque PBR meshes retain object-space geometry in shared pages.
Transform changes update compact object records, not vertex streams. Compatible
materials consume compute-generated per-view indirect commands in multi-draw
batches. World-camera LOD/streaming policy is independent of pass visibility.
There is no per-frame readback of visibility or draw counts.

CPU responsibilities remain asset/network reception, scene and material ownership,
initial topology/UV preparation, resource admission and retirement, publication of
object transforms, GL submission, and on-demand picking. Streaming feedback is
bounded and asynchronous. Rigged meshes, alpha ordering, HUDs, media, selected
objects, and flexible deformation retain their established consumers until their
specific contracts are migrated; this milestone must not change their semantics.

## Integration sequence

1. Share immutable resident vertex/index pages. Keep 16-bit page-local indices,
   bound memory including staged replacements, and retain page ownership through
   every draw record. Range leases prevent overwriting live ranges; free neighbours coalesce. Ordinary
   GL buffer updates order reuse after prior GPU consumers. Empty pages retire via
   the existing vertex-buffer lifetime mechanism.
2. Gather compatible resident commands on GPU and reject out-of-view bounds with
   the actual pass projection/modelview. Use fixed-capacity commands with zero
   instance counts for rejected entries. No draw-ID or indirect-count extension
   becomes required. Preserve all ordinary material/texture/culling state.
3. Remove transform-driven geometry invalidation: publish object-space geometry
   and object/normal transforms, integrate matching shader consumers across
   deferred, depth, shadow and reflection passes, and retain UV/picking semantics.
4. Replace eligible CPU per-object visibility and draw-record rebuilding with
   persistent registration. Retain coarse scene/streaming traversal separately;
   GPU visibility must not merely duplicate CPU rejection.

Steps 1â€“2 alone are infrastructure, not completion of the approved CPU offload.
Initial conversion and CPU draw-list traversal remain until steps 3â€“4 are wired.

## Correctness and performance gates

Test command tails, empty/authored-empty LODs, noncontiguous slot order, page-local
index offsets, ownership across replacement/deletion, invalid bounds (fail open),
clip boundaries, different pass cameras, and GL binding restoration. Check actual
GL shader execution and pixels on native AMD and Mesa/Zink. Preserve existing
LOD, particle and retention regressions. Validate teleport/origin shifts, material
arrival, geometry eviction, context teardown and shader reload in the viewer.

Measure CPU rebuild/conversion/submission times, CPU upload bytes, draw calls,
resident/staged memory, GPU pass time and p50/p95/p99 frame time, both while decode
is busy and idle. No performance improvement follows merely from using compute.
Build and fully stage an Autobuild RelWithDebInfo viewer without an installer.

## Status

Implemented on the feature branch:

- Shared resident geometry pages with range leases and bounded admission.
- Local-space resident vertex/normal/tangent streams and compact transform records.
- GPU per-view bound transformation/culling and indirect command gathering for
  compatible opaque PBR color/reflection and depth/shadow draws.
- Event-driven spatial-group packet registration and persistent GPU candidate
  lists, including invalidation and cross-view lifetime protection.

The CPU still performs coarse scene/occlusion policy, initial topology/UV/color
preparation, asset streaming, material ownership, and normal fallback-buffer
maintenance. Static, unrigged opaque PBR meshes are the initial eligibility;
rigged/active/selected/alpha/HUD/media/flexible paths retain their established
contracts. Routine fallback-buffer retirement and broader eligibility are not
implemented. Full live-scene correctness and performance acceptance remain open;
there is no measured frame-time claim.

Validation so far:

- `scripts/tests/test_mesh_pages.py`: production page admission, ownership,
  allocation failure, range reclamation/coalescing and material/view compatibility.
- `scripts/tests/test_compute_resume.py`: existing teleport/resumed preparation
  dependency checks still pass.
- `scripts/perf/test_gl_compute_mesh.py`: production LOD and batch shaders pass
  on native AMD and Mesa/Zink, including gathered multi-draw color/depth parity,
  separate view matrices, invalid bounds, stale slots and dispatch tails.
- Windows Autobuild RelWithDebInfo **7.2.5.82065** compiled, linked and fully
  staged without an installer. Configuration matches the Release dependency/
  feature set. The branded executable matches the linked binary by SHA-256:
  `DA6EA37DAA6381A4EDAFE1DBF9C13FF06EB3AEF4E5882CBA520B117C50AC4FA4`.
  The staged batch shader matched source at that checkpoint. An isolated no-login Zink startup
  reached login, passed the existing texture publication/readback self-test, and
  shut down with exit code 0. This does not exercise a resident world-mesh scene;
  in-world acceptance remains open.

The page pool deliberately retains the existing CPU mirrors used by conversion
and range copying. Removing those mirrors requires separate consumers to stop
reading them; this implementation does not claim a reduction in resident bytes.

### Depth/shadow submission checkpoint

Color and untextured depth/shadow passes share the same batching walk and resident
LOD. Nonresident records and state boundaries preserve submission order. If batch
compute cannot run, every member is submitted through its established draw path.
The compute clip mask follows `GL_DEPTH_CLAMP`: clamped shadows retain casters
outside the near/far planes while still rejecting lateral outliers.

`test_mesh_submission.py` exercises the production submission functions with
observable draw/material operations: 520 records, the 256-command boundary,
nonresident and material breaks, and successful/failed GPU submission in both
textured and untextured passes. Native AMD and Mesa/Zink shader tests cover both
clip masks and distinct pass cameras. This does not replace in-world shadow QA.

### Resident visibility metadata checkpoint

Bounds now live in a slot-indexed GPU table (32 bytes per resident slot, 256 KiB
at the current 8192-slot capacity). Publication uploads changed bounds together
with LOD metadata; shader reload reconstructs the table from its CPU copy.
A render pass uploads only four-byte slot IDs instead of 48-byte records with
repeated bounds. This removes the two per-candidate CPU bound copies and reduces
that per-pass input upload by 12x. It is not a measured frame-time improvement.
The GPU tests exercise a bound update without republishing candidate IDs.

Candidate enumeration is still CPU work. Moving bounds into persistent storage
is preparation for persistent registration, not a claim that CPU culling or
transform conversion has been removed.

### Local-space transform checkpoint

Unrigged resident pages now hold local-space positions, normals and tangents.
The initial converter copies these attributes and packs the immutable resident
slot into the existing fourth position lane; it does not multiply them by the
object matrices. PBR color/reflection and opaque shadow vertex shaders fetch the
position and inverse-transpose normal transforms from a buffer texture. Tangent
handedness is preserved. Rigged bind-shape conversion remains unchanged.

The table uses seven RGBA32F texels per slot (896 KiB at 8192 slots), below the
[OpenGL minimum buffer-texture capacity](https://wikis.khronos.org/opengl/Buffer_Texture).
This adds no requirement beyond the viewer's OpenGL 4.3 baseline. The compute
visibility pass reads the same matrices and transforms local bounds on the GPU.
Texture bindings, active texture unit and the transform enable flag are restored
before ordinary draws. Shader reload reconstructs metadata alongside commands.

Eligible position-only updates preserve resident page ranges and publish compact
matrix/LOD metadata; unchanged transforms do not mark uploads dirty. Geometry,
material, planar-texgen, legacy bump, selection and eligibility changes retain
invalidation. The initial static/unrigged/opaque-PBR eligibility is unchanged.

**CPU work still present:** the ordinary face buffers are maintained for selection,
unsupported passes and failure recovery; initial topology/UV/color preparation,
scene traversal and candidate enumeration also remain. This checkpoint removes
resident vertex transformation/repacking, not every CPU fallback conversion.
Persistent registration is connected in the following checkpoint. Retirement of
routine fallback-buffer maintenance and in-world performance/visual acceptance
remain open.

Validation adds production admission/publication and scoped binding tests, plus
native AMD and Mesa/Zink color/depth comparisons for the production transform
helper with nonuniform/mirrored scales and nonzero resident slots. Compute tests
move a local bound by updating only the transform record.

The transform checkpoint compiled and linked successfully with Autobuild
RelWithDebInfo and passed the Release feature/dependency configuration check.
The complete staged executable and linked binary have SHA-256
`39B55C864868CC8315EF6949D56F1B7FF53E392280865D8DC2FBF41B3AF36E2A`.
Isolated native AMD Core-profile and Mesa/Zink starts loaded the viewer shaders,
reached login, passed the existing texture self-test and exited with code 0.
These startup tests do not exercise in-world resident mesh publication.

### Persistent draw registration checkpoint

Spatial groups now cache compatible opaque PBR packets when draw-map membership
or resident page publication changes. Resident tokens notify only their subscribed
group caches on invalidation/publication. Transform-only metadata changes do not
reclassify packets. Groups without a multi-record packet retain the original map
and its cross-group batching; classification is retried on a relevant event.

Each packet uploads its slot list once, then reuses it across views and frames.
Normal submission visits packet heads and skips member compatibility checks,
slot-array construction and candidate uploads. GPU visibility still uses each
pass's matrices and depth-clamp state. Coarse CPU group culling, occlusion policy,
render-type exclusions and surface-area limits remain in the existing pipeline.
Initial material grouping stays on the CPU when registrations change; transparent
ordering is not part of this path.

Cull results hold strong references to their submissions because the render maps
contain raw pointers. Replacing a group's cache in a later view therefore cannot
free packets still used by an earlier view. Invalidated packets restore each
original draw's state. Candidate buffers retire with their packets or GL context;
shader reload keeps immutable candidate lists. Allocation failure uses existing
single-record submission without retrying allocation every frame.

Registration is currently within spatial groups. This bounds ownership and keeps
the existing visibility policies, but may limit merging across group boundaries;
its submission savings versus extra packet boundaries require in-world timing.
Tests cover 520 records, repeated world/shadow views without reclassification,
invalidated/retained old views, no-packet groups waking on publication, one upload
across twelve views, context recreation and allocation-failure cleanup.

The registration checkpoint compiled, linked and was fully restaged through
Autobuild RelWithDebInfo, with the Release feature/dependency check passing.
The linked and staged executable SHA-256 is
`FD7B5171B9CF5521B4885495EAF7FB5F852D2819637680908E53F952954D888B`;
the four affected staged shaders match source. Isolated native AMD and Mesa/Zink
Core-profile starts reached login, passed the texture publication/readback
self-test and exited with code 0. These are startup checks, not live-world mesh
correctness or frame-time measurements. The local build retains version 82065;
use this hash to distinguish it from earlier checkpoints with that build number.

### Foliage-card update investigation

Live testing reported foliage cards remaining visibly rectangular until moving
the camera away and back. Inspection found that asynchronous texture delivery
notified face consumers before publication, when component counts and alpha-mask
classification still described the previous GL image. Completion now dirties
the texture's attached faces after adoption when either classification changes.
This uses the existing render-pass/rebuild mechanism without a scene-wide scan;
unchanged detail upgrades and rejected uploads do not trigger it. Regression
coverage includes pending fences, component changes, mask-only changes, material
channels, and unchanged/rejected uploads. Reflection and mesh shaders are unchanged.
The timing defect is confirmed in code; whether it explains the reported scene
still requires retesting the foliage after rebuilding.

### Live startup observation, 2026-09-28

The tester reports improved overall performance and faster loading, with brief
startup stalls. This is subjective acceptance feedback, not an A/B benchmark.
The 06:05:39 UTC session used Mesa/Zink on an RX 9070 XT. Five-second reporting
windows recorded display-scope maxima of 1434.71 ms at 06:06:18, 598.613 ms at
06:06:23 and 1056.43 ms at 06:06:28. The swap scope reached 533.132 ms in the
last window. These are CPU-side elapsed durations, including driver waits;
nested scope maxima must not be added or assumed to identify the same frame.
The broader frame-delta probe also recorded 2.40681 seconds during login.

Resident preparation's reported per-frame maximum over the first minute after
world entry was 8.5705 ms. Texture delivery retained its 2048 MiB ceiling;
sampled reservations peaked around 861 MiB through 06:07:14 and sampled pending
uploads were zero. Admission deferrals did occur, so these periodic samples do
not establish that pressure never occurred between reports. The log supports
the existence of stalls, but does not isolate first-use shader/pipeline work,
driver synchronization or other startup tasks as their cause.

Separately, the particle pipeline initialized and became active, then logged
`Resident particle view preparation failed; restarting emission on CPU without
a parallel simulator` at 06:06:16 UTC. This fallback needs its own investigation;
the faster-loading observation must not be attributed to continuing GPU particle
simulation in this session. Reflection behavior is retained unchanged.

Particle follow-up: the generic `view preparation` message combined failures
from material/texture publication, GPU view ordering, and alpha intervals. No
kernel GL-error message preceded the recorded fallback, so the existing log
cannot identify its exact cause. The failure handler destroys resident state
and latches CPU execution for the remainder of the session. New diagnostics
separate those stages and report missing texture storage, snapshot GL errors,
material-buffer errors, invalid camera inputs and nonfinite/out-of-order alpha
boundaries. This changes failure reporting, not capability gates or fallback
policy. Production particle texture-cache and pipeline-service tests pass on
the same Mesa/Zink driver, including compressed/sRGB textures, shader reload,
view culling, alpha intervals and invalid-boundary rejection. Live reproduction
with the diagnostic build remains necessary to establish the specific cause.

The next live session identified that cause at 06:39:18 UTC: material 1 selected
a texture snapshot with `source=0 revision=2`. Both the requested image and the
fetched default particle image lacked published GL storage. LLTexUnit could bind
the resident viewer default, but the snapshot still received the unpublished
particle default and failed, permanently disabling GPU simulation for the session.
Selection now checks each candidate's GL name and falls through to the same
resident viewer default used by ordinary texture binding. It also maintains
demand for the fetched particle default. Each frame selects the requested image
again, so publication replaces the placeholder without restarting simulation.
The production selector and real bindless snapshot pass the missing-source
regression on Mesa/Zink. In-world confirmation remains pending.

The fixed RelWithDebInfo viewer was compiled, linked and fully staged; the
linked/staged SHA-256 is
`B7907F212C59DF69EA858865699A24985873CC2ADAF7E19FDCC012D5CB52969F`.
The texture regression passed on native AMD and Mesa/Zink, and isolated Zink
startup reached login and exited successfully. In the subsequent live Zink
session, GPU simulation, ordering and alpha submission activated at 06:54:56 UTC
and no particle fallback was logged through 06:56:37 UTC, beyond the previous
startup failure point. This confirms the observed startup case remained active;
it is not a claim of exhaustive scene or long-session coverage.
