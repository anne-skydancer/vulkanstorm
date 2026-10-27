# 04 — Rendering, windowing and imaging

This chapter covers everything between decoded pixels and the swapped frame:
the GL wrapper library `indra/llrender`, the platform window and context layer
`indra/llwindow`, the image containers and codecs `indra/llimage` with their
JPEG 2000 back ends (`llimagej2coj`, `llkdu`), and the viewer's render pipeline
in `indra/newview` (LLPipeline, draw pools, spatial partitions, viewer textures,
GLTF/PBR, reflection probes, shadows, environment, post-processing, camera,
display and the render side of LLViewerWindow). On this line (`vkstorm-rust`,
branch `codex/rust-inventory-model`) the raster codecs other than JPEG
(PNG/TGA/BMP/DXT), the JPEG 2000 codec, the texture cache and the texture fetch
state machine are already Rust behind `vkstorm-ffi`; nothing in `llrender`,
`llwindow`, the pipeline or the shaders calls Rust. The renderer is the LL
upstream deferred PBR OpenGL pipeline with Firestorm additions and three
Vulkanstorm additions: the Mesa/Zink GL-over-Vulkan loader, a per-pixel linked
list (PPLL) alpha order-independent transparency path, and a render-backend
selector whose "Vulkan" value falls back to OpenGL. The renderer source is
byte-identical to the baseline audited by the separate native-Vulkan design
study (`doc/vulkan/`), so that study's line references apply here unchanged.

All counts below were taken at `HEAD` = `8d101eccb6` of
`codex/rust-inventory-model` on 2026-10-27. Paths are relative to `indra/`
unless they start with `doc/`, `scripts/` or `.github/`.

## Scope and size

### Libraries

Command (per directory `D`, run from `indra/`):

```sh
git ls-files D | grep -E '\.(cpp|h|mm|inl)$' | grep -v '/tests/' | xargs cat | wc -l
```

| Directory | Source files | Source lines | Test files | Test lines | Origin |
|---|---:|---:|---:|---:|---|
| `llrender` | 52 | 28,068 | 0 | 0 | LL upstream, 74 `<FS` markers, 2 Vulkanstorm PPLL hunks in `llshadermgr.cpp` |
| `llwindow` | 43 | 22,579 | 1 | 55 | LL upstream, 111 `<FS` markers, Vulkanstorm `lllinuxzink.h` and its test |
| `llimage` | 21 | 7,752 | 2 | 569 | LL upstream containers; PNG/TGA/BMP/DXT bodies replaced by Rust calls |
| `llimagej2coj` | 2 | 183 | 1 | 204 | Rewritten by Vulkanstorm as the thin C++ side of the Rust J2C codec |
| `llkdu` | 5 | 2,053 | 1 | 327 | LL upstream Kakadu glue; only built with `USE_KDU` (see Risks) |

`<FS` marker counts: `git grep -o -E '<FS[:>/ ]' -- <path> | wc -l`.

### `llrender` by group

Command: `ls <files> | xargs cat | wc -l` inside `indra/llrender`.

| Group | Files | Lines | Main files |
|---|---:|---:|---|
| GL state, capability manager, headers | 7 | 4,912 | `llgl.*`, `llglheaders.h`, `llglstates.h`, `llgltypes.h`, `llglcommonfunc.*` |
| Immediate-mode `LLRender`, 2D utils, primitives | 8 | 5,128 | `llrender.*`, `llrender2dutils.*`, `llrendernavprim.*`, `llrendersphere.*` |
| Vertex buffers | 2 | 2,288 | `llvertexbuffer.*` |
| Shaders | 4 | 4,496 | `llglslshader.*`, `llshadermgr.*` |
| Textures, GL images, cube maps, UI images | 15 | 4,977 | `llimagegl.*`, `llgltexture.*`, `lltexture.*`, `llcubemap*.*`, `lluiimage.*`, `lltexturemanagerbridge.*` |
| Render targets, legacy post | 4 | 1,504 | `llrendertarget.*`, `llpostprocess.*` |
| Fonts | 12 | 4,763 | `llfontgl.*`, `llfontfreetype.*`, `llfontfreetypesvg.*`, `llfontregistry.*`, `llfontbitmapcache.*`, `llfontvertexbuffer.*` |

### `llwindow` by group

| Group | Files | Lines | Built when |
|---|---:|---:|---|
| Win32 (`llwindowwin32.*`, `llkeyboardwin32.*`, `lldragdropwin32.*`, `lldxhardware.*`) | 8 | 7,011 | `WINDOWS` |
| SDL2 (`llwindowsdl2.*`, `llkeyboardsdl2.*`) | 4 | 3,789 | `LINUX` and not `USE_SDL1` (`llwindow/CMakeLists.txt`) |
| SDL1 (`llwindowsdl.*`, `llkeyboardsdl.*`) | 4 | 3,699 | `LINUX` and `USE_SDL1` |
| macOS (`llwindowmacosx*`, `llopenglview-objc.*`, `llappdelegate-objc.h`, `llkeyboardmacosx.*`) | 9 | 5,162 | `DARWIN` |
| Common, headless, Mesa headless, callbacks, keyboard base, Zink loader | 18 | 2,918 | always / `BUILD_HEADLESS` |

### Viewer render pipeline (`indra/newview`)

Command, with `g` a shell function over `git ls-files <patterns> | grep -E
'\.(cpp|h|inl)$' | sort -u` piped to `wc -l` (files) and `xargs cat | wc -l`
(lines), run in `indra/newview`:

| Group | Patterns | Files | Lines |
|---|---|---:|---:|
| Pipeline | `pipeline.cpp pipeline.h` | 2 | 14,032 |
| Draw pools | `lldrawpool*` | 26 | 8,632 |
| Spatial partition, octree, drawables, faces | `llspatialpartition.* llvieweroctree.* lldrawable.* llface.*` | 8 | 12,571 |
| Viewer textures, texture list, fetch, cache, dynamic textures | `llviewertexture.* llviewertexturelist.* lltexturefetch.* lltexturecache.* llviewertextureanim.* lldynamictexture.* llviewerdynamictexture* lltextureview.*` | 14 | 14,141 |
| GLTF / PBR | `gltf/* gltfscenemanager.* llgltfmateriallist.* llfetchedgltfmaterial.* llgltfmaterialpreviewmgr.* llviewergltf*` | 20 | 10,464 |
| Shader manager | `llviewershadermgr.*` | 2 | 3,991 |
| Reflection and hero probes | `llreflectionmap* llheroprobemanager.*` | 6 | 3,355 |
| Environment, sky, water | `llsettingsvo.* llenvironment.* llsky.* llvosky.* llvowater.* llvowlsky.* llvoground.*` | 12 | 9,606 |
| Camera, display, viewer window | `llviewercamera.* llviewerdisplay.* llviewerwindow.*` | 6 | 11,430 |
| **Total of the groups** | | **96** | **88,222** |

Not counted above but render-adjacent: `llvovolume.cpp` (7,313 lines, holds
`LLVolumeGeometryManager`), `llvoavatar.cpp` (13,389), `llviewerobject.cpp`
(8,352), `llglsandbox.cpp` (1,183), `llscenemonitor.cpp` (764),
`llfeaturemanager.cpp` (867) with `featuretable.txt`, `featuretable_linux.txt`,
`featuretable_mac.txt`; the settings data model `llinventory/llsettings*.cpp`;
`llprimitive/llgltfmaterial.*`; avatar baking in `llappearance/lltexlayer.*`.
These belong to the object-store, avatar and asset chapters.

Draw pool files by size (`wc -l lldrawpool*.cpp | sort -rn`): alpha 1,141,
terrain 1,118, bump 1,085, avatar 969, base `lldrawpool.cpp` 922, wlsky 521,
water 358, materials 289, simple 215, tree 168, pbropaque 95, waterexclusion
79, sky 57.

### Shaders

Command (in `indra/newview/app_settings/shaders`):
`git ls-files '*.glsl' | awk -F/ 'NF>=3{print $1"/"$2} NF<3{print "(root)"}' | sort | uniq -c`.
225 GLSL files, 25,653 lines (`git ls-files '*.glsl' | xargs cat | wc -l`).
`class1`/`class2`/`class3` are quality tiers; `LLShaderMgr::loadShaderFile`
(`llrender/llshadermgr.cpp:462`) searches from the requested class downwards.

| Family | class1 | class2 | class3 | Total |
|---|---:|---:|---:|---:|
| `deferred` (G-buffer, lighting, shadows, sky, post, PBR, SMAA/FXAA/CAS, OIT resolve) | 100 | 7 | 15 | 122 |
| `interface` (UI, debug, copy, probes generation, occlusion, highlight) | 44 | 3 | 0 | 47 |
| `objects` (forward simple/bump/preview) | 14 | 0 | 0 | 14 |
| `lighting` | 9 | 0 | 2 | 11 |
| `windlight` (atmospherics) | 8 | 0 | 0 | 8 |
| `avatar` | 6 | 0 | 0 | 6 |
| `environment` (water, underwater, sRGB) | 4 | 0 | 2 | 6 |
| `effects` (glow) | 4 | 0 | 0 | 4 |
| `post` (Firestorm/Exodus vignette, snapshot frame) | 3 | 0 | 0 | 3 |
| `gltf` (scene-manager PBR) | 2 | 0 | 0 | 2 |
| root (`errorF.glsl`, `errorV.glsl`) | | | | 2 |
| **Total** | 194 | 10 | 19 | **225** |

