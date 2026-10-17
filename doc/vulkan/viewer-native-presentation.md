# Native presentation in the viewer: first implementation checkpoint

## All-skin tint and connected qualification, 10 October 2026

Source `fa6d71c213ebff00e3aad1640499d717ae2e231d` on `vkstorm-vulkan`
corrects the shared native tint conversion to match OpenGL's normalized U8
truncation. All eight packaged skin families and every catalogued theme are
covered; their color definitions are preserved. All **68 viewer diagnostic
cases** passed, including independent inactive-tab asset/readback checks.

All **26 connected cases** passed with **100 mandatory assertions per case**,
actual authenticated conference-start requests, core/synchronization validation,
no GL access and clean shutdown. Coverage is the 23 English skin/theme
selections, default German and Modern at 125%/150% scale. The ledger at
`.tmp/connected/parity/matrix-qualified.json` records the distinct tested images:
22 retained build45 passes, three build47 passes and the final Vintage build48
pass. [The all-skin audit](connected-ui-gap-audit.md#all-skin-tint-and-connected-qualification-10-october-2026)
records exact source reuse, preserved failures, executable-hash witnesses,
Vintage's image/optional-control fixes and measured finite replay deadlines.
This is not every skin/language combination or a new Linux/live-grid result.

The complete RelWithDebInfo stage is
`build-vcuiparity-final-local/newview/RelWithDebInfo`, without an installer.
Build48 executable SHA-256 is
`a9cc8510879ec9d861e675fe6526273956a6f31aef60c329230986f7e25eec84`.
Original user-test stages are preserved. All 76 dedicated source/compiler,
runner/staging tests and 22 documentation/source-pin tests passed. Qualification
uses isolated Windows SwiftShader; ordinary native viewer launches use the
machine's Vulkan driver.

## Historical shared connected UI checkpoint, 10 October 2026

Current implementation source is `99f1ade5ac9183cfcd546cf7138e5e9bdd34da20`
on `vkstorm-vulkan`. The fully staged Windows RelWithDebInfo viewer is at
`build-vcuiparity-local/newview/RelWithDebInfo`; the original user-test image at
`build-vcabout-local/newview/RelWithDebInfo` remains preserved. The build42
executable SHA-256 is `0d97da860640d37f7243e9714cf541e856a7048abdebf54bf75bc6c630fe61bb`;
its PDB SHA-256 is `9bec4b968e1367dc12342fc90761cd27631e35cdfadd1de3111e89efa1428ac9`.

The current integration uses the ordinary skinned navigation/favorites/status,
main menus, saved toolbars/Toybox, nearby-chat floater, conversations, People,
Contacts and inventory controllers. Context actions retain their shared CPU
account/protocol routes; scene-only radar/map/zoom/voice paths remain deferred.
Native input pause/resume, optional scene-menu closure, console/notification
completion, inbox freshness storage and account-scoped callback retirement now
use their actual production owners. Logout/relogin retires those account owners.
Contacts content has eight additional UI pixels of right inset in the default,
Modern and Vintage English layouts, inherited by localized overlays. This is a
shared XUI correction for both rendering backends.

Build40's Modern skin at 150% passed all 99 connected replay assertions; its
default and Modern base replays also passed. AnsaStorm failed the fixture's
opaque-click test because it accepted an unrelated popup. The fixture now
requires a fresh actual Url Popup and its exact clipboard result. Build41's
AnsaStorm replay passes all 99 assertions with that stricter check; the original
failure remains evidence. A subsequent AnsaStorm Paste failure has no proven
single cause. Windows clipboard reads now use bounded Unicode text and match the
write path's representation; extracted actual read/write-body tests preserve the
exact clipboard oracle. This corrects a concrete read/write asymmetry without
attributing the earlier failure to it conclusively. Build42's AnsaStorm replay
passes all 99 assertions. Build40/41 captures and failures remain preserved.
These are specific local Windows software-device results, not complete
renderer/UI acceptance.

The build42 matrix was stopped after nine completed passes to qualify the
subsequent shared tint fix; it does not supply full-matrix acceptance.
All 67 dedicated source/compiler/session-runner/staging tests and all
22 documentation/source-pin tests passed. Final qualification remains pending
until the full skin and viewer matrices finish with their required artifacts,
validation and teardown evidence. No physical GPU or new Linux/live-grid result
is implied by this checkpoint.

Source: `05907fc6e4058ce21c79685cf86511bb70196c9e` on `vkstorm-vulkan`.
Status: implemented development diagnostic. CI run
[37742422223](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37742422223)
passed all nine original cases on Windows SwiftShader, Linux SwiftShader and
Linux Lavapipe. The expanded [V3 completion record](viewer-ui-substrate.md#v3-completion-9-october-2026)
records normal viewer integration and its 68-case interface/resource matrix.
Dedicated CI run [37982489750](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37982489750) passed
all 68 expanded cases on each of the three platform/runtime combinations.
Connected UI/chat was a subsequent stage at that historical checkpoint; the current integration is recorded above. World rendering remains deferred.

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

## Renderer-aware Help / About â€” 10 October 2026

The shared About report follows the archived `H:\vulkanstorm` floater's
backend-neutral **Rendering API** / **Version** wording. It identifies the
running window's backend, rather than a preference awaiting restart. OpenGL
(including Mesa/Zink) reports its initialized GL renderer and version. Native
Vulkan reports cached properties of the Diligent-selected physical device,
without calling GL or guessing a Windows adapter. Its API version is the
physical device's supported Vulkan version. Driver reporting now uses the
installed Windows driver package version matched to that physical device's
Vulkan LUID through DXGI. If that lookup is unavailable, and on Linux, it uses
the driver's supplied release/build text, then the documented vendor-specific
packed-version decoding. It never reports the Vulkan API version as the driver
version. The DXGI lookup creates no graphics device or GL context.
[Microsoft driver-version query](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiadapter-checkinterfacesupport)
and [Vulkan driver properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceDriverProperties.html)
describe these sources. Extracted production fixtures cover exact adapter
matching, resource retirement, missing metadata and version decoding on both
platform paths; live driver output remains a separate runtime check.
Device-local GPU memory is reported when available;
separate host heaps are not presented as Vulkan VRAM. The VRAM budget row uses
the shared saved-setting report on both backends: the configured MB limit, or
localized Unlimited. That row describes the viewer setting, not driver-reported
free memory or a Vulkan heap's changing allocation budget.
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


### Background UI qualification (11 October 2026)

All Windows Vulkan test executables are now launched by
`scripts/vulkan_viewer_process.py` on a unique private window station and
never-activated desktop. This isolates windows, focus and clipboard from the
working desktop. A kill-on-close Job Object contains descendants; bounded
execution captures combined output and kills the process tree on timeout.
Setup failure is decisive: there is no visible-desktop fallback. Test result
JSON records the window station and desktop identifiers. Diagnostics no longer
move or query the physical pointer to qualify injected input.

Linux CI continues to launch UI cases under Xvfb. The dedicated Windows CI
job first verifies actual isolation, unchanged active desktop/focus/clipboard,
output capture and timeout cleanup. The local Windows account cannot create a
private window station (WinError 5), so local UI execution stops before creating
a viewer; the final isolated runtime matrix requires CI evidence.

The native hover fix shares OpenGL's hover-enter/leave controller, including
capture and stationary-pointer layout changes, and delivers pending Win32
motion before window leave without replaying it afterward. Skin assets and
button colors remain unchanged. Earlier source `5edef0916ac20ca6f7bb3191eb72abd340e05f47`
proved the hover-lifecycle assertion for all 24 startup skin selections. That
run did not pass its entire diagnostic matrix; it is not qualification of the
final isolated executable. Previous connected-session evidence is retained
with its original source/image identity. Final runtime and live-account
acceptance must remain separate from source and build checks.


### Selected-device memory reporting (11 October 2026)

About queries the selected physical device's Vulkan memory properties and
reports the sum of device-local heap capacities in MiB. The previous
Diligent `LocalMemory` field excludes CPU-visible local memory and could be
zero when GPU memory was classified as unified. Counting heaps includes
CPU-visible device-local memory (Resizable BAR/UMA), excludes separate host
heaps, and avoids counting aliased memory types more than once. This is
driver-reported capacity, not free memory or a viewer allocation budget.
Unknown capacity remains unknown rather than borrowing OpenGL state.
Compiler fixtures cover selected-device identity, multiple local heaps,
host-only exclusion, zero capacity and sizes beyond 32-bit bytes. Physical
GPU display evidence remains separate.

### Connected script notifications (11 October 2026)

A live post-login crash exposed inconsistent admission: `script_floater` was
an allowed registration name, but its `LLScriptFloater` widget class was
rejected. The active shared `show()` implementation dereferenced the failed
construction result. Native admission now includes that real floater and its
`LLToastScriptTextbox` form. Shared XUI, skinning, docking, dialog stacking,
notification responses and inventory-offer handling remain in use. Failed
construction and failed stack lookup are checked before dereferencing.

Console compiler fixtures exercise the actual widget policy and construction
failure branch. The connected replay additionally opens real ScriptDialog and
ScriptDialogGroup/textbox notifications, submits their actual controls, and
requires decoded ScriptDialogReply packets with the connected identities,
object, channel, button index and text. Missing control or packet evidence
fails qualification. This extends CI coverage; passing console/build checks
does not establish live-account or isolated UI runtime acceptance.

### Shared budget and chat editing (11 October 2026)

About no longer clears the native renderer's VRAM budget row after formatting
the shared saved settings. Both backends report the same configured MB limit
or localized Unlimited. A compiler fixture executes that actual reporting
block for both backends, including an explicit zero limit.

The shared chat entry now preserves the text editor's successful key-handling
result. Previously Backspace edited the text but returned unhandled, allowing
native focus/root routing to delete again or sound an error on the newly empty
field. Impossible deletion still sounds once; Return continues to bubble to
the enclosing chat submit controller. A compiler fixture exercises the actual
editor branch, chat override and native routing, including selection, word
deletion, read-only state and input history, without opening a viewer window.

### Native connected playback and statistics (11 October 2026)

The text UI receives authenticated current-parcel metadata and exposes the
shared music and parcel-media status controls. Music uses the existing
streaming audio fade/volume engine. Parcel media opens the existing 2D native
LLMediaCtrl frontend and selects CEF/VLC using the parcel MIME type. First-use
notices, filter lists/prompts, autoplay settings and explicit stop remain in
use. Region/account replacement retires playback and pending approvals.
Accepted URLs are HTTP(S); other media transport schemes, media on world-object
surfaces, and live codec/output qualification remain outside this evidence.

Native frame/network measurements feed the shared Statistics, Lag Meter and
status FPS controls. Simulator measurements come from admitted current-host
SimStats messages. Chart history uses native primitives, and absent network
or simulator measurements are reported as unavailable. The client lag cause
describes native UI frame time instead of blaming disabled world settings.
Console fixtures and decisive connected replay assertions cover these paths;
the rebuilt executable still requires isolated CI and live playback/UI
qualification. This does not establish complete connected UI parity.

### Tab-content button edges (11 October 2026)

The tab container applied a three-pixel tab-label inset while drawing its
entire content panel. Child panels reach closer to the container boundary,
so a right-aligned button's expanded focus border could be cut off while
its inward corner pixels remained visible. Content now uses the container
bounds and retains ancestor clipping. This shared fix applies to both
renderers and every skin; it does not enlarge button hit targets or alter
skin assets. The extracted production clip fixture covers several panel
widths and UI scales, retains ancestor restrictions and fails against the
previous implementation. Graphical confirmation remains separate.

## Connected account UI survey — 11 October 2026

The survey compares native admission with real menu/toolbar/app-link handlers,
panel factories, capability consumers and account retirement. Controllers that
require a world region must use the native account transport or remain
explicitly unavailable; enabled-control counts do not establish integration.

Marketplace seller services use the account's DirectDelivery capability and
reject HTTP/retry replies from retired accounts. Dataset, pending work and
signal slots reset together while the singleton remains alive for suspended
coroutines. Listings callbacks retain weak view handles. Inventory link
replacement admits ordinary inventory links with account-guarded confirmations
and completions. Current-outfit and saved-outfit operations remain tied to
appearance ownership and unavailable in the native text stage. Compiled
production regressions verify capability resolution, reset, seven HTTP resume
boundaries, retry suspension, dead views and retired callbacks. Seller-server
transactions still require runtime qualification with an appropriate account.

The Firestorm bridge is a real scripted attachment. Its controller requires
self-avatar attachment-point ownership, object UUID/local ID and inventory-item
mapping, task-inventory transfer, actual script upload and authenticated
attachment handshake. The world progress branch now admits avatar, animation
and object updates and constructs native CPU avatar owners. It still lacks the
shared COF/wearable, attachment queue and task-inventory/script ownership needed
by the bridge. Full bridge operation therefore remains an
implementation gap. A CPU transport adapter must establish those owners before
bridge callbacks can be enabled; arbitrary chat/HTTP cannot qualify a bridge.

This survey and console qualification do not establish full connected UI
parity. Runtime evidence and remaining bridge, appearance/world and voice
service integrations must be recorded separately.

The expanded world requirement and reference pass/ownership survey are tracked
in [full-render-chain-survey.md](full-render-chain-survey.md). Text-stage exclusions
above are historical qualification limits; they are required integration work
under full OpenGL parity, not permanent feature exclusions.


### Startup and window-close correction (17 October 2026)

Region connection is not startup completion. Native startup now waits for the
inventory system-folder callbacks, restores active gestures through the shared
`LLStartUp::initActiveGestures` routine, initializes account favorites, region
group titles and camera presets, requests lost items and reports window shape.
Texture/cache workers and native appearance continue running beneath progress
through the saved `PrecachingDelay`. Clothing waits for complete COF and
resolved wearable publication, with the reference ten-second warning path;
a timeout is recorded separately from readiness. Connected replay must observe
inventory, miscellaneous initialization, precache and clothing stages before
accepting the connected UI.

The Win32 close contract exposed a separate logout defect: returning true from
`LLViewerWindow::handleCloseRequest` immediately calls `handleQuit`, which
forces exit. Native windows now use the shared confirmation/graceful quit route
and false return. The compiled regression executes the actual callback against
the Win32 caller's forced-quit branch. Headless return-to-login evidence alone
does not qualify a visible window-close logout screen.

These corrections do not establish complete startup or connected UI parity.
First-login library-outfit creation, final snapshot/restore, native bridge
attachment creation and the reported map/Places/snapshot/Quick Preferences/
profile-image integrations still need production integration and qualification.
The existing bridge scene-owner guard remains closed; calling its initialization
routine is not evidence of a functioning bridge.
