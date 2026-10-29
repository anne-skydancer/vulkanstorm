# 06 — Feature completeness

The Vulkan path is a separate, connected viewer, not a renderer swapped in
under the OpenGL viewer. It logs in through the shared `idle_startup()` sequence,
then hands the session to a CPU-side `VSNativeSession`. That session owns its own
region, object, avatar and terrain state and deliberately does not create
`LLWorld`, `LLViewerRegion` or `LLViewerObject`. Today it draws terrain, an
atmospheric sky dome, a flat water plane, prims (including sculpts, flexis and
meshes), glTF scenes, CPU-skinned avatars with attachments and animesh, particles,
HUD attachments, name tags, beacons and the full 2D UI. It does this with one
forward shader that has sun and ambient lighting only. There are no shadows, local
lights, reflection probes, specular or BRDF shading, real water, clouds, sun or
moon discs, stars, tone mapping, anti-aliasing or post effects. Glow is
accumulated into an off-screen target that nothing reads. Chat, IM, inventory,
people/radar, minimap, world map, teleport, region crossing, audio, media and
snapshots all have wired Vulkan paths. Building is the clearest refused area: the
`build` floater, the build toolbar command and the pie-menu Edit, Take, Delete and
Return items are all outside the Vulkan admission sets, so users cannot reach
them. The parity ledger records only one of 3131 entries (`FEATURE.Startup`) as
source-qualified, and none as authenticated-runtime-qualified. Nothing in this
chapter was run. "Implemented" means only that code exists and is wired into the
Vulkan frame or session.

Scope and method: worktree `codex/vulkan-survey` at `51d1f729a6`. Shell commands
are quoted so readers can rerun the counts. All paths are relative to the
repository root unless they start with `indra/newview/`, written here as
`newview/`.

## How the Vulkan renderer is selected

### Build-time gate

| Item | Evidence | Effect |
|---|---|---|
| `USE_DILIGENTCORE` option, default `OFF` | `indra/cmake/DiligentCore.cmake:4` | Without it, no Vulkan code is compiled. |
| Platform restriction | `indra/cmake/DiligentCore.cmake:9-11` | Only Windows/Linux x64 are supported; macOS cannot build the Vulkan path. |
| `VS_NATIVE_VULKAN=1` define | `indra/newview/CMakeLists.txt:2766-2767` | Set only when `USE_DILIGENTCORE` is on. |
| Diagnostics/replay sources | `indra/newview/CMakeLists.txt:38`, `:2390-2405` | 19 of the 100 `vs*.cpp` files (`vsvkdiag`, `vsr*replay`, `vsparity*`, `vsuifixture`, `vsuipixeloracle`, `vsnativereplay`, `vsnativeimreplay`, …) build only with `VS_VULKAN_DIAGNOSTICS=ON`, which also requires `RelWithDebInfo`. |
| CI that enables it | `.github/workflows/software_vulkan.yml:673` (`-DUSE_DILIGENTCORE:BOOL=ON -DVS_VULKAN_DIAGNOSTICS:BOOL=ON`) | This is the only workflow that turns the Vulkan path on (`grep -rln DILIGENTCORE .github`). |

Inference: ordinary Release/CI viewer builds do not contain the Vulkan renderer.
In those builds `VSRenderBackend::canStartSession("Vulkan")` returns `false`
(`newview/vsrenderbackend.h:11-17`).

Code size split (`ls newview/vs*.cpp newview/vs*.h`, then `wc -l`):

| Group | Files | Lines |
|---|---:|---:|
| All `vs*.cpp/.h` | 280 (100 `.cpp`, 180 `.h`) | 63,353 |
| Production (names not matching `replay|readback|vkdiag|parity|fixture|pixeloracle`) | 241 | 51,501 |
| Diagnostics/replay/fixtures | 39 | 11,852 |
| `VS_NATIVE_VULKAN` lines in shared (non-`vs*`) files | 233 files | 1,299 lines |

### Run-time selection

1. **Setting.** `RenderBackend` is a persisted string with values `OpenGL`
   (default), `Vulkan` or `Zink` (`newview/app_settings/settings.xml:12285-12295`).
   `RenderBackendPending` is a non-persisted shadow that the Preferences combo
   binds to (`settings.xml:12296-12305`). Unknown values are normalised to
   `OpenGL` (`newview/vsrenderbackend.h:7-10`, `newview/llappviewer.cpp:4193-4196`).
2. **Preferences UI.** Preferences → Graphics → Hardware has a combo
   `render_backend` with items "OpenGL", "Mesa/Zink" and "Vulkan (in
   development)" (`newview/skins/default/xui/en/panel_preferences_graphics1.xml:966-990`).
   On commit, `onRenderBackendCommit` (`newview/llfloaterpreference.cpp:4138-4181`)
   does the following:
   - If the build cannot run a Vulkan session, it shows the `VulkanRendererUnavailable`
     notification ("Vulkan is still in development and cannot run a normal
     viewer session yet. Your renderer selection has not changed",
     `notifications.xml:1748-1755`) and resets the combo (`:4143-4147`).
   - Otherwise it shows `ChangeRenderBackend` ("Shutdown now" / "Later"). Only
     "Shutdown now" writes `RenderBackend` and quits (`:4183-4198`). "Later"
     reverts the combo, so nothing is saved (`:4200-4203`).
   - The `VulkanNotAvailableTooltip` string (`strings.xml:3021`) is not referenced
     by any `.cpp` (`grep -rn VulkanNotAvailableTooltip --include=*.cpp`). No code
     probes for a Vulkan driver before the user selects Vulkan.
3. **Startup admission.** After `initConfiguration()`, a build without Vulkan
   support that has `RenderBackend=Vulkan` shows `MBVulkanRendererUnavailable` and
   exits (`newview/llappviewer.cpp:1040-1045`). The string says this explicitly:
   "there is no OpenGL fallback" (`strings.xml:9`). The window path refuses again
   (`llappviewer.cpp:4176-4181`).
4. **Vulkan entry.** In a Vulkan build with `RenderBackend=Vulkan`,
   `LLAppViewer::init` sets `mNativeVulkanLogin` and returns
   `initNativeVulkanLogin()` (`llappviewer.cpp:1070-1076`). This skips the rest of
   the OpenGL `init()`. The Vulkan init (`llappviewer.cpp:1590-1683`):
   - starts the shared CPU services (texture cache/fetch/decode, mesh repo, voice
     client, floater registry, IM manager, settings listeners);
   - creates the window through `LLViewerWindow(..., render_backend=="Vulkan")`,
     whose constructor calls `initNativeWindow()` (`llappviewer.cpp:4223-4231`);
   - `initNativeWindow()` creates the Diligent `VSVulkanContext`, installs the UI
     admission sets (`vs_startup_ui_admission()`) and builds the Vulkan root view
     (`newview/vsviewerwindow.cpp:137-189`).
5. **Frame loop.** Each frame runs `frameNativeVulkanLogin()` instead of `doFrame()`
   (`llappviewer.cpp:1956`, `:1977`). Its steps (`llappviewer.cpp:1700-1828`):
   - Below `STATE_WORLD_INIT` it calls `idle_startup()` (the shared login). At
     `STATE_WORLD_INIT` it calls `VSNativeSession::begin()`.
   - It then calls `tick()`, the AFK check, audio, media focus and the snapshot
     controllers.
   - Finally it calls `gViewerWindow->drawNativeUI()`, which renders world and UI
     through `VSVulkanContext` (`newview/vsvulkancontext.cpp:560-640`).
