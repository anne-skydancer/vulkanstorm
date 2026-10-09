# Current DiligentCore insertion audit

This is the authoritative entry point for insertion coverage on `vkstorm-vulkan`,
source `f80e6c725f72d7a9cd6b3d8657fecf0e6b087548`. DiligentCore is the selected
GHI, not a candidate awaiting comparison with bgfx. The dependency preparation
does not implement a renderer. Windows/Linux are in scope, with NVIDIA, AMD and
Intel first-class; macOS/Metal are outside the selected product scope.

**Acceptance status: insertion locations and responsibilities reconciled for the declared current-source scope.** The generated ledger has no unmapped or unreviewed in-scope candidate. This combines source-reviewed roots and callback obligations, facade/interface proofs, exact typed receiver exceptions and bounded exclusions. It does not mean that every algorithm has been ported, that every shader permutation has been compiled, or that runtime parity is measured. Runtime parity, performance and hardware qualification remain unmeasured.

The first UI/connected-chat implementation-plan item additionally has a
[source-route and gate acceptance record](milestone1-source-acceptance.md), with
checked startup, protocol, settings, floater and init/destroy registration
censuses. Its proposed gates are implementation requirements, not existing
runtime enforcement. The planned connected-chat checkpoint admits chat without world rendering.

## Source pin refresh for existing UI fixes

The source pin now includes the already-applied modern-skin progress panel and
colour corrections. The two English progress panels restore the complete base
layouts and required control names, retaining the registered blue fill colour.
The widget uses that registered colour instead of inline component attributes.
These fixes preserve the existing I08 UI rendering responsibilities; they are
accepted baseline behavior, not Vulkan failures or missing patches.

All four changed files have explicit I08 review entries and SHA-256 hashes in
the insertion records. At that UI refresh, regenerated discovery had the same
28,263 candidates in 1,731 files; those
counts alone do not prove XUI semantics. The progress panel tests check layout,
required controls and colour bindings. Runtime Vulkan parity remains unqualified.

The current refresh additionally includes Vulkan implementation CI and a hashed
Diligent acquisition synchronization overlay. The two standalone test files
have explicit I25 whole-file reviews: they are not linked into viewer runtime.
The current ledger has 28,281 candidates in 1,733 files, including 19
test-only candidates. The standalone tool supports generic device discovery and
headless execution; its software drivers are CI infrastructure. Viewer rendering
source and item 1's 43 route-source
hashes are unchanged. The dependency overlay has local Windows SwiftShader
evidence; full CI and viewer runtime qualification remain separate.

## Native viewer checkpoint refresh

The current source now implements a development-only native viewer
window/clear/presentation path. Its [implementation and qualification record](viewer-native-presentation.md)
distinguishes actual viewer execution from standalone harness evidence. Connected
UI/chat and world rendering remain unimplemented. The diagnostic source has
whole-file I02 reviews; affected factory, platform and startup/cleanup ranges
were re-reviewed and their positions/hashes refreshed. The current generated
ledger has 28,516 candidates in 1,752 files (19 standalone test-only candidates).
The UI/chat boundary manifest now covers 94 hashed source files, including the
new diagnostic and its application/window interfaces. Registry admission and
the complete proposed UI/chat gate set remain implementation requirements.
Local Windows diagnostic evidence does not establish cross-platform parity.

## Evidence and interpretation

* [Insertion records](diligent-insertion-records.json) define current ownership,
  exact root evidence, public Diligent API routes, work packages and mappings of
  all 73 prior responsibility records. I24 is explicitly a triage bucket, not a
  completed implementation seam or an approved exclusion.
* [Insertion sites](diligent-insertion-sites.csv) list source path, line,
  out-of-class function context where lexically recognizable, source statement,
  record mapping and semantic review disposition. A declaration/macro/inline
  context is labeled instead of being assigned to an unrelated prior function.
* [Summary](diligent-insertion-summary.json) is generated from current tracked
  source. Candidate counts include false positives, types, headers and config
  variants; they are not counts of edits, features or active runtime paths.
