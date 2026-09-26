# Redundant-allocation audit

Research only, 2026-09-24. Code inspected at vkstorm-devel 99ca06a8a3.
The subsequent measurement implementation is described below. mcp-vulkan is outside scope.

## Measurement implementation (not yet validated in world)

The development build now logs GeometryMemory every five seconds alongside ComputeLOD.
It separates published/staged logical vertices and indices, rounded CPU backing,
category peak CPU bytes, stage allocation/release counts, publication/release counts,
and rigged destination weight bytes/peak/face count. Counters are lifecycle-driven;
the rigged face walk happens only when its existing full-face copy occurs.
Peaks are independent category high-water marks and must not be added as if simultaneous.
Event counts are cumulative, not rates. Rigged weights exclude malloc bookkeeping.
CPU backing sizes reuse the actual pool size-class calculation, not a second formula.
Existing FocusMemory VBO logging provides live/cached totals for reconciliation.

The accounting_matches field checks logical compute bytes against resident_bytes,
also guarded by a development assertion. Staged release includes publication transfer;
it must not be interpreted as a physical free. No geometry/texture ownership, eviction,
buffer reuse, scheduling policy or cache limits changed. Reports follow the existing
compute reporting path; disabled compute reporting is not evidence of zero rigged data.

## Findings and the purpose of the existing allocations

### Published compute vertex mirrors: first candidate

`llcomputelod.cpp` creates a separate LLVertexBuffer for each staged face, fills
the available LOD ranges, and replaces Resident::buffer only at complete-generation
publication. The old visible buffer remains available during preparation. Normal
drawables retain their separate fallback geometry. This overlap protects visibility
and pass consistency; it is not an arbitrary duplicate to delete.

`llvertexbuffer.cpp:copyResidentRange` has one caller, in compute preparation.
Vertex attributes are copied twice: memcpy between CPU mirrors and
glCopyBufferSubData between GPU buffers. Indices are read from the CPU mirror and
rebased for their new vertex offset. Published compute drawing uses setBuffer and
drawIndirect; the traced compute path does not rewrite the published vertex bytes.

Therefore CPU vertex mirrors after publication are a plausible removable allocation,
whereas CPU index mirrors remain required by the present refinement algorithm.
This is a candidate interface change, not proof that a free() is safe today.

Necessary changes if the measurement justifies implementation:

- Explicitly finalize a compute buffer only after all pending writes are flushed.
- Make mapping/writing a finalized buffer fail explicitly; no hidden GPU readback.
- Refine by copying GPU vertices and rebasing retained CPU indices into a new buffer.
- Separate CPU backing ownership from GPU name ownership in destruction/reuse.
- Account for GPU-only cached entries without treating them as normal writable hits.
  Simply recreating a full CPU allocation on every cache hit could recreate the
  allocation churn already seen with the rejected reusable-buffer cap.
- Preserve the current general-purpose buffer path and platform upload requirements.
  Apple's unmap path can rebuild complete buffers from CPU data.
- Keep buffer bindings, dirty-range tracking, deferred GL deletion and shutdown valid.

Acceptance cases: progressive arrivals; repeated camera LOD changes; rigged and
unrigged meshes; material/colour/UV invalidation; origin shift and teleport; selection
and return from selection; shadows/depth/material passes; resource exhaustion;
object death during preparation; renderer shutdown. No missing geometry, GPU
readbacks, loss of detail or regression in frame-time tails.

### Rigged face weight copies: second candidate

LLRiggedVolume::update uses copyVolumeFaces, which copies the full LLVolumeFace
arrays, including weights. Its skinning update reads weights from the source face,
while writing deformed positions to the destination. The traced picking/octree
consumers do not require those destination weights.

The rest of the rigged face is not disposable: genBBoxes and relative transforms
use it; ray intersection interpolates normals, texture coordinates and tangents;
selection and debug rendering consume positions and indices. It is not merely a
second rendering mesh.

Potential saving is 16 bytes times the number of vertices with copied destination
weights, plus only other attributes independently proven unused. Measure actual
allocated rigged faces, not avatar source totals. Audit all consumers and generic
LLVolumeFace mutation/copy methods before introducing a specialized copy mode.
Do not retain pointers into unowned source memory to save those copies.

