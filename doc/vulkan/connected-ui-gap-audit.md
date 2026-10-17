# Connected native viewer UI gap audit

The minimum deliverable is the real skinnable connected viewer UI with ordinary login, local chat, direct IM and group IM. World drawing remains deferred. A constructed test panel or nearby-chat-only replay does not establish that acceptance.

This inventory describes implementation and outstanding qualification separately. It does not declare the deliverable accepted or claim that any pending build/runtime check has passed.

## Connected chrome and nearby frontend: current integration

### All-skin tint and connected qualification, 10 October 2026

Implementation source is `fa6d71c213ebff00e3aad1640499d717ae2e231d` on
`vkstorm-vulkan`. Shared native image, rectangle and triangle tints now use the
same clamp and normalized U8 truncation as the actual OpenGL renderer. This
corrects inactive-tab shading across all eight packaged skin families and every
catalogued theme, without changing their color definitions. All **68** staged Windows viewer diagnostic cases passed, including independent
selected inactive-tab asset/readback probes for every skin/theme
(`.tmp/connected/parity/diagnostic44/results.json`).
The diagnostic image's rendering implementation is identical to the final stage;
it predates the later retired-IM guard, precise Vintage corrections and replay
control-type correction. Subsequent inventory changes respect optional skin
controls while preserving each existing button or label/tab layout.

All **26 connected cases** passed with **100 mandatory assertions per case**,
actual authenticated conference-start receipts, pinned SwiftShader and
core/synchronization validation, clean shutdown and no GL access. They cover
all 23 catalogued English skin/theme selections, the default German overlay,
and Modern at 125%/150% scale. This is all-skin/theme coverage, not every
skin/language combination. The evidence ledger is
`.tmp/connected/parity/matrix-qualified.json`: 22 successful build45 cases are
retained, German and both scale cases passed canonically on build47, and
Vintage passed canonically on build48. Subsequent changes only correct Vintage's
missing Send-button image reference, omit the exact scene-owned spatial voice-monitor leaf (including
German), make the replay use the common chat-input base class, and correct the
optional inventory controls omitted by Vintage. Existing button bindings and
labels in the retained successful skin cases preserve their prior behavior.
Their scope and each tested executable/source revision are recorded; this is not a claim
that all cases ran on one binary. Reused reports record loaded DLL hashes; their
main executable hash is supported separately by the contemporaneous
`build45-manifest.json` and retained build45 image, rather than by the per-case
DLL observer.

Vintage's real repeated Preferences traversal retains all 66 paths and 396
draws. Profiling measured sustained main-thread work in the pinned software
renderer; the original 180-second self-quit interrupted the exhaustive replay.
The canonical viewer/process/case deadlines are now finite 480/500/530 seconds.
A real stalled-child test proves termination, output preservation and failure;
no acceptance assertion or pixel tolerance was relaxed. Failed Vintage45,
Vintage46, its privately bounded fixture failure, Vintage47 dummy controls,
and aborted German45 evidence remain preserved separately from accepted results. The real conference
remap/close and delayed-notification fixes also retain the original replay43
failure evidence.

The complete Autobuild-library RelWithDebInfo stage, without an installer, is
`build-vcuiparity-final-local/newview/RelWithDebInfo`. Its build48 executable
SHA-256 is `a9cc8510879ec9d861e675fe6526273956a6f31aef60c329230986f7e25eec84`;
PDB SHA-256 is `14242731f95b65d6fdb37049d4c8387faaa535c3afba996683b83b2f517213ba`.
Original user-test stages are preserved. The 76 dedicated source/compiler,
runner and staging tests and 22 documentation/source-pin tests passed.
These results qualify local Windows software rendering and deterministic
connected protocol/UI replay. They do not establish new Linux, physical-driver
or live-grid interoperability results. Normal native viewer launches continue
to use the machine's Vulkan driver; the software ICD is selected by the isolated
test environment.

The user's live login establishes that ordinary credentials can connect the native viewer. It also exposed missing address/favorites controls, toolbar placement and pointer behavior, plus a crash during IM interaction. Those reports prevent treating the earlier diagnostic replay as connected OpenGL UI parity.

The current source uses the shared `LLNavigationBar`, location input, favorites and navigation history, `LLStatusBar`, main menu and `LLToolBarView` rather than a second chrome layout. Account toolbar positions, display modes, alignment and layout use the existing `toolbars.xml` serializer. The real Toybox configurator and command drag/drop operate through UI mouse capture and shared two-dimensional drop targets; they do not select world tools. Restore/clear confirmations retain account-generation ownership. Native OS input/cursor handling belongs to the viewer window integration.

The shared Contacts tab content has eight additional UI pixels of right inset in the default, Modern and Vintage layouts. Right-following buttons retain their dimensions and move with the tab content; localized overlays inherit the change. This XUI correction applies to both renderer backends.

Normal connected chat now opens the actual skinned `FSFloaterNearbyChat`, including its `FSChatHistory`, chat entry, volume/channel controls, history/search, rich/plain styles and existing emoji/mention controls. `VSPlainChat::useSharedFrontend(true)` selects that frontend and focuses its real editor. Typed local-chat and IM/group-IM metadata is forwarded into the shared history; the diagnostic transcript remains available for protocol/readback fixtures. Shared chat transformations and RLV policy precede native session transport. Account reset retires nearby chat, text search, inventory and Toybox controllers and their scoped settings callbacks. Logout immediately hides the shared nearby frontend. Nearby log policy remains account-owned and avoids duplicate logging.

