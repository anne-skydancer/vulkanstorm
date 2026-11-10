<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Cloud shadows from the flat cloud layer (stage L2 of the light study)

Design: `doc/light/light-model-study.md` §3 (draft PR #429). The owner's decisions on #429 that this
stage implements: cloud shadows at physical strength 1.0 ("what the sky draws, the ground gets") through
the `RenderCloudShadowStrength` debug setting; dome parallax on the flat layer with H = 15 km; a
default penumbra height of 1.5 km and a map of 4 km, tuned in-world; the moon uses the same map; reflection
probes use the map's mean, never a passing shadow. The principle: light behaves like real light, so the
shadows depend only on the world and the sky, never on the camera.

## What changes

- **The map** (`llcloudshadowmap.{h,cpp}`, `deferred/cloudShadowMapF.glsl`). Once a frame, a 512 x 512
  R8 target (with mips) holds T, the share of the sun's (or the moon's) light the flat layer lets
  through, for a 4096 m square of the world: 8 m texels. The pass runs the sky's own cloud taps
  (`cloudsF.glsl`'s `alpha1`, line for line) and writes `1 - alpha1`. It runs in the sky shader group,
  so it reads the sky's own cloud uniforms (scroll, density, scale, variance, `cloud_shadow`); the noise
  textures and the blend are bound by the same function as the sky's (`LLDrawPoolWLSky::bindCloudNoise`,
  taken out of `renderSkyCloudsDeferred`, unchanged).
- **The layout** (`llcloudshadow.h`, renderer-free). The map is anchored in the world: its origin is a
  multiple of extent / 8 (512 m) in global coordinates, so a static scene reads the same texels while
  the camera walks and turns, as S1's cascades do. It follows the agent's camera in global coordinates
  (continuous across regions), not the view being drawn.
