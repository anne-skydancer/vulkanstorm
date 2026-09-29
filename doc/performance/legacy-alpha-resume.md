# Legacy alpha investigation — resume notes

Historical capture notes from the 2026-09-29 shutdown pause. Broader automated
validation has since resumed; see `mesh-offload-completion.md` for current status.

Latest user validation, received after pausing: "Second candidate functions well
with legacy alpha. No issues that I can see." This supersedes the earlier open
visual result for that candidate. Record it as a successful user visual check,
not exhaustive coverage or completion of the broader GPU-offload work. Do not
restart capture sessions unless further investigation is needed or requested.

## Scope and workspace

Current priority: correct camera-dependent PBR glass behavior in legacy alpha
(`RenderAlphaSortMethod=0`). PPLL (`=1`) resolves the observed symptom but is not
the requested legacy-alpha fix. User reproduced the issue in baseline, before
the mesh offload changes. Test scene: Isle of Repose.

Worktree: `C:/Dev/vulkanstorm/worktrees/mesh-offload-completion`, branch
`codex/mesh-offload-completion`. Do not move worktrees. This checkpoint is being submitted as a draft PR.
The broader item-1 GPU mesh offload work remains incomplete; see
`mesh-offload-completion.md` for its implementation and qualification gates.

## Candidates and evidence

1. Restore deferred shader state after alpha glow replay in `lldrawpoolalpha.cpp`.
   Production-handoff fixture passes; user reports mitigation, not resolution.
2. `llalphasort.h` plus spatial-group/volume integration detects adjacent face-depth
   inversions from the current viewing direction. Refreshes keys at rebuild time.
   Avoids rebuild requests while the order remains valid. CPU metadata check, not
   GPU sorting. Tests pass. The earlier recording showed a remaining artifact; the later
   user report confirms no visible issue with the second candidate. Exhaustive
   coverage and the complete cause remain unestablished.

Latest fully staged candidate:
`build-vc170-64/newview/RelWithDebInfo/Vulkanstorm-RelWithDebInfo.exe`

SHA-256: `a8135cec085c04109bcf359314b54251474faa459f5f606fb0b102fc45382c8d`.
Build/stage result: `.tmp/alpha-sort-build-result.json`. Release feature parity,
alpha sort regression, particle alpha integration, and native OpenGL/Zink startup
checks passed. Startup tests deliberately close after 25 seconds; user mistook
one for a crash after logging in. Do not use timed smoke viewers for scene tests.
A separate Windows crash event existed at 14:12 during the early SLURL handoff
attempt; do not conflate it with the later normal timer shutdowns.

Recordings supplied by user in OneDrive Videos/Screen Recordings:
- `Screen Recording 2026-09-29 133857.mp4`
- `Screen Recording 2026-09-29 143613.mp4`

Extracted frames/contact sheets: `.tmp/glass-recording` and `.tmp/glass-recording-2`.
The second recording shows entire panes transitioning between blue/cloudy and
clearer appearance with camera movement. Ordering/state within individual draws
has not been established; avoid another speculative fix before inspecting captures.

## Historical capture procedure (only if the symptom returns)

RenderDoc 1.46 installed in `C:/Program Files/RenderDoc`.
`.tmp/launch-alpha-capture.py` launches an untimed candidate with an isolated
`.tmp/world-validation` profile, legacy alpha zero, and SLURL handoff disabled.
User may run alongside baseline briefly, but cannot keep two viewers open long.
Do not disturb the baseline viewer. User performs login.

IMPORTANT: launch the viewer outside the sandbox (request escalation when active).
An earlier sandbox launch had blocked network access, could not access machine ID,
and displayed a saved-credential decoding warning. Do not alter credentials.

`.tmp/trigger-alpha-capture.py` uses RenderDoc's target-control API, reads the target
ID from `.tmp/alpha-captures/launch.log`, and triggers a frame. Run it using:

`qrenderdoc.exe --python C:/Dev/vulkanstorm/worktrees/mesh-offload-completion/.tmp/trigger-alpha-capture.py`

Launch this helper hidden; it exits itself after capture. The script logs completion
to `.tmp/alpha-captures/trigger.log`. Use unrestricted execution/escalation for the
local target connection if the sandbox blocks it. A new viewer launch changes the
target ID. Old PIDs and IDs must not be reused after shutdown.

Only capture currently saved:
`.tmp/alpha-captures/legacy-glass_frame3169718.rdc` (about 1 MB).
It is the blocked LOGIN SCREEN, not the scene, and is not glass evidence.
Its thumbnail is `capture-1.png`. The capture mechanism itself worked.

The restricted viewer was terminated and an unrestricted capture viewer relaunched;
the task was then paused before the user confirmed login/camera readiness. If further capture is requested,
launch a fresh untimed capture session, have the user log in and frame the affected
glass, then trigger capture directly. Capture cloudy and clearer states if possible.
Verify thumbnails before replay analysis. The observed renderer in the restricted
session was native AMD despite requesting Zink; verify the actual backend in the
new session rather than assuming the command-line request succeeded.

These notes describe the earlier capture attempt. The current draft PR checkpoints
the subsequent implementation and validation; it does not integrate the branch.