Inventory uses the actual `LLFloaterSidePanelContainer` / `LLSidepanelInventory` / `LLPanelMainInventory` graph, including the inventory tabs, search/filter/sort, folder views and reviewed account actions. The standard bottom-toolbar chat entry is `FSNearbyChatVoiceControl`; its native text route does not create the spatial voice-monitor owner. The People/Contacts command uses the existing skinned account frontend and reviewed contact/group actions. Scene-dependent radar/minimap functionality remains outside this text-only stage.

Context menus use the same XUI items and production registrars as those frontends. Reviewed resident/account URL actions pass the ordinary command trust rules; scene-dependent verbs stay excluded. Rich object-chat headers derive their location from native session metadata and open the actual CPU remote-object inspector, avoiding `LLWorld`. Block List opens the native account floater regardless of the legacy sidebar preference. Gallery actions reject retired account owners and empty selections; delayed RenameItem responses carry account-generation stamps. Replay assertions commit actual nearby/direct/group options, resident/object header menus and inventory right-click Properties. These assertions still require execution in the staged viewer.

The connected native root and window use the same rounded physical-to-logical extent at fractional UI scale. A previous truncated root retained a one-pixel discrepancy in follows-based chrome layout after resizing. Status has its own root-owned container: its height comes from the selected layered `main_view.xml` metadata, without constructing the scene-bearing main-view graph. Menus retain their separate height; navigation and toolbars reserve the actual status row. Geometry qualification checks the status text against its selected XUI parent, top offset and height. The existing time and balance text deliberately overhang the row by one and three pixels respectively in the standard skin; source-derived witnesses preserve this OpenGL layout instead of changing XUI or applying a general bounds tolerance. Native status FPS now reads the CPU frame periods sampled by `update_native_statistics()` from the ordinary native frame loop. Statistics and Lag Meter use their existing skinned controllers, with native triangle history drawing and explicit unavailable values until a measured sample exists. Current-circuit ping, packet-loss and bandwidth samples are account/host scoped; admitted `SimStats` replies use the shared decoder without constructing `LLWorld`. Account changes clear the recording and jitter state. Empty statistic recordings are not queried. Compiler fixtures exercise the actual sampler, account/host reset and native chart geometry; the new real Stats/Lag Meter replay remains pending staged execution.

Shared Edit callbacks retain focused-editor ownership. Delete no longer dereferences an absent scene-object menu when closing menus after text deletion. Resident chat-header context controllers also perform direct enable queries outside registrar admission; native scene zoom, radar tracking and radar-based teleport-to now return disabled before lazy radar construction. Manual menu setters cannot re-enable map, voice or those scene actions, and direct deferred scene callbacks report the existing unavailable notification before constructing scene/reporting owners. The same direct radar predicates in Contacts and name-list menus are guarded. Profile, IM, friendship, invitations, payments, mute, history, and teleport offer/request text protocols retain their reviewed shared routes.

Profile menu admission now includes exact existing account/image callback payloads, including Change Photo, while preserving the shared capability/loading gates and ordinary inventory texture picker/save controller. First Life image controls use their existing programmatic callbacks. Real inventory construction includes the drag-and-drop trash button, complete marketplace inbox inventory/folder/item types and their shared badge drawing. Inbox freshness IDs are saved and retired after their account inventory owners are destroyed. Toybox is a read-only drag-source palette and intentionally has no insertion caret; its shared draw no longer asks the factory for a dummy caret. Editable toolbars still require their actual skinned insertion caret.

Native account completion initializes the ordinary localized automatic-response defaults explicitly, preserving all user-edited account settings. Navigation and inventory already have explicit native completion paths; notification channels use their separately owned CPU completion/reset lifecycle. The legacy scene login signal is not broadcast indiscriminately. These implementation corrections and the actual context/menu/notification actions require the final staged runtime qualification currently in progress; no passing result for this source batch is implied here.

The live crash dump identifies an access violation in `send_agent_pause` at `llworld.cpp:1765`, reached from `LLWindowWin32::gatherInput` during native viewer input. Its legacy region iteration assumed a scene owner. Native pause/resume now pack the ordinary agent/session/serial fields and send through the current authenticated simulator host, without constructing `LLWorld`, selection, camera or avatar services. Window block/unblock events before login and after teardown also stay on the native route, and timeout resume remains balanced when no message system exists. This identifies the reported focus/modal-input crash; it does not imply that every possible IM failure has the same cause.

The expanded replay later exposed a separate notification-path crash: `LLHandlerUtil::logToNearbyChat` respected the saved Nearby Chat Console preference but dereferenced an absent `gConsole`. Native integration now requires the actual shared text-console owner, including ordinary local/IM/group formatting, line/background drawing and visible-session suppression. The UI draw scope avoids GL state construction under native capture. Account reset clears queued and drawn paragraphs and session suppression; deferred name callbacks check generation and weak console identity. Notification delivery still reaches the nearby history independently of optional console display. This source correction requires a new staged replay; it is not recorded here as passing.