6. **Failure behaviour.** If the Vulkan window or context fails during creation,
   `initNativeWindow` releases what it built and rethrows
   (`newview/vsviewerwindow.cpp:177-189`).
   Inference: there is no automatic or in-session fallback to OpenGL or Zink. The
   user must edit `RenderBackend` back. The tooltip also calls the two renderers
   "peer renderers" (`panel_preferences_graphics1.xml:976`), and
   `doc/vulkan/full-parity-roadmap.md:20-22` states that GL/Zink is "not a Vulkan
   fallback".
7. **Zink is not Vulkan.** `Zink` runs the OpenGL pipeline over Mesa's
   GL-on-Vulkan driver (`newview/llappviewerwin32.cpp:1103-1125`,
   `llappviewer.cpp:4183-4192`). None of the `vs*` code is involved.

### Three admission layers decide what is reachable under Vulkan

| Layer | Where | Mechanism | Size |
|---|---|---|---|
| Simulator messages | `newview/vsnativesession.cpp:193-198`, `:1346-1373` | `LLMessageSystem::setMessageAdmission` → `VSNativeSession::admit`. Messages outside the sets are dropped. The first 8 are logged as "Deferred Vulkan message" (`:1371`). | 12 world messages, 116 session messages, 3 transport messages, plus about 15 named special cases |
| UI construction | `indra/llui/vsuiadmission.h:10-42`, installed at `newview/vsviewerwindow.cpp:159` | Closed sets for widget types, floater names, panel factories, toolbar commands, menu/pie callbacks and XUI children. Enforced in `llfloaterreg.cpp:187,308,316,378,545,621`, `lluictrlfactory.h:138-242`, `llpanel.cpp:369,849`, `lltoolbar.cpp:433,996,1109,1155,1422`, `llmenugl.cpp:621-936` and `pieslice.cpp:59-73`. | 206 floater names, 106 panel names, 57 toolbar commands, about 494 widget `typeid`s, about 220 literal callback names plus 10 `isNativeCallback` helpers (`newview/vsstartupui.cpp:397-853`) |
| Graphics settings | `newview/vsgraphicssettings.h:16-49`, used at `newview/llfloaterpreference.cpp:2229-2257` and `newview/quickprefs.cpp:1097` | Preferences controls bound to feature-table settings or to "pending passes" are disabled unless a Vulkan consumer exists. | 13 implemented names + 3 capability-dependent names; 11 named pending passes |

What the user sees when something is refused:

- **Floater not admitted.** `LLFloaterReg::showInstance` returns null and calls
  `VSUIAdmission::unsupported`. This raises `GenericAlert` with "[CONTROL] is not
  available in this Vulkan development stage"
  (`newview/vsstartupui.cpp:606-611`, `strings.xml:8`).
- **Menu or pie item whose callback is not admitted.** The item is built but
  disabled, and its commit is ignored (`indra/llui/llmenugl.cpp:847-860`,
  `newview/pieslice.cpp:59-60`).
- **Toolbar command not admitted.** It is dropped from the toolbar
  (`indra/llui/lltoolbar.cpp:433`).
- **Widget type not admitted.** The factory returns null and logs "Rejected Vulkan
  widget type" (`newview/vsstartupui.cpp:555`).

## Feature matrix

Status legend:

- **Implemented**: Vulkan code exists and is wired into the session or frame for
  the main reference behaviour.
- **Partial**: wired, with named sub-features missing.
- **Stub**: a visible substitute stands in for the reference behaviour.
- **Refused**: the user entry point is blocked by admission. There is never an
  OpenGL fallback inside a session.
- **Absent**: no Vulkan code path.

The "Ledger" column quotes `doc/vulkan/parity/acceptance-ledger.json`
`implementation` values. Its free-text notes come from
`doc/vulkan/parity/source-dispositions.json`.

### Startup, session and navigation

| Feature | Status | How it is reached under Vulkan / why not | Evidence | Ledger |
|---|---|---|---|---|
| Login (credentials, MFA, TOS, grid) | Implemented | The shared `idle_startup()` runs up to `STATE_WORLD_INIT` inside the Vulkan frame loop. Login-stage widgets such as `LLFloaterTOS` and toasts are admitted. | `newview/llappviewer.cpp:1756-1759`; `newview/vsstartupui.cpp:549`; MFA covered by a diagnostic at `newview/vsvkdiag.cpp:1450-1460` | `FEATURE.Startup`: `implemented`, the only `source_qualified`/`software_runtime_qualified` entry |
| Login/logout progress screens | Partial | `setShowProgress`/`setProgressString` are driven from the Vulkan session. | `newview/vsnativesession.cpp:1979-1986` | `FEATURE.LoginProgress` and `FEATURE.LogoutProgress`: `partial` |
| Circuit, capabilities, simulator features | Implemented | `VSNativeSession::begin()` sends `UseCircuitCode`. Circuits are tracked per host. | `newview/vsnativesession.cpp:579-610`, `vsnativecircuits.cpp` | Covered by `FEATURE.Startup` |
| Teleport (location, landmark, lure, home, cancel) | Implemented | Vulkan senders for `TeleportLocationRequest`, `TeleportLandmarkRequest` and `TeleportLureRequest`. Progress, failure and local-teleport handlers drive the shared progress view. | `newview/vsnativesession.cpp:1216-1273`, `:1335-1340`, `:1939-1990` | No dedicated entry. App routes are in `APP_COMMAND` (`unqualified`). |
| Region crossing | Implemented | `CrossedRegion` and `TeleportFinish` call `changeRegion()`, which reuses or enables neighbour circuits. | `newview/vsnativesession.cpp:1276-1300`, `:1933-1938` | `SURVEY.Neighbour-simulators-and-variable-regions`: `unqualified` |
| Neighbour regions / variable-size regions | Partial | `EnableSimulator` is handled and the scene keeps per-region owners with terrain seam stitching. | `newview/vsnativesession.cpp:1488`; `newview/vsworldscene.cpp:885-920` | Same as above |
| SLURL / teleport via SLAPP | Partial | Same-grid SLURLs teleport. Cross-grid SLURLs show `NativeSessionRegionUnavailable`, whose text still says "Region changes are not available in this Vulkan UI/chat stage" (wording is stale now that changes are supported). | `newview/llurldispatcher.cpp:233-252`; `strings.xml:3365` | `APP_COMMAND.*`: `unqualified` |
| Logout / quit | Implemented | `requestLogout` and `logoutFramePresented`; `cleanupNativeVulkanLogin`. | `newview/vsnativesession.h:55-57`; `newview/llappviewer.cpp:1830ff` | `FEATURE.LogoutProgress`: `partial` |
| Persistent disk texture cache | Partial | `LLTextureCache` and `LLTextureFetch` are created in Vulkan init, but the Vulkan image consumers are not cache subscribers per the ledger. | `newview/llappviewer.cpp:1607-1612` | `FEATURE.PersistentTextureCache`: `native-subscriber-integration-pending` |

### World rendering

All world drawing goes through one PSO family named "Vulkan world geometry"
(`newview/vsworldrenderer.cpp:553-589`). The fragment shader has four branches: sky,
fullbright/solid, four-layer terrain, and textured surface
(`vsworldrenderer.cpp:444-483`). Lighting is
`ambient + sunColour * max(dot(n, sun), 0)` (`:467`). The source says explicitly
that ORM and legacy specular inputs "remain material inputs for R5/R6, rather than
inventing a replacement BRDF" (`:479-480`).

