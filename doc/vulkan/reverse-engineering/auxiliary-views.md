# Auxiliary views, temporal probes, environment, UI/HUD, and picking

Repository: `anne-skydancer/vulkanstorm`; inspected HEAD `59108e15a1f8f94d2da7c674d937d19f5cf9450d` (clean worktree). This is source analysis, not a GPU execution/capture. All citations are repository-relative paths and **1-based inclusive lines**. `pipeline.cpp` and `llviewerdisplay.cpp` were searched only for connecting callsites; their enclosing policies belong to the parent investigation.

## 1. Frame scheduling and the essential temporal boundary

- Main application order is native window input → canonical `mainloop.post(newFrame)`/coroutine yield → movement-input scanning → `idle()` → `display()` → regular reflection-probe manager update → snapshot floater updates. Sources: `indra\newview\llappviewer.cpp:1662-1713`, `1722-1762`, **`1785-1805`**. In particular, `mReflectionMapManager.update()` is **after the displayed scene**, not before it. Ordinary probes generated here generally first affect the next displayed frame; snapshots taken immediately afterward can consume the newer data.
- The display block requires not exiting, not headless, and a viewer window. Probe/snapshot updates additionally require `STATE_STARTED` (`llappviewer.cpp:1785-1805`). The regular manager independently rejects probes-disabled, teleport display, pre-startup, and logout (`indra\newview\llreflectionmapmanager.cpp:206-219`); it asserts no cube capture is already running.
- `idle()` calls UI layout/event handling even while disconnected (`llappviewer.cpp:6275-6281`). Later world update moves drawables, updates particles, chooses pilot camera / joystick flycam / agent camera, then computes apparent angles with camera and objects in sync (`llappviewer.cpp:6428-6469`). Do not conflate CPU simulation/input updates with render-view work.
- Parent connection callsites (enclosing conditions to be incorporated by parent): `indra\newview\llviewerdisplay.cpp:1593` invokes queued picking; `1842-1843` invokes hero-probe `update()` then `renderProbes()`; `1865` updates environment; `1872` updates HUD objects; `2034` updates sky; `2322` updates environment for cube rendering. Thus mirror rendering is on a different scheduling path from the after-display regular-probe update.

**Vulkan implication (derived):** a single `renderFrame()` that updates all probe maps before the main camera would change temporal behavior. Model main camera, hero cube faces, ordinary cube face, and screenshot re-renders as distinct view jobs with separate target, uniforms, masks, visibility, and presentation policy. Preserve which scene render sees the old versus newly filtered probe version.

## 2. Ordinary reflection probes: residency, scheduling, capture, filtering

### Residency and prioritization

- Maximum ordinary count is 256; irradiance face size is 16 (`indra\newview\llreflectionmapmanager.h:37-41`). Detail levels are static-only=0, static+dynamic=1, realtime=2 (`51-56`). Coverage is separately none / manual / manual+terrain / full including automatic (`indra\newview\llreflectionmap.h:47-53`).
- Coverage relevance uses **actual Firestorm switch logic**, not the commented-out upstream expression: no non-default probes for NONE, manual only for MANUAL_ONLY, no automatic volume-group probes for MANUAL_AND_TERRAIN, all for FULL (`indra\newview\llreflectionmap.cpp:293-343`). Default fallback is explicitly exempted from this relevance skip in manager update (`llreflectionmapmanager.cpp:395-399`).
- Dynamic allocation derives capacity from coverage and observed probe count, with configured rounding/clamping and hard maximum 256 (`llreflectionmapmanager.cpp:228-269`). Cube index 0 is permanently reserved for default (`151-157`, `1579-1585`).
- Manager processes deferred deletion/creation before selecting work (`300-318`). Creation during `gCubeSnapshot` is deferred, because its probe collection is being used by rendering (`531-564`, `682-705`). Automatic volume-group registration accepts volume partition octree nodes of size 15..17 only (`656-679`).
- It finishes one face of the already-in-progress probe first (`335-339`), sorts non-default probes by previously recorded camera distance (`341-345`), frees out-of-capacity slots except the in-progress probe, and assigns free slots to closest candidates (`347-381`). Then it refreshes distances to **surface of influence sphere** = camera-to-origin minus radius (`401-420`). This order creates an intentional/observed one-update lag in the distance sort.
- Both incomplete candidates prioritize distance; both complete candidates use age minus `0.1 * distance`; every third newly selected update lets completed probes bypass incomplete probes to avoid starvation (`170-203`, `501-512`). Complete occluded probes are excluded from normal rendering work but the oldest gets origin/radius and timestamp refreshed (`427-446`, `514-519`). A completed probe's fade-in increases by frame interval toward 1 (`422-425`).
- Default probe follows camera plus 64 m in Z (`134-142`), gets effective distance -4096 before initial completion and 64 afterward (`412-420`), and is forced at least as often as `RenderDefaultProbeUpdatePeriod` when coverage is enabled; with coverage NONE it is limited to that frequency (`488-499`).