The mounted attachment replay also exposed an omitted notification completion hook. Native `handleLoginComplete` returns before the legacy login signal; consequently the channel manager's startup gate remained closed and group notices were stored without visible toasts. Native completion now invokes the actual CPU notification lifecycle explicitly after account/chrome initialization, resetting account storage filenames and loading persistent/DND notifications. Account retirement destroys stored/visible toasts and resets that gate. The attachment assertion uses the actual mounted group-notice controller and native window input; it does not construct a detached substitute panel.

New replay assertions exercise actual inventory controls, bottom chat, window pause/resume, shared nearby Send and incoming/forwarded history, style preservation, and reversible toolbar customization with save/reload. Their presence is implementation, not passing evidence. Current runtime qualification is still open; construction, layout and actual menu actions must all complete in the same staged viewer. Earlier passing source revisions do not qualify these changes. These changes are not accepted until a completely staged runtime passes the new assertions, draw/readback checks, teardown/relogin and packaged-skin qualification. The historical results below qualify their recorded source revisions only.

## Shared services and UI integration implemented

| Area | Implementation and disposition |
| --- | --- |
| Login/account lifecycle | `vsnativesession.cpp`, `llstartup.cpp` and `llappviewer.cpp` own ordinary authentication, account initialization, native transport and lifecycle. Root integration is being qualified with the existing login button and XML-RPC/capability fixture, rather than bypassing login. |
| Direct/group IM transport | `vsnativeim.cpp` admits simulator-host and agent-target checked UDP IM into `LLIMProcessing` and the existing `LLIMMgr`/`LLIMModel`. `llimview.cpp` uses native region capability ownership for conference, invitations, start replies, participants, errors and server closure. Deferred HTTP/name/translation callbacks carry session-generation guards. |
| Conversation UI | Existing `FSFloaterIM`, `FSFloaterIMContainer`, `FSFloaterContacts`, group/contact lists, IM control panels, chat histories and chiclets remain the production skinned controls. Native routes now use the actual nearby-chat floater and shared toolbar chrome while avoiding scene-console ownership. Incoming and outgoing text uses the shared mute, typing, history, offline and autoresponse logic. |
| Offline and reset | `vsnativeim.cpp` requests offline IM once per active generation after mute-list readiness. `LLIMMgr::disconnectAllSessions()` clears shared sessions, timers, invitations, pending participant updates and conversation floaters. `vs_native_im_reset()` destroys profile/group/account dialogs before deleting caches containing their retained pointers. |
| Notifications | `llimprocessing.cpp` stamps account-owned friendship/group notifications; shared responders check freshness. `vsstartupui.cpp` admits the complete notification-list/chiclet/toast graph, not only its outer floater. Group notice parsing bounds binary buckets and preserves subject/body. |
| Contact/group/account data | `llgroupmgr.cpp` bridges native capabilities and guards suspended member/ban requests; account reset preserves registered observers while clearing group data. `llavatarpropertiesprocessor.cpp` guards profile HTTP/name work and resets account request state. `fsfloatercontacts.cpp` re-resolves buddy relationships after asynchronous name resolution and uses weak UI callbacks. `LLFloaterAvatarPicker` routes directory/UUID searches through native capabilities with enqueue-time generation guards; keyed picker instances retire at account reset. Its scene-dependent NearMe tab explicitly reports deferred world functionality. `LGGContactSets::resetAccount()` disconnects name lookups and clears retained account sets/aliases before relogin; contact-set and alias notification responses retain generation/weak-view guards. |
| Profile/group images | `LLProfileImageCtrl`, `LLFloaterProfileTexture`, `LLPanelProfileSecondLife`, `LLTextureCtrl` and `LLThumbnailCtrl` consume CPU/native `LLUIImage` facades. Remote assets load through production GetTexture; decoded dimensions and readiness come from the current generation. Profile refresh retires previous requests. Actual texture selection waits for decoded dimensions instead of accepting placeholder size. |
| Inventory offers and text assets | Native IM admission includes resident/task inventory offers and their replies. Existing LLOfferInfo and fetch/open/discard observers carry account-generation ownership; postponed name publication and stored responder restoration retain scope. Accepted offers use the real inventory model, destination-folder replies and production texture/notecard previews. Texture preview uses current-generation CPU images, dimensions, refresh and permission-checked PNG/TGA export. Notecards retain assetstorage load and agent update/save with guarded upload continuations. Scene previews remain deferred. |
| Inventory tree images | `llfolderviewitem.cpp` draws the same skin arrow and favorite images through UIImage. `LLUIImage::drawRotated` and `VSUIResources` publish rotation through the existing native packet affine transform. It does not extract a GL texture from native images. Inventory model/pump/HTTP operations are owned by the main native account integration. |
| Preferences | Existing Preferences panels and renderer selector are admitted. Native graphics controls requiring the deferred world renderer are disabled while OpenGL/Zink/Vulkan selection remains usable. Text translation, autoreplace, spellcheck/import, proxy, permissions, color picker and contact-set/blocklist dialogs use their existing controls. Color picking publishes its CPU HSL gradient and preserves palette/settings behavior. Scene pipette remains unavailable until world integration. |
| Group management dialogs | Existing group invite/member/bulk-ban panels are admitted. Native group actions consistently choose the existing standalone skinned group floater even when the saved OpenGL sidebar preference is disabled. Group invitation owner-confirmation and avatar-picker callbacks use weak panel handles. Unregistered invite/bulk-ban floaters are destroyed on account reset. Account land-contribution checks use native MoneyBalanceReply credit/commitment ownership instead of constructing a GL status bar. |
| Transcripts | Production conversation-log/list/item/preview types are admitted. History-load callbacks have scoped connections, and account previews/log dialogs are retired at reset. The normal account-specific transcript services still supply data. |

