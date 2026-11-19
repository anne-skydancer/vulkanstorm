<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Volumetric clouds (stage L7)

The volumetric clouds (stage L7 of the light plan) draw from three noise textures that ship as one data file,
`app_settings/clouds/cloud_noise.vkcn` (stage L7-0, the first sections). Stage L7 draws them: the main view and every
photo show automatically generated volumetric clouds, their kind and variability set by the sky's cloud sliders, and
they cast the ground's cloud shadows (section "L7: the clouds").

## The file

Made by [3p-cloudnoise](https://github.com/anne-skydancer/3p-cloudnoise) (release `v1.0.0`, package `cloudnoise` in
`autobuild.xml`, platform `common`), uncompressed, 8,585,472 bytes:

| Entry | Size | Source |
|---|---|---|
| base | 128 x 128 x 128 RGBA8 | TileableVolumeNoise (Sébastien Hillaire, MIT): the "unpacked" shape texture of its `main.cpp` (R Perlin-Worley, G/B/A Worley FBMs) |
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
| CI checks the staged file and licences of every build, and for package builds installs the Windows installer silently (file, licences, read-only attribute, removal by the uninstaller) or extracts the Linux tarball | `.github/actions/viewer-build/action.yml`, `scripts/clouds/check_cloud_noise.py` |
| Start-up check: SHA-256 of the whole file against the compiled-in hash, then the container and every entry's hash | `LLCloudNoise::loadAndVerify`, called from `LLAppViewer::init` before the cache |

If the file is missing or damaged the viewer logs a warning (`LL_WARNS("InitInfo")`) and starts, with no message box
and no exit (owner decision, 2026-11-12; the `MBCloudNoiseDamaged` string is removed). When the volumetric clouds first
draw they read the file again (on a worker thread) and, if it is bad, draw the flat layer and show the notice
`CloudNoiseDamaged` once a session. The configure-time check in `CloudNoise.cmake` still stops a build without
the file. The check runs before the cache is opened, on every machine,
because it does not depend on the renderer. It reads the file once (size checked first), hashes it twice (the whole
file, then each entry), and frees the 8.6 MB again; the clouds load and verify it again when they first draw
(`LLCloudNoise::loadInto`, which shares no state, on the General worker thread). An
open that fails is logged with its reason and tried once more after 200 ms. Besides a user who edits the file in a
user-writable install, the warning can be caused by an antivirus quarantining or locking the file, disk errors, or an
interrupted upgrade; reinstalling fixes all of them.

Read-only: the Inno installer (the Windows installer CI builds) installs the file with the read-only attribute and the
flags `overwritereadonly` and `uninsremovereadonly`, so upgrades replace it and the uninstaller removes it; the Linux
tarball carries mode 0444. The Velopack installer, which these builds do not use (`USE_VELOPACK=OFF`), is not covered.

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

## L7: the clouds

The stack (owner, 2026-11-18): **the physical clear base sky, then the automatically generated volumetric clouds, then
the cloud kind and variability layer, then the EEP atmospheric filter.** Every layer lives in the passes' uniforms and
textures, never in a pass over the finished image, so a photo (snapshot, tiled high resolution, 360, saved) draws the
same clouds as the screen.

The model is the Aurora Viewer's volumetric clouds (`crates/aurora-render/src/clouds.rs`, `shaders/clouds.wgsl`, the
owner's own code, GPL-3.0-or-later, used one way), after Schneider and Vos (SIGGRAPH 2015, Nubis 2017): a weather map,
a height profile per cloud type, a Perlin-Worley base noise eroded by a Worley noise with a curl offset; Hillaire's
energy-conserving integration (SIGGRAPH 2016), Wrenninge's multiple-scattering octaves and the approximate Mie phase of
Jendersie and d'Eon (SIGGRAPH 2023). Ported to OpenGL 4.6 fragment passes (no compute).

### The cloud kind and variability layer (`llcloudscape.h`)