### Twelve-face transaction and realtime exception

`indra\newview\llreflectionmapmanager.cpp:752-793` is the authoritative state machine:

1. Render one `mUpdatingFace` through `updateProbeFace`.
2. After six faces, rebuild neighbors and rewind face index.
3. First six are the irradiance-generation phase (`mRadiancePass=false`).
4. Set `mRadiancePass=true` for the next six.
5. After this second group, mark `mComplete=true`, release `mUpdatingProbe`, reset radiance flag.

That is normally **12 manager updates for a newly initialized probe**, one face per update. Already complete probes remain sampleable while refreshing; completion is not cleared at the start of their ordinary refresh.

- At REALTIME detail, the closest resident dynamic probe additionally renders all six faces each manager update. Its separate `mRealtimeRadiancePass` alternates irradiance and radiance phases; manager temporarily overrides and restores the shared radiance flag (`467-486`). “Odd/even frame” is a comment shorthand: actual implementation toggles a boolean after a successful six-face realtime update.
- Non-realtime scratch slot is `mReflectionProbeCount`; realtime uses `mReflectionProbeCount+1` **when the probe differs from `mUpdatingProbe`** (`830-835`). Do not assume independently allocated transient scratch per face: intermediate cube contents persist across manager calls.
- Each update switches `gPipeline.mRT` to `mAuxillaryRT`, clamps local-light contribution for high-ambiance irradiance captures, then restores `mMainRT` (`794-806`, `828`). Default capture further restricts render types to sky/WL sky/water/voidwater/clouds/terrain (`808-825`). Even when local coverage is NONE there remains this environment fallback.

### Filter/data dependencies

- Regular probe resolution is power-of-two, clamped 64..512 (`llreflectionmapmanager.cpp:1504-1508`). Persistent radiance array holds `count+2` cubes with mips; irradiance array holds `count` cubes, 16×16, no mips (`1522-1544`). Capture/filter intermediate target is allocated at `4 * probeResolution` with depth; the mip-target vector has `log2(probeResolution)` entries, from full resolution down to 2×2 (`273-293`). `mMaxProbeLOD=log2(res)-1` (`1519-1520`).
- Each face consumes `mAuxillaryRT.screen`, performs horizontal Gaussian into manager render target and vertical Gaussian back into screen (`860-883`), then shader-downsamples into successive mip targets and copies each to the chosen **scratch cube layer** `sourceIdx*6+face` (`886-927`). This is not simply API-generated mipmaps.
- When `face==5`, the phase chooses radiance convolution or irradiance convolution (`937-1037`). Radiance loops all generated mip levels, uses roughness `i/(mipChain.size()-1)`, and renders all six orientations per mip; outputs copy to resident `probe->mCubeIndex*6+cf` (`942-989`). Irradiance locates the 16×16 mip-sized target and writes one mip for all six resident irradiance faces (`990-1035`).
- Irradiance capture is not literally “no reflections”: uniforms use ambient scale 0 but radiance scale **0.5** during `gCubeSnapshot && !isRadiancePass()`, versus both 1 otherwise (`1144-1150`). Local lights can be reduced independently (`801-805`). The two-stage scheme approximates one-bounce lighting; do not replace it with recursive full indirect feedback.
- API cube storage allocates every requested mip to 1×1 but the manager's filter chain stops at 2×2. Sampling max LOD follows the manager's chain, not the raw allocated number of levels. Array layer order is +X,-X,+Y,-Y,+Z,-Z (`indra\llrender\llcubemaparray.cpp:47-76`); filtering uses distinct clip-to-cube look/up tables (`78-100`). Preserve both capture and convolution orientation conventions.
- `LLCubeMapArray::allocate` chooses RGB packed HDR `R11F_G11F_B10F` or RGBA16F depending components, RGB8/RGBA8 when HDR disabled; cube layers are `count*6`; clamp addressing; anisotropic for mipmapped images and bilinear otherwise (`llcubemaparray.cpp:142-197`). Current resize constructor uses a CPU `glGetTexImage`/scale/upload path (`108-136`), **not** a GPU image-copy abstraction. This should be re-designed with explicit typed per-layer/mip transfers rather than blindly translating raw GL bindings.

