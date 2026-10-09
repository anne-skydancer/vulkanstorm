# V3: viewer UI resource and packet substrate

Source: `e7e9e6e6aa68ab125b4af41a2955259aa0d2ba4c` on `vkstorm-vulkan`.
Status: V3 implementation complete; expanded platform qualification in progress.
The ordinary viewer owns native login, menus, progress, required dialogs and
skin-aware plain nearby-chat controls. V4 connected transport/session admission
and subsequent world rendering remain separate roadmap stages.

The dated implementation sections below preserve historical evidence; the
[V3 completion record](#v3-completion-9-october-2026) supersedes their open-item
statements.

## Implemented ownership and drawing

[VSUIRenderer](../../indra/newview/vsuirenderer.h) owns Diligent texture
generations, shaders, pipelines, vertex uploads and bindings. Packets retain
their exact immutable texture generation; replacing a producer handle cannot
change already queued draws. A Diligent completion fence controls retirement,
without a per-draw idle wait. The lifecycle owner waits for idle at teardown.
There is one vertex buffer upload per ordered packet list, with six vertices
per quad; draws preserve paint order. Lazy atlas packing is implemented;
draw-call coalescing remains future work.

Each packet supplies logical bounds, UV bounds, tint, a half-open clip, affine
transform, straight or premultiplied alpha blending, and nearest or linear
sampling. The coordinate origin is top-left. A positive DPI scale transforms
logical coordinates into physical pixels before NDC/scissor conversion. Clips
round outward and clamp to the render target. Premultiplied packets must supply
premultiplied texture RGB and tint RGB. SDR RGBA8/BGRA8 UNORM targets are admitted;
other target formats fail explicitly. No ambient GL state is consulted.

[VSUIFont](../../indra/newview/vsuifont.h) uses the viewer's Autobuild FreeType
library and caller-selected font bytes to produce CPU coverage, bearings,
advance and glyph index. It owns no GPU objects and imports no GL texture/font
cache. Unicode scalar values are checked; absent characters use `.notdef`.
GPU publication is a separate renderer operation. This does not yet implement
the viewer's complete font fallback, shaping, style or atlas-cache policy.

The new files are compiled only with `VS_VULKAN_DIAGNOSTICS`; the production
baseline workflow and renderer are unchanged. The existing corrected XUI files
are unchanged. This is not a new widget toolkit or an XUI replacement.

## Viewer execution checks

The existing nine WSI cases are retained. The runner adds `ui-positive`,
`bad-ui` and `ui-orientation`. UI fixtures draw into the actual restored
320 by 240 viewer swapchain before presentation, at explicit 1x and 2x scales.
They exercise asymmetric texture orientation, clipping, paint order, affine
translation, both alpha modes, both sampler pipelines, ASCII/Greek/Cyrillic
glyph coverage, replacement glyph sizes, immutable texture replacement, and
generation retention before GPU completion and retirement after its fence.
Invalid upload bytes/extents and invalid glyph requests must be rejected.

An independent CPU compositing oracle compares every readback pixel, including
opaque output alpha. RGB tolerance is two bytes for fixed-point blend rounding.
Font coverage comes from the CPU rasterizer; these checks establish its GPU
publication and positioning, not an independent reference for FreeType's
rasterization. Deliberate wrong expected pixels and flipped UVs must produce
the expected failure with clean teardown and no Vulkan validation errors.

Artifacts include actual and expected `viewer-ui-1x*.ppm` and
`viewer-ui-2x*.ppm`, lifecycle JSON, validation/loader logs, loaded-library
hashes, executable revision/hash and the staged DejaVuSans font hash. The runner
records `ui_substrate_qualified` separately from `ui_chat_qualified=false` and
`world_qualified=false`. Scale changes are explicit test inputs; this does not
qualify OS DPI events, keyboard focus or IME.

## V3 completion, 9 October 2026

The production viewer now supplies the entire V3 interface/resource scope:

- Ordinary native application login retains the existing authentication,
  credentials, browser pixels, retry and required-dialog controllers. Login
  and edit menus run under the closed native widget admission policy.
- `VSPlainChat` is a viewer-owned panel using the selected skin's text-editor
  and line-editor defaults. It preserves Unicode input and history across DPI
  recreation, supports scrolling and preediting, bounds retained history and
  clears submitted text only after an attached sender accepts it. The V4
  connected-session owner must attach transport and show the panel.
- Required modal alerts, critical messages, TOS consent/cancellation and both
  MFA forms use their real viewer controllers and response callbacks. Native
  agreement dialogs adapt to small viewports so their action controls remain
  reachable. Corrected XUI and skin overlays are preserved.
- Optional restored floaters and direct browser factories are denied before
  construction. Explicit actions show translated unsupported status; blocked
  browser popup requests are closed through the existing media controller.
- Native system DPI is combined with the saved UI scale. Win32 DPI messages
  preserve the supplied scale across resize; SDL display changes refresh it.
  SDL composition/commit events use the production editor's preeditor. Normal
  frames maintain focus and retire deferred UI owners before input handling.
- Native diagonal strokes, filled/outlined triangles and editor spellcheck
  marks emit packets. Fractional-DPI scissor conversion preserves physical
  clipping edges. The independent CPU coverage oracle uses the device's
  advertised `subPixelPrecisionBits`; interpolation and strict color checks
  remain separate from coverage rounding.
- A general fence deliberately blocks GPU completion while producer references
  are discarded. Resource generations must remain alive through retirement
  attempts, then expire after completion. Teardown waits for outstanding work.

The dedicated workflow requires 68 viewer cases. In addition to WSI, resource,
input and fault-injection cases, the full required interface/action sequence
runs under all 24 packaged skin/theme/language selections. Each positive startup
case produces 18 actual/expected pixel pairs over 17 frames, including an extra
scrolled transcript readback, required dialogs, resize/DPI and restoration.
The runner checks selection identity, artifacts, loaded library hashes,
validation diagnostics and ordered cleanup. Its ordinary 30-second login
launch has diagnostic dispatch disabled and requires actual CEF pixel
publication and clean shutdown.

Local Windows SwiftShader qualification passed all 68 cases from the complete
`RelWithDebInfo` stage, with zero validation errors and zero unexplained GL
trap entries. Evidence: `.tmp/v3-completion-final/results.json` and its logs,
selection records and readbacks. Expanded Linux qualification is pending the
dedicated CI; source implementation does not establish an untested platform.

The prior ordinary-login CI
[37925511896](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37925511896)
passed all 44 earlier cases on Windows SwiftShader, Linux SwiftShader and Linux
Lavapipe, including actual browser pixels and clean normal shutdown. Real-server authentication and connected nearby-chat delivery
belong to V4; world parity remains later work. Physical multi-monitor DPI and
an installed OS IME service are unavailable in these software runners. The
native event adapters and direct preeditor are exercised without claiming
those unavailable services. Live skin switching and additional optional
controls are outside this V3 acceptance matrix.

## Expanded resource and producer integration

`VSUIResources` publishes decoded `LLImageRaw` pixels through native `LLUIImage`
facades without constructing `LLTexture` or `LLImageGL`. Bottom-row-first CPU
storage becomes canonical top-left RGBA. One to four component images are
supported. The facade emits ordered normal, alpha-mask and nine-slice packets;
attempts to obtain a GL texture or draw a native UI image in 3D fail explicitly.
GPU replacement copies the old texture, uploads only the changed rectangle and
retains both generations until the completion fence.

`VSUIFontCache` packs lazily rasterized glyphs into pages with transparent
gutters. Repeated glyph lookup does not rasterize or upload again. Reset removes
producer entries while queued draws retain their immutable GPU generations.

`VSUIFontBridge` connects the existing `LLFontGL` geometry publisher to packets.
New font caches capture an explicit CPU-only backing policy; FreeType writes
their raw atlases without allocating or updating GL images. The existing font
layout code supplies kerning, alignment, fallback and style geometry. Native
publication rejects a GL-backed cache or nonzero world depth. Atlas generation
changes compare CPU pixels against the published snapshot and upload the
bounding changed region. Font producer teardown removes its page registry;
queued/submitted packets still own their generations. The resources must outlive
the bridge, and its font faces/registry must be destroyed before the bridge
releases an owned FreeType manager. Styles and fallback paths need further
fixture coverage; preserving their producer code is not runtime qualification
of every path.

`VSUIAdmission` supplies exclusive scoped widget, floater and panel-factory
policies. Generic builders, direct floater builders, custom panel builders,
specialized panel callbacks and restored floater admission check policy before
construction. Denied floater show requests also stop before validation callbacks.
With no native owner, the original GL admission behavior is retained. A fixture-only allowlist is implemented; the complete required
native panel/floater policy remains open.

The expanded viewer fixture exercises real TGA encode/decode and image facade
draws, partial replacement, masks and nine-slice scaling, lazy atlas reuse,
rollover/reset and the existing font producer's ASCII/Greek/Cyrillic geometry
and subsequent glyph upload. It also invokes denied registered panel and direct
floater factories. Device-free tests execute the actual restored-floater loop
with counters, proving denial before settings reads/callbacks and safe handling
of a failed allowed construction. The launcher requires explicit evidence for
the facade, atlas, font producer and admission checks. This remains a diagnostic
fixture; actual login/chat XUI, focus, scrolling and OS IME/DPI events remain open.

## Initial-slice local verification, 8 October 2026

The full Windows RelWithDebInfo viewer build and complete staging check passed.
All twelve viewer cases passed using the pinned local SwiftShader runtime with
zero validation errors, two UI readbacks and complete teardown. Actual/expected
readback images were inspected. The local evidence records the executable and
font hashes and the modified-worktree state; it is not a clean CI qualification.
A Clang syntax check against the Autobuild Diligent Linux interfaces passed,
including the four conflicting platform macros; this is not a Linux runtime
check. All 70 source-audit, runner, staging and related regression tests passed.
Cross-platform qualification of these new cases remains pending the dedicated
Vulkan workflow.

To exercise the opt-in fixture manually, add `VS_VULKAN_DIAGNOSTIC_UI=1` to an
existing native diagnostic launch using `VS_VULKAN_DIAGNOSTIC=<evidence directory>`.
The CI runner supplies this setting automatically for its three UI cases.

## Renderer selector and normal-session admission

The graphics preferences expose OpenGL, Mesa/Zink and Vulkan (in development).
The Vulkan option preserves its backend identity; selecting it currently shows
an availability explanation and restores the active selection, without saving
a replacement renderer or requesting shutdown. The restart callback independently
checks availability before saving. Mesa/Zink remains an OpenGL provider for AMD
drivers affected by OpenGL ICD regressions, not a native Vulkan fallback.

A manually persisted Vulkan selection stops normal startup after configuration
and before GL provider selection. `initWindow` also refuses the Vulkan-to-GL
route, and its caller checks its result. The native diagnostic launch remains
independent; compiling its Diligent path does not qualify a usable UI/chat session.
The shared `VSRenderBackend` policy can admit Vulkan sessions only once that
integration is implemented.

The Windows RelWithDebInfo build/staging and twelve native cases passed after
this change. All 73 regression tests passed, including compilation/execution of
the actual preference callbacks with device-free fixtures: peer identity, Vulkan
rejection, supported restart commit, cancel restoration, and early startup
guard ordering. These checks do not claim an interactive native XUI session.

## Expanded-slice verification, 8 October 2026

The full Windows RelWithDebInfo build and staging check passed. All twelve
viewer diagnostic cases passed on the pinned SwiftShader runtime, including
facade/atlas/font/admission evidence, both pixel readbacks, both intentional UI
pixel failures, zero validation errors and complete teardown. The 1x/2x images
were visually inspected. Local results record executable/font hashes and a
modified documentation worktree; this is not cross-platform CI qualification.

At this earlier checkpoint, all 75 related regression tests passed after the catalog refresh. Discovery
reconciles 28,338 witnesses in 1,741 files; the boundary manifest hashes 73
source files. A renderer syntax probe against the Autobuild Linux Diligent
interfaces passed (with a host `_countof` macro warning); it does not substitute
for a Linux build or runtime test. The existing dedicated workflow executes the
expanded cases without changing production CI, release publication or `latest`.

At that earlier checkpoint, required XUI construction/traversal, native image-provider asset lookup and
primitive/transform/nested-clip adapters remain open, along with focus, editing,
scrolling, platform DPI and IME qualification. Full V3 acceptance is not claimed.

## Existing XUI integration checkpoint, 8 October 2026

`VSUIDrawBridge` owns native rectangle, border, matrix and nested clip adapters
and the existing font producer. Default UI GL state scopes are bypassed only
inside this explicit owner; packets supply the replacement raster state.
Button/scrollbar additive and alpha-modulated additive glows have explicit native
blend pipelines. Legacy GL behavior remains available outside the owner.

`VSUIImageProvider` reads the existing skin texture declarations and overlays,
decodes their local assets on the CPU and supplies native facades. Clip and
inner/outer scale declarations are preserved, including collapsed/reversed
center UV ranges. Missing/invalid assets fail; remote UUID images are excluded
from this local provider. No GL image or texture producer is constructed.

`VSUIFixture` constructs the unchanged corrected `panel_progress_mini.xml`,
a real line editor and a read-only plain transcript with its real scroller. Its
closed type set owns these controls and their internal views/buttons/borders.
The fixture owns settings, translations, colors, font registry, event recorder
and UI singleton. Widget-factory defaults are released before borrowed fonts
and native image assets; partial construction takes the same cleanup route.

Checks cover Unicode/backspace, Tab focus traversal, transcript scrolling and
preedit installation/cancellation. Queued Win32 or SDL2 character/key/focus
events must reach the UI owner. These are synthetic native events; OS keyboard
layout, actual IME composition and platform DPI transitions remain unqualified.

Actual swapchain readback is compared with a CPU triangle/texture/blend oracle
(`viewer-xui*.ppm`). RGB tolerance is three bytes for interpolation/filter/blend
rounding; opaque output alpha is exact. The oracle has separate known pixel
cases for orientation, clips, alpha/additive blending, DPI and shared triangle
edges. It verifies GPU execution of widget packets, not an independent reference
for every XUI layout. Required control names, editing/focus/scroll state and
known additive pixels are checked separately. The earlier independent fixed
pattern and font publication oracles remain in place.

The full Windows RelWithDebInfo build and complete staging passed. All fourteen
viewer cases passed on pinned SwiftShader, including `bad-xui` and
`ui-construction`, with zero Vulkan validation errors and zero XUI pixel
mismatches. The XUI image was inspected. Local evidence remains separate from
cross-platform CI and full V3 acceptance. Login/status/required alerts and
agreements, additional control states, OS DPI/IME and full G-UI/G-RESOURCE
closure remain open. Normal Vulkan sessions stay unavailable until their
integration is qualified; connected transport remains V4.

The refreshed source accounting passes with 28,410 witnesses in 1,747 files
and 90 boundary source hashes. All 76 related regression tests pass.

## Native pointer input and geometry guard checkpoint, 8 October 2026

The viewer window callbacks now route mouse-down/up, hover and vertical wheel
events to the existing admitted widgets. Captured input is translated into the
captor's local coordinates; otherwise it traverses the root. Focus loss cancels
capture. A queued native click must focus the real editor and release capture;
a queued wheel event must change the real transcript viewport after layout.
Win32 injection establishes cached cursor coordinates before button-down and
supplies screen coordinates for wheel messages. SDL2 uses its native mouse and
wheel events. Callback owners are cleared before widget teardown.

`LLRender::begin` rejects GL geometry while the native drawing owner is active,
before changing GL renderer state. The isolated `ui-gl-trap` case proves process
failure and clean teardown. A device-free test also verifies that ordinary GL
begin/flush behavior is preserved outside that owner. This guard does not prove
that every remaining control or raw GL call has been ported.

Native nested clipping now preserves the existing scissor's extra physical
pixel at 1x, 1.25x and 2x scales. The regression executes the actual clip adapter
against known physical extents, empty clips and inactive ownership. This is
separate from OS DPI transitions and actual XUI traversal at changed DPI.

The Windows RelWithDebInfo build and full staging passed. All fifteen viewer
cases passed on pinned SwiftShader with zero validation errors and zero XUI
pixel mismatches; the readback was inspected. Local evidence is in
`.tmp/viewer-vulkan-mouse-local`. The runner explicitly records synthetic native
events and unqualified OS DPI/IME, connected UI/chat and world rendering.
Cross-platform qualification of this checkpoint awaits the dedicated workflow.

Source accounting passes with 28,415 witnesses in 1,747 files and 91 boundary
source hashes; all 78 related regression tests pass. Full V3 remains open: required login/status/alerts/agreements,
their media/resource and internal-control dependencies, additional interaction
states, general native lifecycle admission and platform DPI/IME qualification.
The earlier resource/font producer checkpoint passed all three software CI
platforms in [run 37796771010](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37796771010);
that run does not qualify these later XUI/input changes.

## Shared native UI owner and skin checkpoint, 8 October 2026

`VSUIContext` now owns the native drawing bridge, image provider, UI singleton,
root, widget defaults, translations, color table and CPU-backed font registry.
Its caller supplies settings and the native window; those borrowed objects and
native resources must outlive the owner. An explicit caller admission policy
must be installed before UI construction. Teardown releases controls and
factory defaults before fonts, facades and the drawing bridge, including after
partial construction. This replaces fixture-owned bootstrap with a reusable
viewer component. The native UI/resource sources compile whenever
`USE_DILIGENTCORE=ON`; diagnostic fixtures and readback oracles still require
`VS_VULKAN_DIAGNOSTICS=ON`.

Skin selection comes from `SkinCurrent`, `SkinCurrentTheme`, language and font
settings. Existing `LLDir` paths resolve base, selected skin, theme, translated
XUI and user overlays; existing texture declarations, colors and widget defaults
are used. The editor retains the skin's text-field image together with its text
colors. Forcing a solid background in the fixture made high-contrast text
unreadable and has been removed. Skin/theme selection is initialized with the UI
owner; live skin switching within an existing owner is not qualified.

The staged runner enumerates every entry in `skins/skins.xml`, requires the
listed skin/theme directories, and verifies the actual selected
skin/theme/language in lifecycle evidence. Twenty-two catalog selections, the
base default skin and its German XUI overlay each run the existing fixture,
input checks and GPU readback oracle. The fifteen presentation/UI/failure cases
remain mandatory. This qualifies skin resolution and rendering for the admitted
controls, not every panel/control state in every skin. Required login, status,
alerts, agreements and their media/resource dependencies remain open.

The previous pointer/XUI CI [run 37822981181](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37822981181)
passed Windows and failed both Linux jobs. Linux enabled font vertex collection,
which entered `LLRender::beginList` before native glyph publication.
`LLFontVertexBuffer::render` now bypasses both GL display-list construction and
replay while the native font owner is active. A regression executes the actual
cache implementation with stubbed GPU/font endpoints, including a populated GL
cache followed by native publication and return to ordinary GL caching. The
native atlas still caches glyphs. The Win32 button-down callback also uses its
message's captured client coordinates rather than the cursor position from a
later poll; its actual queued callback has a regression check.

Local Windows qualification: the full Autobuild-backed RelWithDebInfo viewer
build and complete staging passed. All 39 SwiftShader cases passed, with zero
validation errors and zero XUI pixel mismatches. Representative default,
high-contrast, Starlight and German readbacks were inspected. Evidence is in
`.tmp/viewer-vulkan-skins-local`; it records executable/library hashes and the
modified source worktree. This is local development evidence. Cross-platform
runtime qualification of the Linux font correction awaits the dedicated CI.
All 81 related regression tests pass after source-accounting refresh.

Normal Vulkan startup still fails closed through `VSRenderBackend::canStartSession`.
The new owner currently has a diagnostic consumer; extracting it does not wire
the normal session. Next, give a native session owner the existing viewer
settings/window and Diligent resource lifetimes, connect normal frame/reshape
and input callbacks, then admit the required startup panels and their callbacks.
Only open normal-session admission after that path passes lifecycle, resource,
UI, DPI and platform-input qualification. Full V3, connected UI/chat and world
rendering remain unqualified.


## Native viewer-window and required startup owners, 9 October 2026

Source: `e7e9e6e6aa68ab125b4af41a2955259aa0d2ba4c` on `vkstorm-vulkan`.
`LLViewerWindow` now has a native constructor and owns `VSVulkanContext`, the
skin-aware UI owner, native root, login holder, floaters, popup owner, progress
views and standard alert channels. Native frame, reshape, mouse, keyboard,
Unicode and focus routes use that graph; cleanup releases controls and channels,
then Diligent resources/swapchain, then the borrowed platform window.
`LLAppViewer::initWindow` has a native dispatch branch. These reusable sources
build with `USE_DILIGENTCORE`; the startup probe remains diagnostic-only.

`VSVulkanContext` uses the machine's Vulkan loader discovery and verifies the
created adapter against the selected device. Software ICD selection is supplied
by the test launcher. Native UI rendering and presentation do not enter a GL
context. Native progress images and media plugin buffers publish CPU bytes to
immutable Diligent resources. Plugin copying preserves stride, row orientation,
BGRA conversion and opaque RGB semantics. Media buffers are never retained by
reference after plugin access. Live browser execution is not yet qualified.

The native owner now shares the viewer's translation/substitution initialization
and UI sound callbacks. Skin image facade names retain their public declaration
names; internal GPU ownership keys are not exposed to controls resolving images
again. Ordinary named XUI panels use their admitted base widget; registered
specialized factories remain subject to the closed admission policy. Alerts use
the real popup/root layout before world chrome exists. Modal controllers tolerate
an absent menu container. Critical messages bind only controls present in their
XUI. Native shadows and context cones emit explicit colored triangle packets.

`VS_VULKAN_DIAGNOSTIC_STARTUP=1` adds an offline startup qualification through the
actual `LLViewerWindow`, `FSPanelLogin`, progress controllers, `GenericAlert`
notification/alert handler and `message_critical`/`LLFloaterTOS` controller. The
sample alert/message text is test-only. This probe verifies Unicode/key routing,
focus release, required controls, resize, configured DPI scaling,
minimize/restore, nine GPU readbacks and ordered teardown. It requires zero
pixel mismatches (per-channel tolerance 3), zero validation errors, the expected
actual adapter and loaded-library hashes. Injected UI, frame and cleanup failures
must exit with the expected error and complete teardown; crashes do not pass.

Local Windows verification: the full Autobuild-backed RelWithDebInfo viewer and
complete assets/runtime stage passed. All 43 staged SwiftShader cases passed,
including four startup cases and the existing packaged skin/theme matrix.
Readbacks of login, modal alert, critical message and progress were inspected.
Evidence: `.tmp/viewer-vulkan-startup-final/results.json` and its per-case logs,
readbacks and library hashes. This is local development evidence, not archived
cross-platform CI acceptance. The required-dialog test at 640x480 does not
establish responsive layout or visibility of every button in small windows.

CI run [37844112807](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37844112807)
passed Windows and failed both Linux jobs: native primary font faces were marked
as fallback faces, causing an assertion during lazy glyph creation. The registry
now preserves primary glyph-generation ownership while native drawing supplies
CPU-backed fonts. A regression executes the actual registry decision. Linux
runtime confirmation of this correction awaits the dedicated CI.

Normal `LLAppViewer::init/frame/cleanup` startup and connected-session admission
remain incomplete: `VSRenderBackend::canStartSession("Vulkan")` still fails
closed. The startup probe's login callback does not authenticate. Browser-backed
TOS/media execution, authentication callbacks, OS DPI/IME, required-dialog action
coverage and full G-UI/G-RESOURCE closure remain open. This implementation is
viewer code with an offline diagnostic consumer; it is not complete V3 or
connected UI/chat acceptance.


## Normal native application login integration, 9 October 2026

The normal `RenderBackend=Vulkan` application path is now wired through
`LLAppViewer::init`, `frame` and `cleanup`. Native builds admit this path through
`VSRenderBackend::canStartSession`; builds without native Vulkan continue to
refuse it before graphics initialization. Device discovery uses the machine's
Vulkan loader and installed drivers. The software ICD override belongs to CI.

After the existing configuration and HTTP initialization, native startup owns
CPU image/filesystem services, disk cache, coroutines, credentials, voice and
HTTP pumping, and the reusable native viewer window/UI. It avoids the OpenGL
texture workers, feature recommendations and world UI owners. The frame loop
runs the existing `idle_startup` state machine and native UI drawing. The actual
`FSPanelLogin` Connect callback advances the existing authentication states;
credential handling, proxy setup, authentication transport, progress, failure,
retry, required modal alerts, TOS and MFA controllers are retained. The login
and edit menus use admitted controls and explicit-color native primitives.
Cleanup stops authentication, UI, media/audio/plugins, network services, threads
and remaining singleton/coroutine owners in order.

The dedicated viewer CI now requires an ordinary 30-second native login launch
with diagnostic dispatch disabled. It requires the selected software device,
actual login-screen construction, CEF plugin startup, publication of
`login_html` browser pixels to native resources, validation without errors and
clean exit. A crash, timeout, missing required log evidence or missing browser
pixel publication fails the job. The existing deterministic UI/readback and
skin matrix remain independent checks.

Local Windows qualification used a complete staged `RelWithDebInfo` viewer:
all 44 viewer cases passed, including the normal launch. After tightening the
normal check to require real browser pixel publication, that launch passed
again with a 723-by-737 login browser image, synchronization validation enabled
and exit code 0. Evidence is in
`.tmp/viewer-normal-login-final/results.json` and
`.tmp/viewer-normal-login-pixels-final/normal-login/result.json` plus its
`viewer.log`. No installer or release publication was produced. Linux runtime
confirmation is left to the dedicated platform jobs.

CI run [37867720427](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37867720427)
passed the ordinary Windows login launch, including browser pixel publication
and clean shutdown. Both Linux software devices passed the diagnostic matrix
but the ordinary launch crashed during WebRTC audio-device initialization:
`adm->Init()` returned -1 because the headless runners had no audio service.
The dedicated workflow now provisions a private PulseAudio server with a
48-kHz stereo null sink and its monitor capture source. It checks both devices,
exports the server address to the viewer and archives the server log and device
inventory. Normal voice initialization and all login acceptance checks remain
enabled. Linux confirmation of this infrastructure correction awaits CI;
the previous Linux crash is not counted as a Vulkan login pass.

Real-server authentication has not been exercised; its implementation now uses
the existing authentication path rather than the offline probe's rejecting
callback. Connected region/session/chat integration remains V4 work. An
otherwise successful native authentication reaches an explicit notification and
retry boundary before GL world creation, instead of hanging or entering that
renderer. This does not qualify a connected session, all G-UI/G-RESOURCE
contracts, OS IME/DPI event coverage, or full V3/world parity. The diagnostic's
`normal_session_admitted=false` field describes that probe's consumption; the
new ordinary-launch evidence is recorded separately.
