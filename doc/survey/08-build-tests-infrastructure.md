# 08 — Build, dependencies, tests and port infrastructure

This chapter covers how the Vulkanstorm viewer is built, packaged, tested and released, and the infrastructure the incremental Rust port depends on. The viewer is built with Autobuild and CMake. It pulls 73 prebuilt third-party packages declared in `autobuild.xml`. One of them, `vkstorm-ffi`, is the Rust port's static library, built in vkstorm-core-rust's CI and pinned by URL and hash. Viewer CI (`.github/workflows/build.yaml`) builds Windows and Linux and runs 22 focused integration tests, 18 of which are Rust-port tests (parity, oracle or corpus tests against frozen C++ or recorded fixtures). vkstorm-core-rust's own CI builds and publishes the FFI package but runs no `cargo test`. Today the Rust port can only be consumed as AVX2, Second Life-only, Windows/Linux x64 packages, and on main the inventory crate for the next rung is an empty skeleton.

Snapshot: viewer checkout `C:\Users\annes\source\repos\vulkanstorm`, branch `codex/rust-inventory-model` at `8d101eccb6` (identical to `origin/vkstorm-rust`); vkstorm-core-rust `main` at `1b183c7`. Survey date 2026-10-27. Counts were taken with the commands shown, excluding the `build-vc180-64` build tree. Nothing was built or run for this chapter.

---

## Build system

### Layers

| Layer | Entry point | Role |
|---|---|---|
| Autobuild 3.9.3 | `autobuild.xml` (4451 lines) | Package manager for the 3p prebuilts (`installables`) and a configuration table (`package_description.platforms.*.configurations`) that runs CMake or `scripts/configure_firestorm.sh`. CI pins `autobuild==3.9.3` (`build.yaml:112`). |
| Firestorm wrapper | `scripts/configure_firestorm.sh` (701 lines) | Turns `--avx2 --lto --no-opensim --zink --package --vksaudio ...` flags into `-D` options (lines 484–566) and runs `cmake -G ... ../indra` (line 645). It always passes `-DLL_TESTS:BOOL=OFF` (line 646). |
| Linden build-farm script | `build.sh` (556 lines) | Upstream TeamCity/"shared buildscript" driver (`pre_build`, `build`, `package_llphysicsextensions_tpv`). Neither the vkstorm workflows nor the documented local loop use it. |
| CMake ≥ 3.16 | `indra/CMakeLists.txt` (225 lines) | Root project. `CMAKE_CXX_STANDARD 20` (lines 38–41). Options `OPENSIM` (60), `TESTBUILD` (83), `USE_AVX_OPTIMIZATION`/`USE_AVX2_OPTIMIZATION` (101–115, which also add a compile definition of the same name). `add_subdirectory` for the 20 libraries (128–164), `test` when `LL_TESTS` (166–168), `newview` (205), `integration_tests` (220–224). |
| CMake modules | `indra/cmake/` (97 `*.cmake` files) | One module per dependency (`APR.cmake`, `CURL.cmake`, ...), each defining an `ll::<name>` INTERFACE IMPORTED target, plus the common flags, test macros and the Rust bridge. |

### Autobuild configurations (`autobuild.xml`)

Configuration names from `package_description.platforms`. Parsed with a short `xml.etree` script over `autobuild.xml`.

| Platform | Configurations |
|---|---|
| common | RelWithDebInfo, RelWithDebInfoOS, Release, ReleaseOS, ReleaseFS, ReleaseFS_AVX, ReleaseFS_AVX2, ReleaseFS_open, RelWithDebInfoFS, RelWithDebInfoFS_open |
| windows64 | RelWithDebInfo, RelWithDebInfoOS, Release, ReleaseOS, RelWithDebInfoFS, RelWithDebInfoFS_open, ReleaseFS (default), ReleaseFS_AVX, ReleaseFS_AVX2, ReleaseFS_open |
| linux64 | Release, ReleaseOS, default, RelWithDebInfoFS, RelWithDebInfoFS_open, ReleaseFS (default), ReleaseFS_open |
| darwin64 | RelWithDebInfo, RelWithDebInfoOS, Release, ReleaseOS, RelWithDebInfoFS, RelWithDebInfoFS_open, ReleaseFS, ReleaseFS_open |

- The `common` `RelWithDebInfo`/`Release` entries set `-DUSE_VKS_AUDIO:BOOL=ON` and turn OpenAL and FMOD off. The `*FS*` entries delegate to `configure_firestorm.sh --config/--build`.
- All `common` `*FS*` entries pass `--opensim`. On this line `indra/cmake/Rust.cmake:59–61` stops package builds with `FATAL_ERROR` when `OPENSIM` is set. So `autobuild configure -c ReleaseFS_open` only works with `--no-opensim` appended, as `doc/rust_port.md:1884` does.
- `windows64 ReleaseFS*` adds `--kdu`. With `USE_KDU` on, `indra/llimagej2coj/CMakeLists.txt:5` skips the Rust JPEG 2000 library entirely.

### CI configure versus autobuild

CI does not run `autobuild configure`. It evaluates `autobuild source_environment -c ReleaseFS_open`, then calls `cmake -S indra -B <dir>` directly with a fixed option set (`build.yaml:135–172`):

- **Windows:** `LL_BUILD='/MD /O2 /Ob2 /std:c++20 /permissive- ... /DLL_RELEASE=1 /DNDEBUG ...'` (line 146), `-G 'Visual Studio 17 2022' -A x64 -DUSE_MESAZINK=ON -DUSE_INNOSETUP=ON`.
- **Both platforms:** `-DOPENSIM=OFF -DINSTALL_PROPRIETARY=OFF -DUSE_KDU=OFF -DUSE_VKS_AUDIO=ON -DUSE_OPENAL=OFF -DUSE_FMODSTUDIO=OFF -DUSE_AVX2_OPTIMIZATION=ON -DUSE_LTO=ON -DUSE_PRECOMPILED_HEADERS=ON -DUSE_DISCORD=OFF -DUSE_BUGSPLAT=OFF -DUSE_TRACY=OFF -DUSE_VELOPACK=OFF -DLL_TESTS=OFF`.
- **Release builds:** `scripts/ci_release_config.py` (41 lines) re-reads `CMakeCache.txt` and fails unless that exact feature set is present (lines 15–22). Its `--development` mode checks the RelWithDebInfo variant.

### Common flags (`indra/cmake/00-Common.cmake`, 249 lines)

| Area | Lines | Content |
|---|---|---|
| `LL_BUILD` | 3–10 | `$ENV{LL_BUILD}` is appended to `CMAKE_CXX_FLAGS`. On Windows, `/MD`/`/MT` are stripped, because CRT selection belongs to `MSVC_RUNTIME_LIBRARY` (CMP0091). |
| LTO | 60–63 | `USE_LTO` (default OFF) sets `CMAKE_INTERPROCEDURAL_OPTIMIZATION ON`. |
| Configurations | 66 | `CMAKE_CONFIGURATION_TYPES "RelWithDebInfo;Release"`. No Debug or MinSizeRel. |
| MSVC | 69–117 | Static libs (`BUILD_SHARED_LIBS OFF`), `/LARGEADDRESSAWARE /NODEFAULTLIB:LIBCMT`, `/Zo /GS /TP /W3 /Zc:forScope /Oy- /Oi /Ot /fp:precise /MP /permissive-`. |
| AVX | 119–130 | MSVC only: `/arch:AVX` or `/arch:AVX2`. |
| Warnings as errors | 133–135, 242–244 | `/WX` unless `VS_DISABLE_FATAL_WARNINGS`. `-Werror` on GCC/Clang unless disabled. |
| Linux | 148–202 | `-fexceptions -fno-math-errno -fno-strict-aliasing -fsigned-char -msse2 -mfpmath=sse -pthread -fvisibility=hidden -fno-stack-protector`, `_FORTIFY_SOURCE=2` when the compiler does not already set it, rpath `$ORIGIN:$ORIGIN/../lib`. |
| macOS | 204–214 | `-g`, `GL_SILENCE_DEPRECATION`, signing identity. |

Finding: no CMake module passes `-mavx2` or `-march` to GCC/Clang. The only hit for `mavx|march=` is the 32-bit `-march=pentium4` at line 195. On Linux, `USE_AVX2_OPTIMIZATION` therefore only adds a preprocessor definition (`indra/CMakeLists.txt:111`), while the Rust package for linux64 is compiled for `x86-64-v3`. Inference: Linux C++ is built at the SSE2 baseline unless `LL_BUILD` from `FirestormViewer/fs-build-variables` (pinned at `c17312d3`, `build.yaml:103–109`, not in this tree) adds it.

### Variables, prebuilt mechanism, PCH

- **`indra/cmake/Variables.cmake`** (218 lines) defines:
  - the platform flags `WINDOWS`, `LINUX` and `DARWIN` (lines 8–10, 105–144);
  - `LL_TESTS` (default OFF, line 36);
  - `AUTOBUILD_INSTALL_DIR = ${CMAKE_BINARY_DIR}/packages` and `LIBS_PREBUILT_DIR` (56–58);
  - `USE_PRECOMPILED_HEADERS` (default ON, line 199).

  There is no FreeBSD platform flag.
- **`indra/cmake/Prebuilt.cmake`** (72 lines): the `use_prebuilt_binary(<name>)` macro (lines 25–58) runs `autobuild install --install-dir=${AUTOBUILD_INSTALL_DIR} <name>` at configure time. It runs again only when `autobuild.xml` is newer than `packages/cmake_tracking/sentinel_installed` or when the previous attempt failed. `use_system_binary` (61+) serves the Conan path. Packages are therefore fetched by CMake configure, not by `autobuild install`. CI caches `.autobuild-installables` keyed on `hashFiles('autobuild.xml')` (`build.yaml:129–133`).
- **Precompiled headers:** one PCH, for the viewer executable only. `indra/newview/CMakeLists.txt:2277–2281` calls `target_precompile_headers(... llviewerprecompiledheaders.h)` and `vs_target_textual_pch` from `CompilerCacheHeaders.cmake`.

