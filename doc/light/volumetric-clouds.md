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

If the file is missing or damaged the viewer logs a warning (`LL_WARNS("InitInfo")`) and starts: until the clouds
draw (L7-2) nothing reads the file, so there is no message box and no exit (owner decision, 2026-11-12; the
`MBCloudNoiseDamaged` string is removed). **What a damaged file means once the clouds draw is pending the owner's
decision.** The 2026-11-12 decision held only until L7-2 uses the file; the decision before it (2026-11-08, #429
decision 9) was a fatal "file damaged, reinstall" error and an exit, explicitly not a flat-layer fallback. The options
are: fatal as decided on 2026-11-08; the flat layer with a notice; or a notice only. Until the owner answers, the
volumetric clouds read the file again when they first draw (on a worker thread) and, if it is bad, draw the flat layer
and show the notice `CloudNoiseDamaged` once a session. The configure-time check in `CloudNoise.cmake` still stops a
build without the file. The check runs before the cache is opened, on every machine,
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
the EEP cloud sliders alone (decision D2 (a) of the 2026-11-18 plan, taken as recommended: every EEP works, other viewers
see the same sliders) it sets up to three bands, sorted by height, each with a WMO genus and its height range, height
profile, extinction, erosion, noise scale, march budget and drift:

| Band | Genera | From |
|---|---|---|
| the creator's band: the sky's own cloud pattern (the flat layer's texture and taps) | low: cumulus, stratocumulus, stratus (the owner's Aurora ladder, by **Coverage**); grown by **Density D** into cumulus congestus (tops to 6 km), cumulonimbus with an anvil (tops at 11 km), or nimbostratus from a sheet. Middle: altocumulus, altostratus. High: cirrocumulus, cirrostratus | the etage from **cloud scale**, the WMO's element-size criterion (Sc wider than 5 degrees, Ac 1 to 5, Cc under 1): below `RenderCloudscapeMiddleScale` (0.15) the layer stands at 3 km, below `RenderCloudscapeHighScale` (0.05) at 7 km, rising smoothly as the scale falls from 1.25 to 0.8 times each value; 0 never lifts the layer |
| middle (generated) | altocumulus cells | the debug setting `RenderCloudscapeAltocumulusCover` (default 0: no band), over a low creator's band. No EEP lever drives it: Detail D, the first one tried, is 1 (its maximum) in `LLSettingsSky::defaults()`, so every sky made in the EEP editor would have had a 60 % altocumulus deck. Which lever, or new EEP keys (D2 (b)), is the owner's decision |
| high (generated) | cirrus fibres (the noise stretched along the wind), cirrostratus as it closes | **cloud variance** (the flat layer's streaks): `RenderCloudscapeCirrusVariance` (0.8) per unit, plus `RenderCloudscapeCirrusBase` (0). None of the 768 library skies has a cloud variance |

The default sky (`LLSettingsSky::defaults()`, the one every new EEP sky starts from) and the legacy `Default.xml` are
one band of fair-weather cumulus at 1.2-2 km.

**The bands never overlap in height**, so the trace composites them front to back and the light reaching a band passes
through the bands above it: a generated band that would reach into the band below (towers, nimbostratus) stands on its
top instead (0.1 km above), and fades out as its base reaches the ceiling of its etage (the middle etage 7 km, the high
13 km). So cirrus over a cumulonimbus stands at 11.1-12.3 km, altocumulus over congestus on the towers' tops, and no
altocumulus band stands over a cumulonimbus.

Heights are WMO's for temperate latitudes, **above the observer**: like the flat dome, the cloud layer keeps its height
above the camera (the sky is fixed for the observer, #439), so a skybox at 3000 m has its cumulus at 4.2-5 km absolute and
a flight never reaches the layer. The cloud shadows on the ground stay camera-independent. Physical altitudes (the camera
above the region's ground, flying into and above a layer) would be a change of that rule: the owner's decision.

The extinction per genus comes from the OPAC cloud models (Hess, Koepke and Schult 1998, BAMS 79, Tables 1a and 1b)
through beta = 3 L / (2 rho r_eff): stratus 57.3, cumulus 67.6, stratocumulus 34.5 (OPAC's own Sc distribution), cirrus
0.46 km^-1; altostratus (6.5) and cirrocumulus (2.5) have no OPAC model and come from their optical depths. One fill
factor (1.63) scales them all so that cumulus keeps the Aurora Viewer's tuned 110 km^-1 at the noise's density 1;
Density D scales the extinction as in Aurora (clamped at 1.5).

Every parameter of the creator's band is continuous in cloud scale, so a day cycle that blends it across a threshold
changes nothing at once (the test walks the scale in steps of 0.0001): the heights, the profile, the extinction and the
erosion are blended; the base noise's tile (10.5 km low, a half in the middle etage, a quarter in the high one) is
cross-faded in the middle of each rise, where the genus changes (two base-noise fetches there only); the trace's reach
(45 km low, 120 km aloft) and the step cap ease as the base rises from 2 to 3 km; the march budget follows the etage as
the generated bands of the same etages have it (64 steps and 5 light taps low, 32 and 3 in the middle, 16 and 2 high), so
a thin sheet aloft, which the trace never leaves early, costs what the generated bands cost.

**The etage rule changes the look of about a quarter of the library: the owner's decision.** 210 of the 768 library skies
have a cloud scale under 0.1875 (166 rise into the middle etage, 44 at least partly into the high one, 5 of them
overcast). Measured in the offline check (the same stand-in cloud texture, the library's median Density D 0.71 and
Detail D 0.125; the flat layer's mean opacity over a tile against the volumetric clouds' 1 - mean transmittance in the
zenith and horizon views):

| Sky | Etage, genus | Flat layer | Volumetric (zenith, horizon) | Trace at 1080p, ms |
|---|---|---|---|---|
| broken (coverage 0.30), scale 0.42 | low, Cu 1.2-2 km | 0.29 | 0.31, 0.29 | 0.20-0.21 |
| broken, scale 0.12 | middle, Ac 3-3.6 km | 0.29 | 0.13, 0.17 | 0.12-0.17 |
| broken, scale 0.03 | high, Cc 7-7.4 km | 0.29 | 0.01, 0.02 | 0.11-0.12 |
| overcast (0.62), scale 0.42 | low, St 0.4-0.9 km | 1.00 | 0.99, 0.67 | 0.16-0.24 |
| overcast, scale 0.12 | middle, As 3-4.5 km | 1.00 | 0.99, 0.66 | 0.32-0.37 |
| overcast, scale 0.03 | high, Cs 7-8.2 km | 1.00 | 0.33, 0.37 | 0.48-0.52 |

The low etage keeps the creator's opacity; the middle etage keeps an overcast sky's but thins broken cells; the high
etage turns the creator's opaque small clouds into a faint veil (cirrocumulus) or a thin one (cirrostratus), which is
physical for those genera but not the creator's look (#543: the visible sky is the creator's look). The options for the
decisions page: (a) the WMO rule as it is (physical genera aloft); (b) the WMO heights and element sizes with the
creator's opacity (the lifted layer keeps the low etage's extinction); (c) no lift (both thresholds 0): every layer stays
low, and the elements of a small-scale sky are up to 7 times larger than drawn (the tile cannot shrink below 6.3 km).
Before the march budget the two overcast sheets aloft cost 0.67-0.82 ms (As) and 2.1-2.4 ms (Cs) in total, with the
same mean transmittance (within 0.01).

**Variability**, mean-preserving: every band's cover c becomes c (1 + (1 - c) M), with M a zero-mean field in [-1, 1]
independent of c, so the mean cover stays the sky's (the shadow map's mean and the overcast veil do not move) and no
cover leaves [0, 1]. M is a patch field plus a growth and decay field:

- the patch field: a few plane waves repeating every creator's tile, so the patches are a tile across whether the map
  holds one tile or 16 (cloud variance), mean exactly 0.5 over the map. The creator's band takes it at the amplitude
  `RenderCloudscapePatchBase` 0.3 plus `RenderCloudscapePatchVariance` 0.4 per unit of cloud variance; the generated
  bands, which have no pattern of their own, at all the amplitude the growth leaves (1 - `RenderCloudscapeGrowth`);
- the growth and decay field: the base noise's Worley FBM moving through its third axis, so that a cell lives
  `RenderCloudscapeLifetime`, 1200 s; amplitude `RenderCloudscapeGrowth` 0.3; its mean over the map is the slice's,
  computed from the noise file.

The growth runs on the cloud scroll's clock: a paused scroll, or a sky whose clouds stand still, freezes it. The bands
drift with the sky's scroll (cloudsF.glsl: the pattern moves east with the scroll's x and north with its y), faster
aloft: (1 + h / `RenderCloudscapeShearHeight`) / (1 + h0 / it), with h0 the creator's band's lower part. The whole cloud
field drifts with the wind, as clouds do; the flat layer scrolls only its main tap and keeps its detail tap and its
cloud-variance taps still, so its clouds morph as they move. The volumetric clouds therefore keep the flat layer's large
shapes (its main tap) where the flat layer has them, while their finer detail and their variance pattern ride with the
clouds; reflections, which show the flat layer until L7-4, drift apart from the sky in that detail. The weather map
resolves the detail tap at 32 texels a repeat (a coarse mip of the cloud texture, about 25 m texels); the finer detail
is the 3D noise's.

