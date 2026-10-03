# Vulkan GHI dependencies

`vkstorm-vulkan` prepares DiligentCore's Vulkan backend for Windows/Linux x64.
The viewer still renders with its existing OpenGL pipeline. The disabled Vulkan
entry in Preferences identifies the planned backend; dependencies alone do not
implement scene rendering, UI rendering, resource management, or presentation.

The source lock is `vulkan-dependencies.json`. These are two separate Autobuild
packages, built together so the engine and loader use the same Vulkan headers:

* **3p-vulkan** (`vulkan`): Khronos Vulkan-Headers and Vulkan-Loader API 1.4.365.
  This is not a LunarG SDK distribution and does not include validation layers.
* **3p-diligentcore** (`diligentcore`): current DiligentCore revision
  `bcb8b11eecd0899962c330b798ebe3e786b02bbb`, with its pinned submodules.
  Only the Vulkan backend and its GLSL/SPIR-V tools are built. Other backends,
  tests, HLSL, archiver, and optional upscaling integrations are disabled.

Upstream sources: [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers),
[Vulkan-Loader](https://github.com/KhronosGroup/Vulkan-Loader), and
[DiligentCore](https://github.com/DiligentGraphics/DiligentCore).
Latest API headers do not impose Vulkan 1.4 as the viewer's future GPU minimum.
Device requirements and capability negotiation belong to renderer qualification.

## Build and install

Requirements: Python 3, Git, CMake 3.24+, Autobuild, and a C++ compiler. Windows
uses Visual Studio 2022 x64. Linux uses Ninja, GCC/Clang, and the Vulkan loader's
X11/Wayland development dependencies. Both use RelWithDebInfo.

From the repository root:

```powershell
python scripts/build_vulkan_dependencies.py --install-dir build-vulkan/packages --autobuild .venv/Scripts/autobuild.exe
```

On Linux, omit `--autobuild` when Autobuild is on PATH. The command clones and
verifies the source pins, builds both packages, includes licenses and provenance,
and writes archives plus a SHA-256-pinned `autobuild.xml` under
`.tmp/vulkan-dependencies`. Existing package output is deliberately protected;
use a fresh `--work-dir` for another package build.

To install the same archives into another viewer build directory:

```powershell
python scripts/build_vulkan_dependencies.py --install-only --install-dir build-vulkan/packages --autobuild .venv/Scripts/autobuild.exe
```

Configure the viewer using its normal Autobuild workflow, with
`-DUSE_DILIGENTCORE=ON`, RelWithDebInfo, and installer generation disabled.
The package installation directory must be that build's `packages` directory.
`DiligentCore.cmake` verifies the locked package versions and source revisions,
exposes `ll::diligentcore` and `ll::vulkan`, and validates runtime libraries.
Runtime staging and viewer manifests include both libraries and their licenses.
The default remains OFF until the Vulkan renderer can use them.

The packages are local artifacts, not published releases. The main viewer's
`autobuild.xml` is not populated with nonexistent archive URLs. Publishing both
platforms and promoting their actual hashes to the main manifest is a later step.

## Qualification

The standalone smoke check consumes installed packages and stages their runtime
libraries beside the executable:

```powershell
cmake -S 3p/tests -B .tmp/ghi-smoke -DGHI_PACKAGES=C:/Dev/vulkanstorm/build-vulkan/packages
cmake --build .tmp/ghi-smoke --config RelWithDebInfo
ctest --test-dir .tmp/ghi-smoke -C RelWithDebInfo --output-on-failure
```

Windows x64: both dependency projects compile against API 1.4.365; Autobuild
archive installation and a public-header/engine-factory smoke check are tested.
Linux packaging paths are supplied but have not been built on this Windows host.
No viewer Vulkan rendering, swapchain, GPU/vendor compatibility, performance,
or complete viewer build is qualified by this dependency preparation.
