# Licensing: compatibility audit and source headers

This document backs [`NOTICE.md`](../NOTICE.md). Vulkanstorm is licensed under
`GPL-3.0-or-later` (decision of the owner, 2026-11-08). Linden Lab and
Firestorm code stays LGPL-2.1, with its headers unchanged, and is combined
into Vulkanstorm, which as a whole is licensed GPL-3.0-or-later (LGPL-2.1 is
compatible with inclusion in a GPL-3.0 work). Distributing a *binary* under
GPL-3.0 is only honest if everything linked into it, or bundled with it as part
of one work, can be distributed under GPL-3.0 too. This is the audit of what
the shipped binaries link with, re-done on 2026-11-08 on `vkstorm-devel` (re-checked for each line below) from
`autobuild.xml`, the CMake configuration, the CI configure line and the package
licence files in `packages/LICENSES/` of a local build.

## This line: `vkstorm-canary`

Audited on `vkstorm-devel` and re-checked against this line's own `autobuild.xml`
and CI configure line (2026-11-08). The configure options are devel's. This line
follows upstream Firestorm 7.2.5, which brings these package differences:

| Component | Licence | Status | Notes |
|---|---|---|---|
| SDL3 3.2.24 (replaces SDL2) | Zlib upstream (`autobuild.xml` says "lgpl") | OK | either way; the package licence label should be checked (finding 4) |
| glext 2025.10.27 | Khronos (MIT-style) | OK | |
| open-libndofdev 0.14 | BSD | OK | |
| fltk, mesa (old), libuuid, glh_linear, llappearance_utility, vlc-speakerfill | | not on this line | |

Confirmed on this line (2026-11-08): **Discord, BugSplat and KDU are off,
Havok is a stub, FMOD is off, and Vivox is gone** (see below). Mesa/Zink is not
shipped (`USE_MESAZINK` is off in the CI configuration). This line has no
GPL-only code that is not compatible with GPL-3.0.

## Vivox removed (2026-11-08)

Vivox (the SLVoice SDK) is proprietary and cannot be distributed with a
GPL-3.0 viewer. Upstream Firestorm built it for OpenSim viewers only
(`<FS:TJ> Keep Vivox only for OpenSim viewers`); Second Life builds, including
every CI build, never contained it, so those builds behave exactly as before.
Removed: `llvoicevivox.*`, its selection in `llvoiceclient.cpp`, the
`slvoice` package in `autobuild.xml`, the SLVoice and Vivox library staging in
`viewer_manifest.py`, `Copy3rdPartyLibs.cmake` and the installers, the Vivox
acceptable use policy page (`VivoxAUP.*`), the Vivox logo of the start screen,
the `Vivox*` debug settings and the Wine voice proxy settings, and the Vivox
licence text in `licenses-*.txt`. OpenSim builds keep voice through WebRTC
(`llwebrtc`, the os-webrtc-janus compatible path of Firestorm 7.1.10 and
later); the voice-server selection no longer offers Vivox.

## Status key

**OK** compatible with GPL-3.0; **separate** a separate program, distributed
alongside (an aggregate, GPL-3.0 section 5); **blocker** linked and not
compatible; **off** not built or shipped in our configuration; **data** not
code.

### Linked into the viewer

