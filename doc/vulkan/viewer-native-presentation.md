# Native presentation in the viewer: first implementation checkpoint

Source: `05907fc6e4058ce21c79685cf86511bb70196c9e` on `vkstorm-vulkan`.
Status: implemented development diagnostic. CI run
[37742422223](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37742422223)
passed all nine original cases on Windows SwiftShader, Linux SwiftShader and
Linux Lavapipe. The expanded [V3 completion record](viewer-ui-substrate.md#v3-completion-9-october-2026)
records normal viewer integration and its 68-case interface/resource matrix.
Dedicated CI run [37982489750](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37982489750) passed
all 68 expanded cases on each of the three platform/runtime combinations.
Connected UI/chat and world rendering remain subsequent stages.

The first CI run passed the Windows build and native viewer diagnostic. Its
Linux Lavapipe viewer build failed because the diagnostic used unqualified SDL
headers. The corrected includes use `SDL2/SDL.h` and `SDL2/SDL_syswm.h`, matching
the viewer's Autobuild include root and existing SDL2 window implementation.
Linux build and runtime acceptance subsequently passed in run 37742422223.

The next Linux build reached Diligent parsing and exposed inherited X11
`Bool`/`True`/`False` and `sys/mman.h` `MAP_TYPE` macro collisions. The diagnostic
now saves, undefines and restores those macros around its Diligent includes.
Its evidence label uses a preprocessor platform branch because `LL_WINDOWS`
is undefined on Linux. A local Clang syntax probe using the pinned SDK's Linux
interfaces passed with all four conflicting macros defined, verified Diligent
structure sizes and the readback mapping signature, and checked that the
platform macros were restored. This is a header compatibility check on Windows;
full Linux acceptance subsequently reached the runtime stage described below.

CI run [37683689649](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37683689649)
passed all viewer cases on Windows SwiftShader and Linux Lavapipe. Linux
SwiftShader passed eight cases but exited with X11 `BadAccess` during the
`after-swapchain` fault case, before writing lifecycle evidence. The archived
log identifies extension opcode 130, minor opcode 1; the pinned SwiftShader XCB
surface implementation queues MIT-SHM attachments asynchronously and removes
their local mappings during swapchain destruction. The evidence and source
indicate an attachment/retirement race for an unpresented swapchain.

Diagnostic cleanup now performs an X11 round trip before releasing swapchain
resources, then drains detach/free requests before destroying the owned SDL
window. Vulkan device idle alone does not synchronize the X server. The display
connection remains borrowed from SDL; cleanup does not close it independently.
Unexpected X11 errors remain fatal. Run 37742422223 passed Linux SwiftShader,
including the unchanged early teardown fault case.

## Implemented integration

The staged viewer executable now has a development-only native Vulkan mode.
`LLAppViewer::init` admits it before the normal GL/UI/world/worker bootstrap;
`frame` runs its event/presentation lifecycle; `cleanup` releases only its owned
resources. Platform entry points return its actual failure code and skip normal
GL/NVAPI startup and unowned platform cleanup. The diagnostic does not save user
settings or create/remove normal viewer run markers.

`LLWindowManager::GraphicsAPI::Vulkan` creates the real Win32 or SDL2 viewer
window; legacy `use_gl=false` without this explicit mode still means headless.
Win32 avoids the GL provider/context and queries actual native client geometry.
SDL2 creates a resizable X11 window without `SDL_WINDOW_OPENGL`, exports its
owned SDL handle for native extraction, and uses window dimensions rather than a
GL/software SDL surface for input-coordinate conversion. Unsupported Vulkan
fullscreen admission is rejected. Native GL context/swap/vsync entry points
throw before entering the GL API.

The diagnostic owns the pinned Diligent factory/device/context/swapchain, uses
queried adapter identities and prefers an available machine GPU over software.
The diagnostic requires Khronos validation. Software ICD isolation and
synchronization-validation settings are supplied by the CI launcher; they do
not change normal machine-driver discovery into a software-only product policy.

The source is [vsvkdiag.cpp](../../indra/newview/vsvkdiag.cpp).
Its window integration is reusable infrastructure; its scripted frame progression,
fault injections and development entry controls are qualification facilities.
The later usable UI/chat backend must add its actual resources and scheduling;
the diagnostic does not implement them.

## Cases and evidence

The positive viewer case presents three frames each at initial 320x240,
resized 640x360 and restored 320x240. It suspends three injected zero-extent
notifications and two minimized frame attempts without presenting. Win32 must
observe actual HWND minimization. SDL2/X11 records whether a window manager
honored the request; Xvfb alone cannot qualify an absent minimize event.

A copy/readback of the viewer's final clear backbuffer verifies RGB (16,32,48)
within one UNORM unit and alpha 255, accounting explicitly for RGBA/BGRA surface
storage. `viewer-clear.ppm` preserves that image. Uniform clear pixels do not
qualify UI/text orientation, blending or world drawing; those remain later tests.

Seven failure stages (`before-window`, `after-window`, `after-device`,
`after-swapchain`, `frame`, `shutdown`, `gl-trap`) and a wrong-clear-pixel case
prove that expected errors reach the process exit and CI runner while owned
cleanup completes. These are application fault injections, not claims of actual
driver device-loss or OS allocation-failure qualification.

Shutdown unbinds targets, waits for submitted work and pending X11 requests,
releases the swapchain and drains its X11 retirement requests,
destroys the native viewer window (joining its Win32 thread), then releases
device/context ownership. JSON lifecycle evidence and module mappings are
required. Missing validation, wrong selected device/library, crashes, timeouts,
unexpected diagnostics or missing/stale evidence fail the launcher. The native
window entry traps and GL-manager state assertions cover this bounded path;
they do not establish that the later full viewer callback closure is GL-free.

Local verification on 7 October 2026:

* Full Autobuild-backed `RelWithDebInfo` viewer build and complete runtime,
  plugin and asset staging passed without generating an installer.
* All nine viewer cases passed on the pinned local Windows SwiftShader runtime;
  that runtime retains its earlier prototype-recipe qualification limit.
* The positive case observed HWND minimization, recorded 9 presentations,
  3 zero-extent skips and 2 minimized skips, verified the clear readback, and
  completed shutdown with zero Diligent/validation errors.
* The runner verified actual mapped GHI/loader/ICD/layer hashes and matching
  selected device identity against prerequisite harness evidence.

Local artifacts are in `.tmp/viewer-vulkan-diagnostic-local`; they are not a
substitute for archived CI evidence. Linux runtime behavior is pending CI.

## Reproduction and CI

Use the existing Vulkan Autobuild packages and viewer feature configuration,
with `USE_DILIGENTCORE=ON`, `VS_VULKAN_DIAGNOSTICS=ON`, `PACKAGE=OFF` and
`CMAKE_BUILD_TYPE=RelWithDebInfo`. Diagnostics default OFF and are not compiled
into ordinary production builds. The option requires the pinned GHI and a
RelWithDebInfo development configuration.

The [dedicated workflow](../../.github/workflows/software_vulkan.yml) builds and
stages the viewer first, then launches the same executable from its staged
directory through [run_vulkan_viewer_diagnostic.py](../../scripts/run_vulkan_viewer_diagnostic.py).
The Windows case uses hosted-only temporary manifest registration; Linux uses
Xvfb. Release publishing and `latest` remain untouched.

Local Windows commands after configuring the existing build:

```text
cmake --build build-vulkan-ci-local --config RelWithDebInfo --parallel 3
python scripts/check_vulkan_ci_staging.py build-vulkan-ci-local
python scripts/run_vulkan_viewer_diagnostic.py --build-directory build-vulkan-ci-local --runtime .tmp/software-vulkan-runtime/runtime.json --harness-evidence .tmp/vulkan-presentation-item4-local/results.json --evidence .tmp/viewer-vulkan-diagnostic-local
```

The launcher supplies `VS_VULKAN_DIAGNOSTIC` as a fresh evidence directory and
`VS_VULKAN_DIAGNOSTIC_FAIL` only for isolated negative cases. Invoke through the
launcher for qualification: setting the diagnostic environment variable alone
does not supply validation configuration, pin verification or evidence review.

Next work is V3/V4 in the [viewer integration plan](viewer-integration-plan.md):
native UI resources and ordered drawing, then the connected UI/chat closure.
World elements follow incrementally after that checkpoint. This diagnostic
closes the initial native window/clear/presentation seam, not the entire
harness-to-viewer integration gap or full supported rendering parity.

## UI owner and packaged skins

The diagnostic now consumes the reusable `VSUIContext`, which reads the
selected skin/theme/language/font settings and initializes the existing overlay
resolution, widget defaults, color table, translations and native image/font
producers. Reusable native UI sources compile under `USE_DILIGENTCORE` rather
than only under the diagnostic option. The fixture remains development-only;
normal Vulkan application login is now wired as described below; connected-session acceptance remains open.

The runner now adds 24 skin/theme/language cases to its fifteen existing cases:
every packaged catalog selection, base default and German default XUI. Missing
catalog directories, incorrect selection identity, UI/input failures or missing
readbacks fail qualification. Local Windows passed all 39 cases under
SwiftShader with zero validation errors. This covers the fixture's admitted
controls, not full skin functionality, normal-session wiring or OS DPI/IME.
See [shared UI ownership and qualification limits](viewer-ui-substrate.md#shared-native-ui-owner-and-skin-checkpoint-8-october-2026).


The 9 October native viewer-window integration is recorded in
[the UI substrate checkpoint](viewer-ui-substrate.md#native-viewer-window-and-required-startup-owners-9-october-2026).
It adds reusable `LLViewerWindow` device/UI ownership and required startup
controls, exercised by an offline viewer startup probe. The subsequent
[normal application login integration](viewer-ui-substrate.md#normal-native-application-login-integration-9-october-2026)
wires ordinary initialization, frames, login callbacks, browser pixels and
shutdown, with a required CI launch outside diagnostic dispatch. Connected
region/chat admission remains gated at the V4 boundary. Neither startup result
establishes full V3 or connected-session acceptance.

## Renderer-aware Help / About — 10 October 2026

The shared About report follows the archived `H:\vulkanstorm` floater's
backend-neutral **Rendering API** / **Version** wording. It identifies the
running window's backend, rather than a preference awaiting restart. OpenGL
(including Mesa/Zink) reports its initialized GL renderer and version. Native
Vulkan reports cached properties of the Diligent-selected physical device,
without calling GL or guessing a Windows adapter. Its API version is the
physical device's supported Vulkan version; the vendor-specific driver value
is explicitly labeled raw. Dedicated GPU memory is reported when available;
shared system memory and GL texture budgets are not presented as Vulkan VRAM.
The obsolete vendor row is removed from existing localized system reports.

The existing corrected XUI, four tabs, and Copy to Clipboard remain shared.
Native admission includes the About floater, tab container, and its exact
private custom button type used by Starlight. Starlight's title-bar focus
highlight uses the native explicit-color rectangle path. Development staging
now includes the existing contributor extraction and Autobuild-generated
package/license information, with missing files rejected by the staging check.

Local Windows qualification passed all **68** staged viewer cases under the
pinned SwiftShader ICD and core/synchronization validation. Every packaged
startup skin/theme and German XUI opens About through actual Help-menu mouse
input, verifies the selected device/API report, switches all four tabs, and
copies the displayed report to the OS clipboard. Each startup positive case
retains 24 actual/expected pixel readbacks and `viewer-about.txt`; validation
errors and pixel mismatches were zero. The renderer-report regression suite
passed 20 tests, including initialized/uninitialized GL, Mesa/Zink, native
startup and post-window-teardown reporting. The staging suite passed 20 tests,
and the documentation suites passed 21.

The runnable `RelWithDebInfo` stage is `build-vcabout-local/newview/RelWithDebInfo`
in the Vulkan worktree. It was linked with the existing Autobuild-installed
libraries using the local configured build cache: changed compilation inputs
were redirected to this worktree, unchanged compiled inputs were checked for
byte identity, and temporary project overrides were restored afterwards.
Runtime/assets were staged without generating an installer. The prior user-test
stage was preserved. Evidence is in `.tmp/about-viewer-final/results.json`,
which records the pre-commit HEAD plus dirty worktree accurately; the tested
implementation is committed as `163ee9e2e8`. Its executable SHA-256 is
`c442dd872e45e5bbfe0f6dd8b52646442d8813461ca75c623d5ad7dfd458ae19`.
These results qualify local Windows software rendering; Linux CI and physical
vendor-driver execution remain separate evidence. This About update makes no
new claim of world-rendering parity or live-server authentication acceptance.
