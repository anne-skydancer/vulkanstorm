# Progressive OpenGL mesh LOD and bounded streaming

Development worktree: `gl-compute-mesh`, branch `codex/gl-compute-mesh`.

## Current behavior

Mesh levels are Lowest, Low, Medium and High (0-3). Cold requests go through the
first missing authored prerequisite rather than requesting all four levels at
once. Missing levels use the repository's authored-level resolution. Known failed
levels are skipped when requesting a finer fallback. Cached usable geometry stays
visible while a finer source arrives. HUD request behavior is unchanged.

This request policy applies to unrigged mesh, rigged avatar attachments and
Animesh. It also applies to objects using the normal direct-draw path. The system
avatar body has its separate joint/pixel-area LOD policy.

GPU residency no longer waits for every level. Completed levels form an availability
mask; commands for unavailable levels reference the best resident fallback. An
intentionally empty authored range remains empty, distinct from missing data.
Each object publishes all its faces together at the main-world preparation point.
Its opaque, alpha, emissive, shadow and PPLL replay submissions share that mapping.

CPU picking retains the existing available source mesh; residency no longer forces
High on the CPU. Selection/editing returns to the existing CPU LOD path. Picking
can therefore use a coarser silhouette than the GPU-refined geometry until that
interaction changes the CPU LOD. This requires in-world validation.

## GPU selection and streaming feedback

The first compute phase calculates one desired level per avatar from its animated
bounds and center (full diagonal for ordinary avatars; half for control avatars).
The second phase selects each mesh's indirect range. Static meshes use their own
center/radius. Existing distance, FOV, close-range and rounding policy is retained.
Skinning remains in the established vertex shaders and joint palettes.

Draws never wait for a CPU LOD decision. Every 250 ms at most, the GPU demand buffer
is copied to a staging buffer and fenced. The main thread only consumes a completed
fence, using a zero-timeout poll, to schedule missing detail. This is asynchronous
streaming feedback, not synchronous render-decision readback. Weak ownership tokens
reject feedback belonging to deleted, changed or reused mesh slots.

Refinement builds replacement packed buffers while the old buffers remain drawable.
Previously resident vertex attributes are copied on the GPU, with coherent CPU
shadow data; only the rebased old indices and newly converted geometry are uploaded.
The replacement is published after every face is ready. Old buffer release uses the
existing vertex-buffer pool and GL lifetime rules. No new blocking GPU fence is
introduced for drawing or replacement.

The existing face converter has 16-bit vertex offsets. If all cached levels cannot
fit together, the resident set retains the finer levels that fit rather than refusing
High forever. Subsequent GPU demand can request a different resident subset.

## Bounded processing

- Resident geometry budget: `RenderGLComputeMeshBudgetMiB`, default **1024 MiB**,
  clamped to 64-2048. Includes active geometry and staged replacements. Lowering
  the setting limits new allocation; it does not immediately evict active buffers.
- Resident preparation: `RenderGLComputeMeshPrepareMS`, default **1.5 ms** per
  world frame, checked between work units; at most 32 units and 64 queue scans.
- Allocation, one face/LOD conversion or copy, and publication are separate units.
  Stop admitting further units after 4 MiB of logical allocation/transfer/copy work in a frame.
- Mesh completion delivery: **1.5 ms**, at most eight completion units per frame,
  round-robin across geometry, unavailable geometry, skin and physics/decomposition.
- Each mesh or skin completion notifies at most eight waiting objects per unit,
  with a 0.25 ms check between notifications. Waiters remain in the authoritative
  maps, so object unregistration remains effective between frames.
- The existing drawable-rebuild queue now honors the time budget passed by
  `display()` (50 ms per second of frame time, clamped to 0.1-5 ms per batch),
  with at most 64 entries per frame.
  Unfinished entries rotate to the tail; untouched entries retain their geometry.
- New network submissions pause at a backlog of 256 loaded mesh/skin completions;
  already in-flight requests finish normally.
- Pending resident jobs are capped at 8,192; active face records at 8,192 and shared
  avatar records at 1,024. Rebuilds/deletion cancel work through ownership tokens.

Time and byte targets are cooperative, not hard real-time guarantees: one source
volume publication, face conversion, allocation or GL operation can overrun. The
scheduler records maximum batch duration instead of claiming that such operations
are preemptible. Individual spatial-group rebuilds and region-cache saving have not been
converted into resumable jobs by this change.

## Admission and lifecycle

Resident indirect draws cover static opaque PBR mesh and rigged legacy/PBR meshes,
including avatar bodies, hair, clothing and playing Animesh. HUDs, selected objects,
animated UVs, media, GLTF scene objects, unsupported layouts, and unrigged moving
objects retain the existing draw path. Their mesh asset requests still progress.

Skin/bind-shape changes, geometry/material rebuilds and deletion invalidate resident
ownership. Agent-origin changes restart pending conversion while shifting active
metadata. Mesh-source arrivals alone preserve valid active residency for refinement.
Shader reload replaces compute buffers/program while retaining drawable geometry;
context teardown invalidates all records. Both `RenderGLComputeLOD` and
`RenderGLComputeMesh` must be enabled for resident offload.

