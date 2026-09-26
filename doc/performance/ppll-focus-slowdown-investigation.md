# PPLL persistent slowdown investigation — 2026-09-25

Read-only source/runtime-log investigation; no renderer settings or implementation changed.

## Evidence identity

Last run: Vulkanstorm RelWithDebInfo 7.2.5.81974, Mesa/Zink on RX 9070 XT.
Log: `%APPDATA%/Vulkanstorm_x64/logs/Vulkanstorm.log`, 14:50:30–15:52:48 UTC.
Local Irish times below are UTC+1. Log SHA256: 95e4f37433c5ab6d74c66192634f997d5267a6b8a9a5c2590c488b6ac76fd646.
The run loaded from `worktrees/gl-compute-mesh/build-vc170-64/newview/RelWithDebInfo`.
The source and staged resolve shader currently match SHA256
`0a1deb7114cd5dc49ff1e9b0978f33e7cc651fed1c7e78ad322b5df2f583317c`.
The local Mesa checkout `C:/Dev/mz-src` is at 00e42c51b10, matching the revision reported by the run.
Current staged-file hashes do not independently prove which bytes were read earlier in the session.

## Abrupt transition

OITProfile interval medians:

| UTC interval | Capture GPU ms | Resolve GPU ms | Requested nodes | Capacity |
|---|---:|---:|---:|---:|
| 15:10:00–15:13:11 | 6.196 | 1.018 | 83,292,220 | 33,554,432 |
| 15:13:13–15:13:40 | 78.518 | 87.364 | 83,374,490 | 33,554,432 |
| 15:17:00–15:18:59 | 44.553 | 41.339 | 15,944,816 | 33,554,432 |
| 15:35:00–15:36:00 | 77.811 | 87.437 | 69,604,270 | 33,554,432 |

At 15:13:12 UTC (16:13:12 local), the first slow profile reports capture 373.656 ms and resolve 89.5243 ms (log line 11786). The preceding profile (11771) reports capture 9.09172 ms and resolve 1.0196 ms. Requested nodes change from 83,378,238 to 83,391,280, about 0.016%. Node count alone does not characterize list-length distribution or visibility, but there is no aggregate workload explosion at this transition.

Head clear remains about 0.005 ms. Capacity is 512 MiB, total PPLL allocation about 525.184 MiB; viewport remains 2560x1350. Allocation logs occur at startup and shutdown resize, not at the transition. Focus was lost at 15:12:57 and returned at 15:13:24; the expensive state persists after return. No evidence of a resize triggering this particular collapse.

The lower-load 15:17–15:18 interval is also slow despite no node-pool overflow. Overflow therefore cannot explain the complete persistent slowdown on its own. GPU timings are sampled, not every-frame traces; CPU swap time overlaps GPU execution and must not be added to GPU stage times.

## Source trace

- `pipeline.cpp::allocateAlphaOITBuffers` allocates the GPU-written node pool using `glBufferData(..., GL_DYNAMIC_DRAW)`.
- Mesa `src/mesa/main/bufferobj.c::buffer_usage` maps that hint to PIPE_USAGE_DYNAMIC.
- Zink `zink_resource.c::create_buffer` initially requests host-visible/device-local memory for this usage. `update_alloc_info_flags` adds HOST_COHERENT for dynamic buffers.
- If the device-local-visible allocation fails, `alloc_bo`'s allocation loop can demote dynamic/coherent allocations to HOST_VISIBLE_COHERENT. The non-dynamic alternative demotes to DEVICE_LOCAL.
- This is an allocation-time policy. It does NOT prove the existing pool was migrated at 15:13:12; the log has no physical residency/placement evidence for that event. Windows/driver residency and scheduling remain unmeasured.
- Allocation is reused when dimensions/capacity/layer limit match. Focus is not part of the reuse key and there is no focus-triggered pool recovery in this path.
- Every capture resets the allocator and clears heads, with buffer/texture update barriers. Capture-to-resolve uses image/storage/atomic barriers. Append checks capacity before publishing a node; resolve traversal is bounded. No demonstrated missing reset or unbounded list growth across frames was found.
- Both independent profiling paths show the resolve slowdown. The detailed OIT profiler reads a copied counter only after its final query is available; it does not read the live allocator. No synchronous per-frame counter-read path was observed in the log. Instrumentation can still affect the driver and needs an A/B check.

## Confirmed pressure amplifier

Capture increments the allocator before an opaque-depth visibility test. Four shader families append data, including fragments behind opaque geometry; the resolve then rejects occluded nodes. Successful capture discards prevent ordinary late depth tests from undoing those SSBO side effects.

The pool was saturated even while performance was better: approximately 83.4M requests against 33.55M slots. Overflow falls back to ordinary blending rather than out-of-bounds node writes. This is bounded allocation, not evidence of an ever-growing PPLL pool. The number of requests rejected by an earlier opaque-depth test is not yet measured.

Private commit around the transition is approximately 24 GiB and remains similar across it. The process has roughly 11.7 GiB resident physical memory there. These counters neither isolate driver storage nor prove a leak. Mesh completion and drawable-rebuild queues are near empty around the abrupt transition; no corresponding CPU mesh-preparation surge explains the persistent state.

## Ranked interpretation and next discriminating checks

1. Resource-residency/bandwidth degradation affecting the large node pool is the strongest current hypothesis: capture and resolve slow together under nearly unchanged demand, small head clearing does not, and the slowdown persists with reduced demand. The rate change is compatible with slower memory access, but PCIe traffic was not measured.
2. Background scheduling/driver state or profiling interaction remains possible. Focus timing is correlation, not proof; no renderer focus-triggered storage mutation was identified.
3. Heavy overdraw, atomic contention and late opaque rejection amplify cost but do not alone explain this step change.

A controlled development experiment should record DXGI local/nonlocal usage and budget across focus transitions, separate the pool identity/generation from shader timing, and compare the current dynamic allocation against an unmapped immutable node pool. Immutable storage removes the dynamic/coherent hint in this Mesa path but does not guarantee physical VRAM residency. Preserve the same capacity, image quality and normal render semantics; keep counter reset/readback storage separate.

A one-time, deliberate fresh allocation of the same-sized pool after degradation can discriminate stale resource state from continuing scene cost. It is a diagnostic experiment, not a proposed every-refocus workaround, and can itself stall or alter memory pressure. Compare profiling enabled/disabled as a separate variable.

Independently, prototype capture-only opaque-depth rejection using a safe read-only depth source and exact current depth semantics, with no feedback sampling of an attached writable image. Do not change the legacy/residual rigged-depth ordering, skip alpha layers, or increase the pool budget as a blind fix.

Existing logs cannot prove the physical-residency hypothesis retrospectively. Another controlled capture is needed to identify that final mechanism; no new viewer session was launched for this investigation.
