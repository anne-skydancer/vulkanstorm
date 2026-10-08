# Milestone 1: item 1 source and boundary acceptance

Item 1 is: reconcile the catalog with current source; identify initialization,
callback and cleanup routes reachable by minimal UI/chat; distinguish retained
CPU services, native Vulkan replacements and deferred graphics facilities.
Its exit condition is current evidence passing its checks, with an explicit
gate and accounted-for callers for every deferred path.

**Accepted for source analysis and implementation planning**, subject to the
scope and verification below. Gates in this document are implementation
requirements, not claims that the current viewer enforces them. The current
viewer still initializes OpenGL and world facilities. Item 1 does not accept an
executable, a Vulkan build, rendered pixels, a live login, or hardware support.
Those remain later implementation and qualification work.

The source baseline is `48dcecfd18de92e01a23c70a22497ce85e0acd3d`.
The [insertion ledger](diligent-insertion-catalog.md) supplies broad source
coverage; [the boundary manifest](milestone1-boundaries.json) adds exact source
hashes, startup states, message registrations, settings registrations and
floater registrations. Together they cover the existing renderer inventory and
the admission routes for the smaller executable slice. Corrected progress-panel
XUI is the accepted baseline. Identical XUI with those fixes is successful.

## Configuration and retained behavior

Current-source refresh on 8 October 2026: the catalog and manifest are pinned
to `dcb74d7a2339da44e28d88c8de55f019dd578f08`. A separate native viewer diagnostic
branches before normal bootstrap and exercises window/device/presentation and
owned cleanup. Its [qualification record](viewer-native-presentation.md) is
separate from this UI/chat acceptance. Changed source/ranges were reviewed;
73 source hashes now include the diagnostic, platform/application interfaces,
native UI resources, CPU-only font backing, real image/font publication and
pre-construction factory gates. The required XUI admission sets and input
integration remain open; the gates exercised in fixtures do not close G-UI.
The complete UI/chat gate set remains unimplemented and this manifest retains
`gates_implemented=false`. The earlier baseline/counts below are historical
source acceptance, not current native UI/chat or world execution evidence.

Implement on `vkstorm-vulkan`, with the pinned DiligentCore Vulkan backend.
Vulkan is a peer backend. Failed initialization or device recovery terminates
the Vulkan session with a useful error; there is no OpenGL fallback. GL/Zink
remains the separate GL backend for AMD OpenGL ICD regressions.

The initial platform slice is Windows x64 native HWND and Linux SDL2/X11.
SDL1, macOS, headless GL-window reuse, Wayland and proprietary pathfinding
are outside this slice. Their build and platform branches remain in the wider
catalog, with explicit exclusion at configuration/admission. Qualification must
name the actual compiled configuration; this scope does not claim all Linux
window systems are supported.

Retain authentication, grid discovery, secure credentials, mandatory agreements,
agent/session IDs, host/region metadata, seed capabilities, UDP circuit setup,
movement-complete readiness, network timeouts, acknowledgements, throttling,
name lookup, mute/filter services, nearby-chat send/receive, local history,
focus/IME/clipboard, native UI assets, status/errors, reliable logout and CPU
settings/history persistence. Chat uses a plain transcript initially, selected
by mode rather than overwriting the user's saved rich-history preference.
No world-space bubbles, typing animations, avatar thumbnails or scene picking
are necessary to communicate in nearby chat.

The policy is a **closed admission set** for this mode. Unlisted floaters,
widgets, settings side effects and protocol/application actions cannot create
optional services. This is necessary because a virtual draw inventory cannot
resolve every dynamically registered constructor. Unknown input gets a bounded
unsupported/error outcome, not permission to enter the old renderer.

## Source route graph and required cuts

Each row names an actual caller chain, its required retained behavior and the
cut before graphics or optional construction. The gate IDs are specified below.
Source symbol anchors and hashes are checked by the boundary checker.

