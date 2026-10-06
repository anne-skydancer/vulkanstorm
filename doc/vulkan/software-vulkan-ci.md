# Item 2: independent Vulkan implementation CI

The [Vulkan qualification workflow](../../.github/workflows/software_vulkan.yml)
is separate from production CI because this work introduces a permanent graphics
API dependency and new pipeline logic. It runs on `vkstorm-vulkan`, relevant
development PRs, manual dispatch and the future `vkstorm_1.1.0` name. It has no
release publication permission, installer job or `latest` update.
Runs on a branch are serialized without canceling an active qualification when
new fixes are pushed, preserving its test results and diagnostic artifacts.

## Scope and driver selection

CI qualifies the general native Vulkan implementation through software Vulkan
devices. SwiftShader and Lavapipe are test infrastructure, not the viewer's
production driver policy. Assume AMD/NVIDIA hosts are never available: no vendor
runner, physical GPU or vendor-driver result is required for Item 2 acceptance.
The production viewer must discover and use the machine's installed Vulkan
drivers; software ICD isolation belongs only to this standalone CI launcher.
The native viewer backend and its integration remain subsequent work.

The executable's `auto` path enumerates available Vulkan devices, preferring a
discrete GPU, then integrated GPU, then virtual GPU, then another available
device such as a CPU implementation. It passes the selected matching adapter to
Diligent instead of rejecting hardware devices. CI exercises this same path
with each isolated software ICD. A compiled policy test supplies synthetic AMD,
NVIDIA and CPU device descriptions to check selection, software-only discovery,
explicit selection and rejection of an unavailable requested vendor. These are
selection-policy tests, not fabricated vendor execution results.

Every graphics job also runs offscreen drawing/readback and all four negative
probes with no display variables and without Xvfb or a native window. Native
presentation remains a separate test using HWND or Xvfb. Headless results record
`presentation_qualified: false`. Device/API/driver/vendor identifiers and hashes
of actually mapped libraries are archived. This qualifies implementation
correctness at the tested Vulkan contract; it does not establish AMD/NVIDIA
driver-specific behavior, physical-device performance or desktop presentation.

## Branch mandate

`vkstorm-release` is feature-frozen: only security fixes and functionality patches
are accepted. `vkstorm-devel` experiments with improvements excluded from that
baseline. `vkstorm-canary` targets Vulkanstorm 1.0.1, based on upstream 7.2.5;
rename it to `vkstorm_1.0.1` when that upstream version is released.
Continue Vulkan work on the existing `vkstorm-vulkan` branch, referred to as
`vkstorm_vulkan` in the mandate. When development is ready, rename it to
`vkstorm_1.1.0`, with native Vulkan as the default renderer. Neither rename is
performed early. These are development/integration policies, not a claim that
GitHub can automatically determine whether a patch is a functionality fix.

The `vs` filename prefix applies to new viewer `.cpp`/`.h` files. Standalone test
and harness files use descriptive names. Vulkan is a peer backend, without GL
fallback. GL/Zink is the existing GL backend for AMD OpenGL ICD regressions; its
CI results do not qualify native Vulkan.

## Jobs and provenance

| Job | Device and display | Mandatory work |
|---|---|---|
| Source evidence | CPU Python checks | Catalog/boundary checks, audit regressions, accepted XUI fixes, failure-classification tests |
| Windows x64 | SwiftShader, headless plus native HWND | Selection policy, device, rendering/readback, replacement, negative probes, separate native swapchain/lifecycle; staged development viewer |
| Linux x64 | SwiftShader, headless plus SDL2 X11 through Xvfb | Same checks; presentation obtains XCB from the SDL-owned X11 display |
| Linux x64 | Lavapipe, headless plus SDL2 X11 through Xvfb | Independent software implementation, same mandatory checks |

The harness consumes the exact [Autobuild GHI packages](../../scripts/build_vulkan_dependencies.py)
used by the viewer. Native shaders use Diligent's pinned GLSL compiler and Vulkan
backend. The [CI-only dependency lock](../../scripts/vulkan_ci_dependencies.json)
pins SwiftShader, validation layers, required transitive validation dependencies
and a checksummed Mesa source archive. SwiftShader uses its vendored LLVM backend;
unused test/debugger/LLVM-submodule dependencies are disabled. These dependencies
are isolated from the production Autobuild manifest.

