# Restore upstream CPU rendering operations

Date: 2026-09-30. Branch: `codex/restore-cpu-rendering`.

The offload work recovered some performance on AMD/Mesa, but remained slower
than AMD native OpenGL. The user subsequently observed worse performance on
NVIDIA. These are hardware-specific observations, not evidence of a general
pipeline speedup or proof that every individual offload caused the regression.

The requested restoration covers CPU-to-GPU offload changes only. It preserves
the fork's added features and unrelated optimizations. The user explicitly
exempted the accelerated JPEG2000 decoder after confirming its benefits; the
existing OpenCL OpenJPEG package is retained.

## Comparison and restoration

Compared fork `810ca800b1` with Firestorm `82f51e700c`. Their merge base is
`a22e6e4ce6`; the only upstream commit missing from the fork is a translation
update. No upstream branch was merged, and no worktree was moved.

| Operation | Restored execution |
| --- | --- |
| Mesh positions, normals, tangents, UVs, weights, color, glow and indices | Upstream CPU vertex-buffer production |
| Mesh LOD, transform invalidation and per-view draw submission | Upstream CPU selection, rebuilds and direct draw ranges |
| Particle emission, simulation, spatial groups, sorting and picking | Upstream CPU simulation and consumers |
| Particle billboard/ribbon geometry and submission | Upstream CPU geometry and ordinary alpha draw lists |

Removed the additional compute dispatchers, resident pages and registrations,
indirect-command generation, offload shader variants and their debug settings.
Ordinary upstream GPU vertex shading, skinning and rasterization remain.

## Historical scope

The expanded review follows the current fork back to its August 30 renderer
selector groundwork (`09a759e304`), not just the September 29 mesh PR. The
history inventory contains 304 non-merge commits absent from upstream, including
duplicate development/master ports and upstream cherry-picks. Current source
differences and dependency definitions, rather than commit titles alone,
determine what still runs.

| Historical change | Disposition |
| --- | --- |
| `3117fa4485`, September 23: automatic OpenCL OpenJPEG packages | Preserve by explicit user direction; the J2C decoder provided benefits |
| `d95f48e433`, September 23: progressive compute mesh LOD, rigged/Animesh support | Removed with the mesh compute implementation; CPU LOD path restored |
| `aace7713ea`, `2a7f86b61b`, September 24: compute dependency wakeup and fallback refinement | Remove compute machinery; preserve independent streaming and skin-arrival correctness fixes |
| `a366de1234`, September 27, and follow-ups: resident GPU particles | Remove simulation, geometry, culling, sorting, picking and submission offloads |
| `e5f8f7aa21`, `ea3f0580b4`, `9fbe39073a`, `a2fd9f2387`: mesh pages, visibility, transforms and packet reuse | Remove resident GPU path and restore CPU geometry/direct submission |
| `e9e7cc176a`, September 29: buffer production and broader compatible batching | Remove compute production and resident batching across mesh categories |
| `182946d9f6`, August 31: procedural starfield | Preserve added feature |
| `24debc0afd`, `276f9018d7`, `50f303522a`, September 4: transparency modes/interleaving | Preserve added features and subsequent correctness fixes |
| Native Vulkan UI, Mesa/Zink selection, CPU texture workers, mesh downloads, GL Core allocation recovery | Preserve; outside the requested CPU-to-GPU restoration |

The OpenJPEG change was missed in the first pass because acceleration resides in
an external binary package. Its source at `C:/Dev/openjpeg` confirms automatic
OpenCL encode/decode selection. The initial attempt to restore the official CPU
package was withdrawn following the user's explicit instruction to retain the
beneficial decoder. `autobuild.xml` retains `2.5.4.35853057817` for Windows/Linux,
including the existing package's encoding behavior. No changes were made to that
external source repository. The proposed OpenCL JPEG/PNG design in `99ca06a8a3` is a
design document; current JPEG/PNG implementation does not contain that offload.

Removed orphaned particle capability probes and image-content revision tracking.
Retained generic upstream GL entry-point declarations and sampler-buffer type
recognition; neither dispatches work. Searches of current viewer C++ sources
find no remaining mesh/particle compute dispatch or indirect multidraw calls.

The earlier repository at `H:/VulkanStorm` was inspected read-only at archive
tip `04f36a4caa`. Its GHI/Primitive3D migrations and rejected synchronization/UI
experiments are not the current renderer implementation. In particular, current
vertex/image upload code does not contain the archive's FreeBSD-specific
`af3d7b76a2` synchronization changes, and the `bfd7388f62` fixed rebuild budget
is absent. Existing upstream occlusion code remains. The harvested native UI
is an added feature and remains. This is an audit of what reaches the current
candidate, not a restoration of every experimental branch in the archive.

## Preserved features

The mixed rendering files retain PPLL, depth peeling, interleaved alpha,
per-face alpha-order invalidation, glow texture-state restoration and emissive
transform correction. Mesh/skin arrival-order recovery remains. Renderer
selection, native Vulkan UI, audio, texture delivery, memory management and mesh
download changes are outside this restoration.

Offload-specific tests were retired with their implementations. The shared
offscreen GL bootstrap now lives in `scripts/perf/gl_test_context.py`, retaining
PPLL and SSR diagnostic support. Historical implementation and tests remain
available in Git at `810ca800b1`.

## Validation

Passed: alpha sort order, alpha/glow state, mesh/skin completion, texture
publication, mesh request priority, deferred mesh retries and avatar texture
retention. Release development-hook and development feature-configuration checks
also pass. A source audit verifies 15 core CPU-path files against upstream,
plus unchanged PPLL/depth-peeling shader helpers and 11 transparency/interleaving
methods against the pre-restoration fork.

After the expanded cleanup, texture publication/budget tests and release-policy
checks passed again. A temporary CPU OpenJPEG package passed lossless grayscale,
RGB and RGBA round trips, but was withdrawn from the candidate as directed;
that check does not qualify the retained accelerated package. This Windows
environment does not qualify the Linux build or runtime.

The existing offscreen PPLL fixture fails on NVIDIA 617.14 at its head-image
assertion. The same fixture fails against unchanged shader sources from
`810ca800b1`, in `fullbrightF.glsl`, cycle 1, capacity 8. This is not a passing
PPLL qualification; its cause remains unresolved and is outside the offload
restoration.

The Autobuild-configured RelWithDebInfo build and full runtime staging passed.
All 225 staged GLSL files match source; the named executable matches the linked
binary (SHA-256 `582442c5cf55a12a2a24681ec3f3b9c35c7a22b39599ecc5d408cefb7d9fadef`).
An isolated NVIDIA native-OpenGL startup with shader caching disabled exited
normally after 26.6 seconds (exit 0), without login. No installer was generated.
The staged accelerated `openjp2.dll` matches the retained installed package
(SHA-256 `e3c8b5dce72ba3c80403d61ed79edf7e23d668ce95af7cb763d9ce7fb7c1f301`).

Candidate: `build-vc170-64/newview/RelWithDebInfo/Vulkanstorm-RelWithDebInfo.exe`.

In-world correctness and matched performance comparison remain required; restoring upstream operations
does not establish a measured performance recovery. The SSR tiling and
NVIDIA/Zink missing-scene issues remain separate.