### Source LODs, normal buffers and texture pixels: preserve policy

LLVolumeMgr already shares source volumes by full parameters and LOD. A zero
reference count for one sibling LOD is not evidence that it will not be requested
again. The failed source-eviction experiment is not reinstated.

Ordinary vertex buffers support CPU construction, partial updates and reuse. Their
CPU backing cannot be globally removed using the narrower compute observation.

Saved raw texture pixels serve callbacks, previews, editing and export, with an
existing expiry path. Copying raw pixels just before releasing the old reference
suggests transient copy cost, not a measured multi-gigabyte persistent saving.
Do not change sharpness, focus handling or retention to pursue this target.

GLTF selection uses an extra buffer for different highlight transforms and releases
it when deselection is processed. This is a feature-specific lifetime to measure if
needed, not evidence of general world-mesh duplication.

## Accounting corrections

The default VBO pool allocates both a CPU block and a GL buffer of the rounded size.
Its live/cached/distributed counters are logical buffer accounting, not driver
physical-residency measurements. They do not include arbitrary driver copies.
Compute geometry is a subset of these buffers, not an additive memory category.

Compute resident_bytes sums getSize()+getIndicesSize() for staged and published
buffers. Despite its name it does not isolate published data, vertex-only data, or
the pool's extra size-class padding. The pool rounds requests to improve reuse;
that is a deliberate speed/memory tradeoff. Preserve it until measured otherwise.

The latest inspected Vulkanstorm.log identifies Release 81956 under Mesa/Zink with
a 1024 MiB compute budget. Its last sample reports 577,973,420 logical compute bytes
(about 551 MiB), including indices and staging. It is not a measurement of the
rebuilt 81973 rollback binary or of reclaimable CPU vertex bytes.

## Proposed measurement, before optimization

Add incremental counters at allocation/publication/destruction, not a per-frame
full scene scan. Separate these disjoint logical categories:

1. Published compute vertex bytes and index bytes.
2. Staged compute vertex bytes and index bytes.
3. Pool-rounded CPU backing bytes for each category.
4. Idle reusable CPU backing, distinct from live backing.
5. Rigged destination weight bytes and the number of owning faces/objects.

Track current and peak bytes, allocation/free events and publication/cancellation
transitions. Count an allocation once even when multiple LLPointer references exist.
On publication transfer the category instead of adding a second allocation.
Log snapshots infrequently using existing development logging. Avoid synchronous
GPU queries, global allocation tracing and per-vertex instrumentation for this task.

Invariant: total compute logical vertex+index bytes across staged and published
categories equals resident_bytes. Pool-rounded backing must reconcile with its
own categories, with unrelated buffers separate. It is not valid to equate logical
buffer totals with process working set.

Run a settled scene, movement/camera refinement, teleport, focus-loss/refocus and
return to a quiet scene. Record process commit/working set alongside category
bytes and frame-time tails. The first potential saving is the published compute
CPU vertex backing actually observed, not the entire VBO total or the configured
budget. No claim that this alone reaches a 4-6 GiB viewer footprint.

## Next decision

Instrument these categories on the development branch, measure the rollback build
behaviour, then decide whether compute vertex finalization is worth its interface
cost. Keep the rigged weight optimization independent so each result is attributable.
No asset pooling, budget increase, texture-policy change or cache cap is proposed.

## Follow-up: preserve vertex reuse; investigate larger owners (2026-09-25)

The user explicitly prioritizes the hot-path reuse behaviour after the earlier cap
caused an FPS collapse. Defer both allocation-removal proposals: the observed vertex
mirror saving is modest and destination weights have not appeared in this run.

Completed 81974 Mesa/Zink run: 122 GeometryMemory samples, zero accounting mismatches;
maximum sampled published CPU vertices 177.23 MiB; zero rigged destination weights.
Last snapshots (a few seconds apart, not an atomic memory census):

- Physical working set 7930.49 MiB (7.74 GiB), observed peak 9778.70 MiB.
- Process committed memory 18377.90 MiB (17.95 GiB), observed peak 20282.62 MiB.
- Live VBO-pool rounded backing 2,232,249,744 bytes (2.08 GiB), idle cache
  27,482,848 bytes (26.21 MiB). Compute buffers are already included.