GHI runtime and import libraries install under `bin/release/vulkan-ghi` and
`lib/release/vulkan-ghi`. CEF retains ownership of its own loader at the ordinary
package path. When GHI is enabled, viewer staging selects the same pinned loader
as the standalone test; CEF's copy does not overwrite it. The `ghi1` package
revision records this install layout. The default production configuration
continues to use its existing CEF loader.
Windows also retains CEF's private loader beside `dullahan_host.exe` in
`llplugin`; it serves a separate process from the viewer's tested GHI loader.
Linux's shared runtime directory explicitly selects the tested GHI loader.

Qualification exposed an acquisition/layout-transition synchronization failure
in the pinned Diligent swapchain path. The
[hashed dependency overlay](../../3p/3p-diligentcore/acquire-layout-transition.md)
uses a conservative acquire wait covering all commands. The `acquire1` package
version and patch/resulting-source hashes distinguish the tested binary from
unpatched DiligentCore. No performance benefit is claimed.

Linux qualification also exposed unconditional requirements for unused Linux
window systems. The [surface-extension overlay](../../3p/3p-diligentcore/available-linux-surfaces.md)
enables advertised XCB/Xlib/Wayland extensions and rejects an unsupported surface
when requested. The `wsi1` package revision retains mandatory X11 presentation;
it does not count Wayland as tested.

Runtime cache entries include ICD/layer manifests, binaries, hashes and licenses,
not source/build trees. Keys include host image, platform, driver, pins and build
recipe, including the included Diligent CMake helper and dependency overlays.
A cache hit still checks runtime lock agreement and binary hashes. GHI
archives are reinstalled through Autobuild, not copied around its metadata checks.
System compiler and development packages come from the runner; their actual
versions are recorded. This is repeatable testing with recorded environments,
not a claim of bit-for-bit reproducible builds across changed OS repositories.

The runner sets `VK_DRIVER_FILES` to one ICD and an isolated validation layer
path. Elevated Windows loaders ignore these environment overrides. On the
hosted Windows runner only, the runner temporarily registers the pinned ICD and
layer manifests in the machine's Vulkan registry keys, restoring any previous
values after the tests. Binaries stay in the isolated staging directory. No
registration is permitted on local or self-hosted machines by this option.
The launcher verifies the isolated software device identity; the executable
supports normal available-device selection as well as explicit selectors.
Missing devices and validation fail decisively. It reports actual loaded libraries; the
runner verifies that loader/Diligent came from the test's staged directory.
Core and synchronization validation are required. Shader features and WSI
support are checked by running the mandatory operations, not assumed from an
ICD's advertised API version.

## Test contracts

`tests/software_vulkan/diligent_render_test.cpp` is a standalone test, never
linked into viewer runtime. Rendering calls use DiligentCore; raw Vulkan is
limited to loader/device preflight and isolated intentional-invalid-use probes.

The rendering oracle is an 8x8 RGBA8 target with a 2x2 asymmetric uploaded
texture, nearest sampling, alpha blending and an asymmetric scissor
(left=1, top=2, right=6, bottom=7). All pixels are
compared against independently computed CPU expectations, including untouched
background and alpha. Interpolated vertex texture coordinates check Diligent's
top-row convention (+NDC Y, texture V=0), rather than sampling by fragment
position. One UNORM rounding unit is permitted. Both original and
replacement generations are copied to separate staging textures. The test drops
caller-owned texture, binding and pipeline references after submission, submits
the replacement, then waits and checks both generations. PPMs and diagnostics
are retained even when comparison fails.

Native presentation exercises clear/present, resize, a skipped minimized frame,
restore, resource completion and destruction on Win32 and SDL2/X11. Minimize
events on a virtual display do not establish every desktop/window-manager
behavior, nor does this test establish DPI handling or full viewer UI correctness.
No GL window/context or GL rendering is used by the harness.

Negative processes must fail normally with their expected diagnostic:

* Zero-sized buffer: `VUID-VkBufferCreateInfo-size-00912` proves core validation
  is loaded and reports invalid usage. Its callback requests abort before driver
  dispatch; the probe requires `VK_ERROR_VALIDATION_FAILED_EXT`, avoiding an
  invalid driver call that Mesa's asserted builds reject.