- **The dome parallax.** The sky dome is the cap of a sphere of radius 15 km whose centre is 0.96 R
  below the camera, with planar texture coordinates from the horizontal position of the dome point. The
  cloud shader (`cloudsV.glsl`) now adds `(camera.x, -camera.y) / (2 R cloud_scale)` (`cloud_parallax`,
  the camera's global position in metres). The clouds then stay where they are when the camera moves
  horizontally (a visible drift when flying: 1 km moves the sky by 0.08 of a repeat; walking 100 m,
  0.008) and the cloud seen in a direction is a function of the point and the direction only. The
  ground's lookup is `texcoord(p, L) = layer_texcoord(p + dome_offset(L))`, so at the camera it is exactly
  the texture coordinate the sky draws over the sun. Moving vertically changes nothing. With the
  strength at 0 the parallax is 0 and the sky is drawn as before.
- **The lookup.** `calcAtmosphericVars` (`atmosphericsFuncs.glsl`), which every shader that lights with
  the sun calls (softenLight, alpha, pbralpha, material, water, pbrmetallicroughness / glTF, haze),
  multiplies `sunlit` by `1 - strength (1 - T)`. T is one trilinear tap of the map at the eye position's
  world (east, north); the LOD of the tap is the penumbra (`H_pen x 0.0093 x sun_scale`, 14 m at 1.5 km:
  0.8 of a mip). The map fades to its mean over the outer 6 % of its width. `sunlit` is the sun's or the
  moon's light by `sun_up_factor`, and the map is built for the same light direction. The haze (`additive`)
  keeps `cloud_shadow` as it was.
- **Combination.** The result multiplies the sun shadow map and PCSS (`scol`): independent occluders of one
  source. The shadow passes are untouched.
- **Probes** (`gCubeSnapshot`) read the map's mean (the top mip), so a probe refreshed once in a while
  never bakes a passing shadow.
- **Degradation.** No pass, and no tap (the strength uniform is 0 and the lookup returns at once), when:
  `RenderCloudShadowStrength` is 0; clouds are hidden (Rendering Types); an HDRI replaces the sky; the
  sky has no cloud texture or `cloud_scale` < 0.001 (the same conditions under which the sky draws no
  clouds); the shader or the target failed to load. Only a 2D R8 target, mips and a fragment pass are
  needed (GL 3.0), so the GL3 tier keeps the shadows. Shaders that do not call `calcAtmosphericVars` for the
  sun never see the sampler.
- **Settings:** `RenderCloudShadowStrength` (1.0), `RenderCloudShadowPenumbraHeight` (1500 m),
  `RenderCloudShadowExtent` (4096 m). On devel they are tuned live in the **Sky** tab (Preferences →
  Graphics → Sky), a tuning panel that is never integrated into vkstorm-release. The values settled there
  become the defaults. In release builds they remain debug settings with no Preferences entry.

## Tests

- `indra/newview/tests/llcloudshadow_test.cpp`, in CI beside the cascade test (renderer-free):
  - the sky's texture coordinate rebuilt from the dome's geometry (vertex on the sphere, the dome-to-world
    axis permutation, `llvowlsky.cpp`'s planar coordinates, `cloudsV.glsl`'s flip and scale, the parallax)
    equals the header's closed form, and the ground lookup at the camera in the light's direction
    equals it, for 4000 random cameras (within 300 km), light directions and cloud scales;
  - the lookup does not depend on the camera; the sky shifts by the parallax and nothing else;
  - the scroll moves the sky and the ground the same way, 2 R cloud_scale metres per repeat, east for a
    positive `x` and south for a positive `y`;
  - the map's origin is on the step lattice, the lattices of two cameras coincide after re-centring, the
    same world point has the same texel centre and lookup from either map, and the camera is always in
    the middle of the map; a small camera move keeps the origin;
  - the altitude fade is the dome point's height, not the camera's.
- `scripts/light/check_cloud_shadow.py --check` (a GPU, not in CI): the viewer's own `cloudsV/F.glsl` and
  `cloudShadowMapF.glsl` run for 60 random worlds (camera within 300 km, any light, cloud scale, scroll,
  variance, `cloud_shadow`, blend, maximum height); the sky's `alpha1` and the map's `1 - T` agree to
  1e-5 (worst 9.8e-6; 41 of the 60 cases partly covered). This ties the C++ geometry, the altitude fade and the
  shader to what the sky draws.

## Cost, measured

`scripts/light/check_cloud_shadow.py --bench`, RTX 5070 Ti, driver 621.14, GL 4.6, `GL_TIME_ELAPSED` medians
of 40 frames, the viewer's `atmosphericsFuncs.glsl` unmodified. The offline pass runs `calcAtmosphericVarsLinear` on every pixel once; the viewer calls it in the
soften pass and in each alpha, material and water draw, so the lookup counts there are higher, and the
in-world Frame Profile is the owner's check.

| Item (3 runs) | 1080p | 1440p |
|---|---|---|
| Map pass 512 x 512 R8 and its mips, with clouds | 0.0345-0.0346 ms | - |
| One full-screen lookup pass, strength 1 against strength 0 | 0.0005-0.0007 ms | 0.0022-0.0029 ms |
| Strength 0 (or no clouds) against the shader before the stage | +0.0002 to -0.0001 ms | +0.0004 to -0.0003 ms |
| Total, map + one lookup pass, with clouds | 0.035-0.036 ms | 0.037-0.038 ms |
| With no clouds | no map pass; lookups return at once (below the timer's noise) | |

Gate: <= 0.05 ms at 1080p for the map and the lookups, and 0 with no clouds. CPU: one pass of draw
calls and a handful of uniforms per frame, plus a 4 x 4 matrix per shader bind (the inverse of the view is
kept while the view stays the same).

## What stays the same

With the strength at 0, or with no clouds drawn, the sunlight is multiplied by exactly 1 and the sky gets
no parallax: the output is today's. With the strength at 1 (the default) and clouds, skies with
`cloud_shadow` above the default now darken the ground in moving patches: on the study's stand-in cloud
texture the mean direct light is 0.85 at 0.27, 0.34 at 0.38 and about 0 from 0.62. The real cloud texture
is judged in-world.

## What the owner checks in-world (study §5, L2)

- Under a broken sky the sun goes behind a cloud and the ground darkens.
- Shadows drift with the clouds the way the sky scrolls (the same direction and speed).
- Walking and turning change nothing: no crawl, no swimming.
- Flying up or sideways: the sky's clouds drift (the parallax), the shadows do not slide against them.
- Overcast presets look overcast.
- Tuning: `RenderCloudShadowStrength`, `RenderCloudShadowPenumbraHeight`, `RenderCloudShadowExtent`, live in the devel-only Sky tab (Preferences → Graphics → Sky).

## Build and launch checks

RelWithDebInfo + LTO through Autobuild, staged (`build-vc180-64/newview/RelWithDebInfo/vulkanstorm-bin.exe`), no installer. A 60 s minimised launch with a scratch profile and no login loads the basic, water, effects and interface shaders without error. The login screen does not load the deferred shaders or draw clouds, so the map pass and the lookups were not run in the viewer: the shaders are compiled and compared with the sky's by `check_cloud_shadow.py`, and the in-world frame cost is the owner's check.
