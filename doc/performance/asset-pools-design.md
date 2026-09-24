# Asset pools for mesh sources, decoded pixels, and preparation work

Status: first ownership/reclamation implementation built for development testing.
The broader architecture below remains the roadmap, not a claim that every phase is implemented. Applies to vkstorm-devel.

## Implementation checkpoint (2026-09-24)

Implemented:
- Move-only LLVolumeLease for preparation consumers of the existing decoded source pool.
- Per-LOD reclamation only with zero manager leases and no external LLPointer borrowers. A bounded 64-group scan releases at most one decoded payload per frame, outside the manager lock. Idle grace is 60 seconds normally, 5 seconds under the existing system-memory-low signal. These are initial test values; reclamation/redecode behavior needs in-world validation. Procedural volumes and unavailable-asset results retain their existing policy.
- Sole-owner, identical-size raw-to-saved pixel transfer at destroyRawImage. External consumers and resized derivatives retain independent copies. Existing saved expiry, texture demand, focus handling, pressure discards and decoder concurrency are unchanged. This is ownership transfer, not unrestricted copy-on-write pixel sharing.
- Lightweight queued object references guarded by the compute-owner generation, with a strong lifetime pin only during a service turn.
- Output-byte reservations charged alongside staged/live compute geometry; cancellation and dependency waits release staged work and source leases. Reentrant cancellation keeps payloads alive until the executing callback returns.
- Development counters for active preparations, reserved/staged bytes, reclaimed LODs and saved pixel transfers/copies.

Remaining phases: complete unique-allocation accounting and retention reasons; immutable shared pixel storage across independently mutable consumers; an explicit asset-key producer registry beyond existing fetch coalescing; additional priority scheduling; cross-object prepared-output sharing; GPU-only immutable compute buffers. These are not enabled by this checkpoint. The first in-world gate must establish no visibility regression, acceptable reacquisition behavior and no renewed frame-time collapse before advancing the broader rollout.


## Goal and constraints

Reduce unnecessary CPU retention and simultaneous temporary allocations while preserving image quality, mesh visibility, progressive loading, picking, and reliable fallback rendering. Aim for 4-6 GiB working set in representative busy scenes; also track private commit so paging is not mistaken for a saving. This is a measured target, not a hard process limit or a promise that every scene fits.

Preserve current test settings: texture disk budget 4096 MiB, general asset disk budget 2048 MiB, compute geometry budget 768 MiB including staged replacements. Preserve size-uncapped reusable vertex/index buffers and existing age-based cleanup. Do not impose a new cap on that pool.

## Verified starting points

- LLVolumeMgr already shares LLVolume entries by parameters and LOD. LLVolumeLODGroup::derefLOD decrements per-LOD references but does not release a zero-reference LOD. LLVolumeMgr deletes the group when its total references reach zero. Thus a referenced sibling can retain otherwise unused LOD allocations.
- LLViewerFetchedTexture::saveRawImage creates a separate LLImageRaw for ordinary saved images, including when dimensions and components are unchanged. Icon, thumbnail, and sculpt paths can also create resized derivatives. Existing raw-image pointers and data locks must be respected.
- Compute BuildJob retains up to four volume references plus staged faces. Staged and published compute buffers already share resident_bytes accounting. Runnable stale jobs are currently discarded when serviced; parked jobs are removed on invalidation.
- LLVertexBuffer::copyResidentRange reads CPU backing data and copies vertices on both CPU and GPU; indices are rebased on CPU. Permanent CPU mirrors cannot simply be freed under the current interface.

## Architecture: common ownership rules, specialized pools

Use one shared ownership/accounting vocabulary, with separate mesh and pixel implementations. Do not replace the existing caches with a second parallel cache of the same allocations. A preparation scheduler borrows from these pools; it is not another source-asset cache.

An entry has an identity and generation, state, payload bytes/capacity, consumer leases, last-use information, and an optional in-flight producer. States: absent -> producing -> ready -> idle -> evicted. Failure/cancellation is generation-qualified; late completion cannot publish into a replaced generation.

