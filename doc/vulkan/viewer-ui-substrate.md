# V3: viewer UI resource and packet substrate

Source: `bb1a0840aeaf0c06ed9f8d04430a06032d6a96c1` on `vkstorm-vulkan`.
Status: resource and producer integration underway; full V3 acceptance remains open. This code
runs in the staged viewer's native diagnostic lifecycle. An initial closed widget set now constructs corrected mini-progress XUI and
plain editors, with native window input/focus, Tab traversal, scrolling and the
preeditor contract. Full required-panel admission and connected chat remain open.

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

## Remaining V3 work

1. Extend the implemented local skin provider, rectangle/border/transform/clip
   and alpha/additive adapters to every required control path. Initial widget
   traversal passes; additional primitives and interaction states remain open.
2. Define and own the required widget/panel/floater admission sets in the native
   lifecycle. An initial fixture set is admitted; the remaining required
   panels and their internal widget/callback dependencies still need admission.
3. Admit the login, status/progress, nearby transcript/input and required
   alerts/agreements through the native lifecycle; remove their reachable GL
   resource publication paths.
4. Extend the qualified focus, Tab, keyboard editing and scrolling checks to
   the full admitted interface. Qualify platform DPI changes and OS IME where
   runners support them; the direct preeditor contract does not qualify OS IME.

Full G-RESOURCE/G-UI closure and V3 exit remain open until these integrations
and their viewer-level checks pass. Connected transport/login/chat acceptance
is the subsequent V4 checkpoint.

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