| Feature | Status | How it is reached / why not | Evidence | Ledger |
|---|---|---|---|---|
| Terrain (heightfield, patches, 4-texture splat) | Partial | `LayerData` is decoded into ground patches. Packets use four swatch textures plus the alpha ramp, ported from `terrainF.glsl`. Terrain-PBR material snapshot via `VSTerrainAssets`. | `newview/vsworldscene.cpp:1071-1170`; shader `vsworldrenderer.cpp:454-464`; `vsterraingeometry.cpp`, `vsterrainshader.h` | `SURVEY.Terrain`: `partial` ("four-way/PBR splatting, geometry LOD … remain"); 24 `SETTING` entries owned by "Vulkan local terrain asset/transform controller" |
| Sky / atmosphere | Partial | A sky dome is built with `WLSkyDetail` tessellation. The vertex shader ports the reference haze/sunlight calculation from EEP uniforms (`environmentSky`), with rainbow and halo textures. Without an EEP sky, a fixed two-colour gradient quad is drawn instead. | `newview/vsworldscene.cpp:1077-1135`; `vsworldrenderer.cpp:220-312`, `:445-450`; `vsenvironmentstate.cpp:47-85` | `SURVEY.Analytic-atmosphere-sky-sun-moon-clouds-stars`: `placeholder` (see Ledger coverage: likely stale) |
| Clouds, stars, sun/moon discs | Absent | Cloud uniforms are packed into the environment buffer (`vsenvironmentstate.cpp:80-82`), but no cloud, star or celestial geometry is drawn. The shared `LLSettingsVOSky` update returns early under Vulkan before `gSky.setSunTextures`. | `newview/llsettingsvo.cpp:680`; no `cloud`/`star` producer in `vsworldscene.cpp` (`grep -n -i cloud newview/vsworldscene.cpp` → 0) | Same survey row |
| EEP day cycle / environment editors | Partial | The environment floaters (`env_fixed_environmentent_sky/water`, `env_edit_extdaycycle`, `my_environments`, `env_adjust_snapshot`) and the `World.EnvSettings`/`World.EnvPreset` callbacks are admitted. The sky shader consumes the current EEP sky. | `newview/vsstartupui.cpp:558-581`, `:690-707`; commit `74620e4483` | `SURVEY.EEP-day-cycle`: `unqualified` |
| Water | Stub | One flat quad per region at `water_height`, fixed RGBA `(.08,.28,.38,.65)`, alpha-blended. No reflection, refraction, normals, fog or underwater handling. `LLSettingsVOWater` updates also return early under Vulkan. | `newview/vsworldscene.cpp:1171-1178`; `newview/llsettingsvo.cpp:1155` | `SURVEY.Water-void-water-haze-exclusion`: `placeholder`; `FEATURE.Underwater`: `not-integrated` |
| Prims (volumes, linksets, TEs) | Partial | Object updates (full, compressed, cached, terse) are parsed into Vulkan `Object`s. Volumes are tessellated with LOD; parent/child transforms; per-face texture, colour, alpha, fullbright and glow. | `newview/vsworldscene.cpp:1439-1550`, `:557-580`; `vsvolumelod.h` | `SURVEY.Standard-primitives-linksets`: `partial` |
| Sculpts | Partial | The sculpt ID is decoded from extra params, and a sculpt generation drives the rebuild. | `newview/vsworldscene.cpp:198`, `:423`, `:4495` | `SURVEY.Sculpt`: `unqualified` |
| Flexible prims | Partial | The shared CPU solver `vs_flexible_parameters`/`vs_flexible_step` runs in the scene, with wind. | `newview/vsworldscene.cpp:406`, `:4541-4552`; `vsflexiblepath.cpp` | `SURVEY.Flexible`: `unqualified` |
| Meshes (assets, LOD, decode) | Partial | `VSWorldAssets` handles mesh header/LOD decode, cache and retry through the mesh capability, within a budget. | `newview/vsworldassets.cpp:90`, `:217-260` | `SURVEY.Mesh-assets-LOD`: `partial` ("R1 … software qualified … Full geometry/texture/local-editing/material producer parity remains") |
| Trees and grass | Partial | `VSVegetation` holds the shared CPU tree/grass producers extracted from the reference viewer. Grass is rebuilt when terrain changes. | `newview/vsvegetation.h:1-12`; `newview/vsworldscene.cpp:891-918` | `SURVEY.Trees-grass`: `unqualified` |
| Avatars: body, wearables, bakes | Partial | `VSWorldAvatar` owns `LLAvatarAppearance`/`LLWearableData`. It handles `AvatarAppearance`, local bake compositing (with budget refusals) and body LOD. | `newview/vsworldavatar.cpp:159`, `:745-866`, `:1415-1460`, `:1957` | `SURVEY.Self-other-system-avatars`: `partial` |
| Avatars: animation | Partial | `AvatarAnimation` → CPU `LLCharacter` motions; skeleton reset and resync. Built-in AO ownership is not established. | `newview/vsworldavatar.cpp:1111-1157`, `:1460-1541`; `vsavatarmotions.cpp` | `FEATURE.BuiltinAO`: `native-avatar-integration-pending` |
| Attachments, rigged mesh, animesh | Partial | Attachment-point transforms, CPU palette skinning of rigged faces (`skinRiggedFace`) and control avatars for animesh. | `newview/vsworldavatar.cpp:1820-1956`; `newview/vsworldscene.cpp:402`, `:512` | `SURVEY.Rigged-attachments-animesh`: `partial` |
| Avatar complexity / jellydoll / impostors | Partial | Jellydoll and too-complex visibility policy is implemented. Impostor settings are read, but no impostor rendering exists. | `newview/vsworldavatar.cpp:406-502`, `:446` | `SURVEY.Avatar-cloth-physics-visibility`: `unqualified` |
| Particles | Partial | `VSParticleEffects` admits shared `LLViewerPart` sources and appends packets every frame. Particle picking and beams are supported. | `newview/vsparticleeffects.cpp:103-160`, `:299`, `:577`; frame hook `vsvulkancontext.cpp:589` | `SURVEY.Particles-effects`: `unqualified` |
| Alpha / transparency | Partial | Packets are stable-sorted: background, then opaque, then alpha back to front, then overlay, then HUD. An alpha-mask cutoff exists (`material.y`/`material.z`). No pre/post-water split and no order-independent transparency. | `newview/vsworldrenderer.cpp:920-935`, `:471`; `vsfacealphapolicy.h` | `SURVEY.Alpha-masks-PPLL`: `partial`; 87 `DRAW` passes: `native-reference-pass-integration-pending` |
| Shadows (sun, spot) | Absent | No shadow maps. The only `shadow` hits in Vulkan code are a protocol `CastsShadows=false`, cloud-shadow sky maths and avatar "too slow without shadows" bookkeeping. | `grep -n -i shadow newview/vs*.cpp` (20 hits, none a shadow pass) | `SURVEY.Sun-spot-shadows`: `not-integrated` |
| Local / projector lights | Absent | `PARAMS_LIGHT` is recorded on objects (`vsworldscene.cpp:1506-1507`), but the shader uses only `sun` and `ambient`. | `newview/vsworldrenderer.cpp:367`, `:467` | `SURVEY.Sun-moon-local-projector-lights`: `partial` ("fixed sun/ambient shader inputs only") |
| PBR / glTF materials on faces | Partial | `VSMaterial` snapshots `LLGLTFMaterial`: base colour (sRGB→linear), normal map, emissive, occlusion and UV transforms. Metallic and roughness are uploaded (`vsworldrenderer.cpp:1027`) but unused. No image-based lighting and no BRDF. | `newview/vsworldrenderer.cpp:465-483`; `vsmaterial.h`, `vspbrfacepolicy.h` | `SURVEY.GLTF-PBR`: `unqualified` |
| glTF scenes (non-material assets) | Partial | `VSWorldGLTF` owns glTF scenes with CPU animation. | `newview/vsworldgltf.h:1-8`; `newview/vsworldscene.cpp:420-421` | `SURVEY.GLTF-scene-assets…`: `unqualified` |
| Legacy materials (normal/specular/shiny/bump) | Partial | Normal maps, including those generated for procedural bump through `VSUIResources::legacyBump`, are applied through `mappedNormal`. Specular maps, shininess and environment are not shaded. | `newview/vsworldscene.cpp:4744-4753`; `newview/vsworldrenderer.cpp:422-443`, `:479-480` | `SURVEY.Legacy-diffuse-bump-shiny-normal-specular`: `unqualified` |
| Animated textures (TE anim) | Partial | `TextureAnim` is decoded into `LLViewerTextureAnim` and its mode is kept per object. | `newview/vsworldscene.cpp:213-218`, `:389-395` | `SURVEY.Animated-TE-PBR-texture-matrices`: `unqualified` |
| Reflection probes / mirrors | Absent | Only probe parameters for editing (`VSEditObject::getReflectionProbe*`) and highlight selection exist. No probe capture or sampling. | `newview/vseditselection.cpp:580-690`; `newview/vsworldscene.cpp:5105` | `SURVEY.Probes-irradiance-radiance-mirrors`: `not-integrated` |
| Glow | Partial (not visible) | With `RenderGlow`, emissive and alpha packets are drawn a second time into an RGBA16F glow target. `glowView()` has no caller outside its definition, so nothing composites or blurs the target. | `newview/vsworldrenderer.cpp:893-901`, `:1214-1215`; `grep -rn "glowView()" newview` → definition only | `RenderGlow*` listed as pending passes in `vsgraphicssettings.h:38-39` |
| Post-processing (HDR, exposure, tone map, gamma, FXAA/SMAA, DoF, CAS, SSAO, SSR) | Absent | No tonemap/exposure/FXAA/SMAA/SSAO code (`grep -ci` → 0 each). The world target is sRGB RGBA8/BGRA8. | `newview/vsworldrenderer.cpp:193`; `vsgraphicssettings.h:36-42` | `SURVEY.HDR-…`, `SURVEY.Glow-DoF-FXAA-…`, `SURVEY.SSR-SSAO-history`: `unqualified` |
| Wireframe | Implemented | `gUseWireframe` → triangle wireframe for scene packets when the device supports it. | `newview/vsvulkancontext.cpp:577`; `newview/vsgraphicssettings.h:51-56`; admitted `Advanced.ToggleWireframe` (`vsstartupui.cpp:661`) | — |
| HUD attachments | Partial | HUD packets are drawn last, with an orthographic projection mirroring `get_hud_matrices()` and HUD zoom. | `newview/vsworldrenderer.cpp:936-966`; `vsvulkancontext.cpp:576` | `FEATURE.HUDProjection`: `not-integrated` (see Ledger coverage) |
| Name tags | Implemented | `LLHUDNameTag::renderNative()`/`finishNative()` are drawn as a UI pass before the HUD and depth-tested against world depth. Policy in `VSAvatarNameTagPolicy`, with budget refusals. | `newview/vsvulkancontext.cpp:621-630`; `newview/llhudnametag.cpp:999-1010`; `newview/vsavatarnametagpolicy.h` | UI/setting entries `unqualified` |
| Prim hover text (`llSetText`) | Absent | Inference: the `Text` field of compressed updates is read and discarded (`vsworldscene.cpp:1461`). No Vulkan `LLHUDText` exists for objects. Only beacon labels use `LLHUDText::renderNativeBeaconLabels`. | `newview/vsworldscene.cpp:1461`; `newview/vsvulkancontext.cpp:671`; `newview/vsbeaconpresentation.cpp:100` | — |
| Beacons / highlights | Implemented | `VSBeaconPresentation` draws object beacons, sun and moon beacons and region-edge beacons with labels. `appendAlphaHighlights` draws the alpha highlights. | `newview/vsbeaconpresentation.cpp:106-202`; `newview/vsworldscene.cpp:6338-6345`, `:6680` | `SURVEY.Physics-build-debug-service-overlays`: `unqualified` |
| Selection outlines / light radius | Partial | `VSSelectionLightPresentation` draws the light radius of the selection. Full selection silhouettes were not found in the Vulkan frame. | `newview/vsvulkancontext.cpp:596-599`; `newview/vsselectionlightpresentation.h` | `SURVEY.World-HUD-picks-and-selection`: `unqualified` |
| Parcel lines / ban lines / tracker / HUD icons | Implemented | `parcelPresentation`, `trackerPresentation` and `LLHUDIcon::appendNative` run each frame. | `newview/vsvulkancontext.cpp:594-601` | `unqualified` |
| Voice visualiser dots | Implemented | `VSWorldScene::appendVoice`. | `newview/vsworldscene.cpp:5833`; `vsvulkancontext.cpp:620` | — |
| Media on a prim (MOAP) | Partial | `media:<id>` images from `LLViewerMediaImpl`/`LLMediaCtrl` replace face textures. Vulkan pie click, hover and double-click media targets. | `newview/vsworldscene.cpp:1711`, `:4759`, `:6329`; `newview/llviewermedia.cpp:3099`; `newview/lltoolpie.cpp:2186-2550` | `SURVEY.Media-on-a-prim`: `unqualified` |
| Occlusion culling / G-buffer / deferred | Absent (by design so far) | Forward renderer with frustum/distance visibility and a spatial index; no occlusion queries. | `newview/vsworldscene.h`, `vsspatialindex.cpp`, `vsvolumevisibility.h` | `SURVEY.Visibility-G-buffer`: `partial`; `SURVEY.Asynchronous-occlusion-and-timers`: `unqualified` |

