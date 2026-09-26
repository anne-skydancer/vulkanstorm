# FreeBSD 15.x native viewer: long-term reference

Status: future work; not an implementation commitment for the current OpenGL milestone.
Recorded: 2026-09-26.
Integration target: feature branches based on `vkstorm-devel`, then integration into `vkstorm-devel`.

## Objective and agreed direction

Produce a native Vulkanstorm viewer for FreeBSD 15.x, distributed as a FreeBSD `.pkg`, using system-installed libraries and Chromium-backed media on a prim (MOAP).

FreeBSD is intended to replace macOS as the third supported platform alongside Windows and Linux. Retire Apple-specific infrastructure as its responsibilities are accounted for; preserve useful shared POSIX code. This is a platform port, not a mechanical replacement of `DARWIN` with `FREEBSD`.

Keep the rendering contract at OpenGL >= 4.3 Core. Do not restore legacy OpenGL compatibility for this port. Preserve the current modernization priorities: correct visuals, performance, and selective CPU-to-GPU offload. Both existing OIT methods remain for now; depth peeling must pass through deprecation before retirement, with PPLL the longer-term sole method.

The FreeBSD work is a separate, longer-term track. This document does not claim a successful FreeBSD build, working MOAP, or FreeBSD 15.x runtime support.

## Reference: Megapahit

Megapahit provides a closely related viewer implementation to study before inventing equivalent portability and packaging work. The following observations were checked against its public `main` source on the date above. Links are moving references, not pinned revisions; pin a commit when implementation begins.

| Area | Observed implementation | Application to Vulkanstorm |
| --- | --- | --- |
| Native build | README documents a FreeBSD build with installed dependencies | Starting point for dependency inventory and platform build behavior |
| System libraries | OpenJPEG uses `pkg_check_modules(... libopenjp2)`; APR uses `apr-1` and `apr-util-1` | Adapt installed-library discovery to Vulkanstorm's imported targets |
| Windowing | The non-Windows/macOS branch selects SDL window and keyboard sources | Study reusable POSIX/SDL behavior and separate Linux-specific assumptions |
| Packaging | `CPACK_BINARY_FREEBSD` and `CPACK_FREEBSD_PACKAGE_*` metadata; README runs `cpack -G FREEBSD` | A direct `.pkg` route worth evaluating before adding a separate Ports recipe |
| Browser media | FreeBSD media subdirectories include example and VLC, but omit CEF | Does not establish a working Chromium MOAP solution |
| Voice | README explicitly excludes WebRTC voice on FreeBSD; viewer CMake excludes its implementation/linking there | Track voice as a separate feature gap, not an incidental build workaround |

Sources:

- [Megapahit FreeBSD build instructions](https://github.com/megapahit/viewer/blob/main/README.md#freebsd)
- [OpenJPEG dependency discovery](https://github.com/megapahit/viewer/blob/main/indra/cmake/OpenJPEG.cmake)
- [APR dependency discovery](https://github.com/megapahit/viewer/blob/main/indra/cmake/APR.cmake)
- [Window source selection](https://github.com/megapahit/viewer/blob/main/indra/llwindow/CMakeLists.txt)
- [Media plugin selection](https://github.com/megapahit/viewer/blob/main/indra/media_plugins/CMakeLists.txt)
- [Viewer linking and FreeBSD package metadata](https://github.com/megapahit/viewer/blob/main/indra/newview/CMakeLists.txt)
- [CEF integration](https://github.com/megapahit/viewer/blob/main/indra/cmake/CEFPlugin.cmake)

Reuse the design selectively. Megapahit's audio choices, dependency versions, packaging workarounds, and feature exclusions are not Vulkanstorm policy. In particular, its FreeBSD build is useful precedent but not proof that our complete target is already solved.

## Vulkanstorm starting points

These observations describe the repository configuration inspected during the discussion; recheck the implementation branch before editing.

- `indra/cmake/Variables.cmake` recognizes Windows, Linux, and Darwin, without an explicit FreeBSD platform branch.
- `indra/cmake/Prebuilt.cmake`: `use_system_binary()` currently handles Conan targets only. Its name does not mean native FreeBSD library discovery already exists.
- `indra/cmake/Linking.cmake` contains Linux-specific libraries and an Apple framework fallback. FreeBSD needs explicit treatment rather than falling into that fallback.
- `indra/cmake/CEFPlugin.cmake` requests prebuilt Dullahan and links CEF and its wrapper. A standalone Chromium executable does not satisfy those link dependencies.
- Windowing, process launching, shared memory, directory discovery, platform statistics, endian definitions, and plugin loading need a targeted portability audit.
- Read `doc/ci_release_parity.md` and the current build configuration before selecting dependencies. Vulkanstorm's established configuration includes OpenJPEG and SoLoud; do not assume upstream KDU or copy Megapahit's OpenAL choices automatically.

## Chromium-backed MOAP: principal unresolved dependency

The required result is an embedded browser that renders web content onto viewer surfaces and receives viewer input, with working audio and browser subprocess management. Launching an external browser is not an implementation of MOAP.

FreeBSD maintains a Chromium browser port. That establishes a useful platform codebase, but does not by itself supply the CEF/Dullahan embedding interface used here.

Investigate these paths before choosing an implementation:

1. Native FreeBSD CEF plus Dullahan, preferably provided as separately managed system packages. Check the CEF version, wrapper ABI, offscreen rendering support, helper executables, resources, and sandbox behavior.
2. Another native Chromium embedding backend, if the existing interface cannot be supported reasonably. This requires an explicit media-plugin adaptation and verification of feature coverage and frame-transfer costs.

Do not claim either path is available or selected yet. Account for browser security updates, version coupling, codecs, cookies, navigation, focus, mouse/keyboard input, audio, crash recovery, and subprocess cleanup. Validate interaction with the viewer's graphics context and GPU workload.

References:

- [FreeBSD Chromium port](https://github.com/freebsd/freebsd-ports/blob/main/www/chromium/Makefile)
- [Dullahan embedded browser wrapper](https://github.com/secondlife/dullahan)
- [Chromium Embedded Framework](https://github.com/chromiumembedded/cef)

## Proposed work sequence

### 1. Pin references and inventory dependencies

Pin the Megapahit revision being studied. Map Vulkanstorm dependencies to FreeBSD base-system or Ports/pkg providers, including required versions, compile options, ABI constraints, and any fork-specific patches. Identify unavailable dependencies explicitly. Decide an initial architecture and exact FreeBSD 15.x release for validation; amd64 is a proposed first target, not a settled restriction.

### 2. Establish native platform and system-library configuration

Add explicit FreeBSD detection and deliberate shared POSIX/SDL paths. Audit Linux-only assumptions instead of defining FreeBSD as Linux. Retain Autobuild orchestration as required by the repository conventions, while preventing the FreeBSD system-library path from silently downloading foreign prebuilt dependencies. Determine whether Autobuild itself needs FreeBSD platform support.

Use dependency discovery that fails clearly on unsupported versions. Preserve Windows/Linux behavior. Keep FreeBSD-specific packaging and dependency policy separate from renderer behavior.

### 3. Prove browser integration early

Build a minimal native embedded-browser proof, then connect it to the viewer media-plugin lifecycle. Verify rendered pixels, input, navigation, audio, resource discovery, sandboxing, and subprocess teardown before treating the complete viewer as achievable with the selected browser dependency.

A viewer-only compile with CEF disabled can be a diagnostic milestone, but does not satisfy this project's MOAP requirement.

### 4. Build and stage a runnable viewer

Use `RelWithDebInfo` for development, with Release-equivalent features and dependencies except installer generation. Stage the executable, application assets, shaders, media plugins, and helper programs needed to run directly. System-managed shared libraries should resolve through declared installed dependencies rather than copied foreign runtime bundles.

Validate on native FreeBSD, including OpenGL >= 4.3 Core, plugin IPC, audio, texture decoding, and representative in-world rendering. A jail can validate build/package isolation; desktop/GPU testing still needs an appropriate runtime host.

### 5. Produce and validate the release package

Evaluate Megapahit's CPack FREEBSD route first. A Ports recipe and Poudriere build may then provide reproducible dependency and package validation; they are not prerequisites implied merely by requesting a `.pkg`.

Define installation paths, package origin, metadata, license notices, dependency declarations, desktop integration, and asset/helper discovery. Produce the release `.pkg` separately from ordinary development staging. Verify installation, upgrade, launch, and removal on a clean FreeBSD 15.x environment.

### 6. Retire obsolete macOS infrastructure

Remove Apple-specific source selection, Objective-C/Cocoa integrations, frameworks, signing, bundle packaging, CI remnants, and obsolete documentation once their shared responsibilities have been identified. Preserve attribution and licensing. Avoid mixing wholesale deletion with unverified portability substitutions.

## Acceptance criteria

- A reproducible native FreeBSD 15.x build using declared system dependencies.
- A development viewer runnable from its staged directory, without an installer.
- A release `.pkg` that installs and runs on a clean target environment with its dependencies resolved.
- OpenGL >= 4.3 Core initialization and representative in-world visual correctness.
- Working Chromium-backed MOAP: content rendering, input, navigation, audio, and reliable plugin/subprocess lifecycle.
- Verified texture decoding and mesh/rendering behavior; no assumed GPU speedup without measurements.
- An explicit feature matrix, including the unresolved voice situation. Missing features must not be hidden by successful packaging.
- Windows/Linux regression checks and accurate build documentation.

## Decisions still required

- Initial CPU architecture, exact 15.x baseline, and available native build/GPU test host.
- Native browser embedding provider and its maintenance/update path.
- Treatment of dependencies absent from Ports or requiring Vulkanstorm-specific patches; any exception to system-library policy needs an explicit decision.
- Voice implementation and whether an initial release can have a documented voice limitation.
- FreeBSD graphics-driver and OpenCL coverage, including whether the planned GPU J2C workflow is available on the target hardware.
- CPack-only distribution versus a maintained Ports recipe and package repository.
- When FreeBSD joins CI release publication. The existing `latest` tag contract remains tied to successful Windows and Linux Release builds until explicitly changed.

Related local references: `doc/vulkan/opengl-core-infrastructure.md`, `doc/vulkan/rendering-pipeline-targeted-audit.md`, and `doc/ci_release_parity.md`.
