# Independent Vulkan connected-UI completeness audit

Baseline: `c06ed7f2df8bf5100fe76d06d3f77e3b30ad07c7` on `vkstorm-vulkan`.

Status: **unqualified**. This is an independent source audit, not a declaration that the UI is complete or that pending R3 qualification has passed. It does not change R3's acceptance or certify later working-tree corrections. No production code was changed by this audit. UI work must remain unpushed until all three R3 qualifications and their uploaded evidence have been verified, as requested.

## Findings

The native connected UI is still governed by startup-era closed admission lists. These lists reject whole floater/controller families, remove toolbar commands and permanently disable menu items. Separate service and input gates also refuse actions that survive construction. A successful test of the admitted connected-session subset cannot demonstrate complete UI integration.

The requested outcome is functional UI parity: the same shared control, layout, selection and authorization logic used by OpenGL, connected to actual native services. Legitimate shared permission, selection, maturity, RLVa, account, media-availability and hardware conditions must remain. Removing a native blanket gate without integrating its controller or renderer is not a fix.

## Census and evidence method

[The machine-readable inventory](connected-ui-completeness.json) preserves every discovered source registration and every menu callback occurrence separately, with its file, line, parameter, admission result and production dependency family. All baseline input came from immutable Git blobs, so concurrent implementation changes cannot alter this audit's baseline.

The census scans 1,579 C++ source files and 8,207 shipped XUI/command XML files, including every tracked skin, theme and language. Of those XML files, 1,760 have menu roots. All parsed successfully.

| Inventory | Total | Admitted | Excluded | Dynamic |
| --- | ---: | ---: | ---: | ---: |
| Distinct registered floater names | 229 | 77 | 152 | 0 |
| Floater registration occurrences | 234 | 82 | 152 | 0 |
| Toolbar command names | 69 | 24 | 45 | 0 |
| Distinct registered panel-factory names | 123 | 51 | 72 | 0 |
| Panel-factory registration occurrences | 128 | 56 | 72 | 0 |
| XUI panel-factory uses | 650 | 176 | 90 | 384 |
| Menu callback/parameter occurrences | 4,217 | 1,055 | 3,162 | 0 |
| Distinct menu callback/parameter combinations | 2,594 | 571 | 2,023 | 0 |

Panel-factory registrations include 96 class injectors and 32 owner-local named factory-map entries. Repeated registrations, aliases and compile-time branches are retained; these counts are source occurrences, not a claim that every branch is active on one platform. Named XUI panels invoke a specialized factory only when that owner is present on the runtime factory stack; 384 such uses therefore remain dynamic rather than being mislabeled as definitively routed.

Menu decisions were obtained by compiling the baseline callback predicate and delegated Inventory, People and Chat predicates verbatim in a standalone C++ evaluator with string LLSD parameters. Non-menu callbacks are separately retained but marked dynamic: direct control binding is not governed by the menu admission decision. Helper-based widget admission and custom-builder behavior are explicitly unresolved in each registration's `widget_status`. Admission never means end-to-end functionality.

The local reproducibility helper is `.tmp/audit_connected_ui_inventory.py`; its extracted policy evaluator is `.tmp/connected_ui_policy.cpp`. They are audit tools, not viewer changes. The JSON records policy-source hashes and baseline revision. XML overlays are inventoried individually; geometry and overlay merging still require runtime qualification.

## Shared roots

