# V4 connected UI/chat implementation

V4 now has a production CPU session owner in `vsnativesession.cpp`, reached by
ordinary Vulkan viewer login. Authentication and required agreements remain in
the existing startup flow; the successful response enters this owner before
legacy world initialization. The viewer establishes the reliable simulator
circuit, obtains reviewed seed capabilities, processes region handshake and
agent movement completion, and opens its native connected controls. The connected
UI uses the existing skinned Contacts, direct/group IM conversations, notification,
profile, Preferences and transcript controls, together with the actual skinned
nearby-chat floater and shared navigation/status/menu/toolbar chrome.
Vulkan continues to use available machine drivers during normal execution.

## Ownership and admission

The owner retains agent/session identity, simulator host/circuit, region handle,
region UUID/name/owner/access/flags, region dimensions and local agent position.
It owns neither LLWorld nor LLViewerRegion, terrain, avatars, drawables, parcels,
or world media. Account inventory, text assets and UI images are CPU services
retained for messaging attachments and notifications. Agent movement updates CPU position without invoking legacy
position observers. Periodic AgentUpdate packets maintain protocol readiness
without activating camera or avatar resources.

UDP semantic dispatch and direct HTTP message dispatch share default-deny
admission. Transport decoding and reliable acknowledgements precede semantic
admission. Reviewed simulator messages preserve handshake, movement, nearby chat,
name replies, CPU mute-list transfers, direct/group IM, account inventory/profile/
group replies, alerts, logout and disconnect behavior.
Unknown messages, world updates and child-simulator admission are consumed without
calling legacy handlers. Reviewed named/location/landmark/home/lure navigation
uses native wire requests and an authenticated destination seed/circuit/handshake/
movement transition. Account UI survives the transition; retired-region callbacks
cannot publish into its replacement. These new paths still require the current
integration replay qualification. Command URLs are gated before legacy command
execution or scene lookup; login location selection remains available before
connection.

The session generation changes before teardown. Circuit, seed-capability,
event-queue, name, translation, mute transfer and notification callbacks carry
weak ownership and/or generation admission. Event-queue work uses a weak poll
owner, checks both its stopped state and session generation, and cancels suspended
HTTP work. Mute transfers complete cancellation callbacks before destruction.
Partial initialization, logout, disconnect and relogin use the same reset path.
Reset also retires conversation/profile/account dialogs, clears inventory and
contact-set ownership, and disconnects name and settings callbacks before their
controls are destroyed. Contacts snapshots current buddies when constructed
after login rather than relying on an already-consumed startup notification.
Connection establishment has a 60-second deadline; logout waits at most five
seconds for acknowledgement. Vulkan failure ends the native session.

The [connected UI gap audit](connected-ui-gap-audit.md) records the expanded
minimum acceptance: ordinary login, fully functioning connected text UI, local
chat, direct IM and group IM. Implementation and its current qualification are
recorded separately there; the historical nearby-chat CI result below does not
qualify the expanded integration by itself.

## Nearby chat and settings

Normal connected execution uses the existing `FSFloaterNearbyChat` XUI and
`FSChatHistory`, with its real chat editor, Send/volume/channel controls, rich/plain
history, transcript/search, emoji and mention controls. The standard bottom-toolbar
chat entry remains the shared `FSNearbyChatVoiceControl`. Both send through the
native account transport after shared text transformations and RLV policy; they
require no avatar animation or scene command owner. The optional shared `LLConsole`
uses native font/image drawing, ordinary console preferences and visible-session
suppression; spatial voice rendering remains deferred. Notification history remains
available independently of console visibility. Queued/drawn console text and
session suppression retire at account reset, and asynchronous name formatting
retains the account generation and weak console owner. The shared floater is hidden immediately during logout,
and its account-owned instance and scoped setting callbacks retire during reset.

`VSPlainChat` retains a diagnostic transcript/input for the independent pixel and
protocol oracle. Its `useSharedFrontend(true)` bridge opens and focuses the real
nearby-chat editor for normal execution. Local-chat source/owner/type metadata and
IM/group metadata reach the actual shared history through `appendChat`, while the
diagnostic transcript preserves fixture evidence. Existing account log ownership
prevents duplicate writes and observes the nearby/IM logging preferences.

