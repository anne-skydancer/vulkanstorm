# Resident GPU particle pipeline

Development: `codex/gpu-render-offload`, based on `vkstorm-devel`.
Release integration: `codex/gpu-render-offload-release`, based on `master`. Updated 2026-09-27.

## Contract and current status

Windows/Linux retains **OpenGL 4.3 Core** as the viewer baseline. The resident
particle path requires OpenGL 4.5 / GLSL 4.50 and
`GL_ARB_fragment_shader_interlock`, `GL_ARB_bindless_texture`,
`GL_ARB_shader_draw_parameters`, and `GL_ARB_indirect_parameters`.
Missing advanced capabilities select CPU simulation, ordering and submission
automatically. They do not reject viewer startup. There is no offload toggle,
small-population CPU threshold, or parallel CPU simulator on the resident path.
Particles remain in sorted alpha, outside both PPLL and depth peeling.

The live viewer integration is implemented: source emission feeds GPU births;
resident simulation replaces CPU group updates; the alpha traversal consumes GPU
ordering and counts. The integrated viewer pixel test passes on native AMD and
Mesa/Zink: color blending, opaque-depth rejection, writable depth, interleaved
glow, depth-only rendering and split alpha intervals. In-world visual and
frame-time acceptance remains necessary; Linux has not been built.

## Data flow

1. The CPU resolves source objects, targets, wrists, textures and region wind,
   runs emission scheduling and random birth generation, and uploads births and
   source snapshots. It does not keep a second copy of live particle state.
2. Compute allocates dead slots, enforces the particle limit, establishes ribbon
   links, integrates motion and appearance, and retires expired/killed particles.
3. Per view, compute expands billboard/ribbon vertices, creates spatial ranges
   and conservative bounds, culls groups, sorts back to front and creates
   adjacent material ranges. World and HUD domains are separate.
4. The existing non-particle alpha traversal supplies its depth boundaries.
   Compute partitions the particle stream around those boundaries. Indirect
   count draws consume GPU-generated intervals without downloading counts.
5. Ordered fragment interlock applies the particle's blend factors to a detached
   HDR color image. Scene depth and virtual particle depth preserve LEQUAL
   behavior. A covered-quad replay resolves writable depth before the next world
   alpha draw. GPU commands interleave color and glow for each particle in one
   indirect-count call, so nearer alpha attenuates farther glow before adding
   its own emission. Glow remains an alpha-channel contribution.

The stable particle ABI is 192 bytes, source ABI 128 bytes, material ABI 16 bytes.
Slots never move during sorting. Generation checks prevent recycled particles or
sources from being mistaken for previous owners. The pool is bounded at 8192.

## Ownership and CPU work that remains

`llparticleviewer.cpp` owns source/material references and the viewer integration.
`llparticlepipeline.cpp` owns GPU buffers, kernels, dispatch and indirect commands.
`llparticletexture.cpp` owns immutable bindless snapshots of mutable viewer images.

These operations deliberately remain on the CPU:

- Network/object lifetime, source scheduling, random emission and birth records.
- Resolving object/wrist/target transforms and uploading source/region snapshots.
- Texture acquisition, parcel-media selection, reference ownership and GPU
  resource creation. Births use indexed source/material lookups, not a complete
  source/material scan for every new particle.
- Traversal of non-particle world alpha, shader/resource binding and submission.
- Processing a user pick: GPU intersection returns only the winning source
  handle and distance. This is an on-demand synchronization, not a frame loop.
- Texture streaming receives a fenced aggregate demand per material at most
  once per simulation second. Polling is nonblocking. This telemetry contains
  no particle positions, draw counts or sorted ranges.

No per-frame particle-state readback is used to preserve CPU spatial groups or
CPU faces. Source lifetime can outlast emission until its particles expire.
Explicit source kills are distinct from merely ending emission. Origin shifts
rebase existing GPU slots before births already expressed in the new coordinate
system arrive. Region cleanup kills resident world particles in that region and
spares HUD particles. Wind follows the resident position across region boundaries
and preserves this fork's agent-region width convention.

A dispatch/allocation/shader failure restarts emission on the CPU and latches
that fallback for the session. Existing resident particles disappear on this
exceptional transition; maintaining seamless continuity would require the CPU
shadow simulation or bulk recovery readback that this design avoids. Context
teardown releases resident state and all dependent resources.

## Texture safety

Bindless handles freeze texture storage/sampler state. The normal texture cache
must remain mutable, so particles use immutable GPU copies in the original format
and mip chain. `LLImageGL` revisions trigger refresh when content or sampler state
changes. Unchanged textures are not recopied each frame. Content-only changes
reuse the immutable handle; allocation/sampler changes retire it behind a fence.
The copies consume additional GPU memory. Context teardown may wait for resource
retirement; ordinary frame polling does not deliberately wait.

## Semantic and performance qualification

GPU simulation advances all live particles each frame instead of reproducing the
CPU invisible-group update cadence. Source scheduling remains CPU-owned. GPU slot
admission replaces the old live-CPU-count throttle; these scheduling differences
must be assessed with bursty and offscreen emitters.