| Root | Baseline evidence | Correction required |
| --- | --- | --- |
| Specialized widget allowlist | `indra/newview/vsstartupui.cpp:223–328` | Reuse shared widgets with native drawing and real controller ownership. A base panel admission does not admit its derived types. |
| Floater-name allowlist | `vsstartupui.cpp:330–342`; `indra/llui/llfloaterreg.cpp:187,308,316` | Restore production floater construction, nested factories and callbacks. |
| Panel-factory allowlist | `vsstartupui.cpp:343–355`; `indra/llui/llpanel.cpp:849`; `llpanel.h:283` | Integrate class injectors and owner-local factories, not just outer windows. |
| Toolbar-command allowlist | `vsstartupui.cpp:363–366`; `indra/llui/lltoolbar.cpp:433,996,1109,1155,1422` | Restore shipped command routes and normal enable/check state. Toybox also hides rejected commands. |
| Callback/parameter allowlist | `vsstartupui.cpp:368–470`; `indra/llui/llmenugl.cpp:820–862` | A rejected visible, enable, click or check callback can permanently disable an item. Integrate the complete callback family. |
| Child removal | `vsstartupui.cpp:473–482` | Restore People Nearby and Vintage nearby voice monitor with actual spatial data/services. |
| Setting observer allowlist | `indra/newview/llviewercontrol.cpp:1295–1375` | Saved graphics/scene choices may change without any observer. Bind native controllers before exposing working choices. |
| Debug-setting edit gate | `llfloatersettingsdebug.cpp:59–67` | Replace narrow ToggleControl-based read-only policy with actual native setting ownership. |
| App/SLURL action allowlist | `llcommandhandler.cpp:230–277` | Integrate native service handlers while retaining ordinary browser trust and throttling rules. |
| World context menus skipped | `llviewermenu.cpp:649–666` | Native startup returns before avatar/object/attachment/land context-menu construction. |
| Native world mouse dispatch | `vsviewerwindow.cpp:399–444` | Right-click starts camera drag; it does not invoke the reference world context interaction. Route native picks into real menu/tool controllers. |

## Production dependency map

`VSNativeSession` explicitly excludes `LLWorld` and `LLViewerRegion` ownership (`vsnativesession.h:27`). Shared controllers still assume these owners. Instantiating a fake reference region is unsafe: `LLViewerRegion`'s constructor (`llviewerregion.cpp:624–712`) constructs terrain/composition, parcel overlays and spatial partitions. Share CPU data, policy and transaction controllers and provide native graphics/service adapters instead.

`LLViewerParcelMgr` is a useful separation seam: its constructor (`llviewerparcelmgr.cpp:119–225`) creates real CPU `LLParcel` and selection data, but its query/selection methods resolve `LLWorld`/`LLViewerRegion` (`:260`). Native parcel flags for icons do not substitute for complete parcel properties, selected boundaries, ownership, access lists, mutations and server acknowledgements.

`VSWorldScene` already exposes authenticated generation-bearing object and face handles, picking, material lookup, selection bounds, task inventory, attachments and avatar access (`vsworldscene.h:30–98`). These are usable integration foundations, but do not by themselves instantiate the reference selection/tool/UI controller chain.

## Direct production gaps