- GL texture base-image estimate 4,055,020,472 bytes; this is not a CPU allocation
  census or measured physical GPU residency and must not be added to working set.
- Mesh completion payload/backlog and texture upload completions were zero in the
  final samples. This does not prove the separate image-decode queue was empty.

CPU source volumes: LLVolumeFace has a combined positions/normals/UV allocation,
plus separate index, tangent and weight arrays and optional octree storage. Count
the combined allocation once. LLVolumeMgr already shares by full parameters/LOD.
Mesh completion deep-copies faces into that shared volume; the completion source
can overlap transiently, but its empty final queue does not explain persistent
multi-gigabyte duplicate completions. Measure managed source bytes, unique volumes,
octrees and transient completion volumes as separate owners, without new eviction.

Raw images: LLImageRaw currently exposes an object count, not a full live-byte
counter. Several references can point to one allocation. Count at allocation and
ownership-transfer boundaries, covering allocate/reallocate/delete, adopt/setData,
and releaseData. Distinguish an ownership transfer from a free. Then classify saved,
active decode and completion references without summing shared pixels repeatedly.
The existing signed formatted-image counter is not a substitute for decoded pixels
or a thread-safe wide allocation ledger. Do not change expiry or focus behaviour.

Repository metadata: skin-info maps deliberately duplicate records to avoid hot-path
mutex work and have periodic cleanup. Preserve that purpose. Header and physics
decomposition maps merit byte/count instrumentation. Decomposition entries own hull
and physics mesh vectors; inspected insertion/access/shutdown paths show no explicit
map eviction, but this is only a lifetime candidate, not evidence of a large leak.
Raw pointers returned to callers prevent introducing eviction without an ownership
audit. Count vector capacities, not only element counts.

Decoder: inspected C:/Dev/openjpeg/src/lib/openjp2/opencl_backend.c declares a 64 MiB
aggregate worker-buffer budget. acquire_worker includes idle capacities and trims
idle buffers when admission needs space. This source evidence does not cover host
scratch, driver allocations or establish the exact installed DLL revision. Do not
attribute gigabytes to OpenCL scratch without reconciling the package and measuring.

Next measurement priority: unique LLImageRaw pixel bytes and LLVolumeFace source
arrays, followed by octrees and repository decomposition/header capacities. Keep
allocator bookkeeping/retained heap and driver-private memory as explicit unknowns.
Requested live allocation bytes and process working set are different quantities;
do not label their difference a leak. No runtime change was made in this follow-up.

## Phase two instrumentation

ImageMemory now reports raw_owned_bytes, raw_owned_peak_bytes,
other_image_owned_bytes and raw_detached_cumulative_bytes. The base image class
updates atomic byte totals whenever its private buffer/size changes; all four raw
constructors select the raw category. Multiple references to an image do not add
charges. Failed allocations with no pointer add no bytes. Detachment subtracts
ownership and records cumulative transfer traffic, not a physical free. A later
adoption counts the new owner. Unowned transfer intervals and temporary allocations
inside resize/codec operations are excluded; this is an owned-payload measure, not
a malloc high-water mark. Concurrent category reads are individually atomic, not
one globally locked snapshot. Other-image storage is separate from raw pixels.

SourceMeshMemory uses a copied parameter cursor and visits at most 64 groups per
existing five-second reporting interval. It retains no volume references and never
evicts anything. A report is emitted at sweep completion with slice count, groups,
loaded LODs, faces and faces with octrees. Values are collected across that time span;
insertions/deletions during the sweep make this an approximate census. A stable
scene permits a more representative completed sweep. No single-frame full scan.

The source estimate includes combined position/normal/UV capacity, padded indices,
and active tangent/weight arrays. It excludes source objects outside LLVolumeMgr,
temporary decoder/remapping storage, allocator overhead, octree bytes, optional
separate joint arrays and driver allocations. Current face counts can understate
retained attribute capacity after topology transformations, hence the estimate label.
Do not add this estimate blindly to rigged/source subsets from other measurements.

Validation: the actual image-ledger update method was exercised with repeated updates,
growth/shrink, detach/adopt, category separation, null allocation and eight concurrent
writers. Integration is compiled with the viewer; in-world measurements remain needed.
No texture demand, ownership lifetime, cache limit or worker scheduling policy changes.