Cirrus fibres read the noises in the wind's frame (along the wind compressed by the stretch, across it unchanged); the
offset is computed on the CPU in double from the band's unwrapped origin and wrapped in 1680 km along each of the
noises' own axes, so the wrap never shows (`LLCloudscape::noise_offset`, tested). When the wind's direction changes (a
day cycle between skies of different scroll directions, or editing the scroll), the fibres turn with it about the grid's
origin, so far from it they slide; a world-anchored stretched pattern cannot turn without that.

The light on the clouds is the EEP filter on them (decision D5 (a)): the flat layer's own terms, read from the sky's
uniforms (cloudsV.glsl), with softenLightF.glsl's scale of every sky pixel (sky_hdr_scale, and 1.1 in classic mode, so a
legacy sky's volumetric clouds are as bright as its flat ones and its dome). The sun at the cloud layer (AT1's
transmittance) is tinted by the cloud colour and fades with the clouds' own sun/moon handover (L3); the ambient is lifted
by the cover; the haze in front of a cloud is cloudsV.glsl's haze below the clouds at the cloud's own distance (the haze
ends at max_y). A band's sunlight and sky light pass through the bands above it (one tap where the light ray crosses
each one's middle height, times its path through it; a sun lower than about 2.9 degrees is followed as if at that
elevation). `RenderVolumetricCloudsSunGain` (4.5) and `RenderVolumetricCloudsSkyGain` (0.9) are the Aurora Viewer's
gains, fitted side by side with the flat layer. `LLCloudscape::cloud_light()` is the C++ twin of the shader's light
inputs.

