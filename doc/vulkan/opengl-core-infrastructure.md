# OpenGL Core infrastructure: first implementation milestone

Branch: `codex/opengl-core-modernization`, based on local `vkstorm-devel`
at `4a6056aa15`. The earlier audits describe `master` at `50871c432f`;
their source observations are historical references, not updated line numbers
for this branch.

The Windows/Linux startup contract is **OpenGL >=4.3 Core**. Both existing OIT
methods remain. Depth peeling will be deprecated before retirement; PPLL's current
4.4 feature gate remains while a 4.3 implementation is planned and qualified.

## Changes

* Windows requests Core contexts from the configured maximum down to 4.3 and
  no longer falls back to a legacy rendering/upload context. The temporary WGL
  bootstrap context remains necessary for entry-point discovery.
* Linux SDL2 explicitly requests Core, tries 4.6 down to 4.3, and keeps those
  attributes for shared contexts. Failed shared-context creation frees its wrapper.
* GL initialization checks the actual returned version and profile. Rejection logs
  include renderer, vendor, version and profile. Context-creation failure reports
  the minimum requirement to the user.
* Legacy false Core-profile settings migrate to true; Windows/Linux Intel feature
  masks agree. The broad Intel version/GLSL spoof is removed; other Intel feature
  masks remain. Untested Intel drivers are not thereby certified as correct.
* Upload-worker flags are set before construction, so failed context creation can
  disable both texture and media worker use without being overwritten afterward.
* Convex physics-debug drawing uses the existing buffered vertex helper instead of
  client arrays. This establishes Core legality; it is not a new mesh offload path.
* Render-target reuse requires a successful prior allocation and matching primary
  color format as well as dimensions/depth/usage/mip policy. Dimensions are clamped
  before comparison. Failed allocation/completeness checks release partial owned
  resources; failed color creation no longer leaks a texture. Mip state is
  initialized/reset. Borrowed attachments, shared depth and resizing invalidate
  owned-allocation reuse. Existing multiple-color-attachment reuse is preserved.
* The Windows manifest-copy target now depends on UI shader compilation. A clean
  build exposed the missing dependency; executable-only builds are insufficient.
  The resource compiler also now receives its include directory through the CMake
  source property, preserving paths containing spaces.
* `scripts/ci_release_config.py --development` (after its cache/platform arguments)
  validates Release feature parity with RelWithDebInfo and packaging disabled.

## Regression checks

`python scripts/tests/test_render_target.py` compiles the complete production
render-target header/implementation against a fault-injecting GL test driver.
Only project includes are replaced. It exercises matching-request reuse,
same-size format changes, depth/color allocation failures and retry, framebuffer
incompleteness with and without debug checks, effective-size reuse, multiple color
attachments, invalid dimensions, depth-only targets, borrowed color attachments
and borrowed-versus-owned depth. It also checks resource and accounting cleanup.
The test is included in Windows/Linux CI. It does not replace real-driver tests.

`python scripts/perf/gl_core_probe.py` runs a hidden-window Windows test of a
4.3 Core request, required compute/MDI entry points and a shared-texture readback.
Use `--dll <bundled-mesa-opengl32.dll>` to test Zink. This probes the driver,
not the viewer's complete context creation or rendering pipeline.

Observed locally on the AMD Radeon RX 9070 XT:

* Native AMD: actual 4.3 Core, driver `26.9.1.260826`; shared texture verified.
* Bundled Zink: actual 4.6 Core, Mesa `26.3.0-devel`, `00e42c51b1`; shared texture verified.
* Render-target regression checks passed with MSVC 2022.
* Development configuration parity check passed.

Full Windows RelWithDebInfo build and staging passed (MSVC 2022, LTO enabled,
Release-equivalent features, packaging/installer generation disabled). Staged viewer:
`C:/Dev/vulkanstorm/.tmp/glcore-build/newview/RelWithDebInfo/vulkanstorm-bin.exe`.
Runtime libraries, media plugins, Mesa, shaders, skins, fonts and settings are staged.

Native GL and Zink viewer startup smoke runs used separate temporary APPDATA and
LOCALAPPDATA directories, no auto-login, a 4.3 maximum request, an explicit legacy
false Core-profile setting, and automatic exit after 20 seconds. Both exited 0,
created main/shared Core contexts, reported the expected driver/version, and
completed cleanup. Logs are under `.tmp/core-smoke-opengl` and `.tmp/core-smoke-zink`
in the primary workspace. Certificate-expiry warnings occurred in fresh-profile
startup; these checks do not certify login/network behavior. No GL invalid-operation
or framebuffer-incompleteness diagnostics were observed in those startup logs.
Linux compilation/runtime and the broader AMD/NVIDIA/Intel visual matrix still
require qualification. No frame-rate or GPU-offload improvement is claimed here.

## Next milestones

Capture representative in-world correctness/performance baselines, then introduce
persistent geometry and compatible opaque batches before extending per-view GPU
culling/LOD. Keep allocation/overflow behavior explicit during PPLL consolidation.
The soft rendering/decode scheduling policy is unchanged by this milestone.