| Route | Existing callers and callees | Milestone disposition |
|---|---|---|
| R01 process/native startup | `LLAppViewer::init` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `selectGLBackend`, `initWindow` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `LLViewerWindow` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `LLWindowManager::createWindow` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ Win32/SDL2 context creation | G-BACKEND replaces selection/window/context/feature identity before GL loading, probes, shaders, fonts, buffers or shared-context workers. Keep native events, handle, metrics and input. The existing `use_gl=false` headless route is unsuitable. SDL2 currently ignores that argument. |
| R02 renderer/UI bootstrap | Window constructor ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ shader manager, font/image/buffer initialization ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `initBase`, `initGLDefaults`, `initWorldUI` | G-UI permits only the minimal login/status/chat widget closure. G-RESOURCE replaces UI image/font/buffer/shader ownership. G-WORLD blocks pipeline, sky, bump, partition/tool/avatar setup. Do not call the bulk world-UI bootstrap and hope hidden widgets stay idle. |
| R03 login/startup | `LLAppViewer::idle` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `idle_startup` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ all `EStartupState` branches; `do_startup_frame`, `pump_idle_startup_network` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `display_startup` | Keep authentication transitions, deadlines and message draining. G-STARTUP splits mixed state bodies and removes visual waits. Replace startup draw independently of the network pump. `gTextureList.updateImages` is currently unconditional and must become an owned UI-asset pump. |
| R04 region/session establishment | WORLD_INIT ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `LLWorld::addRegion` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `LLViewerRegion` constructor; `process_region_handshake` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `unpackRegionHandshake`; AGENT_SEND ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `CompleteAgentMovement` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `process_agent_movement_complete` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `gAgent.setRegion` | G-SESSION creates CPU region/session state without land/composition/parcel graphics, partitions, water objects, sky or probes. Preserve validated identity/host/origin/capabilities, handshake reply, circuit ack and movement readiness. Advertise only implemented handshake capabilities. Gate scene shifting/camera/avatar effects inside region changes, not merely the final draw. |
| R05 startup/network dispatch | `do_startup_frame`, `pump_idle_startup_network`, `idleNetwork` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ message checker/acks ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ startup handler registry; seed cap ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `LLEventPollImpl` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ queued/direct `handleMessage` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `LLMessageSystem::dispatch` | G-PROTOCOL applies to both UDP and HTTP/application dispatch. Retain transport decode, ack, retry and disconnect semantics. Gate semantic handlers before optional singletons or world object creation. Late queued work checks session generation at execution. |
| R06 incoming chat | `ChatFromSimulator` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `process_chat_from_simulator` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ filters/name/translation/history ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `FSFloaterNearbyChat::addMessage` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `LLChatHistory::appendMessage` | Keep source metadata even when `gObjectList` has no sender object. G-CHAT removes avatar typing, bubbles and object effects; G-UI selects plain text without avatar-header constructors. Translation/name callbacks retain CPU behavior and reject expired session/widget generations. |
| R07 outgoing chat/input | native events ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ view/focus/control callbacks ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `FSNearbyChat::sendChat`, `sendChatFromViewer`, `really_send_chat_from_viewer`; typing callback ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ agent start/stop typing | Keep input, Unicode, history, channel/type/agent/session protocol fields and reliable send. G-CHAT splits protocol typing from avatar animation and gates mention pickers that search scene avatars. Nearby chat does not require `gAgentAvatarp`. |
| R08 connected idle/frame | main loop ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `idle`, `updateUI`, `display`; STATE_STARTED block ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ reflection manager and snapshot floater updates; idle ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ object/particle/move/apparent-angle updates | G-FRAME retains network/task/UI scheduling and submits clear plus UI only. G-WORLD prevents scene producers and picking. G-AUX also cuts snapshot/probe work outside `display`, including hidden floater updates. Stopping `display` alone is insufficient. |
| R09 dynamic UI/application admission | `LLFloaterReg::getInstance`, `showInitialVisibleInstances`, XUI factory, menu actions, LLURL actions, login web view, chat links/headers | G-UI checks before construction, including saved visibility restoration and direct XUI child factories. G-AUX checks public preview/capture/tool entry points, so an indirect caller cannot bypass admission. Preserve required error/agreement dialogs. Denied action shows unsupported status; a pending callback receives failure/cancellation. |
| R10 settings/observers | `settings_setup_listeners` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ both `setting_setup_signal_listener` overloads ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ commit signals; shader/resize/font/media/world callbacks; region-changed and UI observers | G-SETTINGS checks at callback execution, including chained setting changes and programmatic changes. Route native resize/vsync/UI font changes to the new owners; retain explicitly admitted CPU settings. G-WORLD/G-AUX backstop direct resource callbacks and observers. No listener may instantiate an unowned optional service. |
| R11 resize/resource completion | window reshape/events ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `LLViewerWindow::reshape`; `stopGL`, `restoreGL`; font lazy glyphs/UI image decode; async texture/media queues | G-RESOURCE owns swapchain extent/DPI/font atlas/UI uploads and retirement. G-BACKEND replaces recreation with Vulkan behavior or clear failure. G-AUX blocks media/browser/world producers not admitted to the UI. CPU bytes stay owned until completion; shutdown/cancel invalidates callbacks before destruction. |
| R12 logout/disconnect/cleanup | `idleShutdown` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `saveFinalSnapshot`, uploads, LogoutRequest; network error/kick/logout reply ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `forceDisconnect`, `disconnectViewer`; `cleanup` ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ `shutdownViews`, workers, `shutdownGL`, window destruction | G-LIFETIME preserves bounded logout and CPU persistence, marks the deferred final snapshot complete without waiting for bytes, cancels work, disconnects observers, destroys views, joins owned producers, retires native resources and destroys the window. Partial init/relogin use the same ownership ledger. No cleanup method for an uninitialized world/GL subsystem is invoked. |