The structural constants are debug settings, all on the devel Sky tab (Preferences > Graphics > Sky, section
"Volumetric clouds"), which is left out of every release integration.

### The passes (`llvolumetricclouds.{h,cpp}`, `app_settings/shaders/class1/deferred/cloudsVol*.glsl`)

| Pass | Size | Program | When |
|---|---|---|---|
| noise upload | 128^3 + 32^3 + 128^2 RGBA8, counted in the texture memory | - | once: the file read and checked on the General worker thread, then 8 slices of the base noise a frame (16 frames); the flat layer draws meanwhile |
| weather | 512^2 (one tile); 2048^2 with cloud variance (16 tiles: the flat layer's disturbance repeats every 16) | `cloudsVolWeatherF.glsl` | drawn whole when its side or period changes (the first map, cloud variance turned on or off: one frame, 0.45 ms at 2048^2); then a change of what it is made of (sliders, textures as they load, the day cycle's blend) or a 2 s step of the growth clock redraws it in 128-row strips, one a frame |
| trace | W/4 x H/4, RGBA16F + R16F | `cloudsVolTraceF.glsl` + `cloudsVolFuncsF.glsl` | every frame: a quarter of the half-resolution pixels near the sky (the deferred depth at the far plane), on a checkerboard |
| reconstruction | W/2 x H/2 RGBA16F, ping-pong | `cloudsVolReconF.glsl` | every frame: reprojected for the rotation and the horizontal move less the clouds' own drift with the wind (the creator's band's), clamped to the fresh 3x3 neighbourhood; dropped on a resize, a camera jump or a jump of the drift over 1 km, or a frame without the clouds |
| composite | full screen | `cloudsVolCompositeV/F.glsl` | after the lighting and the local lights, before the transparent and water passes: `dst * T + L` on the sky's pixels (the screen triangle at the far plane, LEQUAL against the deferred depth), the alpha (glow) untouched; every GL state, the blend factors included, put back |
| still | none: drawn onto the lit image | the trace with `CLOUDS_VOL_STILL`, the composite's vertex shader | a photo (`gSnapshot`: rawSnapshot and its tiles, simpleSnapshot, the 360 capture): every sky pixel (the composite's depth test), twice the steps, 4 sub-samples keyed on the pixel's place in the whole photo, blended as the composite is, in strips of about a megapixel with a flush between them (one draw of a 7680^2 photo would take 0.4-0.8 s on the RTX 5070 Ti, and longer on slower cards, near the driver's watchdog) |
| cloud shadow map | 512^2 RG8 (the L2 map) | `cloudShadowVolF.glsl` | every frame while the clouds draw (`LLCloudShadowMap::update`): 16 steps through the creator's band and 6 through each other along the light (a sun under about 2.9 degrees is followed as if at that elevation, so a low sun keeps a closed deck's shadow), then L2b's optics (beam, diffuse) |

