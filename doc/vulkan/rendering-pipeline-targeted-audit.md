# Targeted rendering-pipeline audit

> Current implementation contract: Windows/Linux OpenGL **>=4.3 Core**.
> Both OIT paths remain for now. Depth peeling will be deprecated before it is
> retired; PPLL is the long-term sole OIT target. Its current 4.4-dependent calls
> need a qualified 4.3 implementation before that transition is complete.


Reader reference: [acronyms and terminology](rendering-audit-glossary.md).

Date: 2026-09-26. Source: `50871c432fff2335d0b52a48a2c8cbc32170d76b`.
Static source audit; no renderer changes or measured speedups. This report narrows
the broader [Core/Zink audit](gl-core-zink-audit.md) to rendering modernization
and CPU mesh work. References below are repository paths and line numbers at
that revision, under `indra/`.

## Recommended architecture

Platform decision: **Windows/Linux only; macOS compatibility is no longer a
requirement.** This supersedes the earlier 4.1 proposal, which was retained for
macOS compatibility. The implementation contract is **OpenGL >=4.3 Core**.
Remove compatibility-profile operation after reachable dependencies are replaced.
Request higher Core versions when available; no 4.1/macOS tier is required.

Compute, SSBOs and multi-draw indirect are baseline capabilities. The current
PPLL path still requires 4.4 features; keep that feature gate and depth peeling
during this transition. Deprecate depth peeling first, then retire it after PPLL
is qualified on the 4.3 baseline and covers the required transparency workloads.
Do not raise the startup floor to 4.4 as a shortcut for that work.

Direct drawing and CPU support geometry remain useful for ineligible objects,
picking, diagnostics and staged migration. Requiring compute capability does not
mean every operation benefits from a compute dispatch.

Explicitly record format support, limits and required extensions rather than
inferring all capabilities from a version number. The older modernization draft's
4.3 proposal agrees with this contract; the current implementation milestone enforces it.
Dropping macOS does not by itself establish a need for 4.5 or 4.6; qualify any
further increase against specific architectural benefits and Windows/Linux coverage.

## Pipeline and ownership

The relevant path is object/volume -> drawable faces -> spatial groups -> packed
vertex/index buffers and draw records -> per-view culling/sorting -> draw pools.
Geometry rebuilding is dirty-state driven; it is not a full conversion of every
mesh on every frame. Camera, shadow, probe and HUD passes have different contracts.

Entry points: `newview/llviewerdisplay.cpp:1809,1885,2016,2038` for world shadow,
sorting, deferred geometry and lighting; `:2214,2220,2255` for another view path;
`newview/pipeline.cpp:3844` for `postSort`; `newview/llvovolume.cpp:6527,6666`
for mesh rebuilding and draw-record generation.

The existing compute system already selects resident mesh LOD and creates indirect
commands. It does not replace the CPU scene graph, all geometry preparation, or
per-view visibility traversal. Normal rigged rendering already skins on the GPU.

## Modernization findings, in priority order

### R1. Establish a consistent Core context contract

Windows context creation can descend to 3.0 and compatibility/legacy creation
(`llwindow/llwindowwin32.cpp:1963`). Linux SDL context creation lacks explicit
version/profile attributes (`llwindow/llwindowsdl2.cpp:666`). The non-Darwin Core
setting defaults false (`newview/app_settings/settings.xml:12600`), and
`newview/llfeaturemanager.cpp:836` rewrites some Intel GL/GLSL version values.

Request and verify Core >=4.3 for main, shared and recreated contexts. Reconcile
old saved settings, fail clearly below the floor, and replace synthetic version
mutation with narrowly justified workaround flags. Audit shader-version branches
against the resulting contract (`llrender/llshadermgr.cpp:575`).

Benefit: predictable capability behavior and removal of obsolete branches. Context
policy alone is not a frame-rate optimization. Test context sharing, resize,
fullscreen changes, texture upload workers and startup failure behavior.

### R2. Replace the reachable client-array physics visualization

`newview/llspatialpartition.cpp:2595` unbinds the vertex buffer, uses
`glVertexPointer`, and draws with CPU hull-index pointers. This is a reachable
convex physics-debug path and a concrete Core blocker. Move it to buffer-backed
generic attributes and indices using the renderer's normal ownership rules.

The renderer already creates a VAO (`llrender/llrender.cpp:875`); claiming that
all VAO infrastructure is absent would be incorrect. Other legacy-looking terrain
and material helpers require reachability classification: the audited old terrain
functions have definitions/declarations but no callers. Remove proven dead code
after checking build variants, rather than treating every old API spelling as an
active visual defect.

### R3. Make render-target reuse and allocation failure safe

`llrender/llrendertarget.cpp:105` returns success for matching size, texture usage,
depth and mip settings **without comparing the requested color format**. It also
sets those cached properties before allocation finishes: a failed allocation can
leave a subsequent identical request eligible for that early success return.