**Vulkan implication (derived):** track hazards separately for render targets, scratch cube faces/mips, resident radiance faces/mips, and irradiance layers. The source and destination can be subresources of the same cube-array object, so whole-resource layouts/ownership assumptions are inadequate. Do not sample an uninitialized probe; preserve old complete resident contents while a refresh uses scratch. Retain the six-face barrier before convolution and the first irradiance-then-radiance ordering.

## 3. Probe selection is per camera, not only per frame

- `getReflectionMaps` only includes resident, complete, unoccluded maps; transforms probe origin with **current** `gGLModelView`, computes min/max eye-depth, sorts by depth, and assigns transient packed `mProbeIndex` (`indra\newview\llreflectionmapmanager.cpp:603-654`). `mCubeIndex` is persistent storage identity; `mProbeIndex` is valid only immediately after selection (`indra\newview\llreflectionmap.h:113-125`).
- Uniform update creates 256 one-meter depth buckets (clamped 0..255), tracks influence min/max depth, and sets bucket first-candidate indices (`llreflectionmapmanager.cpp:1116-1178`). Spheres are transformed into current camera coordinates, while manual volumes live-track object movement/scale (`1186-1205`). `refIndex.x` is cube index, `.y` points to neighbor list, `.w` is priority with negative sign encoding a box (`1208-1218`).
- Box transform is inverse(current modelview × object world transform × half-extents), taking camera positions to object unit cube (`indra\newview\llreflectionmap.cpp:256-285`). Automatic origin adjustment raycasts toward eight group corners, moves above ground and encloses geometry; manual box radius is half-scale vector magnitude and manual sphere radius is half X scale (`77-190`). Automatic near clip is half group radius or 1 m for terrain; manual near clip is configured, all clamped to ≥0.1 m (`220-240`).
- Neighbor relationships are symmetric sphere intersections, excluding default and irrelevant probes (`llreflectionmapmanager.cpp:1065-1102`); packed shader list skips unavailable/occluded/nonresident indices, allows ≤64 neighbors per probe, max 4096 scalar entries, and aligns each list start to four integers (`1226-1271`).
- Exact CPU UBO layout is `ReflectionProbeData` in `indra\newview\llreflectionmapmanager.h:58-101`; it includes ordinary data **and** hero data. Hero uniforms are updated and copied before stream-upload (`llreflectionmapmanager.cpp:1302-1325`). `setUniforms()` lazily creates the buffer and binds `UB_REFLECTION_PROBES` (`1343-1354`). Header explicitly requires agreement with `class3/deferred/reflectionProbeF.glsl`; shader owner should verify ABI.
- Preview special policy: `forceDefaultProbeAndUpdateUniforms(true)` temporarily marks all non-default probes occluded and builds fallback-only uniforms; `false` restores CPU occlusion flags but does **not** rebuild uniforms (`1664-1697`). Callsite pair: `indra\newview\llgltfmaterialpreviewmgr.cpp:446`, `554`.

### Occlusion is asynchronous rendering, unlike picking

- Probe occlusion uses bounding-cube `GL_ANY_SAMPLES_PASSED` draw, current shader box uniforms and cube VB (`indra\newview\llreflectionmap.cpp:397-409`).
- Inside radius×sqrt(3)+1, force visible and do not query; otherwise allocate or poll query availability, consume only ready result, keep old state while pending (`346-395`). Shader profiling disables this code (`349-351`). Ordinary manager skips default when issuing queries (`llreflectionmapmanager.cpp:1650-1661`).
- Port to nonblocking Vulkan query availability, preserving “unknown/pending retains previous visibility” and inside-volume exception. A synchronous query wait could serialize the whole frame and alter scheduling behavior.

## 4. Hero/mirror view policy is not the ordinary probe algorithm