Steps: the creator's band 64 low (up to 96 on a low band's grazing paths: no step longer than 0.3 km on paths up to 28.8
km), 32 in the middle etage, 16 in the high one; the altocumulus band 32, cirrus 16; light taps 5, 3 and 2. The sky's
pool skips the flat layer in the main view and the photos while the clouds draw (`LLDrawPoolWLSky::renderDeferred`);
cube snapshots (reflection probes, mirrors) keep the flat layer (the cloud panorama for reflections is L7-4). The clouds
draw under the flat layer's own conditions (the Sky and Clouds render types, the WindLight shaders, a cloud texture, no
HDRI sky). Everything runs on the main (GL) thread: `LLVolumetricClouds::update()` runs once a frame outside any pass
(before the mirrors' hero probes when they draw, else after the environment's update) and makes the main view's targets
there, so a target that cannot be allocated leaves the flat layer drawing; `LLVolumetricClouds::release()` frees
everything in `LLPipeline::releaseGLBuffers`.

### The toggle

**Preferences > Graphics > Volumetric clouds** (`RenderVolumetricClouds`, the one Preferences entry decided for L7). The
hardware recommendation enables it at **High** (GPU class 5: a benchmarked memory bandwidth above 384 GB/s) and disables it
below. The setting's own default is off. Selecting Low, Medium, or High in Preferences or Performance keeps the current
cloud renderer, so a sky such as the built-in Midday no longer changes cloud models on a tier switch. The checkbox still
controls the cloud renderer directly. Turning it off frees the noise (8.5 MB of VRAM) and the targets.

