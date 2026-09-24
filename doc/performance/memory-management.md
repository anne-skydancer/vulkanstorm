# Firestorm upstream memory-management baseline

Runtime memory management is restored to Firestorm upstream
`128568690c89a97d1781de28f26412fdb4efe54a` (the local upstream reference).
This replaces the Alchemy-derived adaptation; no Kokua policy is introduced.

The restored paths cover system-memory pressure response, scene-memory limits,
texture-demand traversal, background texture downscaling, and GL texture allocation
accounting. The bounded 32-face scan and its cross-visit demand history, automatic
RAM-percentage scene budgets, pressure helper, and replacement texture-allocation
ledger are removed along with their dedicated tests.

Upstream scene settings default to 2048/4096 MB. Its adaptive code may lower saved
limits to 768/2048 MB when available memory drops below 8096 MB. These thresholds
control scene loading and retention, not a hard cap on total process memory.
The Firestorm draw-distance optimization preference and disabled upstream
offscreen-object eviction predicate are preserved.

OpenCL JPEG2000 acceleration, GPU mesh LOD, and bounded mesh streaming are separate
and unchanged. The requested 6400 MB total disk-cache default remains, providing
approximately 4096 MB of usable texture-body cache. Existing user settings are not
rewritten by this source change.

Validation compares the restored production paths with the pinned upstream source,
checks settings XML and removed-helper references, and checks the patch for whitespace
errors. Full viewer rebuilds and an in-world focus/recovery comparison remain necessary
before claiming the sustained slowdown is resolved.