* [Checker](check_diligent_insertions.py) reconciles every previous GL candidate,
  verifies every previous shader registration at its actual source line, rejects
  stale reviewed-source hashes and separates mechanical mapping from reviewed
  source coverage. It cannot preprocess C++, resolve arbitrary dynamic dispatch,
  inspect opaque binary implementations or prove runtime behavior.

Discovery scans tracked code beyond the original `indra` GL-call inventory:
registry-validated GL calls, state/resource wrapper users, GL types/constants,
proc-address macros, WGL/GLX/SDL GL operations, renderer interface includes,
virtual draw/render/lifecycle roots, GPU profiler macros, indirect snapshot and
resource calls, all GLSL modules, shader registrations, settings/feature tables,
platform/build switches and runtime staging. Source review must additionally
follow dynamic/callback and producer/consumer edges; lexical completeness is not
semantic completeness.

The prior inventory establishes 73 overlapping responsibility records, 71
active/conditional and two dormant, not 73 independent features. Its 223 GL
candidate files, 225 GLSL modules and 690 registrations are historical census
bounds. Current rendering source remains unchanged from that audit; dependency,
packaging and login/preferences changes do not change this fact.

## Insertion ownership and dependency order

| Package | Required locations and responsibilities | Acceptance gate |
|---|---|---|
| WP0 | Current-source catalog; resolve every candidate to a reviewed seam or evidence-backed CPU/dormant/test/platform exclusion; immutable source and public API evidence | No unreviewed in-scope path, unknown callback or unmatched original contract; unknown external implementations explicitly bounded |
| WP1 | I01/I02: process backend, native window factory, context-independent handles, device/swapchain, feature identity, resize, failure and complete teardown | Native diagnostic window/clear without GL context; resize/minimize/DPI/fullscreen/present/failure/teardown checked |
| WP2 | I03ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“I07: draw/state/resource/shader/target substrate; immutable generations and ABI; upload/readback and multi-view lifetime | Shader reflection and producer schema agree; GPU-delay replacement tests; resources and CPU byte owners retained through actual completion |
| WP3 | I08ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“I10: ordered XUI/widget/image/text, glyph atlases and lazy uploads; browser/media remain later scope unless explicitly admitted | First slice: native login/status and connected nearby chat, input and presentation; gate world graphics and optional UI producers while retaining CPU session services. Full Preferences/browser parity is later scope. |
| WP4 | I11ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“I15: CPU scene snapshots and query feedback; main/cube/HUD frame roots; pools/materials/glTF/avatar/terrain/particle producers | Constrained world tier visibly matches supported contracts; unsupported routes disclosed; complete per-draw state |
| WP5 | I16/I17: shadows, probes/mirrors, deferred lighting, water/exclusion, sorted alpha/glow, PPLL fallback, post/history | Per-pass/temporal comparisons and valid cross-view/subresource dependencies; exact blend/mask/color and overflow semantics |
| WP6 | I18ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“I22: all preview/bake/impostor/map/tool/label/query/capture/pathing/profiling producers and consumers | No reachable GL callback in native path; synchronous CPU consumers wait for submitted completion; all facilities covered or capability-gated |
| WP7 | I23: actual platform packages, staging, manifests, capability policy and vendor/driver matrix | Fully staged Autobuild viewer and Release qualification; public Vulkan selection only after full supported renderer acceptance |
| WP8 | Optional vendor/common utilities, parallel recording, queues, compute/batching changes | Measured benefit and maintained visual/lifetime parity with independent equivalent functional fallback |

These are ownership/dependency boundaries, not time estimates. Diligent owns
backend memory/descriptors/PSOs/commands/submission/barrier lowering/retirement
machinery. Viewer glue owns asset generations, semantic packets, pass ordering,
per-view histories, settings and consumer contracts. Do not recreate a generic
Vulkan RHI behind a thin Diligent wrapper.

## Source-backed seams that the small initial catalog missed

### Window creation precedes every render resource

`LLAppViewer::init` selects the GL provider before `initWindow`; backend
resolution must therefore move before that selection. `LLViewerWindow` constructs
the OS window, then immediately compiles shaders and creates GL buffers, fonts,
images and shared-context workers. Replacing only `swapBuffers` cannot enter a
native path.