Contacts Group Titles now admits the existing skinned role/title list and retains actual GroupTitlesRequest/Reply and title/active-group actions; account reset destroys its observers before group caches. Native Group Search uses `VSGroupSearch` as a floater host for the existing `LLPanelDirGroups` controls, search history, pagination and real UDP directory results, with ordinary group Profile actions. It does not create a replacement protocol/model or expose an ID-only input. Current/named region assignment in the title floater still needs the deferred region/world owner, while role/title account actions remain wired. The connected replay captures actual GroupTitlesRequest and DirFindQuery account/transaction fields, supplies corresponding decoded title/directory replies, selects real rows and invokes Info/Profile controls. These additional routes still need construction, draw and protocol qualification.

Group account confirmations (join, leave and eject) now carry current-account ownership; paid join decisions recheck balance/group limits when answered. Deferred leave-group observers and snooze prompts expire across account reset. Group inspect/list URLs select the real native profile/Contacts routes instead of depending on saved legacy sidebar preferences. Native startup explicitly initializes the Emoji CPU dictionary; help and server URL requests use the browser route, while well/chiclet signals and asynchronous row-name lookups have scoped lifetimes.

Resident Pay now uses the authenticated native simulator host through the shared `give_money` packet path, with a known-balance requirement and the normal confirmation policy. Unknown or insufficient balance produces a packaged modal notification before any transfer is sent. Native inventory sharing uses the existing inventory model and skinned inventory controls; scene actions must report the deferred world functionality before constructing scene services. These integrations require actual control, encoded-request and stale-account replay evidence.

The existing image and sound upload dialogs retain their ordinary metadata, permissions, destination and upload controllers. Image preview publishes the decoded local pixels through native UIImage resources, including pan/zoom/clipping; avatar and sculpted previews remain deferred scene features. The authenticated owner requests NewFileAgentInventory, and balance refresh and legacy storage fallback use its simulator host. Deferred inventory confirmations, folder operations and file pickers retain weak view/account ownership and resolve current inventory objects by UUID. Account reset retires upload dialogs and progress popups. Qualification must observe actual J2C/Vorbis request bodies and server-returned inventory IDs, rather than infer success from construction.

## Qualification required before acceptance

The staged native viewer must complete ordinary login through actual skinned controls, including credential editing, consent/challenges/account setup and a visible connection failure/retry path. Every supported skin must construct and draw the connected controls without factory rejections, GL access traps or missing assets.

`vsnativeimreplay.cpp` exercises real contacts/conversation controls and encoded direct/group UDP messages, incoming offline text, typing, mute suppression, group start/participants, leave/reopen, invitations, start/event errors, server force-close and notification responses. It accepts actual inventory notifications and group attachments, checks encoded acceptance and folder targets, waits for HTTP-fetched inventory and the real texture preview, and opens the real profile texture preview and waits for decoded current-generation GetTexture publication. It exercises Preferences edit/apply/cancel across drawn frames, actual contact-set editing and blocklist unblocking, and waits for production transcript preview to display received offline text. It opens the actual resident Profile through its production action, waits for AgentProfile notes and edits/discards through the real editor. Group information receives encoded profile/member/role/role-member replies with genuine request IDs, draws its charter and selects an actual member row. These assertions require runtime execution before they count as evidence. The parent replay captures ordered native UI packets and deterministic readback. This fixture demonstrates exercised integration only; it does not certify all server behaviors or constitute live-grid credential qualification.

Runtime qualification must additionally cover production Preferences opening/apply/cancel, contact-set and blocklist editing, transcript previews, profile first/second-life image refresh/save, group roles/member selection/invitation/ban responses and logout/relogin while requests are pending. Unknown balance/credit must not authorize a paid operation. A failing factory, missing required capability/response handler or stale callback remains a concrete acceptance gap even if another replay stage passes.

Connected protocol dependencies include AgentProfile, UserInfo/AgentPreferences, ChatSessionRequest, GetTexture, AvatarPickerSearch/GetDisplayNames, UntrustedSimulatorMessage, GroupMemberData/GroupAPIv1 and experience capabilities where used. UDP group/profile pages need their actual property/member/role/title/notices/account/land and picks/classified/parcel reply handlers, plus corresponding admitted outbound requests. These services must be wired and qualified by the native owner; admitting their UI class alone is insufficient.

Profile pick/classified world-map and teleport actions show NativeWorldUnavailable before initiating scene services; native simulator migration uses the implemented CPU navigation owner and awaits the current integration replay qualification. World-map drawing, scene pipette, world graphics quality and material/scene previews are deferred world features. Their controls must not construct scene/GL services from the connected text UI. Deferring world drawing does not defer authentication, connected account ownership, messaging, notification responses, skins or ordinary text controls.

