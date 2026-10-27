# 01 — Native rendering stack

Surveyed revision: `vkstorm-vulkan` at `51d1f729a6` (worktree branch
`codex/vulkan-survey`), compared with `origin/vkstorm-release` (`6e12272e9a`).
Nothing was built or run for this chapter. Every status below comes from reading
the source and the CI facts supplied with the survey brief.

**Summary.** The native renderer exists and is wired in. With
`-DUSE_DILIGENTCORE=ON`, the viewer compiles a second frame loop
(`LLAppViewer::frameNativeVulkanLogin`). That loop creates a DiligentCore Vulkan
device and swapchain on the viewer's own window, draws the shared LLUI widget tree
through a native 2D packet renderer, draws a forward-shaded world (objects,
avatars, terrain, sky, flat water, HUD) into the same backbuffer, and presents.
**Headline status: the device, swapchain, UI, frame flow and resource accounting
are Implemented or close to it. The world renderer is Partial. It is a
single-pass forward renderer with simple sun-plus-ambient lighting, no shadows,
no local lights, no reflections or probes, no real water, no post-processing and
no anti-aliasing. Its emissive "glow" target is drawn into but never
composited.** No shipped or CI Release build enables the native renderer: the
option defaults OFF and only the `software_vulkan.yml` qualification workflow
turns it on. With the option OFF, `RenderBackend=Vulkan` stops startup instead of
falling back to OpenGL. The standalone software-Vulkan test build is currently
broken because two renderer sources include viewer headers (see
[Known breakage](#known-breakage)).

## Scope and size

| Measure | Value | Command (from the worktree root) |
|---|---|---|
| Commits ahead of release | 659 | `git rev-list --count origin/vkstorm-release..HEAD` |
| Total diff vs release | 2268 files, +558,942 / −24,699 | `git diff --shortstat origin/vkstorm-release...HEAD` (includes `doc/vulkan` evidence catalogs) |
| Native `indra/newview/vs*` files | 280 (100 `.cpp`, 180 `.h`), 63,353 lines | `ls indra/newview/vs*.cpp indra/newview/vs*.h \| wc -l`; `wc -l indra/newview/vs*` |
| Native `indra/llrender/vs*.h` | 6 files, 893 lines (shared reference geometry, incl. `vsuiprimitive.h` 543 lines) | `wc -l indra/llrender/vs*` |
| `VS_NATIVE_VULKAN` guard lines | 1,506 in 260 files; 1,300 of them in 234 non-`vs*` shared files, all in `indra/newview` | `git grep -h VS_NATIVE_VULKAN -- indra \| wc -l`; `git grep -c ... \| grep -v '/vs[a-z0-9]*\.\(cpp\|h\):'` |
| `VS_VULKAN_DIAGNOSTICS` guard lines | 76 | `git grep -h VS_VULKAN_DIAGNOSTICS -- indra \| wc -l` |
| `llrender` + `llwindow` delta | 36 files, +1,910 / −816 | `git diff --stat origin/vkstorm-release...HEAD -- indra/llrender indra/llwindow` |

This chapter covers the **rendering-stack subset**: 47 files totalling 10,573
lines. They are the device/swapchain owner, the world and UI renderers, UI
resources, fonts, the image provider, snapshot/preview/scene-monitor, budgets,
upload preparation, shader embedding, the native window glue, and the diagnostics
harness. Command:

```
cd indra/newview && wc -l vsvulkancontext.* vsworldrenderer.* vsuirenderer.* vsuiresources.* \
  vsuicontext.* vsuidrawbridge.* vsuifont*.* vsuiimageprovider.* vsscenemonitor.* vssnapshot.* \
  vsscenepreview.* vsterrainshader.h vstextureupload.h vsresourcebudget.h vstexturememory.h \
  vsgputimingledger.h vsimagemips.h vsimageusage.h vsgraphicssettings.h vsrenderbackend.h \
  vsviewerwindow.cpp vsworldcontracts.h vsmaterial.* vsuipixeloracle.* vsvkdiag.cpp \
  vsvulkandiagnostic.h vsimmediate*.h vspreviewbuffer.*
```

The three core GPU files are small: `vsvulkancontext.cpp/.h` (907 + 68),
`vsworldrenderer.cpp/.h` (1,217 + 169) and `vsuirenderer.cpp/.h` (746 + 98),
3,205 lines in all. The world **scene producers** (`vsworldscene.*`,
`vsworldavatar.*`, `vsworldgltf.*`, `vsterraingeometry.*`, `vsvegetation.*`,
`vsparticleeffects.*`) hold another 13,015 lines. `vsworldscene.cpp` alone is
6,745 lines. Those producers belong to the world-content chapters and appear here
only as packet sources.

## Build and enablement

### CMake options

| Option | Default | Effect | Evidence |
|---|---|---|---|
| `USE_DILIGENTCORE` | OFF | Verifies the locked packages and creates the imported targets `ll::diligentcore` and `ll::vulkan`. In `newview`, it adds the native sources, links Diligent, Vulkan and FreeType, and defines `VS_NATIVE_VULKAN=1`. On Linux it also links `X11-xcb`. | `indra/cmake/DiligentCore.cmake:4`, `:14-31`, `:33-65`; `indra/newview/CMakeLists.txt:2359-2389`, `:2763-2771` |
| `VS_VULKAN_DIAGNOSTICS` | OFF | Requires `USE_DILIGENTCORE` and a `RelWithDebInfo` build. Adds `vsvkdiag.cpp`, the R1/R2/R3 replay drivers, the parity baseline, the UI fixture and the pixel oracle, and defines `VS_VULKAN_DIAGNOSTICS=1`. | `indra/newview/CMakeLists.txt:38-44`, `:2390-2406`, `:2759-2761` |
| `USE_MESAZINK` | OFF | Separate Mesa Zink GL-over-Vulkan runtime for the **OpenGL** pipeline (`RenderBackend=Zink`). It has no relation to the native renderer. | `indra/cmake/MesaZink.cmake:1-30` |

- `DiligentCore.cmake:1` describes itself as "dependency preparation. This does
  not enable a native renderer". That is no longer accurate: in `newview` the
  same option now defines `VS_NATIVE_VULKAN` (`indra/newview/CMakeLists.txt:2766-2767`).
- `vsrenderbackend.h` is compiled in every build (`CMakeLists.txt:2357`). All
  other native rendering sources compile only with `USE_DILIGENTCORE`.
- Runtime staging copies `GraphicsEngineVk_64r.dll` and `vulkan-1.dll` (Windows)
  or `libGraphicsEngineVk.so` and `libvulkan.so*` (Linux) from a private
  `vulkan-ghi` directory. That keeps them apart from CEF's own Vulkan loader
  (`indra/cmake/DiligentCore.cmake:41-62`, `indra/cmake/Copy3rdPartyLibs.cmake:342-343`,
  `indra/newview/viewer_manifest.py:82-88`, `:751-752`, `:2343-2345`).
- **Who enables it.** `USE_DILIGENTCORE` is set to ON in only one workflow step:
  `.github/workflows/software_vulkan.yml:673`, which also sets
  `VS_VULKAN_DIAGNOSTICS=ON` and `PACKAGE=OFF`. A grep of `build_viewer.yml` and
  `autobuild.xml` finds no `USE_DILIGENTCORE`. **Inference:** every
  installer/CI Release artifact on this line is built without the native
  renderer.

### Third-party packages (`3p/`)

- The lock file `3p/vulkan-dependencies.json` pins Vulkan-Headers and
  Vulkan-Loader at API 1.4.365 (package `1.4.365-ghi1`). It pins DiligentCore at
  `bcb8b11eecd0899962c330b798ebe3e786b02bbb` (package
  `2.5.6-bcb8b11e-acquire1-wsi1-ghi1`) with two hashed patches.
- `3p/3p-diligentcore/` does not contain a full package recipe. It holds two
  overlay patches with rationale notes and a CMake shim (`vulkan-headers.cmake`)
  that substitutes the pinned Vulkan headers:
  - `acquire-layout-transition.patch`: waits at ALL_COMMANDS for the acquired
    swapchain image. This fixes a `SYNC-HAZARD-WRITE-AFTER-READ` seen on
    SwiftShader (`acquire-layout-transition.md:9-17`).
  - `available-linux-surfaces.patch`: enables XCB, Xlib and Wayland surface
    extensions only when the ICD advertises them. Lavapipe rejected instance
    creation without it (`available-linux-surfaces.md:8-17`).
- `scripts/build_vulkan_dependencies.py` (203 lines) clones, verifies, patches
  and builds the loader and the Diligent Vulkan-only backend. It turns off
  D3D11, D3D12, GL, Metal, WebGPU, Archiver, SuperResolution and HLSL, and sets
  `DILIGENT_DEVELOPMENT=ON` (`:122-132`).
- The packages are **local artifacts**: "The main viewer's `autobuild.xml` is not
  populated with nonexistent archive URLs" (`3p/README.md:51-58`). The Vulkan
  package "is not a LunarG SDK distribution and does not include validation
  layers" (`3p/README.md:14-15`).
- `3p/README.md:3-6` is stale. It still says "The viewer still renders with its
  existing OpenGL pipeline. The disabled Vulkan entry in Preferences …".
- `indra/cmake/VulkanGltf.cmake` (5 lines, `use_prebuilt_binary(vulkan_gltf)`)
  is a glTF asset package. Despite its name it is not part of the GPU backend.

### Vulkan loader, SDK and Mesa/Zink

| Item | Relationship to the native renderer |
|---|---|
| Vulkan loader | A private build of Khronos Vulkan-Loader, staged under `vulkan-ghi`. The device uses normal ICD discovery: "ICD selection belongs to the process/test environment" (`vsvulkancontext.h:19-21`). |
| LunarG SDK / validation layers | Not shipped. `RenderDebugGLSession` turns on `VK_LAYER_KHRONOS_validation` only if the user has installed it (`vsvulkancontext.cpp:229-236`, `vsviewerwindow.cpp:157-158`). CI builds its own pinned validation layers (`software_vulkan.yml:595`). |
| Mesa Zink | A GL provider for the OpenGL pipeline, selected with `RenderBackend=Zink` (`llappviewerwin32.cpp:1103-1125`, `MesaZink.cmake:3-7`). `doc/vulkan/README.md:35-37` says the same: "not a native Vulkan viewer backend or a Vulkan fallback". |
| Mesa Lavapipe / SwiftShader | Software ICDs used only by CI (`software_vulkan.yml:512-527`). |

## Components

Status key: **Implemented** = code exists, is reachable in a
`USE_DILIGENTCORE=ON` build, and covers the component's evident scope.
**Partial** = reachable but a material subset is missing. **Stub** = an entry
point exists with no real behaviour. **Absent** = no code found. "Reachable"
always means reachable only in a `USE_DILIGENTCORE=ON` build.

### Status overview

| Component | Status | Key evidence |
|---|---|---|
| Build integration and packaging | Implemented (local packages only) | `DiligentCore.cmake`, `newview/CMakeLists.txt:2359-2771` |
| Backend selection at startup | Implemented, no fallback by design | `vsrenderbackend.h:7-18`, `llappviewer.cpp:1040-1046`, `:1070-1076`, `:4174-4197` |
| Preferences renderer selector | Implemented | `llfloaterpreference.cpp:4126-4205`, `panel_preferences_graphics1.xml:967-988` |
| Native window creation | Partial (windowed only; Linux X11 only) | `llwindow.cpp:445-463`, `llwindowsdl2.cpp:500`, `vsvulkancontext.cpp:283-293` |
| Adapter/device selection | Partial (automatic only, no user override) | `vsvulkancontext.cpp:217-247` |
| Renderer info (About/crash) | Implemented | `vsvulkancontext.cpp:249-277` |
| Swapchain, resize, minimize, vsync, present | Implemented | `vsvulkancontext.cpp:295-304`, `:505-533`, `:700-711` |
| Device loss / surface loss recovery | Absent | no `DEVICE_LOST`/`OUT_OF_DATE` handling in `vs*.cpp` (grep) |
| Fullscreen | Absent | `llwindow.cpp:447-451` |
| World renderer core (meshes, PSOs, sorting, HUD) | Implemented | `vsworldrenderer.cpp:191-1212` |
| World lighting / materials / PBR | Partial | `vsworldrenderer.cpp:473-482` |
| Terrain (PBR terrain shader) | Implemented for its scope | `vsterrainshader.h`, `scripts/generate_vulkan_terrain_shader.py` |
| Sky / atmosphere | Partial | `vsworldrenderer.cpp:219-301`, `:446-452` |
| Water | Stub-level placeholder | `vsworldscene.cpp:1171-1178` |
| Transparency | Partial (sorted straight alpha; no PPLL) | `vsworldrenderer.cpp:928-941` |
| Shadows | Absent | grep `shadow` finds only cloud-shadow sky terms |
| Local / projector lights | Absent | no light-source shading in world shaders |
| Reflections / probes / mirrors | Absent | — |
| Glow / bloom | Partial (written, never composited) | `vsworldrenderer.cpp:894-907`, `:1214-1215` (no callers) |
| Post-processing, tonemap, AA, DoF, SSAO | Absent | grep finds none |
| GPU timing | Partial (per-avatar duration queries only) | `vsworldrenderer.cpp:113-145`, `:1172-1199`, `vsgputimingledger.h` |
| UI renderer | Implemented | `vsuirenderer.cpp:104-191`, `:489` |
| UI integration with shared LLUI | Implemented | `vsuidrawbridge.cpp:9-21`, `llrender2dutils.cpp:57-97` |
| Fonts | Implemented | `vsuifont*.cpp`, `vsviewerwindow.cpp:419-428` |
| Texture upload / compression / mips | Implemented | `vstextureupload.h`, `vsuirenderer.cpp:201-277` |
| Resource budgets and VRAM policy | Implemented (fixed constants) | `vsresourcebudget.h`, `vsuirenderer.cpp:84-96`, `vsworldrenderer.h:139`, `vsvulkancontext.cpp:369-397` |
| Snapshots / previews / scene monitor / pixel inspect | Implemented | `vsvulkancontext.cpp:784-839`, `:765-783`, `:651-664`, `:443-492` |
| Pixel oracle and `vkdiag` diagnostics | Implemented (diagnostic builds only) | `vsuipixeloracle.*`, `vsvkdiag.cpp` |
| Shaders | Implemented as inline GLSL, compiled at runtime | see [Shaders](#shaders) |
| Threading | Single-threaded submission | see [Threading](#threading) |

### Backend selection at startup — Implemented, no fallback by design

- **Setting.** `RenderBackend` (String, persisted, default `OpenGL`) is
  inherited from the release baseline (`app_settings/settings.xml:12285-12295`;
  the same block exists on `origin/vkstorm-release`). The Preferences combo
  writes to the non-persisted `RenderBackendPending` (`settings.xml:12296-12306`).
  The combo offers OpenGL, "Mesa/Zink" and "Vulkan (in development)"
  (`panel_preferences_graphics1.xml:967-988`).
- **Admission.** `VSRenderBackend::canStartSession` returns `true`
  unconditionally when `VS_NATIVE_VULKAN` is defined. Otherwise it returns
  `false` for `Vulkan` (`vsrenderbackend.h:11-18`). `normalize()` maps every
  value other than `Vulkan`/`Zink` to `OpenGL` (`:7-10`).
- **Startup.** Right after `initConfiguration()`, if admission fails, the viewer
  shows `MBVulkanRendererUnavailable` and returns `false` from `init()`
  (`llappviewer.cpp:1040-1046`). The message says: "Startup has stopped before
  graphics initialization; there is no OpenGL fallback"
  (`skins/default/xui/en/strings.xml:9`). The release baseline differs: it logs a
  warning and falls back to OpenGL for `Vulkan`
  (`git show origin/vkstorm-release:indra/newview/llappviewer.cpp`, line 3736-3740).
- **Native branch.** In a `VS_NATIVE_VULKAN` build with
  `RenderBackend == "Vulkan"`, `init()` sets `mNativeVulkanLogin` and returns
  `initNativeVulkanLogin()` (`llappviewer.cpp:1070-1076`). It skips the whole
  ordinary GL initialization that follows. `initWindow()` refuses to create a GL
  window when Vulkan is requested in a build without native Vulkan
  (`:4174-4180`).
- **Preferences.** `onRenderBackendCommit` refuses an unavailable backend with
  the `VulkanRendererUnavailable` notification. If the choice is accepted, it
  asks for a restart and writes `RenderBackend` only on "Shutdown now"
  (`llfloaterpreference.cpp:4138-4205`).
- **Fallback.** None, deliberately: if device, adapter or swapchain creation
  fails, the native path throws (see below) and never tries GL. This matches the
  mandate in `doc/vulkan/qualification-roadmap.md:23-30`.
- **Inference (risk):** a user whose persisted `RenderBackend` is `Vulkan` and
  who then runs a build without native Vulkan cannot reach Preferences to change
  it. They have to edit `settings.xml` or pass a command-line override.

### Native window — Partial

- `LLViewerWindow` gains a native constructor branch (`llviewerwindow.cpp:2248-2253`)
  that calls `initNativeWindow` (`vsviewerwindow.cpp:137-190`). That function
  requests `LLWindowManager::createWindow(..., GraphicsAPI::Vulkan, headless)`
  (`vsviewerwindow.cpp:141-144`).
- `LLWindowManager::createWindow` builds an `LLWindowWin32` or `LLWindowSDL`
  without a GL context (`indra/llwindow/llwindow.cpp:445-463`). It **refuses
  fullscreen**: "Native Vulkan fullscreen admission is not implemented" (`:447-451`).
- Linux requires SDL2 on X11 (`llwindowsdl2.cpp:500`,
  `vsvulkancontext.cpp:286-288`). **Wayland is not supported natively**, even
  though the Diligent patch allows Wayland surfaces.
- In both window classes, GL entry points throw `std::logic_error` when there is
  no GL context, e.g. `llwindowwin32.cpp:1242`, `:1958`, `:2015-2028`, `:3971`;
  `llwindowsdl2.cpp:987`, `:1240`, `:2805-2852`. These are guards, not features.

### Device and adapter selection (`VSVulkanContext`) — Partial

`VSVulkanContext::Impl::Impl` (`vsvulkancontext.cpp:213-318`) does the following:

1. Loads the Diligent Vulkan factory (`:217`) and enumerates adapters with a
   **minimum version of Vulkan 1.1** (`EnumerateAdapters({1,1})`, `:220-223`).
2. Requests optional features: BC texture compression, wireframe fill, duration
   queries and occlusion queries (`:225-228`).
3. **Selects the adapter automatically.** It takes the adapter with the highest
   `ADAPTER_TYPE` value (discrete > integrated > software), and the first such
   adapter on a tie (`:230`, `:237-239`). There is no setting, environment
   override or vendor preference. Hybrid-GPU systems get whatever the loader
   enumerates first among adapters of the top type.
4. Checks that the device it got matches the adapter it chose (`:242-245`).
5. Fills `renderer_info` for About and crash reports: adapter, API version,
   driver version and device-local heap MiB (`:249-275`). On Windows it reads the
   driver version through DXGI by LUID match, without creating a D3D device
   (`:139-168`).
6. Creates the swapchain: `RGBA8_UNORM` colour, **no depth attachment**, usage
   `RENDER_TARGET | COPY_SOURCE` (`:295-304`). In headless diagnostic replay it
   creates an offscreen RGBA8 target instead (`:273-277`, `:360-368`).
7. Builds the owner graph: `VSUIRenderer`, `VSUIResources`, `VSWorldRenderer`,
   `VSWorldScene`, and the selection-light, parcel, beacon and tracker
   presentations (`:306-313`). Any failure runs `cleanup()` and rethrows
   (`:315-318`).

`vsvkdiag.cpp:374-410` repeats the same adapter policy for the diagnostic
harness.

Status rationale: device creation is complete. Selection is automatic only, and
the documented 1.3 feature floor is not enforced (see
[Relation to the design study](#relation-to-the-design-study)).

### Swapchain and presentation — Implemented (windowed)

- **Resize.** `present()` compares the window extent with the swapchain size each
  frame and calls `swapchain->Resize` (`vsvulkancontext.cpp:525-532`).
- **Minimize or zero extent.** Skips drawing, flushes uploads and retires
  completed work (`:509-517`).
- **Present.** `swapchain->Present(RenderVSyncEnable ? 1 : 0)` (`:700-704`).
  Offscreen paths flush instead and call `ReleaseStaleResources` (`:705-711`).
- **Pixel inspection.** One 4-byte readback at a time, fenced and nonblocking
  (`:443-492`).
- **Not present:** fullscreen, HDR or wide-gamut swapchains, MSAA, explicit
  present-mode choice beyond the vsync interval, and handling of surface or
  device loss. Diligent handles swapchain recreation internally;
  `doc/vulkan/framework-comparison.md:77` notes that "general device-loss
  reconstruction is not proven".

### World renderer (`VSWorldRenderer`) — Partial

**Architecture.**

- A single forward pass into the swapchain backbuffer. The world depth buffer is
  a `D32_FLOAT` texture owned by the renderer, cleared to 1 and tested with
  `LESS_EQUAL` (`vsworldrenderer.cpp:888-911`, `:554`).
- An optional `RGBA16_FLOAT` "emissive glow" target (`:894-907`).
- Input is a flat `std::vector<Packet>` (`vsworldrenderer.h:53-101`), produced on
  the CPU by `VSWorldScene::packets` (`vsworldscene.cpp:3815-4982`) and by
  overlay presenters.

**Packet order.** A stable sort (`vsworldrenderer.cpp:928-941`) puts non-HUD
before HUD, background (sky) first, overlays after scene, opaque before alpha,
and alpha back-to-front by `distanceSquared`. HUD draws clear depth and switch to
an orthographic HUD projection (`:943-975`, `:1050-1057`).

**Pipelines.**

- PSOs are created lazily per key and cached in memory. The key covers target
  format, mode, topology, cull, terrain program, wireframe, custom blend, depth
  compare and depth write (`:191-199`, `:553-590`).
- Blend factors mirror `LLRender::eBlendFactor` (`:571-586`).
- There is no persistent pipeline cache. `PipelineStateCache` and
  `RenderStateCache` do not appear anywhere in `vs*.cpp` (grep).

**Per-draw work.**

- One `DrawIndexed` per packet, with no instancing (`:1179-1185`).
- One pooled uniform buffer per draw, refreshed with `UpdateBuffer` (`:1059-1068`).
- A shader resource binding, rebuilt whenever the pipeline changes (`:1070-1075`).
- `SetRenderTargets` on every draw (`:1155`).
- `DRAW_FLAG_VERIFY_ALL` on every draw (`:1162`).
- Completion is tracked with a fence (`:1207`).

**Geometry.**

- Immutable vertex and index buffers. The vertex format is 72 bytes: position,
  normal, two UV sets, tangent and colour (`vsworldrenderer.h:15-20`,
  `vsworldrenderer.cpp:53`).
- `publishMesh` keeps the old generation if the new upload is refused
  (`vsworldrenderer.h:143-145`).
- Region-local vertices plus a per-draw `regionOffset` act as a floating origin
  (`vsworldrenderer.h:88-90`).
- The renderer also supports triangle-encoded wide lines and screen-space points
  (`vsworldrenderer.cpp:316-344`).

**Culling.** `packets()` rejects objects by distance (far clip plus object size)
(`vsworldscene.cpp:3934-3935`, `:4273-4274`) and walks neighbour regions
(`:4923-4924`). **Inference:** the packet path does no view-frustum or occlusion
culling. The only frustum tests in `vsworldscene.cpp` (`:6492`, `:6582`) serve
object-visibility and selection queries. Occlusion queries are requested as a
device feature (`vsvulkancontext.cpp:228`) but no world code issues one.

**Skinning.** The world vertex shader has no joint palette
(`vsworldrenderer.cpp:200-369`). Rigged faces are republished through
`publishMesh` (`vsworldscene.cpp:4837-4846`). **Inference:** skinning runs on the
CPU and re-uploads vertices.

**Lighting and materials — Partial.**

- `VSMaterial::State` carries legacy and PBR inputs: base colour, normal,
  ORM/specular, emissive and occlusion channels, metallic, roughness, specular,
  shiny, bump, alpha mode and glow (`vsmaterial.h`).
- The fragment shader uses base colour, the normal map, occlusion, emissive and
  alpha mask, with illumination = `ambient + sunColour·max(N·L, 0)`
  (`vsworldrenderer.cpp:473-482`).
- It does **not** use metallic, roughness or specular. `surfaceParams.x/y` and
  `specular` are uploaded (`:1027`) but the only reference to them in the shader
  is `surfaceParams.w` (`:478`). The code says so itself: "ORM channels and
  legacy specular/gloss remain material inputs for R5/R6, rather than inventing
  a replacement BRDF in this forward foundation" (`:479-480`). The terrain
  generator has the same note (`scripts/generate_vulkan_terrain_shader.py`, the
  `wrapper` block: "retain roughness/metallic inputs for R4/R5's G-buffer/BRDF").
- Legacy bump codes are turned into normal maps by a GPU kernel
  (`VSUIRenderer::generateBump`, `vsuirenderer.cpp:317-437`).
- Output is manual sRGB encoding into an `RGBA8_UNORM` target (`:420-421`, `:482`). There
  is no HDR intermediate.

**Other world features.**

| Feature | Status | Evidence |
|---|---|---|
| Terrain (four-layer PBR, triplanar, paint) | Implemented for its scope | Embeds the reference `pbrterrainUtilF.glsl`. Four detail variants (`vsworldrenderer.cpp:532-552`); legacy four-swatch terrain path (`:454-467`) |
| Sky | Partial | The vertex shader ports `class1/deferred/skyV.glsl` haze and sunlight equations (`:219-301`, `:346-354`). The fragment shader adds rainbow and halo terms (`:389-411`, `:446-452`). Falls back to a fixed gradient when there is no environment (`:451`). `vsworldscene.cpp:5457` still reports `"sky"="native_default_gradient"` in evidence. Clouds, stars and the sun/moon disc are not separately rendered. **Inference**, from grep: no cloud or star geometry or shader exists. |
| Water | Placeholder | One flat quad per region at water height, colour `{.08,.28,.38,.65}`, drawn as sorted alpha (`vsworldscene.cpp:1171-1178`). No animation, reflection, refraction, fog or underwater view. |
| Transparency | Partial | Sorted straight-alpha blending with per-packet custom blends (`vsworldrenderer.cpp:571-586`, `:928-941`). No PPLL or order-independent transparency. |
| Glow | Partial / effectively inert | `glowOnly` and alpha packets draw into an `RGBA16F` target (`:894-907`, `:983-997`). `VSWorldRenderer::glowView()` (`:1214-1215`) has **no callers** in `indra/` or `tests/` (grep `glowView`). The glow never reaches the presented image. `RenderGlow` only enables writing it (`vsvulkancontext.cpp:578`). |
| Shadows (sun, cascades, projector) | Absent | grep `-i shadow` in `vsworldrenderer.cpp` finds only cloud-shadow sky terms (`:227`, `:294-297`) |
| Local point, spot or projector lights | Absent | `getIsLight` appears only in edit/selection code (`vseditselection.cpp:818`, `vsselectionlightpresentation.cpp:38`) |
| Reflection probes, SSR, mirrors | Absent | — |
| Post-processing (tonemap, exposure, FXAA/SMAA, DoF, SSAO, gamma) | Absent | grep finds no `fxaa`, `tone.?map`, `ssao`, DoF |
| MSAA | Absent | no sample count in the swapchain or world target (`vsvulkancontext.cpp:298-302`, `vsworldrenderer.cpp:890`) |
| Wireframe | Implemented (optional feature) | `view.wireframe = gUseWireframe` (`vsvulkancontext.cpp:577`), `WireframeFill` (`:226`) |

**GPU timing — Partial.**

- `QUERY_TYPE_DURATION` queries wrap avatar and attachment draws only
  (`vsworldrenderer.cpp:121-145`, `:1172-1199`). Results go into the shared
  avatar-complexity statistics through `VSGPUTimingLedger` (`vsgputimingledger.h`).
- Per-pass GPU timing is absent. So are debug groups and labels:
  `BeginDebugGroup` and `InsertDebugLabel` are not used anywhere (grep).
- CPU frame profiling (`VS_VULKAN_FRAME_PROFILE`) exists only in diagnostic
  builds (`vsvulkancontext.cpp:192-197`, `:717-741`).

### UI renderer (`VSUIRenderer`, `VSUIResources`, draw bridge) — Implemented

- **Packets.** A packet is a textured quad or 6-vertex triangle set with clip,
  affine transform, blend (straight, premultiplied, additive, additive-alpha,
  replace), sampling (nearest, linear, anisotropic), an alpha-mask mode and an
  optional world depth for billboards (`vsuirenderer.h:24-42`).
- **Pipelines.** Lazily built PSOs keyed by format, blend, sampling, mask and
  world-depth flag. Scissor is enabled, cull mode is none, and the sampler clamps
  at LOD 0 (`vsuirenderer.cpp:104-191`). The shaders are four tiny GLSL
  programs: `vs`, `depth_vs`, `fs` and `mask_fs` (`:112-142`).
- **Integration with shared LLUI.** The shared `LLRender2D` immediate-mode
  helpers get native sinks: `setNativeUI` and `setNativeTriangles`, active while
  a `VSUIDrawBridge` is in scope (`indra/llrender/llrender2dutils.cpp:57-97`,
  `vsuidrawbridge.cpp:9-21`).
  - `gl_rect_2d` and similar helpers branch on `LLRender2D::isNativeUI()`
    (`llrender2dutils.cpp:181-403`).
  - The original vertex sequences live in `indra/llrender/vsuiprimitive.h`
    (543 lines), "Shared reference immediate-mode UI geometry. Both renderer
    peers execute these original vertex/color sequences" (`:1-6`).
  - These hooks are runtime checks, not `#if` guards. **Inference:** they are
    compiled into every build, including Release.
- **Frame use.** `drawNativeUI` lays out and updates the real `LLRootView`.
  `present()` then calls `resources->begin`, the draw callback
  (`tool->draw(); mRootView->draw(); drawDebugText()`), `resources->finish()` and
  `renderer->draw` (`vsviewerwindow.cpp:404-415`, `vsvulkancontext.cpp:667-680`).
- **World-attached UI.** Name tags and RLVa overlays are drawn between the world
  and the HUD through the `beforeHud` callback, sharing world depth
  (`vsvulkancontext.cpp:615-635`).
- **Fonts.** The shared `LLFontGL`/FreeType rasterizer feeds append-only font
  atlases (`createFontAtlas`/`updateFontAtlas`, `vsuirenderer.h:61-62`). A DPI
  change re-initializes fonts and releases font pages (`vsviewerwindow.cpp:419-450`).
- **Pixel oracle.** `vsuipixeloracle.cpp` (172 lines) is an independent CPU
  rasterizer of the same packets, used by diagnostics to compare against GPU
  output within a tolerance (`vsuipixeloracle.h:1-12`). It is compiled only with
  `VS_VULKAN_DIAGNOSTICS` (`CMakeLists.txt:2402`), plus the standalone test
  (`tests/software_vulkan/CMakeLists.txt:26`).
- **Name check.** No `vsuiprimitive*` exists under `indra/newview`. The name
  belongs to `indra/llrender/vsuiprimitive.h`.

### Texture upload and resource budgets — Implemented (fixed constants)

- **Upload.**
  - `VSTextureUpload::prepare` builds mip chains on a worker and can BC3-encode
    them with `stb_dxt` (`vstextureupload.h:1-60`). Compression requires the
    optional `TextureCompressionBC` device feature (`vsuirenderer.cpp:204-209`).
  - Remote images are prepared on the `General` work queue and published on
    `mainloop` (`vsuiimageprovider.cpp:158-185`).
  - Images are immutable generations. `replace()` builds a new generation
    (`vsuirenderer.cpp:439-487`). Only font atlases update in place.
- **Budgets.** `VSResourceBudget` is a lock-free byte reservation; a charge is
  released when the last owner drops it (`vsresourcebudget.h:1-51`). The limits
  are hard-coded constants:

  | Budget | Limit | Evidence |
  |---|---|---|
  | Core UI images | 256 MiB | `vsuirenderer.cpp:84` |
  | Remote and media images | 512 MiB | `vsuirenderer.cpp:84` |
  | Avatar composites | 1 GiB | `vsuirenderer.cpp:88` |
  | World meshes | 256 MiB (the constructor default; `vsvulkancontext.cpp:308` passes none) | `vsworldrenderer.h:139` |
  | Mesh snapshots | same as world meshes | `vsworldrenderer.cpp:167-168` |

- **VRAM policy.** `VSTextureMemory::Policy` ports `LLViewerTexture::updateClass`
  (`vstexturememory.h:1`). Each frame it is fed the detected device-local heap
  size, the existing `FSOverrideVRAMDetection`, `RenderMaxVRAMBudget` and related
  settings, mesh residency and system-memory state
  (`vsvulkancontext.cpp:369-397`, `:502`). Its discard bias drives
  `refreshInactiveImages` while minimized (`:514`).
- **Inference:** the fixed 256/512/1024 MiB caps do not scale with the detected
  VRAM. Only the discard-bias policy does.

### Snapshots, previews, scene monitor, pixel inspection — Implemented

- **Snapshots.** `VSVulkanContext::snapshot` renders into a dedicated RGBA8
  target, can omit UI or HUD, and reads back colour or depth. Depth is converted
  to the GL depth convention (`vsvulkancontext.cpp:784-839`). The shared snapshot
  floaters reach it through `vssnapshot.cpp:87`, `:159`, and the native loop
  calls `LLFloaterSnapshot::update()` once per frame (`llappviewer.cpp:1786-1795`).
- **Previews.** `createPreviewRenderer` and `renderPreview` give previews their
  own `VSWorldRenderer` on the same device. They render to an immutable image
  (`vsvulkancontext.cpp:765-783`) and are used by `vsscenepreview.cpp:42`, `:116`.
- **Scene monitor.** `VSSceneMonitor` captures and compares frames for
  `LLSceneMonitor` (`vsvulkancontext.cpp:651-664`, `vsscenemonitor.cpp:104-105`).
- **Pixel inspection.** The debug colour inspector uses `inspectPixel`
  (`llviewerwindow.cpp:979-980`).

### Diagnostics harness (`vsvkdiag`) — Implemented, diagnostic builds only

- `VSVulkanDiagnostic` (`vsvkdiag.cpp`, 1,676 lines) is entered only when
  `VS_VULKAN_DIAGNOSTICS` is compiled in **and** `VS_VULKAN_DIAGNOSTIC=<dir>` is
  set. It replaces normal `init()` and `frame()` (`llappviewer.cpp:903-913`,
  `:1945-1948`).
- It creates its own device and swapchain (`vsvkdiag.cpp:374-410`). It runs
  window, presentation, UI-fixture and skin/theme/language cases selected by
  environment variables (`:296-298`, `:1051-1053`), and checks results with the
  pixel oracle.
- Headless replay (`VS_VULKAN_DIAGNOSTIC_HEADLESS=1` with
  `VS_VULKAN_DIAGNOSTIC_REPLAY`) swaps the swapchain for an offscreen target
  (`vsvulkancontext.cpp:748-756`).
- Release-hook policy: AGENTS.md requires these development-only hooks to stay
  out of `vkstorm-release`. They are confined behind `VS_VULKAN_DIAGNOSTICS`,
  which defaults OFF.

### Shaders

| Program | Location | Language and creation |
|---|---|---|
| World vertex + fragment (objects, sky, legacy terrain, lines, points) | Inline raw strings, `vsworldrenderer.cpp:200-369`, `:370-483` | GLSL 4.5 source, compiled at runtime by Diligent (`SHADER_SOURCE_LANGUAGE_GLSL`, `:487-493`). The uniform-block ABI is checked against C++ structs through reflection (`checkWorldABI`, `:54-74`, `:495-507`). |
| Terrain vertex | The same source with `#define VS_TERRAIN_PASS` (`:510-531`) | Runtime GLSL |
| Terrain PBR fragment (4 detail variants) | `vsterrainshader.h` (632 lines, generated) | Generated by `scripts/generate_vulkan_terrain_shader.py` (141 lines). It embeds the shared reference `app_settings/shaders/class1/deferred/pbrterrainUtilF.glsl`, rewrites samplers to separate texture/sampler objects, and splits the result into MSVC-safe raw-string chunks. `--check` mode runs in CI (`software_vulkan.yml:100`) and in `scripts/tests/test_terrain_materials.py:86`. |
| UI vertex, depth-vertex, fragment, mask-fragment | `vsuirenderer.cpp:112-142` | Runtime GLSL |
| Bump/normal-map generator | `vsuirenderer.cpp:322-373` | Runtime GLSL |
| Scene-monitor compare | `vsscenemonitor.cpp:104-105` | Runtime GLSL |

- No native shader files exist on disk, there is no offline SPIR-V step, and
  there is no shader cache.
- The roughly 225 legacy GLSL files under `app_settings/shaders` are not used by
  the native path. The one exception is `pbrterrainUtilF.glsl`, embedded at
  generation time.
- **Inference:** shaders compile on first use of each PSO key, during a frame, so
  first-use stalls are likely. Nothing was measured.

### Threading

- All GPU submission goes through one Diligent **immediate context** on the main
  thread (`vsvulkancontext.cpp:240`). `NumDeferredContexts` does not appear in
  `vs*.cpp` (grep).
- CPU work runs on the viewer's work queues: image preparation on `General`,
  publication on `mainloop` (`vsuiimageprovider.cpp:165-185`, `vsworldgltf.cpp:489`,
  `:665`), plus the existing texture fetch, cache and decode threads started in
  `initNativeVulkanLogin` (`llappviewer.cpp:1607-1616`).
- Resource retirement is fence-based and nonblocking (`VSUIRenderer::retire`,
  `vsuirenderer.cpp:735`; world fence, `vsworldrenderer.cpp:1207`). A blocking
  `WaitForIdle` happens only for DPI or font reloads, snapshot readback and
  teardown (`vsvulkancontext.cpp:422`, `:840-845`, `vsviewerwindow.cpp:422`,
  `:438`).

## Frame flow

The flow from the main loop to present, in a `USE_DILIGENTCORE=ON` build with
`RenderBackend=Vulkan`:

```
LLAppViewer::frame()                                llappviewer.cpp:1943-1984
 └─ frameNativeVulkanLogin()   (instead of doFrame) llappviewer.cpp:1700-1828
     ├─ perf stats, frame counters, timers           :1702-1720
     ├─ window events, keyboard, mouse scan           :1721-1733
     ├─ mainloop pump, coroutines, WorkQueue 2 ms     :1736-1739
     ├─ LFS / decode / texture cache / fetch update   :1740-1743
     ├─ service pump, mesh repo, idle callbacks       :1744-1748
     ├─ inventory / avatar-tracker observers          :1749-1752
     ├─ startup state machine (idle_startup)          :1760
     │   or VSNativeSession::begin()/tick()           :1762-1763   (CPU world/session)
     ├─ AFK, agent flags, account services, audio     :1765-1778
     ├─ media focus, snapshot floater updates         :1780-1795
     ├─ LLDrawable::incrementVisible()                :1800-1804
     └─ gViewerWindow->drawNativeUI()                 :1807
         └─ LLViewerWindow::drawNativeUI()            vsviewerwindow.cpp:351-417
             ├─ DPI check, publishCamera, updateUI, layout fixes
             └─ VSVulkanContext::present(dpi, drawUI)  vsvulkancontext.cpp:760-764 → Impl::present :494-741
                 ├─ updateTextureMemory()                          :502
                 ├─ minimized/zero extent → flush+retire, return   :509-517
                 ├─ swapchain resize if extent changed              :525-532
                 ├─ bind backbuffer, clear to (16,32,48)            :535-538
                 ├─ if session Connected:
                 │   ├─ camera/view/environment state               :563-586
                 │   ├─ worldScene->packets(); particles; alpha highlights;
                 │   │   parcel/selection/beacon/tracker/HUD-icon packets  :591-605
                 │   └─ worldRenderer->draw(target, …, beforeHud=nametags+RLV overlay) :615-635
                 ├─ LLSceneMonitor capture/compare                  :651-664
                 ├─ UI: resources->begin; beacon labels; draw();    :667-678
                 │   resources->finish(); renderer->draw(packets)   :680
                 ├─ debug pixel readback request                    :699
                 ├─ swapchain->Present(vsync)                       :700-704
                 └─ renderer->retire(); worldRenderer->retire()     :715-716
     ├─ background-yield sleep when unfocused/minimized   :1810-1815
     └─ FPS-limit sleep                                    :1816-1824
```

Notes:

- The world is drawn **only** when `VSNativeSession` is in phase `Connected`
  (`vsvulkancontext.cpp:561`). Before that, login and progress screens are
  UI-only.
- No GL code, `gPipeline` render path or `display()` runs. The native loop calls
  only `gPipeline.initializeRenderDebugFeatures()` and `initializeRenderTypes()`
  for shared CPU policy (`llappviewer.cpp:1592-1593`) and reads its debug-feature
  masks (`vsvulkancontext.cpp:597`, `:619`).
- An exception from `require()` inside `present()` is a `std::runtime_error`.
  `frame()` catches only `LLContinueError` and `std::bad_alloc`
  (`llappviewer.cpp:1953-1981`). **Inference:** any GPU-side failure during a
  frame ends the process through the unhandled-exception path. There is no
  recovery.

## Relation to the design study

`doc/vulkan/` is a large design and evidence set (README 117 lines,
architecture 159, framework-comparison 176, qualification-roadmap 198,
software-vulkan-ci 463, full-parity-roadmap 331, world-feature-parity 198, plus
catalogs). Its main claims compare with the code as follows.

| Design claim | Source | Code reality | Verdict |
|---|---|---|---|
| DiligentCore is the selected GHI; the viewer keeps scene/pass/material semantics and Diligent supplies device, PSOs, descriptors and barriers. | `README.md:4`, `:57`; `architecture.md:21` | Diligent Vulkan backend pinned and linked. Viewer-owned packets, sort and materials. Diligent PSOs, SRBs, transitions and fences. | **Implemented** |
| Vulkan is a peer backend; no OpenGL fallback; failures reported and the session stopped. | `qualification-roadmap.md:23-30` | `canStartSession`, `MBVulkanRendererUnavailable`, no GL path in the native branch | **Implemented** |
| Create the device through `CreateDeviceAndContextsVk` and the swapchain from the viewer's native window with `CreateSwapChainVk`; start with one immediate context. | `framework-comparison.md:47` | Exactly this (`vsvulkancontext.cpp:240`, `:303`) | **Implemented** |
| Proposed initial floor of **Vulkan 1.3**, with `synchronization2`, `dynamicRendering` and `timelineSemaphore` queried. | `architecture.md:110`; `README.md:115` | `EnumerateAdapters({1,1})` gives a **1.1** floor (`vsvulkancontext.cpp:220`). `queryDriverProperties` also says "The normal native device requires Vulkan 1.1" (`:115-117`). None of those features is requested. | **Diverges** (the docs call it a proposal) |
| Two frames in flight, histories by rendered-view frame serial. | `architecture.md:114` | Frame pacing is left to Diligent; the viewer only fences retirement. No temporal histories exist. | **Not applicable yet / Absent** |
| GLSL math retained, rewritten as explicit Vulkan modules compiled to SPIR-V with one ABI schema. | `architecture.md:120`; `framework-comparison.md:129` | Inline GLSL 4.5 compiled at runtime by Diligent. ABI checked by reflection at PSO creation. No offline SPIR-V. | **Partial** |
| PSO key covers variant ABI, formats, topology, raster, depth, blend; persistent PSO cache keyed by device, driver and shader. | `architecture.md:124` | The key exists (`vsworldrenderer.cpp:196-198`). No persistent cache. | **Partial** |
| Debug groups, labels and GPU timestamps per named pass. | `framework-comparison.md:142` | Only per-avatar duration queries. No labels. | **Partial** |
| Render graph / pass plan owned by the viewer. | `render-graph-design-outline.md:6` ("outline, not yet written") | One hard-coded forward pass plus a glow side-target | **Absent** (design itself unwritten) |
| G-buffer → deferred lighting, PPLL transparency, shadows, probes, SSR, post. | `architecture.md:135-147`; `qualification-roadmap.md:56-60` | None present (see the world feature table above) | **Absent** |
| First deliverable requires **full supported rendering parity** including lighting, shadows, water, transparency and post-processing. | `README.md:8-10`; `qualification-roadmap.md:56-63` | Forward sun/ambient only; placeholder water; no shadows or post | **Not met**, consistent with the docs' own "world rendering remains open" (`README.md:51`) and "not achieved" (`world-feature-parity.md:18-20`) |
| Optional FSR1 / DLSS through Diligent SuperResolution. | `README.md:61`; `framework-comparison.md:152-157` | The package is built with `DILIGENT_NO_SUPER_RESOLUTION=ON` (`build_vulkan_dependencies.py:131`) | **Absent** (deliberately excluded) |
| UI/chat checkpoint passed on all three software platforms in CI run 38012380599. | `README.md:45-49` | Historical claim. At `51d1f729a6` the graphics jobs fail before reaching viewer tests (below). | **Stale at this revision** |
| "first authenticated native frames at approximately 14 FPS" | `world-feature-parity.md:12-13` | A user observation recorded in the doc. It was not reproduced for this survey and is not a measurement claim of this chapter. | Unverified |

Stale statements found in the documentation (not in the code):

- `DiligentCore.cmake:1` ("does not enable a native renderer").
- `3p/README.md:3-6` ("still renders with its existing OpenGL pipeline", "disabled Vulkan entry").
- `architecture.md:4-6` ("First implement native Vulkan UI and connected chat with
  no world rendering"). This was superseded by the scope expansion in
  `qualification-roadmap.md:32-41`.
- `world-feature-parity.md:38-48`, the first-increment table. It describes a
  "Fixed +X region camera" and "Avatar … Not admitted"; later code has camera,
  avatar and glTF producers (`vsworldavatar.cpp`, `vsworldgltf.cpp`). The table
  is labelled as the first increment, but it reads as current status.

## Known breakage

1. **CI `graphics` jobs fail (supplied CI fact).** At `51d1f729a6`, the
   `source-evidence` job passes (`software_vulkan.yml:24`). All `graphics`
   matrix jobs (`:510-527`: Windows SwiftShader, Linux SwiftShader, Linux
   Lavapipe) fail at "Build standalone staged Diligent tests" (`:602`).
   - Cause: `tests/software_vulkan/CMakeLists.txt:24-31` compiles
     `vsworldrenderer.cpp`, `vsuipixeloracle.cpp` and `vsuirenderer.cpp`
     standalone. It defines `LL_LLVIEWERPRECOMPILEDHEADERS_H=1` so the viewer PCH
     is skipped, and states (`:28`) "These production renderer sources depend
     only on the GHI and standard library".
   - Two sources break that rule (`git blame` attributes both to `116a410fc8`,
     "Implement connected UI and R3 service checkpoint [skip ci]", 2026-10-18):
     - `vsworldrenderer.cpp:3` includes `llperfstats.h`, used at `:1163-1170`
       (`LLPerfStats::RecordTime`/`RecordSceneTime` around avatar and HUD draws).
     - `vsuirenderer.cpp:4` includes `llimagegl.h`, used at `:108`
       (`LLImageGL::sGlobalUseAnisotropic`).
   - Because every later step in the job depends on this one, the
     staged-viewer presentation, connected-UI, R1, R2 and R3 replay steps
     (`:735-972`) do not run. **This revision therefore has no current CI
     evidence for any native rendering behaviour.** The `[skip ci]` tag hid the
     regression when it landed.
   - Fix direction (Inference): inject the anisotropy flag and the perf-stat
     hooks through the renderer's constructor or callbacks, so the GHI-only
     contract holds again.
2. **Glow target is dead output.** It is allocated, cleared and drawn into every
   frame that has glow packets, but never sampled (`vsworldrenderer.cpp:894-907`;
   `glowView()` has no callers). This costs GPU work and an `RGBA16F`
   full-screen allocation with no visible effect.
3. **Linux Wayland sessions and fullscreen cannot start the native renderer**
   (`llwindowsdl2.cpp:500`, `llwindow.cpp:447-451`). These are explicit
   refusals, not crashes.
4. **No device-loss handling** (see Frame flow). **Inference:** a lost device
   ends the session through an unhandled exception.

## Risks and open questions

- **Not shipped and no current evidence.** The native renderer is compiled only
  in the diagnostic CI configuration, and that configuration currently fails
  before the viewer is built. No artifact exists in which a user could select
  Vulkan and get a frame. Can a non-diagnostic `USE_DILIGENTCORE=ON` build be
  produced and staged at all, given that the packages are not published
  (`3p/README.md:55-58`)?
- **Adapter choice.** The highest-type-wins rule with no override
  (`vsvulkancontext.cpp:237-239`) is a risk on hybrid laptops and multi-GPU
  Linux systems. The docs require "hybrid/mixed-adapter selection" qualification
  (`README.md:65`). The code has no knob for it.
- **API floor.** The code admits Vulkan 1.1 devices while the design proposes
  1.3. Which one is the contract? Nothing checks optional features beyond the
  four listed.
- **Per-draw overhead (Inference, not measured).** Each packet does its own
  `SetRenderTargets`, `UpdateBuffer`, SRB commit and `DrawIndexed`, with
  `DRAW_FLAG_VERIFY_ALL`. The Diligent package is built with
  `DILIGENT_DEVELOPMENT=ON`. Add CPU skinning with re-upload and no frustum
  culling, and CPU cost will scale with scene size.
- **Runtime shader compilation.** It runs on first use, mid-frame, with no
  persistent cache.
- **Fixed budgets.** The 256 MiB, 512 MiB and 1 GiB caps are independent of the
  detected VRAM. Low-VRAM devices could oversubscribe, while high-VRAM devices
  are artificially limited.
- **LDR-only output.** There is no HDR intermediate, so exposure, tonemapping,
  bloom and the planned FSR/DLSS (which the design places before tonemap) would
  need a new frame structure. The render-graph design is still "outline, not yet
  written" (`render-graph-design-outline.md:6`).
- **Settings recovery.** A persisted `RenderBackend=Vulkan` on a non-native
  build blocks startup with no in-viewer way back (Inference, from
  `llappviewer.cpp:1040-1046`).
- **Shared-code footprint.** The 1,300 `VS_NATIVE_VULKAN` lines in 234 shared
  `newview` files, plus the unconditional native hooks in `llrender2dutils.cpp`
  and the GL-guard throws in `llwindow`, are always compiled into this line.
  Whether they are all safe in OpenGL-only builds is not established here.
- **Open question.** Which of the reference-ported features (sky equations, PBR
  terrain sampler, blend factors) have pixel-level parity evidence after
  `116a410fc8`? Every replay that would show it currently sits downstream of the
  broken CI step.
