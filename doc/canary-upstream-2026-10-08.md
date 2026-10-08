# Canary upstream integration, 8 October 2026

Destination: `vkstorm-canary`, based on
`bea22a8402d65b3775b604f695e2467dbaa5b701`.
Source: Firestorm `7.2.5_preview`,
`74a5a1e5d2fb606c8ef104707a0d437dc50e7119`.
The integration branch is `codex/canary-upstream-oct08`.

This incorporates 25 upstream commits since the shared baseline, including
avatar skinning bounds and bone remapping, callback erasure safety, moved-cache
purging, image allocation telemetry, WebRTC device reinitialization and package
updates, local mesh import, UI, bridge and map fixes.

## Conflict decisions

Both upstream build workflows gain the expanded symbol patterns covering
WebRTC's `symbols/Release` directory and excluding stripped public PDBs.
Their Linux pattern retains `do-not-directly-run-vulkanstorm-bin`.

`configure_firestorm.sh` retains canary's existing configuration. SoLoud is the
default; `--openal`, `--fmodstudio` and `--soloud` remain explicit, mutually
exclusive choices. Upstream removed the OpenAL option because it enables that
backend by default, whereas Vulkanstorm's audio policy explicitly defaults it
off. Retaining the complete canary script also preserves Second Life defaults,
Zink selection, Inno packaging and the `RelWithDebInfo` development default.

The other changes merge automatically. The WebRTC package revisions and new
platform link dependencies are incorporated together. Canary branding, selected
dependencies, source-count versioning, skin fixes and publication policy are
retained. One trailing space in the upstream plugin plist is removed.

## Qualification

All 15 existing Python regression tests and eight publication tests pass.
Workflow YAML, changed XML/plist and shell syntax are checked locally, together
with the standalone version parser. These checks do not qualify a complete
viewer build or interactive operation; Windows/Linux build results are required
from the integration PR. Audio device switching and the upstream runtime fixes
remain outside the local execution evidence.

Integrate through a PR into canary. No release/development/Vulkan branch or
`latest` update is part of this change. Canary remains a 7.2.5 preview integration
branch; it is not renamed for an upstream beta tag.

## Post-merge review corrections

PR #134 identified two upstream defects. Voice activation called
`workerDeployDevices(false)` without the counter increment owned by
`deployDevices`. Activation now uses that wrapper. Map replies can provide
zero height; `insertRegion` now normalizes each zero extent independently to
256 meters before storage and both tracking checks, preserving nonzero
variable-region extents.

The focused regression launcher compiles the actual production methods into
device-free task-queue and region-storage fixtures. It covers repeated voice
enable/disable, a pending module reset overlapping activation, absent devices,
normal/missing/variable map extents, live/down/nonexistent regions and exclusive
upper boundaries. Both fixtures compile and fail during execution on the
original merged source, and pass on the corrected source. All 17 Python tests
and eight publication tests pass locally; these tests do not exercise physical
audio devices or a live grid. The focused tests also run in the existing canary
build workflow before the platform builds.

The follow-up review extended these corrections to failed deployment attempts
and both live map decoders. Lock timeout, absent modules and caught exceptions
now reach common deployment completion. Failed attempts retire their count
without updating peer track state, and retain a requested module reset for the
next pending or new request. Pending requests continue through the existing
queue; a failure alone does not create an unbounded retry loop.

Both map decoders normalize zero or misaligned extents independently before
passing the result to region insertion. The fixtures compile their actual
extent-reading/validation blocks and exercise them through production insertion
and tracking. Regression coverage now includes absent size blocks, valid
dimensions paired with zero/misaligned dimensions, deterministic lock timeout
and lock exceptions, device-operation exceptions, module disappearance and
recovery, pending reset retention and unchanged peer state on failed attempts.
Both expanded fixtures fail during execution on the preceding PR source and
pass with the corrections. Physical audio concurrency and live-grid execution
remain outside this evidence.