### Interaction and tools

| Feature | Status | How it is reached / why not | Evidence | Ledger |
|---|---|---|---|---|
| Camera (orbit, pan, zoom, roll, presets, first person/mouselook) | Implemented | `VSNativeSession::camera*` + `VSNativeCamera`; `gAgentCamera` Vulkan pose. Camera floaters and presets admitted; `View.Mouselook` admitted. | `newview/vsnativesession.cpp:1060-1160`; `vsstartupui.cpp:812-818` | `UI.*` `shared-source-present/native-effect-unqualified` |
| Avatar movement (walk/run/fly/jump/crouch/sit) | Implemented | Key actions → `movementAction` → `AgentUpdate` (rate limited). `canFly()` checks region and parcel. Sit via `AvatarSitResponse` and the admitted `Object.SitOrStand`/`Self.SitDown`. | `newview/vsnativesession.cpp:1078-1102`, `:2486-2500`, `:2616`; `llappviewer.cpp:1722-1726` | `COMMAND.fly`: admitted (`vsstartupui.cpp:612-620`); `COMMAND.move`: **not admitted** |
| Picking (ray against prims/mesh/avatars/particles) | Implemented | `VSWorldRenderer::intersect`, `VSWorldAvatar::collisionPick`, `VSParticleEffects::pick`. | `newview/vsworldrenderer.cpp:731`; `vsworldavatar.cpp:2127`; `vsparticleeffects.cpp:577` | `SURVEY.World-HUD-picks-and-selection`: `unqualified` |
| Left-click touch / click actions / grab | Partial | Vulkan click intent and pie priority in `LLToolPie`; `lltoolgrab.cpp` has 38 Vulkan guards. `Object.Touch` admitted. | `newview/lltoolpie.cpp:108-371`, `:704`; `newview/vspieclickpolicy.h` | — |
| Object selection / properties | Partial | `VSObjectSelection` (2,360 lines) handles `ObjectProperties*`/`ForceObjectSelect`. Inspect (`Object.Inspect`) and area search are admitted. | `newview/vsnativesession.cpp:1399-1410`; `newview/vsobjectselection.cpp` | `FLOATER.inspect_object`: **not admitted** |
| Pie menus | Partial | Pie slices are created. Only admitted callbacks are enabled; the rest are greyed out. 115 ledger pie entries; at least 30 admitted by literal callback name. Admitted: Touch, Sit/Stand, Inspect, Land.Buy/BuyPass, Detach, Take Off, hover height, pathfinding select, script actions. Disabled: Edit, Build, Take, Delete, Return, Buy object, Pay, Mute, Derender, Report, avatar Freeze/Eject/Add Friend/IM (avatar actions may be admitted elsewhere through helper gates). | `newview/pieslice.cpp:59-73`; `newview/vsstartupui.cpp:621-846`; menus `menu_pie_*.xml` | See Ledger coverage |
| Building: Build floater, create, edit, manipulators | Refused | The `build` floater, the `build`/`build_options` items and the `Object.Edit`/`Object.Build`/`Land.Build` callbacks are outside the admission sets, so building cannot be opened. `LLFloaterTools` has Vulkan selection hooks (`llfloatertools.cpp:111-143`, `:753`, `:1234`), and `VSEditObject` provides "genuine Vulkan object/property views for the existing Build controllers" (`newview/vseditselection.h:1`). `llmaniptranslate.cpp`, `llmaniprotate.cpp`, `llmanipscale.cpp`, `lltoolplacer.cpp`, `lltoolbrush.cpp` and `lltoolface.cpp` have 0 Vulkan guards. | `newview/vsstartupui.cpp:558-581` (no `"build"`); `grep -c VS_NATIVE_VULKAN` on the files listed | `FLOATER.build`, `FLOATER.build_options`, `COMMAND.build`: not admitted; `UI.BuildTool.*` 0/22 admitted |
| Texture / material / face editing | Partial (back end only) | `VSMaterialSelection`, `VSFaceSelection`, `vsfacedrop` (drag-drop texture onto face) and `ObjectImage`/`ObjectMaterial` senders exist. `material_editor` and `live_material_editor` are admitted. The Build floater texture tab is unreachable. | `newview/vsmaterialselection.cpp:267`; `newview/vseditselection.cpp:1013-1031`; `newview/vsfacedrop.cpp:151` | `FLOATER.gltf_asset_editor`: not admitted |
| Task inventory / contents / scripts | Partial | `VSTaskInventory` with `ReplyTaskInventory`. `openobject`, `buy_object_contents`, script queues and `preview_script` admitted. | `newview/vsnativesession.cpp:197`; `vstaskinventory.cpp`, `vstaskinventorycontroller.h` | `unqualified` |
| Pay / buy object, buy land | Partial | `PayPriceReply` handled; `pay_resident`, `buy_object`, `buy_land`, `buy_currency` admitted. Errors `NativePaymentBalanceUnavailable`/`NativePaymentInsufficientFunds`. | `newview/vsnativesession.cpp:1377-1378`; `notifications.xml:184-192` | `FLOATER.pay_object`: not admitted |
| Firestorm LSL bridge | Partial | `VSBridgeAdapter` sends bridge prim shape/update/image. `fslslbridge.cpp` has 25 Vulkan guards, and bridge functions are blocked until the Vulkan avatar is ready. | `newview/vsbridgeadapter.cpp:125-151`; `newview/fslslbridge.cpp:83-89` | `SURVEY.Firestorm-bridge`: `not-integrated` |
| Upload (image, sound, anim, model, script) | Partial | `upload_*` floaters and `File.Upload*` callbacks admitted. Upload cost gate `NativeUploadCostUnavailable`; `local_mesh_floater` and `bulk_upload` not admitted. | `newview/llviewermenufile.cpp:1546`; `newview/vsstartupui.cpp:600` | — |
| Poser / pose stand / animation explorer | Refused | `fs_poser`, `animation_explorer` and the `poser` command are not admitted; `fs_posestand` floater is admitted but the `pose_stand` command is not. | admission lists | `UI.Poser.*` 0/23 admitted |
| RLVa | Partial | `rlv_strings` admitted and the RLVa overlay effect is drawn. `rlv_behaviours`, `rlv_console` and `rlv_locks` are not admitted. | `newview/vsvulkancontext.cpp:631-636` | — |