The connected control audit also ports slider/list/container texture-state preparation, debug borders and spellchecker diagonal strokes to explicit native primitives. Badge fallback preserves its original triangle strip geometry, root-space coordinate rounding and replacement blending; nested owner transforms are applied exactly once. Context-menu branches and badge factories are admitted after their rendering paths are supported, with rejected type identities recorded for diagnosis. Preferences Block List always uses the existing skinned standalone blocklist in native mode, independent of a legacy sidepanel preference. Preferences construction/apply avoids world-camera FOV access and disabled graphics preset initialization; its texture-preview flyout and private no-draw layout placeholders use exact type admission. These source changes still require the staged connected replay and pixel qualification; admission and a clean compile are not acceptance evidence.

The dummy-class failures exposed missing exact admission for the shared notification-list, context-menu branch, scroll-column header, flyout and accordion-header controls. Their native draw paths are implemented before admission; private header/layout types expose identity checks from their owning libraries. The unused texture-preview lookup for a nonexistent `buttons_panel` was removed, preserving the actual packaged `button_panel`. The contact-set configuration now binds the declared `OnlineOfflinetoNearbyChat` setting with its exact spelling. Dummy creation, rejected widgets and failed XUI setting bindings are decisive replay failures. Admitted group/profile/account replies also require their actual shared handlers; an unhandled server reply fails evidence assessment.

## Evidence and qualification limits

### Connected skin corrections

Nearby chat now loads its controls from the selected layered XUI rather than
constructing their rectangles in C++. The custom chat-input parameter block
uses the factory's ordinary inherited line-editor defaults; copying those
defaults during descriptor construction produced an inverted input rectangle.
Explicit top-left layout keeps the region heading, logout button, transcript
and input within the panel. Transcript colors use the existing chat palette.
The connected control strip also declares its layout and chiclet host in XUI.

Native skin images now preserve the OpenGL helpers' physical-pixel rounding for
image extents and inner nine-slice boundaries at fractional scaling. Native
widget strokes retain their requested fractional width and are centered on
their shared vertices; outline bands avoid double-compositing translucent
corners. Compiler-backed regressions cover these production geometry paths.
Driver-dependent OpenGL wide-line rasterization is not certified identical.

The nearby transcript uses the ordinary `simple_text_editor` factory tag,
retaining its previous `LLTextEditor` type and inherited skin defaults. The
viewer-specific `text_editor` tag selects a different type and is unnecessary
for this plain transcript. Vintage Item Properties now supplies its required
Export checkbox and the controller's exact sale-combo name and numeric values.

The connected replay separately requires positive, contained, nonoverlapping
chat/control geometry and records the actual rectangles. Pixel comparison alone
previously passed an incorrectly laid-out panel, so it cannot substitute for
this check. The standard skin at 125% now passes both checks and its captured
chat layout has been visually inspected. Recorded qualification below separates
the initial connected matrix, its corrected vintage rerun and final viewer checks.

Relogin no longer reuses function-static Preferences child pointers. Account
notifications remain expired after their owner disappears, and final native
shutdown is idempotent even when a suspended seed coroutine retains the owner.
These corrections address failures observed in the staged connected replay.

The SDL native preeditor boundary now cancels only an active composition when
focus changes or an empty editing event arrives. `resetPreedit()` can delete
ordinary selected text, so calling it without a composition erased the selected
login username on Linux. Actual committed text still replaces selection, and
new composition still replaces the selected range. Compiler-backed tests cover
focus switching, disable, empty cancellation, active cancellation and commit;
actual Linux window-event evidence remains separate.

The XUI fixture now checks replacement blending against known RGBA pixels and
the independent oracle's alpha, rather than assuming every target pixel stays
opaque. RGB and alpha retain the existing tolerance of three. Actual and
expected alpha PGM captures are required alongside the RGB PPM captures. The
unsupported-owner probe uses world map because Preferences is now supported.

Notification replay responses assign the selected existing button explicitly; `LLSD::with()` only inserts absent keys and leaves the response template's undefined values unchanged. Persisted DND restoration likewise assigns its restore flag explicitly. Payment qualification requires the actual confirmation callback and encoded transfer, including balance and retired-window rejection.

The native connected frame now pumps the shared CPU idle callbacks and buddy observers. Inventory views depend on those callbacks for initial folder construction and filtering; creating their panels without running this service leaves the actual selectors unusable. Native UI audio initializes a neutral listener and pumps the existing audio engine without constructing a world camera.

The native session consumes the ordinary Second Life login benefit packages and authenticated OpenSim economy replies. Account reset clears current/package benefits and economy prices; malformed or missing benefits retain unknown costs and block affected transactions while the text login remains available. The replay requires supplied login benefits or an actual encoded economy request/reply before upload, and preserves rejection of stale confirmations and unknown balances.

Inventory Sound, both existing Properties preference routes, thumbnail editing and the inventory Script editor use their existing skinned controls. Inventory scripts load through the authenticated asset service and save through `UpdateScriptAgent`; file-picker, asset, upload and modal callbacks are scoped to their originating window and account. Script task/world controls remain outside this text-only stage. The staged replay must observe actual editing and encoded metadata/source uploads before these paths can be reported as qualified.