Program objects: `llviewershadermgr.h` declares 127 `extern LLGLSLShader`
globals (`grep -c -E '^extern LLGLSLShader' llviewershadermgr.h`), 9 of them
arrays (FXAA/SMAA ×4, multi-light, material variants
`LLMaterial::SHADER_COUNT*2`, PBR terrain paint types, normal debug); 131
`createShader()` calls in `llviewershadermgr.cpp`. The Vulkan study counts 690
registration/variant rows (`doc/vulkan/shader-registration.csv`).

Identified non-LL shaders: `post/exoPostBaseV.glsl` and `post/exoVignetteF.glsl`
(`gPostVignetteProgram`, "Import Vignette from Exodus", `<FS:CR>` at
`newview/llviewershadermgr.cpp:138`), `post/snapshotFrameF.glsl`
(Firestorm snapshot frame), `deferred/rlvF.glsl`/`rlvV.glsl` (`gRlvSphereProgram`,
RLVa), and `deferred/alphaOITResolveF.glsl` (Vulkanstorm PPLL). Inference: the
remaining shaders, including SMAA and CAS, are LL upstream; the file headers
were not checked one by one.

## Components

### Key classes

| Class | Declared | Responsibility | Origin |
|---|---|---|---|
| `LLGLManager` (`gGLManager`, `llgl.cpp:996`) | `llrender/llgl.h:65` | Context capability probe: version, vendor, extensions, VRAM; `initGL` `llgl.cpp:1075`, `initWGL` `llgl.cpp:1031` | LL |
| `LLGLState`, `LLGLEnable`, `LLGLDisable`, `LLGLDepthTest`, `LLGLS*` | `llgl.h:237-302`, `llglstates.h:35-169` | RAII GL capability toggles with a shadow-state checker `LLGLState::checkStates` (`llgl.cpp:2511`) | LL |
| `LLGLFence`, `LLGLSyncFence` | `llgl.h:386`, `:398` | Fence wrappers used by threaded uploads | LL |
| `LLRender` (`gGL`, **`thread_local`**, `llrender.cpp:52`, `llrender.h:556`) | `llrender.h:287` | Immediate-mode emulation (`begin`/`end`/`flush` `llrender.cpp:1572/1591/1608`), matrix stacks and `syncMatrices` (`:1003`), blend/colour mask, texture units | LL |
| `LLTexUnit`, `LLLightState` | `llrender.h:63`, `:242` | Per-unit binding cache and filter/address state; legacy light state | LL |
| `LLVertexBuffer` | `llvertexbuffer.h:92` | VBO/IBO allocation (`allocateBuffer` `llvertexbuffer.cpp:1256`, `createGLBuffer` `:1140`), mapping (`mapVertexBuffer` `:1292`, `flush_vbo` `:1372`, `unmapBuffer` `:1410`), attribute binding (`setBuffer` `:1679`), draws (`drawRange` `:917`, `drawArrays` `:942`); `initClass(LLWindow*)` `:955` | LL |
| `LLGLSLShader`, `LLShaderFeatures`, `LLShaderUniforms` | `llglslshader.h:130`, `:36`, `:68` | Program object, uniform/sampler maps (`mapUniforms` `llglslshader.cpp:850`), `bind` (`:1049`, variant `:1081`, rigged `:1088`), `bindTexture` (`:1117-1177`) | LL |
| `LLShaderMgr` | `llshadermgr.h:33` | Source assembly with `#define` injection (`loadShaderFile` `llshadermgr.cpp:462`), link (`:970`), reserved attribute/uniform tables (`initAttribsAndUniforms` `:1219`), program binary cache (`loadCachedProgramBinary` `:1138`, `saveCachedProgramBinary` `:1185`) | LL; PPLL sampler/gate hunks at `:656`, `:1577` are Vulkanstorm |
| `LLImageGL`, `LLImageGLThread` | `llimagegl.h:60`, `:346` | GL texture object: create (`createGLTexture` `llimagegl.cpp:1498/1532/1631`), upload (`setManualImage` `:1319`, `setSubImage` `:1124/1233`), readback (`readBackRaw` `:1812`), `scaleDown` (`:2468`), destroy (`:1943`); background upload thread on a shared context (`:2629-2661`) | LL |
| `LLGLTexture`, `LLTexture` | `llgltexture.h:40`, `lltexture.h:45` | Abstract texture over `LLImageGL`, base of `LLViewerTexture` | LL |
| `LLRenderTarget` | `llrendertarget.h:61` | FBO with colour attachments and optional depth (`allocate` `llrendertarget.cpp:105`, `addColorAttachment` `:205`, `bindTarget` `:420`, `flush` `:503`, `release` `:354`) | LL |
| `LLCubeMap`, `LLCubeMapArray` | `llcubemap.h:37`, `llcubemaparray.h:35` | Cube maps; probe array storage (`LLCubeMapArray::allocate` `llcubemaparray.cpp:142`) | LL |
| `LLFontGL`, `LLFontFreetype`, `LLFontRegistry`, `LLFontBitmapCache`, `LLFontVertexBuffer`, `LLFontFreeTypeSvgRenderer` | `llfontgl.h:46`, `llfontfreetype.h:94`, `llfontregistry.h:95`, `llfontbitmapcache.h:43`, `llfontvertexbuffer.h:35`, `llfontfreetypesvg.h:35` | FreeType glyph rasterisation into GL atlas pages, string layout and quad emission through `gGL` (`LLFontGL::render` `llfontgl.cpp:119-448`), font XML registry (`initClass` `:856`) | LL (SVG glyphs via nanosvg) |
| `LLRender2D`, `LLUIImage` | `llrender2dutils.h:125`, `lluiimage.h:44` | 2D quad helpers and nine-slice UI images used by `llui` | LL |
| `LLWindow`, `LLWindowManager`, `LLWindowCallbacks` | `llwindow/llwindow.h:43`, `:308`, `llwindowcallbacks.h:32` | Platform window, input dispatch, GL context incl. `createSharedContext`/`makeContextCurrent`/`destroySharedContext` (`llwindow.h:84-89`); factory `LLWindowManager::createWindow` (`llwindow.cpp:410`) | LL |
| `LLWindowWin32` | `llwindowwin32.h:45` | Win32 window, message pump on its own thread `LLWindowWin32Thread` (`llwindowwin32.cpp:367`), WGL pixel format and context (`createSharedContext` `:1912`, GL 3.0–4.6 descending), `swapBuffers` (`:3870`) | LL + FS |
| `LLWindowSDL` (SDL2) | `llwindowsdl2.h:49` | Linux SDL2 window and GL context (`SDL_GL_CreateContext` `llwindowsdl2.cpp:757`, shared `:2688`) | LL/FS |
| `LLWindowMacOSX` | `llwindowmacosx.h:44` | Cocoa window, core-profile GL 4.1 | LL |
| `LLWindowHeadless`, `LLWindowMesaHeadless` | `llwindowheadless.h:32`, `llwindowmesaheadless.h:36` | No-display and OSMesa variants | LL |
| `LLKeyboard` (+ Win32/SDL/macOS/headless) | `llkeyboard.h:54` | Key state and translation | LL |
| `LLDXHardware` | `lldxhardware.h:36` | DXGI/D3D9 VRAM and adapter probe on Windows | LL |
| `LLLinuxZink::activate`, `LLLinuxZink::Environment` | `llwindow/lllinuxzink.h:15`, `:33` | Load the bundled GLVND GLX provider `libGLX_vulkanstorm.so.0` by absolute path and set five environment variables for the GL context, restored afterwards | **Vulkanstorm** |
| `LLImageBase`, `LLImageRaw`, `LLImageFormatted` | `llimage/llimage.h:115`, `:204`, `:332` | Pixel buffers, scaling/compositing/mip helpers (`llimage.cpp`, 2,737 lines, C++), formatted-image base | LL |
| `LLImagePNG`, `LLImageTGA`, `LLImageBMP`, `LLImageDXT` | `llimagepng.h:32`, `llimagetga.h:35`, `llimagebmp.h:35`, `llimagedxt.h:35` | Thin wrappers over `vks_png_*`, `vks_tga_*`, `vks_bmp_*`, `vks_dxt_*` | LL interface, **Rust** bodies |
| `LLImageJPEG` | `llimagejpeg.h:44` | libjpeg-turbo codec (kept C++, `doc/rust_port.md` "JPEG verdict") | LL |
| `LLImageJ2C`, `LLImageJ2CImpl` | `llimagej2c.h:40`, `:105` | J2C container, discard/byte budget (`calcDataSizeJ2C` → `vks_j2c_calc_data_size`, `llimagej2c.cpp:275`), link-time impl factory `fallbackCreateLLImageJ2CImpl` (`:40`) | LL interface, Rust budget |
| `LLImageJ2COJ` | `llimagej2coj/llimagej2coj.h` | Thin side of the Rust J2C codec (`vks_j2c_*`, `llimagej2coj.cpp:30-144`) | **Vulkanstorm rewrite** |
| `LLImageDecodeThread` | `llimage/llimageworker.h:34` | 8-thread `LL::ThreadPool` "ImageDecode" (`llimageworker.cpp:70`) | LL |
| `LLImageFilter`, `LLImageDimensionsInfo` | `llimagefilter.h:64`, `llimagedimensionsinfo.h:37` | Snapshot filters (941 lines); header-only dimension probe incl. JPEG | LL |
| `LLPipeline` (`gPipeline`, `pipeline.cpp:262`) | `newview/pipeline.h:93` | Pools, render types, cull/sort orchestration, render targets, deferred lighting, shadows, post, impostors | LL + 100 `<FS` markers + Vulkanstorm PPLL |
| `LLDrawPool`, `LLRenderPass`, `LLFacePool` | `lldrawpool.h:44`, `:131`, `:402` | Per-pool render passes; batch submission `pushBatches` (`lldrawpool.cpp:431`), `pushBatch` (`:573`), `pushGLTFBatches` (`:779`); pool types `POOL_SKY`…`POOL_ALPHA` (`lldrawpool.h:57-79`) | LL |
| `LLViewerOctreeEntry/Group/Partition`, `LLOcclusionCullingGroup`, `LLViewerOctreeCull` | `llvieweroctree.h:74`, `:181`, `:345`, `:274`, `:369` | Octree, frustum cull traveller, GL occlusion queries (`doOcclusion` `llvieweroctree.cpp:1172`, `checkOcclusion` `:1107`) | LL |
| `LLSpatialGroup`, `LLSpatialPartition`, `LLSpatialBridge`, `LLCullResult`, `LLDrawInfo` | `llspatialpartition.h:207`, `:382`, `:444`, `:476`, `:80` | Per-node geometry and draw-info lists, partition cull (`cull` `llspatialpartition.cpp:1424/1447`), cull result buckets (`pushDrawInfo` `:4391`) | LL |
| `LLVolumeGeometryManager` | `llspatialpartition.h:662`, bodies in `llvovolume.cpp:5572-6604` | Face registration, geometry rebuild, `genDrawInfo` batching | LL |
| `LLDrawable`, `LLFace` | `lldrawable.h:63`, `llface.h:64` | Render-side object node; face geometry fill (`getGeometryVolume` `llface.cpp:1192`), texture virtual size (`getTextureVirtualSize` `:2266`) | LL |
| `LLViewerTexture`, `LLViewerFetchedTexture`, `LLViewerLODTexture`, `LLViewerMediaTexture`, `LLViewerTextureManager` | `llviewertexture.h:96`, `:277`, `:568`, `:592`, `:656` | Viewer texture state, VRAM budget and discard bias (`updateClass` `llviewertexture.cpp:501`), stats (`processTextureStats` `:1803`), fetch driving (`updateFetch` `:2075`), GL creation (`preCreateTexture` `:1488`, `createTexture` `:1639`, `postCreateTexture` `:1651`), loaded callbacks (`:2585`) | LL + 24 `<FS` |
| `LLViewerTextureList` (`gTextureList`, `llviewertexturelist.cpp:75`) | `llviewertexturelist.h:88` | Registry and per-frame update (`updateImages` `:837`, `updateImageDecodePriority` `:927`, `updateImagesCreateTextures` `:1114`, `updateImagesFetchTextures` `:1253`) | LL |
| `LLViewerShaderMgr` | `llviewershadermgr.h:35` | Viewer program registry and loading (`setShaders` `llviewershadermgr.cpp:532`, `loadShadersDeferred` `:1096`, `loadShadersInterface` `:3234`) | LL + FS + Vulkanstorm OIT |
| `LL::GLTF::Asset`, `GLTFSceneManager` | `gltf/asset.h:353`, `gltfscenemanager.h:38` | Experimental full-scene GLTF (tinygltf); upload (`Primitive::upload` `gltf/primitive.cpp:540`), render (`gltfscenemanager.cpp:578-638`) | LL |
| `LLGLTFMaterial`, `LLFetchedGLTFMaterial`, `LLGLTFMaterialList` | `llprimitive/llgltfmaterial.h:50`, `llfetchedgltfmaterial.h:37`, `llgltfmateriallist.h:40` | PBR material data, viewer binding, asset/override fetch | LL |
| `LLReflectionMapManager`, `LLReflectionMap`, `LLHeroProbeManager` | `llreflectionmapmanager.h:47`, `llreflectionmap.h:35`, `llheroprobemanager.h:50` | Radiance/irradiance probe placement, cube capture (`updateProbeFace` `llreflectionmapmanager.cpp:773`), UBO upload (`updateUniforms` `:1084`); hero (mirror) probes (`llheroprobemanager.cpp:76`, `:313`) | LL |
| `LLEnvironment`, `LLSettingsVOSky/Water/Day` | `llenvironment.h:50`, `llsettingsvo.h:88/123/154` | Environment selection and blending (`update` `llenvironment.cpp:1759`), uniform push (`updateShaderUniforms` `:1840`); VO settings apply (`LLSettingsVOSky::applySpecial` `llsettingsvo.cpp:749`) | LL |
| `LLSky`, `LLVOSky`, `LLVOWLSky`, `LLVOWater` | `llsky.h:42`, `llvosky.h:197`, `llvowlsky.h:32`, `llvowater.h:43` | Sky objects, sun/moon, sky dome geometry (`LLVOSky::updateSky` `llvosky.cpp:679`), water planes | LL |
| `LLViewerCamera` | `llviewercamera.h:41` | View/projection and frustum (`updateCameraLocation` `llviewercamera.cpp:89`, `setPerspective` `:294`) | LL |
| `LLViewerWindow` | `llviewerwindow.h:150` | Implements `LLWindowCallbacks`; creates the `LLWindow` (`llviewerwindow.cpp:2033`), GL defaults (`initGLDefaults` `:2193`), 2D/3D setup (`setup2DRender` `:6848`, `setup3DRender` `:6865`), UI draw (`draw` `:2959`), picking (`pickAsync` `:5389`, `performPick` `:5430`), snapshots (`rawSnapshot` `:6127`) | LL + 87 `<FS` |

