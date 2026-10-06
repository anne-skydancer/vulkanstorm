# Item 2: independent software Vulkan CI

The [Software Vulkan qualification workflow](../../.github/workflows/software_vulkan.yml)
is separate from production CI because this work introduces a permanent graphics
API dependency and new pipeline logic. It runs on `vkstorm-vulkan`, relevant
development PRs, manual dispatch and the future `vkstorm_1.1.0` name. It has no
release publication permission, installer job or `latest` update.

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
| Windows x64 | SwiftShader, native HWND | Device, rendering/readback, resource replacement, native swapchain/lifecycle and negative probes; staged development viewer |
| Linux x64 | SwiftShader, SDL2 X11 through Xvfb | Same checks; XCB connection obtained from the SDL-owned X11 display |
| Linux x64 | Lavapipe, SDL2 X11 through Xvfb | Independent software implementation, same mandatory checks |

The harness consumes the exact [Autobuild GHI packages](../../scripts/build_vulkan_dependencies.py)
used by the viewer. Native shaders use Diligent's pinned GLSL compiler and Vulkan
backend. The [CI-only dependency lock](../../scripts/vulkan_ci_dependencies.json)
pins SwiftShader, validation layers, required transitive validation dependencies
and a checksummed Mesa source archive. SwiftShader uses its vendored LLVM backend;
unused test/debugger/LLVM-submodule dependencies are disabled. These dependencies
are isolated from the production Autobuild manifest.

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
recipe. A cache hit still checks runtime lock agreement and binary hashes. GHI
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
The executable rejects multiple devices, hardware devices, wrong software
device names and missing validation. It reports actual loaded libraries; the
runner verifies that loader/Diligent came from the test's staged directory.
Core and synchronization validation are required. Shader features and WSI
support are checked by running the mandatory operations, not assumed from an
ICD's advertised API version.

## Test contracts

`tests/software_vulkan/diligent_render_test.cpp` is a standalone test, never
linked into viewer runtime. Rendering calls use DiligentCore; raw Vulkan is
limited to loader/device preflight and isolated intentional-invalid-use probes.

The rendering oracle is an 8x8 RGBA8 target with a 2x2 asymmetric uploaded
texture, nearest sampling, alpha blending and a central scissor. All pixels are
compared against independently computed CPU expectations, including untouched
background and alpha. One UNORM rounding unit is permitted. Both original and
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

A crash, timeout, unrelated error or successful invalid-use process does not
pass a negative test. Positive tests require normal exit, device/library evidence,
readback artifacts and no error diagnostics. Optional untested capabilities
cannot be relabeled as passed or silently substituted with OpenGL.

The viewer is built with Autobuild's `RelWithDebInfoFS_open` configuration,
Release-equivalent dependency/feature choices and packaging disabled. Existing
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

Item 2 is accepted only after every matrix job succeeds, with a fresh dependency
build (cache miss or `clean_dependencies=true`) and a subsequent cached run, and all artifacts
are reviewed. A compiled harness or uploaded workflow alone is insufficient.
Native viewer UI, protocol replay and real connected chat will extend this CI
in subsequent items. Software-device results do not qualify physical devices,
driver performance or live-session behavior.