### The Rust bridge (`indra/cmake/Rust.cmake`, 102 lines)

Six libraries `include(Rust)`: llaudio:10, llcommon:15, llfilesystem:7, llimage:9, llimagej2coj:12 and llmath:7 (line numbers in each `CMakeLists.txt`). Eight link `vkstorm_ffi`: llaudio:73, llcommon:322, llcorehttp:73, llfilesystem:58, llimage:64, llimagej2coj:31, llmath:108 and llmessage:195. llcorehttp and llmessage get the module transitively.

| Mode | Lines | Behaviour |
|---|---|---|
| Package (default) | 56–94 | Refuses `OPENSIM` (59–61) and any build without `USE_AVX2_OPTIMIZATION` (62–64). Then `use_prebuilt_binary(vkstorm-ffi)` and `find_library(vkstorm_ffi.lib / libvkstorm_ffi.a)` create an IMPORTED STATIC `vkstorm_ffi`. The include dir is `${LIBS_PREBUILT_DIR}/include`. The system libraries rustc reported come from `vkstorm_ffi.native-libs.txt` (78–93). |
| Source (`-DVKS_CORE_RUST_DIR=<checkout>`, development only) | 18–55 | FetchContent clones Corrosion `v0.6.1` from GitHub at configure time (19–26). `corrosion_import_crate(... CRATES vkstorm-ffi PROFILE release FEATURES full[;opensim])` follows (28–41). `RUSTFLAGS` is set from the AVX switches: `x86-64-v3`, `x86-64-v2 +avx` or `x86-64` (48–55). |
| Both | 96–102 | Links `ll::libcurl` (with OpenSSL, nghttp2 and zlib-ng), because vkstorm-http binds libcurl by symbol. |

The source mode uses Cargo profile `release`. Published packages use `ffi-package`: fat LTO, 1 codegen unit, `debug = "line-tables-only"` (`vkstorm-core-rust/Cargo.toml` `[profile.ffi-package]`). A local source build therefore does not reproduce the shipped library's codegen.

### Audio backend selection (`indra/cmake/VKSAudio.cmake`, 27 lines)

`USE_VKS_AUDIO` defaults ON unless FMOD or OpenAL is requested (8–12). Combining it with either is a `FATAL_ERROR` (14–16). It also fails on anything other than Windows/Linux x64 (19–21). It pulls the `ffmpeg` package, which the engine loads at run time and does not link (22–24). `indra/llaudio/tests/audio_backend_selection.cmake` tests these rules.

### Staging and packaging

