# V3: viewer UI resource and packet substrate

Source: `f622b9ff1e2c294f3e62a6e4840710d8419b3f64` on `vkstorm-vulkan`.
Status: first implementation slice; full V3 acceptance remains open. This code
runs in the staged viewer's native diagnostic lifecycle. Existing XUI widget
construction, focus, IME, scrolling and connected chat are not yet admitted.

## Implemented ownership and drawing

[VSUIRenderer](../../indra/newview/vsuirenderer.h) owns Diligent texture
generations, shaders, pipelines, vertex uploads and bindings. Packets retain
their exact immutable texture generation; replacing a producer handle cannot
change already queued draws. A Diligent completion fence controls retirement,
without a per-draw idle wait. The lifecycle owner waits for idle at teardown.
There is one vertex buffer upload per ordered packet list, with six vertices
per quad; draws preserve paint order. This is a correctness substrate, without
atlas packing or draw-call coalescing yet.

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
320Ãƒâ€”240 viewer swapchain before presentation, at explicit 1Ãƒâ€” and 2Ãƒâ€” scales.
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

1. Connect image decode/asset publication and lazy glyph caching to neutral
   viewer facades, with atlas/subimage updates and generation-aware consumers.
2. Adapt existing UI image/font/draw producers to ordered packets. Preserve
   corrected XUI behavior and implement optional-widget admission before
   construction, including direct factories and restored floaters.
3. Admit the login, status/progress, nearby transcript/input and required
   alerts/agreements through the native lifecycle; remove their reachable GL
   resource publication paths.
4. Qualify focus, keyboard editing, scrolling, platform DPI changes and IME
   where runners support it. Preserve explicit coverage limits.

Full G-RESOURCE/G-UI closure and V3 exit remain open until these integrations
and their viewer-level checks pass. Connected transport/login/chat acceptance
is the subsequent V4 checkpoint.

## Local verification, 8 October 2026

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