Maintain build revision, staged runtime/assets, selected software Vulkan device, validation logs, fixture protocol results, UI packet/readback comparison and failure captures with each qualification run. Software-device CI proves the implemented Vulkan path on those devices; no AMD/NVIDIA physical host is assumed available. Vendor hardware behavior and live-grid interoperability remain separately identified qualification limits.

### Historical local Windows qualification before the current parity integration

The fully staged RelWithDebInfo viewer uses the Autobuild-installed GHI libraries
and pinned SwiftShader runtime. The initial connected matrix completed 26 cases:
25 passed, and vintage failed the decisive dummy-control check. After correcting
its Item Properties XUI, the complete vintage replay passed. The cases cover all
24 packaged skin/theme/language selections plus the standard Vulkanstorm skin at
125% and 150% UI scale. Initial failures remain preserved as evidence.

The final transcript tag and RGBA check were then qualified by all 68 staged
viewer runtime cases, which passed. These include every packaged skin in the
XUI and startup probes, actual OS Login/Return input, dialogs, presentation,
failure injection, normal login startup and shutdown. The standard-skin alpha
capture matches the independent oracle exactly, including replacement alpha 128.
The final executable's connected replay at 150% also passed all 76 required
assertions, including local chat, direct/group IM, account controls, logout and
relogin. Maximum RGB difference in its nearby-chat readback is one, within the
unchanged tolerance of three. Its captured layout was visually inspected.

Source is pinned to `8dd932e856ea1d50e27023f8f0041c66d40c0cfd`.
Local evidence is retained under `.tmp/connected/skin-matrix63`, `vintage65`,
`viewer67` and `replay67`; the staged executable and source/hash manifest remain
under `build-vcloginfix-local` and `.tmp/connected/build67-manifest.json`.
All 24 viewer source regressions, four compiler-backed UI/oracle regressions,
11 session-runner regressions and 21 documentation checks pass.

Actual Linux SDL runtime confirmation is pending the final dedicated CI run.
The compiled production-source regression demonstrates the selection-preserving
fix, but does not substitute for SDL/X11 window-event execution. This evidence
does not qualify live-grid interoperability, physical GPU behavior or world
rendering, nor claim identical layout to the legacy rich nearby-chat floater.


### Background UI qualification (11 October 2026)

All Windows Vulkan test executables are now launched by
`scripts/vulkan_viewer_process.py` on a unique private window station and
never-activated desktop. This isolates windows, focus and clipboard from the
working desktop. A kill-on-close Job Object contains descendants; bounded
execution captures combined output and kills the process tree on timeout.
Setup failure is decisive: there is no visible-desktop fallback. Test result
JSON records the window station and desktop identifiers. Diagnostics no longer
move or query the physical pointer to qualify injected input.

Linux CI continues to launch UI cases under Xvfb. The dedicated Windows CI
job first verifies actual isolation, unchanged active desktop/focus/clipboard,
output capture and timeout cleanup. The local Windows account cannot create a
private window station (WinError 5), so local UI execution stops before creating
a viewer; the final isolated runtime matrix requires CI evidence.

The native hover fix shares OpenGL's hover-enter/leave controller, including
capture and stationary-pointer layout changes, and delivers pending Win32
motion before window leave without replaying it afterward. Skin assets and
button colors remain unchanged. Earlier source `5edef0916ac20ca6f7bb3191eb72abd340e05f47`
proved the hover-lifecycle assertion for all 24 startup skin selections. That
run did not pass its entire diagnostic matrix; it is not qualification of the
final isolated executable. Previous connected-session evidence is retained
with its original source/image identity. Final runtime and live-account
acceptance must remain separate from source and build checks.

## Remaining connected control dispositions (current source audit)

Statistics, Lag Meter and status FPS are implemented CPU services, rather than disabled scene controls. Their current replay opens the actual floaters, delivers a trusted encoded simulator statistic, checks recorded frame data, draws the views and commits the real minimize/restore control. Passing this newly extended replay is still required.

Direct/group voice calls remain a genuine integration gap: the text-session path does not establish the voice account/capability and call lifecycle. This is distinct from the intentionally omitted spatial nearby voice monitor, whose speaker ownership depends on the deferred world. Scene rendering, radar, spatial inspectors, camera/build tools and scene performance reports remain intentionally unavailable. Allowlisted account actions are not, by themselves, evidence of runtime parity; current-skin draw, input, account reset and delayed-callback qualification remain separate.

The command/controller census identified four CPU route omissions, now wired to their existing controllers: `settings_debug` / `Advanced.ShowDebugSettings`, `webbrowser` / `ToggleWebBrowser`, `defaultchatbar` / `MainChatbarVisible`, and Guidebook/`howto` plus Help Browser. Exact widget/name/command/callback admission preserves their ordinary XUI; account reset retires the browser and debug controllers. Help honors the existing external-browser preference, with native embedded help otherwise using the shared media frontend. Debug Settings inspects all settings; edits, defaults and sanity resets invoke only settings whose CPU observers are already approved by the native setting policy. Scene-owned settings remain read-only until their services exist. Scoped selected-setting and filter connections prevent callbacks into a retired floater. An extracted-production compiler fixture verifies the native write boundary and unrestricted legacy behavior. Runtime construction, browser interaction and toolbar actions for these new routes remain pending; source admission alone does not qualify them.