### Startup state census

All 29 enumerated states, including `STATE_INVENTORY_SEND2` and the source's
`STATE_LOGIN_CONFIRM_NOTIFICATON` spelling, are present in the manifest. They
are classified as follows; the enum names may stay for protocol sequencing.

* FIRST/FETCH_GRID_INFO/LOGIN_SHOW/LOGIN_WAIT/LOGIN_CLEANUP/AGENTS_WAIT/
  LOGIN_AUTH_INIT/LOGIN_CURL_UNSTUCK/LOGIN_PROCESS_RESPONSE/
  LOGIN_CONFIRM_NOTIFICATON: retain CPU authentication and native UI. FIRST's
  OpenGL device identity and LOGIN_SHOW's embedded marketing browser are replaced
  or gated; do not bypass a required server agreement.
* AUDIO_INIT/BROWSER_INIT/MULTIMEDIA_INIT: advance without initializing optional
  audio/media plugins. FONT_INIT initializes the native UI font owner.
* WORLD_INIT: split CPU agent/region/session setup from drawable, postprocess,
  appearance, surface, tool and bulk world-UI class initialization.
* SEED_GRANTED_WAIT/SEED_CAP_GRANTED/WORLD_WAIT/AGENT_SEND/AGENT_WAIT: retain
  capability/circuit/movement exchange, retries and failure UI; omit texture
  prefetch, environment, radar/contacts/IM initialization and static-eye/avatar
  callbacks. Guard any direct `gAgentAvatarp` use.
* INVENTORY_SEND/INVENTORY_CALLBACKS/INVENTORY_SKEL/INVENTORY_SEND2/MISC: retain
  login-response identity and required session metadata; advance without
  inventory/outfit/gesture/voice/bridge/pathfinding services or observers. No
  retained nearby-chat handler is allowed to depend on those skipped services.
* PRECACHE/WEARABLES_WAIT: wait only for required native UI assets and validated
  session readiness with timeout; never wait for an absent avatar/outfit.
* CLEANUP/STARTED: finish status/focus/chat activation and continue CPU networking.
  Gate auto-restored floaters, hover-height/avatar calls, world-map observers,
  visual replay and auxiliary updates. `reset_login` cancels the old generation
  before restarting; UI readiness and session readiness are separate conditions.

### Gate contracts and caller outcomes