- Requires mirrors and reflection probes enabled, started, not teleporting; update also avoids logout (`indra\newview\llheroprobemanager.cpp:76-104`). Registered candidate must be a live drawable reflection-probe **box**, within camera far distance, frustum-visible, with its rotated local +Z face toward camera (`135-179`). Selection requires **both** closer Euclidean distance and smaller camera-direction projection than previous best (`171-177`), not plain closest distance.
- Mirror camera origin is reflection of viewer camera about mirror plane: decompose offset into normal projection and rejection, then `point=reject-project+hero_pos`; store normal, position, and clip plane (`190-225`). This produces cubemap views from reflected eye, rather than a single planar framebuffer.
- Render condition additionally rejects disconnection/logout/dead mirror. Temporarily force regular manager's radiance pass true and mark `mRenderingMirror=true` (`242-264`, `296-301`).
- `RenderHeroProbeUpdateRate` coerces to 1,2,3,6. Render only faces with `gFrameCount % rate == face % rate`, so `6/rate` faces per frame, **then regenerate radiance every frame from the mix of refreshed and retained scratch faces** (`265-294`). Dynamic avatars/particles require mirror's dynamic flag plus hero detail >0 (`284-290`). There is no ordinary twelve-face irradiance/radiance transaction despite copied comments above `updateProbeFace`.
- Switch to `mHeroProbeRT`, call probe update with 0.01 near clip, restore main targets (`254`, `313-328`). Apply Gaussian and mip passes; hero mip pass additionally binds deferred depth and passes near/far uniforms (`353-423`). Note actual depth uniform is `probe->getNearClip()` (`402`), not the 0.01 capture override; preserve/assess this distinction rather than assuming they match.
- Hero filtering uses `gHeroRadianceGenProgram`, always scratch `count+1`, convolves all six faces but loops only **`mMipChain.size()/4`** output mips (`437-499`); hero uniform reports `mMipChain.size()` (`541-543`). Do not claim every advertised hero mip gets freshly convolved.
- Header capacity is 2 (`indra\newview\llheroprobemanager.h:38-39`), allocation is count+2 three-component cube array, but active renderer operates on `mProbes[0]` and reports one hero probe (`llheroprobemanager.cpp:279-301`, `516`, `570-612`). Hero intermediate targets use RGBA16F/RGBA8, whereas allocated final cube uses three components (`106-124`, `587-592`).
- After rendering, `mProbes[0]->mViewerObject=mNearestHero` and `autoAdjustOrigin()` runs (`300-301`), so capture origin and later influence-volume origin have different uses. Uniforms derive sphere/box from object-linked state (`502-543`).
- Important clipping bridge: hero call at `321` does **not** pass `useClipPlane=true` to `LLReflectionMap::update` (default false in `indra\newview\llreflectionmap.h:62`). The actual mirror plane uniform is prepared by `LLSettingsVOWater::applySpecial`, from hero normal/position transformed to current eye space, and `MIRROR_FLAG` follows `isMirrorPass()` (`indra\newview\llsettingsvo.cpp:1084-1121`). Shader owner should connect clip-plane consumers. Merely enabling a hardware custom clip plane is not an equivalent transcription.

## 5. Cube capture is a stateful auxiliary view, not a readback

`LLReflectionMap::update` records update time, resolves dynamic/near-clip policy, and delegates to `LLViewerWindow::cubeSnapshot` (`indra\newview\llreflectionmap.cpp:53-75`).

`indra\newview\llviewerwindow.cpp:6655-6807`:

- Requires deferred and no nested cube snapshot; capture dimensions come from **current target set's deferred-screen width** (`6660-6663`), not from a readback output image.
- Save camera and current modelview/projection; disable occlusion; square aspect **before** 90-degree FOV; set origin and near plane (`6668-6684`).
- Optional custom camera clip plane is saved/set/restored, but hero currently uses shader clipping as above (`6686-6692`, `6794-6797`).
- Push render mask; static capture suppresses avatar, control-avatar and particles (`6694-6717`); disable UI and HUD attachments; replace raw world viewport with square (`6719-6732`).
- Select one +X,-X,+Y,-Y,+Z,-Z face using defined up vectors (`6734-6758`). Set `gDisplaySwapBuffers=false`, `gCubeSnapshot=true`, call **`display_cube_face()`**, then clear flag (`6760-6771`). This function does **not** copy into the cube array; managers later filter/copy target data.
- Restore UI/mask/HUD, clear draw-order lists, restore raw world rect, saved camera and matrices, 3D viewport, occlusion (`6774-6804`). It sets swap flag true rather than restoring arbitrary incoming value (`6774`).