- **`indra/newview/viewer_manifest.py`** (2731 lines) contains `ViewerManifest` (72), `Windows_x86_64_Manifest` (597), `Darwin_x86_64_Manifest` (1401), `LinuxManifest` (2305), `Linux_i686_Manifest` (2547) and `Linux_x86_64_Manifest` (2628). `newview/CMakeLists.txt` invokes it with `--vksaudio=${USE_VKS_AUDIO}` and `--avx2=${USE_AVX2_OPTIMIZATION}` (lines 2397–2919).
- **Vulkanstorm additions:**
  - FFmpeg runtime: Windows `avcodec-63.dll` (741–743); Linux `libavcodec.so*`, `libavutil.so*`, `libswresample.so*` (2408–2414); licence `ffmpeg.txt` (801–804, 2413).
  - Mesa Zink runtime into `mesa\` / `lib/mesa` when `--mesazink=on` (747–750, 2322–2327).
  - Inno Setup: `inno_file_commands` (1025) and `inno_package_finish` (1046–), template `indra/newview/installers/windows/installer_template.iss`. The NSIS template `installer_template.nsi` and its `lang_*.nsi` files are still in the tree.
- **Inno Setup:** `indra/cmake/InnoSetup.cmake` (7 lines) defaults `USE_INNOSETUP` to ON when `WINDOWS AND PACKAGE`. `doc/windows_inno_installer.md` (9 lines) records the payload manifest and version-resource rules. Its local qualification note says installer installation, upgrade and uninstallation behaviour were not established. CI installs Inno Setup with `choco install innosetup` (`build.yaml:124–127`), and `scripts/tests/test_inno_payload.py` checks the payload (`build.yaml:116`).
- **Finding:** `viewer_manifest.py` stages no `vkstorm-ffi` licence files. Grepping it for `vkstorm|third-party` returns nothing, and the explicit `LICENSES` copies cover only `websocketpp.txt`, `lsl_definitions.txt`, `luau.txt`, `ffmpeg.txt` and `mesazink.txt`. The package ships `LICENSES/vkstorm-core-rust.txt` and `LICENSES/third-party.txt`, the latter covering the crates compiled in, including the MPL-2.0 symphonia crates. These reach `packages/LICENSES/` but not the installed viewer.

### Platform status

| Platform | Status |
|---|---|
| Windows x64 | Primary. CI uses `windows-2022` with VS 2022 (`AUTOBUILD_VSVER 170`, directory `build-vc170-64`). The local tree is `build-vc180-64`. `vkstorm-ffi` windows64 archive pinned. |
| Linux x64 | CI uses `ubuntu-24.04`, gcc-14 (`build.yaml:91–101`), Visual Studio generator not used. Directory `build-linux-x86_64`. `vkstorm-ffi` linux64 archive pinned. Mesa Zink loader tests run under xvfb (`build.yaml:174–190, 227–237`). |
| macOS | Not buildable on this line as configured. `vkstorm-ffi` has no darwin64 archive, `VKSAudio.cmake:19–21` rejects non-Windows/Linux, and package-ffi's matrix has no macOS runner. `build_sl_mac_only.yml` is an inherited Firestorm workflow triggered by `Firestorm*.*.*` branches. |
| FreeBSD | Runner-image work only. `.github/workflows/freebsd-image.yaml` builds a FreeBSD 15.1 QCOW2 image and checks a toy C++20/CMake/Ninja program (`scripts/ci/freebsd/README.md`). `Variables.cmake` has no FreeBSD platform. Autobuild needs a local patch (`scripts/ci/freebsd/patch_autobuild.py`). No viewer build exists. |

---

## Third-party dependencies

The viewer's `autobuild.xml` declares **73 installables**. The table below was produced by parsing `installables` (name, version, platform keys, license) and matching each against `use_prebuilt_binary(...)` calls in `indra/cmake/*.cmake`. It was then mapped to consumers through each `ll::<target>` reference in `indra/*/CMakeLists.txt`, or through `include(<Module>)` where the module defines no target.

Rust-port status uses these terms:

| Term | Meaning |
|---|---|
| **replaced** | No longer used by the viewer. |
| **partly** | Rust took over some uses; some remain. |
| **kept** | Untouched by the port. |
| **new** | Added by the port. |
| **unused** | No `use_prebuilt_binary` references it. |

| Package | Version | Platforms | License | CMake (line) | Consumers | Rust port |
|---|---|---|---|---|---|---|
| SDL2 | 2.24.1 | linux64 | zlib | LLWindow:33 | llwindow | kept |
| apr_suite | 1.7.5-18696779749 | darwin64, linux64, windows64 | apache | APR:9 | llcommon | kept |
| boost | 1.86 | d, l, w | boost 1.0 | Boost:13 | llcommon (transitively all) | kept |
| breakpad | 2.48.0 | linux64 | BSD | bugsplat:37 | newview (crash reporting; off in CI) | kept |
| bugsplat | 5.0.1.0-71fc41e | darwin64, windows64 | Proprietary | bugsplat:17 | newview (`USE_BUGSPLAT=OFF` in CI) | kept |
| colladadom | 2.3.0-r10 | d, l, w | SCEA | LLPrimitive:23 | llprimitive | kept |
| cubemaptoequirectangular | 1.1.0 | d, l, w | MIT | CubemapToEquirectangularJS:2 | newview (assets) | kept |
| curl | 7.54.1-13259824618 | d, l, w | curl | CURL:9 | llcorehttp; `vkstorm_ffi` (Rust.cmake:102) | kept: vkstorm-http drives libcurl by hand-written bindings |
| dictionaries | 1.a01bb6c | common | various open source | Hunspell:6 | llui/newview data | kept |
| discord-rpc | — | d, l, w | MIT | Discord:21 | newview (`USE_DISCORD=OFF` in CI) | kept |
| discord_sdk | 1.4.9649.16733550144 | darwin64, windows64 | discord_sdk | Discord:11 | newview | kept |
| dullahan | 1.26.0…chromium-139.0.7258.139 | d, l, w | MPL | CEFPlugin:8 | media_plugins/cef | kept |
| emoji_shortcodes | 15.3.2.10207138275 | common | MIT | ViewerMiscLibs:23 | newview data | kept |
| expat | 2.6.4-r1 | d, l, w | expat | EXPAT:8 | llxml; llcommon links it only for its include directory (`llcommon/CMakeLists.txt:313–315`) | partly: the XML LLSD parser is Rust (vkstorm-llsd) |
| ffmpeg | 9.0.2.39711573873 | linux64, windows64 | LGPL-2.1-or-later | VKSAudio:24 | loaded at run time by vkstorm-audio (HE-AAC, Opus) | **new** (rung six) |
| fltk | 1.3.5 | linux64 | LGPL/fltk | UI:9 | linux_crash_logger, llwindow (`ll::uilibraries`) | kept |
| fmodstudio | 2.02.20 | d, l, w | fmod | FMODSTUDIO:28 | llaudio, newview (optional backend, off) | kept (option) |
| freetype | 2.13.3-r3 | d, l, w | FreeType | FreeType:12 | llrender | kept (pulls libpng, FreeType.cmake:4–6) |
| glext | 68 | common | Khronos | GLEXT:7 | llwindow | kept |
| glh_linear | 1.0.1-dev4 | common | BSD | GLH:7 | headers via GLEXT.cmake | kept |
| glib | 2.48.0 | linux64 | LGPL | GLIB:5 | media_plugins/gstreamer10, newview (Linux) | kept |
| glm | v1.0.1 | common | MIT | GLM:7 | llprimitive, llwindow | kept |
| glod | 1.0pre3.532346 | d, l, w | GLOD 1.0 | GLOD:7 | newview | kept |
| gntp-growl | — | d, l, w | bsd | Growl:11 | newview | kept |
| gstreamer10 | 0.10.6.294903 | linux64 | LGPL | GStreamer10Plugin:12 | media_plugins/gstreamer10 | kept |
| havok-source | 2012.1-2 | d, l, w | havok | Havok:5 | via LLPhysicsExtensions | kept |
| jemalloc | 5.1.0 | linux64 | jemalloc (BSD derived) | jemalloc:12 | newview | kept |
| jpegencoderbasic | 1.0 | d, l, w | NONE | JPEGEncoderBasic:2 | newview | kept |
| kdu | 8.6 | d, l, w | Kakadu | LLKDU:22 | llkdu, llimage (when `USE_KDU`; off in CI) | kept (alternative to the Rust J2C) |
| libhunspell | 1.7.2.11968900321 | d, l, w | LGPL | Hunspell:10, ViewerMiscLibs:17 | llui | kept |
| libjpeg-turbo | 3.0.4-r1 | d, l, w | libjpeg-turbo | JPEG:10 | llimage | kept: the Rust JPEG candidate was measured and not adopted (`llimage/CMakeLists.txt:57–58`) |
| libndofdev | 0.1.11968678219 | darwin64, windows64 | BSD | NDOF:11 | newview | kept |
| libpng | 1.6.44-r2 | d, l, w | libpng | PNG:16 | only freetype's colour bitmap fonts (FreeType.cmake:4–6) | partly: the viewer's PNG codec is Rust (vkstorm-image) |
| libuuid | 1.6.2 | linux64 | UUID | ViewerMiscLibs:5 | llwindow/newview | kept |
| libxml2 | 2.13.5-r2 | d, l, w | mit | LLPrimitive:25, GStreamer10Plugin:13 | via colladadom; gstreamer | kept |
| llappearance_utility | 0.0.1 | linux | Proprietary | LLAppearanceUtility:8 | root CMakeLists (proprietary only) | kept |
| llca | 202407221423.0 | common | mit | LLCA:4 | newview (CA bundle) | kept |
| llphysicsextensions_source | 1.0.11137145495 | common | internal | LLPhysicsExtensions:23 | llprimitive, newview | kept |
| llphysicsextensions_tpv | 1.0.577418 | d, l, w | internal | LLPhysicsExtensions:36 | llprimitive, newview | kept |
| lsl_definitions | 0.6.12 | common | MIT | LSLDefinitions:4 | newview | kept |
| mesa | 7.11.1.297294 | linux | mesa | none | none | **unused** entry |
| mesazink | 26.3.0-devel | linux64, windows64 | MIT | MesaZink:17 | newview staging (`USE_MESAZINK`) | kept |
| meshoptimizer | 220.0.0-r1 | d, l, w | meshoptimizer | Meshoptimizer:10 | llmeshoptimizer | kept |
| mikktspace | 1 | d, l, w | Apache 2.0 | Mikktspace:5 | llmath (LLMath.cmake) | kept |
| minizip-ng | 4.0.7-r3 | d, l, w | minizip-ng | LLPrimitive:24 | via colladadom | kept |
| nanosvg | 2022.09.27 | darwin64, linux, windows64 | Zlib | ViewerMiscLibs:21 | llwindow/newview | kept |
| nghttp2 | 1.64.0-r1 | d, l, w | MIT | NGHTTP2:8 | llcorehttp; `vkstorm_ffi` via curl | kept |
| nvapi | 560.0.0-r1 | windows64 | MIT | NVAPI:10 | newview | kept |
| ogg_vorbis | 1.3.5-1.3.7.11968798109 | d, l, w | ogg-vorbis | Audio:9 | llaudio (`LLVorbisEncode`, oracle), newview (`llaudio/CMakeLists.txt:71–74`) | partly: decoding is Rust (vkstorm-vorbis); encoding stays C |
| open-libndofdev | 0.13.222872202 | linux64 | BSD | NDOF:13 | newview | kept |
| openal | 1.24.2-r1 | d, l, w | LGPL2 | OPENAL:15, 29 | llaudio, newview (optional backend, off) | kept (option) |
| openjpeg | 2.5.4.37071874243 (`3p-openjpeg` `v2.5.4-cpu-avx2.1`, `autobuild.xml:2369, 2383`) | d, l, w | BSD | none | none (only comments, `Copy3rdPartyLibs.cmake:262, 280`; `integration_tests/llui_libtest/CMakeLists.txt:58–71` still copies `openjp2.dll`) | **replaced** by vkstorm-openjp2/vkstorm-j2c (rung one); entry is dead |
| openssl | 1.1.1w-r3 | d, l, w | openssl | OpenSSL:9 | llcorehttp; `vkstorm_ffi` via curl | kept |
| openxr | 1.1.40-r1 | d, l, w | Apache 2.0 | OpenXR:13 | newview | kept |
| pcre | 8.35.979fd86 | linux64 | bsd | LLPrimitive:63 | llprimitive | kept |
| slvoice | 4.10.0000.32327… | d, l, w | Mixed | ViewerMiscLibs:20 | newview (Vivox; not in the SL Release per `doc/rust_port.md:42–46`) | kept |
| sse2neon | 1.8.0 | common | MIT | SSE2NEON:9 | llcommon, llmath | kept |
| threejs | 0.132.2 | common | MIT | ThreeJS:2 | newview | kept |
| tinyexr | 1.0.9-5e8947c | common | 3-clause BSD | TinyEXR:4 | newview | kept |
| tinygltf | 2.9.3-r1 | common | MIT | TinyGLTF:4 | llprimitive | kept |
| tracy | v0.11.1.11706699176 | d, l, w | bsd | Tracy:20 | llcommon, newview (off in CI) | kept |
| tut | 2008.11.30 | common | bsd | Tut:4 | `indra/test`, all unit tests | kept |
| velopack | 40232ef.23500976684 | darwin64, windows64 | MIT | Velopack:13, 43 | newview (off in CI) | kept |
| vhacd | 4.1.0-r2 | common | BSD | VHACD:7 | llphysicsextensionsos | kept |
| viewer-fonts | 1.0.0.242151216 | common | Various open source | ViewerMiscLibs:22 | newview data | kept |
| viewer-manager | 3.0-f14b5ec | d, l, w | viewerlgpl | ViewerManager:2 | newview | kept |
| **vkstorm-ffi** | 0.1.0.1b183c77e7.full | linux64, windows64 | LGPL-2.1-only | Rust:67 | llaudio, llcommon, llcorehttp, llfilesystem, llimage, llimagej2coj, llmath, llmessage | **the port** |
| vlc-bin | 3.0.21.11968962952 | darwin64, windows64 | GPL2 | LibVLCPlugin:8 | media_plugins/libvlc | kept |
| vulkan_gltf | 1.0.0 | common | Sascha Willems | VulkanGltf:4 | newview | kept |
| webrtc | m137.7151.04.23.22004231636 | d, l, w | MIT | WebRTC:9 | llwebrtc | kept |
| websocketpp | 0.8.2.24525603568 | common | websocketpp | websocketpp:6 | llcorehttp | kept |
| xxhash | 0.8.2-10285735820 | common | xxhash | xxHash:5 | llcommon (LLCommon.cmake) | kept |
| zlib-ng | 2.2.3-dev0.g8aa13e3.d20250206 | d, l, w | zlib-ng | ZLIBNG:13 | llaudio, llcommon, llinventory; curl | kept in C++ (the Rust crates use `zlib-rs` 0.6.8 internally) |

"d, l, w" stands for darwin64, linux64, windows64. `use_prebuilt_binary` also names `SDL`, `dbus_glib` and `gstreamer` (LLWindow:16/24, DBusGlib:13, GStreamer010Plugin:11). None of these has an `autobuild.xml` entry. They are reachable only on legacy paths.

`vkstorm-ffi` pin (`autobuild.xml:2912–2949`): sha1-hashed archives `.../vkstorm-core-rust/releases/download/ffi-v0.1.0/vkstorm_ffi-0.1.0.1b183c77e7.full-{linux64,windows64}-40285224044.tar.zst`. Its version encodes `<crate version>.<core-rust commit>.<rung set>` (`packaging/build_package.py:90–94`). The commit `1b183c77e7` is vkstorm-core-rust `main` HEAD. `origin/vkstorm-release` has no `vkstorm-ffi` entry and no `Rust.cmake` (checked with `git show origin/vkstorm-release:...`), so the release line contains no Rust.

---

## Tests

### Framework

- **tut 2008.11.30** (`indra/cmake/Tut.cmake:4`) is the test framework. The harness is `indra/test/test.cpp` plus `indra/test/lltut.{h,cpp}`. `indra/test/` holds 47 files, 20 of them `*_tut.cpp` legacy suites built when `LL_TESTS` (`indra/CMakeLists.txt:166–168`).
- **`LL_ADD_PROJECT_UNIT_TESTS(project sources)`** (`indra/cmake/LLAddBuildTest.cmake:10`) builds one executable per `tests/<name>_test.cpp`. Each runs through `LL_TEST_COMMAND` / `run_build_test.py` (345 lines) as a custom command producing `PROJECT_<p>_TEST_<n>_ok.txt` (lines 141–172). Building the `<project>_tests` target runs them.
- **`LL_ADD_INTEGRATION_TEST(name extra_sources libs [script])`** (`LLAddBuildTest.cmake:189`) builds `INTEGRATION_TEST_<name>` from `tests/<name>_test.cpp` and runs it as a POST_BUILD step (lines 271–279). An optional wrapper script (a mock server) launches the executable. Building the target is running the test, which is how CI and `doc/rust_port.md:1884–1897` invoke them.
- **Test switch:** `LL_TESTS` defaults OFF (`Variables.cmake:36`), and `configure_firestorm.sh:646` forces it OFF. The documented loop re-runs `cmake -S indra -B <dir> -DLL_TESTS:BOOL=ON` after configure (`build.yaml:215`, `doc/rust_port.md:1888`).

### Counts per library

Integration counts come from `grep -rnoE 'LL_ADD_INTEGRATION_TEST\s*\(\s*\w+'` over `CMakeLists.txt` and `*.cmake`, excluding the macro definition. `*_test.cpp` counts come from `find -name '*_test.cpp'`. The "other" column is `*_test.cpp` files with no integration target of the same name. These are unit tests in `LL_ADD_PROJECT_UNIT_TESTS` lists, or standalone tests such as `newview/tests/viewer_version_parser_test.cpp` and `llwindow/tests/linux_zink_loader_test.cpp`, which CI compiles directly with g++ (`build.yaml:34–41, 180–182`).

| Library | Integration targets | Other `*_test.cpp` | Integration target names |
|---|---|---|---|
| llaudio | 2 | 0 | llaudio_parity, llaudio_music |
| llcharacter | 0 | 1 | — |
| llcommon | 39 | 5 | apply, bitpack, classic_callback, commonmisc, lazyeventapi, llbase64, llcond, lldate, lldeadmantimer, lldependencies, llerror, lleventcoro, lleventdispatcher, lleventfilter, llframetimer, llheteromap, llinstancetracker, llleap, llmainthreadtask, llpounceable, llprocess, llprocessor, llprocinfo, llrand, llsdserialize, **llsdserialize_parity**, llsingleton, llstreamqueue, llstring, llsys, lltrace, lltreeiterators, llunits, lluri, stringize, threadsafeschedule, tuple, workqueue, llexception |
| llcorehttp | 4 | 0 | llcorehttp, jsonrpcws, wsserver, **llcorehttp_parity** |
| llfilesystem | 1 | 1 | lldir |
| llimage | 1 | 1 | **llimagecodecs** |
| llimagej2coj | 1 | 0 | **llimagej2coj** |
| llinventory | 4 | 0 | inventorymisc, llinventory_runtime, llparcel, **llinventory_parity** |
| llkdu | 0 | 1 | — |
| llmath | 10 | 7 | alignment, llbbox, llquaternion, mathmisc, m3math, v3dmath, v3math, v4math, xform, **llvolume** |
| llmessage | 5 | 5 | llavatarnamecache, llhost, llpartdata, llxfer_file, **llmessagetemplate** |
| llprimitive | 1 | 3 | **llmeshunpack_parity** |
| llui | 1 | 2 | llurlentry |
| llwindow | 0 | 1 | — |
| llxml | 1 | 0 | llcontrol |
| newview | 15 | 17 | cppfeatures, llsechandler_basic, llsecapi, llslurl, llviewercontrollistener, llviewernetwork, llviewerassetstats, **lltexturecache_parity**, **lltexturefetch_parity**, **llhttpretrypolicy**, **llinventorymodel_parity**, **llinvfetch_parity**, **llaisupdate_parity**, **llinventory_offer_oracle**, **llinventory_task_oracle** |
| viewer_components | 0 | 1 | — |
| **Total** | **85** | **45** | |

Bold names are Rust-port tests. `llinventory` declares `LL_ADD_PROJECT_UNIT_TESTS(llinventory "")` with an empty list ("no real unit tests yet!", `llinventory/CMakeLists.txt:66–70`).

### Port tests: parity, oracle and corpus tests

There are 18 port tests. Every one is also in the CI focused test step (`build.yaml:217–225`).

| Rung | Target | Defined at | Reference | Fixtures / harness |
|---|---|---|---|---|
| 1 J2C | `INTEGRATION_TEST_llimagej2coj` | `llimagej2coj/CMakeLists.txt:35` | Rust codec over the default skin's textures | `VKS_J2C_TEST_TEXTURE_DIR` = `newview/skins/default/textures` |
| 2 LLSD | `INTEGRATION_TEST_llsdserialize_parity` | `llcommon/CMakeLists.txt:374` | frozen C++ oracle `llcommon/tests/llsdserialize_oracle.{h,cpp}` in process | in-process corpus; `llsdserialize_tool` writes the vkstorm-llsd fixtures |
| 2 volume | `INTEGRATION_TEST_llvolume` | `llmath/CMakeLists.txt:140` | recorded C++ mesher corpus | `llmath/tests/fixtures` (2 files, 17 MB); `VKS_VOLUME_DUMP/BENCH` |
| 2 image | `INTEGRATION_TEST_llimagecodecs` | `llimage/CMakeLists.txt:75` | PNG/TGA/BMP/DXT through `LLImage*` over the skin's textures | `llimage/tests/llimagecodecs_manifest.txt` |
| 3 messages | `INTEGRATION_TEST_llmessagetemplate` | `llmessage/CMakeLists.txt:226` | corpus recorded from the C++ template system | `llmessage/tests/fixtures` (1 file, 9.8 MB) |
| 4 HTTP | `INTEGRATION_TEST_llcorehttp_parity` | `llcorehttp/CMakeLists.txt:128` | scenarios recorded from the C++ HTTP layer | `llcorehttp/tests/fixtures/llcorehttp_parity.xml`; `tests/vkshttp_mock_server.py` |
| 5 assets | `INTEGRATION_TEST_llmeshunpack_parity` | `llprimitive/CMakeLists.txt:103` | bit-level mesh unpack corpus | `llprimitive/tests/fixtures` (6.1 MB) |
| 5 assets | `INTEGRATION_TEST_lltexturecache_parity` | `newview/CMakeLists.txt:3183` | byte-level texture cache record | `newview/tests/fixtures/lltexturecache_parity{.bin,_events.xml}` |
| 5 assets | `INTEGRATION_TEST_lltexturefetch_parity` | `newview/CMakeLists.txt:3207` | fetch decision record | `lltexturefetch_parity.xml`; `tests/vksassets_mock_server.py`; stubs `lltexturefetch_parity_stubs.*` |
| 5 assets | `INTEGRATION_TEST_llhttpretrypolicy` | `newview/CMakeLists.txt:3233` | retry backoff (vkstorm-assets) and Retry-After parsing | — |
| 6 audio | `INTEGRATION_TEST_llaudio_parity` | `llaudio/CMakeLists.txt:93` | SoLoud output and decoder corpora | `llaudio/tests/fixtures` (74 files, 9.6 MB); adapters `llaudio_parity_{adapter.h,decode_adapter,vks_adapter}.cpp`; generator `fixtures/make_llaudio_parity_scenes.py` |
| 6 audio | `INTEGRATION_TEST_llaudio_music` | `llaudio/CMakeLists.txt:108` | parcel music end to end on the null device | `newview/tests/vksaudio_mock_stream_server.py`; `newview/tests/fixtures/music` |
| 7 inventory R1 | `INTEGRATION_TEST_llinventory_parity` | `llinventory/CMakeLists.txt:84` | frozen copies `llinventory/tests/llinventory_oracle.{h,cpp}` and corpora C1–C10 | `llinventory/tests/fixtures` (19 files, 8.9 MB); tool `llinventory_tool` (built, not run) |
| 7 inventory R2 | `INTEGRATION_TEST_llinventorymodel_parity` | `newview/tests/inventory/r2.cmake:16` | real `llinventorymodel.cpp` etc. against stubs; frozen copies in `llinventorymodel_parity_generic.cpp` | `newview/tests/fixtures/inventory/cache`; headers `llinventorymodel_parity_c1..c5,final,msgs,real.h` |
| 7 inventory R3 | `INTEGRATION_TEST_llinvfetch_parity` | `newview/tests/inventory/r3-fetch.cmake:32` | real `llaisapi.cpp` (renamed through `llinvfetch_parity_ais.cpp`/`_bgf.cpp`) | `fixtures/inventory/ais/fetch`; `tests/vksinventory_mock_server.py`; option `VKS_INVFETCH_PRE_D2_GATES` (line 26) |
| 7 inventory R3 | `INTEGRATION_TEST_llaisupdate_parity` | `newview/tests/inventory/r3-update.cmake` | `AISUpdate` paths of `llaisapi.cpp` | `fixtures/inventory/ais/update` |
| 7 inventory R4 | `INTEGRATION_TEST_llinventory_task_oracle` | `newview/tests/inventory/r4-task.cmake` | frozen task-inventory decision code (`llinventory_task_oracle*.cpp/h`) | `fixtures/inventory/task` |
| 7 inventory R4 | `INTEGRATION_TEST_llinventory_offer_oracle` | `newview/tests/inventory/r4-offer.cmake` | frozen offer/give code (`llinventory_offer_oracle_{frozen,copies,fakes}.cpp`) | `fixtures/inventory/{offer,give,msgs}` |

All files matching `*parity*` or `*oracle*` under `indra/` (63 files) belong to these targets. They are listed by `find indra \( -iname '*parity*' -o -iname '*oracle*' \)`.

Status of the rung-7 tests:

- They run against today's C++. None of `indra/llinventory` links `vkstorm_ffi` (`llinventory/CMakeLists.txt:59`: `llcommon llmath llmessage llxml`).
- vkstorm-ffi has no inventory feature.
- Inference: the rung-7 tests are recorders and baselines waiting for the Rust side. They currently pin the C++ against its frozen copies and fixtures.

### Copy-check scripts

Frozen oracle copies are delimited by `// FROZEN COPY begin: <source path>, <function>, lines <a>-<b>, blob <hash>` … `// FROZEN COPY end`. A copy-check script normalizes both sides, removing comments and `#if 0` and collapsing whitespace. It requires every region to match exactly once in the live file. It also enforces an "identifier fence": any live line mentioning a guarded identifier must lie inside a copied region or an allowlist (`indra/newview/tests/check_r2_copies.py:1–30`).

