# Mesa/Zink performance: archived failure review

Reviewed 2026-09-22. Performance branch baseline: `fcaf5d5101`.

## Objective and current baseline

Target uncapped FPS comparable to native OpenGL at equivalent quality, smooth
hero-probe reflections, and no visual regressions. Keep both current PPLL fixes.
The user reports no recurrence of flashing since those fixes. Earlier flashing
was a separate correctness issue, not evidence of an FPS bottleneck.

## Historical evidence

Archive repository: `H:/VulkanStorm`, read without modifying its checkout or stash.
Historical branch: `archive/phase-p08c-regression-fix-20260830`, tip
`04f36a4caa807d80d2dceffd974a4c2859ca6e63`.

Read historical files with `git show <revision>:<path>`; they are not present in
the archive's current reconstruction checkout.

### Rejected WGL synchronization changes

Source: `doc/renderer/mesa-zink-rejected-sync-experiments.md` at the archive tip.
Tests dated 2026-08-15 used RX 9070 XT, AMD Adrenalin 26.7.1, Gallium WGL/Zink,
and `WGL_ZINK_FRAME_LATENCY=2`.

- A bounded fence queue replacing the generic end-of-frame wait, retaining one
  prior frame in flight and waiting with `st->pipe`, did not eliminate flashing.
  Frequency varied; flashing returned during longer runs and region changes,
  including water/environmental rendering.
- Keeping that queue but passing a null pipe context to `fence_finish` also
  left visible one-frame flashing. Both candidates were explicitly rejected.
- Historical Mesa stash objects: `b7d4ae0ad4caeb2020176422cf81e1dfce4c0523`
  and `870b9a3e18f448275f32d21d79358bf7419c1cb7`.

Do not reuse these patches as performance fixes. Reopening their hypothesis
requires new evidence identifying resource lifetime or presentation dependencies.
The record establishes failed acceptance, not proof that either patch originated
the flashing, nor a measured FPS diagnosis. Its accepted package at that time
(`5a5f893842`) is historical, not an instruction to downgrade today's package.

### UI semantic and resource-identity failures

Source: `doc/renderer/p08c-ui2d-migration-checkpoint.md` at the archive tip.

- Constant-color mode replaced sampled RGB instead of multiplying by tint,
  flattening textures and icons. Batching/state optimization must preserve the
  exact sampling, tint, blend, clipping and ordering contract.
- Reopened dialogs reused logical texture identities with restarted generations,
  conflicting with persistent residency. Resource reuse must preserve content
  identity and generation across destruction/recreation; reopening tests matter.
- Focused corrections and passing contracts did not establish complete UI parity.

### Rejected UI performance stash

Archive stash named `rejected-p08c-ui-performance-experiments-20260830` contains
GHI UI assembler/consumer/presentation changes including reusable upload, vertex
and index buffers and binding-resource changes. Inspected its diff only.
The stash name establishes rejection of the combined experiment; it does not
establish which individual change caused a defect. Do not transplant it or infer
that all buffer reuse is unsafe. Any new reuse needs explicit GPU completion,
ownership and invalidation reasoning and independent validation.

## Consequences for the performance work

1. Profile existing vertex uploads and hero-probe stages first; do not begin by
   altering WGL frame latency, fence contexts or texture synchronization.
2. Measure upload call counts, bytes and CPU cost before testing coalescing.
   Treat persistent buffer reuse as a separate, higher-risk experiment.
3. Retain current synchronization unless measurements and dependency analysis
   justify a specific replacement. A faster short run is insufficient evidence.
4. Test one change at a time, in RelWithDebInfo, with matched OpenGL/Zink scenes.
   Record frame-time distributions, GPU/driver/backend identity and memory use.
5. Include stationary and moving-camera video, long runs, region transitions,
   water/hero probes, transparency/glow, texture streaming and reopened dialogs.
   These are regression checks, not claims that those actions caused old defects.
6. Reject new mesh flicker, missing geometry, changed tint/blending, stale textures,
   probe seams or temporal instability regardless of average FPS improvements.

No archived patches were applied. No performance or visual qualification has
yet been performed for this branch.