**Vulkan implication (derived):** implement an explicit immutable/stacked view context (camera, model/projection, world rect, UI/HUD masks, occlusion enabled, radiance/mirror mode, target set), not global mutable state leaking between command buffers. Cube capture must not acquire/present a new swapchain image. Copy/convolution is a distinct producer after scene rendering.

## 6. Environment time and invalidation

- Environment update advances time delta, cloud scroll and cached camera yaw **only if not `gCubeSnapshot`** (`indra\newview\llenvironment.cpp:1759-1780`). It still recomputes settings uniforms and marks every shader, rigged variant and GLTF variant dirty during cube rendering (`1782-1798`).
- Sky/water supply per-group uniform blocks (`1826-1837`, `1854-1871`); apply `SG_ANY` plus the shader's own group (`1840-1851`). These are per-view because applySpecial transforms planes using current modelview (see mirror bridge).
- Fixed sky changes use `DayInstance::setSky()` hash difference to request reset (`1241-1257`, `1337-1340`, `2832-2845`). Water setter updates/replaces water but does not itself return a probe-reset signal (`2848-2857`).
- Environment switching creates a time blend only for noninstant transitions with existing sky+water, otherwise directly swaps instance; always rebuild settings uniforms (`1633-1653`). Sky transition begins `pause(duration)` + `reset()` and resumes on blender completion (`2979-3012`); injected animated sky changes use the same pattern (`3581-3617`). Instant injected changes bypass that pause/reset branch.
- Pause skips **non-default** probes; timeout resumes as a safety net (`llreflectionmapmanager.cpp:221-224`, `395-399`, `1046-1054`). Reset is deferred flag only (`1041-1044`); initialization resets phase, residency, neighbors, completion and fade-in but **preserves prior default completion** and reinstates default at slot0/radius4096 (`1547-1586`). Do not assume reset allocates/clears every texture; arrays are recreated only when their dimensions/count require it (`1522-1544`).
- Per-probe fade-in is active. Do not overstate a reset-wide fade: `mResetFade` starts at 1 in the header (`llreflectionmapmanager.h:277`), the inspected update adds/clamps it and capacity change assigns 1 (`llreflectionmapmanager.cpp:226`, `267-268`, `383`), while `reset()` only sets `mReset`. Thus a “reset fades all lighting from zero” description is not supported by this source.
- Sky object has a separate legacy environment-cube path. `updateSky()` updates sun/moon directions, then bypasses legacy generation entirely when probes are enabled (`indra\newview\llvosky.cpp:679-712`). Without probes it compares atmospheric variables/throttles, generates one CPU-math face per call, then uploads/swaps sky/shiny textures and recreates cube after six faces (`714-803`). `forceSkyUpdate` resets cached atmospherics and stage (`670-677`). This legacy cubemap is not the ordinary reflection-probe array.
- Firestorm environment may explicitly disable water draw type when permitted and normal-map asset is `IMG_TRANSPARENT`; it re-enables only if this code disabled it (`llenvironment.cpp:1604-1630`).

## 7. User screenshots: re-render + readback, not picking

### `rawSnapshot`

- Sets no-post and suppresses presentation; temporarily changes UI/HUD visibility, chooses full world viewport when UI is omitted; snapshots with UI are limited to screen size (`indra\newview\llviewerwindow.cpp:6183-6244`).
- Oversized deferred snapshots without UI may allocate scratch target and temporarily resize pipeline screen buffers and camera viewport/aspect; otherwise compute crop/zoom tile factors (`6250-6314`).
- For each tile, mark depth dirty and invoke **`display(do_rebuild, scale_factor, subfield, true)`** (`6350-6374`). Nondeferred fallback explicitly invokes `render_ui`/`swap` (`6376-6382`), while suppressing real presentation with the flag.
- Reads RGB8 color or float GL depth row-by-row (`6384-6468`); depth screenshots convert with source's near/far formula to 8-bit grayscale or packed 24-bit RGB (`6347-6348`, `6412-6466`). Native Vulkan depth-range/reversed-Z conventions must be translated to this export contract or purposefully changed/tested; copying raw Vulkan depth bytes will not reproduce output.
- Cleanup marks depth dirty, restores HUD, re-enables UI if disabled, rescales output, clears draw orders when rebuilt and restores screen buffers/viewport after resize (`6478-6541`). Note rawSnapshot re-enables UI rather than fully restoring `prev_draw_ui`; simple/cube paths instead condition re-enable on the saved flag. Describe these as actual asymmetries, not an ideal state guard.

