# 03 — Vulkan UI and input

Surveyed revision: `vkstorm-vulkan` at `51d1f729a6` (worktree branch
`codex/vulkan-survey`), compared with `origin/vkstorm-release`. Nothing was built
or run for this chapter. Every status comes from reading source, and every
count comes from a command given in this chapter. "Reachable" means a code path
from an admitted widget, menu item, tool or input event leads to the code. It
does not mean the path was exercised.

**Summary.** The Vulkan renderer does not replace the viewer's UI. It runs the
shared LLUI/XUI widget tree, skins, fonts, floaters and menus, and redirects
their 2D drawing into Vulkan packets. LLRender2D sends rectangles, triangles and
clips to `VSUIResources`, and those packets go to `VSUIRenderer`. The one
exception is construction: a closed admission policy in
`indra/newview/vsstartupui.cpp` decides which widget types, floaters, panel
factories, toolbar commands and menu callbacks may exist at all. At
`51d1f729a6` it admits 494 exact widget types plus 33 helper predicates, 206
floater names (34 of 236 registered names refused), 106 panel-factory names, 57
of 69 toolbar commands, and about 650 callback and parameter literals.
**Headline status: the UI substrate, login screen, window, mouse, keyboard and
widget tooltips, the chat/IM/contacts UI, minimap, world map and preferences are
Implemented or close to it, because they reuse the shared code.
In-world interaction is Partial.** Only five tools are registered under Vulkan.
The Build/Edit floater (`build`) is refused. Most object and avatar pie-menu
slices are disabled by the callback gate. Teleport-offer and object IM
dialogs are dropped. A Vulkan source file that the selection-light presentation
needs is not in CMake.

## Scope and size

