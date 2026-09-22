# Release harness cleanup

Source reference: master `21ea999a9aa645215fd98a5e28d31919c0f324b1`.
Change class: cleanup; NV-00, NV-02, NV-03, NV-14, NV-17 apply.

The release line preserves viewer developer options, including Fast Timers,
memory leak simulation, full-resolution texture testing, debug settings,
`RenderVulkanSelfTest`, validation layers and ordinary operational logging.
Standalone tests and developer scripts remain outside the product runtime.

## Source contract before editing

1. OpenGL startup normally draws the viewer window. `gl_render_ui_test_scene`
   replaces that tree with synthetic primitives; `gl_capture_frame_once` reads
   the back buffer and writes a private raw format. Both are environment-only
   harnesses, not viewer screenshot or developer-menu features. Remove those
   alternate paths, retaining ordinary drawing and the settings-backed self-test.
2. Native startup normally prepares resources, begins the UI frame, walks the
   real widget tree, flushes and presents. `LLVKUITestScene`, session capture
   arming and `LLVKContext::readbackSwapchain` form a separate experiment. The
   readback helper has only the session harness caller; its last-presented image
   index has no other consumer. Remove that chain. Retain normal acquire,
   submit, present, upload and retirement behavior and normal clear color.
3. The smallest native design is the existing normal path with the alternate
   environment branches removed. Preserve clip intersections, widget preparation,
   viewer callback registration, text and image emission. Remove logging-only
   counters and their private accessors, preserving the vertex offset used to
   append real geometry. No shared rendering abstraction or GL oracle change is
   needed. Test-scene replacement is removed from both backends; ordinary GL
   drawing is the pinned reference.

Roots and obligations: `display_startup`, `LLVKSession::renderUIFrame`,
`LLVKUIRender::prepareFrame/renderFrame`, widget/folder chrome, text/image
emission and `LLVKUI2D::flushRun/end`. Registered viewer hooks remain live.
Color/alpha/coordinate contracts and production GPU ownership do not change.
Removing the readback also removes its separate device-idle/staging submission;
normal shutdown and presentation synchronization remain required.

Focused disproof checks: search all consumers before removing declarations;
verify settings/menu files unchanged; inspect the diff for lost normal draw or
preparation calls; compile/link the affected viewer and check the executable for
removed environment switches. A build and marker scan do not establish visual
parity or explain the reported 404 ms OpenGL frame. Runtime validation remains
unverified until performed with the user's viewer session.

## Validation, 2026-09-22

- Source search finds none of the 15 audited `VULKANSTORM_*` switches in the
  runtime source. Test-scene files and build entries are removed, along with
  capture declarations, callers and the readback-only image index.
- Compared the production emission/preparation/clip call sequences against
  master in the walker, widgets, folders, images, text and UI sink: unchanged.
  This is a bounded static check, not proof of runtime equivalence.
- Settings XML, developer menu, memory simulator, texture controls and Vulkan
  self-test implementation remain unchanged against master.
- Windows Release compilation and viewer linking passed with the existing
  Mesa/Zink configuration, Tracy disabled and packaging enabled. The resulting
  executable contains none of the 15 removed switches; `RenderVulkanSelfTest`,
  `TextureLoadFullRes` and `mem_leaking` remain present.
- Executable SHA-256:
  `78c9b4c0a1429bd103b753c1b897def42739a6f808e49572a9a5d0a7ddd63c9d`.
- Full default Release build, including installer packaging, exited 0:
  `cmake --build build-vc170-64 --config Release -- /m:6`.
  Installer: `Vulkanstorm-Release_AVX2-7-2-5-81941_Setup.exe` (unsigned local
  build). Build log: `build-vc170-64/compile-release-harness-cleanup.log`.
- No viewer session or GPU validation run was launched. Visual behavior and
  performance remain runtime-unverified; no claim is made that this cleanup
  fixes the reported OpenGL frame time.