| Component | Licence | Status | Notes |
|---|---|---|---|
| OpenSSL 3.5.9 | Apache-2.0 | OK | GPL-3 only, not GPL-2; [3p-openssl](https://github.com/anne-skydancer/3p-openssl) |
| APR, apr-util | Apache-2.0 | OK | GPL-3 only |
| Boost | BSL-1.0 | OK | |
| Collada DOM | SCEA Shared Source License 1.0 (MIT-style; package text says MIT) | OK | |
| libcurl 8.23 | curl (MIT-style) | OK | [3p-curl](https://github.com/anne-skydancer/3p-curl); HTTP and HTTPS only, OpenSSL 3.5, nghttp2, zlib-ng; no libssh2, brotli, zstd, GSS |
| expat | MIT | OK | |
| FreeType | FreeType License (or GPL-2.0) | OK | FTL is GPL-3-compatible |
| Hunspell | MPL-1.1 / GPL-2.0 / LGPL-2.1 tri-licence | OK | GPL-2.0-or-later option; taken under LGPL-2.1 or the GPL |
| libjpeg-turbo | IJG, BSD-3-Clause, Zlib | OK | |
| libndofdev (Windows), open-libndofdev (Linux) | BSD | OK | |
| libpng | libpng | OK | |
| libxml2 | MIT | OK | |
| meshoptimizer | MIT | OK | the only mesh LOD generator (GLOD is gone) |
| mikktspace | Zlib upstream (`autobuild.xml`: Apache-2.0) | OK | either way |
| minizip-ng, zlib-ng, nanosvg | Zlib | OK | |
| nghttp2 | MIT | OK | |
| NVAPI (Windows) | MIT | OK | |
| Ogg, Vorbis | BSD-3-Clause | OK | |
| OpenJPEG 2.5.4 | BSD-2-Clause | OK | [3p-openjpeg](https://github.com/anne-skydancer/3p-openjpeg) |
| OpenXR loader | Apache-2.0 | OK | GPL-3 only |
| SoLoud 202002 (miniaudio backend), miniaudio | Zlib; public domain or MIT-0 | OK | [3p-soloud](https://github.com/anne-skydancer/3p-soloud); built with `WITH_MINIAUDIO=1 WITH_NOSOUND=1 WITH_NULL=1` only |
| WebRTC (voice) m144 | BSD-3-Clause, with bundled abseil, BoringSSL, perfetto, libc++ (Apache-2.0, Apache-2.0 WITH LLVM-exception), opus, libvpx, libaom, dav1d, libyuv, protobuf (BSD) | OK | the package licence file lists no FFmpeg, OpenH264 or GPL component |
| xxHash | BSD-2-Clause | OK | |
| V-HACD | BSD-3-Clause | OK | |
| glh_linear, glext (Khronos), glm, tinygltf, tinyexr, websocketpp, vulkan_gltf, cubemaptoequirectangular, three.js, jpeg_encoder_basic | BSD, MIT, Khronos | OK | headers or web content; `jpegencoderbasic` is labelled "NONE" in `autobuild.xml` but its licence file is Adobe's BSD-style licence |
| gntp-growl (Windows), libnotify (Linux) | BSD; LGPL-2.1 | OK | |
| SDL3, glib, jemalloc, breakpad, pcre (Linux) | Zlib; LGPL-2.1+; BSD-2; BSD-3; BSD | OK | |
| Microsoft C++ runtime (Windows) | Microsoft redistributable | OK | treated as a System Library under GPL-3.0 section 1 |
| viewer-manager | Linden Lab LGPL-2.1 (`viewerlgpl`) | OK | Linden Lab code, LGPL-2.1, part of the GPL-3.0 whole |
| TileableVolumeNoise (the cloud noise file is generated by it) | MIT, Copyright (c) 2017 Sébastien Hillaire | data, OK | not linked: its output is shipped as `app_settings/clouds/cloud_noise.vkcn`, with its licence text in `licenses/` |
| glm 0.9.7 (generator only) | MIT ("Happy Bunny"), Copyright (c) 2005 - 2015 G-Truc Creation | OK, not shipped | runs the generator on CI only; no glm code is in the viewer or the data file. The "Restrictions" line of its licence (using the software for military purposes makes a Bunny unhappy) is a non-binding wish, not a licence term (owner decision, 2026-11-10) |
| 3p-cloudnoise generator ([3p-cloudnoise](https://github.com/anne-skydancer/3p-cloudnoise)) | MIT, Copyright (c) 2026 Anne Skydancer | OK, not shipped | the code that makes the noise file; its licence text ships in `licenses/cloudnoise.txt` |

Data shipped with the viewer (fonts under OFL-1.1, the Bitstream Vera/DejaVu
licence and CC BY 4.0 for Twemoji artwork; spelling dictionaries; the CA
bundle; `lsl_definitions`; emoji shortcodes; the Tycho-2 star data,
BSD-2-Clause; the cloud noise, TileableVolumeNoise, MIT) is not linked code and
keeps its own licence.

The cloud noise (`app_settings/clouds/cloud_noise.vkcn`, the `cloudnoise`
package) is data generated by the code of
[3p-cloudnoise](https://github.com/anne-skydancer/3p-cloudnoise) from
**TileableVolumeNoise** (MIT, Copyright (c) 2017 Sébastien Hillaire,
<https://github.com/sebh/TileableVolumeNoise>, commit 1f879a26): its base
(128^3) and erosion (32^3) volumes are that code's own output, and its curl
texture is a Vulkanstorm derivation from the same Perlin noise (said so in the
3p-cloudnoise README and in the generator's source; the container header only
carries the generator id). The generator also uses
**glm 0.9.7** as TileableVolumeNoise vendors it (MIT, "Happy Bunny";
Copyright (c) 2005 - 2015 G-Truc Creation); no glm code is in the shipped file.
glm's "Restrictions" line (military use makes a Bunny unhappy) is a non-binding
wish, not a licence term; the owner accepted it as such (2026-11-10).
The generator's own code is MIT. The licence texts of all three ship in
`licenses/` (`TileableVolumeNoise.txt`, `glm.txt`, `cloudnoise.txt`), and the
installer-contents check of CI requires them.

### Separate programs shipped with the viewer

| Component | Licence | Status | Notes |
|---|---|---|---|
| SLPlugin + `media_plugin_cef` (Dullahan, CEF, Chromium) | Dullahan MIT (upstream `secondlife/dullahan` LICENSE.txt, "Copyright (c) 2016, Linden Research, Inc."), CEF BSD-3-Clause, Chromium BSD-3-Clause with many permissive and LGPL parts | separate, OK | own process; SLPlugin itself is Linden Lab code. The FFmpeg inside Chromium is LGPL, compatible |
| SLPlugin + `media_plugin_libvlc`, VLC 3.0.21 (devel only: the Vulkanstorm speaker-fill VLC module) | libVLC LGPL-2.1-or-later; VLC modules LGPL-2.1+ and GPL-2.0-or-later; speaker-fill module LGPL-2.1-or-later ([3p-vlc-speakerfill](https://github.com/anne-skydancer/3p-vlc-speakerfill)) | separate, OK | GPL-2.0-or-later modules are GPL-3-compatible; the plugin process that loads them is under GPL terms. See finding 2 |
| Inno Setup installer (Windows) | Inno Setup License | separate | the installer program, not the viewer |

### Off in our builds (would need review before enabling)

| Component | Licence | Status if enabled |
|---|---|---|
| Kakadu (`USE_KDU`) | proprietary | blocker |
| FMOD Studio (`USE_FMODSTUDIO`) | proprietary | blocker |
| Havok / `llphysicsextensions_tpv` (`HAVOK`, `HAVOK_TPV`) | proprietary | blocker; the stub is used |
| BugSplat (`USE_BUGSPLAT`) | proprietary | blocker |
| Discord Social SDK (`discord_sdk`, commented out in `indra/cmake/Discord.cmake`) | proprietary | blocker |
| `llappearance_utility` (Linux, `INSTALL_PROPRIETARY`) | Linden Lab proprietary | blocker |
| Vivox / SLVoice | proprietary | removed from the tree on 2026-11-08 |
| discord-rpc (`USE_DISCORD`) | MIT | OK |
| OpenAL Soft (`USE_OPENAL`) | LGPL-2.1-or-later | OK |
| Mesa / Zink (`USE_MESAZINK`) | MIT | OK, not shipped |
| Tracy (`USE_TRACY`), Velopack (`USE_VELOPACK`) | BSD-3-Clause; MIT | OK |

The About box's credits list (`floater_about.xml`) still names some of
these (Havok, FMOD, Kakadu) from the upstream text; they are not in our
binaries.

### Rust core (`vkstorm-core-rust`, the `vkstorm-ffi` static library)

Linked into the viewer on the `vkstorm-rust` line only (not on
`vkstorm-devel`). Its crates are `GPL-3.0-or-later` (original code) or ports
of Linden Lab code (LGPL-2.1, part of the GPL-3.0 whole); the vendored ports keep their own
licences (`vkstorm-openjp2` BSD-2-Clause, the libogg/libvorbis part of
`vkstorm-vorbis` BSD-3-Clause, the SoLoud part of `vkstorm-audio` Zlib). The
crates.io dependencies reachable from `vkstorm-ffi` with all features
(`cargo metadata --all-features`) are under MIT, Apache-2.0, Zlib, ISC, BSD,
Unlicense, Unicode-3.0, IJG and MPL-2.0 (Symphonia); MPL-2.0 is
GPL-compatible through its section 3.3 (Secondary Licenses), and none of them
is GPL-only or proprietary. `vkstorm-http` loads the viewer's libcurl at run
time, so it links OpenSSL 3.5 as part of the viewer.

### Forks of third-party packages (`anne-skydancer/3p-*`)

[3p-curl](https://github.com/anne-skydancer/3p-curl),
[3p-openssl](https://github.com/anne-skydancer/3p-openssl),
[3p-openjpeg](https://github.com/anne-skydancer/3p-openjpeg),
[3p-soloud](https://github.com/anne-skydancer/3p-soloud),
[3p-mesazink](https://github.com/anne-skydancer/3p-mesazink) and
[3p-vlc-speakerfill](https://github.com/anne-skydancer/3p-vlc-speakerfill)
carry their upstream licences (curl, Apache-2.0, BSD-2-Clause, Zlib, MIT,
LGPL-2.1-or-later); they are not relicensed and stay listed in the tables
above.
[3p-cloudnoise](https://github.com/anne-skydancer/3p-cloudnoise) is not a fork:
it is a generator (MIT) that vendors TileableVolumeNoise (MIT) and glm 0.9.7
(MIT), see the data paragraph above.

## Findings

Open items from the audit. None is a linked incompatible library; all are
checks or clean-ups listed as follow-ups.

1. **Linden Lab and Firestorm code that is not LGPL.** The Linden Lab
   `llappearance_utility` is proprietary and off. Anything else with a
   `$LicenseInfo` header other than `viewerlgpl` or `fsviewerlgpl` has not
   been audited file by file; the headers of the tree were sampled, not
   enumerated. Uncertain.
2. **VLC modules.** VLC 3.0.21 is LGPL and GPL (libVLC and most modules
   LGPL-2.1-or-later, others GPL-2.0-or-later), both compatible with GPL-3.0.
   The `vlc-bin` package licence file holds only the GPL-2.0 text, so the one
   remaining check is that no shipped module is GPL-2.0-only. Low risk
   (separate process).
3. **Stale licence text files.** `indra/newview/licenses-{win32,linux,mac}.txt`
   still carry the OpenSSL 1.x and SSLeay licence text (OpenSSL 3.5 is
   Apache-2.0) and credits for packages Vulkanstorm does not ship. Harmless,
   but the files should be regenerated from the package list.
4. **SDL3 licence label.** `autobuild.xml` labels the SDL3 package "lgpl"; SDL3 is Zlib-licensed upstream. Either is compatible; check the package's licence file and fix the label.

## Spelling dictionary: Russian

The `dictionaries` package ships a `ru_RU` dictionary whose licence ("use,
copy, redistribute granted; modifications only as patches; all other rights
reserved") is not a free-software licence. It is replaced by the LibreOffice
Russian dictionary (<https://github.com/LibreOffice/dictionaries>, `ru_RU/`,
commit eff9495b70722d8e590e97b1710cfc7c00f6ce72 of 2024-07-28). It is the
same dictionary (Alexander I. Lebedev's Russian ispell data, converted for
Hunspell, plus a `TRY` line by Laszlo Nemeth) under a BSD-3-Clause-style
licence with a clause that modified versions be clearly marked, which is
GPL-3.0 compatible. The files are `indra/newview/dictionaries-ru/ru_ru.aff`
and `ru_ru.dic` (the names the package uses; the `.aff` is UTF-8 where the
old one was KOI8-R, which Hunspell reads from its `SET` line) with the
licence text beside them. `viewer_manifest.py` copies them over the package's
files, so the viewer loads the same names. The other dictionaries are LGPL,
MPL/GPL/LGPL tri-licences or BSD (data).

## Source headers

Linden Lab and Firestorm headers (`$LicenseInfo` with `license=viewerlgpl`
and the like) are not changed.

Original Vulkanstorm files carry

```
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
```

On 2026-11-08 every file with the dual Vulkanstorm header
(`LGPL-2.1-or-later OR GPL-3.0-or-later`) was changed to
`GPL-3.0-or-later`, together with the licence prose of the C++ and GLSL
headers. How the set had been chosen (2026-11-04): files absent from upstream
Firestorm (`upstream/7.2.5_preview` and `upstream/master`) that either carried
a Vulkanstorm header claiming LGPL-2.1 only, or carried no header at all in
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
  `indra/newview/licence_texts.py`.

`scripts/tests/test_licence_files.py` checks that the licence files are in
place, that no file with a Vulkanstorm copyright line carries another SPDX
expression than `GPL-3.0-or-later`, that every SPDX expression in the tree is
compatible with GPL-3.0, and that Vivox stays out.

### Follow-up: Vulkanstorm files not yet marked

Added by Vulkanstorm, but not marked because their origin needs a look (they
may adapt Linden Lab or Firestorm code, or stub it for tests). Until reviewed,
take them under the GPL, version 3 or later.

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
  can move to the GPL header.
- Added by Vulkanstorm, upstream text kept: `scripts/shadows/legacy_reference/*.cpp.txt`
  (excerpts of Firestorm 7.2.5's `pipeline.cpp` for the Legacy shadow method check,
  with the Linden Research header and `SPDX-License-Identifier: LGPL-2.1-only`) and
  `scripts/perf/reference/shadowUtil_before_spot_fade.glsl` (an earlier
  `shadowUtil.glsl`, Linden Research header unchanged). Left under LGPL-2.1.