### UI and services

| Feature | Status | How it is reached / why not | Evidence | Ledger |
|---|---|---|---|---|
| Vulkan 2D UI rendering (all LLUI widgets, fonts, images) | Implemented | `VSUIContext`, `VSUIRenderer`, `VSUIDrawBridge` and `VSUIFont*` render the shared LLUI tree through Diligent. | `newview/vsviewerwindow.cpp:160-175`; `vsuirenderer.cpp`, `vsuidrawbridge.h` | — |
| Floaters admitted | Partial | 202 of the 236 `FLOATER` ledger entries are in the admission set. The 34 left out, from the script below: `animation_explorer, auction, avatar_render_settings, avatar_textures, build, build_options, bulk_upload, export_collada, forget_username, fs_export, fs_import, fs_poser, gltf_asset_editor, im_container, impanel, inspect_object, local_mesh_floater, moveview, nearby_chat, particle_editor, pay_object, phototools, pref_joystick, prefs_graphics_advanced, rlv_behaviours, rlv_console, rlv_locks, telehubs, test_*(5), vram_usage`. | `newview/vsstartupui.cpp:558-581` | `FLOATER`: all 236 `registered/native-construction-unqualified` |
| Nearby chat | Implemented | `fs_nearby_chat`/`native_nearby_chat` admitted; `ChatFromSimulator` admitted; `sendChat` → `ChatFromViewer`; typing state. | `newview/vsnativesession.cpp:2454-2462`; `vsplainchat.cpp` | — |
| IM / group chat / conference | Implemented | `vs_native_im_receive` installed for `ImprovedInstantMessage`. `fs_im_container`, `fs_impanel` and IM wells are admitted; the LL `impanel`/`im_container` are not (the FS variants are used). | `newview/vsnativeim.cpp:123-132`; `newview/llimview.cpp` (16 guards) | — |
| Inventory | Partial | `inventory`, `secondary_inventory`, `fs_partial_inventory` and `item_properties` admitted. A limited callback set (`LLPanelMainInventory::isNativeCallback`). Wear and attach go through `VSNativeAppearance`. | `newview/llpanelmaininventory.cpp:3525-3540`; `newview/llinventorybridge.cpp:8087-8095`, `:8235-8245`, `:8648-8651` | `UI.Inventory.*` 3/36 literal; `SURVEY.Appearance-inventory-actions`: `unqualified` |
| Appearance / outfits / wearables editing | Partial | `appearance`, `editoutfit`, `outfits` and the panels `panel_outfit_edit`/`panel_edit_wearable` admitted. `VSNativeAppearance` keeps the COF and wearables. | `newview/vsstartupui.cpp:582-585`; `newview/vsnativeappearance.cpp` | `COMMAND.appearance`, `COMMAND.wearable_favorites`: not admitted (floater `fs_wearable_favorites` admitted) |
| People / friends / groups / profiles | Implemented | `people`, `profile`, `fs_group`, `group_picker`, `vs_group_search` and the group panels admitted. `LLPanelPeople::isNativeCallback`. | `newview/vsstartupui.cpp:558-605`, `:745-751`; `vsgroupsearch.cpp` | — |
| Radar | Implemented | `fs_radar`, `radar` command and the `Radar.*` callbacks admitted. `CoarseLocationUpdate` admitted. | `newview/vsstartupui.cpp:716-735`; `vsnativesession.cpp:1359` | — |
| Minimap | Implemented | `LLNetMap::drawNative()` with Vulkan object and parcel layers; 46 of the 49 `Minimap.*` callbacks admitted. | `newview/llnetmap.cpp:332-333`, `:2398-2880` | `UI.Minimap.*` 46/49 |
| World map | Partial | `world_map` floater admitted; `MapBlockReply`/`MapItemReply` admitted. `llworldmapview.cpp` has 5 Vulkan guards. `WMap.*` context callbacks are not admitted (0/12). | `newview/llworldmapview.cpp:379-382` | — |
| Preferences | Partial | `preferences` and most panels admitted. Graphics controls without a Vulkan consumer are disabled. `prefs_graphics_advanced` and `pref_joystick` not admitted. | `newview/llfloaterpreference.cpp:2229-2257`; `vsgraphicssettings.h` | 1014 `UI_SETTING`: `unqualified` |
| Notifications / toasts / script dialogs | Implemented | Toast, alert, script floater and notification well types admitted. | `newview/vsstartupui.cpp:535-549` | — |
| Land / region / estate tools | Partial | `about_land`, `region_info`, `top_objects`, `god_tools` and the `ParcelProperties`, `RegionInfo` and `EstateOwnerMessage` messages admitted. `telehubs` floater not admitted. | `newview/vsnativesession.cpp:1351-1357`, `:1374-1385` | — |
| Search / destinations / marketplace / web | Implemented | `search`, `legacy_search`, `destinations`, `marketplace*`, `web_content`, `help_browser` admitted. | `newview/vsstartupui.cpp:558-581`; `newview/llfloaterwebcontent.cpp:163-165` | — |
| Toolbars | Partial | 57 of 69 `COMMAND` entries admitted. Not admitted: `RFO, appearance, avatar, beacons, build, compass, move, move_lock, phototools, pose_stand, poser, wearable_favorites`. | `newview/vsstartupui.cpp:612-620` | `COMMAND`: `declared/native-action-unqualified` |