| Gate | Placement and affected responsibility IDs | Retained caller outcome |
|---|---|---|
| G-BACKEND | Before provider/window/resource initialization, device identity, crash probes and recovery; I01/I02/I21/I25 | Native Win32/SDL2-X11 device/present lifecycle; unsupported configuration or Vulkan failure reported before resource creation. No implicit GL loading/context or Tracy GL collection. |
| G-RESOURCE | At UI facade calls and asynchronous producer publication; I03ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI09/I26/I27 | UI packets, state, clipping/blend, buffer/image/font uploads and clear target implemented through DiligentCore. CPU decoders/caches remain CPU where genuinely independent. No old GL method used as a facade implementation. |
| G-UI | Before registry/factory construction and at public action dispatch; I08/I10/I18ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI22/I24 | Admit login, progress/status, plain nearby chat, required alerts/agreements and their basic widgets/assets only. Denial returns no instance plus visible supported error where user initiated; callers handle absence rather than dereference it. Keep corrected XUI controls and layout. |
| G-STARTUP | At each state branch and both startup-frame helpers; I01/I08/I11ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI15/I18/I20/I24 | Preserve protocol sequence and deadlines, split mixed work, advance skipped states explicitly. No avatar, texture, inventory or media wait can prevent connected chat. |
| G-SESSION | Region constructor/addRegion, handshake, movement completion and agent region change; I11/I14/I15/I16/I24 | Valid CPU session with region host/origin/name/flags/caps. Gate terrain/cache allocations before use; no null land/drawable access. Initial login supported; unsupported teleport/crossing receives controlled visible disconnect rather than silently retaining a stale host. Child-simulator enable traffic cannot allocate a scene. |
| G-PROTOCOL | Handler registration/admission plus UDP/HTTP/application dispatch, before invoking semantic handlers; I10ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI22/I24 | Allow reviewed session/chat/error handlers with mixed side effects split; consume/ack and discard deferred visual/optional messages. Unknown message is logged at bounded rate, never dynamically opens UI/services. Retained alerts cannot invoke an unsupported task/UI without G-UI. |
| G-CHAT | Incoming effects, outgoing typing/animations, transcript headers and mention actions; I08/I14/I20/I22 | Sender/name/filter/history/translation and nearby protocol retained. No avatar requirement, bubbles, scene search, thumbnails or animation submission. |
| G-FRAME | Process main-loop STATE_STARTED auxiliaries, idle producers, display/updateUI/render selections; I11ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI17/I20ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI22 | CPU network/task/UI clocks remain serviced; clear plus UI only. No geometry, scene culling, particles, water, probes, HUD, postprocessing, selection or occlusion queries. Pending picks return empty/cancel exactly once. |
| G-WORLD | At shared world initialization/update/resource/observer entry points, backed by call-site cuts; I11ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI17/I20/I22/I24 | No drawable/avatar/partition/terrain/sky/material/probe ownership. Do not simply set draw masks or allow object decode then assume null drawables are safe. Preserve required CPU session/chat data in the split G-SESSION/G-CHAT adapters. |
| G-AUX | Public snapshot/preview/bake/map/pathing/media/profiling entry points and scheduler callbacks; I10/I16/I18ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI22/I24 | Denied image readback returns explicit unsupported/failure, not fabricated pixels; cancelled pick/upload/readback completions resolve once. Block world-cache/media/plugin producers before start. CPU diagnostics/timers remain; GPU GL scopes are not entered. |
| G-SETTINGS | Listener execution, direct setters and observer callbacks; I01/I02/I06ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI22/I24 | Save values without activating deferred resources. Admit only reviewed CPU/UI/native resource callbacks. Dispatch separately for plain nearby history versus deferred IM history; UI mode does not overwrite saved settings. Chained listeners receive the same policy. |
| G-LIFETIME | Process init/destroy registry dispatch, quit/logout/reset/error/partial-init cleanup and completion delivery; all owned families | No stale callback, unowned singleton initialization/teardown, GL cleanup, final scene snapshot or wait on a deferred upload. Preserve logout/history/settings, bounded timeout, worker cancellation and actual GPU completion before retirement. |

The 27 insertion families are accounted for: I01ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI09 are replaced or split by
BACKEND/RESOURCE/UI/STARTUP/LIFETIME; I10 is deferred through UI/AUX/SETTINGS;
I11ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI17 through SESSION/PROTOCOL/WORLD/FRAME/CHAT; I18ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œI22 through UI/AUX/FRAME;
I23 retains Autobuild/dependency/staging work for the scoped native platform;
I24 remains mixed and is resolved at the explicit admission boundaries above,
not declared CPU-safe wholesale; I25 is bounded build/test/platform exclusion;
I26 retains audited CPU image work with native publication; I27 requires native
facade implementations and cannot legitimize a hidden GL include/callback.
All families additionally obey LIFETIME and mode checks on observers/settings.

## Registration census and dynamic routes

The manifest records every lexical startup message registration (including
`setHandlerFuncFast`), settings listener call (including computed terrain names)
and viewer floater registration. The registry snapshots include source locations
and full normalized call text. Their source files are hashed. Commented-out
registrations are excluded; compile-time alternatives remain accounted for.

Each registry entry has an explicit disposition: retained/split or deferred at
G-PROTOCOL, G-SETTINGS or G-UI. The default-deny disposition is an intentional
cut at registration **and** invocation, not a conclusion that the existing
callback is unreachable. Retained protocol handlers need adaptations described
in R04ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œR07/R12. Session establishment uses circuit/name/seed-cap callbacks outside
the central registry too. HTTP queued and direct dispatch must share that policy.
GenericMessage/streaming messages must not dispatch arbitrary registered methods.
IM, object updates, image packets, appearance/animation, layers, maps, parcel,
inventory and tool handlers are outside the nearby-chat admission set.

The retained protocol set has 17 startup registrations: movement completion,
region handshake, nearby chat, logout, kick, alerts/freeze, feature-disabled,
simulator enable/disable and crossing/teleport status. These are **split**, not
approved unchanged. Ignore unused neighbor-enable traffic without creating a
region; disabling the active simulator yields a bounded disconnect. Reject
crossing/teleport completion with controlled disconnect until CPU region
transition support is implemented. Movement readiness follows validation of
agent/session/region identity, never an early flag assignment before validation.
Alert callbacks and status notifications must not open optional floaters or
trigger scene effects. Stats, economy, health and environment/time-sync handlers
are deferred because the minimal transcript/session path does not require them.