Conservative frustum culling is implemented. The resident path does not apply the
legacy tiny-particle cutoff or the group area break in `LLVOPartGroup::updateGeometry`.
The latter uses the largest area encountered, then discards the rest of that CPU
group; it is not a total-area budget. Recreating it with different GPU spatial
bins would make visibility depend on unrelated grouping changes. Keeping live,
visible particles avoids that disappearance, at the cost of potentially more
raster work. A future screen-size admission policy needs separate visual and
performance evaluation, particularly for tiny particles and long ribbons.

World alpha interleaving has an offscreen interval check; water/reflection views,
real HUD/world lighting and default-framebuffer paths still need live visual
qualification. View geometry now derives the world camera from the current view
matrix, including secondary views. HUD render-type selection is separate.

The largest performance risks are bitonic dispatch overhead at sparse population,
state setup for empty alpha intervals, fragment interlock contention in dense
particles, extra texture memory/copies and contention with OpenCL decoding.
No FPS gain is claimed for this integration from synthetic correctness tests.
The earlier user-observed approximately 8-to-11 FPS improvement concerned the
preceding geometry-offload build, not the new resident simulation implementation.

## Driver compatibility

Native AMD 26.9.1 failed to fetch a bindless material sampler from the fragment
SSBO when additional fragment shader objects were linked. Reduced real-GL
reproduction isolated this from texture contents, residency and buffer layout.
The resident vertex shader now reads material data and passes a flat sampler and
blend metadata to the fragment shader. The handle is uniform within each draw.
This arrangement passes both drivers without driver-name detection or CPU work.
Shared atmospheric fragment helpers retain matching vertex interface outputs for
Mesa's stricter linking. The blend regression fixture includes an additional
fragment helper to cover this linking arrangement.

## Validation and reproduction

Use the project's Autobuild `RelWithDebInfoOS` workflow with Release feature and
dependency settings, KDU off/OpenJPEG on, and no installer. Run
`copy_w_viewer_manifest` after linking and verify the branded staged executable
matches `vulkanstorm-bin.exe`; the manifest can run before the final link.
The staged viewer needs its libraries, plugins, shaders and application assets.
Build 82004 has been compiled, linked and staged at
`build-vc170-64/newview/RelWithDebInfo/Vulkanstorm-Release.exe` in this feature
worktree. The staged and linked executable SHA-256 hashes match. Packaging is off;
OpenJPEG is enabled and KDU is disabled.

Windows real-GL checks:

```powershell
python scripts/perf/test_particle_pipeline.py
python scripts/perf/test_particle_pipeline_service.py
python scripts/perf/test_particle_texture.py
python scripts/perf/test_particle_ordered_blend.py
```

The tests accept `--opengl <path-to-mesa/opengl32.dll>` for Mesa/Zink. Readbacks
inside these fixtures are assertions, not the production frame path. The service
test compiles the production C++ dispatcher against a small host shim, then uses
real driver calls. It exercises capacities 0/1/65/8192, generation/ribbon repair,
birth admission, geometry, intervals/indirect counts, GL state restoration,
frustum bounds, picks, region retirement, cross-region wind and texture demand.
The scalar kernel suite checks 366,482 simulation/order/range fields.

On the development branch only, the RelWithDebInfo hook
`VULKANSTORM_PARTICLE_SELFTEST=1` runs once at login
in an isolated test process. It initializes the default environment, emits two
known HUD particles into an offscreen target and checks actual viewer color,
depth and glow results. Search its log for `PARTICLE_SELFTEST: PASS`; reaching
login or successfully linking shaders alone does not mean this test passed.
The release integration removes the hook, its invocation and declaration. The
release policy check rejects its environment variable in runtime sources.
Standalone kernel, dispatcher, texture, blend and integration tests remain.

Completed Windows checks include native AMD and Mesa kernel/service/texture/blend
tests, the integrated viewer pixel fixture on both drivers, and CPU fallback
startup with GL 4.3 and with interlock hidden in an isolated Mesa test process.
Texture checks cover mip refresh, sampler replacement, RGB, sRGB and BC3 copies.
The viewer fixture restores clear color and depth; its blue assertion background
must not affect the login screen.

Remaining acceptance work requires a logged-in scene: ribbons, HUD/world lighting,
texture changes, teleport/origin shifts, water/reflections, particle picking,
both OIT modes, and frame-time measurements under the particle stress script.
Linux compilation remains an independent platform check.

## In-world admission regression (2026-09-27)

The first logged-in test reported missing scripted effects and editing beams.
Live inspection found resident sources and active simulation, but no material
buffer, view preparation or draw target. The deferred-lighting pass narrowed its
render-type mask without preserving particle bits, so the new alpha consumer
never ran. The pass now preserves world/HUD particle visibility by intersection;
it does not force particles on when the caller disabled them.

The ordinary/OIT residual world stream also now supplies bounds-depth boundaries,
matching its actual group sort. Avatar/ensemble depths are used only by the merged
stream. Otherwise attachments can make interval boundaries nonmonotonic and force
fallback once the mask defect is removed.

`scripts/tests/test_particle_alpha_integration.py` compiles the production mask
methods, the actual post-deferred mask call, and the particle boundary expression.
It covers inherited on/off combinations and attachment depths that differ from
bounds depths. The offscreen pixel fixture alone did not cover these integration
boundaries. The user subsequently confirmed that particles display in-world with the rebuilt
fix. Broader appearance, lifecycle and performance acceptance remains open.
