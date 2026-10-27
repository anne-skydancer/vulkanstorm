# vkstorm-vulkan code survey

A survey of the `vkstorm-vulkan` line: what the native Vulkan viewer is, what
exists, what is wired in, what is partial or refused, and what is broken. It
complements the design study in `doc/vulkan/` (README.md, architecture.md,
framework-comparison.md, qualification-roadmap.md), which describes the
intended design; this survey describes the code.

- **Baseline:** `vkstorm-vulkan` at `51d1f729a6` (the #225 merge,
  2026-10-27), compared with `vkstorm-release`.
- **Method:** source reading plus CI history from GitHub. Nothing was built or
  run for this survey. "Implemented" means the code exists and is wired in, not
  that it was observed working. Statements not read from source are labelled
  *Inference*; counts give their commands.
- The owner's own assessment, which the survey confirms: very early alpha and
  extremely partial.

## Chapters

| # | Chapter | Covers |
|---|---|---|
| 01 | [Native rendering stack](01-rendering-stack.md) | Build/enablement, Diligent device and swapchain, world and UI renderers, shaders, frame flow, design study vs code |
| 02 | [Native session and world data](02-session-and-world.md) | VSNativeSession, scene and object store, avatars, terrain, environment, regions/parcels, materials/glTF, textures and meshes |
| 03 | [Native UI and input](03-ui-and-input.md) | Shared UI under native drawing, admission gates, window and input, tools, picking/selection, camera, chat/IM, maps, preferences |
| 04 | [Integration, build and history](04-integration-build-history.md) | Hooks into shared code, effects on the OpenGL viewer, build options and CI, the 659 commits |
| 05 | [Qualification and evidence](05-qualification-and-evidence.md) | CI jobs and current results, standalone/viewer-process/R2 tests, insertion catalog, parity ledger, test inventory |
| 06 | [Feature completeness](06-feature-completeness.md) | How Vulkan is selected, feature-by-feature matrix, incompleteness markers, biggest gaps |

## The line at a glance

- **Size:** 659 commits ahead of `vkstorm-release`. Native code is
  `indra/newview/vs*` (280 files, ~63k lines); 885 existing C++ files changed;
  ~400k lines of evidence data under `doc/`; 342 test scripts (04, 05).
- **Enablement:** the native renderer is compiled only with
  `USE_DILIGENTCORE=ON` (default OFF), which only the software-Vulkan CI job
  sets. In any other build, choosing "Vulkan (in development)" is refused, and
  `RenderBackend=Vulkan` set by hand stops startup with no OpenGL fallback
  (01, 06).
- **Architecture:** a separate connected viewer. `VSNativeSession` and
  `VSWorldScene` replace `LLWorld`/`LLViewerObject`; the shared llui/XUI tree
  runs with its 2D drawing redirected; closed allow-lists decide what is
  reachable (128 simulator messages, 206 of 240 floaters, 57 of 69 toolbar
  commands) (02, 03, 06).
- **What renders:** terrain, a sky dome, prims (sculpts, flexis, meshes),
  glTF, CPU-skinned avatars with attachments and animesh, particles, HUDs,
  name tags, beacons and the minimap, with one forward pass and sun plus
  ambient light. Water is a flat tinted quad (01, 06).
- **What does not:** shadows, local lights, specular/BRDF, reflection probes,
  clouds/stars/sun and moon discs, post-processing and anti-aliasing; glow is
  drawn into a target nothing reads. Building/editing is unreachable; hover
  text is discarded; no macOS path; no fullscreen; Linux X11 only (01, 03, 06).
- **Qualification:** `source-evidence` passes since #225. The software-Vulkan
  `graphics` jobs fail at their first build step; the native viewer was last
  compiled in CI on 2026-10-18 and the last fully green qualification run was
  2026-10-16. The parity ledger marks 1 of 3131 entries implemented and none
  runtime-qualified; ~90% of the tests compile extracted production code and
  none execute Vulkan (04, 05).

## Fix list, in priority order

### 1. Make the native build compile again (blocks all runtime evidence)
- `vsworldrenderer.cpp` includes `llperfstats.h` and `vsuirenderer.cpp`
  includes `llimagegl.h` (116a410fc8), breaking the standalone Diligent test,
  whose CMake requires these sources to depend only on the GHI and standard
  library. All three graphics jobs fail there (01, 05).