* Consecutive buffer fills without a barrier: a write-after-write synchronization
  hazard proves synchronization validation is actually enabled.
* Deliberately wrong expected pixel: oracle mismatch proves image failures are
  decisive rather than ignored.
* Deliberately flipped vertex texture V: the same CPU oracle must reject reversed
  texture orientation, independently of the pixel-expectation corruption probe.

A crash, timeout, unrelated error or successful invalid-use process does not
pass a negative test. Positive tests require normal exit, device/library evidence,
readback artifacts and no error diagnostics. Optional untested capabilities
cannot be relabeled as passed or silently substituted with OpenGL.

The viewer uses Autobuild's `RelWithDebInfoFS_open` environment and the existing
CMake `use_prebuilt_binary()` installer, selecting only enabled dependencies.
This avoids unconditional installation of disabled FMOD/Kakadu packages with
local-only SDK URLs. Configuration follows production CI's CMake path with
RelWithDebInfo, Release-equivalent dependency/feature choices and packaging disabled. Existing
manifest copy targets stage libraries, plugins, shaders, fonts and XUI without
an installer. A staging checker verifies required assets and that the viewer's
GHI library bytes match those tested. It does not launch the existing GL viewer
and count that as a native Vulkan pass.

## Reproduce and qualify

From an x64 Windows/Linux development environment with the workflow's tools and
Linux development packages installed:

```text
python scripts/build_vulkan_dependencies.py --work-dir .ci/ghi --install-dir .ci/test-packages
python scripts/build_software_vulkan.py --work-dir .ci/runtime --driver swiftshader
cmake -S tests/software_vulkan -B .ci/test-build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DAUTOBUILD_INSTALL_DIR=<absolute .ci/test-packages>
cmake --build .ci/test-build --config RelWithDebInfo --parallel 3
```

On Windows run `scripts/run_software_vulkan_tests.py` with the executable under
`.ci/test-build/RelWithDebInfo`; on Linux use `.ci/test-build/diligent_render_test`
under `xvfb-run -a`, passing `--runtime .ci/runtime/runtime.json --evidence .ci/evidence`.
Use `--driver lavapipe` for the Linux cross-check. The workflow contains complete
viewer Autobuild/staging commands and archives source, package, image/toolchain,
runtime, diagnostic, result, readback and staging evidence.

Add `--headless` to the runner for the five no-window modes; this clears display
variables and omits presentation. `ctest --test-dir .ci/test-build -C RelWithDebInfo
--output-on-failure` runs the device-selection policy checks without a GPU.

For normal machine-driver discovery outside CI, build validation-only runtime
metadata in a fresh directory:

```text
python scripts/build_software_vulkan.py --work-dir .ci/system-runtime --driver installed
python scripts/run_software_vulkan_tests.py --executable <staged diligent_render_test> --runtime .ci/system-runtime/runtime.json --evidence .ci/system-evidence --headless
```

This path builds the pinned validation layer but installs no ICD, sets no driver
manifest override and requires no specific vendor. An optional `--icd` (and
`--icd-library` for a manifest soname) explicitly isolates an installed driver
and records its manifest/library hashes without copying its vendor stack.
Optional `--vendor` and `--device-name` narrow diagnostic selection; none is an
acceptance requirement. Hosted-only Windows registry registration is unavailable
in this mode; use an unelevated local process so isolated layer discovery works.

Item 2 is accepted only after every matrix job succeeds, with a fresh dependency
build (cache miss or `clean_dependencies=true`) and a subsequent cached run, and all artifacts
are reviewed. A compiled harness or uploaded workflow alone is insufficient.
Native viewer UI, protocol replay and real connected chat will extend this CI
in subsequent items. Software-device results do not qualify physical devices,
driver performance or live-session behavior.

## Qualification record

### Dependency-lock checklist (Item 1)

