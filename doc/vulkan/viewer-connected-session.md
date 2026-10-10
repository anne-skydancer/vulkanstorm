# V4 connected UI/chat implementation

V4 now has a production CPU session owner in `vsnativesession.cpp`, reached by
ordinary Vulkan viewer login. Authentication and required agreements remain in
the existing startup flow; the successful response enters this owner before
legacy world initialization. The viewer establishes the reliable simulator
circuit, obtains reviewed seed capabilities, processes region handshake and
agent movement completion, and opens its existing native nearby-chat panel.
Vulkan continues to use available machine drivers during normal execution.

## Ownership and admission

The owner retains agent/session identity, simulator host/circuit, region handle,
region UUID/name/owner/access/flags, region dimensions and local agent position.
It owns neither LLWorld nor LLViewerRegion, terrain, avatars, drawables, parcels,
media or inventory. Agent movement updates CPU position without invoking legacy
position observers. Periodic AgentUpdate packets maintain protocol readiness
without activating camera or avatar resources.

UDP semantic dispatch and direct HTTP message dispatch share default-deny
admission. Transport decoding and reliable acknowledgements precede semantic
admission. Reviewed simulator messages preserve handshake, movement, nearby chat,
name replies, CPU mute-list transfers, alerts, logout and disconnect behavior.
Unknown messages, world updates and child-simulator admission are consumed without
calling legacy handlers. Teleport/crossing requests cause a controlled disconnect.
Command URLs are gated before legacy command execution or region lookup; login
location selection and the login command remain available before connection.

The session generation changes before teardown. Circuit, seed-capability,
event-queue, name, translation, mute transfer and notification callbacks carry
weak ownership and/or generation admission. Event-queue work uses a weak poll
owner, checks both its stopped state and session generation, and cancels suspended
HTTP work. Mute transfers complete cancellation callbacks before destruction.
Partial initialization, logout, disconnect and relogin use the same reset path.
Connection establishment has a 60-second deadline; logout waits at most five
seconds for acknowledgement. Vulkan failure ends the native session.

## Nearby chat and settings

The existing native chat panel gains region status and a Log out control. Input history retains at most 128 submitted
lines, supports recall and draft restoration, and clears when the session detaches. Its real
input sends reliable UTF-8 ChatFromViewer packets with agent/session identity,
channel and chat type. `/whisper`, `/shout`, `/123` and repeat-channel `//` syntax
are supported; rejected sends retain input. Typing start/stop uses protocol packets
without an avatar. Incoming source/owner/type/audibility metadata drives mute and
anti-spam filtering, display-name caching, optional translation and saved history.
Immediate simulator-name display preserves arrival order while a bounded owned
name lookup updates subsequent messages. Stale callbacks cannot publish into a
new session. Required controls retain the selected skin and native font resources.

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
connection/logout deadlines, controlled crossing and partial-init cancellation.
It asserts that LLWorld was never instantiated and compares the native connected
chat frame against the independent CPU pixel oracle.

The dedicated software workflow runs this replay on Windows SwiftShader, Linux
SwiftShader and Linux Lavapipe after the existing V3 viewer matrix. It retains
session XML/JSON, actual/expected PPMs, packets, HTTP requests, validation/loader
logs, source and staged dependency hashes. Missing stage evidence, artifacts,
validation errors, GL traps, crashes, timeouts or unexpected devices fail the job.
The launcher verifies locked runtime revisions and staged library checksums.
These tests publish no release and do not advance latest.

**Live server qualification is untested:** no credentials or accessible test
server have been supplied. Replay does not demonstrate authentication against a
real server, real-server nearby send/receive, disconnect or logout. Those are the
separate connected-chat acceptance criteria in the roadmap. World rendering and
full supported parity remain V5 and subsequent work.

## Execution evidence

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