Name-cache constructors separately register four legacy name request/reply
handlers; retain these CPU services under G-PROTOCOL and gate their UI observer
effects. Xfer-manager registration separately adds four file-transfer handlers;
defer the manager and these handlers since staged UI assets and nearby chat do
not require simulator file transfer. HTTP avatar-name lookup remains an owned
CPU request, with session-generation cancellation. Ordinary mute/filter logic
is retained; optional restraint, contact, inventory or scene-dependent filter
extensions cannot instantiate skipped services from the chat handler.

The init/destroy CRTP registration census includes declarations and friend
references conservatively. `LLAppViewer::init` fires `LLInitClassList` before
world initialization; gating WORLD_INIT alone therefore misses wearables,
wearables gear-menu and IM-well initialization. Admit only the CPU emoji
dictionary initialization. Defer those optional initializers and the favorites,
inbox, top-info, search-history and inventory callback-manager destruction
registrations. G-LIFETIME replaces blanket `fireCallbacks` with mode/ownership
checks; no destroy callback may instantiate its singleton. Preserve those
services' saved data by leaving unowned services untouched. Emoji dictionary
loading itself is CPU XML work; emoji textures/pickers remain subject to G-UI.

Settings admission is conservative: the snapshot allows reviewed chat-font,
console, bandwidth, spellcheck, logging, native resize/vsync and nearby history
effects; all other registrations are deferred. This does not prohibit saving
their values. Registration wrappers alone are insufficient: direct callback
calls, singleton observers, URL commands, restored XUI, lazy assets and public
capture/pick entry points must enforce the same boundary before side effects.
The broader source ledger retains those producer/interface/callback obligations.

`LLPanelLogin` currently navigates an embedded browser; `LLFloaterTOS` may require
HTML. Marketing media is deferred. Mandatory text/critical agreements must use
native widgets. For HTML-only mandatory agreements, offer the existing external
account/acceptance route and relogin where the service supports it; otherwise
show a clear unsupported-login error. Never simulate acceptance or silently
advance authentication. Supporting embedded browser pixels would expand the
first slice and require separate G-RESOURCE/media producer qualification.

## Verification and acceptance limits

Verification completed on 2026-10-05: the current insertion checker accepted
28,263 candidate sites in 1,731 files and all 690 shader registrations, with no
unreviewed in-scope candidate. The milestone checker accepted 43 hashed source
files, 12 routes, 27 responsibility families, 29 startup states, 118 startup
message registrations, 208 settings registrations, 216 floater registrations,
four name-cache and four file-transfer registrations, plus 13 conservative
init/destroy references. All 21 audit regression tests and both progress-panel
tests passed. No viewer build, software-ICD run or live session was performed.

Run from the repository root:

```text
python doc/vulkan/check_diligent_insertions.py --accept
python doc/vulkan/check_milestone1_boundaries.py
python -m unittest discover -s doc/vulkan -p test_*.py
python scripts/tests/test_progress_panels.py
```

The boundary checker rejects stale source, incomplete registry/state/family
accounting, missing gates and absent source anchors. Its failure tests mutate
that evidence to verify rejection. It does not preprocess C++, build a call
graph, execute gates, prove arbitrary binary callbacks safe or qualify rendering.
Review of the caller cuts above supplies the semantic interpretation, with a
closed admission policy supplying the dynamic-construction boundary. Any newly
admitted widget/service/message/setting, new platform or changed source requires
re-review; an unchanged lexical count is insufficient.

Later implementation acceptance must exercise these cuts with deterministic
protocol replay (including HTTP queued events, relogin, timeout, malformed and
unknown messages), settings cascades, denied restored floaters, late callbacks,
partial-init cleanup, and no-GL entry-point traps. Run native Vulkan with a
software ICD (SwiftShader on Windows/Linux, optionally Lavapipe on Linux),
validation and synchronization validation, and offscreen/readback UI comparisons.
Resize/minimize/recreation still requires the native window route, possibly a
virtual display on Linux. A real server session remains required to qualify
connected chat; replay alone does not do so. Software devices establish only
their tested behavior, never physical-device performance or driver qualification.

Item 1 is complete when both evidence checkers and regression tests pass at
this source baseline. Implementation of the named gates/native adapters and
software-device/live-session qualification remain required work for subsequent
items. This acceptance must not be presented as the first executable milestone
having passed.