The shared XUI context menus retain their production account callbacks. Native
resident/object headers open the reviewed profile and CPU object inspector;
object location metadata comes from the connected transport instead of a scene
lookup. Conversation options and Block List use the shared controllers. Inventory
gallery actions and delayed rename responses retain account ownership. The replay
now commits real nearby/direct/group options, header menus and inventory Properties;
their successful staged execution remains a qualification requirement.

Reliable UTF-8 ChatFromViewer packets preserve agent/session identity, channel and
volume; negative-channel chat uses ScriptDialogReply. Typing start/stop remains
protocol-only. Incoming source/owner/type/audibility metadata drives mute,
anti-spam, display-name caching, translation and saved history. Immediate
simulator-name display preserves arrival order; bounded name lookups update later
messages and cannot publish into a later login.

Saved setting values remain intact while legacy graphics/optional observers are
denied at invocation. Native owners handle font replacement, scale, vsync and SDL
IME settings; log throttling remains an admitted CPU callback. Login completion
marks per-account settings eligible for persistence without emitting the legacy
world-dependent completion observers. The replay excludes account file writes.

## Qualification

`scripts/run_vulkan_session_replay.py` launches the completely staged viewer through
ordinary LLAppViewer init/frame/cleanup, with diagnostic replay injection restricted
to diagnostic builds. A local HTTP simulator fixture supplies seed capabilities
and queued events; real encoded UDP loopback packets establish the circuit and
exercise incoming chat, outgoing chat/channel/type/typing payloads and identity.
The replay checks malformed and unknown messages, wrong host, UDP/HTTP admission,
queued expired work, relogin, settings cascades, denied optional UI and URL commands,
connection/logout deadlines and partial-init cancellation. The new navigation
assertions exercise authenticated transitions instead of the earlier controlled
crossing-disconnect behavior.
It asserts that LLWorld was never instantiated and compares the native connected
chat frame against the independent CPU pixel oracle.

The dedicated software workflow runs this replay on Windows SwiftShader, Linux
SwiftShader and Linux Lavapipe after the existing V3 viewer matrix. It retains
session XML/JSON, actual/expected PPMs, packets, HTTP requests, validation/loader
logs, source and staged dependency hashes. Missing stage evidence, artifacts,
validation errors, GL traps, crashes, timeouts or unexpected devices fail the job.
The launcher verifies locked runtime revisions and staged library checksums.
These tests publish no release and do not advance latest.

**Current qualification is incomplete.** The user has achieved live credential
login, but reported connected chrome/layout gaps and an IM-interaction crash.
That observation does not qualify all local/direct/group messaging, account
controls, reconnect or logout behavior. The expanded shared frontend and native
navigation implementation requires a new completely staged replay and live
retest. The expanded replay has reached shared chrome and actual context-menu actions,
but has not completed all newly required stages; it is not accepted yet. World rendering and full
supported rendering parity remain V5 and subsequent work.

The reported live dump resolves to `send_agent_pause`, `llworld.cpp:1765`, called
from Win32 input gathering. Native focus/modal pause and resume now send the
ordinary authenticated agent/session/serial packets without iterating legacy
world regions. Native window block/unblock before login or after teardown also
avoids that region path and balances timeout resume without a message system. The replay adds `connected_window_pause_resume` to exercise that
path alongside actual IM controls. New assertions also cover the shared inventory,
bottom chat, nearby frontend and reversible toolbar configuration. Their source
implementation is not a substitute for successful execution.

## Historical execution evidence

The final production source is `d617185e40d0df7ac866fb47fb0d17252e0e185e`.
The complete RelWithDebInfo viewer stage and Windows SwiftShader replay are
qualified locally before submission to the dedicated three-platform CI matrix.
The existing 68-case V3 matrix passed locally with the V4 implementation; focused
UI/startup regression checks additionally cover subsequent lifecycle changes.
The final replay includes actual encoded mute requests and file completion,
authoritative CPU mute loading, cancellation of an in-flight transfer, Unicode
input recall, draft restoration and logout history isolation.