### Media, audio, voice, snapshots, settings

| Feature | Status | How it is reached / why not | Evidence | Ledger |
|---|---|---|---|---|
| Sound effects / attached sounds | Implemented | `init_audio()` at connect. The `SoundTrigger`, `PreloadSound` and `AttachedSound*` handlers are installed. `VSWorldSound` owns attached sources. `audio_update_*` runs each frame. | `newview/vsnativesession.cpp:2213`, `:428-431`; `newview/llappviewer.cpp:1771-1777`; `vsworldsound.cpp` | — |
| Parcel audio stream / parcel media | Partial | `ParcelMediaUpdate`/`ParcelMediaCommandMessage` admitted. `llviewerparcelmedia.cpp` has 14 Vulkan guards; `clearParcelPlayback` runs on region change. | `newview/vsnativesession.cpp:201-203`, `:1287` | — |
| Web/media plugins (CEF, VLC) | Partial | `LLViewerMedia` update and focus each frame; `vs_update_media_image` publishes frames to Vulkan images. | `newview/llappviewer.cpp:1779-1784`; `newview/llviewermedia.cpp:1810`, `:3099`; `newview/llmediactrl.cpp:866` | `SURVEY.Media-on-a-prim`: `unqualified` |
| Voice (WebRTC/Vivox) | Partial | `LLVoiceClient` initialised in Vulkan init. Voice floaters and the `speak`/`voice` commands admitted. `setRegionVoiceEnabled` exists. `llvoicewebrtc.cpp` has 0 Vulkan guards. | `newview/llappviewer.cpp:1667`; `newview/vsnativesession.h:102`; `newview/vsstartupui.cpp:578` | `SURVEY.Voice-and-spatial-services`: `partial` ("Vulkan provisioning/region/spatial lifecycle still requires integration") |
| Gestures | Implemented | `LLGestureMgr::update()` uses the Vulkan character adapter. `gestures` floater and command admitted. | `newview/llappviewer.cpp:1686-1698`, `:1770` | — |
| Snapshots (to disk, inventory, profile, Flickr, Primfeed, simple snapshot) | Partial | `LLViewerWindow::rawSnapshot` → `vs_native_snapshot`; `simpleSnapshot` → `vs_native_simple_snapshot`. Snapshot controllers update once per frame. Depth layer is read from world depth. No post-processing, so captures do not match the GL pipeline. | `newview/llviewerwindow.cpp:6554-6556`, `:6917`; `newview/vssnapshot.cpp:23`, `:140`; `newview/llappviewer.cpp:1786-1795`; `newview/vsvulkancontext.cpp:563-570`, `:623` | `SURVEY.Snapshots-previews-bakes-maps-impostors`: `unqualified` |
| 360 capture | Partial | `360capture` floater/command admitted; the snapshot camera path covers "all six panorama faces". | `newview/vsvulkancontext.cpp:565-567` | — |
| Model / avatar previews (upload model, appearance) | Partial | `VSModelPreview`, `VSScenePreview` and the avatar `configurePreview` UI owner. | `newview/vsmodelpreview.cpp`; `vsworldavatar.h:41-46` | — |
| Graphics settings with Vulkan consumers | Partial | Implemented: `RenderAvatarLODFactor, RenderAvatarPhysicsLODFactor, RenderAvatarMaxComplexity, RenderAutoMuteSurfaceAreaLimit, RenderFarClip, RenderTerrainLODFactor, RenderTerrainPBRDetail, RenderTerrainPBRPlanarSampleCount, RenderTreeLODFactor, RenderVolumeLODFactor, WLSkyDetail, Disregard128/96DefaultDrawDistance`, plus `RenderCompressTextures`, `RenderAnisotropic` and `RenderMaxTextureResolution` when the device allows. Pending (disabled): `RenderDepthOfField, RenderGlow*, RenderSharpness, RenderFSAAType/Samples, RenderTransparentWater, RenderGammaFull`. | `newview/vsgraphicssettings.h:16-42` | `SETTING`: 292 `native-controller-pending`, 49 `native-reference-present/effect-unqualified` |
| VSync / resolution / window | Partial | VSync is set at window creation. `toggleVSync` is skipped under Vulkan. | `newview/vsviewerwindow.cpp:141`; `newview/llviewerwindow.cpp:7374` | `SURVEY.Resolution-VSync-and-presentation`: `unqualified` |
| Performance floater / avatar render settings | Implemented | `performance` and `fs_avatar_render_settings` admitted with Vulkan callbacks; GPU timing ledger per avatar. | `newview/vsstartupui.cpp:624`; `newview/vsworldrenderer.cpp:875-886`; `vsgputimingledger.h` | — |

## Incompleteness markers

None of these commands modify files. The production versus diagnostics split
follows the file-name filter above.