### `simpleSnapshot`

- Always hides UI/HUD, creates requested-size scratch, resizes screen buffers, and renders **`num_render_passes`** copies of `display(..., for_snapshot=true)` (`6546-6613`). Its comment explicitly says repeated renders mitigate lagging occlusion state/missing objects (`6588-6596`).
- Single RGB readback follows; depth dirty, draw order, targets and visibility restored afterward (`6615-6648`). This is the 360-capture-related convenience path, distinct from `cubeSnapshot`'s GPU-only probe scene render.

**Vulkan implication (derived):** provide offscreen rerender targets, size restoration, tile/crop transforms, optional post/UI/HUD, GPU→host staging with completion, and no present during captures. Screenshot calls may execute additional scene renders inside one application frame; do not advance simulation simply because another render pass is recorded. Only cube snapshots explicitly freeze environment time through `gCubeSnapshot`.

## 8. UI, world labels, HUD attachments: three distinct layers

- UI CPU update runs layouts before calculating world rect; mouselook or disabled UI chooses full-window world area (`indra\newview\llviewerwindow.cpp:3872-3884`). `setup2DRender/Viewport` uses raw **window** dimensions; `setup3DRender/Viewport` uses raw **world-view** dimensions and offsets (`6904-6935`). They are not necessarily identical.
- `LLViewerWindow::draw` resets modelview, binds `gUIProgram`, scales by display scale; screenshot zoom/subregion additionally translates/scales UI and LLUI scale factor (`2985-3055`). It draws active tool overlay first (`3058-3059`), root view next, then separate top-most control (`3165-3185`), restoring scale/stacks (`3201-3208`).
- UI buffer disabled means full-window dirty rect (`3002-3005`). `LLView::drawChildren` traverses reverse child order, skips invisible/invalid/off-root/clean-rect widgets, applies UI translation per child (`indra\llui\llview.cpp:1288-1340`). Dirty rect accumulates a high-level ancestor's screen rect (`1343-1363`). Retained UI-buffer rendering must preserve untouched pixels, not clear full buffer every update.
- Nested UI clipping intersects clip-stack rectangles, flushes pending geometry before changing scissor, scales/floors origins and ceils extents **plus one pixel** (`indra\llui\lllocalcliprect.cpp:35-106`). Vulkan viewport orientation and scissor conversion must preserve this contract and nested restoration.
- `LLHUDView` is a UI widget that draws tracking arrow and children and handles arrow clicks (`indra\newview\llhudview.cpp:58-71`). This is not 3D HUD attachment geometry.
- `LLHUDObject::updateAll()` updates text, icons, nametags then sorts objects (`indra\newview\llhudobject.cpp:284-290`). `renderAll()` binds UI shader and defaults depth off, rendering visible HUD objects in sorted order (`294-321`), but subclasses override depth:
  - World floating text enables depth-test/no depth-write, except hover-highlight option can disable depth-test (`indra\newview\llhudtext.cpp:123-139`).
  - Avatar nametags enable depth-test/no depth-write (`indra\newview\llhudnametag.cpp:226-233`).
  - Icons explicitly enable depth-test, compute camera-facing quad geometry, distance/lifetime fade and bounce animation (`indra\newview\llhudicon.cpp:83-176`).
- Text on HUD attachments is kept in a separate visible list; considered visible if parent live and sorted by HUD-space X depth (`llhudtext.cpp:481-494`, `693-711`). `renderAllHUD()` draws that list with depth disabled (`748-765`). World text rejects behind-camera, far/faded/frustum-excluded content and moves label toward camera by object radius (`497-576`).
- Nametags perform nearest-first screen-area LOD assignment, pairwise screen-rectangle overlap relaxation, then damped position interpolation; skip overlap solve when camera velocity is high (`llhudnametag.cpp:720-837`). Matching only glyph rendering misses temporal label positioning.
- `hud_render_text` projects world/HUD anchor with current matrices and raw world viewport, switches to orthographic text rendering, compensates font scale, and reconstructs GL clip-depth translation `-(2*z-1)` (`indra\newview\llhudrender.cpp:56-135`). Preserve depth mapping for world text even though fonts use UI-style orthographic rendering.
- Selection visuals are render work: update silhouettes once for non-HUD/non-pick call; separate world/HUD silhouettes; HUD selection uses aspect-adjusted orthographic projection with avatar HUD bounding box and coordinate-frame rotation (`llviewerwindow.cpp:5147-5178`, `5248-5265`). `for_gl_pick` naming does not imply these visuals implement ordinary input hit-testing.
- Effects must continue aging when UI is absent: `renderAllForTimer()` explicitly invokes effect timers even if effect not visible (`llhudobject.cpp:325-350`). `LLHUDManager::updateEffects` calls live-effect update and `sendEffects` is network serialization, not a GPU render pass (`indra\newview\llhudmanager.cpp:59-103`).
- Parent callsite bridge: world/UI HUD rendering and timer-only variants appear at `indra\newview\llviewerdisplay.cpp:2678-2691`, with additional timer paths `1542`, `2887`; parent should place these relative to postprocessing/composite and inspect enclosing conditions.

