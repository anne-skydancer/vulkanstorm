# vkstorm-vulkan-direct: a Vulkan renderer written directly against Vulkan

This line builds a Vulkan renderer for the viewer without a graphics
abstraction library. It sits beside `vkstorm-vulkan-rhi`, which builds one on
DiligentCore. Both lines branch from `vkstorm-vulkan`, the shared base, and are
qualified against the same reference: the OpenGL viewer.

Status (2026-10-28): plan only. Nothing here is implemented, built or measured.

## Principle

- **The logic stays as the OpenGL viewer has it.** What the pipeline does and
  in which order: the render passes, draw pools, culling, material and shader
  selection, the deferred and forward paths. That logic is transcribed, and
  where it is OpenGL-free it is shared with the OpenGL viewer instead of
  copied (see "Shared base").
- **The rendering is written for Vulkan.** Explicit passes with barriers
  derived from the known pass order, buffers and descriptors managed up front,
  pipelines or shader objects in place of the OpenGL state machine. It does
  not emulate OpenGL and is not a translation layer like Zink.
- **The OpenGL viewer does not change.** It is the reference. Its results stay
  identical; every extraction of OpenGL-free logic into shared code carries an
  equivalence test against a frozen copy of the original. Vulkan-only code is
  compiled only when the Vulkan path is enabled.

## Relation to the design study

`doc/vulkan/` is the source audit and design study of the OpenGL renderer: the
frame and resource graphs, the 73 contract records, the shader inventory (225
GLSL files, 690 registration and variant rows) and the GL state and lifetime
contracts. That audit applies to any backend and is this line's map of what
must be reproduced.

The study also concluded that a bespoke Vulkan backend was beyond the
project's capacity and selected DiligentCore. For this line, the owner has
superseded that conclusion: the `-rhi` line keeps the DiligentCore approach,
and this line tests the direct approach so the two can be compared on the
same qualification harness.

## Technical baseline

| Area | Choice |
|---|---|
| Platforms | Windows and Linux; NVIDIA, AMD and Intel. Software Vulkan (lavapipe, SwiftShader) in CI only, for qualification. |
| API | Vulkan 1.3: dynamic rendering, `synchronization2`, timeline semaphores. `VK_EXT_extended_dynamic_state3` and `VK_EXT_shader_object` where present, with ordinary pipelines and dynamic state as the fallback. |
| Loader | [volk](https://github.com/zeux/volk): loads Vulkan entry points straight from the driver, no link-time dependency on the Vulkan loader library. |
| Memory | [Vulkan Memory Allocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator): device memory suballocation, memory type selection, budgets. |
| Shaders | The viewer's GLSL compiled to SPIR-V with glslang; reflection for descriptor layouts. The GLSL changes Vulkan needs (explicit bindings, uniform blocks) are made per shader family. |
| Validation | Vulkan validation layers clean in every CI run (necessary, not proof of parity). |

All three libraries are vendor-neutral and under permissive licences (volk and VMA MIT, glslang BSD and Apache-2.0 terms); each is
pinned as an autobuild package like the viewer's other dependencies.

## Where the Vulkan path meets the viewer

The viewer's render classes mark where the OpenGL logic hands off to
rendering: `LLRender`/`gGL`, `LLVertexBuffer`, `LLImageGL`, `LLRenderTarget`,
`LLGLSLShader`/`LLShaderMgr`, the draw pools and `LLPipeline`'s passes. The
Vulkan path replaces what happens behind each hand-off; it does not
reimplement those classes' OpenGL-shaped interfaces. For example, immediate
mode drawing through `gGL` (`gGL.begin/end`; `gGL.` appears in 93 `newview` files) becomes
explicit batched vertex streams.

What the OpenGL driver does implicitly, each Vulkan pass states explicitly:
streaming vertex data, mid-frame buffer and texture uploads, hazards between
render-to-texture passes, readbacks, mipmap generation and per-draw uniforms.

## Build order

Each step is qualified before the next starts.

1. Device, swapchain and frame loop; volk and VMA packages; validation in CI.
2. Upload, streaming and synchronisation: staging, ring buffers, the pass
   ordering and its barriers, readback.
3. Shader path: GLSL to SPIR-V for the first shader family, reflection,
   descriptor layouts.
4. UI: the 2D drawing behind `LLRender`, so the login screen and floaters
   render under Vulkan.
5. World, pass by pass in the OpenGL pipeline's order: terrain, sky, opaque
   geometry, alpha, avatars, water, then the deferred lighting, shadows,
   reflection probes and post-processing.

## Qualification

- **Results:** each transcribed pass is compared with the OpenGL viewer on the
  same scene and inputs, with the tolerance stated per pass.
- **Zink as a yardstick:** the viewer already runs on Vulkan through Mesa Zink,
  a generic OpenGL-to-Vulkan translation. The direct path should match its
  results and beat its performance; otherwise it is not worth carrying.
- **Against `-rhi`:** the same scenes and measurements on both lines, so code
  size, effort, results and performance can be compared.

## Shared base

`vkstorm-vulkan` holds the work both Vulkan lines need: OpenGL-free logic
extracted from the renderer with its equivalence tests, the qualification
harness and the parity ledger. Base lines are never merged into each other,
so shared work lands on `vkstorm-vulkan`, `vkstorm-vulkan-rhi` and this line
through parallel PRs.

## Open questions

- The CMake option and compile definition for this path (the `-rhi` line uses
  `USE_DILIGENTCORE` and `VS_NATIVE_VULKAN`).
- How the Vulkan path is selected at runtime, and the fallback to OpenGL when
  it cannot start.
- The minimum driver versions implied by the Vulkan 1.3 baseline on each
  vendor.