| Script | Lines | Run by |
|---|---|---|
| `indra/llinventory/tests/check_r1_copies.py` | 375 | POST_BUILD of `INTEGRATION_TEST_llinventory_parity` (`llinventory/CMakeLists.txt:104–106`); every copied source is an `OBJECT_DEPENDS` of the oracle (91–103) |
| `indra/newview/tests/check_r2_copies.py` | 182 | closing test of `llinventorymodel_parity` |
| `indra/newview/tests/check_r3a_copies.py` | 486 | `llaisupdate_parity` via `VKS_AISUPDATE_COPY_CHECK` (`r3-update.cmake:32, 41`) |
| `indra/newview/tests/check_r3b_copies.py` | 296 | `llinvfetch_parity` test 1 (`r3-fetch.cmake:20`) |
| `indra/newview/tests/check_r4a_copies.py` | 360 | `llinventory_task_oracle` test 8 via `VKS_TASK_COPY_CHECK` (`r4-task.cmake:8, 23`) |
| `indra/newview/tests/check_r4b_copies.py` | 419 | `llinventory_offer_oracle` (`r4-offer.cmake:10, 15`) |

Rungs 1–6 have no copy-check scripts. Their oracle is either the frozen `llsdserialize_oracle.cpp`, which no script checks against live code because the live C++ is gone, or recorded corpora.