| Family | Confirmed baseline exclusions/refusals | Required wiring |
| --- | --- | --- |
| Places, land and landmarks | Address info/buy/add-landmark return at `lllocationinputctrl.cpp:668,684,697`; info disabled `:879`; landmark hidden/disabled `:1251`; status equivalents refuse at `llstatusbar.cpp:1826,1856,1866`. Land/Places/detail floaters and factories excluded. | Complete authenticated parcel/region data and selection/send owners, existing Places/landmark/account models, land management transaction replies. |
| Map, minimap, radar and compass | Floaters and toolbar commands excluded. `LLNetMap::draw` returns without LLWorld (`llnetmap.cpp:310`) and then uses legacy map textures. | Native topology/avatar/parcel snapshots, existing map protocol/tracking models, Vulkan map drawing and shared list/controller integration. |
| Nearby People and avatar picker | Nearby tab removed before construction; nearby updates skipped at `llpanelpeople.cpp:1121,1822,1953`; picker Near Me disables list/OK at `llfloateravatarpicker.cpp:476–482`. | Native avatar enumeration, positions/range and name-cache-backed shared list updates. |
| Profile, Favorites and history location actions | Pick map/teleport refuse at `llpanelprofilepicks.cpp:855,868`; Classified equivalents `llpanelprofileclassifieds.cpp:1310,1324`; history map/details refuse at `llpanelteleporthistory.cpp:67,628,649` and `llteleporthistorystorage.cpp:370`; Favorites map/create-pick refused `llfavoritesbar.cpp:1585,1618`. | Native teleport already exists through LLAgent. Separate optional map tracking from teleport, then integrate map and Places controllers. |
| Avatar actions | Offer TP `llavataractions.cpp:256`, map `:641`, request TP `:742`, track `:926`, teleport-to `:944`, zoom permission/action `:1983,2012`, script info `:2034` refuse Vulkan. Chat context adds its own refusal at `fschathistory.cpp:444–450`. | Real native sends/capabilities, avatar snapshots and native focus/tracking while retaining normal account/RLVa/permission policy. |
| Firestorm bridge | `fslslbridge.cpp:64` unconditionally denies native ownership. Startup's call to `initBridge` does not overcome it. Native GenericMessage handler forwards only ExperienceEvent (`vsnativesession.cpp:365–371`). | Attachment creation/rez/wear/task inventory/script upload/confirmation, bridge handshake and inbound messages, with account/circuit guards and actual completion/error UI. |
| World context and build tools | Reference world menus skipped; native right-click used for orbit. Build/object tool families excluded. | Native selection and properties/permissions, touch/sit/open/buy/take/delete/return/transform/link/create operations, task inventory and script transactions, existing controls with native tool rendering. |
| Appearance and attachments | Appearance/outfit/hover/gesture/animation-preview families excluded. `LLInventoryPanel::attachObject` refuses at `llinventorypanel.cpp:2097`. Inventory callback policy excludes wear/attach/gesture/create-wearable action families. | Connect existing VSNativeAppearance transactions, native avatar/attachment handles, previews and hover-height to shared controllers; retain inventory and RLVa permissions. |
| Camera and movement | Basic camera controls admitted, but menu/toolbar variants remain excluded: zoom/reset angles/last chatter/selection focus/joystick, fly/run/sit/move-lock. Native drag sensitivity remains fixed at `vsviewerwindow.cpp:454–462`. | Route shared command variants to native state, normal input/focus/restriction policy and reference saved sensitivity; integrate relative mouselook behavior. |
| Graphics, environments and presets | Quickprefs, Phototools, environment editors, advanced graphics/view and graphics preset windows excluded. Preferences disables Defaults/Wireframe and returns (`llfloaterpreference.cpp:2241`); atmosphere commit returns `:3150`; choices explicitly retained for future rendering `:4058`; preset creation omitted `:4087`. Graphics preset status and lighting clicks refuse/return (`llstatusbar.cpp:1075`, `llnavigationbar.cpp:1045`). | Native capability recommendations, setting observers and renderer effects; existing environment/day-cycle/preset models consumed by native renderer and previews. Simply admitting Quickprefs is unsafe: `quickprefs.cpp:2137` dereferences legacy gAgentAvatarp. |
| Snapshots and sharing | Snapshot/simple/360, preview helpers, Flickr/Primfeed routes excluded; text-only scene snapshot exception remains in `llviewermenufile.cpp:1057`. | Production native readback/capture lifecycle, aspect/UI inclusion, color/orientation, encoding and shared save/upload/share model. Frame observer hooks are not full snapshot integration. |
| Voice | Speak/voice commands and call/device/effect/control windows excluded; spatial monitor removed. Engine/account initialization is insufficient. | Native spatial/channel lifecycle, shared participants/call/volume/device controls and ordinary mute/permission state. |
| Sound and media | Sound-preview network Play refuses at `llpreviewsound.cpp:86`; audition is separate. Sound explorer, stream-title/history, device and media-settings routes excluded. Parcel playback exists but object media remains separate. | Actual simulator sound trigger and emitter positions, object-media ownership and authenticated interaction, existing audio/media controllers. |
| Upload/editor/task workflows | Model/material/animation upload refuses at `llinventoryfunctions.cpp:4226`; model/material/local mesh/live material/particle/import/export/task script/bulk permission UI excluded. | Shared upload/permission/transaction model plus native preview rendering, selection and asynchronous completion. Existing image/sound upload is not equivalent. |
| Pipettes and thumbnails | Texture/color pipette callbacks return at `lltexturectrl.cpp:1212` and `llfloatercolorpicker.cpp:465`; thumbnail snapshot refuses at `llfloaterchangeitemthumbnail.cpp:557`. | Native pick/material/readback adapters through reference tool contracts and production capture owner. |
| Currency and commerce | Buy-currency status action refuses at `llstatusbar.cpp:1030`; currency/payment/tracker/object purchase windows excluded. Marketplace toolbar variant absent. | Existing account/browser/economy/capability models; object/land purchases also need real selection, sale properties, permissions and replies. |
| Moderation, estate and reporting | Reporter/region/estate/god/pathfinding windows excluded. Experience Report disabled at `llfloaterexperienceprofile.cpp:175,875` and `llpanelexperiencelog.cpp:210,257`. | Native estate/region permission and capability owners, real simulator replies and report/capture model. Shared authorization remains mandatory. |
| CPU/account utility windows | Forgotten usernames, preference presets, window size, protected folders, Discord, blacklist, script/log/notification tools remain excluded. | Review and reuse CPU controllers, admitting nested factories and observers; do not retain startup exclusion merely because no earlier test exercised them. |
| Advanced/developer facilities | Broad renderer/UI/debug/cache/admin callback families excluded. | Native diagnostic and effect implementations where applicable, retaining intentional hardware, permissions and destructive-debug semantics. GL-only counters cannot be relabeled as Vulkan measurements. |