## Connected top-menu row correction

Native connected-menu startup previously arranged branch items before assigning the final menu-holder height. `setRect` changed the owner rectangle without rearranging its children, leaving hover tests and highlight fills bound to shorter rectangles. `fit_native_menu_bar` now requests and completes arrangement after the final owner placement, before input. The production-extracted compiler fixture reproduces the upper-half miss and checks upper/lower coverage, highlight-height geometry, nominal widths and hidden items across several row heights. Runtime OS hover/click qualification for this correction remains pending.

## Account utility and emoji census closure

The source survey found actual account UI omissions beyond the connected chat
fixtures. Inventory Settings was excluded by native admission, and its shared
controller requested an `ok_btn` absent from the default XUI. Its actual floater
is now admitted; an optional skin OK button uses `findChild`, while the ordinary
floater close control remains available. Account retirement destroys the owner
and preserves its global inventory behavior settings and favorite color.

Events now use the shared notifier and browser floater, authenticated
`EventInfoReply`, login reminder loading and connected timer updates. Local
account retirement clears reminders without sending unsubscribe requests for
the retiring account. Duplicate details replace the owned reminder, and delayed
notification responses cannot open a floater for a different login.

Experiences now admit the existing list, search, profile, permission and log UI.
Account capabilities replace null legacy-region checks. The native seed includes
the search, admin, creator, acquisition, permission and update capabilities used
by those controllers. An optional policy in the shared experience cache captures
the originating account both before queued requests execute and before their
results dispatch. Native profile/list callbacks also retain weak UI ownership;
retirement removes permission listeners and persists/retires the account log.
The legacy experience abuse reporter still requires its scene-dependent report
owner and is disabled in this text-only stage; enabling the account graph does
not qualify scene reporting.

New Pick and Set Location routes use the current native region and parcel name,
including Classified location metadata. They clear a legacy scene parcel UUID
rather than borrowing stale scene ownership. Picks created through the ordinary
URL controller no longer silently return because `gAgent.getRegion()` is null.

Emoji prediction and explicit selection previously requested the unadmitted
`emoji_picker`, so the shared helper returned without presenting a chooser. The
actual picker and its exact CPU grid/divider/icon/preview/list types are now
admitted. It uses the existing dictionary, fonts, keyboard/mouse selection,
shortcode replacement and chat editor callbacks. Account retirement disconnects
the helper from its editor, destroys the picker, saves the retiring account's
recent/frequent emoji state and clears its cached filename and lists.

Five compiler fixtures execute the production inventory-settings methods,
notifier ownership/responder methods, experience request/result guards, emoji
prediction/editor/picker commit and retirement methods, and native profile
location controllers. These pass without a graphics or audio device. They do
not substitute for staged native drawing, OS input, browser execution,
authenticated live-service interoperability or cross-account runtime evidence
for this new batch. The previously documented voice and scene prerequisites
remain separate.

## Additional connected CPU routes and reported UI corrections (11 October 2026)

The survey found real routes omitted by native admission, rather than controls
that correctly wait for world rendering. Marketplace storefront, Grid Status,
Destination Guide and standalone Teleport History now use their existing
controllers, selected-skin XUI and toolbar/menu commands. Destination Guide
keeps its authenticated login/configured URL; native startup no longer replaces
it with an empty region-feature URL. Grid Status uses the existing Second Life
default and any available advertised grid URL. A grid without a supplied status
URL receives an explicit unavailable-service message. OpenSim SimulatorFeatures
status discovery and periodic RSS polling are not qualified by this browser
route.

Teleport History now records native locations in the ordinary persistent model
before a floater is opened, and saves/retires that account's storage and scoped
observer before relogin. List/filter, timezone, expansion, copy, remove and clear
use the real frontend. Deferred confirmation checks account freshness and the
exact originally selected entry, so removal/reindexing cannot teleport to a
different item. Asynchronous SLURL copies retain only a weak panel and account
guard. Confirmed current-grid navigation uses the native session service;
foreign-grid entries require a login to that grid. Map and Place Details retain
visible disabled actions plus guarded direct callbacks because those owners
require the deferred scene. Globally parented menus retire with their account
panel instead of retaining its callbacks.

The native simulator path now installs the actual shared ScriptQuestion,
ScriptDialog and LoadURL handlers. Replies and asynchronous experience/name
resolution retain originating-account and simulator-host ownership. Ordinary
AlertMessage and AgentAlertMessage use the existing named notification templates,
substitutions and Modal flag instead of flattening all alerts into ErrorMessage.
Native guards omit snapshot/parcel-collision and scene restart-floater side
effects; existing restart toasts remain available. Events, Experiences, seller
Marketplace and ordinary link replacement are integrated in the accompanying
account-service changes, with exact frontend admission rather than a blanket
floater allowlist. Emoji Picker and its reviewed internal font/panel widgets
are also admitted; account ownership and selection callbacks are handled by the
shared picker integration.