Acquire returns a lease or a readiness subscription, never a render-thread blocking wait. Concurrent acquisition joins a single production operation for that representation. Publication occurs on the owning thread. Consumers borrow immutable data concurrently. A mutable producer is private until publication; mutation after publication requires an explicitly separate derived entry or a private copy.

Leases record reasons: visible source, picking/fallback, preparation, CPU texture callback/sculpt/edit, or upload-in-flight. Reason counts explain retention but bytes are counted once per allocation. RAII releases ownership. Eviction candidates have no consumer leases; the idle cache owns their last reference. Keep existing strong references conservatively until all relevant callers have migrated. Untracked borrowers make an entry non-evictable.

Do not hold pool locks during decode, GL operations, destruction, or callbacks. Remove an eviction candidate from lookup under lock, then reclaim outside the lock on the appropriate thread. CPU task completion and GPU completion are distinct: retain an upload lease until the actual upload API no longer needs its CPU input.

## Mesh source pool

Key: asset identity, asset/content generation, resolved authored LOD, and all geometry-affecting volume parameters. Preserve the existing full parameter key initially; only canonicalize after proving equivalence. Several nominal LODs resolving to the same authored LOD share one entry. Unique/deformed volumes remain per-object derivatives.

Adapt LLVolumeMgr ownership rather than copying LLVolumeFace arrays into a new cache. Give each LOD independently tracked payload ownership, allowing an unused LOD to enter the idle set even when a sibling remains active. Audit direct getGroup/refLOD access and completion publication before enabling eviction: no raw volume pointer may survive reclamation without a lease.

Positions, indices, normals, texture coordinates, weights, and tangents are tracked as separate capacities where useful. Lazy tangent generation and spatial acceleration structures need one coordinated producer or an immutable derived-data entry; do not declare LLVolume immutable while genTangents or copyVolumeFaces can mutate it.

Objects retain the source needed for picking, bounds, and direct fallback. Compute preparation borrows only the LODs it will consume; it releases them on publication/cancellation. Missing optional LODs need keys/subscriptions, not empty retained volume objects. Never evict active fallback data to admit speculative refinement.

## Decoded pixel pool

Key: asset/content generation, actual decoded discard level or dimensions, component layout, and decoding/derived transformation identity. Auxiliary planes and resized/icon/sculpt derivatives are distinct representations. Mutable local textures, video frames and render targets stay outside this pool initially.

Introduce an immutable pixel-storage handle with existing LLImageRaw adapters. Audit every mutating operation and writable data accessor before sharing. First optimization: where saveRawImage needs identical pixels, share storage between raw and saved roles instead of copying. Preserve distinct saved/raw metadata and existing callback/lifetime contracts. Requests for actual rescaling produce a separate keyed derivative. Writable access explicitly obtains a private copy; do not silently allow aliases to observe modifications.

Saved, sculpt, edit, callback and active-upload users pin the representation they need. An idle decoded image is evictable without deleting its compressed disk asset or resident GL texture. A higher-resolution decode is not interchangeable with a lower-resolution request unless an explicit existing conversion produces the right representation. Preserve progressive decoding and request priority escalation; coalescing must not delay first visible pixels behind a full-resolution job.

## Preparation ownership and bounded admission

Separate lightweight demand records from active jobs that hold source leases and output buffers. Demand records contain a weak object generation, target representation, priority and dependency subscriptions. No strong object or source references are needed merely to wait in the queue.

Before activating a job, atomically reserve estimated incremental staged bytes, including buffer rounding and overlap with old visible geometry. Active preparation remains subject to the existing 768 MiB compute limit, time/work allowances, and a measured staging admission watermark. Reservations prevent many individually affordable jobs from collectively overcommitting. Allocation converts reserved bytes into actual bytes; publication transfers staged to resident ownership without double charging.

Do not choose an arbitrary new hard staging ceiling before observing active/staged peaks. Start with accounting and existing total budget. Evaluate a staging watermark separately from reusable-buffer capacity. Allow one oversized first-visible job only when it fits the total compute budget; otherwise keep/use the normal direct path rather than silently dropping the object. Reservations must leave capacity for progress, and completion work gets service before new refinements.