### One frame, from `display()` to swap

`LLAppViewer::doFrame` (`newview/llappviewer.cpp:1562`) calls `display()`
(`llappviewer.cpp:1774`) on the main thread with `gGLActive = true`, then, once
logged in, `gPipeline.mReflectionMapManager.update()` (`llappviewer.cpp:1781`)
and the snapshot floaters. Inside `display()` (`newview/llviewerdisplay.cpp:469`):

| Step | Line (`llviewerdisplay.cpp`) | Call | What happens |
|---|---:|---|---|
| 1 | 485-494 | `LLPipeline::refreshCachedSettings`, `resizeScreenTexture`, `resizeShadowTexture` | Reallocate targets if the window or settings changed |
| 2 | 508-512 | `LLVertexBuffer::unbind`, `LLGLState::checkStates`, `disableLights` | Reset shadow GL state |
| 3 | 554-566 | `LLViewerTexture::updateClass`, `gBumpImageList.updateImages`, `gTextureList.updateImages` | Startup-path texture update (pre-login) |
| 4 | 669-693 | teleport / logout / restore-GL branches | Progress screens, GL restore |
| 5 | 778-780 | `render_ui`, `swap` | Disconnected: UI-only frame |
| 6 | 821-831 | `resetFrameStats`, `mHeroProbeManager.update()` | Hero (mirror) probe capture |
| 7 | 848-854 | `display_update_camera` (`:220`), `LLEnvironment::instance().update(&camera)` | Camera and environment blend for this frame |
| 8 | 868-874 | `createObjects`, `processPartitionQ`, `updateGeom`, `updateGL` | Drawable creation and geometry rebuild under a time budget (`pipeline.cpp:2093`, `:3220`, `:2994`, `:2881`) |
| 9 | 898 | `gPipeline.updateCull` (`pipeline.cpp:2634`) | Octree/frustum cull into `LLCullResult` (occlusion: `doOcclusion` `pipeline.cpp:2778`) |
| 10 | 922-944 | `generateSunShadow` (`pipeline.cpp:11119`), `LLVOAvatar::updateImpostors` | Sun and spot shadow maps (`renderShadow` `:10686`), avatar impostors |
| 11 | 963-975 | `LLViewerTexture::updateClass`, `gTextureList.updateImages` | VRAM budget, discard bias, decode priorities, GL creation of decoded textures (after culling so stats are fresh) |
| 12 | 999-1009 | `gPipeline.stateSort` (`pipeline.cpp:3281`), `rebuildPools` | Sort visible drawables into pools; rebuild pool list |
| 13 | 1023 | `gSky.updateSky` (`llsky.cpp:276`) | Sky object update |
| 14 | 1122-1130 | `renderObjects` (selection) / `renderGeomDeferred` (`pipeline.cpp:4213`) | G-buffer pass over pools; reflection-probe UBOs bound (`pipeline.cpp:4267-4268`) |
| 15 | 1148-1152 | `rt.flush()`, `renderDeferredLighting` (`pipeline.cpp:9674`) | Sun light + SSAO, soften/atmospherics, local lights, projectors, multi-light; then `renderGeomPostDeferred` (`pipeline.cpp:10153`, alpha, water, PPLL capture through `LLDrawPoolAlpha`), haze (`:10213`) |
| 16 | 1158 | `LLSceneMonitor::capture` | Scene-monitor capture |
| 17 | 1165, render_ui `:1598` → `gPipeline.renderFinalize()` (`pipeline.cpp:9191`) | Post chain | `copyScreenSpaceReflections` → `generateLuminance` → `generateExposure` → `tonemap` (+ optional `applyCAS`) or `gammaCorrect` → `generateGlow` → `combineGlow` → `renderDoF` → `applyFXAA` or SMAA (`generateSMAABuffers`, `applySMAA`) → FS vignette (`renderVignette` `:8726`), RLV sphere, FS snapshot frame (`renderSnapshotFrame` `:8773`) → `visualizeBuffers` (debug) |
| 18 | `render_ui` `:1598-1690` | `gViewerWindow->setup2DRender`, `LLViewerWindow::draw` | HUD attachments (`render_hud_attachments` `:1384`) and the `llui` view tree through `gGL` |
| 19 | 1166, `swap` `:1696` | `gViewerWindow->getWindow()->swapBuffers()` | `SwapBuffers(mhDC)` on Win32 (`llwindowwin32.cpp:3870`), GPU profiler collect |