| Marker (case-insensitive) | Hits in `newview/vs*` | Files | Notes |
|---|---:|---:|---|
| `TODO` | 2 | 2 | `vscameraview.cpp:64` and `vsregionlimits.cpp:386`. Both are comments copied from reference code. |
| `FIXME` | 0 | 0 | |
| `not yet` | 0 | 0 | |
| `stub` | 0 | 0 | |
| `placeholder` | 0 | 0 | |
| `HACK` | 18 | 10 | All inherited: `HACKED_GODLIKE_VIEWER` ifdefs and reference comments (`vsvegetation.cpp:383`, `vsterrainshader.h:337,345`, `vswindfield.h:27`). |
| `not implemented` / `unimplemented` | 3 | 1 | `vsregionlimits.cpp:334,346,396`, copied from the reference region-limits code. |
| `unsupported` / `not supported` | 21 | 10 | Format-check exceptions plus a per-region `unsupported` counter for object updates the scene cannot represent (`vsworldscene.cpp:1182,1533,1547,3344,3647,3726`, reported in evidence at `:5467`). |
| `refus*` | 83 | 23 | Almost all are budget refusals: mesh, image, name-tag and timing allocations. The old generation is kept on refusal (`vsworldrenderer.cpp:618-622`, `vsuirenderer.cpp:235-289`, `vsworldavatar.cpp:221-249`, `:745-866`). These do not refuse features. |
| `fallback` | 40 | 10 | Data fallbacks such as the default texture, terrain-height edges and the wearables-metadata fallback (`vsnativeappearance.cpp:648`). None is a renderer fallback. |
| `unavailable` / `not available` | 61 | 14 | Mostly asset or capability state (`vsworldassets.h:13`). The user-facing ones are listed below. |
| `throw std::runtime_error` | 97 | 37 | Invariant checks (`requireWorld`, `require`). In production files a failure ends the frame or session; see Risks. |
| `pending` | 445 | 46 | Resource-state vocabulary, not a marker. |
| Phase tags (`R2`…`R6` in production comments) | 14 | — | `grep -oE "\bR[1-9](/R[1-9])*\b"`: R2 ×10, R3 ×1, R4/R5 ×1, R5/R6 ×1. The two deferral comments are `vsworldrenderer.cpp:479` and `vsterrainshader.h:620`. |

The commands:

```sh
cd indra/newview
for k in TODO FIXME "not yet" stub placeholder HACK "not implemented\|unimplemented" \
         "unsupported\|not supported" refus "fallback\|fall back\|falls back" \
         "unavailable" "throw std::runtime_error" pending; do
  printf "%-40s %5s files=%s\n" "$k" "$(cat vs*.cpp vs*.h | grep -ci -- "$k")" \
         "$(grep -lic -- "$k" vs*.cpp vs*.h | wc -l)"; done
```

What these counts show: the Vulkan code is written without TODO or stub markers.
Incompleteness is expressed in other ways:

1. **Closed admission sets.** These are the main mechanism. Anything not named is
   refused: floaters, commands, menu callbacks, widget types and simulator
   messages (see "Three admission layers").
2. **Pending-pass lists.** `vs_graphics_pending_pass` (`vsgraphicssettings.h:36-42`)
   names the render passes with no Vulkan consumer, and the Preferences UI disables
   them.
3. **Early returns in shared code.** Shared code returns early under Vulkan at
   `isNativeVulkan()` (338 `isNativeVulkan()` call sites in shared files; 9 bare
   early-returns). Notable ones:
   - `llsettingsvo.cpp:680`: no sun/moon textures.
   - `llsettingsvo.cpp:1155`: no water pool update.
   - `llfloaterpreference.cpp:354`: no avatar-customise camera.
   - `fslslbridge.cpp:87`: bridge blocked without an avatar owner.
4. **User-facing strings.** Four strings and notifications are used:
   - `NativeVulkanUIUnavailable` (`strings.xml:8`) is the general refusal message.
   - `NativeSessionRegionUnavailable` (`strings.xml:3365`) is used only for
     cross-grid SLURLs (`llurldispatcher.cpp:251`).
   - `VulkanRendererUnavailable` and `MBVulkanRendererUnavailable` handle selection
     in builds without Vulkan support.
   - `NativeUploadCostUnavailable` and the `NativePayment*` notifications handle
     missing account data.