Wire-delivered crossing, frozen/unfrozen status and feature status pass through
the production HTTP dispatch path. Circuit retirement invokes the actual pending
reliable-message callback, verifies that reset is deferred until dispatch returns,
and checks that partial reset removes transport entries and rejects old-generation
callback work. Connected expiry is injected into the real circuit state; it does
not wait for the platform-dependent transport watchdog timer. The launcher checks
loaded ICD and validation-layer hashes as well as staged GHI hashes. Ordinary
login honors the active session validation flag after startup consumes its
one-shot setting, so this path loads core and synchronization validation.

CI run 38005384864 stopped at source evidence because locale-dependent reads
changed Unicode review text on Windows. The catalog checker now reads and writes
UTF-8 explicitly; acceptance is checked with both the Windows default mode and
Python UTF-8 mode before resubmission. No graphics jobs ran in that failed run.
Earlier CI runs were superseded by the final input-history and transport revisions.
Their source-only success does not qualify this final graphics implementation.
Run 38005657430 passed source evidence and the standalone Vulkan/headless checks;
Linux viewer compilation then found an incorrectly cased anti-spam header name.
The include now matches tracked `NACLantispam.h`, and source evidence checks all
native viewer includes for case mismatches before the platform builds. The
Windows runtime behavior is unchanged by this portability correction.
Verified GHI/software-runtime dependencies are now cached after the standalone
checks, before viewer compilation, so a viewer failure does not discard them.
## Software qualification complete: 10 October 2026