### Fixture conventions

- **Location:** fixtures live beside the test in `<lib>/tests/fixtures/`, and CMake passes the path through `VKS_<AREA>_FIXTURE_DIR`. Examples: `llaudio/CMakeLists.txt:97`, `llcorehttp:136`, `llinventory:83`, `llmath:142`, `llmessage:229`, `llprimitive:105`, `newview:3188, 3216`, and `newview/tests/inventory/r*.cmake`.
- **Sizes** (`du -sh`): newview 48 MB (1375 files, 46 MB of it inventory), llmath 17 MB, llmessage 9.8 MB, llaudio 9.6 MB, llinventory 8.9 MB, llprimitive 6.1 MB, llcorehttp 268 KB. That is roughly 100 MB in plain git: no LFS, and only `binary` attributes (`.gitattributes`: `*.vksv`, `*.vksm`; per-directory `.gitattributes` in llaudio, llinventory and newview inventory fixture dirs).
- **Recording rules:** `llinventory/tests/fixtures/README.md` sets them for rung 7:
  - recorded from frozen copies at a named commit (`60184e5054`) that includes approved bug fixes;
  - never edited by hand;
  - written to both repositories by `vkstorm-core-rust/tools/inventory-fixtures/model/regenerate.py`, which dumps twice and replaces nothing unless the dumps are identical;
  - `llinventory_tool dump` exits 3 on success so a recording cannot pass as a test;
  - test 5 checks the fixture tree, including `digests.txt` keys.
- **Recording and benchmark switches:** `VKS_*_RECORD`, `VKS_*_BENCH`, `VKS_*_BENCH_OUT` (`doc/rust_port.md:1898–1919`).

### Repository-level script tests

`scripts/tests/` holds 15 Python test files and one Node test (`ci_publish_latest.test.cjs`), plus `check_release_hooks.py` and `fixtures/`. They run in `build.yaml:26–33, 116–117, 122` and `check-pr.yaml:37, 48–49`.

---

## CI and release flow

### Viewer workflows (`.github/workflows/`, 14 files, 2868 lines)

| Workflow | Lines | Trigger | What it does | Vulkanstorm-specific? |
|---|---|---|---|---|
| `build.yaml` "Build" | 320 | `pull_request`; push to `vkstorm-release`, `vkstorm-devel`, `vkstorm-canary`, `vkstorm-vulkan`, `codex/**`; tags `Second_Life*`; dispatch; `workflow_call` (3–9) | `check-publication` → `build` (matrix) → `latest` / `release` (detailed below) | yes |
| `check-pr.yaml` "Check PR" | 49 | PR opened/edited/reopened/synchronize | `check-description` (≥ 20 chars); `no-base-merges` when `base_ref` starts with `vkstorm-` (`test_check_no_base_merges.py` then `check_no_base_merges.py origin/<base> <head sha>`); `chat-ui-regression` (`test_button_hover.py`, `check_release_hooks.py`) | yes |
| `freebsd-image.yaml` | 95 | push to `codex/freebsd-runner-image`, PRs touching `scripts/ci/freebsd/**`, dispatch | Packer and QEMU FreeBSD 15.1 image with toolchain qualification | yes |
| `pre-commit.yaml` | 18 | PR; push `main`, `contribute`; tags `v*` | `pre-commit/action@v3.0.1` | inherited |
| `cla.yaml` | 26 | `issue_comment`, `pull_request_target` | CLA assistant | inherited |
| `label.yaml` | 15 | `pull_request_target` | labeler | inherited |
| `stale.yaml` | 25 | schedule | stale issues | inherited |
| `build_viewer.yml` "Build Viewer" | 620 | push `Firestorm*.*.*`, `*alpha`, `*nightly`, `*preview`; **schedule `00 03 * * *`**; dispatch | Firestorm's multi-grid/AVX matrix build | inherited |
| `build_sl_mac_only.yml` | 607 | push `Firestorm*` etc.; dispatch | Firestorm macOS build | inherited |
| `tag-fs-build.yml` | 171 | `workflow_run` of "Build Viewer"; dispatch | Firestorm tagging | inherited |
| `deploy_only.yml`, `sign.yml` | 98, 142 | dispatch | Firestorm deploy and signing | inherited |
| `qatest.yaml` | 610 | `workflow_run` of "Build"; dispatch | Linden self-hosted QA runners; runs only when `head_branch` starts with `Second_Life` (67–73) | inherited |
| `tag-release.yaml` "Tag a Build" | 72 | **schedule `21 2 * * 2,4,6`**; dispatch | creates `Second_Life_<channel>#<sha8>-<date>` tags with `secrets.LL_TAG_RELEASE_TOKEN` | inherited |

`build_viewer.yml` and `tag-release.yaml` carry schedule triggers. Schedules run on the default branch, `vkstorm-release`. Neither has a `github.repository` guard: grepping `build_viewer.yml` for `repository` matches only the `fs-build-variables` checkout. Inference: unless the workflows are disabled in repository settings, they fire on this fork and either fail or do nothing useful. That cannot be checked from the tree (see Risks).

### `build.yaml` in detail

1. **`check-publication`** (20–44) runs on ubuntu-latest:
   - Node test of the publisher;
   - Python tests: symbol archive, version schema, contacts layout, CI numeric boundaries, NDOF CMake, SLua UI resources, SLua review;
   - two direct g++ compile-and-run tests;
   - `check_release_hooks.py`, only when `github.ref` or `base_ref` is `vkstorm-release` (42–44).
2. **`build`** (46–271), `fail-fast: false`.
   - **Matrix:** `{windows-2022, build-vc170-64}` and `{ubuntu-24.04, build-linux-x86_64}` (51–55).
   - **Build type:** `VIEWER_BUILD_TYPE` is `Release` and `VIEWER_PACKAGE` is `ON` only for `refs/heads/vkstorm-release` or `refs/tags/Second_Life*`. Everything else is `RelWithDebInfo` without a package (58–59).
   - **Build number:** the build number is `git rev-list --count HEAD`, which needs the full history (72–86).
   - **Steps:**
     - install tools: Linux build dependencies, autobuild, Inno Setup;
     - run the installer payload and Velopack version tests and the Linux install script test;
     - configure (above);
     - check the Mesa Zink loader on Linux;
     - build `lltrace_compile_check` with `VS_CI_TRACE_COMPILE_CHECK=ON` (192–195);
     - **build and stage the whole viewer** (197–198);
     - **fail if a qualification build produced an installer** (200–209);
     - **focused test step** (211–225);
     - check the packaged Zink runtime on Linux;
     - upload the installer, the qualification viewer or the build evidence (`CMakeCache.txt`, configure log, `installed-packages.xml`, ...).
   - **Focused test step:** reconfigures with `-DLL_TESTS=ON` and builds 22 targets: `lltrace`, `llinventory_runtime`, `jsonrpcws`, `wsserver`, and the 18 port tests listed above (`llimagej2coj` … `llinventory_offer_oracle`, which includes `llhttpretrypolicy`). Building them runs them. The other 63 integration tests and all project unit tests do not run in CI.
3. **`latest`** (273–300) runs only for `anne-skydancer/vulkanstorm` on `refs/heads/vkstorm-release` with event push, dispatch or schedule. It downloads both `*-installer` artifacts and runs `scripts/ci_publish_latest.cjs` (117 lines):
   - skips if the release body records a newer run (`<!-- vulkanstorm-latest-run:N -->`, 28–31);
   - skips if the source is older than the current `latest` tag (40–47);
   - otherwise stages assets under temporary names, renames them, force-moves `tags/latest` to the source commit (87–90) and marks the release `make_latest`.

   `needs: build` with a two-entry matrix means `latest` advances only after both Windows and Linux succeed, which matches AGENTS.md.
4. **`release`** (302–320): on a pushed `Second_Life*` tag, creates a GitHub prerelease with the Windows `.exe` and Linux `.tar.xz`.

`vkstorm-rust` is **not** in the push branch list (line 8). Only PRs into it and pushes to `codex/**` branches build. Inference: the merge commits that land on `vkstorm-rust` are never built as such.