Waiting on dependencies releases optional source leases and idle output reservations. For partially built output, retain it only with a charged reservation and a bounded, measured retention policy to avoid repeated reconstruction. Cancellation immediately detaches heavy payloads; deferred bookkeeping holds only lightweight records. In-flight consumers keep their own leases until safely finished. Publish a complete generation at the existing main-world frame boundary, retaining old visible geometry until replacement is valid.

Coalesce per-object equivalent requests first. Across objects, share prepared output only if a complete conversion identity matches (source generations, layout, transformed attributes, material/texture indices, skin binding, etc.). Initially exclude cross-object prepared buffers: decoded source sharing is safe without asserting that transformed render data is identical.

## Scheduling

Top-level order: visible/in-range world geometry, self avatar and attachments, other avatars and animesh, optional prefetch. Within a class, first usable geometry precedes refinement; visibility, screen importance and distance determine service order. Use bounded aging/minimum service to prevent starvation. Offscreen status can reduce speculative priority; it must not release data needed by active picking/fallback consumers or introduce a new offscreen-object eviction scheme. HUD rules and existing alpha/opaque pass order are unchanged.

## Reclamation and accounting

Separate active pinned, idle reclaimable, in-flight decode, reserved preparation, allocated staging, live CPU buffer mirrors, reusable buffers, and published GPU storage. Record shared allocation identity so raw/saved aliases and LOD aliases are not double-counted. Never sum GPU byte estimates into process RAM.

Pressure policy reclaims cold zero-lease source/pixel entries and cancels obsolete speculative work in bounded batches. Use hysteresis and reuse/eviction/redecode metrics to avoid oscillation. Asset-pool idle watermarks are tunable after measurement, not a blanket per-frame eviction loop or a cap on the reusable VBO pool. Pinned allocations may exceed a soft target; report the ownership reasons rather than discard required data.

Log byte totals, peak active preparations, reservations, cancel-to-release delay, shared hits, producer joins, evictions, rapid reacquisitions/redecodes, and failed admissions every five seconds in the development build. Sampling should read maintained counters, not walk every face each frame. Retain process working-set/private-commit and frame-time measurements. Driver/allocator/profiler memory remains an explicitly unassigned category until independently measured.

## Delivery and validation

1. Add unique-allocation accounting and ownership reasons; establish native OpenGL and Mesa baseline traces. No eviction changes yet.
2. Introduce lightweight preparation demand and reservations; promptly release cancelled heavy work. Verify cancellation, fairness and complete-generation publication.
3. Add per-LOD mesh leases and idle reclamation once raw-pointer consumers and mutable derivatives have been audited.
4. Add immutable pixel storage and saved/raw sharing; migrate mutation paths and callback/upload lifetimes before enabling pixel eviction.
5. Evaluate removal of CPU mirrors for immutable compute buffers as a separate change, with a GPU-side copy/index strategy. Preserve existing VBO reuse.

Regression tests: same-key acquisitions share one payload/producer; distinct generations and representations do not; cancellation with late completion; last-lease release; alias byte accounting; sibling-LOD eviction; mutation isolation; duplicate raw/saved handles; decoder failure/retry; reservation conversion/rollback; oversized jobs; starvation bounds; object deletion, teleport and origin shift during preparation; partial material/skin arrival; unsupported compute direct fallback.

In-world gates: no missing world meshes or camera workaround; correct picking, rigged clothing, animesh, sculpties and editing; stable detail and texture sharpness; normal progress for cold/warm caches, teleports, crowded scenes and focus transitions. Compare settled working set and private commit, memory peaks, time to first geometry, refinement time, and frame-time tails under matched conditions. Reject memory savings that increase rapid redecoding/reconstruction or reproduce the capped-VBO FPS collapse.

Each phase stays independently reviewable on development. No master PR until the user validates in-world behavior. No promise of 4-6 GiB until allocation accounting shows it is attainable for the measured scene.