Two independent chiclet problems were identified in production code. Native
startup did not assign the actual chiclet container to the notification-channel
owner, so toast bounds ignored the bar. The channel now follows the visible
selected top/bottom host, including live placement changes. Native toolbar
placement also subtracted the navigation height from its parent origin even
though panel_toolbar_view already reserves navigation space. It now begins
immediately below the selected-skin status row, matching the ordinary
main_view.xml toolbar_view_holder. This removes the second navigation offset
that lowered the chiclets. Logout uses the existing fullscreen/mini progress
preference and localized LoggingOut text; the progress surface is drawn above
connected popups and hides when logout completes.

Production-extracted console regressions cover stale/reindexed history
confirmation, account storage teardown ordering, grid-status URL selection,
notification host/account rejection, actual toast bounds, logout progress policy
and toolbar/chiclet parent coordinates. These establish the tested source
contracts; they do not establish all-skin runtime, browser content, live-grid
notifications or visual parity for this new batch. Staged viewer/runtime
qualification remains pending the rebuilt image.

Remaining concrete mixed-service utilities are distinguished from gray scene
controls: Asset Blacklist's list/filter/persistence and sound audition are CPU
operations, but its temporary-derender reset and removal interact with the scene
object/derender list. Its frontend is not admitted by this batch. Sound Explorer
contains sound-history browsing/local audition, while Look At moves the world
camera and resolves LLViewerObjects. Its region sound-source feed is absent
from the native text transport; admitting the floater alone would not supply
those data or actions. These mixed utilities therefore remain separate porting
gaps, not evidence that their entire CPU functionality is inherently world-only.
Voice call setup remains a non-world integration gap outside the accepted
text-only call lifecycle. No full connected-UI parity claim follows from this
survey or admission policy.

## Local qualification for the 11 October survey

Source revision `db15b81419a06b8547e72b3a4509892d98e7051f` was rebuilt in the
canonical `build-vc170-64` directory as RelWithDebInfo, without an installer.
`scripts/check_vulkan_ci_staging.py build-vc170-64` passed complete runtime,
plugin and asset staging. Fifty diagnostic/controller compiler tests, the
focused account/emoji/profile/media/app-link regressions, twenty-one session
replay tests and twenty-two documentation tests passed. The insertion catalog
accepts source coverage and explicitly keeps runtime parity unqualified.

No foreground UI was launched. This batch has no new live-account, graphics
readback, all-skin screenshot, external media or vendor-driver runtime evidence.
The staged executable is available for user qualification; pending runtime
checks and the concrete residual services above remain open.


## First-world rendering admission reconciliation (17 October 2026)

The text-only stage is no longer a valid blanket reason to disable connected UI.
Native construction remains a closed set because the native scene is not an
LLWorld/LLViewerObject scene. First geometry does not make controllers that
explicitly depend on those reference owners safe to construct. Each remaining
restriction must be justified by its actual missing service and ported to the
native equivalent; it is an implementation gap, not completed UI parity.

This checkpoint restores the existing Animation Overrider floater, its toolbar
command, selected-skin XUI, full/compact controls, set selection and activation.
The actual AO engine, inventory link operations and animation transport were
already integrated in R2; their UI admission was obsolete. No replacement AO
frontend or animation engine is introduced. The editor retires before account
sets are destroyed, owns its engine signal connections, and uses weak floater
handles for asynchronous set creation and confirmation. Removal confirmation
also checks the original inventory set identity and current selection.

The Online Status menu now admits the existing Do Not Disturb, autorespond,
non-friend autorespond, reject teleport offers, reject friendship requests and
reject group invitations controllers and state checks. The ad-hoc conference
settings use their ordinary saved controls. These are shared account policies,
not graphics capabilities; their old exclusion was unjustified.

The connected replay constructs/draws the actual AO editor in both layouts and
commits each of the six status menu items twice, requiring the actual LLAgent
state to toggle and restore. The R2 replay selects and activates its real
inventory-backed AO set through the shared editor controls before verifying
simulator animation transport. These run within existing headless replays and
skin selection; no foreground test windows are introduced.

Concrete remaining integration dependencies found during this pass:

| UI route | Missing production integration, rather than a text-only exemption |
| --- | --- |
| Movement controls | Shared joystick drawing still constructs GL state; button movement/yaw and capture release must reach the native movement/camera owner. |
| Camera controls | Joysticks and presets target LLAgentCamera, whereas native presentation uses VSNativeCamera; both actions and displayed state need transposition. |
| Away status | LLAgent::setAFK rejects a connected native session because it requires LLViewerRegion; native account/region readiness and sit-on-away need integration. |
| World map, minimap, Place Details and history/profile map actions | Map/parcel controllers require their own data and presentation owners; rendered terrain alone does not supply them. |
| Nearby People/radar | Spatial list/model feed and native object/avatar identity resolution must replace LLWorld lookups. |
| Build/edit, object and land context menus | Picking, selection, permission and parcel services must resolve the native scene, not uninitialized reference globals. |
| Asset Blacklist and Sound Explorer | Mixed CPU browsing/persistence/audition and native scene derender/source/camera operations still require integration. |
| Gestures frontend/editor | Engine and wire path exist; frontend admission, save capability routing and account-scoped asynchronous callbacks remain to be integrated. |
| Environment/graphics controls | Controls must target implemented native render state; enabling a GL-only shader/pipeline callback is not integration. |

This list records concrete observed dependencies, not an exhaustive declaration
that every remaining UI route has been qualified. No full connected-UI parity
claim follows from first-world rendering or this checkpoint.