[CI run 38012380599](https://github.com/anne-skydancer/vulkanstorm/actions/runs/38012380599)
passed source evidence and all three independent graphics jobs at commit
`2d7298e2b097600666bbf3ffd3293077214472c8`. Each job built and completely staged
the RelWithDebInfo viewer without an installer, passed six standalone presentation
cases and five headless cases, passed the existing 68-case viewer matrix, and
passed all 27 connected-session replay stages. The production source catalog
remains pinned to `d617185e40d0df7ac866fb47fb0d17252e0e185e`; subsequent commits
changed only CI and qualification records.

All three uploaded artifacts were downloaded and checked against the run commit,
staging executable checksum, GHI loaded-library hashes, locked ICD/validation-layer
hashes, license checksums, XML/JSON stage results and shutdown/validation logs.
The actual and independent expected connected-chat PPMs were compared again:

| Platform/runtime | Viewer cases | Session stages | Maximum RGB difference |
| --- | ---: | ---: | ---: |
| Windows SwiftShader | 68 passed | 27 passed | 1 |
| Linux SwiftShader | 68 passed | 27 passed | 1 |
| Linux Lavapipe | 68 passed | 27 passed | 2 |

All readbacks remain within the existing strict color threshold of 3; no threshold
was loosened. Validation and GL traps were clean. Runtime reports retain their
dirty-worktree flag, which includes auxiliary CI checkouts and cache inputs;
these reports are not a clean-worktree certification. Source acceptance and the
recorded build/run commit remain separate evidence.

V4 production integration and deterministic software qualification are complete.
Real-server authentication, nearby send/receive, disconnect and logout remain
explicitly untested because credentials/server access are unavailable. The replay
does not qualify those live-server criteria, physical multi-monitor DPI, OS IME
services or world rendering.


## Login input regression reported during live testing: 10 October 2026

The user could see the login screen but could neither enter credentials by
clicking the fields nor activate Login. Earlier software qualification was
insufficient: it assigned keyboard focus and invoked button commits directly,
so passing those checks did not establish pointer-driven login usability.

The native window's empty full-window popup and floater containers inherited
mouse opacity instead of the `mouse_opaque="false"` policy in `main_view.xml`.
They intercepted hit testing before the login controls. The native construction
now uses that same click-through policy and excludes those containers from tab
stops. Their child dialogs and popups still receive events normally.

The native Unicode path also failed to translate the deferred Return character
into a control key event. It now submits after preceding text events, preserving
modifier exclusions and avoiding duplicate submission for controls which handle
Return on keydown.

The startup acceptance probe now waits for the first real presentation/layout,
clears focus, delivers Win32 or SDL mouse/text events, types synthetic credentials,
and verifies the existing login controller callback from both a Login click and
Return. It records `login_os_input_verified` and `login_submit_actions`; CI rejects
missing evidence or a count other than two. The callback is intercepted only in
the offline probe; production continues through the ordinary startup callback.
This does not certify live authentication or a real server session.


Local Windows requalification of the repair passed all 68 viewer cases, including
OS credential-field clicks, synthetic text entry, Login and Return submission in
every positive startup skin/theme/language case. Required modal and agreement
buttons are now clicked through the same OS route. Core/synchronization validation
reported zero errors and all 18 startup readbacks per positive case matched the
existing pixel oracle. The production session replay also passed, as did 19 viewer
regression tests and 21 source-documentation tests. This evidence uses the pinned
SwiftShader runtime; live credentials/server testing remains unqualified.

The preceding results describe the earlier local-chat baseline. The expanded
connected UI, direct/group IM, inventory-offer, profile, group-management and
Preferences integration now has fresh local Windows qualification, recorded in
[the connected UI audit](connected-ui-gap-audit.md#recorded-local-windows-qualification-10-october-2026).
The connected matrix exercised all 24 packaged skin/theme/language selections
and standard-skin 125%/150% scaling; its vintage dummy-control failure was fixed
and the complete vintage replay rerun successfully. Final source also passed
all 68 viewer runtime cases and all 76 required connected assertions at 150%.
Factory rejections and dummy controls remain decisive failures alongside
crashes, validation errors, missing evidence and GL access. Final Linux SDL
runtime confirmation is pending dedicated CI; live-grid interoperability remains
a separate qualification. The earlier baseline does not qualify these additions.

The subsequent menu-crash report exposed a remaining shared callback dependency:
opening Viewer evaluates the hidden Close Window enable callback during menu
layout. That callback accessed the snapshot floater container, which the native
text-only UI does not construct. A normal no-login viewer run reproduced the
access violation in `LLFileEnableCloseWindow::handleEvent`, through
`LLMenuGL::arrange` and `LLFloaterView::getFrontmostClosableFloater`.
Source commit `4581519cddb0d0d98ae231dd108ae20cd5f21194` makes the snapshot owner
optional in all four close/enable/group-close callbacks while retaining ordinary
UI closing and existing snapshot precedence. Native startup also registers the
shared Edit and spellcheck callbacks before creating text controls. The dedicated
CI now compiles and executes the actual close callbacks with absent and present
snapshot owners; startup qualification opens Viewer through native OS mouse
events before the existing Help/About checks. Opening only Help had not covered
the failing callback.

The final Windows RelWithDebInfo stage passed all 68 viewer cases, including
Viewer-menu opening for every startup skin selection and all 24 startup
readbacks per positive case. The ordinary no-login menu reproduction exited
cleanly after the fix, with no access violation or missing Edit callbacks.
The exact callback fixture also retained OpenGL snapshot precedence and ordinary
floater/group closing, including the empty-owner case. This is software Vulkan
qualification; it does not establish live-grid or physical-driver acceptance.
The same final stage passed all 76 required connected-session assertions with
the standard skin at 150% scaling, including local chat, direct/group IM,
Preferences and relogin. Linux runtime confirmation remains pending dedicated CI.

### Graphics preferences and repeated tab switching, 10 October 2026

Source fix `7c615dcbb71fccc67b243c254c313c7e28ab56e2` removes the native
blanket disabling of graphics controls, preserves saved FOV/FSAA choices and
bypasses legacy hardware recommendations that could overwrite settings. Scene
wireframe and hardware Defaults remain unavailable without their renderer owner.
Settings remain readable, editable and persistent with world rendering disabled.
The reported preferences crash dump identifies a null write in
`LLRender::color4ub`, called by translucent `LLColorSwatchCtrl::draw`; that draw
now omits the redundant GL color mutation and uses backend-neutral UI matrices.

The Windows RelWithDebInfo viewer built in the existing Autobuild-generated
cache passed full staging and actual native session replay in default and
`ansastorm_modern` skins. Each replay passed 78 required assertions, traversing
66 admitted top-level/nested preference paths over 396 frame draws, including
repeated switching, Apply, Cancel, reopen and fresh saved-settings readback.
RenderBackend remained Vulkan and world owners remained zero. Fifteen focused
source regressions passed separately. This is Windows SwiftShader/loopback
evidence; manual interaction, physical GPU, live-grid and Linux qualification
remain unperformed for this fix.

The original `build-vcloginfix-local` user executable is preserved (SHA-256
`dc231443d8c7823485553e3f21b8626da10bfeea50cb6e98c066f129adcfe830`). The
corrected complete stage is the existing `build-vcabout-local` directory
(executable SHA-256
`f759a71359adeead46e400c3ad45e0038e56c2db78af439935b1edbff06f1d01`).
Local evidence is in `.tmp/preferences/qualification.json` and its linked replay,
build and source-test records; these scratch artifacts are not distributed.

Local history reconciliation merged `codex/vulkan-restore-progress-panels` as
`8ddee9d8b5108e80ad82e7950c02d9e9339b9d46` and
`codex/vulkan-first-deliverable` as
`f560e6b41e03756f05c4531bb79ae1d0c1babd04`. Their documentation patches were
already ported as `898df1ab5d` and `e054538629`; both merges retain the newer
scope, source pins and qualification records, with no content change. Local
and remote-tracking ancestry inventory found no other branch descending from
the pre-integration Vulkan tip. Legacy pre-reset Vulkan/UI branches and separate
release, canary, development and GL experiments were excluded. No push, branch
deletion, history rewrite or release action was performed.

Post-integration checks passed the insertion-catalog acceptance and milestone
boundary checks, 21 documentation tests and 92 tests from all 12 dedicated
software-Vulkan CI regression scripts (each run in its own process, matching
the workflow). A combined module invocation was unsuitable: the diagnostic
fixture imports from its script directory, and its session upload test also
encountered a connection reset; both scripts passed in the intended standalone
invocations. The complete stage passed its staging check and a fresh default-skin
native session replay passed all 78 assertions. All seven build-manifest source
hashes still match the qualified executable; the two ancestry-only merges need
no recompilation. Source analysis acceptance remains separate from runtime
qualification, and the software-runtime limits above still apply.

### Login friend-status crash: qualification

The scoped working-tree fix based on HEAD
`d2570e155f01604c12195d3b4cdee5e1cdb2c574` routes friend-status notices to the
native `VSPlainChat` owner. The reported executable (SHA-256 `f759a713...`) and
dump `vulkanstorm-bin.exe.23872.dmp` identify a callback entering the legacy
nearby-chat handler. A synthetic loopback debugger reproduction locates the
null access at `FSFloaterNearbyChat::addMessage:300`, RVA `0x26aaa2` in that
image; native startup does not construct the legacy nearby-chat owner.
The Windows work queue converts this access violation into an uncaught exception
and fail-fast termination. No live login was submitted during diagnosis.

The corrected Windows RelWithDebInfo image has SHA-256
`56b997fa82d07e2a9eef9eb0ce12dbed678aaf44923547cf6df8c00d1c7ed3f6` and is
fully staged in `build-vcabout-local/newview/RelWithDebInfo`. It passed the exact
synthetic friend-online trigger and all 79 connected assertions in default and
`ansastorm_modern` skins, including online/offline history modes, relogin and
Preferences. World owners remained zero. Ninety-three dedicated source tests
passed, with one local HTTP connection-reset failure passing on retry. The
original crash executable/PDB and dump/log are retained in `.tmp/login-crash`;
the older `build-vcloginfix-local` image remains unchanged. The handed-off image
has not been rebuilt or replaced during this documentation reconciliation.

The fix and regressions are committed as
`f06d246d3c79c1c12c06f58df1425f6e4dbfca16` directly on `vkstorm-vulkan`; no
separate fix branch exists to merge. Its [scoped source review](login-crash-working-tree-review.json)
preserves the pre-commit build evidence and records the canonical source-pin
resolution against that actual commit. The insertion negative fixtures isolate malformed review
metadata after the clean-source precondition; an additional regression checks
that dirty source is rejected before review. Their original rejection assertions
remain intact. After explicit local commit approval, the existing insertion and
platform review ranges were remapped against unchanged text and supplemented
with the scoped I08/I25 changed-block reviews. The boundary hash and generated
insertion artifacts were refreshed. Catalog generation and independent
verification with `--accept`, the boundary acceptance command and all 22
documentation tests passed. The previously rejected committed-source checks are
closed without changing their production guards. The executable was built before
the commit from the same reviewed source bytes and remains byte-for-byte
unchanged; this repin does not claim a new build. Live-grid, physical
GPU, manual interaction and Linux acceptance of this fix remain unqualified.