`LLWindowManager::createWindow` is in `indra/llwindow/llwindow.cpp`, not a separate
manager source. Its `use_gl=false` path creates a headless window. On Linux both
SDL constructors ignore the existing flag, and default SDL2 creates an
`SDL_WINDOW_OPENGL` plus `SDL_GL_CreateContext`. Win32 independently loads
`opengl32.dll` in its constructor. A real backend/window mode must cross the
factory and platform constructors, rather than reinterpret a boolean intended
for headless behavior.

Win32 exposes HWND. SDL2 `getPlatformWindow()` returns null; its native extraction
currently caches Xlib handles and warns on non-X11. Diligent's
`LinuxNativeWindow` has XCB/Xlib/Wayland fields, but that does not implement the
viewer input/clipboard/native-window behavior for Wayland. Start supported Linux
qualification with SDL2/X11 and distinguish native Wayland additional scope.

### Public texture APIs and CPU-facing interfaces also carry GL

`LLTexture::getGLTexture`, `bindDefaultImage`, `bindDebugImage` and immediate
update contracts feed `LLGLTexture`, whose public constructors/creation methods,
target/format parameters and texture-name setters expose GL types. The
`LLTextureManagerBridge` abstract factory returns `LLGLTexture` and requests GL
creation; the viewer implementation is in `llviewertexture.cpp`. The substrate
must decouple these public boundaries, not merely replace `LLImageGL` internals.

Ordinary thumbnail/UI/asset callers may remain unchanged behind a compatible
semantic interface. That is a reviewed backend-transparency decision, not an
assumption that every texture reference requires a Diligent object or that every
CPU-facing caller is safe to exclude.

`LLImageRaw::setSubImage` is a genuine namesake false positive: its implementation
locks CPU storage and copies rows with `memcpy`; the reviewed range is recorded.
Pixel data alone does not establish GPU work.

### Text measurement can create/upload a glyph

`LLFontGL::getWidthF32`/drawable-character helpers call `getGlyphInfo`; a miss
calls `LLFontFreetype::addGlyph`/`addGlyphFromFont`, allocates/extends an atlas via
`LLFontBitmapCache` and calls `LLImageGL::setSubImage`. Font metric users therefore
cannot all be marked CPU-only. Keep rasterization/metrics, but split atlas CPU
updates from image generation publication and draw lifetime. Reset/destroy paths
retain raw atlas ownership while safely retiring GPU generations.

`LLScreenClipRect::updateScissorRegion` flushes draws before changing pixel-scaled
GL scissor; `LLLocalClipRect` depends on the font/UI origin. Native UI packets must
capture that order, scale, clipping and origin, including ordinary derived
widgets, previewed floaters and editor line-number/selection drawing.

### Settings callbacks and diagnostics can recreate live resources

`llviewercontrol.cpp` setting listeners release/create GL buffers, resize
screen/shadow targets, reset probes/hero probes, refresh shader/environment and
other render state. Settings/menu/pipeline-listener and telemetry call sites are
part of the insertion catalog. They must publish a new renderer generation or
request a graph rebuild, without deleting resources used by submitted work.

Feature-manager GPU benchmark, GL version/vendor masks, OpenGL renderer-family
persistence in startup, adapter diagnostics and skinning palette limits need
backend-aware services. A Vulkan dependency flag must not stand in for queried
device features or implemented renderer availability.

### Captures include three different roots and nested views

`LLViewerWindow::rawSnapshot` owns tile/aspect/UI/HUD/depth/no-post capture.
`LLFloater360Capture::capture360Images` changes the camera six times and calls
`simpleSnapshot`, which renders multiple offscreen frames and reads RGB.
`LLViewerWindow::cubeSnapshot` supplies reflection/probe faces with masks,
clipping and view-state restoration. They are not interchangeable calls to a
single final framebuffer readback. Explicit capture/view requests and completed
host transfers must preserve these contracts and consumer encodings.

### Appearance baking includes a second library's rendering