## Startup and logout

No new source evidence was found that production startup/logout progress is absent in this baseline. `vsnativesession.cpp:1831–1916` implements inventory readiness, misc startup, precache, wearables and started transitions. `:2187–2224` presents logout progress and retains it through the first presented frame before a fast authenticated reply can destroy it. This is source evidence, not a substitute for a live regression test.

The bridge is a confirmed exception: a startup invocation is present but its actual owner guard still refuses native operation. Initialization-call coverage must therefore be verified against production completion, not counted as sufficient by itself.

## Existing R2/R3 foundations still blocked at UI routes

* Native teleport already handles location, landmark, home and lure destinations, yet Pick/Classified teleport, Home menu and several avatar/menu variants remain excluded or refuse first.
* Native picking, generation-bearing object/face handles, material lookup, selection bounds and task inventory exist; ordinary world context/tool integration remains missing.
* Native appearance and attachment transactions exist; outer appearance/outfit UI and several inventory action routes remain excluded.
* Native camera orbit/pan/roll/zoom, movement, flying, animation requests and skeleton reset exist; corresponding alternate menu/toolbar callbacks remain blocked.
* Native CPU audio/account initialization exists; network sound Play and various browser/account utilities remain separately refused.

The JSON's `implemented_native_paths_still_blocked` records these actionable mismatches with evidence and required ownership. It does not claim every broad service family is fully implemented.

## Closure and qualification

1. Integrate shared CPU data/policy/controllers with native region, parcel, selection, avatar, attachment, capture, media and renderer owners. Remove stale native refusals after each actual service is connected.
2. Restore complete command/callback families, including enable/check/visible callbacks, direct buttons, toolbar customization, nested factories, action URLs, accelerators and context input.
3. Exercise real control activation, authenticated request/reply, model update and UI feedback. Construction and enabled appearance alone are insufficient.
4. Cover every shipped skin/theme and effective language overlay. Inspect merged geometry as well as callback policy; this static census does not prove visual parity.
5. Exercise logout, account switch, cancellation, late callbacks and region crossing with operations pending. Stale work must cancel safely, not leave controls permanently disabled.
6. Add a permanent completeness check for newly registered shipped UI and direct native gates, so an omitted route cannot silently disappear behind an admission whitelist.
7. Keep real unavailable-service, unauthorized, no-selection, maturity, RLVa and renderer-capability conditions. The acceptance target is no *Vulkan blanket gating* or nonfunctional admitted controls, not bypassing ordinary viewer policy.

R3 runtime and uploaded-artifact qualification remains separate and pending in this audit. Future fixes need their own source/evidence attribution; this baseline must not be presented as qualified UI parity.

## Local integration following this baseline

The census above remains the immutable c06 baseline, not a claim about the current closure count. Local work now connects shared keyboard binding loading and window dispatch, utility/currency lifetimes, authenticated spatial/coarse/map models, native shared map drawing, CPU camera publication and generation-bound task inventory transport. The shared bridge lifecycle is being transposed through native attachment/configuration/script adapters; it is not qualified by the presence of startup calls.

Actual headless viewer replay exposed and corrected Stream Title visibility teardown, the fixture's use of an incorrect Font Test registry key, and the missing native startup key-binding loader. Keyboard turn/release, Enter-to-chat, Backspace, Unicode and release-only gesture routes subsequently executed in the actual staged viewer. The utility tutorial legitimately opens `TutorialNotFound` without a grid tutorial URL; the fixture must acknowledge its real `OK_okbutton` before continuing mouse tests. Full connected replay, the skin/language/scale matrix and complete UI parity remain open until their current-source runtime evidence passes.

Map/minimap and broader world UI are not enabled merely because their rendering/data foundations now exist. Shared parcel selection/hover/collision, region limits, land transactions, full map actions and remaining service/controller families must be connected before their ordinary controls can be accepted. Preserve reference permission, selection, availability, maturity and RLVa policy throughout. The UI push remains held pending all three R3 qualification jobs and verification of the uploaded artifacts.
