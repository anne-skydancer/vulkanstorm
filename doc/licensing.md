# Licensing: compatibility audit and source headers

This document backs [`NOTICE.md`](../NOTICE.md). Vulkanstorm is offered under
`LGPL-2.1-only OR GPL-3.0-or-later` (Linden Lab and Firestorm code, and Rust
ports of it) and `LGPL-2.1-or-later OR GPL-3.0-or-later` (original
Vulkanstorm code). Offering GPL-3.0 for a *binary* is only honest if
everything linked into it can be distributed under GPL-3.0 too. This is the
audit of what the shipped binaries link with, as of 2026-11-05 on
`vkstorm-devel`, re-checked for this line below.

## This line: `vkstorm-canary`

The audit below was made on `vkstorm-devel` and re-checked against this
line's own `autobuild.xml` and CI configure line (2026-11-04). The configure
options are the same as devel's. Canary follows upstream Firestorm 7.2.5,
which brings these package differences:

| Component | Licence | Status | Notes |
|---|---|---|---|
| SDL3 3.2.24 (replaces SDL2) | Zlib upstream (`autobuild.xml` says "lgpl") | OK | either way |
| glext 2025.10.27 | Khronos (MIT-style) | OK | |
| open-libndofdev 0.14 | BSD | OK | |
| fltk, mesa (old), libuuid, glh_linear, llappearance_utility, vlc-speakerfill | | not on this line | |

OpenSSL 3.5.9 is linked, as on devel, so the OpenSSL open point is
resolved here too (GLOD has been removed). This line
has no GPL-only code.

## Open points (read first)

