# Enable available Linux surface extensions

Base: DiligentCore `bcb8b11eecd0899962c330b798ebe3e786b02bbb`, after
`acquire-layout-transition.patch`. The second overlay and intermediate/final
source hashes are recorded in `3p/vulkan-dependencies.json`; the `wsi1` suffix
identifies this package revision.

The Linux Lavapipe CI run reached device creation but Diligent rejected the
instance because `VK_KHR_wayland_surface` was absent. The package compiles XCB,
Xlib and Wayland support, and upstream unconditionally required all three at
instance creation. The CI intentionally uses X11; an unused Wayland extension
must not prevent offscreen rendering or supported XCB presentation.

Enable each compiled Linux surface extension only when advertised by the ICD.
Before creating a requested XCB, Xlib or Wayland surface, check that its extension
was enabled and report an explicit error if it was not. Application-supplied
required extensions remain mandatory. No alternate graphics backend, GL context,
validation suppression or skipped mandatory presentation test is introduced.

Qualification still requires native X11 presentation and deterministic rendering
on both Linux software ICDs. This overlay does not qualify Wayland presentation.
The dependency builder checks both patches, each intermediate postimage, and the
final combined source state before packaging the same library for viewer and test.