5. **Defined but unused.** `NativeWorldUnavailable` ("World features are not yet
   available with the Vulkan renderer", `notifications.xml:171-174`) and
   `NativeVulkanRegionUnavailable` (`strings.xml:7`) are not referenced by any
   `.cpp` (`grep -rn "NativeWorldUnavailable\|NativeVulkanRegionUnavailable"
   --include=*.cpp` → none). Inference: both are left over from an earlier,
   text-only stage.
6. **Deferral comments in the shader.** `vsworldrenderer.cpp:479-480` (ORM and
   specular deferred to R5/R6) and `vsterrainshader.h:620` (roughness and metallic
   deferred to R4/R5).

## Ledger coverage

The ledger (`schema: 1`, `scope: "Full parity acceptance inventory; discovery
only, no runtime acceptance."`) has no "Vulkan covered" boolean. Each entry has
an `implementation` disposition plus three qualification flags (`source_qualified`,
`software_runtime_qualified`, `authenticated_runtime_qualified`) and an
`evidence` list.

### Qualification flags

| Flag | True | False |
|---|---:|---:|
| `source_qualified` | 1 (`FEATURE.Startup`) | 3130 |
| `software_runtime_qualified` | 1 (`FEATURE.Startup`) | 3130 |
| `authenticated_runtime_qualified` | 0 | 3131 |
| non-empty `evidence` | 1 (`FEATURE.Startup`) | 3130 |

### Implementation disposition by category

| Category | Entries | Disposition(s) |
|---|---:|---|
| UI | 1291 | 1250 `shared-source-present/native-effect-unqualified`, 41 `source-registration-unresolved` |
| UI_SETTING | 1014 | 1014 `unqualified` |
| SETTING | 341 | 292 `native-controller-pending`, 49 `native-reference-present/effect-unqualified` |
| FLOATER | 236 | 236 `registered/native-construction-unqualified` |
| DRAW | 87 | 87 `native-reference-pass-integration-pending` (every `PASS_*`/`POOL_*`) |
| COMMAND | 69 | 69 `declared/native-action-unqualified` |
| SURVEY | 45 | 31 `unqualified`, 9 `partial`, 3 `not-integrated`, 2 `placeholder` |
| APP_COMMAND | 38 | 38 `unqualified` |
| FEATURE | 7 | 1 `implemented`, 2 `partial`, 2 `not-integrated`, 1 `native-avatar-integration-pending`, 1 `native-subscriber-integration-pending` |
| SETTING_FAMILY | 3 | 3 `unqualified` |

Phase allocation: R9 2775, R3 246, R4 88, R2 9, R7 4, R5 3, R8 3, R6 2, R1 1. The
phases are defined in `doc/vulkan/full-parity-roadmap.md:43-53`.

Reproduce with:

```sh
python -c "import json,collections as C;e=json.load(open('doc/vulkan/parity/acceptance-ledger.json'))['entries'];print(C.Counter((x['category'],x['implementation']) for x in e))"
```

### SURVEY and FEATURE rows (the ledger's own feature view)

| Row | Ledger disposition | Code reading (this chapter) |
|---|---|---|
| FEATURE.Startup | implemented (qualified) | Agrees. |
| FEATURE.LoginProgress / LogoutProgress | partial | Agrees. |
| FEATURE.PersistentTextureCache | subscriber pending | Agrees. |
| FEATURE.BuiltinAO | avatar integration pending | Agrees. The AO floater is admitted, but the self-avatar animation owner is not established. |
| FEATURE.Underwater | not-integrated | Agrees. |
| FEATURE.HUDProjection | not-integrated | **Looks stale.** An orthographic HUD projection exists (`vsworldrenderer.cpp:936-966`), added in `e0794c1e92`, 2026-10-14. |
| SURVEY.Analytic-atmosphere… | placeholder ("procedural gradient") | **Partly stale.** A ported haze/sunlight dome shader exists (`vsworldrenderer.cpp:220-312`, commit `74620e4483`, 2026-10-18). The gradient remains only for the case with no sky. Clouds, stars and discs are still missing. |
| SURVEY.Water… | placeholder | Agrees. |
| SURVEY.Sun-spot-shadows, Probes… | not-integrated | Agrees. |
| SURVEY.Firestorm-bridge | not-integrated | **Partly stale.** `vsbridgeadapter.cpp` and the guarded `fslslbridge.cpp` were added in `574c842e23` (2026-10-17). |
| SURVEY.Alpha-masks-PPLL, Visibility-G-buffer, Sun-moon-local-projector-lights, Terrain, Mesh, Avatars, Rigged/animesh, Standard prims, Voice | partial | Agrees. |
| SURVEY.Particles-effects, Flexible, Sculpt, Trees-grass, GLTF-*, Media-on-a-prim, Snapshots… | unqualified | Code is wired (see the matrix). "Unqualified" is the right word: nothing is tested against the reference. |

`source-dispositions.json` was last changed in `bf1da6e16c` (2026-10-13). The
regenerated ledger is dated `d60e949144` (2026-10-27), but the regeneration keeps
the hand-written dispositions (`git log -1 -- doc/vulkan/parity/source-dispositions.json`).
Inference: the free-text dispositions lag the code by about two weeks for the
three rows flagged above.

### Cross-check: ledger entries against the admission sets

These counts were computed by matching ledger titles to names extracted from
`newview/vsstartupui.cpp:558-846`. Literal matching ignores the 10
`isNativeCallback` helper gates, so callback figures are **lower bounds**.

| Ledger slice | Admitted | Total | Notes |
|---|---:|---:|---|
| FLOATER | 202 | 236 | The 34 left out are listed in the UI table. |
| COMMAND | 57 | 69 | The 12 left out are listed in the UI table. |
| UI entries from `menu_*`/`pie_*` XUI | ≥325 | 962 | Callback admission applies only to menus and pie slices (`llmenugl.cpp`, `pieslice.cpp`). |
| UI entries from pie menus | ≥30 | 115 | Build, Edit, Take, Delete and Return are all excluded. |
| `UI.Minimap.*` | 46 | 49 | |
| `UI.World.*` | 20 | 31 | |
| `UI.Edit.*` (text editing) | 19 | 21 | |
| `UI.Object.*` | 6 | 49 | Touch, Sit, Inspect only. |
| `UI.Tools.*` | 8 | 42 | |
| `UI.BuildTool.*`, `UI.Poser.*`, `UI.WMap.*`, `UI.Pref.*` | 0 | 22 / 23 / 12 / 63 | `Pref.*` are commit callbacks, which the gate does not filter; the 0 is not a refusal. |

## Biggest gaps

1. **Lighting and shading model.** The renderer is forward-only with sun and
   ambient Lambert lighting:
   - no local or projector lights, no specular, no PBR BRDF;
   - no shadows, no reflection probes or mirrors, no SSAO or SSR.
   These are roadmap phases R4–R6 (`full-parity-roadmap.md:171-215`). Every
   material-heavy scene will look flat compared with OpenGL.
2. **Post-processing chain.** There is no HDR target, exposure, tone mapping,
   gamma, anti-aliasing or depth of field. Glow is accumulated but never
   composited (`vsworldrenderer.cpp:893-901`, `:1214`). The target is LDR sRGB
   (`vsworldrenderer.cpp:193`).
3. **Water and sky.** Water is a fixed-colour translucent quad
   (`vsworldscene.cpp:1171-1178`). There is no underwater mode. Clouds, stars and
   sun/moon discs are missing.
4. **Building is unreachable.** The Build floater, manipulators, create and
   terraform tools, the GLTF asset editor and the particle editor are refused or
   have no Vulkan code, although `VSEditObject`/`VSMaterialSelection` back ends
   exist. This is the largest interaction gap for a Second Life viewer.
5. **Prim hover text is missing.** `llSetText` text is parsed and discarded
   (`vsworldscene.cpp:1461`). Users will notice this immediately in-world.
6. **No GPU occlusion culling or G-buffer.** Performance at scale is unknown.
   `full-parity-roadmap.md:24-26` records a user observation of about 14 FPS as
   "a starting measurement, not a target".
7. **Qualification is almost entirely missing.** 3130 of 3131 ledger entries have
   no source or runtime qualification and no evidence. Even the "Implemented"
   rows above are unverified against OpenGL output.
8. **Release availability.** Release and CI builds do not compile the Vulkan
   path, so a user picking "Vulkan (in development)" in a normal build gets a
   refusal dialog, not the renderer (`DiligentCore.cmake:4`,
   `llfloaterpreference.cpp:4143-4147`).

## Risks and open questions

- **No fallback on failure.** Selecting Vulkan in a Vulkan build commits the next
  launch to Vulkan. If the Vulkan window or context fails, `initNativeWindow`
  rethrows (`vsviewerwindow.cpp:177-189`). The Preferences selector does not probe
  for Vulkan before offering it (`VulkanNotAvailableTooltip` is unused).
  Open question: is there a top-level handler that turns this rethrow into a
  user message, or does the viewer terminate? This chapter did not trace
  `LLAppViewer::init`'s callers.
- **Exceptions in the frame path.** There are 97 `throw std::runtime_error` sites
  in 37 Vulkan files. Many are in the world renderer's per-frame checks
  (`requireWorld`, for example "World extent mismatch" at
  `vsworldrenderer.cpp:871`). Inference: a malformed state ends the frame or
  session rather than degrading. Chapter 05 should confirm how they are caught.
- **Silent message deferral.** Simulator messages outside the admitted sets are
  dropped, and only the first 8 are logged (`vsnativesession.cpp:1371`). A missing
  feature can therefore look like a server-side absence. Open question: which
  commonly used messages are dropped today? `ScriptDialog`, `LoadURL`,
  `ScriptTeleportRequest` and similar would need checking against the 116-name
  session set (`vsnativesession.cpp:199-305`).
- **Disabled is not hidden.** Refused menu and pie items stay visible but greyed
  out (`llmenugl.cpp:850`). Users may read this as a permissions problem, not
  missing Vulkan support.
- **Stale user-facing text.** `NativeSessionRegionUnavailable` says region changes
  are unavailable, but it is now shown only for cross-grid SLURLs. Two
  notifications/strings are dead (`NativeWorldUnavailable`,
  `NativeVulkanRegionUnavailable`).
- **Ledger drift.** The hand-written dispositions in `source-dispositions.json`
  understate HUD projection, sky and bridge work (see Ledger coverage). Because
  the ledger is generated "discovery only", it cannot show what is wired. Open
  question: should `implementation` be refreshed from code (for example with the
  admission-set cross-check above) so the owner can see reachable versus
  unreachable surface?
- **Glow cost without output.** With `RenderGlow` on (pending pass, so the
  Preferences control is disabled, but the saved value still applies at
  `vsvulkancontext.cpp:578`), emissive and alpha packets are drawn twice into an
  RGBA16F target that nothing reads. Inference: this is pure cost until a
  composite pass lands.
- **Partial items that need an owner decision:** impostor rendering (settings are
  read but nothing renders), built-in AO ownership, persistent texture-cache
  subscribers, voice spatial lifecycle (`llvoicewebrtc.cpp` has no Vulkan
  guards), and the LL-variant floaters (`nearby_chat`, `impanel`, `im_container`)
  that are excluded in favour of the FS variants.
- **macOS.** There is no Vulkan path. `DiligentCore.cmake:9-11` refuses non-Windows
  and non-Linux platforms.