### Rigged refinement follow-up

The first crowded-region Mesa run reached about 508 MiB of the 512 MiB budget,
with repeated preparation cancellations after network/completion queues drained.
The follow-up build raises that budget to 1024 MiB at the user's request.
Rejected refinement previously left coarse residency owning CPU LOD indefinitely.
Resource-limited admission now releases resident ownership and restores normal
mesh loading/rebuilding, with a 300-frame admission backoff. Unsupported refinement
also returns an already-resident object to the direct path. This favors correct
detail over retaining offload when the extra geometry cannot fit.

Refinement retains already-resident finer source levels even when current demand
requests a lower missing level. The 16-bit packing limit can still discard coarser
levels. Mesh-arrival notifications now retain attachment overrides, avatar loading
notifications and complexity updates even when resident geometry avoids rebuilding.
Transient zero-sized animated bounds now select High instead of selecting an
often-empty Lowest rigged range. Native AMD and Mesa GPU tests cover that case.

Still-clouded ordinary avatars are excluded from resident admission, avatar LOD
input and resident draws. Control avatars (Animesh) retain their normal eligibility.
The loading cloud itself is a particle effect, not mesh geometry, and remains on
the particle path. Its default RGB is now `(0, 0.5, 0.5)`, matching the Vulkan teal
clear color; particle alpha, timing and any user-provided cloud preset are preserved.
Non-mesh drawable updates are extracted and serviced independently before the
bounded mesh batch. Applying mesh limits to the shared drawable queue could delay
particle geometry updates and make a loading cloud appear frozen. A regression
test places particles behind 100 mesh entries and verifies independent service.

The fixed compute and demand buffers occupy 1,069,056 bytes, separate from the
resident geometry budget. Original direct geometry, CPU volume storage, textures,
render targets and temporary driver allocations are additional costs.

## Logs and validation

`ComputeLOD` logs active slot count, total geometry bytes, the configured budget,
indirect draws, rigged draws, CPU LOD bypasses, metadata uploads, preparation backlog,
work units, aggregate/max batch time, geometry allocation/upload/copy bytes and cancelled jobs.
`MeshBatch` logs mesh/skin/other completion backlogs, queued LOD requests, active
mesh HTTP requests, completion units and maximum completion batch time. The same
category reports drawable rebuild backlog, completed work and maximum batch time.

`test_mesh_streaming.cpp` exercises the production policy helpers: 124 availability
and resident-fallback cases, missing assets, bounded fan-out, deletion while pending,
callback reentrancy, newly registered waiters, visible intermediate-level
promotion and bounded/fair drawable rebuilding. Compile with a C++17 compiler:

```
clang++ -std=c++17 -O2 -I indra/newview scripts/perf/test_mesh_streaming.cpp -o build-vc170-64/test_mesh_streaming.exe
```

`test_gl_compute_mesh.py` uses an offscreen real GL context and the production
compute/skinning shaders. Native AMD OpenGL and packaged Mesa/Zink both pass:
33,068 static command checks, 120 shared-avatar checks, direct/indirect skinning
color/depth parity, and progressive publication of all four levels while GPU
streaming demand independently requests High. A further four color/depth checks
exercise GPU-copied geometry and index rebasing during buffer growth. These tests do not establish full
viewer visual parity or an in-world frame-time improvement.

Live validation remains necessary for cold/warm login, busy regions, Animesh,
attachment changes, transparent hair, skinning, selection, teleporting/deletion,
shader reload, memory pressure and repeated camera/refocus changes.

## Build

RelWithDebInfo, Tracy enabled, no installer. Reconfigured through `autobuild configure` with `USE_MESAZINK=ON` so runtime staging includes the required DLL pair.
Verified on 2026-09-23: `vulkanstorm-bin` and `copy_w_viewer_manifest` both completed
successfully. The EXE and PDB were linked at 21:04 local time with the 1024 MiB
budget and rigged/cloud fixes. The manifest copy was
rerun after linking because its dependency direction otherwise leaves the branded
executable from the previous build.

Run `build-vc170-64/newview/RelWithDebInfo/Vulkanstorm-RelWithDebInfo.exe`.
Its SHA-256 is `7564A6EE0C6F064D98193CF81DB2F1ADE96A91649D4D8B76559FEEBEA971E41F`,
identical to `vulkanstorm-bin.exe`. Staged settings, `cloud.xml`, `meshLODC.glsl`, and both
`mesa/opengl32.dll` and `mesa/libgallium_wgl.dll` match their source/package hashes.
Evidence in `build-vc170-64`: `rigged-refinement-fix-build.log`,
`rigged-refinement-1024-build.log`, `rigged-refinement-stage.log`, and
`rigged-refinement-stage-verification.json`.
The viewer has not been launched for in-world validation of this build.