### Tags

`git tag -l` holds 20 tags, among them `latest`, `stable`, `experimental`, `oldstable`, `development` and `initial-release-installers`.

| Tag | Commit | Date | Contained in |
|---|---|---|---|
| `latest` | `8bbe7feb42` | 2026-10-26 | vkstorm-release |
| `stable` | `8bbe7feb42` | 2026-10-26 | vkstorm-release |
| `oldstable` | `989fd4a677` | 2026-10-15 | vkstorm-release |
| `experimental` | `8218d102e6` | 2026-10-26 | vkstorm-canary only |
| `development` | `b10f545f25` | 2026-09-23 | — |

Only `latest` is automated. Grepping `.github`, `scripts` and `doc` finds no workflow or script that moves `stable`, `oldstable` or `experimental`. Inference: they are moved by hand.

### Branch-policy enforcement

- **`scripts/check_no_base_merges.py`** (173 lines) has two modes:
  - CI mode `<base-ref> [<head>]` rejects any merge in `base..head` whose second or later parent lies on the first-parent chain of any `origin/vkstorm-*` line.
  - `--pending` mode serves the `pre-merge-commit` hook. `VKS_ALLOW_BASE_MERGE=1` overrides it.

  `MIRROR_LINES = ("vkstorm-aleph",)` is exempt (lines 1–40). Its regression tests are in `scripts/tests/test_check_no_base_merges.py`.
- **Git hooks:** `scripts/git-hooks/pre-merge-commit` and `scripts/git-hooks/pre-commit`. `scripts/install-git-hooks.sh` (41 lines) copies them and the checker into `git rev-parse --git-path hooks`, or uses them in place when `core.hooksPath` points there. It chains a foreign hook as `<name>.local` and refuses to overwrite an existing `.local`. Without Python 3.6+ the hook warns and skips the check.
- **`scripts/tests/check_release_hooks.py`** (48 lines) is a regex denylist of development hooks: `VULKANSTORM_*_DEBUG/CAPTURE/...`, `LLVKUITestScene`, `readbackSwapchain`, `USE_TRACY_MEMORY`, and others. It scans every tracked `indra/**/*.{cpp,h,mm,cmake}`, `CMakeLists.txt` and `settings.xml`. It runs on every PR (`check-pr.yaml:49`) and on release builds and PRs (`build.yaml:42–44`).
- **Core-rust copy:** vkstorm-core-rust carries the same checker, hooks and installer (`scripts/`). Its Check PR runs when `base_ref == 'main'`.

---

## Rust port infrastructure

### vkstorm-core-rust layout

```
Cargo.toml            workspace: resolver 3, members crates/* and tools/*; excludes tools/llsd-fixtures, tools/inventory-fixtures
                      workspace.package: version 0.1.0, edition 2024, license LGPL-2.1-only
                      workspace.dependencies: hayro-jpeg2000 0.4, j2k 0.12 (used by tools/j2c-eval)
                      [profile.ffi-package]: inherits release, lto = "fat", codegen-units = 1, opt-level 3, debug = line-tables-only
rust-toolchain.toml   channel = "1.99.1", profile = "minimal"
.cargo/config.toml    [build] rustflags = ["-C", "target-cpu=x86-64-v3"]   (applies to every local cargo invocation)
autobuild.xml         package_description for vkstorm-ffi (linux64, windows64; one "release" configuration running packaging/build_package.py)
packaging/build_package.py   (112 lines)
crates/  (13)         tools/ (6)        docs/j2c-port-plan.md        scripts/ (base-merge checker, hooks)
.github/workflows/    check-pr.yaml, package-ffi.yaml
```

### Crates

| Crate | Purpose | Port rung | `.rs` files | Lines total (src / tests, benches, build) | `#[test]` / test files / benches | Unsafe (non-comment lines with `unsafe {` / `unsafe fn` / `unsafe impl`) | `forbid(unsafe_code)` | External deps (direct) |
|---|---|---|---|---|---|---|---|---|
| vkstorm-core | Placeholder: `OPENSIM_SUPPORT` const only (16 lines); doc promises LLSD, UUID, frames, packers | — | 1 | 16 (16 / 0) | 0 / 0 / 0 | 0 / 0 / 0 | no | — |
| vkstorm-openjp2 | OpenJPEG port (c2rust lineage via the `openjp2` crate) | 1 | 35 | 43,227 (42,407 / 820) | 15 / 3 / 0 | 463 / 99 / 0 | no | bitflags 1.3, byteorder, log, smallvec, sprintf, libc (opt) |
| vkstorm-j2c | Viewer JPEG 2000 contract: metadata, discard levels, truncated decode, upload encode | 1 | 4 | 765 (508 / 257) | 16 / 3 / 0 | 14 / 6 / 0 | no | — |
| vkstorm-llsd | Notation, binary and XML LLSD parse and format into a sink | 2 | 11 | 5,318 (4,190 / 1,128) | 36 / 2 / 0 | 0 | no (but none used) | — |
| vkstorm-image | PNG, TGA, BMP, DXT; JPEG candidate (feature `jpeg`, not adopted) | 2 | 8 | 2,490 (2,140 / 350) | 10 / 1 / 0 | 0 | no (none used) | png =0.18.1, zlib-rs =0.6.8, zune-jpeg 0.5 (opt), jpeg-encoder 0.7 (opt) |
| vkstorm-volume | Parametric prim mesher | 2 | 12 | 3,873 (2,766 / 1,107) | 4 / 1 / 1 | 15 / 0 / 0 (`simd.rs`, `sweep.rs`) | no | — |
| vkstorm-messages | Template parser, builder, reader, data packers, zero-coding | 3 | 10 | 4,837 (3,496 / 1,341) | 25 / 1 / 1 | 0 | **yes** | — |
| vkstorm-http | `LLCore::HttpRequest` service thread, policies, retry, libcurl multi transport | 4 | 14 | 4,562 (3,607 / 955) | 31 / 1 / 1 | 69 / 48 / 5 (`sys.rs`, `transport.rs`) | no | — (libcurl by hand; `build.rs` with `link-curl`) |
| vkstorm-assets | Texture cache, fetch state machine, disk cache, mesh unpack, asset storage | 5 | 21 | 8,011 (6,924 / 1,087) | 46 / 2 / 1 | 0 | **yes** | zlib-rs =0.6.8 |
| vkstorm-vorbis | Ogg Vorbis decoder | 6 | 16 | 8,698 (7,625 / 1,073) | 16 / 2 / 1 | 0 | **yes** | — |
| vkstorm-audio | Mixer (SoLoud port), WASAPI, PulseAudio and ALSA devices, parcel music | 6 | 61 | 20,204 (15,468 / 4,736) | 119 / 13 / 1 | 100 / 45 / 6 | no | symphonia-core/-bundle-mp3/-codec-aac/-bundle-flac =0.6.1, libloading =0.9.0, windows =0.62.2 (cfg windows) |
| vkstorm-inventory | Inventory data-model rules (rung seven): **skeleton**. 30 modules of 1–10 lines each are doc comments only, while `tests/fixtures` holds 1,356 files (55 MB) | 7 | 30 | 120 (120 / 0) | 0 / 0 / 0 | 0 | **yes** | — |
| vkstorm-ffi | C ABI, staticlib, `test = false` | all | 9 | 6,557 (6,557 / 0) | 0 | 480 / 318 / 4; 300 `#[unsafe(no_mangle)]` | no | — |
| **Total crates** | | | 232 | **108,678** | 334 `#[test]` | | | |

Tools (workspace members):

| Tool | `.rs` lines | Purpose |
|---|---|---|
| tools/j2c-eval | 508 (14 / 9 unsafe) | Decoder evaluation: openjp2 0.6, jpeg2k 0.10, hayro-jpeg2000, j2k, image 0.25 |
| tools/image-eval | 187 | Raster codec parity oracle (C++ codecs, standalone) and timing |
| tools/soloud-oracle | 589 | SoLoud C++ oracle for vkstorm-audio |
| tools/vorbis-oracle | 443 | libvorbis oracle |
| tools/llsd-fixtures | Python `regenerate.py` | Runs the viewer's `llsdserialize_tool` and copies the fixtures |
| tools/inventory-fixtures | Python: `model/`, `cache/`, `ais-fetch/`, `ais-update/` (`gen_cases.py`), `offer/`, `task/` (`check_set1_inputs.py`) `regenerate.py`; READMEs for `msgs/` | Recorder drivers for rung 7 |

Commands used:

- **Lines:** `find <crate> -name '*.rs' | xargs cat | wc -l`, split by `-path '<crate>/src/*'`. The workspace total including tools is 110,405.
- **Unsafe:** `grep -rhE '\bunsafe\b' --include=*.rs <crate> | grep -vE '^\s*//'`, then counted for `unsafe\s*\{`, `unsafe\s+(extern "C"\s+)?fn` and `unsafe\s+impl`.
- **Tests:** `grep -rE '#\[test\]'`.

Notes:

- Unsafe is concentrated in four places: the FFI boundary (vkstorm-ffi), the c2rust-lineage codec (vkstorm-openjp2), hand-bound libcurl (vkstorm-http) and OS audio devices (vkstorm-audio).
- **Fixture sizes per crate** (`du -sh crates/*/tests/fixtures`):

  | Crate | Size |
  |---|---|
  | inventory | 55 MB |
  | volume | 17 MB |
  | messages | 11 MB |
  | image | 8.0 MB |
  | llsd | 7.1 MB |
  | assets | 6.8 MB |
  | vorbis | 3.8 MB |
  | openjp2 | 1.3 MB |
  | audio | 709 KB |
  | j2c | 592 KB |

  They are stored in plain git with `binary` attributes, with no LFS.