Item 1 is satisfied. Diligent/Vulkan pins and hashed overlays are retained in
`3p/vulkan-dependencies.json`. SwiftShader and validation use immutable Git
objects; validation's transitive revisions come from its pinned `known_good`
file with immutable tag-object substitutions. Lavapipe uses Mesa 25.2.4 with an
explicit archive SHA-256. Generated Autobuild archives and staged runtimes record
SHA-256 hashes, source/package revisions and licenses. CI archives actual runner,
compiler/build-tool and package metadata. Cache keys cover both locks, build
scripts/options, the workflow, included Diligent CMake helpers and patch files,
alongside platform, driver and runner image. The helper/patch inputs were added
after the checklist audit found that the included CMake helper was omitted from
the key; dependency sources, build options and binaries are unchanged.

### Standalone Diligent executable checklist (Item 3)

Item 3 is satisfied by source audit and review of the fresh/cached evidence in
37445355027 and 37445347375. The qualified executable source is
`957481d40710848516ae04696469a5f0d86bbefd`; the test sources, GHI imports, dependency
lock and test launcher remain byte-identical at the subsequent cache-key audit
commit `350db290e99725cf222f8d68d19a5d72401c5b81`.

| Requirement | Implementation and acceptance evidence |
| --- | --- |
| Standalone executable and naming | `tests/software_vulkan/diligent_render_test.cpp` has its own CMake target and is outside viewer runtime. The user's filename clarification reserves `vs` for new viewer files; standalone tools keep descriptive names. |
| Same Autobuild libraries as the viewer | The target imports `ll::diligentcore` and `ll::vulkan` through the viewer's `DiligentCore.cmake`, using its supplied Autobuild install directory. The same archives install into test and viewer directories; staging evidence matches loader/engine SHA-256 hashes and logs identify the actually loaded staged libraries. |
| Device, shaders and pipeline | The factory creates the selected Vulkan device/context. `pipeline()` compiles GLSL vertex/fragment shaders and creates a textured, blended, scissored graphics pipeline; missing objects and diagnostics fail the process. |
| Upload, textured draw and readback | `draw()` uploads a 2x2 RGBA texture, binds it through a shader-resource binding and immutable nearest sampler, draws into an 8x8 RGBA8 target and copies into a CPU-readable staging texture. `compare()` maps that texture and checks every RGBA channel. |
| Blending, clipping and orientation | The independent CPU oracle computes source-alpha color blending over the known background, checks alpha and untouched pixels, and applies asymmetric bounds (1,2)-(6,7). Asymmetric texture quadrants and interpolated UVs exercise orientation. One UNORM rounding unit is allowed; observed RGB readbacks match exactly across all three configurations and cached runs. |
| Replacement and teardown | Two generations recreate upload textures, bindings, shaders and pipelines. Caller-owned draw resources leave scope after submission and before the explicit idle wait; separate readback textures preserve both generations for comparison. Device/context/texture ownership leaves scope before the final diagnostic assertion and successful process exit. |
| Decisive oracle failure | `bad-pixels` and `bad-orientation` each exit normally with code 1 and the expected pixel-oracle mismatch. Both probes passed in headless and presentation-suite evidence on Windows SwiftShader, Linux SwiftShader and Linux Lavapipe. |

Reviewed file SHA-256 hashes:

* `tests/software_vulkan/CMakeLists.txt`: `aa6ff80d6e1450aa7593229e9a15d67f7248661f583d8a82d28e1ecbd66884c8`.
* `tests/software_vulkan/diligent_render_test.cpp`: `60a36ed066b336299be29d0120a9cd28543dcd4d65424abdb4d0ea18a809b326`.

Evidence covers this deterministic RGBA8 fixture and normal resource lifecycle,
not every shader/texture format or stress under prolonged GPU load. PPM artifacts
contain RGB; alpha is checked in-process by the RGBA oracle. No hardware host is
required, and no vendor-specific execution or native viewer UI/chat result is
claimed. Item 4's native presentation contract remains a separate acceptance
audit; offscreen success does not substitute for it.

### General headless implementation qualification

The extension at `957481d40710848516ae04696469a5f0d86bbefd` has now passed fresh
and cached qualification with artifact review. No AMD/NVIDIA host is required.
Both [37445355027](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37445355027)
and [37445347375](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37445347375)
concluded successfully at that source: 64 Python regressions, compiled selection
policy checks, six presentation/offscreen modes, five no-display modes and full
viewer staging on all three configurations.

