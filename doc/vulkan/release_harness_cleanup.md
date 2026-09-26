# Release harness cleanup

## Master cleanup after modernization merge, 2026-09-26

Merge `be0ae32efc` inadvertently brought the development branch's runtime
harnesses back into master. This cleanup restores the pre-merge release paths
from `50871c432f` wherever the inherited changes were instrumentation only.
The historical validation below describes the earlier cleanup, not this build.

Removed again: synthetic Vulkan/OpenGL UI scenes, environment-driven capture
and swapchain readback, debug emission counters, focus/resize GPU probes, the
PPLL timer/counter sampler and its setting, image/geometry/source-mesh memory
sampling, diagnostic assertion stack traces, and the development Tracy option.
The release profiler configuration and existing viewer debug facilities are
restored. Standalone tools and research documents remain outside the runtime.

Preserved: OpenGL >= 4.3 Core contexts and capability validation, safe shared
upload-context fallback, buffered physics drawing, render-target allocation
recovery, build/staging fixes, and master's JPEG and CI publisher fixes. The
per-frame deferred-buffer deletion drain and shutdown flush are also retained:
these fix GPU resource retirement rather than instrumenting it.

`scripts/tests/check_release_hooks.py` rejects the known removed harnesses in
tracked runtime sources. CI runs it for master pushes and pull requests targeting
master. This is a bounded branch-policy check, not a substitute for reviewing
new development hooks. The unchanged development branch is a negative control.

Validation for this cleanup:

- Policy check passes on cleaned master and rejects 121 occurrences on the
  unchanged development branch.
- Every runtime patch in modernization commit `da921da754` still passes a
  reverse-application check, confirming those edits remain present.
- Production render-target and deferred-buffer retirement regression tests pass.
- All 48 offscreen PPLL cases pass on the local AMD RX 9070 XT driver. This
  existing harness uses a compatibility context; it is not a Core-profile or
  whole-viewer visual acceptance test.
- All eight CI publisher tests pass, and the workflow YAML parses.
- The Windows Release viewer compiled and linked; runtime assets/plugins were
  staged without generating an installer. The final executable contains none
  of the checked capture, UI-test, focus, OIT, and memory-probe markers, while
  `RenderVulkanSelfTest` and `TextureLoadFullRes` remain present.
- After the separate teleport-preparation correction, the final staged viewer
  passed a 20-second isolated-profile Zink startup/shutdown smoke test (exit 0).
  Neither this smoke test nor compilation establishes in-world visual parity.

## Earlier release cleanup

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