- The workspace README is stale:
  - Its crate table omits `vkstorm-audio`, `vkstorm-vorbis` and `vkstorm-inventory`, and lists `vkstorm-audio` and `vkstorm-inventory` as "Planned".
  - Its Tools section omits soloud-oracle, vorbis-oracle and inventory-fixtures.
  - `AGENTS.md` still says the viewer pins core-rust "by commit in `indra/rust/Cargo.toml`". That path no longer exists in the viewer.

### External crates (`Cargo.lock`)

`Cargo.lock` lists 92 packages: 75 external (registry) and 17 workspace. Counted with a regex over `[[package]]` blocks with and without `source`. The external set: adler2, anyhow, autocfg, bitflags (1.3.2 and 2.13.2), bytemuck, byteorder, byteorder-lite, cfg-if, crc32fast, crossbeam-{deque,epoch,utils}, either, fdeflate, fearless_simd, flate2, hayro-jpeg2000, image, j2k (+ j2k-codec-math, -core, -native, -profile, -types), jpeg-encoder, jpeg2k, lazy_static, libc, libloading, libm, log, miniz_oxide (0.8.9 and 0.9.1), moxcms, num-complex, num-traits, openjp2, png, proc-macro2, pxfm, quote, rayon, rayon-core, regex-lite, simd-adler32, smallvec, sprintf, symphonia-{bundle-flac,bundle-mp3,codec-aac,common,core,metadata}, syn (2.0.119 and 3.0.9), thiserror (1 and 2, + impl), unicode-ident, windows (+ collections, core, future, implement, interface, link, numerics, result, strings, threading), zlib-rs, zune-core, zune-jpeg.

Most of the heavy graph (image, rayon, j2k, jpeg2k, openjp2, hayro) belongs to `tools/j2c-eval`. Only the crates the selected features pull in are compiled into the package. `packaging/build_package.py:76–88` records that set in `LICENSES/third-party.txt` via `cargo tree -e normal --features <rungs>`.

### `vkstorm-ffi`: features and header

- **Features** (`crates/vkstorm-ffi/Cargo.toml`):
  - one per rung: `j2c`, `llsd`, `volume`, `image`, `messages`, `http`, `assets`, `audio` (= vkstorm-audio + vkstorm-vorbis);
  - `full` = all eight;
  - `opensim = ["vkstorm-assets?/opensim"]`. Only the assets crate's OpenSim paths are forwarded. vkstorm-messages declares an `opensim` feature, but the ffi crate never enables it, and grep finds no `feature = "opensim"` cfg in vkstorm-messages sources. Cfg counts: assets 29, ffi 15, core 1.
- **Sources:** `src/lib.rs` (39 lines) gates one module per feature. The rung files are `assets.rs` 2135, `audio.rs` 1016, `http.rs` 713, `image.rs` 525, `j2c.rs` 207, `llsd.rs` 304, `messages.rs` 1226 and `volume.rs` 392 lines. Some exports are macro-generated, for example `audio.rs:561`.
- **Header:** `include/vkstorm_ffi.h` (1310 lines) is hand-written; no cbindgen is in `Cargo.lock`. It declares all rungs' functions regardless of features (`lib.rs:21–22`: "a viewer must not call a rung it has not adopted"). Its file comment (lines 3–9) still names the former `indra/rust/vkstorm-ffi/src/*.rs` paths. It also contains a C++ helper template `vks_ffi_string` (line 1288).
- **Header and source sync:** nothing checks mechanically that the header matches the Rust exports. The ffi crate has no tests (`Cargo.toml`: `test = false`, "The viewer's oracles test this ABI end to end"). There is no ABI version symbol: grep for `abi_version|ffi_version|VKS_FFI_VERSION` returns nothing. The viewer does not read the package's `VERSION.txt` at configure time.

Exported functions declared in the header: 390 unique `vks_*` names, from `grep -oE '\bvks_[a-z0-9_]+\s*\(' include/vkstorm_ffi.h | sort -u`. Viewer call sites are occurrences of those names followed by `(` in `indra/**/*.{cpp,h,inl}`, excluding `build*`, counted per top-level library. Counts include test sources: llaudio 8 test, newview 7 test, the rest production. 30 header names have no viewer reference (list below).

| Prefix | Rung | Declared | Viewer call sites by library |
|---|---|---|---|
| `vks_j2c_` | 1 | 13 | llimagej2coj 13, llimage 1 |
| `vks_llsd_` | 2 | 22 | llcommon 23 |
| `vks_volume_` | 2 | 8 | llmath 9 |
| `vks_png_` | 2 | 3 | llimage 3 |
| `vks_tga_` | 2 | 4 | llimage 4 |
| `vks_bmp_` | 2 | 3 | llimage 3 |
| `vks_dxt_` | 2 | 15 | llimage 15 |
| `vks_raster_` | 2 | 6 | llimage 33 |
| `vks_bytes_` | 2 | 3 | llimage 17 |
| `vks_image_` | 2 | 1 | llimage 14 |
| `vks_msg_` | 3 | 53 | llmessage 71, newview 1 |
| `vks_dp_` | 3 | 16 | llmessage 25 |
| `vks_http_` | 4 | 40 | llcorehttp 41 |
| `vks_texcache_` | 5 | 21 | newview 24 |
| `vks_tfetch_` | 5 | 50 | newview 49 |
| `vks_mesh_` | 5 | 25 | llprimitive 12, newview 10, llmath 5 |
| `vks_diskcache_` | 5 | 14 | llfilesystem 11, newview 1 |
| `vks_asset_` | 5 | 13 | llfilesystem 8, llmessage 4, newview 3 |
| `vks_assets_` | 5 | 1 | llfilesystem 1 |
| `vks_retry_` | 5 | 7 | newview 6 |
| `vks_toxic_` | 5 | 5 | llmessage 5 |
| `vks_lfs_` | 5 | 2 | llfilesystem 2 |
| `vks_file_` | 5 | 2 | newview 3 |
| `vks_audio_` | 6 | 46 | llaudio 59 |
| `vks_music_` | 6 | 16 | llaudio 22 |
| `vks_ffi_` | helper | 1 | newview 7, llfilesystem 2 |
| **Total** | | **390** | **360 distinct names used; 538 total `vks_*(` occurrences including non-FFI test helpers** |

By library (production / test): llmessage 105/0, newview 97/7, llimage 90/0, llaudio 73/8, llcorehttp 41/0, llfilesystem 24/0, llcommon 23/0, llmath 14/0, llimagej2coj 13/0, llprimitive 12/0. The heaviest files are `newview/lltexturefetch.cpp` 50, `llaudio/llaudioengine_vks.cpp` 48, `llimage/llimagedxt.cpp` 40, `llcorehttp/httprequest.cpp` 39, `newview/lltexturecache.cpp` 28, `llmessage/llmessagetemplateparser.cpp` 27, `llmessage/lldatapacker.cpp` 25 and `llcommon/llsdserialize.cpp` 23. 35 viewer source files include `vkstorm_ffi.h`.

Declared but never referenced in `indra/` (30):

| Prefix | Unreferenced functions |
|---|---|
| `vks_audio_` | `frames_played`, `global_volume`, `restart`, `voice_set_protect`, `voice_stop` |
| `vks_diskcache_` | `dir_file_size`, `filepath`, `free` |
| `vks_dp_` | `bin_pack`, `bin_pack_s16`, `bin_unpack`, `bin_unpack_s16` |
| `vks_http_` | `curl_version`, `policy_get_long`, `reply_long`, `reply_string` |
| `vks_j2c_` | `calc_discard_level_bytes` |
| `vks_mesh_` | `header_flags`, `header_set_from_flags` |
| `vks_msg_` | `reader_message_size`, `template_total_size` |
| `vks_music_` | `current_metadata`, `url` |
| `vks_retry_` | `policy_retry_count` |
| `vks_texcache_` | `purge_pending` |
| `vks_tfetch_` | `fetcher_debug_count`, `fetcher_semaphore`, `fetcher_total_http_requests`, `worker_get_reason`, `worker_url` |

Some viewer names look like FFI symbols but are not in the header: `vks_path_params` and `vks_profile_params` (static helpers in `llmath/llvolume.cpp`), `vks_assets_install_viewer_log` / `vks_assets_viewer_log` and `vks_texcache_clock` (viewer-side helpers), and `vks_test_*`, `vks_getpid`, `vks_r3a_apply_tag`, `vks_task_oracle` (test helpers).

### Packaging and its CI

- **`packaging/build_package.py`** runs one `cargo rustc -p vkstorm-ffi --profile ffi-package --locked --no-default-features --features <rungs> -- --print native-static-libs`, with `RUSTFLAGS=-Ctarget-cpu=x86-64-v3` hard-coded (lines 32, 38). It stages:
  - `lib/release/{vkstorm_ffi.lib|libvkstorm_ffi.a}`;
  - `vkstorm_ffi.native-libs.txt`;
  - `include/vkstorm_ffi.h`;
  - `LICENSES/vkstorm-core-rust.txt`;
  - `LICENSES/third-party.txt`;
  - `VERSION.txt`.

  The rung set comes from `VKS_FFI_FEATURES` (default `full`).
- **`.github/workflows/package-ffi.yaml`** (77 lines):
  - **Triggers:** push to `main`, tags `ffi-v*`, any PR, dispatch.
  - **Matrix:** `os: [windows-2022, ubuntu-24.04]` × `rungs: ['full', 'j2c']` (24–27). The comment says "j2c: vkstorm-release's first rung", but vkstorm-release does not consume it today.
  - **Steps:** install autobuild 3.9.3; `rustup show active-toolchain || rustup toolchain install`; `autobuild build -A 64 -c release`; `autobuild package --archive-format tzst`; upload the artifacts.
  - **Publishing:** on an `ffi-v*` tag, the `release` job runs `gh release create` with the archives and `SHA256SUMS` (57–77).
  - **Hash mismatch:** the release publishes sha256 sums, while the viewer's `autobuild.xml` pins sha1 hashes (`<hash_algorithm>sha1`). Inference: the sha1 values come from autobuild's own package output, not from `SHA256SUMS`.
