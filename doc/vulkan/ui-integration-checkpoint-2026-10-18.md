# Connected UI checkpoint â€” 18 October 2026

The user requested a commit and push after the camera-focus work, followed by
a break. This explicitly authorizes this checkpoint push despite the earlier
UI qualification hold. It does not establish R3 qualification or complete UI
acceptance. Further implementation is paused at the user's request.

## Committed implementation

- `01c2649ec8`: shared Places, landmarks, Favorites, history, World Map and
  avatar map/tracking/teleport controllers, plus actual default-Pie input and
  retired selection continuation guards.
- `e70b2ba5d9`: native minimap imagery, object/parcel rasters and avatar
  markers; the real avatar inspector and Compass controllers. Rectangular
  region borders have a separate regression check.
- `5d30a0e470`: real tooltip root, shared original hover/timer dispatch and
  retirement; shared context/pie construction and callback admission; shared
  RLVa receive-text, name and location filtering before native translation
  and delivery. The decoded owner UUID is passed explicitly, preserving the
  reference handler's later assignment to `LLChat::mOwnerID`.
- `026f705176`: the actual native camera publication now updates the shared
  `LLAgentCamera::mFocusGlobal`, with nonfinite-focus rejection. Native window
  focus gain/loss and Inspect ALT-release enter the real tool owners. Graphics
  recommendations/presets use actual implemented consumers and selected-device
  facts; wireframe has an actual world pipeline consumer. Unsupported rendering
  effects retain their saved values and remain implementation obligations.
- `b0ed0f09fe`: the original owner-chat command controller is shared with
  native delivery, including bridge/command ordering, actual temporary
  attachment identity, diagnostic outcomes and account retirement between
  commands. Native whisper/shout and emote metadata reach the shared UI.

Earlier local implementation commits are included in this checkpoint's branch
history. They include actual selection, attention, media controls/navigation,
script recovery, Quick Preferences and resource-limit request owners; their
presence is not a claim of complete runtime parity.

## Evidence and executable

Actual MSVC syntax checks passed for the applied source roots using the
canonical Autobuild headers. Compiled controller regressions cover reference
branch/getter ordering, input priorities, owner retirement, tooltip lifecycle,
map rasters and preset behavior. The session-runner unit suite passed 35 tests.

The last complete `ALL` RelWithDebInfo build, runtime/assets staging and build
provenance passed at `fda4ef55ab` (source catalog `e70b2ba5d9`). Its executable
is at `build-vc170-64/newview/RelWithDebInfo/vulkanstorm-bin.exe` in the canonical
worktree. **It predates the latest commits.**

That build's actual headless replay, `.tmp/uk35`, failed after 23.8 seconds at
the strict script-icon window-click assertion. The diagnostic plain-chat panel
covered the icon; the replay had hidden only the shared Nearby Chat frontend.
The committed correction hides and restores both actual frontends for this
unobstructed-world-click case, preserving normal UI first refusal and the real
mouse-input assertion. **The correction and the new tooltip replay have not
yet been rerun in a rebuilt viewer.**

The earlier R3 run `39086372280` failed all three graphics jobs: Windows and
Linux SwiftShader at concurrent texture-cache instances, and Linux Lavapipe
at R2 outfit replay. The downloaded Windows and Lavapipe evidence archives and
their individual manifest hashes verified successfully. These failures remain
open; source-analysis acceptance does not certify runtime parity.

## Resume work

Full connected UI and keyboard navigation still means 100% production
implementation, preserving normal permission, selection, maturity, RLVa and
availability rules. Remaining work includes:

- Complete Camera focus/track/input, Grab/Inspect, shared per-frame hover and
  async-pick dispatch, action callbacks, editing/manipulators and visual feedback.
  Publishing the shared focus value is not the complete Camera tool port.
- Actual name tags, depth-aware UI and original rigging envelopes. The pending
  producer must union joint-space boxes before transforming the aggregate.
- Object sound, Sound Explorer, existing asset blacklist/derender services and
  their actual engine, packet, UI and account lifetimes.
- Complete nearby receive policy beyond the committed RLVa/owner-command
  portions, including original spam, notification and ancillary behavior.
- Actual object-media window-input/navigation/readback evidence; the existing
  UI-browser replay with a synthetic consumer does not qualify MoaP.
- Snapshot/capture, upload/preview and other outstanding UI/service routes,
  native GPU timing/autotune and effects requiring later renderer passes.
- Refresh the canonical full build/stage, run the strict new runtime cases,
  repair the outstanding R3 failures and inspect all three platforms' evidence.

Unapplied agent work is saved locally, with exact status and composition
instructions. These are drafts and must not overwrite fresh canonical sources:

- `.tmp/selection-frontend/RESUME-POINTER-2026-10-18.md`
- `.tmp/sound/SOUND-RESUME.md`
- `.tmp/ui-completion-resume.json` (root resume index and evidence locations)

The name-tag agent's final handoff is indexed in the root resume file. All
agents are stopping for the break. Drafts have no production/runtime acceptance.


## Subsequent implementation checkpoint — 18 October 2026

The user subsequently resumed development and requested that completed R3
implementation be committed and pushed. The earlier pause above describes the
previous checkpoint, not the current work state. This checkpoint is source work;
R3 and complete UI acceptance remain open. Rebuild and runtime qualification
are deferred until the remaining production gaps are closed, as requested.

The source changes since the previous checkpoint include native connected UI
and service integrations for appearance and wearables, sound and voice, particles,
name tags, selection and object editing services, task inventory, snapshots and
previews, Area Search, media settings, directory search and administrative tools.
They also include shared immediate geometry, selection-light drawing, physical
pixel image drawing, source-aware alpha classification and local built-in image
loading, plus utility dialogs and account/selection lifetime guards. Shared
OpenGL policies and existing transports are retained where transposed.

These are implemented components, not a declaration that their entire parent
features are complete. In particular, the full Face panel integration, Poser,
manipulator controllers, world drag-and-drop, land tools, object inspection and
remaining utility paths are still open. Unapplied implementation drafts under
`.tmp` are deliberately excluded from this commit.

Source-only checks and extracted production-body regressions accompany the
implementation. The latest image-provider, shared image drawing and rendering
source checks are recorded locally. No full viewer rebuild, staging refresh,
viewer launch or three-platform runtime qualification was performed for this
checkpoint. The staged executable described above still predates these changes.

The commit requests CI skipping so this explicit backup push does not initiate
qualification ahead of implementation closure. Final qualification must use the
completed source and its corresponding staged executable and evidence.
