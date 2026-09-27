# Resident mesh pipeline: design and acceptance

Development base: `vkstorm-devel` at `7805689030`. Master is not merged into this
branch. Windows/Linux OpenGL 4.3 remains the baseline.

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

Steps 1–2 alone are infrastructure, not completion of the approved CPU offload.
Initial conversion and CPU draw-list traversal remain until steps 3–4 are wired.

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

The first infrastructure implementation is connected to unrigged opaque PBR
submission. Resident ranges share bounded pages; leases coalesce returned space,
and the accounting includes both staged and published page storage. Compatible
consecutive draws share material/model/texture state and one indirect multi-draw.
The compute gather reads existing world-camera LOD commands and uses the current
pass matrices to suppress out-of-view commands. Bounds union all resident LODs.
Invalid bounds fail open. No per-frame command-count readback is introduced.
Clean unrigged neighbours now retain resident geometry across spatial-group
repacking. Rigged records still invalidate under the established policy.

Remaining implementation: object-space residency and transform-only metadata
updates; replacement of eligible CPU visibility/draw-list traversal; expansion of
batch submission to the depth/shadow consumers. Current draw lists still come
from CPU culling, so GPU culling is not yet a replacement of that CPU work.
Initial conversion, transform-driven rebuilds and direct-path buffers remain.
There is no claim of finished CPU offload or measured frame-time improvement.

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
  The staged batch shader matches source. An isolated no-login Zink startup
  reached login, passed the existing texture publication/readback self-test, and
  shut down with exit code 0. This does not exercise a resident world-mesh scene;
  in-world acceptance remains open.

The page pool deliberately retains the existing CPU mirrors used by conversion
and range copying. Removing those mirrors requires separate consumers to stop
reading them; this implementation does not claim a reduction in resident bytes.