Reflection-probe cube faces are rendered later in the same frame from
`LLReflectionMapManager::update` (`llreflectionmapmanager.cpp:206`) via
`doProbeUpdate` (`:731`) and `display_cube_face` (`llviewerdisplay.cpp:1272`),
which reuses the deferred path with `gCubeSnapshot = true`
(`llviewerdisplay.cpp:136`) into `mAuxillaryRT`. The Vulkan study traces this
graph in detail (`doc/vulkan/source-audit.md`, records F01–F10/D01–D17).

### Resource ownership and lifetime

| Resource | Owner | Lifetime and release |
|---|---|---|
| GL context | `LLWindow` subclass (`mhRC` on Win32, created via `createSharedContext` `llwindowwin32.cpp:1738`) | Process lifetime; lost/restored through `LLViewerWindow::stopGL`/`restoreGL` and `LLPipeline::releaseGLBuffers` (`pipeline.cpp:1313`) / `createGLBuffers` (`:1441`) |
| Screen and pass render targets | `LLPipeline` by value: `RenderTargetPack mMainRT`, `mAuxillaryRT` (512² probes/bakes), `mHeroProbeRT`, pointer `mRT` to the current pack (`pipeline.h:732-763`); shadow, luminance, exposure, ping/pong, FXAA/SMAA, glow, sky SH, water, scene map (`pipeline.h:765-855`) | Allocated in `allocateScreenBuffer` (`pipeline.cpp:845`) / `allocateShadowBuffer` (`:1089`), released in `releaseGLBuffers` |
| PPLL OIT buffers | `LLPipeline` raw GL names `mAlphaOITHead`, `mAlphaOITNodes`, `mAlphaOITCounter`, `mAlphaOITOpaqueDepth` (`pipeline.h:776-784`) | `allocateAlphaOITBuffers` (`pipeline.cpp:7986`), released before scene targets (`pipeline.cpp:1397`) |
| Textures | `LLPointer<LLImageGL>` inside `LLGLTexture`; `LLViewerFetchedTexture` held by `gTextureList` map and by faces/materials via `LLPointer` | Reference counted; GL name freed in `LLImageGL::destroyGLTexture`; discard reduction by `scaleDown` (`llimagegl.cpp:2468`) or re-fetch at higher discard |
| Raw/formatted images | `LLPointer<LLImageRaw>`, `LLPointer<LLImageFormatted>` shared among fetch worker, decode pool, GL upload | Thread-safe ref counts; per `doc/rust_port.md` rung five, these containers stay C++ |
| Vertex buffers | `LLPointer<LLVertexBuffer>` held by `LLFace`/`LLSpatialGroup`/`LLDrawInfo`, fonts (`LLFontVertexBuffer`), UI | Ref-counted; GL buffer names from `createGLBuffer` |
| Shader programs | 127 global `LLGLSLShader` objects in `llviewershadermgr.cpp` | Loaded by `setShaders` on startup and on every graphics-setting change, `unloadShaders` (`:745`) |
| Octree / spatial groups | Per region `LLViewerRegion::mImpl->mObjectPartition` (`llviewerregion.cpp:722-729`, 11 partition types `llviewerregion.h:92-105`) | Region lifetime; drawables move between groups on update |
| Drawables | `LLPointer<LLDrawable> mDrawable` in `LLViewerObject` (`llviewerobject.h:865`), created by `createDrawable(LLPipeline*)` (`:248`) | Object lifetime; `LLSpatialGroup::sNoDelete` guards deletion during a frame |
| Fonts | `LLFontRegistry` singleton, glyph pages in `LLFontBitmapCache` as `LLImageGL` | Process lifetime; rebuilt on UI scale change |

### Threading

| Thread | Defined | GL? | Work |
|---|---|---|---|
| Main thread | `LLAppViewer::doFrame` | Yes, primary context | All pipeline work, `gGL` immediate mode, shader binds, most uploads, swap |
| Win32 window thread | `LLWindowWin32::LLWindowWin32Thread : LL::ThreadPool` (`llwindowwin32.cpp:367`) | No GL | Message pump and window calls posted with `mWindowThread->post` (`:1105`, `:1169`, `:1782`, …) |
| GL image upload thread | `LLImageGLThread` (`llimagegl.h:346`, `run` `llimagegl.cpp:2651`) | Yes, shared context from `createSharedContext` (`llimagegl.cpp:2637`) | Texture and media uploads when `sEnabledTextures`/`sEnabledMedia` (GL > 3.95 and settings, `llimagegl.cpp:261-263`); completions posted back to the "mainloop" `LL::WorkQueue` (`llviewertexture.cpp:784-785`, `:1705`; `llviewermedia.cpp:1690-1691`). `gGL` is `thread_local`, so this thread has its own `LLRender` state |
| Image decode pool | `LLImageDecodeThread`, 8-worker `LL::ThreadPool` "ImageDecode" (`llimageworker.cpp:70`) | No | J2C (Rust) and other decodes into `LLImageRaw` |
| Texture fetch thread | `LLTextureFetch` (`LLWorkerThread`) | No | Rust fetch state machine driven by C++ worker plumbing (rung five) |
| Texture cache thread | `LLTextureCache` (`LLWorkerThread`) | No | Rust cache index/file logic (rung five) |
| Main-loop pauses | `llappviewer.cpp:1820-1975` | — | Cache, decode and fetch threads are paused/unpaused around `display()` and drained by `update(max_time)` |

Mesh repository threads and the GLTF material fetch coroutines feed the
renderer but are covered in the asset chapter.

### Third-party dependencies (render, window, imaging)

