# Connected service audit

This source audit follows the native connected text UI integration. UI admission, a registered callback, a working CPU service and account-safe asynchronous delivery are separate requirements. No live account, external media URL or foreground viewer was used for this audit. The console regressions below establish controller behavior; they do not establish live grid or plugin playback acceptance.

## Routes closed in this batch

`LLCommandDispatcher::isNativeConnectedCommand` now admits exact existing CPU handlers for help, debug-setting help, search, keybinding preferences, web profiles, balance requests, bounded positive-channel chat, inventory show/filter/select, object message inspectors, inventory sharing, self-profile aliases, avatar teleport offers and existing pick editing. UUID routes reject malformed/null IDs and unsupported verbs. The dispatcher still requires the connected native phase and passes browser identity, navigation type and trusted-source status to the ordinary registry. Handler click-only, untrusted blocking and throttling policies remain in effect.

`VSNativeSession` decodes current parcel loop, autoscale and media dimensions, rejects invalid fields and dimensions outside 0â€“4096 before replacing playback, retains finite seeks and pause/loop state through consent and plugin startup, and clears pending operations on stop, account retirement or region replacement. The current `LLMediaCtrl` uses the MIME-selected plugin; a running time-based plugin receives pause/start/loop/seek rather than a replacement browser. Fixed dimensions apply when autoscale is disabled; automatic sizing follows the ordinary media control. Identical metadata does not restart playback. HTTP(S) remains the accepted scheme set; this does not establish support for every VLC transport.

The real Nearby Media panel supports the current parcel entry, selected play/stop/pause/mute/volume, enable/disable and copy URL. Native refresh omits the scene priority list, object proximity and media-focus owners. Firestorm's existing music/media separation is preserved: the parcel music toggle and volume controls remain in the status bar, while the Nearby Media panel handles parcel media. Object-face media, zoom and unzoom are deferred. UI admission and runtime qualification of this panel are tracked separately by the shared UI owner.

`test_native_app_url_routing.py` compiles the actual predicate and dispatcher and checks CPU routes, invalid arguments, teardown/login phases and unchanged trust/navigation/browser forwarding. `test_native_parcel_playback.py` compiles actual native controller transitions, including pending plugin startup, looping, finite seek, pause without closing, script/user stop precedence, malformed metadata and teardown; panel contracts prohibit scene-owner calls in the native branches. `test_native_bridge_retirement.py` compiles actual bridge retirement and availability logic and checks callback guards. These tests fetch no media and open no viewer.

## Remaining service census