1. **OpenSSL: resolved by the move to OpenSSL 3.5.** Until 2026-11-05 the
   viewer linked OpenSSL 1.1.1w (`openssl 1.1.1w-r4`), under the OpenSSL
   License plus the original SSLeay License; both carry advertising
   clauses, and the SSLeay License says the code "cannot simply be copied
   and put under another distribution licence [including the GNU Public
   Licence.]". The FSF lists that combination as GPL-incompatible, and
   GPL-3.0's System Library exception does not cover a copy the viewer
   bundles itself, so the binaries could not be offered under GPL-3.0. The
   viewer now links OpenSSL 3.5.9 (`openssl 3.5.9.1`, built by
   [anne-skydancer/3p-openssl](https://github.com/anne-skydancer/3p-openssl)),
   statically into the viewer as before (through `llcorehttp`, libcurl and
   the viewer itself). OpenSSL 3.x is under the **Apache License 2.0**,
   which the FSF lists as compatible with GPL-3.0 and incompatible with
   GPL-2.0 (its patent termination and indemnity terms are further
   restrictions under GPL-2.0 section 6). The FSF gives no separate ruling
   for LGPL-2.1; its section 10 forbids further restrictions in the same
   words as GPL-2.0 section 6, so Apache-2.0 code linked into the viewer has
   the standing under the LGPL-2.1 option that APR and the Apache-2.0 parts
   of WebRTC already had. OpenSSL 3 therefore removes the GPL-3.0 blocker
   and does not change the LGPL-2.1 position.
2. **GLOD: resolved.** GLOD (GLOD Open-Source License 1.0, whose clause 6
   fixes exclusive venue in the courts of Maryland, an additional term
   GPL-3.0 section 7 does not permit) was the mesh uploader's second LOD
   generator. It was removed from every viewer line on 2026-11-05 (#313
   on devel, #315-#318 on release, rust, canary and aleph); meshoptimizer
   is now the only LOD generator, and `scripts/tests/test_glod_retired.py`
   keeps it out.

Everything else linked in the shipped configuration is compatible with
GPL-3.0 (most of it also with LGPL-2.1). Several components are compatible
with GPL-3.0 but **not** with GPL-2.0 (Apache-2.0: OpenSSL 3, APR, OpenXR, and
BoringSSL and others inside WebRTC; the FreeType License): one reason the
GPL option is version 3 or later.

## What is built and shipped

The CI Release and development builds (`.github/workflows/build.yaml`,
checked by `scripts/ci_release_config.py`) configure Windows and Linux with
`OPENSIM=OFF INSTALL_PROPRIETARY=OFF USE_KDU=OFF USE_FMODSTUDIO=OFF
USE_OPENAL=OFF USE_DISCORD=OFF USE_BUGSPLAT=OFF USE_TRACY=OFF
USE_VELOPACK=OFF USE_SOLOUD=ON USE_MESAZINK=ON`, and Inno Setup on Windows.
`HAVOK` and `HAVOK_TPV` are not set, so the Havok physics extensions are
not linked. Package licence texts were read from the installed
`packages/LICENSES/` of a Windows build and from `autobuild.xml`.

Status key: **OK** compatible with GPL-3.0; **separate** a separate
program, distributed alongside (an aggregate); **blocker** linked and not
compatible; **off** not built or shipped in our configuration.

### Linked into the viewer

| Component | Licence | Status | Notes |
|---|---|---|---|
| OpenSSL 3.5.9 | Apache-2.0 | OK | GPL-3 only, not GPL-2; replaced OpenSSL 1.1.1w (open point 1, resolved) |
| GLOD 1.0pre3 | GLOD Open-Source License 1.0 | removed | Open point 2, resolved: retired on every line (#313, #315-#318) |
| APR, apr-util | Apache-2.0 | OK | GPL-3 only, not GPL-2 |
| Boost | BSL-1.0 | OK | |
| Collada DOM | MIT (package text; `autobuild.xml` says "SCEA") | OK | |
| libcurl 8.23.0 | curl (MIT-style) | OK | built against OpenSSL 3.5.9, HTTP(S) only ([anne-skydancer/3p-curl](https://github.com/anne-skydancer/3p-curl)) |
| expat | MIT | OK | |
| FreeType | FreeType License (or GPL-2.0) | OK | FTL is GPL-3-compatible, not GPL-2 |
| Hunspell | MPL-1.1 / GPL-2.0 / LGPL-2.1 tri-licence | OK | taken under LGPL-2.1 |
| libjpeg-turbo | IJG, BSD-3-Clause, Zlib | OK | |
| libndofdev (Windows), open-libndofdev (Linux) | BSD | OK | |
| libpng | libpng | OK | |
| libxml2 | MIT | OK | |
| meshoptimizer | MIT | OK | |
| mikktspace | Zlib upstream (`autobuild.xml`: Apache-2.0) | OK | either way |
| minizip-ng, zlib-ng, nanosvg | Zlib | OK | |
| nghttp2 | MIT | OK | |
| NVAPI (Windows) | MIT | OK | |
| Ogg, Vorbis | BSD-3-Clause | OK | |
| OpenJPEG | BSD-2-Clause | OK | |
| OpenXR loader | Apache-2.0 | OK | GPL-3 only |
| SoLoud, miniaudio | Zlib; public domain or MIT-0 | OK | |
| WebRTC (voice) | BSD-3-Clause, with bundled abseil, BoringSSL, perfetto, libc++ (Apache-2.0, Apache-2.0 WITH LLVM-exception), opus, libvpx, libaom, dav1d, libyuv, protobuf (BSD) | OK | BoringSSL is Apache-2.0 in this release (per the package's licence file) |
| xxHash | BSD-2-Clause | OK | |
| V-HACD | BSD-3-Clause | OK | |
| glh_linear, glext (Khronos), glm, tinygltf, tinyexr, websocketpp, cubemaptoequirectangular, three.js, jpeg_encoder_basic | BSD, MIT, Khronos | OK | headers or web content |
| gntp-growl (Windows), libnotify (Linux) | BSD; LGPL-2.1 | OK | |
| SDL2, glib, jemalloc, libuuid, breakpad, fltk (Linux) | Zlib; LGPL-2.1+; BSD-2; BSD-3; BSD-3; LGPL-2.0 with static-link exception | OK | |
| Microsoft C++ runtime (Windows) | Microsoft redistributable | OK | a System Library under GPL-3.0 section 1 |
| Mesa / Zink (`opengl32.dll`, `libgallium*`, Linux `libGLX_vulkanstorm`) | MIT | OK | the GL driver the viewer loads |

Data shipped with the viewer (fonts under OFL-1.1, the Bitstream Vera/DejaVu
licence and CC BY 4.0 for Twemoji artwork; spelling dictionaries; the CA
bundle; `lsl_definitions`; emoji shortcodes; the Tycho-2 star data,
BSD-2-Clause) is not linked code and keeps its own licence.

### Separate programs shipped with the viewer

| Component | Licence | Status | Notes |
|---|---|---|---|
| SLPlugin + `media_plugin_cef` (Dullahan, CEF, Chromium) | Dullahan MIT, CEF BSD-3-Clause, Chromium BSD-3-Clause with many permissive and LGPL parts | separate, OK | own process; SLPlugin itself is Linden Lab code (dual). Chromium's patent-encumbered codecs are a patent question, not a licence incompatibility |
| SLPlugin + `media_plugin_libvlc`, VLC 3.0.21, the Vulkanstorm speaker-fill VLC module | libVLC LGPL-2.1-or-later; VLC modules LGPL-2.1+ and GPL-2.0-or-later; speaker-fill module LGPL-2.1-or-later | separate, OK | GPL-2.0-or-later modules are GPL-3-compatible. The plugin process that loads GPL modules is already under GPL terms under either option |
| Inno Setup installer (Windows) | Inno Setup License | separate | the installer program, not the viewer |
| SLVoice (Vivox) | proprietary | separate, off | OpenSim builds only; not in our Second Life builds |

### Off in our builds (would need review before enabling)

| Component | Licence | Status if enabled |
|---|---|---|
| Kakadu (`USE_KDU`) | proprietary | blocker |
| FMOD Studio (`USE_FMODSTUDIO`) | proprietary | blocker |
| Havok / `llphysicsextensions_tpv` (`HAVOK`, `HAVOK_TPV`) | proprietary | blocker |
| BugSplat (`USE_BUGSPLAT`) | proprietary | blocker |
| Discord Social SDK (`discord_sdk`, commented out in `indra/cmake/Discord.cmake`) | proprietary | blocker |
| discord-rpc (`USE_DISCORD`) | MIT | OK |
| OpenAL Soft (`USE_OPENAL`) | LGPL-2.1-or-later | OK |
| Tracy (`USE_TRACY`), Velopack (`USE_VELOPACK`) | BSD-3-Clause; MIT | OK |

The About box's credits list (`floater_about.xml`) still names some of
these (Havok, FMOD, Kakadu) from the upstream text; they are not in our
binaries.

### Rust core (`vkstorm-core-rust`, the `vkstorm-ffi` static library)

Linked into the viewer on the `vkstorm-rust` line only (not on
`vkstorm-devel`). Its crates are `LGPL-2.1-only OR GPL-3.0-or-later`
(ports of Linden Lab code); the vendored ports keep their own licences
(`vkstorm-openjp2` BSD-2-Clause, the libogg/libvorbis part of
`vkstorm-vorbis` BSD-3-Clause, the SoLoud part of `vkstorm-audio` Zlib).
The crates.io dependencies reachable from `vkstorm-ffi` with all features
(`cargo metadata --all-features`) are under MIT, Apache-2.0, Zlib, ISC,
BSD, Unlicense, Unicode-3.0, IJG and MPL-2.0 (Symphonia); MPL-2.0 is
GPL-compatible through its section 3.3 (Secondary Licenses), and none of
them is GPL-only or proprietary. `vkstorm-http` loads the viewer's libcurl
at run time, so it links OpenSSL 3.5 as part of the viewer (open point 1, resolved).

## Source headers

Linden Lab and Firestorm headers (`$LicenseInfo` with `license=viewerlgpl`
and the like) are not changed: LGPL-2.1-only is one of the two options, so
the headers stay accurate.

Original Vulkanstorm files carry

```
SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
```

How the set was chosen (2026-11-04): files absent from upstream Firestorm
(`upstream/7.2.5_preview` and `upstream/master`) that either carried a
Vulkanstorm header claiming LGPL-2.1 only, or carried no header at all in
`scripts/` and `doc/vulkan/`:

- Vulkanstorm `$LicenseInfo` LGPL-2.1-only header replaced:
  `indra/newview/llnightsky.cpp`, `indra/newview/llnightsky.h`,
  `indra/newview/tests/llnightsky_test.cpp`,
  `indra/newview/app_settings/shaders/class1/deferred/alphaOITPackF.glsl`,
  `indra/media_plugins/libvlc/vlc_cache_gen.cpp`.
- `SPDX-License-Identifier: LGPL-2.1-only` replaced:
  `indra/newview/llscripteditorposixfile.h`, `indra/newview/llversionparser.h`,
  `indra/newview/tests/llscripteditorposixfile_test.cpp`,
  `indra/newview/tests/viewer_version_parser_test.cpp`.
- Header added: every Vulkanstorm script in `scripts/` (Python, shell and
  JavaScript, including `scripts/ci/freebsd/`, `scripts/perf/` and
  `scripts/tests/`, but not the C++ fixtures) and `doc/vulkan/*.py`, plus
  the new `indra/newview/licence_texts.py`.

`scripts/tests/test_licence_files.py` checks that no file with a Vulkanstorm
copyright line still claims LGPL-2.1 only, and that no SPDX expression in
the tree is GPL-only.

### Follow-up: Vulkanstorm files not yet marked

Added by Vulkanstorm, but not marked in this change because their origin
needs a look (they may adapt Linden Lab or Firestorm code, or stub it for
tests). Until reviewed, take them under `LGPL-2.1-only OR GPL-3.0-or-later`.

- No header: `indra/cmake/CompilerCacheHeaders.cmake`, `InnoSetup.cmake`,
  `LSLDefinitions.cmake`, `MesaZink.cmake`, `SOLOUD.cmake`,
  `websocketpp.cmake`; `indra/llaudio/tests/audio_backend_selection.cmake`,
  `llsoloud_adapter_test.cpp`; `indra/llcommon/tests/lltrace_compile_test.cpp`;
  `indra/llinventory/tests/llinventory_runtime_test.cpp`;
  `indra/llwindow/lllinuxzink.h`, `tests/linux_zink_loader_test.cpp`;
  `indra/media_plugins/libvlc/llvlcaudio.*`, `llvlcaudiobridge.*`,
  `llvlcendpoint.h`, `llvlcspeakerfillconfig.*`, `llvlcwasapi.h`,
  `prepare_miniaudio.cmake`, `tests/*`;
  `indra/newview/llgraphicsidentity.h`,
  `indra/newview/tests/llviewermedia_audio_test.cpp`;
  `scripts/tests/fixtures/*.cpp`.
- `indra/media_plugins/libvlc/vlc_speakerfill.cpp`: a VLC module, shipped
  with its source under LGPL-2.1 (`COPYING.LIB`); keep it compatible with
  VLC's LGPL-2.1-or-later.
- Added by Vulkanstorm but with a Firestorm or Linden Lab header:
  `indra/llaudio/llaudioengine_soloud.*`, `lllistener_soloud.*`
  ("Firestorm Viewer Project"), `indra/llui/llmarkdown.*` and its test
  ("Phoenix Firestorm Project"), `alphaOITResolveF.glsl` ("Linden
  Research"). Left as they are; if they are wholly original, their headers
  can move to the dual header.