Beyond `newview/llviewertexlayer.cpp`, `llappearance/lltexlayer.cpp` and
`lltexlayerparams.cpp` implement layer blends, morph masks and readback.
`renderMorphMasks` has an Intel `glGetTexImage` fallback and normal
`glReadPixels`; alpha-mask arithmetic and the separate morph channel are consumer
contracts. These paths belong to I18 rather than a blanket CPU-appearance
exclusion. Avatar-joint debug geometry belongs additionally to I22.

### Profiling macros hide backend calls

`llcommon/llprofiler.h` enables Tracy OpenGL GPU macros in Tracy configurations;
`llrender/llglheaders.h` includes `TracyOpenGL.hpp`. Every
`LL_PROFILE_GPU*`/`LL_PROFILER_GPU*`, context creation and collect site is a
conditional native insertion/gating point. Retain CPU profiling; do not let a
Vulkan scope execute hidden OpenGL profiling. `USE_TRACY_GPU` alone is not proof
that those header/macro expansions are absent.

### Optional pathing callbacks cannot be inferred from the open-source stub

The open-source `LLPathingLibStubImpl::getInstance()` returns null and render
methods are empty. Viewer facilities gate on that instance. The public interface
also exposes navmesh/VBO draws and callbacks accepting `LLRender`; optional
binary implementations may bypass viewer-owned packet seams. Bound that
configuration at `LLPathingLib`, `LLRenderNavPrim` and the viewer callback roots.
Qualify the actual selected package/interface or explicitly report Vulkan
unavailability for the optional facility. Do not claim equivalent proprietary
pathing behavior from an empty stub.

## Exact library boundary and qualification constraints

Pinned DiligentCore revision:
`bcb8b11eecd0899962c330b798ebe3e786b02bbb`.