| Measure | Value | Command (worktree root) |
|---|---|---|
| UI substrate (`vsuicontext/drawbridge/fontbridge/font/fontcache/imageprovider/resources/renderer`, `vsuiaudio.h`, `vstranslations.h`) | 3,130 lines | `wc -l indra/newview/vsui{context,drawbridge,fontbridge,font,fontcache,imageprovider,resources,renderer}.* indra/newview/vsuiaudio.h indra/newview/vstranslations.h` |
| Admission, window, chat, IM, group search (`vsstartupui.*`, `vsviewerwindow.cpp`, `vsplainchat.*`, `vsnativeim.*`, `vsgroupsearch.*`) | 2,311 lines | `wc -l` on those files |
| Pick, selection and edit (`vspickpolicy.h`, `vspieclickpolicy.h`, `vsselection*.h`, `vscontextselection.h`, `vsobjectselection.*`, `vseditselection.*`, `vsbuildselectionpolicy.h`, `vsmaterialselection.*`, `vsfaceselection.cpp`, `vsobjectcontext.*`, …) | 7,172 lines | `wc -l` on the list in [Picking and selection](#picking-and-selection) |
| Camera (`vsnativecamera.h`, `vscameraview.*`, `vscamerafocusobject.h`) | 285 lines | `wc -l indra/newview/vsnativecamera.h indra/newview/vscamera*` |
| Admission header shared with llui | 42 lines | `wc -l indra/llui/vsuiadmission.h` |
| Diagnostic-only UI replays and fixtures (`vsuifixture`, `vsuipixeloracle`, `vstooltipreplay`, `vshudiconreplay`, `vsnativereplay`, `vsnativeimreplay`) | 5,353 lines | `wc -l`; listed under `if(VS_VULKAN_DIAGNOSTICS)` at `indra/newview/CMakeLists.txt:2390-2406` |
| `VS_NATIVE_VULKAN` guard lines | 1,505 in 259 files; 1,299 in 233 non-`vs*` files | `grep -r VS_NATIVE_VULKAN indra --include=*.cpp --include=*.h \| wc -l` (add `\| grep -v "/vs[a-z0-9]*\.\(cpp\|h\):"` for shared files) |
| `isNativeUI()`/`vs_native_ui_active()` call sites (draw-path forks in shared code) | 151 lines; 32 in `llrender2dutils.cpp` | `grep -rn "isNativeUI\|vs_native_ui_active" indra --include=*.cpp --include=*.h \| wc -l` |
| Delta in shared llui since merge-base `c0ba3ac56d` | 55 files, +573 / −254 | `git diff --stat origin/vkstorm-release...HEAD -- indra/llui` |
| Delta in the main shared UI/input files | `llagentcamera.cpp` +1,285 net-changed lines, `llviewerwindow.cpp` 1,059, `lltoolpie.cpp` 1,105, `llselectmgr.cpp` 1,165, `llviewermenu.cpp` 828, `llnetmap.cpp` 658, `llworldmapview.cpp` 135, `llmenugl.cpp` 86, `lltoolmgr.cpp` 51, `llfloaterreg.cpp` 8 | `git diff --stat origin/vkstorm-release...HEAD -- <file>` |
| UI- and input-related source tests in `scripts/tests` | 140 of 506 files match by name | `ls scripts/tests \| grep -ciE "ui\|menu\|tool\|pick\|select\|camera\|chat\|tooltip\|map\|media\|input\|window\|startup\|admission\|pie"` |

Several names in the brief have **no files**. There are no `vstool*`, no
`vsconnected*` and no `vspick*.cpp` files. The only `vspick*` file is
`vspickpolicy.h`. The only `vscamera*` files are `vscameraview.*` (draw
distance) and `vscamerafocusobject.h`. The Vulkan tool, connected-UI and camera
logic lives inside shared files (`lltoolmgr.cpp`, `lltoolpie.cpp`,
`llviewerwindow.cpp`, `llagentcamera.cpp`, `llviewermenu.cpp`) and in
`vsnativesession.cpp`.

**Change history.** `116a410fc8` ("Implement connected UI and R3 service
checkpoint [skip ci]", 2026-10-18, 828 files) reshaped most of this chapter's
area:

- `vsstartupui.cpp` +293
- `vsviewerwindow.cpp` 213 changed lines
- `llagentcamera.cpp` 1,037
- `llviewermenu.cpp` 486
- `lltoolpie.cpp` 420
- `llviewerwindow.cpp` 375

The command is `git show --stat=200 116a410fc8`. Fifty-nine commits follow it
up to HEAD. Because that commit skipped CI, no CI graphics job has compiled this
UI state together with the Diligent test.

**The older audit is out of date.** `doc/vulkan/connected-ui-completeness-audit.md`
(baseline `c06ed7f2df`, 2026-10-17) reports 77 of 229 floater names admitted.
The figures in this chapter supersede it.

## How the shared UI runs under Vulkan

**Status: Implemented. It reuses the shared UI and replaces only the drawing
sink.**

### Ownership chain

1. **Window and admission.** `LLViewerWindow::initNativeWindow`
   (`indra/newview/vsviewerwindow.cpp:137-190`) does three things in order:
   - creates the OS window with `GraphicsAPI::Vulkan` (`:141-144`);
   - creates `VSVulkanContext` (`:157`);
   - installs the admission policy (`mNativeAdmission = vs_startup_ui_admission()`
     at `:159`).

   It then builds a `VSUIContext` whose root view is a real `LLRootView`
   (`:164-175`).
2. **UI lifecycle.** `VSUIContext::Impl` (`indra/newview/vsuicontext.cpp:37-92`)
   loads the selected skin and theme through `gDirUtilp->setSkinFolder` and
   loads `LLUIColorTable`. It creates the `LLUI` singleton with
   `VSUIImageProvider` as the image provider, loads `strings.xml`, runs
   `LLFontGL::initClass` and sets the root view. This is the same XUI and skin
   system the OpenGL viewer uses.
3. **Draw redirection.** `VSUIDrawBridge` (`indra/newview/vsuidrawbridge.cpp:9-21`)
   installs callbacks with `LLRender2D::setNativeUI` (rectangle and clip) and
   `LLRender2D::setNativeTriangles`. While those callbacks are set,
   `LLRender2D::isNativeUI()` is true (`indra/llrender/llrender2dutils.cpp:57-99`).
   Shared drawing helpers then publish logical rectangles, triangles and clips
   instead of issuing GL calls (`llrender2dutils.cpp:100-142, 181, 226-228`).
   Fonts go through `VSUIFontBridge` into `VSUIResources::fontBatch`
   (`indra/newview/vsuiresources.h:53`).
4. **Frame.** `LLViewerWindow::drawNativeUI` (`vsviewerwindow.cpp:351-418`) runs
   `updateUI()` and layout upkeep. Inside `mVulkanContext->present(...)` it then:
   - draws the Vulkan current tool's overlay (`:407-411`, mirroring `draw2D`);
   - draws `mRootView->draw()` and `drawDebugText()` (`:413`).

   `VSVulkanContext::present` collects the packets with `resources->begin` /
   `finish()` and submits them through `VSUIRenderer::draw`
   (`indra/newview/vsvulkancontext.cpp:667-680`). That happens after the world
   pass and after the name-tag and RLV-overlay sub-passes (`:615-635`).

`drawNativeUI` has two production callers: `indra/newview/llappviewer.cpp:1807`
(the connected frame loop) and `indra/newview/llstartup.cpp:376` (startup
frames). Every other caller is in diagnostic replay sources.

### Where the shared code forks

The fork happens at the primitive layer. 151 call sites test `isNativeUI()`.
They are concentrated in:

- `llrender2dutils.cpp` (32)
- `llsnapshotlivepreview.cpp` (16)
- `llworldmapview.cpp` (11)
- `llpreviewtexture.cpp` (8)
- `llrender.cpp` (8)
- `llhudnametag.cpp` (5)
- `llstatbar.cpp` (5)

Most widgets therefore draw unchanged. Widgets that issue raw GL draw calls
needed explicit Vulkan branches.

Inference: any shared widget that still calls GL directly, without an
`isNativeUI()` branch, has no Vulkan drawing. The `llwindow` code throws
`std::logic_error` on GL context use in a Vulkan window
(`indra/llwindow/llwindowwin32.cpp:1242,1958,2015,2022,2028,3971`;
`llwindowsdl2.cpp:987,1240,2805,2835,2842,2852`). That guards context use, not
`gGL` immediate-mode calls. The completeness audit records one such crash that
has since been fixed (`LLXYVector::draw`).

## Admission gates

**Status: Implemented, wired in, and the dominant limit on Vulkan UI coverage.**

### Mechanism

`VSUIAdmission` (`indra/llui/vsuiadmission.h:10-42`) holds seven process-wide
predicates. A second policy cannot be installed while one is active (`:19-20`).
When no policy is installed, every gate returns true, so OpenGL is unaffected
(`:26-33`).

| Gate | Consulted at | Effect of refusal |
|---|---|---|
| `widget(type_info)` | `indra/llui/lluictrlfactory.h:138,156,191,217,242`; `indra/llui/llfloaterreg.h:102`; `indra/llui/llpanel.cpp:369`; `llpanel.h:292` | Factory returns `nullptr`; the type is logged as "Rejected Vulkan widget type" (`vsstartupui.cpp:555`) |
| `floater(name)` | `indra/llui/llfloaterreg.cpp:187,308,316,378,545,621`; `indra/newview/llfloaterwebcontent.cpp:163` | `getInstance` → `nullptr`, `canShowInstance` → false; `showInstance`/toggle raise the `GenericAlert` "[CONTROL] is not available in this Vulkan development stage." (`vsstartupui.cpp:606-611`; string at `skins/default/xui/en/strings.xml:8`) |
| `panelFactory(name)` | `indra/llui/llpanel.cpp:849`; `llpanel.h:283` | A named factory panel or a class injector returns `nullptr` |
| `command(name)` | `indra/llui/lltoolbar.cpp:433,996,1109,1155,1422`; `indra/newview/llfloatertoybox.cpp:113` | The toolbar button is not created and the Toybox hides the command |
| `callback(name, param)` | `indra/llui/llmenugl.cpp:621,820-862,936`; `indra/newview/pieslice.cpp:59-73` | The callback is not connected and the item is **permanently disabled** (`setEnabled(false)`, `onCommit` returns early) |
| `child(file, name)` | `indra/llui/lluictrlfactory.cpp:137` | The XUI child is skipped |
| `unsupported(name)` | the floater gate | Shows the alert above |

**Exact-type rule.** The widget gate compares `typeid` for equality.
Admitting a base class never admits a derived class (`vsuiadmission.h:31`;
`vsstartupui.cpp:400-556`). The 33 helper predicates fill this gap. Shared
classes define them, for example `LLToolBar::isNativeInternalType`,
`LLFloaterCamera::isNativeWidgetType` and `LLUICtrlFactory::isNativeLayoutType`.
Count them with
`grep -rnE "bool [A-Za-z_:]*::is(Native[A-Za-z]*Type|InspectToastType|PrivacyPanelType|DropTargetType|PermissionsFloaterType|FloaterType)\(" indra | wc -l`,
which returns 33.

**Skin-child policy.** The child gate always returns true
(`vsstartupui.cpp:847-852`), with the comment "no skin children are omitted".
The older audit listed People Nearby and Vintage voice-monitor removals. Those
removals no longer exist.

**Direct construction bypasses the gates.** A class built with `new` is not
checked: `FSPanelLogin` (`indra/newview/fspanellogin.cpp:531`), `LLStatusBar`
and `LLHUDView` (`vsviewerwindow.cpp:788, 802`), and `LLProgressView`
(`:317-325`). Only the children they create through the factory are gated.

### Counts

Each count comes from parsing the gate's line range in `vsstartupui.cpp` with an
ad-hoc Python script. The script is not committed; this paragraph is its full
method. It applies `re.findall(r'typeid\(([A-Za-z_:0-9]+)\)')` to lines 400-557
and `re.findall(r'name == "([^"]+)"')` to each name gate's line range. The
registry counts use `re.findall(r'LLFloaterReg::add\(\s*"([^"]+)"')` over
`indra/**/*.cpp` and `r'LLPanelInjector<[^>]+>\s+\w+\(\s*"([^"]+)"'`.

| Gate (line range) | Admitted | Universe | Refused |
|---|---:|---:|---:|
| Widget `typeid` equalities (`:405-554`) | 494 distinct types (496 refs) + 33 helper predicates | n/a | All other types |
| Floater names (`:558-581`) | 206 distinct names (202 match a registration) | 236 distinct registered names | **34** |
| Panel-factory names (`:582-605`) | 106 distinct (81 match an `LLPanelInjector`) | 97 injector names | 16 injectors |
| Toolbar commands (`:612-620`) | 57 | 69 in `app_settings/commands.xml` | **12** |
| Callback/parameter literals (`:621-846`) | ≈651 distinct string literals, plus 10 delegated `isNativeCallback` predicates (`LLSidepanelAppearance`, `FSFloaterPerformance`, `FSFloaterAvatarRenderSettings`, `LLFloaterGesture`, `FSNameListAvatarMenu`, `FSAreaSearch`, `LLPanelMainInventory`, `LLPanelPeople`, `FSChatHistory`, `FSRadarMenu`) | n/a | All others |

**The 34 refused floater names:**

- Build and editing: `build`, `build_options`, `particle_editor`,
  `gltf_asset_editor`, `local_mesh_floater`, `export_collada`, `fs_export`,
  `fs_import`, `bulk_upload`, `telehubs`, `auction`
- Avatar and animation: `fs_poser`, `animation_explorer`, `avatar_textures`,
  `avatar_render_settings`
- Movement, joystick and graphics: `moveview`, `pref_joystick`,
  `prefs_graphics_advanced`, `phototools`, `vram_usage`
- RLVa: `rlv_behaviours`, `rlv_console`, `rlv_locks`
- Linden-variant chat and IM (Firestorm uses the `fs_*` versions):
  `im_container`, `impanel`, `nearby_chat`
- Objects and login: `inspect_object`, `pay_object`, `forget_username`
- Test floaters: `test_inspectors`, `test_list_view`, `test_text_editor`,
  `test_textbox`, `test_widgets`

**Four admitted names have no registration:** `editoutfit`, `nowwearing`,
`outfits`, `wearable_favorites`. These are dead entries.

**The 12 refused toolbar commands:** `RFO`, `appearance`, `avatar`, `beacons`,
`build`, `compass`, `move`, `move_lock`, `phototools`, `pose_stand`, `poser`,
`wearable_favorites`.

This creates inconsistencies. The `appearance`, `beacons` and `fs_posestand`
floaters are admitted (`:577`, `:568`), but their toolbar commands are refused.

**The 16 refused panel injectors:**

- `fsprimfeedaccountpanel`, `fsprimfeedphotopanel`
- `llflickraccountpanel`, `llflickrphotopanel`
- `panel_block_list_sidetray`
- `panel_landmarks`, `panel_teleport_history`
- `panel_ls_classifieds`, `panel_ls_events`, `panel_ls_groups`, `panel_ls_land`,
  `panel_ls_people`, `panel_ls_places`, `panel_ls_web`
- `panel_sidetray_home`
- `panel_voice_effect`

Inference: some of these refusals conflict with admitted types or floaters. For
example, `LLFlickrPhotoPanel` and `LLLandmarksPanel` are admitted by `typeid`
(`:446`, `:444`), and the `flickr` and `places` floaters are admitted. Whether
those floaters still construct their tab panels depends on the XUI using the
injector name as `class=`. It was not checked at run time.

### Menu impact (static estimate)

Method: parse each skin's `on_click` function and parameter, then check whether
the function name appears literally in the callback gate. For `Floater.*`
entries, check whether the floater parameter is admitted. This used an ad-hoc,
uncommitted ElementTree script over `*.on_click` child elements of
`menu_item_call`, `menu_item_check` and `pie_slice`, matched against the string
literals in lines 621-846. The estimate ignores the 10 delegated
predicates and the parameter-specific rules, so it is an approximation. Use the
audit's compiled evaluator approach for exact figures.

| Menu file (`skins/default/xui/en`) | `on_click` items | Function absent from gate | `Floater.*` items naming a refused floater |
|---|---:|---:|---:|
| `menu_viewer.xml` | 591 | 289 | 10 of 77 |
| `menu_login.xml` | 30 | 9 (debug and logging only) | 4 (the `test_*` floaters) |
| `menu_pie_object.xml` | 54 | 42 | 0 |
| `menu_pie_avatar_other.xml` | 28 | 27 | 0 |
| `menu_pie_avatar_self.xml` | 44 | 25 | 0 |
| `menu_pie_land.xml` | 8 | 5 | 0 |
| `menu_object.xml` / `menu_avatar_other.xml` / `menu_attachment_self.xml` | 45 / 28 / 47 | 32 / 27 / 21 | 1 / 0 / 1 |

Examples of `menu_viewer.xml` functions that are not admitted:

- Building: `Build.Toggle`, `Tools.Link`, `Tools.Unlink`, `Tools.SelectTool`,
  `Object.Duplicate`, `Object.Return`, `Tools.TakeCopy`
- Camera and view: `View.ZoomIn`, `View.ZoomOut`, `View.ZoomDefault`,
  `View.ShowHUDAttachments`, `View.ToggleUI`, `View.LookAtLastChatter`,
  `View.JoystickFlycam`
- World and avatar: `World.AlwaysRun`, `World.SetHomeLocation`,
  `CustomizeAvatar`, `Avatar.ResetSkeleton`
- Almost every `Advanced.*` and `Admin.*` item

The script found 192 distinct unadmitted function or floater names in
`menu_viewer.xml`.

## Startup and login screens

**Status: Implemented. It reuses the shared panels.**

- **Base views.** `initNativeBase` (`vsviewerwindow.cpp:191-350`) builds the
  login holder, hint holder, chat console, `VSPlainChat` owner, menu holder,
  menu-bar holder and status container. The status container's height is read
  from the skin's `main_view.xml` (`:272-285`). It then builds `gFloaterView`,
  the snapshot floater view, the notification channels, the TOS and critical
  message floaters (`:315-316`), `LLProgressView`/`LLProgressViewMini` from
  `panel_progress*.xml` (`:317-325`), the popup view, the tooltip view and the
  debug view.
- **Login panel.** `FSPanelLogin` is the unmodified shared panel. It has no
  Vulkan guards, and `git diff origin/vkstorm-release...HEAD --
  indra/newview/fspanellogin.cpp` is empty. Its web page uses `LLMediaCtrl`
  ("login_html", `fspanellogin.cpp:514`), and that type is admitted.
- **Login menu.** In Vulkan mode, `init_menus` builds only `menu_edit.xml` and
  `menu_login.xml`, then returns before any world context menus
  (`indra/newview/llviewermenu.cpp:730-743`).
- **Startup state machine.** `llstartup.cpp` has 7 guards plus several
  `isNativeVulkan()` branches. They skip the feature-manager GPU checks, Growl,
  and texture updates (`:769-852`). At `STATE_WORLD_INIT` they hand control to
  `VSNativeSession::begin()` (`:2071-2077`). The Vulkan session handles the
  login response (`:4807-4821`).
- **Connected transition.** `VSNativeSession` then calls
  `FSPanelLogin::closePanel()`, `LLToolMgr::initNativeTools()` and
  `gViewerWindow->setNativeConnected(true)`
  (`indra/newview/vsnativesession.cpp:2067-2069`).
- **Progress view.** `llprogressview.cpp` has 8 guards; the progress view is
  kept frontmost (`vsviewerwindow.cpp:400-401`).

**Gaps.**

- Inference: login-screen items that need refused floaters or callbacks are
  disabled. Only debug items are affected: `Advanced.ReportBug`,
  `Develop.SetLoggingLevel` and the four `test_*` floaters.
- No Vulkan startup splash or GPU-tier recommendation runs. The
  `LLFeatureManager` tier logic is skipped (`llstartup.cpp:821-850`).

## Window and input

**Status: Partial overall. Windowed mode, mouse, keyboard and IME are
Implemented. Fullscreen, Wayland and the Vulkan camera-drag path are
Absent or dead.**

| Concern | Evidence | Status |
|---|---|---|
| Window creation | `indra/llwindow/llwindow.cpp:445-463` uses `LLWindowWin32` on Windows and `LLWindowSDL` (SDL2) on Linux | Implemented |
| Fullscreen | `llwindow.cpp:447-451`: "Vulkan fullscreen admission is not implemented", returns `nullptr` | **Absent** |
| Linux display server | `indra/llwindow/llwindowsdl2.cpp:497-503`: "Vulkan viewer currently requires SDL2/X11"; Wayland windows are destroyed | X11 only |
| Resize and DPI | `reshapeNative` (`vsviewerwindow.cpp:430-451`) recomputes the scale (`UIScaleFactor` × system scale, clamped to 0.75–4), reinitializes fonts and reshapes the root. `drawNativeUI` re-checks the scale every frame (`:354-355`). Entry points: `llviewerwindow.cpp:2154, 3062` | Implemented |
| VSync | `RenderVSyncEnable` is passed at creation (`vsviewerwindow.cpp:142`) | Implemented at creation |
| Mouse buttons | `llviewerwindow.cpp:1261` → `nativeMouse` (`vsviewerwindow.cpp:596-621`). This runs global mouse bindings, flushes queued picks before an up event, then calls the **shared** `dispatchAnyMouseClick` with a lifetime/account guard | Implemented, reuses shared code |
| Mouse move and hover | `llviewerwindow.cpp:1706` → `nativeHover` (`:623-648`) stores the point. `llviewerwindow.cpp:4438` → `nativeHoverUpdate` (`:649-702`) handles captor, top control, root and tool in the reference order, then calls the shared `updateToolTip` | Implemented |
| Scroll wheel | `llviewerwindow.cpp:2075,2081` → `nativeScroll` (`:703-723`): captor or root, then media tool, then `cameraZoom` | Implemented |
| Vulkan camera drag | `nativeHover` orbits or pans when `mNativeCameraDrag` is set (`:638-645`). The flag is only ever set to `false` (`llviewerwindow.cpp:1752,1863`) | **Dead code** |
| Keyboard | Shared `handleKey`/`handleKeyUp`. The Vulkan session gets movement keys (`llviewerwindow.cpp:3514-3516`) and the agent-ready check (`:3589-3594`). `LLViewerInput` tracks held global key and mouse bindings for the Vulkan path (`indra/newview/llviewerinput.cpp:1255-1315`) | Implemented, reuses shared code |
| IME (Linux) | SDL composition reaches `LLPreeditor` through `update_native_preedit` (`llwindowsdl2.cpp:462, 1935, 1941`), controlled by `SDL2IMEEnabled` (`:485`) | Implemented |
| IME (Windows) | No Vulkan-specific changes to IMM handling in `llwindowwin32.cpp`. Inference: the existing HWND IMM path applies unchanged | Inherited; not checked |
| Focus | Shared `gFocusMgr`. The chat input takes keyboard focus on connect (`vsnativesession.cpp:2092`) | Implemented |
| Cursor | `nativeHover` resets the cursor to the arrow and un-hides it on every move (`vsviewerwindow.cpp:630-631`), and the hovered widget sets its own | Implemented |
| Widget tooltips | `LLToolTipView` is created at `vsviewerwindow.cpp:335-342` and kept on top (`:397`). `updateToolTip` is called at `:701`. Fixtures exist: `scripts/tests/native_tooltip_dispatch_fixture.cpp`, `native_tooltip_lifecycle_fixture.cpp` | Implemented |
| World hover tooltips (object and land) | `LLToolPie::handleToolTip` (`indra/newview/lltoolpie.cpp:1844-1880`) uses `mHoverPick.getObject()`, which is `gObjectList.findObject` (`llviewerwindow.cpp:8050-8053`). Vulkan mode has no `LLViewerRegion` (comment at `vsviewerwindow.cpp:362`), and only the diagnostic `vsparityscene.cpp:56` feeds `gObjectList` | Inference: object hover tips **Absent**; land tips unverified |
| Debug text and render info | `nativeRenderInfo` (`llviewerwindow.cpp:494-..`, used at `:722-723`); pixel inspection (`:976-980`) | Implemented |

## Tools

**Status: Partial. The shared tool classes are reused, but only a subset is
registered.**

`LLToolMgr::initNativeTools` (`indra/newview/lltoolmgr.cpp:587-609`) registers
five singletons:

- `LLToolPie` and `LLToolCamera` in the basic toolset
- `LLToolGrab` and `LLToolCompInspect` in the basic toolset
- `LLToolCompGun` in the mouselook toolset
- `LLToolCamera` also in the camera and face-edit toolsets

It then selects Pie. The comment explains why the other tools are missing: "The
complete legacy initTools also constructs GPU manipulators (notably
Translate::restoreGL); those registrations join here only with their Vulkan
resource consumers." `nativeCurrentTool()` (`:626-631`) is the only tool lookup
used by Vulkan dispatch (`vsviewerwindow.cpp:410, 687`;
`llviewerwindow.cpp:1272-1276, 4406-4410`). `retireNativeTools` (`:610-624`)
runs on logout and on app shutdown (`llappviewer.cpp:1842`;
`vsnativesession.cpp:2734`).

| Tool | Vulkan code | Reachable? | Status |
|---|---|---|---|
| Pie (click, touch, sit, media, context menu) | `lltoolpie.cpp`: 24 guards, `NativeClickIntent` (`:108`), `NativePiePriority` (`:174`), `NativePieActions` (`:371`), Vulkan media target (`:2186-2550`), Vulkan context selection peer (`:2675`, `:2767`) | Yes, the default tool | Implemented |
| Camera (alt-focus, orbit) | `lltoolfocus.cpp`: 5 guards, `NativeCameraPickPeer` (`:182`) | Yes | Implemented |
| Grab | `lltoolgrab.cpp`: 38 guards | Yes (basic toolset) | Implemented |
| Inspect (composite and rectangle select) | `lltoolcomp.cpp`: 4 guards; `lltoolselectrect.cpp`: 8 guards, `handleNativeRectangleSelection` (`:257`) | Yes | Implemented |
| Gun (mouselook) | `LLToolCompGun` registered | Yes | Implemented (shared code) |
| Translate, rotate, scale, create (placer), face edit, land brush | Not registered. No `LLManip*` Vulkan producer exists in `vs*` beyond `LLManip::rebuild` (`vsbuildgeometry.h:27`) | No | **Absent** |
| Pipette | `lltoolpipette.cpp:68-115`: Vulkan active gate, consumer handle and selection; deactivated on disconnect (`vsviewerwindow.cpp:730`) | Through texture pickers in admitted floaters | Implemented |
| Object picker | `lltoolobjpicker.cpp`: 10 guards, asynchronous Vulkan pick with operation guard (`:63-80`) | From admitted floaters (region and pathfinding tools) | Implemented |
| Morph (appearance editing) | `lltoolmorph.cpp:109-394` previews through `VSNativeAppearance` | Through the admitted `appearance` floater. The toolbar command is refused | Partial |
| Drag and drop | `lltooldraganddrop.cpp`: 9 guards; `vsfacedrop.cpp` drops textures and materials onto faces but requires `gFloaterTools` (`vsfacedrop.cpp:66,146`) | Inventory-to-world drop: Inference partial | Partial |
| Tool overlay drawing | `drawNativeUI` calls `nativeCurrentTool()->draw()` before the root view (`vsviewerwindow.cpp:407-411`) | Yes | Implemented |

**Pie menus.** `init_native_connected_menus` (`llviewermenu.cpp:667-711`) calls
`init_world_context_menus()` (`:600-664`). That function builds every context
menu and every `PieMenu` from the shared XUI. Right-click therefore reaches the
real `LLToolPie` context path. The callback gate leaves few slices active,
however:

- **Object pie** (`menu_pie_object.xml`). Active slices: Touch, Sit/Stand,
  Inspect, the two pathfinding selects and six `Tools.SelectedScriptAction`
  parameters. Disabled slices include Edit, Build, Take, Take Copy, Buy, Pay,
  Open, Delete, Return, Mute, Derender, Attach To and Report.
- **Avatar pie** (`menu_pie_avatar_other.xml`). The only slice whose function
  appears in the gate is `ShowAgentProfile`. That gate admits only the
  parameter `"agent"` (`vsstartupui.cpp:632`), while the slice uses `"hit
  object"`. All 28 slices are therefore expected to be disabled (Inference).
- **Land pie.** Only About Land, Buy Land and Buy Pass are active.

## Picking and selection

**Status: Picking is Implemented. Object selection is Implemented in code.
Editing the selection is mostly unreachable from admitted UI.**

**Picking** (`LLViewerWindow::nativePick`, `vsviewerwindow.cpp:452-595`). It
builds the cursor ray from the Vulkan camera. It tries HUD attachments first
(`pickHUD`), then the world (`VSWorldScene::pick`), with transparent, rigged,
unselectable and reflection-probe options. It applies the RLVa interact and
overlay restrictions (`:506-528`) and picks particles (`:531-538`). HUD icons
compete by camera distance (`:541-554`). Flora is classified (`:563-565`). It
fills surface UV, ST, normal and tangent data (`:583-593`).

Asynchronous picks are queued and resolved with lifetime guards
(`llviewerwindow.cpp:5767-5860`). `LLPickInfo` routes to `nativePick`
(`:7899-7903`, `:8081-8085`). The policy headers carry the original
LLToolPie/LLToolSelect/LLSelectMgr decision logic with renderer-neutral peers:

| File | Lines |
|---|---:|
| `vspickpolicy.h` | 72 |
| `vspieclickpolicy.h` | 30 |
| `vsselectionpolicy.h` | 55 |
| `vsselectionflow.h` | 91 |
| `vscontextselection.h` | 249 |
| `vsobjectinteraction.h` | 28 |
| `vsparticlepick.h` | 48 |

**Selection.** `VSObjectSelection` (`vsobjectselection.cpp`, 2,360 lines;
header `vsobjectselection.h:15-184`) holds real simulator handles. It supports:

- select and deselect: only, family, all
- faces, hover object, transient selection
- link and unlink
- saved transforms, materials and colours
- the grid
- permissions and send-select

`LLSelectMgr` delegates to it when a Vulkan session is active, for example
`linkObjects` / `unlinkObjects` / `enableUnlinkObjects` at
`indra/newview/llselectmgr.cpp:772-860` (27 guards in total).

`VSEditSelection` (1,686 lines) and `vsbuildselectionpolicy.h` (457 lines)
provide "genuine Vulkan object/property views for the existing Build
controllers" (`vseditselection.h:1`). `vsmaterialselection.cpp` (593),
`vsfaceselection.cpp` (109) and `vstexturescaleselection.cpp` (232) back face
and material editing. Their consumers include `llpanelpermissions.cpp`,
`llsidepaneltaskinfo.cpp`, `llfloaterobjectweights.cpp`, `fsareasearch.cpp`,
`llfloatergodtools.cpp` and the media-settings panels. Find them with
`grep -rln vseditselection.h indra/newview`.

**Reachability of editing.**

- `LLFloaterTools` (`build`) is refused, never admitted in this file's history
  (`git log -S'LLFloaterTools' -- indra/newview/vsstartupui.cpp` is empty).
  `LLPanelObject`, `LLPanelVolume` and `LLPanelFace` are not admitted types.
  `gFloaterTools` is set only in `LLFloaterTools`'s constructor
  (`llfloatertools.cpp:547`). Inference: it stays null in Vulkan sessions, so
  the Vulkan code that tests it is inert: `vsfacedrop.cpp:66,146`,
  `vstexturescaleselection.cpp:34`, `vsregionlimits.cpp:336` and
  `vsobjectselection.cpp:1504`.
- The menu entries `Build.Toggle`, `Tools.Link`, `Tools.Unlink`,
  `Object.Duplicate` and `Tools.SelectTool` are not admitted, and neither is
  the `Object.Edit` pie slice.
- What remains reachable for editing:
  - the admitted `task_properties` floater (type `LLSidepanelTaskInfo`, panel
    name `sidepanel_task_info`, `vsstartupui.cpp:591`);
  - `inspect` (`LLFloaterInspect`);
  - object contents and inventory (`LLPanelContents`, `LLPanelObjectInventory`);
  - script queue and bulk-permission floaters;
  - grab-dragging with `LLToolGrab`.

**Selection highlight.** Light-radius presentation exists
(`VSSelectionLightPresentation`) and is appended in `vsvulkancontext.cpp:599`,
but see the build risk below. No Vulkan producer for selection silhouettes was
found: `grep -rli silhouette indra/newview/vs*` matches only an avatar flag in
`vsworldavatar.cpp`. Inference: the yellow and blue outline of selected objects
is **Absent**.

## Camera

**Status: Partial. A parallel CPU camera model replaces `LLAgentCamera`'s
update, and shared camera UI routes into it.**

**State and transport.**

- `VSNativeCamera` (`indra/newview/vsnativecamera.h`, 166 lines) holds yaw,
  pitch, roll, distance, pan, body yaw, held movement keys and the fly flag.
  It uses reference rates: 90°/s orbit, 45°/s roll, 5 m/s pan
  (`vsnativecamera.h:38-49`).
- `VSNativeSession` exposes `cameraOrbit`, `cameraZoom`, `cameraPan`,
  `cameraRoll`, `cameraReset*`, `cameraLookAt`, `cameraFirstPerson` and
  `cameraControls` (`vsnativesession.cpp:1127-1170`).
- `publishCamera` (`:1038-1059`) writes the result into the shared
  `LLViewerCamera`: origin, axes, near 0.1, far `vs_camera_far_clip()`, aspect
  and frustum. The renderer, UI projection and picking therefore share one view.
- Transport: `AgentUpdate` carries `CameraCenter`/`CameraAtAxis` (`:2486-2499`).
  It is sent immediately when controls change, and otherwise as a 10 Hz
  heartbeat (`:2614-2617`).

**Shared routing.** `llagentcamera.cpp` has 41 guards. Shared camera entry
points forward to the Vulkan session when focus is on the avatar:

- zoom (`:1075`, `:1126`), orbit (`:1192`, `:1218`), zoom fraction (`:1379`)
- focus offset (`:2034-2036`), land height (`:2330-2332`)

`completeNativeCameraMode` (`:2856-..`) maps first-person to mouselook. It
respects `EnableMouselook`, RLVa and the joystick override, and leaves the
follow and customize modes alone.

| Mode or feature | Status | Evidence |
|---|---|---|
| Third person: orbit, pan, zoom, keyboard | Implemented | as above; `nativeScroll` → `cameraZoom` |
| Mouselook | Implemented | `View.Mouselook` admitted (`vsstartupui.cpp:817`); `LLToolCompGun` registered |
| Focus on object (alt-click, `cameraLookAt`) | Implemented | `lltoolfocus.cpp:122-..`; `vsnativesession.cpp:1147-1151`; `NativeFocusState` (`llagentcamera.cpp:141`) |
| Camera floater and presets | Implemented (shared UI) | `camera`, `camera_presets`, `fs_camera_small`, `phototools_camera` admitted; `LLJoystickCamera*` admitted (`vsstartupui.cpp:554`) |
| Customize-avatar camera | Partial | the mode is handled in `completeNativeCameraMode` (`:2880-2886`), but the `CustomizeAvatar` menu function is not admitted |
| Joystick / flycam | **Absent from UI** | `pref_joystick` refused; `View.JoystickFlycam` not admitted |
| Zoom menu items (`View.ZoomIn/Out/Default`), Reset Camera Angles | Disabled | not in the callback gate |
| Mouse-drag orbit through `mNativeCameraDrag` | Dead code | see [Window and input](#window-and-input) |

## HUD and overlay

**Status: Partial.**

- **HUD attachments.** They are picked (`pickHUD`, `vsviewerwindow.cpp:499`)
  and drawn by the world renderer (chapter 02). `View.ShowHUDAttachments` is not
  admitted, so the user cannot toggle HUDs from the menu (Inference from the
  menu estimate).
- **`LLHUDView`.** It is created on connect and kept at the back
  (`vsviewerwindow.cpp:797-806`). `LLWorldMapView::initClass()` runs there for
  the shared arrow images (`:804`).
- **Name tags and HUD text.** `LLHUDNameTag::renderNative`
  (`indra/newview/llhudnametag.cpp:999`) is drawn as a depth-tested UI sub-pass
  (`vsvulkancontext.cpp:619-628`). Beacon labels come from
  `LLHUDText::renderNativeBeaconLabels` (`llhudtext.cpp:752`, called at
  `vsvulkancontext.cpp:671`).
- **HUD icons.** `LLHUDIcon::appendNative` (`llhudicon.cpp:436`) supplies them,
  and they are pickable (`LLHUDIcon::intersectNative`, `vsviewerwindow.cpp:544`).
- **RLVa `setoverlay`.** It runs as its own UI pass (`vsvulkancontext.cpp:629-633`)
  and blocks picks (`vsviewerwindow.cpp:523-528`).
- **Debug consoles and HUD info.** Texture, debug, fast-timer, scene-view and
  scene-monitor consoles are admitted, as are camera, wind, FOV, badge,
  cookies and MOTD HUD info (`vsstartupui.cpp:650-659`). Wireframe and Show
  Color are admitted (`:660-661`).
- **Vulkan-only gating.** Every in-world overlay above runs only when
  `RENDER_DEBUG_FEATURE_UI` is set (`vsvulkancontext.cpp:597, 619, 669`).
  Inference: Hide UI (`View.ToggleUI` is not admitted anyway) and that debug
  feature control all of them together.

## Chat, IM and connected UI

**Status: Implemented for text chat, IM, groups, profiles and contacts using the
shared FS UI. Partial at the transport-admission level.**

### Connected chrome

`setNativeConnected(true)` (`vsviewerwindow.cpp:725-826`) builds these from
shared XUI:

- the menu bar (`init_native_connected_menus`, `llviewermenu.cpp:703-710`, from
  `menu_viewer.xml`)
- the navigation bar (`panel_navigation_bar.xml`)
- the toolbar view (`panel_toolbar_view.xml`) with saved account toolbars
  (`loadToolbars`, `:818`)
- the chiclet bar
- `LLStatusBar`
- `LLHUDView`, plus hint registration

It also starts the notification channels (`LLChannelManager::onNativeLoginCompleted`,
`:825`). The Vulkan code adds nothing to the menu model except one remap: on
the Vintage skin, `people` is remapped to `imcontacts` (`llviewermenu.cpp:672-702`).

### Nearby chat

- **Diagnostic owner.** `VSPlainChat` (`indra/newview/vsplainchat.cpp`, skin
  `panel_vs_nearby_chat.xml`) is created at startup
  (`vsviewerwindow.cpp:252-258`). Its sender is wired to
  `VSNativeSession::sendChat` and its keystrokes to typing indicators
  (`vsnativesession.cpp:2081-2092`).
- **Production front end.** Ordinary sessions switch to the shared skinned
  `FSFloaterNearbyChat` (`useSharedFrontend(true)`, `vsnativesession.cpp:2101-2106`;
  `vsplainchat.cpp:104-119`). The private transcript remains only as the
  diagnostic oracle (`#if VS_VULKAN_DIAGNOSTICS`, `:2102-2104`).
- **Console.** The chat console (`LLConsole`) is shown only when
  `FSUseNearbyChatConsole` is set (`vsviewerwindow.cpp:734`).

### IM transport

`vs_native_im_install` replaces the `ImprovedInstantMessage` handler
(`indra/newview/vsnativeim.cpp:121-124`; installed at `vsnativesession.cpp:501`).
`vs_native_im_receive` (`vsnativeim.cpp:68-106`) passes 18 dialog types to the
shared `process_improved_im`:

- `NOTHING_SPECIAL`, `SESSION_SEND`, `TYPING_START/STOP`
- `DO_NOT_DISTURB_AUTO_RESPONSE`
- friendship offered, accepted and declined
- group invitation, notice and notice-requested
- inventory and task-inventory offered, accepted and declined
- `MESSAGEBOX`

**Every other dialog is logged as "Unadmitted IM dialog" and dropped.** That
includes:

- `IM_LURE_USER` (teleport offers), `IM_TELEPORT_REQUEST`, `IM_GODLIKE_LURE_USER`
- `IM_SESSION_INVITE`
- `IM_FROM_TASK` and `IM_FROM_TASK_AS_ALERT` (object IMs)
- `IM_GOTO_URL`
- `IM_CONSOLE_AND_CHAT_HISTORY`

No `vs*` source handles these dialogs:
`grep -rn "IM_LURE_USER\|IM_FROM_TASK\b\|IM_SESSION_INVITE\|IM_TELEPORT_REQUEST" indra/newview/vs*`
returns only replay files. Offline IMs are fetched through
`RetrieveInstantMessages` (`vsnativeim.cpp:125-140`).

### IM and contacts UI

`FSFloaterIM`, `FSFloaterIMContainer`, `FSChatHistory`, `LLChatEntry`,
`FSFloaterContacts`, `FSFloaterRadar` and `LLAvatarList` are admitted, as are
the toast panels (`LLToastIMPanel`, `LLToastNotifyPanel`), `LLIMWellWindow`,
`LLNotificationListView` and `LLScriptFloater`. The admitted conversation
floaters are `fs_impanel`, `fs_im_container`, `imcontacts`, `conversation`,
`preview_conversation`, `im_well_window` and `notification_well_window`
(`vsstartupui.cpp:571-575`). The Linden variants (`impanel`, `im_container`,
`nearby_chat`) are refused. Firestorm uses the FS variants, but 32 shared
`LLFloaterReg::getInstance`/`getTypedInstance` call sites in
`indra/newview/*.cpp` still name the LL floaters. The largest groups are 17 for
`nearby_chat` and 13 for `im_container`, for example `llagent.cpp:2801,2969`.
Find them with a regex over `LLFloaterReg::(getTypedInstance|getInstance)(<…>)?("name"`
for the refused names. Those calls now receive `nullptr`.

### Account lifetime

- `vs_native_im_guard` and its callback wrappers (`vsnativeim.h:27-75`)
  invalidate delayed UI work from a previous login.
- `vs_native_im_reset` (`vsnativeim.cpp:179-288`) and `vs_retire_native_object_ui`
  (`:141-177`) destroy about 60 named floaters and reset account singletons on
  logout.
- Notification payloads carry an epoch and generation stamp (`:290-311`).

### Other connected UI

- **Groups.** `VSGroupSearch` (`vsgroupsearch.cpp`, 39 lines;
  `floater_vs_group_search.xml`) hosts the shared group directory panel. It is
  reached from `llgroupactions.cpp:290`.
- **Profiles, picks, classifieds and groups.** These panels are admitted by
  name (`vsstartupui.cpp:596-605`).
- **SLURL and app commands.** `LLCommandDispatcher` admits only `login` during
  the login phase and a fixed connected set (`isNativeConnectedCommand`,
  `indra/newview/llcommandhandler.cpp:230-287`). The set includes `teleport`,
  `help`, `search`, `keybinding`, `profile`, `balance`, `chat` and `event`.
  Every other command shows "This viewer command is not available in the Vulkan
  UI/chat stage."
- **Orphaned skin file.** `panel_vs_connected_controls.xml` is not referenced
  by any source: `grep -rn panel_vs_connected_controls indra` matches only the
  file itself.

## Maps and minimap

**Status: Implemented. Vulkan drawing is added inside the shared map classes.**

- **Minimap.** `LLNetMap::draw` dispatches to `drawNative()` in Vulkan mode
  (`indra/newview/llnetmap.cpp:332-333`). About 580 Vulkan lines cover the map,
  objects and parcels: `drawNative` (`:2399-2712`), `drawNativeObjects`
  (`:2715-2861`) and `drawNativeParcels` (`:2864-2979`). The object and parcel
  rasters are published as Vulkan images (`:1652-1675`).
- **Minimap menus and floater.** `Minimap.*` callbacks are admitted
  (`vsstartupui.cpp:666, 674`). `LLFloaterMap` and `LLNetMap` are admitted
  types, and the `mini_map` floater is admitted.
- **World map.** `LLWorldMapView` and `LLFloaterWorldMap` are admitted.
  `llworldmapview.cpp` has 16 Vulkan or `isNativeUI` sites:
  - Vulkan region tiles and land-for-sale images (`:527`)
  - object tiles (`:846-849`)
  - avatar and agent markers (`:1106-1109`)
  - current-region detection from the Vulkan session (`:379-380`)

  `llworldmipmap.cpp` (4), `llworldmap.cpp` (3) and `llfloaterworldmap.cpp` (4)
  carry the remaining hooks.
- **Fixtures.** `scripts/tests/native_map_{actions,geometry,images,models,observers}_fixture.cpp`.
- **Gaps.** The `compass` toolbar command is refused. Teleport from the map uses
  the admitted `teleport` command path. Inference: map-driven teleports depend
  on `vsteleportpolicy.cpp`, which is covered in chapter 04.

## Preferences and graphics settings

**Status: Implemented for the shared preferences UI. Partial for graphics:
controls without a Vulkan consumer are disabled.**

- **Coverage.** The `preferences` floater and every panel class used by
  `floater_preferences.xml` are admitted (`panel_preference`, `_backup`,
  `_controls`, `_crashreports`, `_firestorm`, `_graphics`, `_opensim`,
  `_privacy`, `_skins`, `_sounds`; `vsstartupui.cpp:592-595`). The command is
  `grep -o 'class="[^"]*"' skins/default/xui/en/floater_preferences.xml | sort -u`.
  QuickPrefs (`quickprefs.cpp`, 15 guards), preset save, load and delete, and
  `prefs_view_advanced` are admitted.
- **Refused.** `prefs_graphics_advanced` (the advanced graphics floater) and
  `pref_joystick`.
- **Graphics gating.** `LLFloaterPreference::refreshEnabledState`
  (`indra/newview/llfloaterpreference.cpp:2229-2262`) walks every control in
  Vulkan mode. It disables any control bound to a feature-table setting, or to
  a "pending pass", unless `vs_graphics_feature_implemented` accepts it.
  `vs_graphics_feature_implemented` (`indra/newview/vsgraphicssettings.h:17-32`)
  accepts:
  - three capability-dependent settings: compression, anisotropy, maximum
    texture resolution;
  - 13 named settings with a live consumer: avatar LOD and physics LOD,
    maximum complexity, auto-mute area, far clip, terrain LOD, PBR detail and
    planar samples, tree LOD, volume LOD, `WLSkyDetail`, and the two
    draw-distance overrides.
- **Pending passes.** `vs_graphics_pending_pass` (`:36-42`) lists 11 settings
  that are kept but disabled: DOF, the glow family, sharpness, FSAA type and
  samples, transparent water, `RenderGammaFull`.
- **Feature table.** `LLFeatureManager` marks unimplemented features
  unavailable (`indra/newview/llfeaturemanager.cpp:726`). Inference: shadows,
  SSAO and reflection controls are disabled because they appear in neither list.
- **Settings observers.** A separate allowlist limits which setting observers
  may fire under Vulkan (`native_shared_setting_callback`,
  `indra/newview/llviewercontrol.cpp:1300-..`).
- **Backend switch.** `render_backend` stays enabled
  (`llfloaterpreference.cpp:2256`; XUI
  `panel_preferences_graphics1.xml:973`). It writes `RenderBackend` after a
  restart (`app_settings/settings.xml:12285-12299`).

## Media UI

**Status: Implemented through shared controllers, with Vulkan image and hover
plumbing.**

- **In-UI web views.** `LLMediaCtrl` is admitted. In Vulkan mode it publishes
  plugin pixels through `vs_update_media_image` (`indra/newview/llmediactrl.cpp:852-866`).
  This covers the login page, help browser, web content, search, marketplace
  and profile web.
- **Prim media.**
  - `LLPanelPrimMediaControls` is admitted (`vsstartupui.cpp:424`), and
    `llpanelprimmediacontrols.cpp` has 14 guards.
  - Clicks, double-clicks and hover reach `NativePieMediaTarget`
    (`lltoolpie.cpp:2186-2550`).
  - Hover goes through `nativeMediaHover` (`vsviewerwindow.cpp:93-108`) and
    `handleNativeMediaHover` (`lltoolpie.cpp:3003-3008`).
  - Media mouse capture is retired on lifetime loss (`:2985-2989`).
  - `llviewermediafocus.cpp` has 10 guards.
- **Media panels and settings.** `LLPanelNearByMedia`, `LLFloaterMediaSettings`
  (`media_settings`, with general, permissions and security panels),
  `whitelist_entry` and `media_lists` are admitted. The `MediaCtrl.*`,
  `MediaListCtrl.*` and `SelectedMediaCtrl.*` callbacks are admitted
  (`vsstartupui.cpp:818-819`).
- **Change history.** `a9e852d2e8` rewired object and parcel media; the media
  pipeline itself is covered in chapter 04.
- **Snapshots.** The snapshot UI (`snapshot`, `simple_snapshot`, `flickr`,
  `primfeed`, `360capture`) is admitted. Snapshots render through
  `vs_native_snapshot` (`llviewerwindow.cpp:6553-6556, 6916-6917`).
- **Gap.** The Flickr and Primfeed panel injectors are refused (see
  [Admission gates](#admission-gates)).

## Risks and open questions

1. **Missing translation unit (likely link failure).**
   `vsselectionlightpresentation.cpp` and `vsworldpaint.cpp` exist but are not
   in `indra/newview/CMakeLists.txt`. The check
   `for f in indra/newview/vs*.cpp; do grep -q "$(basename $f)" indra/newview/CMakeLists.txt || echo $f; done`
   prints exactly these two. Meanwhile `vsvulkancontext.cpp` includes
   `vsselectionlightpresentation.h`, constructs `VSSelectionLightPresentation`
   (`:310`) and calls `append` and `reset` (`:599, 645`). The header declares
   these out of line (`vsselectionlightpresentation.h:9-11`). Inference: a
   `USE_DILIGENTCORE=ON` viewer link fails with unresolved symbols. This was
   not built. It is consistent with the CI graphics jobs being red for other
   reasons.
2. **Closed allowlists hide failures.** A refused widget returns `nullptr` and
   only logs (`vsstartupui.cpp:555`). Floater callers that assume a non-null
   instance (`getTypedInstance<>`) can crash or no-op. 32 shared call sites
   name refused LL floaters (see [IM and contacts UI](#im-and-contacts-ui)). Most are Linden-only code
   paths that Firestorm does not use; this was not proven site by site.
3. **Permanently disabled menu items.** A menu item whose callback is not
   admitted is disabled for the whole process (`llmenugl.cpp:820-862`). It stays
   disabled even if a later admission change would cover it, until the menu is
   rebuilt. Roughly half of `menu_viewer.xml` (289 of 591 items by the static
   estimate) and most pie slices are affected.
4. **Building and editing are unreachable.** There is a lot of Vulkan selection
   and edit code (about 7,000 lines), but no admitted entry point opens Build,
   moves, rotates or scales an object, or links one. Manipulator tools are not
   registered. `gFloaterTools` stays null. Open question: is the edit code
   exercised only by source fixtures (`scripts/tests/native_selection_*`)?
5. **Dropped IM dialogs.** Teleport offers and requests, object IMs, session
   invites and `GOTO_URL` are dropped. A user cannot accept a friend's
   teleport offer.
6. **Dead camera-drag code.** `mNativeCameraDrag` is never set to true. Either
   remove it or wire it in.
7. **Platform window limits.** Fullscreen is refused. Linux runs on X11 only,
   with no Wayland support.
8. **Inconsistent admission.** Some floaters are admitted while their toolbar
   command or panel injector is refused (`appearance`, `beacons`,
   `fs_posestand`, `flickr`, `places`). Four admitted floater names have no
   registration. Open question: how should the admission lists be maintained?
   They are hand-edited `||` chains; the longest line, 540, is 2,151
   characters (`awk '{print length($0), NR}' indra/newview/vsstartupui.cpp | sort -nr | head -1`).
9. **Object hover tooltips are probably absent.** The tooltip path resolves
   objects through `gObjectList`, which Vulkan sessions do not populate
   (Inference).
10. **No runtime evidence here.** This chapter claims nothing about
    pixel-correct rendering, input latency or IME behaviour. The relevant
    evidence would come from the diagnostic replays
    (`vsnativereplay.cpp`, `vsnativeimreplay.cpp`, `vstooltipreplay.cpp`,
    `vsvkdiag.cpp:1403-1555`), and none of them were run.
11. **Stale audit document.** `doc/vulkan/connected-ui-completeness-audit.md`
    reports 77 admitted floaters, against 206 now. It also describes child
    removals and a right-click camera drag, neither of which exists any more.
    Anyone citing it should re-run its census against HEAD.
