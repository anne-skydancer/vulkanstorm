# Connected native viewer UI gap audit

The minimum deliverable is the real skinnable connected viewer UI with ordinary login, local chat, direct IM and group IM. World drawing remains deferred. A constructed test panel or nearby-chat-only replay does not establish that acceptance.

This inventory describes implementation and outstanding qualification separately. It does not declare the deliverable accepted or claim that any pending build/runtime check has passed.

## Connected chrome and nearby frontend: current integration

The user's live login establishes that ordinary credentials can connect the native viewer. It also exposed missing address/favorites controls, toolbar placement and pointer behavior, plus a crash during IM interaction. Those reports prevent treating the earlier diagnostic replay as connected OpenGL UI parity.

The current source uses the shared `LLNavigationBar`, location input, favorites and navigation history, `LLStatusBar`, main menu and `LLToolBarView` rather than a second chrome layout. Account toolbar positions, display modes, alignment and layout use the existing `toolbars.xml` serializer. The real Toybox configurator and command drag/drop operate through UI mouse capture and shared two-dimensional drop targets; they do not select world tools. Restore/clear confirmations retain account-generation ownership. Native OS input/cursor handling belongs to the viewer window integration.

The shared Contacts tab content has eight additional UI pixels of right inset in the default, Modern and Vintage layouts. Right-following buttons retain their dimensions and move with the tab content; localized overlays inherit the change. This XUI correction applies to both renderer backends.

Normal connected chat now opens the actual skinned `FSFloaterNearbyChat`, including its `FSChatHistory`, chat entry, volume/channel controls, history/search, rich/plain styles and existing emoji/mention controls. `VSPlainChat::useSharedFrontend(true)` selects that frontend and focuses its real editor. Typed local-chat and IM/group-IM metadata is forwarded into the shared history; the diagnostic transcript remains available for protocol/readback fixtures. Shared chat transformations and RLV policy precede native session transport. Account reset retires nearby chat, text search, inventory and Toybox controllers and their scoped settings callbacks. Logout immediately hides the shared nearby frontend. Nearby log policy remains account-owned and avoids duplicate logging.

Inventory uses the actual `LLFloaterSidePanelContainer` / `LLSidepanelInventory` / `LLPanelMainInventory` graph, including the inventory tabs, search/filter/sort, folder views and reviewed account actions. The standard bottom-toolbar chat entry is `FSNearbyChatVoiceControl`; its native text route does not create the spatial voice-monitor owner. The People/Contacts command uses the existing skinned account frontend and reviewed contact/group actions. Scene-dependent radar/minimap functionality remains outside this text-only stage.

Context menus use the same XUI items and production registrars as those frontends. Reviewed resident/account URL actions pass the ordinary command trust rules; scene-dependent verbs stay excluded. Rich object-chat headers derive their location from native session metadata and open the actual CPU remote-object inspector, avoiding `LLWorld`. Block List opens the native account floater regardless of the legacy sidebar preference. Gallery actions reject retired account owners and empty selections; delayed RenameItem responses carry account-generation stamps. Replay assertions commit actual nearby/direct/group options, resident/object header menus and inventory right-click Properties. These assertions still require execution in the staged viewer.

The connected native root and window use the same rounded physical-to-logical extent at fractional UI scale. A previous truncated root retained a one-pixel discrepancy in follows-based chrome layout after resizing. Status has its own root-owned container: its height comes from the selected layered `main_view.xml` metadata, without constructing the scene-bearing main-view graph. Menus retain their separate height; navigation and toolbars reserve the actual status row. Geometry qualification checks the status text against its selected XUI parent, top offset and height. The existing time and balance text deliberately overhang the row by one and three pixels respectively in the standard skin; source-derived witnesses preserve this OpenGL layout instead of changing XUI or applying a general bounds tolerance. Native status FPS displays unknown rather than reading an uninitialized scene frame recorder; empty statistic recordings are not queried.

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