* [Factory/device/swapchain creation](https://github.com/DiligentGraphics/DiligentCore/blob/bcb8b11eecd0899962c330b798ebe3e786b02bbb/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h)
  and [Linux native window](https://github.com/DiligentGraphics/DiligentCore/blob/bcb8b11eecd0899962c330b798ebe3e786b02bbb/Platforms/Linux/interface/LinuxNativeWindow.h)
  supply platform mechanisms; the viewer owns native handles and event lifetime.
* [Device resource/PSO creation](https://github.com/DiligentGraphics/DiligentCore/blob/bcb8b11eecd0899962c330b798ebe3e786b02bbb/Graphics/GraphicsEngine/interface/RenderDevice.h)
  and [context APIs](https://github.com/DiligentGraphics/DiligentCore/blob/bcb8b11eecd0899962c330b798ebe3e786b02bbb/Graphics/GraphicsEngine/interface/DeviceContext.h)
  supply draw, copy, map, query, transition and completion routes listed in each
  insertion record. Named shader-resource signatures normally remap reflected
  SPIR-V bindings; signature index is not a GLSL descriptor set number.
* `EnqueueSignal` does not flush. Vulkan staging texture mapping does not wait for
  the GPU. Signal, submit/flush, completion and map are distinct steps.
  `FinishFrame` retires dynamic context resources and invalidates committed
  bindings; offscreen-only work cannot assume primary-swapchain Present always
  services it.
* Divergent face/mip states need explicit subresource transitions; whole-texture
  tracking cannot safely claim one state for heterogeneous subresources. UAV to
  UAV can still require a dependency. Do not emit transitions inside an active
  explicit render pass or treat `SetState` as an emitted barrier.
* API headers 1.4.365 do not impose a 1.4 GPU minimum. Baseline requirements,
  optional atomics/format support, depth formats, shader layouts and device-loss
  behavior must be queried and qualified separately. Optional FSR/DLSS providers
  are disabled in the current package, not integrated viewer optimizations.

Primary Khronos references accessed 2026-10-03:
[synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
support explicit upload/pass/readback dependencies;
[shader memory layout](https://docs.vulkan.org/guide/latest/shader_memory_layout.html)
supports preserving/reflection-checking block offsets and strides;
[depth](https://docs.vulkan.org/guide/latest/depth.html) supports coordinated clip,
depth-format and reconstruction conversion;
[swapchain semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
distinguishes presentation completion from graphics fences. These are
requirements/guidance, not measured viewer results.

## Verification and remaining implementation qualification

Run from the repository root:

```powershell
python doc/vulkan/check_diligent_insertions.py --write
python doc/vulkan/check_diligent_insertions.py
python doc/vulkan/check_diligent_insertions.py --accept
python doc/vulkan/test_diligent_insertions.py
```

The first regenerates audit artifacts only. The second checks reproducibility
against current source. The third accepts only the reconciled insertion-location scope and rejects any new unresolved site. The tests exercise ambiguity, unknown owner, stale provenance and disposition rejection. Reviewed ranges carry exact source-byte hashes, bounded line ranges,
record IDs, disposition and a specific ownership/trace reason. Generic file
prefix assignments cannot complete this audit. Runtime validation, native
presentation, pixel/temporal comparisons, GPU/vendor/driver portability, memory
budgets and performance remain future qualification even after source acceptance.

There is no outstanding insertion-location gap in the current declared source scope. Optional proprietary pathing internals remain a bounded implementation qualification: the native path must qualify the selected package or gate that facility at the recorded interface/callback roots. Shader ABI, media pixel ownership, format/atomics capability policy, native platform lifetime, visual parity and performance are implementation experiments at identified locations, not missing catalog locations.

## Facade proofs and exceptions

The site ledger distinguishes executable escapes from compile-time references. Active includes are interface dependencies; commented includes are discarded. Ordinary wrapper/type consumers map to the definition that owns backend publication, state or draw lowering. A mapped statement does not certify the surrounding function CPU-only. Explicit raw GL/API loader/type/profiling expressions remain rewrite/gating sites with both subsystem and operation ownership.

Generic method spelling alone does not resolve `setBuffer`, `drawArrays`, `drawRange` or `uniform*`: typed global shader receivers are declared in `llviewershadermgr.h`; local/member exceptions have individual hashed site reviews citing the actual declaration. In particular, pipeline screen/cube buffers (`pipeline.h`817/820), probe members, sky strip iterators, model preview locals, avatar joint buffers and terrain bake locals feed I04. Matrix/light/diffuse and query shader locals feed I06. FSPanelFace image-format/alpha decisions feed I05 metadata rather than a GPU draw or blanket UI exclusion.

Widget dispatch is bounded by the source-derived LLView inheritance graph, including FS-prefixed and final classes. Text-segment dispatch is bounded by LLTextSegment inheritance; separator helpers, embedded images and expandable labels feed I08/I09. `LLTextureView`/bars/preview/tooltips are UI/debug consumers; `LLTexturePipelineTester`/test sessions observe resource statistics; `LLTextureKey` is identity/callback bookkeeping. `LLTextureBridge` actions invoke preview/save roots, `LLTextureUploadData` holds asset/resource ownership, and `LLTextureMaskData` holds avatar callback identity/discard state; these retain the I05/I18 publication/cancellation obligations even when the statement itself is CPU bookkeeping.

Dynamic edges are explicit responsibilities: LLGLUpdate queued virtual updates/cancellation feed I03/I05/I12; LLShaderMgr::updateShaderUniforms feeds I06 and environment/group producers; LLTextureManagerBridge factory and loaded texture callbacks feed I05; LLView/text-segment/font callbacks feed I08/I09; dynamic preview/bake/view callbacks feed I18/I19. Media borrowing/reset/upload/main-thread publication and resource retirement must preserve ownership across those callbacks. The checker cannot resolve arbitrary C++ dynamic dispatch; these source-reviewed boundary contracts supply that evidence.

## V3 substrate refresh

The current source pin includes a native viewer packet renderer and a separate
CPU-only FreeType rasterizer. I08/I09 have explicit reviewed source contracts
and hashes; I03Ã¢â‚¬â€œI06 responsibilities are exercised by the native packet
implementation without replacing or reclassifying existing GL producers.
The [V3 implementation record](viewer-ui-substrate.md) distinguishes deterministic
viewer fixture acceptance from the still-open XUI/admission and input integration.
At that checkpoint the ledger had 28,410 candidates in 1,747 files, and the
boundary manifest covered 90 source hashes. Full UI/chat gates and runtime parity remain open.

The subsequent renderer-selector refresh adds a shared I01 identity/admission
policy. Preferences preserve Vulkan as a peer option and explain current normal-
session unavailability without committing a renderer switch. A persisted Vulkan
request stops before GL initialization; the obsolete Vulkan-to-GL fallback is
removed. Hashes and affected review ranges were refreshed against current source.
This is a bounded admission correction, not completion of all UI/chat gates.

## V3 resource and producer refresh

The current pin includes decoded CPU image publication through native LLUIImage
facades, alpha masks/nine-slices, lazy glyph atlas pages, immutable GPU copy and
subimage replacement, and the existing LLFontGL geometry producer with CPU-only
atlas backing. Default GL resources and font behavior remain mapped to their
existing insertion families; their native branches do not make whole font files
CPU-only. Construction gates cover generic builders, custom panel callbacks,
direct floaters and restored floater settings/callback admission. The required
native panel/floater policy, additional drawing/interaction states and OS DPI/IME
remain open. The local provider, corrected mini-progress XUI, editors/scroller
and native window input/focus now pass in the Windows fixture.

The regenerated ledger has 28,415 witnesses in 1,747 files, with no unmapped or
unreviewed scoped candidate. The boundary manifest hashes 91 sources. These
counts qualify current source accounting, not complete V3 execution. See the
[UI substrate record](viewer-ui-substrate.md) for Windows software-device pixel
evidence and remaining acceptance work. Fresh cross-platform CI is required.

The XUI refresh also re-reviews I03 primitive/state adapters and I08 ordered
widget drawing/skin asset ownership, including additive glow blending. The
source pin is still a source-analysis acceptance; full UI/chat gates are open.

The native pointer-input checkpoint adds capture-aware mouse/hover/wheel routing,
focus-loss capture cancellation and decisive GL geometry rejection inside the
native drawing owner. Fractional-scale clips preserve the legacy physical-pixel
margin. Fifteen Windows viewer cases pass; full V3 and cross-platform acceptance
for this delta remain open. The current source accounting has 28,516 witnesses
in 1,752 files and 105 boundary hashes.


## Shared UI ownership and skin refresh

The native UI bootstrap is now `VSUIContext`, a reusable viewer component built
under `USE_DILIGENTCORE`. I08 review covers lifecycle ownership and the existing
skin/theme/language/font overlay routes. Its current consumer is the diagnostic
fixture; normal Vulkan session startup remains gated. The staged runner requires
all catalog skin/theme directories and the translated German XUI assets, checks
selection identity and runs 24 skin fixture cases in addition to the existing
15 cases. Local Windows passed all 39 under SwiftShader. These are admitted
control checks, not complete skin, connected-chat or world parity.

I09 review covers native text bypassing the GL vertex/display-list cache,
including replay of a previously populated legacy cache; that path caused both
Linux XUI jobs to fail in run 37822981181. I02 review covers Win32 button-down
using its captured event coordinates. Regressions execute the actual cache and
button callback with endpoint stubs. Fresh cross-platform runtime qualification
remains pending. The [UI substrate record](viewer-ui-substrate.md) describes the
qualification limits and remaining normal-session/panel integration work.

## V3 completion refresh, 9 October 2026

Source `f80e6c725f72d7a9cd6b3d8657fecf0e6b087548`: required native viewer
controls and resource ownership are implemented, including production plain
chat, required-dialog actions, direct factory admission, DPI/composition routes
and delayed completion lifetime checks. The ledger reconciles 28,516 witnesses
in 1,752 files across 27,797 tracked paths, with 105 boundary source hashes and
690 shader registration rows. Source coverage is accepted; full runtime parity
remains false. See [the V3 completion record](viewer-ui-substrate.md#v3-completion-9-october-2026)
for the expanded software matrix and its qualification limits.

## V4 source refresh (9 October 2026)

The current pin includes production native CPU session transport, reliable nearby
chat, lifecycle ownership, semantic UDP/HTTP admission and native settings/URL
gates, plus the staged-viewer replay. It scans 27,801 tracked source paths and
accepts 28,533 witnesses in 1,754 candidate files; all 690 shader registration
rows remain reconciled. Runtime qualification is recorded separately in
[the connected-session record](viewer-connected-session.md); source acceptance
continues to make no world-parity claim.