## 9. Picking is predominantly CPU ray intersection, not an ID framebuffer

- `pickAsync` constructs `LLPickInfo`, queueing up to 1024; minimized/overflow returns immediate empty callback. Scheduling deliberately delays input so mouse-up cannot overtake mouse-down's pick (`indra\newview\llviewerwindow.cpp:5420-5458`).
- `performPick` iterates queue, calls `fetchResults`, saves last pick and clears queue (`5461-5473`). `pickImmediate` directly calls the same `fetchResults` (`5492-5508`) despite stale “Performs the GL object/land pick” comment.
- `cursorIntersect` constructs a HUD-space segment and a camera/world ray starting on near plane (`5537-5608`); tests specific object if supplied, otherwise HUD intersections **first**, then world intersections only if none (`5610-5647`). World route has transparent/rigged/unselectable/reflection-probe selectors and GLTF node/primitive outputs.
- Mouse world ray is analytic from camera axes, vertical FOV and scaled world-view center/height; HUD coordinates divide by height and HUD zoom (`5677-5716`). No render-target pixel readback occurs here.
- `fetchResults` CPU-tests icons and objects at 512 m, prefers nearer icon, handles terrain via land-intersection helper, calculates focus/surface metadata, optionally intersects particles to previous world hit, and calls callback (`7518-7657`). Icon intersection builds the same camera-facing quad and calls `LLTriangleRayIntersect` on two triangles (`indra\newview\llhudicon.cpp:199-314`).
- Input policy also includes build-mode invisible selection, debug-alpha selection (`llviewerwindow.cpp:5429-5435`, `5494-5501`), HUD priority, camera-tool behavior, teleport/RLVa interaction restrictions (`5637-5668`), and overlay blocking of non-HUD hits (`7548-7560`).

**Vulkan implication (derived):** keep CPU picking with the correct main-camera matrices/viewport and HUD zoom, and preserve callback/event ordering. Do not add a mandatory GPU ID/color pass or GPU→CPU stall for normal clicks. GPU occlusion queries, screenshot color/depth readback, and selection-outline rendering are separate operations with different dependencies.

## 10. Short parity checklist / parent handoff

1. Preserve after-display ordinary updates vs in-display mirror updates and subsequent screenshot renders.
2. Keep persistent two-phase scratch history, six-face convolution trigger, completion gating, prior resident contents, closest-dynamic alternation and per-probe fade.
3. Port all face orientations, packed HDR formats, filter-generated mips and irradiance size; descriptor/view layouts must permit sampled scratch while writing different resident layers.
4. Rebuild camera-dependent packed probe metadata per view; preserve box inverse transforms, depth buckets, neighbors and UBO ABI.
5. Mirror camera reflection, face cadence, shader clip-plane/MIRROR_FLAG and subset mip-generation policy are not ordinary probes.
6. Environment animation freezes for cube jobs but its uniforms remain view-dependent; sky transition pause/reset/resume still allows default updates.
7. UI buffer retention, nested scissor, DPI/world offsets, world-label depth and orthographic HUD attachments need separate state.
8. Screenshot rerenders/readback suppress presentation and can resize targets; picking remains CPU geometry with queued input semantics.
9. Evidence caveats: copied hero twelve-pass comment does not match implementation; default-reset completion preserved; reset-wide fade-from-zero unsupported; cubeSnapshot does not perform cube copy; picking's “GL” comment is misleading.

No repository files were changed. Validation performed: direct source/callsite inspection plus clean `git status --short` and HEAD verification; no build/runtime parity claim is made.