The plan's line (D3 (a): on from RTX 3060-class up) cannot be drawn by the GPU classes: a class is a benchmarked memory
bandwidth (`LLFeatureManager::loadGPUClass`: class 4, the High level, is 192-384 GB/s), and the RTX 3060 (360 GB/s) shares
class 4 with cards of a half or less of its shader throughput: GTX 1660 Super and Ti (336 and 288 GB/s, about 5 TFLOPS),
RTX 2060, GTX 1070, RX 580. On those the clouds are estimated at 2.4-4.9 ms in the worst case at 1080p (by texture rate,
x4.4, and by FP32, x9), which would take a GPU-bound 25 fps to 22.3-23.6 fps. So the High level leaves them off until the owner decides:
the options are a lighter quality for the High level (the owner's "optimisation methods for mid-range GPUs", #429), a
check of the clouds' measured GPU time over the first frames, or the line at High Ultra as now.

### Cost (measured)

`scripts/light/check_volumetric_clouds.py` builds the six programs from these shader files the way the viewer does,
checks that every uniform the C++ side sets is live, runs them on the real noise file with llcloudscape.h's bands for
nine slider presets, checks the output (finite, transmittance in [0, 1], the shadow map adds no light, cloud where the
sliders put cloud, a low sun's shadow under a closed deck), that the GPU weather map keeps every band's mean cover and
that the shader's light inputs match LLCloudscape::cloud_light (24 skies, half of them in classic mode, worst relative
error 8e-7), and times each draw alone. RTX 5070 Ti, driver 621.14, every pixel sky (the worst case: in-world, buildings
and terrain cover part of it), medians of 20 frames, the two views (12 and 60 degrees up), Detail D 1 (the EEP default):

| Preset (coverage, scale, variance, D) | Bands | 1080p total (trace + reconstruction + composite), ms | 1440p total, ms |
|---|---|---|---|
| fair, the default sky (0.27, 0.42, 0, 1) | Cu | 0.22-0.25 | 0.30-0.34 |
| broken (0.38, ...) | Cu | 0.27-0.28 | 0.37-0.38 |
| stratocumulus (0.45, ...) | Sc | 0.21-0.23 | 0.27-0.34 |
| overcast (0.62, ...) | St | 0.18-0.24 | 0.27-0.35 |
| congestus (0.30, 0.42, 0, 1.6) | Cu con | 0.25-0.32 | 0.34-0.41 |
| cumulonimbus (0.30, 0.42, 0, 2.6) | Cb | 0.39-0.42 | 0.50-0.60 |
| a layer rising into the middle etage (0.30, 0.15, 0, 1) | Ac | 0.25-0.27 | 0.31-0.39 |
| altocumulus layer (0.30, 0.12, 0, 1) | Ac | 0.18-0.21 | 0.24-0.30 |
| three bands (0.27, 0.42, 0.6, 1, altocumulus setting 0.6) | Cu, Ac, Ci | 0.36-0.46 | 0.49-0.64 |

The reconstruction is 0.011-0.016 ms and the composite 0.017 ms (1080p) and 0.03 ms (1440p) of these. The cloud shadow
map is 0.028-0.055 ms. The weather map whole is 0.013-0.019 ms at 512^2 and 0.45 ms at 2048^2; its strips are a quarter
(512^2) or a sixteenth (2048^2) of that. A photo's still trace is 12.5-29.4 ms a 1080p frame, once. Against the
previous version on the same sliders (interleaved runs): one band +0.00-0.03 ms, three bands +0.03-0.05 ms at 1080p
and +0.07-0.09 ms at 1440p (the light through the bands above). The plan's L7-2 figure (0.30 ms at 1080p, 0.35 ms at
1440p) holds at 1080p for the single bands but the towers (congestus seen low 0.32, cumulonimbus 0.39-0.42) and three
bands; at 1440p it holds for fair, stratocumulus, overcast and the altocumulus layer.

Against the fps floor (about 60 fps standing in a crowded room, never below 25 fps moving), the worst case with the
shadow map and a weather strip is 0.54 ms at 1080p and 0.73 ms at 1440p on this GPU: 60 becomes 58.1 and 57.5 fps, 25
becomes 24.7 and 24.6 fps, when the frame is GPU-bound (the crowded room is CPU-bound, 31.9 ms on the main thread, and
the passes overlap it; the main thread pays the uniforms and binds, a few microseconds). The default sky costs 0.25-0.28
ms at 1080p with the shadow map. The cost does not grow with movement. A frame that draws a whole 2048^2 weather map
(cloud variance turned on or off) adds 0.45 ms once.

### Not done, not verified

- No in-world check: the look against the flat layer and the Aurora Viewer's demo skies (the noise file differs from
  the light study bench's generated noise: expect the re-tuning pass L7-0 foresaw), the photo-against-screen check, a
  region crossing and a 360 degree turn for shimmer (the plan's L7-2 gates). Not run in the viewer: the strip-drawn
  photo, the drift in the reprojection, the Sky tab's new section.
- Owner decisions pending: what a damaged noise file means once the clouds draw; the etage rule (the table above); the
  lever for the altocumulus band; physical cloud altitudes against heights above the observer; the GPU-class line.
- The per-sky calibration of the clouds' mean luminance to the flat layer's (+-10 %, D5 (a)): the gains are the Aurora
  Viewer's, as debug settings.
- AT2's physical base for the clouds' light (the sky light at the cloud layer) and far haze (AT2-4) do not exist yet:
  the clouds read the flat layer's light and haze terms, which are the creator's look the filter keeps.
- Reflections (probes, water, mirrors) show the flat layer until L7-4's cloud panorama.
- The slider-to-genus mapping is a design from WMO criteria with no C++ oracle; its thresholds are debug settings. The
  light through the bands above is one tap a band, not a march.
- The other GPU classes' costs are estimates (texture rate and FP32), not timed.