Use a complete allocation descriptor and a successfully allocated state; commit
that state only after success. Clean up partial allocations and check framebuffer
completeness on creation/reconfiguration. Preserve borrowed-attachment ownership.
The current framebuffer-status helper is gated by `gDebugGL` (`:36`).

These are source-level correctness defects in the allocation API, not evidence
that a particular scene currently exhibits wrong colors. Reproduce same-size
format changes and allocation-failure retries before and after the fix. Verify
HDR, transparency targets and resize/recovery behavior. Format-aware memory
accounting is also needed: several allocations currently estimate four bytes per
pixel regardless of actual format.

### R4. Reduce redundant submission and upload work with explicit state ownership

Candidates already traced in the broader audit:

| Location | Change to evaluate | Qualification |
|---|---|---|
| `llrender/llrender.cpp:193` | Elide redundant texture binds in `bindFast` | Invalidate caches correctly across shared contexts and external mutations; Mesa may already suppress downstream work |
| `newview/llcomputelod.cpp:89` | Replace 17 queried state values with an explicit compute-pass state contract | Queries are overhead candidates, not proof of GPU stalls |
| `llrender/llvertexbuffer.cpp:1412` | Correct overlapping inclusive upload chunks | Boundary overlap is one byte; do not market this as a large bandwidth saving |
| `llrender/llvertexbuffer.cpp:939` | Reduce resident-range copies and CPU-shadow maintenance | Inventory CPU readers and partial writes before removing shadows |
| `newview/pipeline.cpp:2828` | Consolidate duplicate hero-probe occlusion work | Readiness guards may already prevent duplicate GPU draws |

Keep texture publication and query waits visible in profiles. Removing a fence or
flush without replacing its ordering contract risks intermittent corruption.
Batching fewer calls can reduce both native-driver and Zink frontend cost, but
must be measured independently on each backend.

## CPU mesh work and GPU opportunities

Priority denotes implementation order/value hypothesis, not measured runtime cost.

| Priority / operation | Current evidence and cadence | Proposed destination and expected gain | Main complications |
|---|---|---|---|
| High: face transformation and repacking | `newview/llface.cpp:1277`, dirty flags around `:1434`, index rebasing `:1505`, positions/normals/tangents `:2128,2170,2205`; called during mesh rebuilds | Keep object-space geometry resident; apply object transforms and suitable UV transforms in vertex shaders. Avoid CPU loops and uploads on transform-only changes; works on 4.1 | Coordinate spaces, baked scale, normal transforms, legacy planar/bump UV semantics, picking, and material edits must remain correct |
| High: draw-record construction and submission | `newview/llvovolume.cpp:6666` sorts faces, partitions buffers and packs geometry; `newview/llcomputelod.cpp:914` submits one indirect command per draw | Stable geometry ranges plus compatible opaque batches, then compute-generated command visibility/LOD. Reduce packing, traversal/submission overhead and GL calls | Shader/material/texture and avatar-palette boundaries; index width; object identity in shaders; lifetime and eviction |
| High after residency: visibility and LOD | CPU spatial traversal remains; compute LOD is gated at `newview/llcomputelod.cpp:81` and dispatched through `:240` | GPU frustum tests and LOD decisions per view, consumed directly by indirect draws; fewer CPU decisions and rejected GPU draws | Main-camera results cannot substitute for shadow/probe/HUD visibility; preserve missing/empty authored LOD semantics and streaming demand |
| Medium: material-channel preview duplication | `newview/llface.cpp:1360` clones geometry for selected GLTF texture-channel rendering | Select the UV/material channel in shaders using shared geometry | Selection overlays must agree with normal rendering; benefit is conditional on this workflow |
| Conditional: morph application | `llappearance/llpolymorph.cpp:544` applies sparse vertex deltas when weights change | GPU morph accumulation for sufficiently large or frequently changing workloads | CPU collision-volume changes and mask semantics remain; small sparse updates may be cheaper on CPU |
| Conditional: flexible-object geometry | `newview/llflexibleobject.cpp:429,732` simulates a path and regenerates geometry as needed | Keep small sequential simulation on CPU; evaluate GPU deformation from uploaded path/control data | Dynamic bounds, LOD, normals and CPU interaction geometry; dispatch cost may dominate small objects |
| Later: occlusion | CPU-managed query history and timeout handling in `newview/llvieweroctree.cpp:1107` | Conservative GPU hierarchical-depth visibility integrated with command generation | Temporal disocclusion, near-plane cases, camera changes, separate views and conservative depth reduction; compare against query-based baseline |
| Low initially: rigged CPU support geometry | `newview/llvovolume.cpp:5223,5261` skins CPU vertices and may rebuild face octrees; picking calls at `:5017,5099` | Prefer better invalidation/on-demand updates and conservative bounds; GPU deformation only where results remain GPU-resident | Picking requires CPU-visible results. Synchronous readback can erase savings and change interaction latency |

Avoid inflated targets:

* `newview/llface.cpp:841` transforms cached volume-face extents under rebuild flags;
  ordinary bounds generation is not a full vertex scan every frame.
* `newview/llvoavatar.cpp:11337` caches skin palettes per skin hash and frame;
  draw-pool upload guards also reuse palette state. Moving this small work to
  compute is not the first priority.
* Normal vertex-shader skinning is already GPU work. Compute pre-skinning could
  amortize repeated passes, but adds output-buffer bandwidth, memory and barriers;
  it needs measurements against the existing vertex-shader path.
* Tangent generation and initial mesh conversion should be classified by asset
  arrival/edit cadence, not counted automatically as recurring frame cost.

## First GPU milestone

1. Introduce stable geometry/material/object handles and persistent resident
   ranges. Keep transient upload storage separate; make retirement fence-safe.
   Persistent residency does not require persistent mapped buffers.
2. Use the existing direct-draw path over those ranges to prove image equivalence
   and reduce rebuild work before adding new compute decisions. This is a migration
   and correctness reference, not a retained 4.1 hardware tier.
3. Build compatible opaque batches. The current packing cap is 65,535 vertices
   (`llvovolume.cpp:6673`); choose base-vertex/local-index ranges or a deliberate
   wider-index path instead of overflowing existing U16 layouts.
4. Extend compute to per-view frustum/LOD command generation on the 4.3 baseline. Use bounded
   command capacity with zero-count rejected commands initially; do not make a
   newer GPU draw-count facility an accidental requirement. Supply object metadata
   without assuming newer draw-ID built-ins exist on all 4.3 devices.
5. Apply the barriers required for each next consumer: indirect commands, vertex
   attributes, indices or SSBO reads. Use fences separately for CPU reuse/lifetime.
   Keep GPU decisions on GPU; asynchronous streaming feedback is a separate path.

Start with eligible unrigged opaque meshes, then expand to rigged/material cases.
Retain direct drawing for ineligible objects. Do not reorder transparent geometry
as though it were opaque: `llvovolume.cpp:6678` preserves original rigged alpha
face order, while unrigged distance sorting follows a different rule. Validate
PPLL coverage and any remaining ordinary blending separately. The long-term choice of PPLL as the sole
OIT method does not imply that UI, additive effects or other specialized blending
must all use fragment lists. Inventory those consumers before deleting alpha code.

### PPLL consolidation

Keep both OIT methods in this milestone. Deprecate depth peeling before retiring
it, and retain its settings and selection logic during that period. Retirement
requires PPLL validation on 4.3 and across the intended world-transparency passes,
including relevant secondary views; only then remove the mode and migrate settings. Test intersecting transparent surfaces, hair, particles,
water and high depth complexity, including resize and low-memory conditions.

Define bounded behavior for node-pool exhaustion and allocation failure. The
existing allocation-failure route to sorted alpha must be reviewed explicitly:
it is current behavior, not an adopted policy for the new framework. During the transition, depth peeling remains an existing selectable method;
its eventual retirement must not leave allocation/overflow behavior undefined. Instrument overflow, memory use and
resolve cost; a single OIT implementation still needs a documented failure policy.

## Shared GPU budget and acceptance

Mesh compute belongs to the rendering side of the proposed soft 60:40
rendering/OpenCL-decode policy. Use admission limits, bounded batches, queue-age
fairness and idle-capacity borrowing. Protect frame deadlines and avoid submitting
long decode queues that cannot be withdrawn. This is a service target, not an
enforceable GPU partition or a ratio of CPU worker counts. Confirm whether decode
actually uses the same physical GPU; include CL readback and GL upload costs.

Before assigning speedups, capture CPU rebuild/sort/submit times, dirty faces and
vertices, upload/copy bytes, draw counts, GPU pass times, residency, readbacks,
decode latency and p50/p95/p99 frame times. Measure change-heavy scenes separately
from static warm scenes. Offloading a CPU bottleneck can make a GPU-bound scene
slower; accept only measured end-to-end gains with correct visuals.

Qualification matrix:

* Native Windows/Linux Core 4.3 with compute and depth peeling, 4.4+ with current PPLL, plus bundled Mesa/Zink on
  representative AMD/NVIDIA/Intel devices; record actual version/profile/settings.
* PBR and legacy materials, normal maps, alpha modes, terrain/water/sky, HUD/text,
  avatars, flexi, media, physics debug, selection/picking, shadows, probes/mirrors
  and snapshots. Include nonuniform scale, animated transforms and missing LODs.
* Resize/HDR/transparency changes, low-memory allocation failures, shared-context
  uploads, cold login/teleports, texture arrivals and geometry eviction/reuse.
* Image comparisons plus targeted picking/bounds tests and GL error validation;
  performance captures without diagnostic instrumentation that distorts timings.

Implement R1-R3 and measurement first; then residency and opaque batching; then
per-view GPU culling/LOD. Expand deformation and GPU occlusion only where captures
justify them. No rendering build or runtime qualification was performed for this
documentation-only audit.