- `vsselectionlightpresentation.cpp` and `vsworldpaint.cpp` are not in
  `indra/newview/CMakeLists.txt`, yet `vsvulkancontext.cpp:310` constructs a
  `VSSelectionLightPresentation`; a native viewer build would fail to link.
  Checked while assembling this index (03).
- Then run the graphics jobs and fix what they report: the native viewer has
  not been compiled in CI since before 116a410fc8, so more breakage is likely.

### 2. Restore the OpenGL reference (the line's contract is broken)
The line's premise is that the OpenGL code stays intact, as the reference
against which the results-based Vulkan code is qualified. The code no longer
meets it:
- About 65% of the changed lines in existing C++ files also compile into
  OpenGL-only builds: ~23k added lines and all 15.9k deleted lines (04).
- 343 shared files had logic moved into shared `vs*` helpers, so the OpenGL
  path now runs Vulkan-line code (for example the region capability list in
  `vscapabilities.cpp`, the source of the FlickrConnect leak) (02, 04).
- Known consequences: the FlickrConnect and friends-only regressions and three
  OpenGL-only compile/link breaks, all from 116a410fc8 (04); unguarded
  behaviour changes in Win32 DPI handling and clipboard retry, Vivox early
  exits, mesh upload completion, and a Preferences "Vulkan (in development)"
  choice that stops startup in OpenGL-only builds (04).

Restoring it means: with `USE_DILIGENTCORE=OFF`, the OpenGL viewer should build
and behave as `vkstorm-release` does. Native code belongs behind
`VS_NATIVE_VULKAN` or in native-only files, and shared logic should not move
out of the OpenGL sources. A check that diffs the OpenGL-only compilation
against `vkstorm-release` would keep it that way.

### 3. Functional bugs in the native path
- `ParcelAccessListReply`, `ParcelDwellReply`, `ParcelObjectOwnersReply` and
  `EstateCovenantReply` are admitted but appear to have no handler in native
  sessions, so land access, dwell and covenant views likely stay empty (02).
- Native IM drops teleport offers/requests, object IMs, session invites and
  GOTO_URL (03).
- Native camera drag is dead code (`mNativeCameraDrag` never set) (03).
- Glow target written every frame, never read (01).

### 4. Scalability
- CPU skinning re-uploaded about every frame per animated avatar; no joint
  palette (02).
- Per-update scans of the whole object map although `VSObjectIndex` exists;
  hard cap of 16384 objects across all regions; no object cache (every
  `ObjectUpdateCached` is a full re-request) (02).
- Per-LOD mesh downloads with no disk cache, decoded on the main thread (02).

### 5. Renderer and feature gaps
Shadows, local lights, PBR shading, reflection probes, water, sky detail,
post-processing, building/editing, hover text, fullscreen, Wayland, macOS
(01, 03, 06). The design study's roadmap (`doc/vulkan/qualification-roadmap.md`)
orders this work; chapter 05 compares it with what is qualified.

### 6. Process
- Avoid large unbuilt `[skip ci]` checkpoints: 116a410fc8 (828 files) was
  committed without a rebuild and accounts for the breakage above (04).
- Every `indra/` change fails `source-evidence` until a re-pin; 6 of 9 runs
  from #189 to #223 first failed on the pin (05). Folding the re-pin into the
  change's own PR (as #224/#225 did) avoids red merges.
- 105 tests added by 116a410fc8 run in no workflow (05).
- 57 of release's 102 commits since the merge base are not on this line,
  among them the SLua port, the Penny Patton camera default and several
  statistics/probe/hover fixes (04).
- Stale documents: `connected-ui-completeness-audit.md`,
  `world-feature-parity.md`, `3p/README.md`, `architecture.md:4-6`, and three
  hand-written ledger dispositions (HUD projection, sky, Firestorm bridge)
  (01, 02, 03, 06).

## Limits

- Nothing was built or run; runtime behaviour, performance and visual
  correctness are unverified.
- Counts come from grep and scripts over the tree, not from a build or call
  graph; chapter 04's guard counts come from uncommitted scratch scripts whose
  method it describes.
- CI results are as read from GitHub on 2026-10-27.