| Package (`autobuild.xml` key) | Used by | Note |
|---|---|---|
| OpenGL (`OpenGL::GL`, `OpenGL::GLU`), `glext`, `glh_linear` | `llrender`, `llwindow`, `newview` | System GL plus headers; Windows delay-loads `opengl32.dll` (`newview/CMakeLists.txt:2293`, Vulkanstorm) |
| `mesazink` | Staged runtime, Windows `opengl32.dll` + `libgallium_wgl.dll`, Linux `libGLX_vulkanstorm.so.0` + `libgallium_vulkanstorm.so` (`cmake/MesaZink.cmake`) | **Vulkanstorm**; "built from the patched Mesa devel tree"; `USE_MESAZINK` OFF outside CI presets |
| `SDL2` | `llwindow` Linux | `LL_SDL2=1` (`cmake/LLWindow.cmake:30`) |
| `freetype`, `nanosvg` | Fonts (`llfontfreetype.cpp`, `llfontfreetypesvg.cpp`) | freetype also pulls libpng for colour bitmap fonts |
| `libpng`, `zlib-ng` | Only through freetype now | Viewer PNG is Rust |
| `libjpeg-turbo` | `LLImageJPEG` | Kept C++ |
| `vkstorm-ffi` | `llimage`, `llimagej2coj`, `llmath`, newview fetch/cache | Rust codecs, mesher, fetch/cache |
| `kdu` | `llkdu` | Only with `USE_KDU`/`INSTALL_PROPRIETARY` |
| `openjpeg` | Still listed in `autobuild.xml`; referenced by `integration_tests/llui_libtest/CMakeLists.txt:58-71` | Not linked by the viewer per `doc/rust_port.md` (see Risks) |
| `tinygltf` | `newview/gltf/llgltfloader.cpp`, `gltfscenemanager.cpp`, `llgltfmateriallist.cpp`, `llprimitive/llgltfmaterial*` | LL |
| `vulkan_gltf` | `cmake/VulkanGltf.cmake`, included by `newview/CMakeLists.txt:55` | LL upstream package name; not a Vulkan renderer hook |
| `mikktspace` | `newview/gltf/primitive.cpp`, `llmath/llvolume.cpp` | Tangents for GLTF; the prim tangents are Rust |
| `meshoptimizer` | `llmeshoptimizer`, `llmath/llvolume.cpp` (`cacheOptimize`) | C++ |
| `glm` | Matrices throughout | |
| `tracy` | GPU zones (`LL_PROFILE_GPU_ZONE`) | |
| `nvapi` | Windows NVIDIA driver profile (outside this chapter's files) | |

### Firestorm and Vulkanstorm additions in this area

| Addition | Where | Origin |
|---|---|---|
| Render backend selector `RenderBackend` = `OpenGL` / `Vulkan` / `Zink`, `RenderBackendPending`, restart prompt | `settings.xml:12285`, `:12296`; `llappviewer.cpp:3729-3757`; `llfloaterpreference.cpp:4030-4091` | Vulkanstorm. `Vulkan` logs a warning and runs OpenGL (`llappviewer.cpp:3736-3739`) |
| Windows Zink: preload bundled `mesa\opengl32.dll` before the delay-loaded GL import, set `GALLIUM_DRIVER`/`MESA_LOADER_DRIVER_OVERRIDE` | `LLAppViewerWin32::selectGLBackend` `llappviewerwin32.cpp:1098-1170`; `/DELAYLOAD:opengl32.dll` `newview/CMakeLists.txt:2293` | Vulkanstorm. Comment refers to an `LLVKProbe` that does not exist on this line (`git grep LLVKProbe` finds only that comment) |
| Linux Zink: GLVND provider by absolute path, environment applied for window creation and restored after | `llwindow/lllinuxzink.h`; `LLAppViewerLinux::selectGLBackend` `llappviewerlinux.cpp:104-127`, `initWindow` `:128-135` | Vulkanstorm |
| Graphics settings per renderer family (OpenGL vs Vulkan, Zink normalised to the shared GL renderer string) | `newview/llgraphicsidentity.h:12-53`; `doc/graphics_settings_persistence.md`; `scripts/perf/test_graphics_identity.py` | Vulkanstorm |
| PPLL alpha OIT (GL 4.4+, SSBO + image atomics; Windows/Linux only) | `pipeline.cpp:7976-8240` (`allocateAlphaOITBuffers`, `beginAlphaOITCapture` `:8112`, `endAlphaOITCapture` `:8158`, `compositeAlphaOIT` `:8171`); `lldrawpoolalpha.cpp:217-228`; `llshadermgr.cpp:656`, `:1577`; `alphaOITResolveF.glsl`, `class2/deferred/alphaF.glsl`, `pbralphaF.glsl`; settings `RenderAlphaOIT*` `settings.xml:11785-11836` | Vulkanstorm (marked `<FS> Vulkanstorm`; "a direct-GL port (no RHI)" `pipeline.cpp:7981`) |
| Exodus vignette, snapshot frame, RLV sphere post effects | `pipeline.cpp:8726`, `:8773`, `:9288-9294`; `llviewershadermgr.cpp:138`, `:237`, `:1072-1075` | Firestorm |
| Texture VRAM cap `FSLimitTextureVRAMUsage` | `llviewertexture.cpp:520-534` | Firestorm |
| Build preview render target `mPreviewScreen` | `pipeline.h:753-757` | Firestorm (`<FS:Beq>`) |
| DoF focus lock | `pipeline.h:728-729` | Firestorm |

No native-Vulkan code, no RHI, and no `vkstorm-ffi` call exists in `llrender`,
`llwindow`, `pipeline.*`, `lldrawpool*` or the shaders
(`git grep -n 'vks_' -- llrender llwindow 'newview/pipeline.*' 'newview/lldrawpool*'`
returns nothing). The only Vulkan API use is the upstream capability probe in
`newview/llviewerstats.cpp:815-871` (loads `vulkan-1.dll` and reads the
instance version for statistics). `scripts/tests/check_release_hooks.py`
passes on this tree ("Release runtime development-hook policy passed.").

## Coupling

### GL and LLRender use across the tree

Commands run from `indra/`:

| Measure | Command | Result |
|---|---|---|
| newview `.cpp`/`.h` files | `git ls-files 'newview/*.cpp' 'newview/*.h' \| wc -l` | 1,784 |
| newview files including any `llrender` header | `git grep -l -E '#include "(<52 llrender header names>)"' -- 'newview/*.cpp' 'newview/*.h' \| wc -l` | 170 |
| newview files including an `llwindow` header | `git grep -l -E '#include "(llwindow\|llkeyboard\|llwindowcallbacks\|llmousehandler\|llcursortypes)\.h"' -- 'newview/*.cpp' 'newview/*.h' \| wc -l` | 100 |
| `gGL.` uses in newview | `git grep -o 'gGL\.' -- 'newview/*.cpp' 'newview/*.h' \| wc -l` | 3,485 in 93 files |
| `gGL.` uses in llui | same over `llui/*.cpp` | 114 in 23 files |
| `gGL.` uses inside llrender | same over `llrender/*.cpp` | 825 |
| Raw `gl*()` calls in newview | `git grep -o -E '\bgl[A-Z][A-Za-z0-9]*\(' -- 'newview/*.cpp' \| wc -l` | 423 in 38 files, 69 distinct entry points |
| Raw `gl*()` calls in llrender / llui | same | 414 / 5 |
| RAII state objects in newview | `git grep -o -E 'LLGL(Enable\|Disable\|DepthTest\|SUIDefault\|SPipeline[A-Za-z]*\|SDefault)\b' -- 'newview/*.cpp' \| wc -l` | 401 (in 52 files with `LLGLEnable/Disable/DepthTest`) |
| `gPipeline.` uses in newview | `git grep -o 'gPipeline\.' -- 'newview/*.cpp' \| wc -l` | 861 in 81 files |
| `LLPipeline::` uses in newview | `git grep -o 'LLPipeline::' -- 'newview/*.cpp' \| wc -l` | 1,314 in 70 files |
| Files including `pipeline.h` | `git grep -l '#include "pipeline.h"' -- 'newview/*.cpp' 'newview/*.h' \| wc -l` | 108 |
| newview files using `LLVertexBuffer` | `git grep -l LLVertexBuffer -- 'newview/*.cpp' \| wc -l` | 42 |
| newview files using `LLGLSLShader` or a `g*Program` | `git grep -l -E 'LLGLSLShader\|g[A-Za-z]+Program\b' -- 'newview/*.cpp' \| wc -l` | 53 |
| `stop_glerror` in newview | `git grep -o stop_glerror -- 'newview/*.cpp' \| wc -l` | 104 |

Include-site counts per header (`newview` / whole tree, `git grep -l '#include "H"'`):
`llrender.h` 67/101, `llfontgl.h` 57/92, `llgl.h` 46/92, `llglheaders.h` 45/58,
`llvertexbuffer.h` 15/22, `llglslshader.h` 11/24, `llrendertarget.h` 7/11,
`lluiimage.h` 6/27, `llimagegl.h` 3/9, `llgltexture.h` 3/11, `llshadermgr.h` 3/7,
`llcubemaparray.h` 3/4, `llrender2dutils.h` 1/4, `llglstates.h` 0/2.

Top `gGL.` users (`git grep -c 'gGL\.' -- 'newview/*.cpp' | sort -t: -k2 -rn`):
`pipeline.cpp` 377, `llspatialpartition.cpp` 221, `llmaniptranslate.cpp` 196,
`lldrawpoolterrain.cpp` 181, `llvoavatar.cpp` 135, `llglsandbox.cpp` 134,
`llviewerdisplay.cpp` 133, `fsmaniptranslatejoint.cpp` 123, `llnetmap.cpp` 97,
`llfloaterimagepreview.cpp` 96, `llworldmapview.cpp` 88, `llviewerwindow.cpp` 88.
Raw GL is concentrated in `pipeline.cpp` (109), `lldrawpoolterrain.cpp` (61),
`llviewerwindow.cpp` (49), `llspatialpartition.cpp` (24), `llviewerdisplay.cpp`
(21), `gltfscenemanager.cpp` (17), `llreflectionmapmanager.cpp` (16). The most
frequent raw calls are `glPolygonMode` (67), `glPolygonOffset` (30),
`glClearColor` (22), `glClear` (21) and fixed-function `glTexGen*` (32).

Other libraries: `llui` has 56 files including an `llrender` header (of 229),
`llappearance` 9 (avatar joints and `LLTexLayer` bake through `LLVertexBuffer`
and `LLGLTexture`). `llprimitive/CMakeLists.txt:68` links `llrender` but no
`llprimitive` file includes an `llrender` header. Inference: that link is stale
or transitive-only.

### Library dependency cycle

`llrender` links `llwindow` (`llrender/CMakeLists.txt:99-108`) and includes
`llwindow` headers in `llfontregistry.cpp`, `llimagegl.cpp` and
`llvertexbuffer.cpp`; `llwindow` links `llrender` (`llwindow/CMakeLists.txt`,
`llwindow_LINK_LIBRARIES`) and every platform window `.cpp` includes `llrender`
headers (`llwindowwin32.cpp`, `llwindowsdl2.cpp`, `llwindowsdl.cpp`,
`llwindowmacosx.cpp`, `llwindowmesaheadless.cpp`). The two are one unit for
any boundary: `LLImageGLThread` and `LLVertexBuffer::initClass` take an
`LLWindow*` for shared contexts, and the window classes call `LLRender`/`LLGL`
for context setup.

### What ties rendering to simulation and data

| Coupling | Evidence | Effect on separation |
|---|---|---|
| Objects own render nodes | `LLViewerObject::mDrawable` (`llviewerobject.h:865`, 172 refs in `llviewerobject.cpp`), `createDrawable(LLPipeline*)` (`:248`), `getPartitionType` (`:678`); 141 `LLDrawable` refs and 56 `markRebuild` calls in `llvo*.cpp`/`llviewerobject*.cpp` | Object update and render rebuild are the same call chain; the object store cannot move without a drawable interface |
| Regions own partitions | `LLViewerRegion` builds 11 partitions (`llviewerregion.cpp:722-729`) | Spatial structure is keyed by simulator regions |
| Object cache shares the octree | `LLVOCacheEntry : LLViewerOctreeEntryData`, `LLVOCacheGroup : LLOcclusionCullingGroup`, `LLVOCachePartition : LLViewerOctreePartition` (`llvocache.h:58`, `:204`, `:216`) | Cache-driven object creation uses render occlusion results; 10 `.cpp` files use `LLViewerOctree` |
| Texture stats from geometry | `LLFace::getTextureVirtualSize` (`llface.cpp:2266`) and `calcPixelArea` (`:2317`) feed `addTextureStats` (`llviewertexture.cpp:904`), which drives discard and fetch priority | The texture policy reads camera and face geometry each frame |
| Global current-view state | `gCubeSnapshot`, `LLPipeline::sShadowRender`, `sUnderWaterRender`, `sRenderDeferred`, `LLViewerCamera` singleton, `thread_local gGL` matrix stacks | Passes communicate through globals; the Vulkan study proposes a `ViewContext` (`doc/vulkan/scene-ui-auxiliary.md` S03) |
| Environment directly sets uniforms | `LLEnvironment::updateShaderUniforms` (`llenvironment.cpp:1840`), `LLSettingsVOSky::applySpecial` (`llsettingsvo.cpp:749`) | Settings data (`llinventory/llsettings*`) is pushed straight into `LLGLSLShader` |
| UI draws in the frame with shared state | `render_ui` → `LLViewerWindow::draw`; `llui` uses `gGL` and `LLFontGL`; manipulators (`llmaniptranslate.cpp` 196 `gGL.`) draw immediate-mode geometry | The UI rung (`doc/rust_port_ui.md` §3c) assumes `LLRender` remains |
| Avatar rendering and bakes | `lldrawpoolavatar.cpp`, `llvoavatar.cpp` (135 `gGL.`), `llappearance/lltexlayer.cpp` | Avatar appearance and GPU baking are intertwined |
| Picking and snapshots read GL | `LLViewerWindow::performPick` (`:5430`), `rawSnapshot` (`:6127`) | Selection and tools depend on framebuffer readback |

## Rust port status

### Wired to Rust (no fallback)

| Area | C++ entry (thin wrapper) | FFI calls | Port record |
|---|---|---|---|
| JPEG 2000 decode/encode/metadata | `LLImageJ2COJ` (`llimagej2coj/llimagej2coj.cpp:30-144`) | `vks_j2c_engine_info`, `vks_j2c_metadata`, `vks_j2c_decode`, `vks_j2c_decoded_size`/`_copy`/`_comment`/`_free`, `vks_j2c_encode`, `vks_j2c_encoded_len`/`_data`/`_free` | `doc/rust_port.md` "Rung one" |
| J2C byte budget per discard | `LLImageJ2C::calcDataSizeJ2C` (`llimage/llimagej2c.cpp:275`) | `vks_j2c_calc_data_size` | "Rung five" (`fetch::budget`) |
| PNG | `LLImagePNG` (`llimagepng.cpp:45-158`) | `vks_png_header`, `vks_png_decode`, `vks_png_encode` | "Rung two: PNG, TGA, BMP, DXT" |
| TGA incl. `decodeAndProcess` | `LLImageTGA` (`llimagetga.cpp:46-167`) | `vks_tga_header`, `vks_tga_decode`, `vks_tga_decode_and_process`, `vks_tga_encode` | same |
| BMP | `LLImageBMP` (`llimagebmp.cpp:68-165`) | `vks_bmp_header`, `vks_bmp_decode`, `vks_bmp_encode` | same |
| DXT and mip helpers | `LLImageDXT` (`llimagedxt.cpp:40-374`) | 15 `vks_dxt_*` (header, decode, encode, fourcc/format tables, mip offset/data, discard sizes, `convert_to_dxr`) | same |
| Shared helpers | all of the above | `vks_raster_*`, `vks_bytes_*`, `vks_image_last_error` | |
| Prim mesher | `LLProfile`, `LLPath`, `LLVolume::generate`, `LLVolumeFace::create*`, tangents (`llmath/llvolume.cpp`) | `vks_volume_profile_generate`/`_num_points`, `vks_volume_path_generate`/`_num_points`, `vks_volume_sweep`, `vks_volume_face_sizes`, `vks_volume_face_build`, `vks_volume_tangents` (×2) | "Rung: the parametric prim mesher" |
| Mesh LOD unpack, skin, decomposition | `llmath/llvolume.cpp`, `llprimitive/llmodel.cpp`, `newview/llmeshrepository.cpp` | 22 distinct `vks_mesh_*` | "Rung five" |
| Texture cache | `newview/lltexturecache.cpp` | 27 `vks_texcache_*` call sites | "Rung five" |
| Texture fetch state machine | `newview/lltexturefetch.cpp` | 49 `vks_tfetch_*` call sites, `vks_retry_*` | "Rung five" |

Counts: `git grep -o -h 'vks_[a-z0-9_]*' -- llimage llimagej2coj llrender llwindow | sort | uniq -c`
and `git grep -o -h 'vks_[a-z0-9]*_' -- 'newview/*.cpp' | sort | uniq -c`.

Since the Vulkan study baseline `1a490c3cb7` (an ancestor of `HEAD`, 214
commits back), `git diff --stat 1a490c3cb7 HEAD` over this chapter's files
touches only `llimage/*` (codec bodies removed, `llpngwrapper.*` deleted),
`llimagej2coj/*`, `newview/lltexturefetch.cpp` and seven lines of
`newview/llviewerwindow.cpp` (UI hover fix). `llrender`, `llwindow`,
`pipeline.*`, `lldrawpool*`, `llviewertexture*`, `llviewershadermgr.cpp`,
`llspatialpartition.cpp` and the shaders are unchanged.

### Still C++ in this chapter

| Component | Reason it is still C++ |
|---|---|
| `LLImageJPEG`, JPEG probe in `LLImageDimensionsInfo` | Measured and kept (Rust candidates slower / not bit-exact) |
| `LLImageRaw` operations (`llimage.cpp`: scale, composite, fill, mip generation used outside DXT), `LLImageFilter` | Not yet in any rung |
| `LLImageDecodeThread`, `LLWorkerThread` plumbing | Kept by rung five's judgement (scheduler semantics) |
| `sculptGenerateMapVertices`, `cacheOptimize`, GLTF tangents (mikktspace) | Left by mesher/asset rungs |
| `LLViewerTexture`/`LLViewerFetchedTexture`/`LLViewerTextureList`/`LLImageGL` | "The GL upload side … C++: Rendering" (`doc/rust_port.md` rung five table) |
| All of `llrender`, `llwindow`, the pipeline, draw pools, octree, probes, environment, post, shaders | Renderer and UI rungs not started |

## Port assessment

### The "textures" rung

`doc/rust_port.md` lists "textures" after audio. Rung five already took the
texture cache, the fetch state machine, the retry policy and the J2C byte
budget, and rung one/two the codecs. Inference: what remains for a textures rung
is the CPU-side texture policy and pixel work between the fetcher and
`LLImageGL`, while GL object ownership stays C++ until the renderer rung.
Concrete candidates, each a decision or pixel function that can take a snapshot
and return a result, in the style of `VksTexFetchEnv`/`VksTexFetchHost`:

| Candidate | Location | Boundary shape | Oracle approach |
|---|---|---|---|
| VRAM budget, free-memory estimate and discard-bias controller | `LLViewerTexture::updateClass` (`llviewertexture.cpp:501-620`), `getSystemMemoryBudgetFactor` (`:705`); 26 `sDesiredDiscardBias` uses in newview | Input: VRAM detected/used, settings (`RenderTextureVRAMDivisor`, `RenderMaxVRAMBudget`, `FSLimitTextureVRAMUsage`), system memory, frame interval, background time; output: new bias, free MB | Record per-frame input/output traces from the C++ |
| Per-texture desired discard and priority | `LLViewerFetchedTexture::processTextureStats` (`:1803`), `LLViewerTextureList::updateImageDecodePriority` (`llviewertexturelist.cpp:927`), `addTextureStats` (`llviewertexture.cpp:904`) | Input: max virtual size, boost level, current/desired discard, bias, full dimensions, flags; output: desired discard, priority, delete/scale-down request | Recorded corpus of states |
| Update scheduling | `updateImages` (`:837`), `updateImagesFetchTextures` (`:1253`), `updateImagesCreateTextures` (`:1114`) | Selection of which textures to touch within a time budget; the map walk stays C++ | Deterministic replay with a fixed clock |
| `LLImageRaw` pixel operations | `llimage/llimage.cpp` (2,737 lines) | Pure buffer functions, same model as `vkstorm-image` | Byte-identical corpora as rung two |
| `LLImageFilter` | `llimage/llimagefilter.cpp` (941 lines) | Pure buffer filters driven by LLSD filter files | Byte-identical over skin filter definitions |
| Upload format decisions | `LLImageGL::createGLTexture`/`setManualImage` format/compression/mip choices (`llimagegl.cpp:1319-1631`) | Return internal format, mip plan, compression choice; the `gl*` calls stay C++ | Table-driven tests, no GL needed |
| UI and bump image lists | `LLUIImageList` (`llviewertexturelist.h:257`), `gBumpImageList` | Probably stay C++ (UI rung / renderer rung) | — |

Excluded from the textures rung: GL names, `LLImageGLThread`, media textures
(`LLViewerMediaTexture`, plugin protocol), dynamic and baked textures.
Prerequisite: the policy functions read globals (`gSavedSettings`,
`gGLManager.mVRAM`, `gFrameIntervalSeconds`, `LLViewerCamera`); these must be
collected into an explicit snapshot struct first, which is a pure C++
refactor that can be validated against the existing behaviour before any Rust
code lands.

### The renderer rung

Inference from the counts above: a Rust renderer behind a C ABI cannot be a
drop-in for `LLRender` call sites, because 3,485 `gGL.` uses, 423 raw `gl*`
calls and 401 RAII state objects in 93/38/52 newview files talk to GL state
directly, and `gGL` is `thread_local` immediate mode. Porting `llrender` one
class at a time behind the existing names would put an FFI call under every
`gGL.vertex3f`. The viable sequencing is:

1. Inside C++, separate the scene description from GL submission: per-view
   draw lists (`LLCullResult`/`LLDrawInfo` already approximate this), an
   explicit view context replacing `gCubeSnapshot`/`sShadowRender`/camera
   singletons, and material/uniform blocks instead of per-call `uniform*`.
   This is the same decoupling the Vulkan study lists as its first work
   packages (`doc/vulkan/qualification-roadmap.md` "Source-analysis work
   before parity implementation").
2. Port the CPU-only, GL-free parts first, behind data boundaries:
   culling math and octree (`llvieweroctree.cpp`, `LLSpatialPartition::cull`),
   batch building (`genDrawInfo` `llvovolume.cpp:6604`, `LLFace::getGeometryVolume`
   `llface.cpp:1192`), probe placement and scheduling
   (`LLReflectionMapManager::update`/`doProbeUpdate`), shadow cascade split
   computation (inside `generateSunShadow` `pipeline.cpp:11119`), sky/atmosphere
   parameter computation (`LLSettingsVOSky`, `LLVOSky::calc` `llvosky.cpp:521`),
   environment blending (`LLEnvironment::update`), and font layout and glyph
   rasterisation (`LLFontFreetype`, `LLFontGL` layout, not its draw).
3. Only then replace GL submission, which is the point at which the
   Vulkan question arises.

### Relation to the Vulkan line

`doc/vulkan/` is a design study for a native Vulkan renderer on Windows and
Linux, audited at `vkstorm-devel` commit `1a490c3cb7`. It catalogues 73
contract/design records over 225 GLSL files and 690 shader registration rows,
compares DiligentCore and bgfx (favouring DiligentCore for qualification, not
adopted), rules out a bespoke Vulkan RHI as the primary implementation, keeps
GL/Zink operational if no abstraction qualifies, and states that no runtime
qualification has been done (`doc/vulkan/README.md`). It does not mention the
Rust port except in passing. On this line:

- The renderer files are identical to that baseline (see "Rust port status"),
  so its frame graph (`source-audit.md`), resource and platform contracts
  (`resources-platform.md`), scene/UI/auxiliary contracts
  (`scene-ui-auxiliary.md`) and shader contracts (`shader-contracts.md`) are
  directly usable as the renderer-rung inventory.
- `doc/rust_port.md` puts Vulkan last, after the renderer and the UI. Inference:
  the renderer rung would therefore be a Rust port that still targets GL (or
  GL through Zink), and the Vulkan rung would replace its submission layer.
  The alternative ordering — move to a qualified graphics abstraction first
  and port to Rust above it — is not discussed in either document and is an
  open decision (see Risks).
- Zink is the only Vulkan execution path that exists today: the unchanged GL
  pipeline running on Mesa's GL-over-Vulkan driver, selected by
  `RenderBackend=Zink`. It is a runtime provider choice, not a renderer port.
- The `RenderBackend=Vulkan` value and the per-family graphics settings
  (`llgraphicsidentity.h`) are placeholders for a later backend.

### Port / keep / defer per component

| Component | Decision | Rationale | Prerequisites |
|---|---|---|---|
| `llimage` raster codecs PNG/TGA/BMP/DXT, J2C | Done | Rungs one/two | — |
| `LLImageJPEG` | Keep | Measured verdict in `doc/rust_port.md` | Re-measure if a faster Rust decoder appears |
| `LLImageRaw` ops, `LLImageFilter` | Port (textures rung) | Pure pixel code, oracle-friendly | Corpus of inputs from skins/snapshots |
| `LLImageDecodeThread` | Keep | Rung five judgement on schedulers | — |
| `llkdu` | Remove or fence | Bypasses the Rust codec if `USE_KDU` is set | Owner decision |
| Texture policy (`updateClass`, `processTextureStats`, priorities) | Port (textures rung) | Decision logic, deterministic given inputs | Snapshot struct refactor; trace recorder |
| `LLViewerTexture` object model, `LLViewerTextureList` map, `LLImageGL` GL side | Keep until renderer rung | Owns GL names, `LLPointer` graph shared with UI and materials | — |
| `LLImageGL` format/mip decision helpers | Port (textures rung, optional) | Pure tables | Extraction from `setManualImage`/`createGLTexture` |
| Fonts: FreeType rasterisation, layout | Port with UI rung or before | CPU work; glyph atlas upload stays C++ | Separate layout from `gGL` emission in `LLFontGL::render` |
| `LLRender` immediate mode, `LLGLState`, `LLTexUnit` | Keep, then replace with the submission layer | Thousands of call sites; no benefit from an FFI wrapper per call | Scene/submission separation |
| `LLVertexBuffer`, `LLRenderTarget`, `LLCubeMapArray`, `LLGLSLShader`, `LLShaderMgr` | Defer to renderer/Vulkan rung | Thin GL object wrappers; their replacement is the backend | Shader binding/vertex ABI decision (Vulkan study U-D3) |
| Shaders (225 GLSL) | Keep GLSL; port the assembly (`loadShaderFile` define injection) only with a toolchain decision | Shader language/toolchain is open in the Vulkan study | SPIR-V/GLSL decision |
| Octree, culling, spatial partitions | Port (renderer rung, early) | CPU geometry; shared with the object cache | Object-store rung's drawable interface |
| Batch building (`genDrawInfo`, `getGeometryVolume`) | Port (renderer rung) | CPU; consumes Rust mesher output already | Vertex layout fixed at the boundary |
| Draw pools and pass sequencing | Port late | Pass graph is GL-state-heavy | View context, draw lists |
| Deferred lighting, post chain, PPLL OIT | Defer to submission port | Mostly GL calls and shader dispatch | Backend decision |
| Reflection/hero probe scheduling | Port (renderer rung) | Scheduling and placement are CPU | Probe capture expressed as a view |
| Environment and sky computation (`LLEnvironment`, `LLSettingsVO*`, `LLVOSky::calc`) | Port (renderer rung or with settings data) | Numeric, deterministic; settings data in `llinventory` | Uniform block definition instead of direct `uniform*` |
| `LLViewerCamera` | Port with culling | Math | — |
| `llwindow` (Win32/SDL2/macOS), input | Keep longest | Platform glue; context creation is backend-specific | Backend decision; a Rust windowing layer would also need IME/drag-drop parity |
| Zink loader (Windows/Linux) | Keep | Process-start provider selection, small | — |
| `LLViewerWindow` render side (setup2D/3D, pick, snapshot) | Defer | Tied to UI and GL readback | UI rung |

### Boundary candidates (C ABI shapes)

| Boundary | Direction | Data across | Granularity |
|---|---|---|---|
| `vks_texpolicy_*` | C++ → Rust per frame and per texture | Snapshot structs (budget inputs; per-texture stats), results (bias, desired discard, priority, actions) | One call per frame plus one per updated texture, or one batched call over an array |
| `vks_imageraw_*` | C++ → Rust | Raster buffers by pointer and length, as `vks_raster_*` | Per operation |
| `vks_cull_*` | C++ → Rust per view | Frustum planes, octree node bounds (SoA arrays), occlusion state; returns visible node indices | Per view per frame |
| `vks_batch_*` | C++ → Rust per spatial group rebuild | Face descriptors (texture ids, material keys, vertex ranges); returns draw-info records | Per rebuild |
| `vks_sky_*` / `vks_env_*` | C++ → Rust per frame | Settings values, time of day, camera; returns uniform block bytes | Per frame |
| `vks_font_*` | C++ → Rust | Glyph rasterisation into caller buffers; layout returns quad lists | Per string or glyph |

Inference: none of these require Rust to make GL calls; all keep GL on the C++
side, consistent with the port's "C++ classes keep their names" rule.

## Existing tests and oracles

| Test | Location | What it checks | CI |
|---|---|---|---|
| `INTEGRATION_TEST_llimagej2coj` | `llimagej2coj/tests/llimagej2coj_test.cpp` (204 lines), `llimagej2coj/CMakeLists.txt:35` | Decodes every skin `.j2c` at discard 0/1/2 and truncated; lossy/lossless round trip; engine string length; junk rejection | `.github/workflows/build.yaml:218` |
| `INTEGRATION_TEST_llimagecodecs` | `llimage/tests/llimagecodecs_test.cpp`, manifest `llimage/tests/llimagecodecs_manifest.txt` | PNG/TGA decodes of the default skin against CRCs of the C++ output; encoder round trips; `decodeAndProcess` on character masks; error messages | `build.yaml:218` |
| `llimage` unit test | `llimage/tests/llimageworker_test.cpp` (`LL_ADD_PROJECT_UNIT_TESTS`, `llimage/CMakeLists.txt:72`) | `LLImageDecodeThread` responder behaviour | LL tests |
| `llkdu` test | `llkdu/tests/llimagej2ckdu_test.cpp` | KDU glue | Only with `USE_KDU` |
| `INTEGRATION_TEST_llvolume` | `llmath/tests`, corpora `llvolume_corpus.vksv` / `.linux.vksv` | Mesher parity, 881 cases | `build.yaml:218` |
| `INTEGRATION_TEST_lltexturecache_parity`, `INTEGRATION_TEST_lltexturefetch_parity` | `newview/tests/lltexturecache_parity_test.cpp`, `lltexturefetch_parity_test.cpp`, fixtures `newview/tests/fixtures/lltexture*`, `textures/*.j2c` | Cache files and fetch outcomes against recorded C++ | `build.yaml:220` |
| Linux Zink loader | `llwindow/tests/linux_zink_loader_test.cpp` (55 lines) | Builds standalone, loads the bundled GLVND provider under Xvfb with lavapipe, records `glxinfo` | `build.yaml:174-190` ("Check Linux Mesa provider selection") |
| PPLL offscreen regression | `scripts/perf/test_ppll_capture.py` on `scripts/perf/gl_test_context.py` | Compiles the real append functions of each shader family in a Windows offscreen GL context, `--opengl` selects Mesa | Manual |
| Graphics identity | `scripts/perf/test_graphics_identity.py` | Renderer-family normalisation in `llgraphicsidentity.h` | Manual |
| Release hook policy | `scripts/tests/check_release_hooks.py` | Rejects development render hooks in release sources; passes on this tree | Release changes |
| Renderer stubs | `newview/tests/llpipeline_stub.cpp`, `llglslshader_stub.cpp`, `llviewershadermgr_stub.cpp`, `llviewertexture_stub.cpp`, `llvieweroctree_stub.cpp`, `llsky_stub.cpp` | Link stubs for other newview unit tests; no render behaviour tested | LL tests |
| GLTF material | `llprimitive/tests/llgltfmaterial_test.cpp` | Material data model | LL tests |
| Crate-side parity | vkstorm-core-rust `crates/vkstorm-image/tests/parity.rs`, `tools/image-eval`, `cargo test -p vkstorm-volume`, J2C tests | Byte-level codec and mesher parity | vkstorm-core-rust CI |

There are no tests in `llrender` or for the pipeline, draw pools, culling,
texture policy, probes, environment or post-processing, and no image-based
frame comparison harness on this line. `check_release_hooks.py` names
development probes (`readbackSwapchain`, `gl_capture_frame_once`,
`LLVKUITestScene`, `TextureDeliveryTest`) that are kept out of release sources;
none are present here. Inference: a renderer-rung oracle needs a frame
capture harness on a development branch, and the texture-policy oracle needs
a trace recorder; neither exists yet.

## Risks and open questions

| # | Risk / question | Evidence | Suggested resolution |
|---|---|---|---|
| 1 | **KDU path bypasses the Rust J2C codec.** `USE_KDU` (forced on by `INSTALL_PROPRIETARY`, `cmake/LLKDU.cmake:6-8`) makes `llimage` link `llkdu` instead of `llimagej2coj` (`llimage/CMakeLists.txt:51-55`), selecting a different `fallbackCreateLLImageJ2CImpl` | Build-time, not runtime, but contradicts "no fallback" in spirit and changes the engine string and cache purge | Decide whether `llkdu` stays; if not, remove it and `cmake/LLKDU.cmake` |
| 2 | **Stale OpenJPEG references.** `openjpeg` still in `autobuild.xml`; `integration_tests/llui_libtest/CMakeLists.txt:58-71` copies OpenJPEG DLLs; commented line `newview/CMakeLists.txt:2315` | `doc/rust_port.md` says OpenJPEG is no longer part of the build | Remove or document as dormant |
| 3 | **`RenderBackend=Vulkan` is a silent fallback.** `llappviewer.cpp:3736-3739` runs OpenGL with a log warning | User-visible setting with no backend | Hide the value until a backend exists, or keep and document |
| 4 | **`LLVKProbe` referenced but absent.** Comment at `llappviewerwin32.cpp:1111` | Leftover from another line | Clarify or remove the comment |
| 5 | **Zink depends on a patched Mesa package** (`cmake/MesaZink.cmake` header comment) built outside this repo; `USE_MESAZINK` OFF by default outside CI presets | Reproducibility of Zink builds | Pin and document the package recipe |
| 6 | **`llrender` ↔ `llwindow` link cycle** | See Coupling | Treat both as one boundary; break the cycle before any windowing port |
| 7 | **Global and `thread_local` render state** (`gGL`, `gCubeSnapshot`, `LLPipeline::s*`) | Coupling table | Introduce an explicit view context before porting culling or passes |
| 8 | **Fixed-function remnants** (`glTexGen*` 32 calls, `glPolygonMode` 67) while macOS forces core profile (`llappviewer.cpp:620-623`) | Raw GL counts | Inventory which calls run under core profile; the Vulkan study's `gl-source-inventory.csv` lists candidates |
| 9 | **Rung order vs Vulkan line.** `doc/rust_port.md` puts renderer before Vulkan; `doc/vulkan/` assumes a C++ renderer over DiligentCore/bgfx | Two plans touching the same code | Decide whether the renderer rung ports above a qualified abstraction or above GL; record the decision in both documents |
| 10 | **Floating-point and driver parity for render oracles.** The mesher rung needed per-toolchain corpora; GPU output varies by vendor and driver (also under Zink) | `doc/rust_port.md` mesher section | Keep render oracles CPU-side (draw lists, uniforms, cull sets) rather than pixels where possible |
| 11 | **Texture policy depends on measured VRAM** (`gGLManager.mVRAM`, `LLDXHardware`), which the comment at `llviewertexture.cpp:526` calls biased | Policy port must reproduce inputs exactly | Record inputs in traces, not the derived budget |
| 12 | **PPLL is a Vulkanstorm feature with no automated in-viewer test** (only the offscreen script) | Tests table | Add to any renderer oracle corpus; note GL 4.4 gating (`pipeline.cpp:7986-8001`) |
| 13 | **`llprimitive` links `llrender` without including it** | `llprimitive/CMakeLists.txt:68` | Verify and drop the link, reducing renderer reach into data libraries |
| 14 | Open: whether the SDL1 path (`USE_SDL1`) and `llwindowmesaheadless` are still built anywhere | `llwindow/CMakeLists.txt` | Check CI presets; drop if unused |
| 15 | Open: whether the experimental `GLTFSceneManager` (full-scene GLTF) is reachable in release builds | `gltfscenemanager.cpp` | Trace its entry points before scoping the renderer rung |

### Gaps in this chapter

- Shader origin (LL vs Firestorm) was attributed by program registration
  comments for five files only; the other 220 were not checked individually.
- The frame flow covers the logged-in deferred path; teleport, logout,
  snapshot (`for_snapshot`), cube-face and preview/bake views are named but
  not traced. `doc/vulkan/source-audit.md` and `scene-ui-auxiliary.md` trace
  them at the same source baseline.
- macOS and SDL1 windowing were sized but not read.
- No runtime behaviour, frame timing or memory use was measured.
