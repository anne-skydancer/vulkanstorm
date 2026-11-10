<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Volumetric clouds: the noise package (stage L7-0)

The volumetric clouds (stage L7 of the light plan) draw from three noise textures that ship as one data file,
`app_settings/clouds/cloud_noise.vkcn`. This stage adds only the file and its checks; nothing draws with it yet.

## The file

Made by [3p-cloudnoise](https://github.com/anne-skydancer/3p-cloudnoise) (release `v1.0.0`, package `cloudnoise` in
`autobuild.xml`, platform `common`), uncompressed, 8,585,472 bytes:

| Entry | Size | Source |
|---|---|---|
| base | 128 x 128 x 128 RGBA8 | TileableVolumeNoise (Sebastien Hillaire, MIT): the "unpacked" shape texture of its `main.cpp` (R Perlin-Worley, G/B/A Worley FBMs) |
| erosion | 32 x 32 x 32 RGBA8 | TileableVolumeNoise: the "unpacked" erosion texture (R/G/B Worley FBMs, A 255) |
| curl | 128 x 128 RGBA8 | **a Vulkanstorm derivation**, not TileableVolumeNoise (it has no curl noise): the curl of TileableVolumeNoise's own periodic Perlin noise (8 cells, one octave, z = 0), stored in R and G as `c * 0.5 + 0.5` |

The header has a magic (`VKCLDNZ1`), a format version, the generator id and, per entry, kind, format, size, offset,
length and the entry's SHA-256. The layout is in `indra/newview/llcloudnoise.h`.

The generator is deterministic only with one toolchain (MSVC v145 14.51, `/MT /fp:precise`, x64 SSE2, Windows SDK
10.0.26100.0): TileableVolumeNoise's hash calls `sinf`, and the UCRT's `sinf` is 1 ulp off the correctly rounded value
on 30 of the 22,060 arguments it can get. The generator checks a fingerprint of `sinf` before it runs and refuses on a
mismatch; CI generates twice and compares the bytes and the pinned SHA-256.

## What the viewer does with it

| Check | Where |
|---|---|
| The package hash (sha1) | `autobuild.xml`, entry `cloudnoise` |
| CMake stops if the file is missing or is not the file the source tree names | `indra/cmake/CloudNoise.cmake`; `CLOUD_NOISE_SHA256` there is the one place the whole-file hash is written |
| Staged next to the other app data, with the licence texts; read-only | `viewer_manifest.py` (`app_settings/clouds/`, `licenses/`; Inno `Attribs: readonly`; mode 0444 in the Linux tarball) |
| CI opens the installer or tarball and checks the file and the licences | `.github/actions/viewer-build/action.yml`, `scripts/clouds/check_cloud_noise.py` |
| Start-up check: SHA-256 of the whole file against the compiled-in hash, then the container and every entry's hash | `LLCloudNoise::loadAndVerify`, called from `LLAppViewer::init` after the cache |

If the file is missing or damaged the viewer shows "A file Vulkanstorm needs is damaged or missing
(app_settings/clouds/cloud_noise.vkcn). Please reinstall." and exits cleanly (a message box and `return false`, as for
the cache; not `LL_ERRS`, so no crash report). It is checked on every machine, including GL 3 ones, because the
check does not depend on the renderer. Only a user who edits the file in a user-writable install can hit it; the
Windows install is per-machine (Program Files).

## Changing the noise

A new noise version is a new `3p-cloudnoise` release. Change, in one commit: the `cloudnoise` entry of
`autobuild.xml` (url, sha1, version) and `CLOUD_NOISE_SHA256` in `indra/cmake/CloudNoise.cmake`. The viewer cannot
start with a mismatched pair.

## Checks

- `python scripts/tests/test_cloud_noise_package.py`: the checker on good and bad synthetic files (bad magic, version,
  header, entry fields, truncated, extended, a flipped byte in each payload, a wrong whole-file hash) and that
  CMake, autobuild, the manifest and the start-up check agree.
- `g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llcloudnoise_test.cpp -lcrypto`: the
  C++ reader on the same cases (both run in `ci-checks`).
- `python scripts/clouds/check_cloud_noise.py FILE [--licences DIR]`: a real file or an extracted installer.