Renderer-free, header-only, tested by `indra/newview/tests/llcloudscape_test.cpp` (CI `ci-checks`, at -O0 and -O2). From
the EEP cloud sliders alone (decision D2 (a) of the 2026-11-18 plan: every EEP works, other viewers see the same
sliders) it sets up to three bands, sorted by height, each with a WMO genus and its height range, height profile,
extinction, erosion, noise scale and drift:

| Band | Genera | From |
|---|---|---|
| the creator's band: the sky's own cloud pattern (the flat layer's texture and taps) | low: cumulus, stratocumulus, stratus (the owner's Aurora ladder, by **Coverage**); grown by **Density D** into cumulus congestus (tops to 6 km), cumulonimbus with an anvil (tops at 11 km), or nimbostratus from a sheet. Middle: altocumulus, altostratus. High: cirrocumulus, cirrostratus | the etage from **cloud scale**, the WMO's element-size criterion (Sc wider than 5 degrees, Ac 1 to 5, Cc under 1): below `RenderCloudscapeMiddleScale` (0.15) the layer stands at 3 km, below `RenderCloudscapeHighScale` (0.05) at 7 km, rising smoothly over a quarter of each value |
| middle (generated) | altocumulus cells | **Detail D** above `RenderCloudscapeAltocumulusDetail` (0.25), over a low creator's band |
| high (generated) | cirrus fibres (the noise stretched along the wind), cirrostratus as it closes | **cloud variance** (the flat layer's streaks): `RenderCloudscapeCirrusVariance` (0.8) per unit, plus `RenderCloudscapeCirrusBase` (0) |

Heights are WMO's for temperate latitudes, above the camera's ground (the sky is fixed for the observer, #439). The
extinction per genus comes from the OPAC cloud models (Hess, Koepke and Schult 1998, BAMS 79, Tables 1a and 1b) through
beta = 3 L / (2 rho r_eff): stratus 57.3, cumulus 67.6, stratocumulus 34.5 (OPAC's own Sc distribution), cirrus 0.46
km^-1; altostratus (6.5) and cirrocumulus (2.5) have no OPAC model and come from their optical depths. One fill factor
(1.63) scales them all so that cumulus keeps the Aurora Viewer's tuned 110 km^-1 at the noise's density 1; Density D
scales the extinction as in Aurora (clamped at 1.5).

Variability, mean-preserving: every band's cover c becomes c (1 + (1 - c) M), with M a zero-mean field in [-1, 1]
independent of c, so the mean cover stays the sky's (the shadow map's mean and the overcast veil do not move) and no
cover leaves [0, 1]. M is a patch field (a few plane waves, mean exactly 0.5 over the map; amplitude
`RenderCloudscapePatchBase` 0.3 plus `RenderCloudscapePatchVariance` 0.4 per unit of cloud variance) plus a growth and
decay field (the base noise's Worley FBM moving through its third axis, so that a cell lives `RenderCloudscapeLifetime`,
1200 s; amplitude `RenderCloudscapeGrowth` 0.3; its mean over the map is the slice's, computed from the noise file).
The growth runs on the cloud scroll's clock: a paused scroll, or a sky whose clouds stand still, freezes it. The bands
drift with the sky's scroll (cloudsF.glsl: the pattern moves east with the scroll's x and north with its y), faster
aloft: (1 + h / `RenderCloudscapeShearHeight`) / (1 + h0 / it), with h0 the creator's band's lower part.

The light on the clouds is the EEP filter on them (decision D5 (a)): the flat layer's own terms, read from the sky's
uniforms (cloudsV.glsl). The sun at the cloud layer (AT1's transmittance) is tinted by the cloud colour and fades with
the clouds' own sun/moon handover (L3); the ambient is lifted by the cover; the haze in front of a cloud is cloudsV.glsl's
haze below the clouds at the cloud's own distance (the haze ends at max_y). `RenderVolumetricCloudsSunGain` (4.5) and
`RenderVolumetricCloudsSkyGain` (0.9) are the Aurora Viewer's gains, fitted side by side with the flat layer.
`LLCloudscape::cloud_light()` is the C++ twin of the shader's light inputs.

### The passes (`llvolumetricclouds.{h,cpp}`, `app_settings/shaders/class1/deferred/cloudsVol*.glsl`)

| Pass | Size | Program | When |
|---|---|---|---|
| noise upload | 128^3 + 32^3 + 128^2 RGBA8 | - | once: the file read and checked on the General worker thread, then 8 slices of the base noise a frame (16 frames); the flat layer draws meanwhile |
| weather | 512^2 (one tile); 2048^2 with cloud variance (16 tiles: the flat layer's disturbance repeats every 16) | `cloudsVolWeatherF.glsl` | a change of what it is made of (sliders, textures as they load, the day cycle's blend) or a 2 s step of the growth clock: one cycle of 128-row strips, one a frame. A new side or period (the first map, cloud variance turned on or off) is built in a second map a strip a frame while the first stays in use, then swapped in: nothing flashes, no frame draws a whole map |
| trace | W/4 x H/4, RGBA16F + R16F | `cloudsVolTraceF.glsl` + `cloudsVolFuncsF.glsl` | every frame: a quarter of the half-resolution pixels near the sky (the deferred depth at the far plane), on a checkerboard |
| reconstruction | W/2 x H/2 RGBA16F, ping-pong | `cloudsVolReconF.glsl` | every frame: reprojected for the rotation and the horizontal move, clamped to the fresh 3x3 neighbourhood; dropped on a resize, a camera jump over 1 km or a frame without the clouds |
| composite | full screen | `cloudsVolCompositeV/F.glsl` | after the lighting and the local lights, before the transparent and water passes: `dst * T + L` on the sky's pixels (the screen triangle at the far plane, LEQUAL against the deferred depth), the alpha (glow) untouched |
| still | W x H RGBA16F | the trace with `CLOUDS_VOL_STILL` | a photo (`gSnapshot`: rawSnapshot and its tiles, simpleSnapshot, the 360 capture): every pixel, twice the steps, 4 sub-samples keyed on the pixel's place in the whole photo |
| cloud shadow map | 512^2 RG8 (the L2 map) | `cloudShadowVolF.glsl` | every frame while the clouds draw (`LLCloudShadowMap::update`): 16 steps through the creator's band and 6 through each other along the light, then L2b's optics (beam, diffuse) |

Steps: the creator's band 64 (up to 96 on a low band's grazing paths, no step longer than 0.3 km), the altocumulus band
32, cirrus 16; light taps 5, 3 and 2. The sky's pool skips the flat layer in the main view and the photos while the
clouds draw (`LLDrawPoolWLSky::renderDeferred`); cube snapshots (reflection probes, mirrors) keep the flat layer (the
cloud panorama for reflections is L7-4). Everything runs on the main (GL) thread; `LLVolumetricClouds::release()` frees
it in `LLPipeline::releaseGLBuffers`.

### The toggle

**Preferences > Graphics > Volumetric clouds** (`RenderVolumetricClouds`, the one Preferences entry decided for L7). The
feature tables recommend it from the **High** graphics level up (GPU class 4: about RTX 3060-class memory bandwidth and
up) and off below; the setting's own default is off, so an existing install keeps the flat layer until the checkbox is
ticked or a graphics level is picked. Turning it off frees the noise (8.5 MB of VRAM) and the targets.

### Cost (measured)

`scripts/light/check_volumetric_clouds.py` builds the six programs from these shader files the way the viewer does,
checks that every uniform the C++ side sets is live, runs them on the real noise file with llcloudscape.h's bands for
eight slider presets, checks the output (finite, transmittance in [0, 1], the shadow map adds no light, cloud where the
sliders put cloud), that the GPU weather map keeps every band's mean cover and that the shader's light inputs match
LLCloudscape::cloud_light (24 skies, worst relative error 8e-7), and times each draw alone. RTX 5070 Ti, driver 621.14,
every pixel sky (the worst case: in-world, buildings and terrain cover part of it), medians of 20 frames, the two views
(12 and 60 degrees up); the CPU was busy with another build, which the draw-only timing does not see:

| Preset (coverage, scale, variance, D, detail) | Bands | 1080p total (trace + reconstruction + composite), ms | 1440p total, ms |
|---|---|---|---|
| fair (0.27, 0.42, 0, 1, 0.12) | Cu | 0.18-0.22 | 0.26-0.28 |
| broken (0.38, ...) | Cu | 0.26-0.27 | 0.30-0.36 |
| stratocumulus (0.45, ...) | Sc | 0.21-0.23 | 0.24-0.31 |
| overcast (0.62, ...) | St | 0.20-0.23 | 0.27-0.34 |
| congestus (0.30, 0.42, 0, 1.6, 0.12) | Cu con | 0.22-0.30 | 0.29-0.33 |
| cumulonimbus (0.30, 0.42, 0, 2.6, 0.12) | Cb | 0.31-0.35 | 0.43-0.44 |
| altocumulus layer (0.30, 0.12, 0, 1, 0.12) | Ac | 0.24-0.37 | 0.38-0.51 |
| three bands (0.27, 0.42, 0.6, 1, 0.8) | Cu, Ac, Ci | 0.37-0.42 | 0.52-0.59 |

The reconstruction is 0.011-0.037 ms and the composite 0.017 ms (1080p) and 0.03 ms (1440p) of these. The cloud shadow
map is 0.028-0.052 ms; the weather map, drawn in strips of a quarter (512^2) or a sixteenth (2048^2, cloud variance) of
it a frame while it refreshes, is at most 0.3 ms whole at 512^2 and 0.47 ms at 2048^2. A photo's still trace is 11-24 ms
a 1080p frame, once. The single-band presets meet the plan's L7-2 figure (0.30 ms at 1080p, 0.35 ms at 1440p) but for
the towers and the altocumulus layer seen low, which march farther.

Against the fps floor (about 60 fps standing in a crowded room, never below 25 fps moving), the worst case with the
shadow map is 0.47 ms at 1080p and 0.64 ms at 1440p on this GPU: 60 becomes 58.4 and 57.8 fps, 25 becomes 24.7 and
24.6 fps, when the frame is GPU-bound (the crowded room is CPU-bound, 31.9 ms on the main thread, and the passes overlap
it; the main thread pays the uniforms and binds, a few microseconds). The cost does not grow with movement. Other GPU
classes, estimated by FP32 (x3.5 an RTX 3060, x9 a GTX 1660): 1.6-2.2 ms and 4.2-5.8 ms (60 becomes 54.7-53.0 and
47.9-44.5 fps); the toggle is off by default below the High level, the 1660's class.

### Not done, not verified

- No in-world check: the look against the flat layer and the Aurora Viewer's demo skies (the noise file differs from
  the light study bench's generated noise: expect the re-tuning pass L7-0 foresaw), the photo-against-screen check, a
  region crossing and a 360 degree turn for shimmer (the plan's L7-2 gates).
- The per-sky calibration of the clouds' mean luminance to the flat layer's (+-10 %, D5 (a)): the gains are the Aurora
  Viewer's, as debug settings.
- AT2's physical base for the clouds' light (the sky light at the cloud layer) and far haze (AT2-4) do not exist yet:
  the clouds read the flat layer's light and haze terms, which are the creator's look the filter keeps.
- Reflections (probes, water, mirrors) show the flat layer until L7-4's cloud panorama.
- The slider-to-genus mapping is a design from WMO criteria with no C++ oracle; its thresholds are debug settings.
