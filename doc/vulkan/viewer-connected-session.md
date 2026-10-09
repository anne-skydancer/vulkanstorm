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

The final production source is `7d3d8b32f4d46b71d9248705bfde6e0b9a4901ec`.
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

Earlier CI runs were superseded by the final input-history and transport revisions.
Their source-only success does not qualify this final graphics implementation.
The final three-platform run and artifact results will be recorded here after
completion. Live-server qualification remains untested.