| Configuration | Fresh dependencies | Cached dependencies |
| --- | --- | --- |
| Windows SwiftShader | 37445347375, original successful Windows job | 37445355027 |
| Linux Lavapipe | 37445347375, original successful Lavapipe job | 37445355027 |
| Linux SwiftShader | 37445355027 | 37445347375, rerun of the interrupted job |

The original Linux SwiftShader job in 37445347375 was interrupted by a runner
shutdown during validation-layer compilation (exit 143), without a compiler
diagnostic. The follow-up built its dependencies from a cache miss and saved
them; the rerun restored that cache and passed. The original successful Windows
and Lavapipe jobs were retained in the completed rerun.

Artifact review verified lock/package revisions, toolchain metadata, runtime
license hashes (21 Windows SwiftShader, 20 Linux SwiftShader, 11 Lavapipe), actual
pinned ICD/layer mappings, viewer/test GHI byte identity and complete staging.
Runtime and GHI hashes match between fresh/cached evidence, and every positive
readback matches the first/replacement hashes below across headless and
presentation runs on all three configurations. Headless evidence explicitly
does not qualify presentation; native viewer runtime remains unqualified.

### Earlier software-only qualification

The earlier software-only infrastructure passed runs and artifact review on
2026-10-06 at `1f146db5fc7ef845ec918a929bfb3baaff639a74`. Calling that complete
Item 2 acceptance was premature: it omitted generic device selection and a
separate no-display execution contract. The scope above corrects that omission
without imposing an unavailable hardware-host requirement. The extension's
completed qualification is recorded above. Local Windows
verification passed the compiled selection-policy test, all six existing modes
and all five headless modes using the previously built pinned SwiftShader runtime.
The earlier run evidence below remains valid for the earlier source.
Both the [complete matrix, run 37417749930](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37417749930)
and the [subsequent cached matrix, run 37420913445](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37420913445)
passed their source checks, all 59 regressions and every graphics job at that
same source commit.

| Configuration | Fresh dependency qualification | Cached qualification |
| --- | --- | --- |
| Windows SwiftShader | [37401120832](https://github.com/anne-skydancer/vulkanstorm/actions/runs/37401120832): cache miss, six native checks and full staging passed | 37417749930 and 37420913445: cache restored, six native checks and full staging passed |
| Linux SwiftShader | 37417749930: cache miss, six native checks, full staging and cache save passed | 37420913445: cache restored, six native checks and full staging passed |
| Linux Lavapipe | 37417749930: cache miss, six native checks, full staging and cache save passed | 37420913445: cache restored, six native checks and full staging passed |

The fresh Windows dependency build predates the portable viewer `llisnan`
assertion and staging-checker corrections; its SDK recipe, dependency cache key
and native test code are unchanged. Both complete matrices qualified the
corrected viewer and checker at the source commit above.

Artifact comparison verified runtime and GHI library hash identity between fresh
and cached runs for each configuration, the loaded pinned ICD and validation
layer, and byte-identical GHI libraries in the standalone test and viewer stages.
Runtime license hashes were checked against the uploaded files: 21 for Windows
SwiftShader, 20 for Linux SwiftShader and 11 for Lavapipe. Uploaded GHI license
files also matched across runs. CEF's private Windows loader remains separate
from the viewer's tested GHI loader.

Both positive modes (offscreen and presentation) produced identical first and
replacement RGB readbacks across all three configurations and their cached runs.
Their SHA-256 hashes are:

* First: `f1278a678c65167426e821bc1ae125df070cc005909c79b3dc14454167f3625a`.
* Replacement: `15dcc45a3bcac94696fdabc82b2d811dbc4ababe2b38ec268c0fc8babf583099`.

The CPU oracle separately checks every RGBA channel, including blending,
asymmetric clipping and texture orientation. Core-validation, synchronization,
wrong-pixel and reversed-orientation negative probes all failed normally with
their expected diagnostics. Full RelWithDebInfo viewer compilation and staging
passed, including required assets, CEF, plugin host and the exact platform media
plugin names; staging evidence records the required binary hashes.

These runs qualify the earlier independent software Vulkan CI and standalone
Diligent infrastructure. The staging evidence retains
`native_viewer_runtime_qualified: false`: native viewer UI/chat, live sessions,
physical devices, Wayland and performance remain outside this qualification.
