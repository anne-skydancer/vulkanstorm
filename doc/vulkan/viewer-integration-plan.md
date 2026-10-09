# Native Vulkan viewer integration plan

Status: staged implementation plan. V1/V2 have an
[implemented native viewer diagnostic](viewer-native-presentation.md); CI run
[37742422223](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37742422223)
passed Windows SwiftShader, Linux SwiftShader and Linux Lavapipe.
V3 interface/resource implementation and expanded software acceptance are complete
on all three platform/runtime combinations; see the [V3 completion record](viewer-ui-substrate.md#v3-completion-9-october-2026).
V4 connected UI/chat is implemented; software replay qualification is being recorded
in [the connected-session record](viewer-connected-session.md). Live-server
qualification remains explicitly untested. Subsequent world rendering remains open.
Planning source: `417e32891aee7bddf73a1ac1bbf7226133f78286` on
`vkstorm-vulkan`, reviewed on 7 October 2026. Continue authorized implementation
on that branch. New viewer-owned C++ files use the `vs` prefix; standalone tools
and tests retain descriptive names.

## Objective and evidence boundary

Implement and qualify native Vulkan UI/connected nearby chat first. Then add
world rendering elements incrementally until full supported rendering parity is
established, including lighting, shadows, water, transparency and
post-processing. Each increment is independently reviewable and qualified.
Temporary omissions must be disclosed and gated; they are not final parity.

The standalone Diligent executable exercises device/resource/rendering/WSI
contracts using the viewer's Autobuild packages. It does not execute the
viewer window factory, startup state machine, resource facades, main loop or
shutdown. Those are the detected implementation gap. Passing harness tests or
compiling/staging the viewer cannot qualify the viewer's native execution path.

Use the pinned DiligentCore Vulkan backend. Diligent owns backend allocation,
descriptors, pipelines, command submission, transitions and retirement. Viewer
code owns semantic packets, asset generations, admission, frame/view plans and
consumer completion. Do not create a second generic RHI. Vulkan is a peer
backend; failure ends the Vulkan session with a useful error, without GL/Zink
fallback. Normal execution discovers available machine drivers; software ICD
selection is isolated to CI/test configuration.

This plan extends the [roadmap](qualification-roadmap.md), follows the current
[insertion catalog](diligent-insertion-catalog.md), and retains the original
[UI/chat route and gate contracts](milestone1-source-acceptance.md) for the
first checkpoint. Their source acceptance does not establish gate implementation
or source acceptance for additional world routes.

## Dependency-ordered work

### V1: backend admission and native viewer window

Close I01/I02 and R01 first. Resolve the backend before
`LLAppViewer::init` selects a GL provider or calls `initWindow`. Introduce an
explicit window/backend mode through `LLWindowManager::createWindow` and the
Win32/SDL2 constructors; do not reinterpret `use_gl=false`, which selects the
existing headless route. Preserve the separately selectable GL backend.

For Vulkan, create the viewer's real HWND or SDL2/X11 window without creating a
GL context or running GL initialization. Retain native events, input, IME,
clipboard, focus, client size and DPI. Expose the handles required by Diligent,
including SDL-owned X11/XCB handles with documented ownership. Initially admit
Windows x64 and SDL2/X11 Linux x64; other window systems stay explicit exclusions.

Exit: launch the staged viewer into a bounded diagnostic mode using its own
window factory; identify the selected mode/native handles, process events, and
close cleanly. GL-path entry traps must reject GL initialization in this mode.
Exercise window-creation failure and cleanup after partial initialization.

### V2: Diligent ownership and presentation in the viewer

Build on V1. Add a viewer-owned session/context with explicit initialization
states and ownership of factory, device, context and swapchain. Verify queried
adapter capabilities and actual selected device. Use installed-driver discovery
by default, not a production SwiftShader/Lavapipe preference.

Connect the viewer's frame and reshape paths to clear/submit/present through
Diligent. Handle zero extent/minimization by suspending presentation, then
restore/recreate the valid swapchain as required. Handle out-of-date/suboptimal
and surface/device failure through the pinned GHI's actual API; inspect and
qualify any recovery assumptions rather than invent unsupported guarantees.
Stop producers and retire submitted work before releasing swapchain, native
window and device ownership. Avoid assuming graphics completion alone proves
presentation completion.

Exit: the actual staged viewer creates and presents a native Vulkan clear
window, repeatedly resizes and restores it, and shuts down under core and
synchronization validation. Capture lifecycle events, extents, generations,
device identity and loaded libraries. Test failure at each initialization step,
pending-work shutdown and a controlled device-failure reporting path. A simulated
failure proves application handling only, not real driver device-loss recovery.

### V3: native UI rendering and resource substrate

Build on V2; implement I03ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“I09/I26/I27 and G-RESOURCE/G-UI. Separate CPU image
decode/font rasterization from GPU publication. Replace UI image/font/buffer
and shader ownership with Diligent resources, preserving generation lifetime,
lazy glyph creation, upload bytes, replacement and readback completion.
Decouple GL-bearing texture facade types where required; retaining an old
GL implementation behind a neutral name is not acceptance.

Use a reusable viewer-owned UI lifecycle with the same settings, skin, theme,
font and language selection as the OpenGL peer. Preserve base/skin/theme/user
asset overlays and translated XUI; skin-specific text-field images and text
colors must remain paired. Run the admitted controls through every packaged
skin/theme plus a translated overlay, with selection identity and readback
checks. This matrix does not qualify additional controls or live skin switching.
The completed normal-login/UI resource scope is recorded in
[the V3 completion record](viewer-ui-substrate.md#v3-completion-9-october-2026).
Connected-session wiring belongs to V4 below.

Submit ordered UI packets with explicit texture/sampler, blend, clip, transform,
origin and DPI state. Preserve the corrected XUI baseline. Admit login,
progress/status, plain nearby transcript/input and required alerts/agreements;
gate optional widgets before construction, including restored floaters and
direct factories. Keep startup resource creation out of the GL path.

Exit: viewer-rendered deterministic UI fixtures and readbacks verify text,
Unicode, glyph replacement, textures, alpha, clipping, ordering and orientation.
Test keyboard focus, IME where the runner supports it, scrolling and DPI changes;
record platform-event coverage limits. Zero unexplained validation errors and
zero trapped GL-path entries are required. Resource replacement tests delay
completion and check lifetime, not merely object creation.

### V4: connected UI/chat checkpoint

Implement the full R02ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“R12/G-* closure in the source acceptance record, including
startup-state splitting, CPU region/session state, UDP and HTTP dispatch,
settings/observer admission, chat effects, and owned init/destroy callbacks.
Keep world/auxiliary producers gated while preserving session transport,
authentication, required agreements, nearby send/receive and bounded logout.
Do not solve GL dependence by suppressing required protocol work.

Exit: deterministic protocol replay checks malformed/unknown messages, queued
HTTP events, late callbacks, relogin, timeout, denied UI/settings cascades and
partial-init cleanup through viewer code. A real server session must separately
demonstrate login, nearby chat send/receive, disconnect and logout. Record that
result as untested if credentials/server access are unavailable; replay alone
cannot establish connected-chat acceptance. This checkpoint has no world draws.

### V5: world contracts and first geometry increments

After V4, extend source/admission accounting for I11ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“I15 and every newly reached
startup, message, setting and resource callback. Design the viewer-owned
frame/pass plan using [component designs](component-designs.md),
[shader contracts](shader-contracts.md) and the
[render-graph outline](render-graph-design-outline.md). The outline is design
input, not an implemented graph or proof of API-neutral source data.

Specify vertex/index/uniform layouts, material/shader variant keys, coordinates,
color encoding, depth/blend/raster state, attachment use and resource generations.
Reflect actual SPIR-V against CPU producers. Audit `LLDrawInfo` and CPU scene
preparation for embedded GL ownership and in-draw side effects before reuse.
Do not translate ambient GL calls one for one.

Add deterministic geometry first, then bounded connected-world object/asset
production and opaque/fullbright/masked material slices. Expand scene admission
only as corresponding resources and consumers are implemented. Add terrain,
legacy/PBR/glTF materials, rigged avatars, foliage/dynamic geometry, sky and HUD
in separately qualified increments; specify required bake/asset dependencies
before admitting avatars or other consumers that need them.

Exit for each slice: the viewer submits that content through Vulkan, compiles
and reflects its reachable shader variants, passes controlled geometry/material
readbacks, and preserves UI/chat and lifecycle checks. Deterministic local scene
fixtures provide software-device CI evidence; actual streamed-world behavior
needs a separately recorded session result. Scene access cannot fall back to GL.

### V6: lighting and dependent views

Build on qualified geometry/resources. Add deferred attachments and lighting,
sun/local/projector lights, shadows, probes and mirrors in dependency order.
Define view identity, partial cube/mip publication, per-view histories and
replacement/reset rules before submitting those views. Preserve format and
encoding contracts rather than forcing every pass to share one tolerance.

Exit: viewer-executed fixtures compare intermediate attachments, depth,
lighting and final composition for each admitted view. Test resize, teleport,
environment changes, replacement and cancellation against actual generations.
Retain earlier geometry/UI/chat coverage.

### V7: water, transparency and post-processing

Add water/exclusion/haze and above/below-water ordering, then sorted alpha,
particles/custom blends/glow, supported PPLL capture/resolve and its independent
functional alternative, and the supported post-processing chain. Do not reorder
passes merely because the new API permits it. Qualify temporal reset and
multi-frame behavior for exposure, SSR, DoF, AA and other supported effects.
Optional optimizations such as instanced light accumulation require separate
equivalence and measured benefit; they are not necessary to close integration.

Exit: per-pass and final viewer readbacks cover alpha order, water crossings,
PPLL overflow/truncation/budget behavior, glow/color encoding and temporal
sequences. Define tolerances from each contract; byte equality is required only
where contractual, not assumed for all floating-point effects.

### V8: auxiliary and interaction parity

Implement supported I18ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“I22 producers/consumers: previews, local bakes,
impostors/maps, media, picking/selection/debug, queries and captures. Bring
prerequisites forward when earlier slices require them. Keep HUD world content
distinct from 2D UI. Verify capture re-entry, tiled/oriented readback, media
dirty-region publication and synchronous consumer completion. Preserve CPU
picking semantics; GPU IDs cannot silently replace them.

Exit: each supported route has viewer execution evidence, completion/cancellation
tests and a reviewed disposition. No supported facility is permanently omitted
because the original UI/chat checkpoint gated it.

### V9: full supported parity acceptance

Reconcile all supported routes and shader variants against the insertion catalog
and roadmap parity matrix. Finish complete Autobuild `RelWithDebInfo` staging
without an installer. Record passed, failed and untested cases, source/package
pins, capabilities and scope. Full parity requires viewer results for all
supported contracts; harness results remain prerequisite evidence.

Software-device correctness acceptance does not establish physical GPU
performance or vendor-driver compatibility. AMD/NVIDIA hosts are assumed
unavailable and are not an implementation acceptance gate. Do not claim measured
performance/support that the evidence cannot establish. Branch rename/default
renderer promotion is a separate readiness action, not automatic on a CI pass.

## CI and acceptance mechanics

Extend the existing dedicated [Vulkan workflow](software-vulkan-ci.md), keeping
production baseline CI, release publishing and `latest` untouched. Retain the
standalone matrix as dependency/GHI regression coverage. Add viewer execution
from its complete staged directory as V1/V2 become executable, then expand the
viewer cases alongside each slice; do not replace viewer tests with harness runs.

Use Windows SwiftShader and Linux SwiftShader/Lavapipe. Linux native window
tests use Xvfb; no-display tests exercise independent offscreen/policy/replay
paths. Use a pinned window-manager dependency if real Linux minimize/focus
events are required; Xvfb alone cannot establish events it does not produce.
Record synthetic extent/failure injections separately from observed OS events.

Every positive viewer case must require validation availability, expected test
device, diagnostics, explicit completion and fresh artifacts. Crashes, timeout,
missing evidence and unexplained validation errors fail the job. Preserve
isolated intentional-invalid-use and pixel/orientation negative tests, and add
viewer-specific proof that its failure path reaches the CI runner. Keep test
hooks development-only. Trap GL rendering/resource/context entry points in the
native path; a loaded-library list alone cannot prove absence of GL execution.

Archive source/dependency revisions, toolchain/configuration, actual device,
loaded library paths/hashes, viewer lifecycle/route records, validation logs,
test results, readbacks and failure images. Publish exact build/test commands
with each acceptance record. Native WSI evidence and offscreen evidence must be
reported separately. Live-session results remain distinct from replay fixtures.

Before each source-changing increment, refresh affected source hashes and
review dispositions in the insertion/boundary evidence. Historical acceptance
must retain its source pin; updating hashes without re-review is insufficient.
Run the insertion/boundary checks, their regressions, and the focused viewer
tests appropriate to the changed slice. Maintain a per-stage record of source,
cases, CI artifacts, passed/failed/untested outcomes and remaining limitations.

## Current implementation boundary

V1–V3 are implemented and software-qualified. V4 production session integration
and the dedicated staged-viewer replay are implemented; consult the
[connected-session record](viewer-connected-session.md) for current qualification.
A real-server session remains untested. V5 introduces world contracts and bounded
geometry increments; no scene producers may be admitted merely because V4 can
connect and exchange nearby chat.