- **`.github/workflows/check-pr.yaml`** (28 lines) runs only the base-merge checker.
- **Finding: no workflow in vkstorm-core-rust runs `cargo test`, `cargo clippy` or `cargo fmt --check`.** The crates' 334 `#[test]` functions and their fixture replays run only on developer machines. Through the ABI, the viewer's 18 port tests run in viewer CI.
- **Existing tags:** `ffi-v0.1.0` only.

### Toolchain, flags, conventions

| Item | Setting |
|---|---|
| Toolchain | `rust-toolchain.toml` pins `1.99.1` (minimal profile); CI relies on rustup reading it |
| CPU baseline | `.cargo/config.toml` sets `target-cpu=x86-64-v3` for every local build, so local `cargo test` binaries need an AVX2 host. The viewer's source mode overrides it through `RUSTFLAGS` (`Rust.cmake:44–55`). |
| Branches (`vkstorm-core-rust/AGENTS.md`) | `main` is the base line; features go on `codex/` branches through PRs; one-way merges only (same checker and hooks as the viewer) |
| Consumers | Shared with vkstorm-aleph, which takes the crates as git dependencies pinned by commit (README) |

Fixture regeneration flows from the viewer to the crates. The viewer builds the C++ recorder executables with `LL_TESTS=ON`: `llsdserialize_tool`, `llinventory_tool`, the inventory recorder tests and the audio, asset and volume `VKS_*_RECORD` modes. Python drivers in `vkstorm-core-rust/tools/*-fixtures` then run them and copy the output into `crates/*/tests/fixtures`. The rung-7 drivers also write the viewer's copy (`llinventory/tests/fixtures/README.md`). Example from `tools/llsd-fixtures/README.md`: `python tools/llsd-fixtures/regenerate.py <viewer>/build-vc170-64/llcommon/RelWithDebInfo/llsdserialize_tool.exe`, then `cargo test -p vkstorm-llsd --test fixtures`.

### Change flow for a rung

As documented in `doc/rust_port.md:17–40`, a rung moves through these steps:

1. Change the crates and the ABI in vkstorm-core-rust through a PR. package-ffi builds `full` and `j2c` archives as artifacts.
2. Push an `ffi-v*` tag to publish a release.
3. Edit the viewer's `autobuild.xml` `vkstorm-ffi` entry (URL, hash, version) on a viewer feature branch.
4. Viewer CI downloads the package and runs the oracles.

During development, `-DVKS_CORE_RUST_DIR` skips steps 1–3.

---

## Port assessment

What is missing or fragile for the next rungs: inventory, then textures, the object store and the renderer, per `doc/rust_port.md:13–15`.

1. **The Rust side of rung 7 does not exist on `main`.** `vkstorm-inventory` is a 120-line module skeleton with `forbid(unsafe_code)`, next to 55 MB of fixtures. vkstorm-ffi has no `inventory` feature, the header has no `vks_inv*` functions, and `indra/llinventory` does not link `vkstorm_ffi`. The viewer side is ready: six recorder test targets, six copy-check scripts and fixtures, all in CI. Inference: the current branch `codex/rust-inventory-model` exists to fill that gap. Landing it needs a core-rust PR, a new `ffi-v*` tag and a viewer `autobuild.xml` bump in lockstep.
2. **There is one package per rung set, not one per rung, and only two sets are built.** The matrix builds `full` and `j2c`. Each new rung changes `full`, which forces every viewer line on `full` to re-pin. vkstorm-release pins nothing yet. Promoting rungs to the release line needs either more rung sets in the matrix or a parameterised build. Each set also needs its own `ffi-v*` tag, because the version string contains the core-rust commit and the rung label.
3. **There are no OpenSim and no non-AVX2 packages.** `Rust.cmake:59–64` stops those configurations. `package-ffi.yaml` has no `opensim` dimension, and `build_package.py:32` hard-codes `x86-64-v3`. The Firestorm configurations `ReleaseFS*` (common) pass `--opensim` and `ReleaseFS_AVX` passes `--avx`, so the stock autobuild configurations fail on this line without `VKS_CORE_RUST_DIR`. The audio rung's OpenSim implications are not forwarded either: `opensim` reaches only vkstorm-assets.
4. **The local development loop differs from what ships.**
   - `VKS_CORE_RUST_DIR` builds with profile `release`, not `ffi-package` (fat LTO, single codegen unit).
   - Corrosion v0.6.1 is cloned from GitHub at configure time (`Rust.cmake:19–26`).
   - `corrosion_import_crate` is not given `--locked` in `Rust.cmake`. Inference: the dependency resolution may drift from `Cargo.lock`.
   - The documented commands target `build-vc170-64`, while the local tree is `build-vc180-64`.
   - No CI job exercises the source mode, which `Rust.cmake:12` says on purpose ("CI always uses the package"), so it can break silently.
5. **Rust tests are not in CI.** Breaking a crate's fixture test is caught only if a developer runs `cargo test` on an AVX2 host before tagging, or indirectly by viewer oracles after a re-pin. vkstorm-audio's tests also need `--features link-curl`.
6. **The ABI is unchecked.** The header is hand-written and declares every rung. There is no generated header, no symbol-list diff and no version handshake. A viewer pinned to a package without a rung it calls fails only at link time with unresolved symbols (inference from `lib.rs:21–22`). Also, 30 declared functions are unused.
7. **Linux is behind Windows.**
   - The C++ side gets no `-mavx2` from CMake while the Rust side is `x86-64-v3`.
   - CI's Linux job uses gcc-14 on Ubuntu 24.04, while `doc/building_linux.md` documents Ubuntu 22.04 with GCC 11 and glibc ≥ 2.34. The archive is built on ubuntu-24.04 runners. Inference: its glibc floor is at least 24.04's, which can exceed the documented 22.04 floor.
   - The audio Linux device loads libpulse/libasound through libloading, which no CI step exercises against real devices.
8. **macOS and FreeBSD have no path.** There is no darwin64 package and no aarch64 target, and `VKSAudio.cmake` excludes macOS. FreeBSD has an image and nothing else.
9. **Licences are not shipped.** The package's `LICENSES/third-party.txt`, which includes MPL-2.0 symphonia, and `vkstorm-core-rust.txt` are not staged by `viewer_manifest.py`.
10. **Dead or misleading build metadata:**
    - the unused `openjpeg` and `mesa` entries in `autobuild.xml`;
    - `integration_tests/llui_libtest/CMakeLists.txt` copying `openjp2.dll`;
    - `doc/rust_port.md:42–46` listing "OpenJPEG" as a required Release feature while line 54 says OpenJPEG is no longer part of the build;
    - stale paths (`indra/rust/...`) in `vkstorm_ffi.h` and in rust_port.md's rung sections, which line 34–38 acknowledges;
    - the stale core-rust README and AGENTS.md.
11. **Fixtures grow in plain git.** About 100 MB in the viewer and about 111 MB in core-rust, with rung 7 alone over 100 MB across both, all without LFS. Duplicated rung-7 fixtures are kept byte-identical only by the regeneration driver; no CI step compares the two trees.

---

## Risks and open questions

| # | Risk / question | Evidence | Suggested check |
|---|---|---|---|
| 1 | Is Linux C++ compiled with AVX2 at all? | No `-mavx2`/`-march` in `indra/cmake` or `autobuild.xml` | Inspect `LL_BUILD` from `fs-build-variables@c17312d3`; read `CMakeCache.txt` flags in the Linux "build evidence" artifact |
| 2 | Do the inherited scheduled workflows (`build_viewer.yml` daily, `tag-release.yaml` thrice weekly) run on this fork? `tag-release` would create `Second_Life_*` tags, which trigger `build.yaml`'s `release` job | Schedule triggers without repository guards | Check the Actions tab or `gh workflow list` for disabled state |
| 3 | Merges to `vkstorm-rust` are never built post-merge | `build.yaml:8` omits `vkstorm-rust` | Decide whether the PR build of the head suffices; add the branch if not |
| 4 | Crate regressions slip in between tags | No `cargo test` in core-rust CI | Add a test job, AVX2 runner caveat included |
| 5 | Header/ABI drift | Hand-written header, no symbol diff | Compare `nm`/`dumpbin /symbols` output against the header in package-ffi |
| 6 | Package glibc floor versus the documented Linux minimum | Built on ubuntu-24.04 | Check the `GLIBC_` versions referenced by `libvkstorm_ffi.a` objects |
| 7 | How are `stable`/`experimental` moved, and what does `experimental` on `vkstorm-canary` mean for this line? | No automation found | Document the manual procedure |
| 8 | The audio rung's OpenSim implications are unknown | `opensim` forwards only to vkstorm-assets | Confirm whether any OpenSim-specific audio or message paths exist |
| 9 | Corrosion-based source builds need network and may resolve differently from `Cargo.lock` | `Rust.cmake:19–41` | Vendor Corrosion or pin by commit; pass `--locked` through `CORROSION` flags |
| 10 | Licence compliance of the shipped installer | Missing `third-party.txt` staging | Add the two `LICENSES` files to the Windows and Linux manifests |
| 11 | KDU builds silently drop the Rust J2C | `llimagej2coj/CMakeLists.txt:5` (`if (NOT USE_KDU)`); `windows64 ReleaseFS*` pass `--kdu` | Confirm the intended policy for `--kdu` on the rust line |
| 12 | Only 22 of 85 integration tests and no unit-test projects run in CI | `build.yaml:217–225` | Decide whether to run the full `LL_TESTS` suite on at least one platform |
| 13 | Two `doc/rust_port.md` statements disagree on OpenJPEG | lines 42–46 versus 54–55 | Correct the Release-requirements bullet |