| Area | Concrete dependency or missing route | Required integration |
| --- | --- | --- |
| Firestorm LSL bridge | `FSLSLBridge::initBridge/startCreation/processAttach/inventoryChanged/setupBridgePrim`, `FSLSLBridgeScriptCallback::fire` depend on self-avatar attachment ownership, joint attachment point, `LLViewerObject` task inventory and `LLViewerRegion` upload/local-ID ownership. | Implement the authenticated CPU attachment/task-inventory adapter described below. Endpoint retirement alone is not bridge acceptance. |
| Current-location landmarks | Native `LLLocationInputCtrl` and `LLStatusBar` explicitly disable new landmark actions. Existing favorites are wired; native pick creation has now received a CPU location path in the concurrent batch. | Carry current region UUID, parcel UUID/description/snapshot and global position into CPU landmark asset creation, using existing authenticated inventory services. This metadata prerequisite does not require drawing a world. |
| Parcel information and land transactions | Native status-bar information/buy-land actions return unavailable; location icons conceal unknown parcel flags. Parcel playback metadata currently does not provide a full land-management owner. | A CPU parcel model and authenticated parcel properties/access/ownership/transaction services; do not fabricate permission icons or construct the legacy world parcel manager. |
| Events — source integration closed, runtime pending | Concurrent integration now supplies `EventInfoReply`, reminder login load and periodic `gEventNotifier.update`, with actual event controllers and account retirement. | Qualify the encoded reply, reminder notification and exact positive-ID/details/notify URL routes in the final build. |
| Experiences — source integration closed, runtime pending | Concurrent integration ports the real experience controllers, capability ownership and deferred profile callback. | Qualify actual profile widgets and exact UUID/profile app route in the final build. |
| Classified app URLs | `LLClassifiedHandler::about` retains a global request set/verb and asynchronously opens a separate classified floater. The existing profile frontend has separate reviewed paths. | Account-safe async request/observer ownership and exact floater admission; do not blanket-admit the app handler. Creation also needs location metadata. |
| Script permissions and URL prompts — source integration closed, runtime pending | Concurrent integration now admits and registers real `ScriptQuestion`, `ScriptDialog` and `LoadURL` handlers. | Qualify encoded permission requests, mute/antispam, trust/confirmation and retired notification rejection; widget presence alone is insufficient. |
| Structured simulator alerts — source integration closed, runtime pending | Concurrent integration routes real structured `AlertMessage/AgentAlertMessage` handlers instead of flattening all incoming alerts. | Qualify structured payload/modal behavior and account-stamped callbacks while scene collision handling remains deferred. |
| Voice | Direct/group/spatial voice depends on voice session/backend lifecycle, capture/playback and speaker services; widget admission does not initialize these. | Separate authenticated voice service integration. The actual native text chat transport is already distinct. |
| Currency purchase | `LLStatusBar::onClickBuyCurrency` still returns native unavailable. Balance requests and money transfer are separate implemented CPU services. | Review HTML purchase/authentication frontend or legacy purchase floater and exact current-grid service ownership; no world-rendering requirement. |
| Navigation utilities — source integration closed, runtime pending | Marketplace/Grid Status/Destination Guide/Teleport History now have real CPU routes. Native simulator-feature discovery is added through the capability service instead of requiring a nonexistent `LLViewerRegion`. | Qualify configured/login and grid-advertised URLs, history/account cleanup and actual controllers in the final build. |
| Arbitrary app/floater and grid-management commands | `openfloater`, connected grid changes and unreviewed generic utility app routes remain excluded. | Exact registered CPU owner/parameter review and account transition rules. Blanket opening would bypass the constructor/lifetime boundary. |
| World controls | Map rendering, camera/lighting presets, avatar appearance/AO, movement, object manipulation, parcel-surface/object-face media and radar require additional world/scene owners. | Implement the relevant world services/rendering roadmap stage; do not label these ordinary text-account callbacks. Some can later share a CPU model with the bridge. |

Rows marked source integration closed reflect the concurrent final source batch and remain subject to final build/replay evidence. Bridge, new landmarks/full parcel management, voice, currency purchase and world-dependent features remain distinct open services; they are not established by the passing controller fixtures.

## Firestorm bridge blocker and safe retirement

The bridge is an in-world scripted attachment, not an account web endpoint. `FSLSLBridge` inherits `LLVOInventoryListener`. `startCreation` explicitly rejects an invalid agent avatar. `processAttach` identifies the self avatar and the `Center 2` attachment, resolves the attachment inventory item and requests the actual object's task inventory. `setupBridgePrim` changes volume/shape/position and sends `MultipleObjectUpdate` using the region's host and object local ID. `FSLSLBridgeScriptCallback::fire` finds the worn attachment and uploads its script through `UpdateScriptTask`. Creation/reattachment also use `LLAttachmentsMgr` and `LLVOAvatarSelf`.

The native text transport currently has no authenticated self-avatar/attachment identity graph and rejects the object-update family needed to establish one. Inventory fetching alone cannot prove that an inventory item is the active bridge attachment. A `<bridgeURL>` chat handshake alone cannot replace that proof: the historical folder authorization string plus object-owner ID is insufficient to demonstrate attachment point, task inventory, script ownership and current region lifecycle.

The necessary adapter must carry current-account/current-region self-avatar and attachment UUID/local-ID/point/inventory-item relationships from bounded object updates; handle removal/region replacement; issue and validate `RequestTaskInventory`/`ReplyTaskInventory` and their transfers; support real attachment rez/detach requests; upload/update the bridge script through the correct task capability; and accept the bridge handshake only from that proven owned attachment. HTTP completions, inventory observers, creation timers and settings integrations must retain generation/region stamps. OpenSim's existing inability to assume the library rock must remain explicit. World drawing may stay disabled while this CPU model operates, but the model must actually exist.

This batch retires the old bridge endpoint, raw inventory reference, authorization/attachment IDs, detachable IDs and creation/timer state on native account reset and region replacement. Legacy bridge availability, handshake, scene creation and late attachment/script callbacks are guarded while a native text session is active. Ordinary OpenGL behavior remains available. **The actual Firestorm bridge remains unimplemented in native mode; these guards and retirement tests do not satisfy its acceptance criterion.**

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
