# Projector shadow pool

Spec: `reports/projector-pool-2026-11-13/design-final.md` (sections 3 to 9). This page says what is built, stage by stage. The
pool is the Stochastic method's (`RenderShadowMethod` 2) projector shadow when `RenderShadowDetail` is 2 (sun and projectors).
Legacy (method 0) keeps its two re-rendered slots, byte for byte (`scripts/shadows/check_legacy_shadows.py`,
`scripts/shadows/check_legacy_glsl.py`).

## What it replaces

Today's two slots (`mSpotShadow[0..1]`, window-sized maps) are handed from lamp to lamp by a view-dependent priority and are
re-rendered every frame, so a lamp's shadow can vanish when the camera turns. The pool gives every projector within the shadows'
reach (`min(RenderFarClip, 128 m)`, measured from the camera's position, with a hysteresis band) a tile of its own in a shared
depth atlas. The tile's size and view come from the light alone; the caster LOD (D6, P3) and the avatars' visibility (D7/D8, P4) still follow the camera until those stages.

## Stage P2: tiles replace the slots

Code: `llprojectorshadows.h/.cpp` (GL), `llprojectorshadowpool.h` (the rules, stage P1).

- **Atlases.** Two `GL_DEPTH_COMPONENT32F` textures made through `LLStochasticEngine::newTexture2D` (NEAREST, no mips, counted in the
  texture memory): S holds the tiles as drawn, L is S plus whatever is drawn over it and is the only texture a lighting program
  reads. 4096 x 4096 (2 x 64 MiB) by default, 2048 x 2048 below 3 GB of video memory. An allocation that fails tries the
  smaller size; if that fails, the projectors have no tile (lit without a shadow), the failure is logged and the allocation is
  tried again with a back-off (5 s, doubling up to 60 s). The method never changes.
- **Tile size** from the light only (`baseSize`: radius and field of view), shrunk together by one power of two when the atlas
  fills (`LevelController`: shrink above 60 % demand, grow back after 3 s at or below 45 %). Placement by a buddy allocator;
  arrivals and departures are incremental, a level step or a size change is one repack (copies for same-size tiles, a nearest
  blit for resized ones, then L to S in full).
- **What a frame does** (`LLProjectorShadows::update`, from `generateSunShadow`, not for probes): the light set, the level and
  placement, then the tile renders the budget allows. P2 has no change tracking, so every tile is a full render (the fixed
  objects and the avatars into S, then S to L for the tile), ordered by `selectStatic` (never drawn first, then the longest
  waiting, then the nearest). **Budget:** twice the measured CPU cost of one tile render per frame, which is what today's two slot
  renders cost, so the render cost is at most today's for a visible lamp (a lamp in range but out of view still costs its tiles, which Legacy does not draw). A static scene therefore still re-renders about two
  tiles a frame in P2; P3 stops that. A frame also stops rendering once the measured time reaches the budget (the first pick always runs).
- **Lookup.** `setupSpotLight` calls `LLProjectorShadows::setupLight` instead of ranking lights: `proj_shadow_idx` (-1 until the tile
  is first drawn), `proj_shadow_matrix` (the matrix of the pose the tile was drawn with, so a lamp that moved keeps a coherent
  shadow until its tile is redrawn), `proj_shadow_tile` (x, y, N, 1/A). `spotLightF.glsl` looks the
  shadow up itself (permutation `PROJECTOR_SHADOW_POOL`), only inside the projector's frustum and on surfaces that face it, after
  the attenuation test. `sampleProjectorShadow` is the filtered lookup of 261253bd43 on the tile, with an exact `CLAMP_TO_EDGE`
  emulation at the tile's edge (no guard texels). The sun pass writes 1 in its projector channels; the slots stay empty.
- **Filter radius in metres (P2b).** The largest radius of the blocker search and of the filter is a length at the receiver,
  `ST_SPOT_RADIUS_MAX_M` = 4 cm (about the 10 texels of a base-size tile at its reference distance), turned into texels of this
  tile per axis in the shader: `clamp(0.04 * k N / w, 1, 12)` (`k N / w` = texels per metre at axial distance `w`). The lower bound
  is one texel (an edge is never narrower than the tile resolves, so a shrunk tile does not alias); the upper bound is 12 texels
  because the cost is fixed (8 search and 32 filter gathers): over a wider disc the same taps are further apart (a 12-texel disc has
  452 texels, and 32 bilinear 2 x 2 taps read 128 of them, 28 % against 41 % at the old 10 texels) and the penumbra turns grainy
  before it turns wrong. A level step no longer changes the radius in metres; it only changes how many texels the 4 cm is.
- **Emitter size from the prim (P2b).** The penumbra's light size is the projector prim's own face, `scale.x` and `scale.y` of the
  pose the tile was drawn with, as the uniform `proj_shadow_light_size` (metres along the map's u and v, each clamped to
  2 cm .. 30 cm; the old constant was 15 cm, at most the near plane's width). A bulb prim gives a sharper shadow than before, a wall washer
  a softer one up to the 4 cm radius cap. Nothing depends on the camera. The sun keeps `shadow_light_size`.
- **Caster dither.** The hashed dither of semi-transparent casters uses `gl_FragCoord.xy - shadow_tile_origin` (zero outside the
  pool), so a tile's pattern does not depend on where the tile sits in the atlas.
- **Lifetime.** Allocated in `allocateShadowBuffer` (the place of the spot maps, not for probe snapshots), released when the method
  or detail leaves Stochastic detail 2, in `destroyGL` and in `cleanup` (while the context lives). Every entry point is on the main
  thread and returns at once off it or while the viewer exits. Nothing is added to `LLRender::shutdown`, `LLImageGL` or
  `LLVertexBuffer::cleanupClass`.

## Gates

| Gate | How | Result |
|---|---|---|
| G1 lookup equivalence | `python scripts/shadows/projector_pool_g1.py` (offline GL, no viewer) | tile in the atlas equals the same code on a standalone map, max abs 0; within 2 ulp of the 261253bd43 source |
| G4 Legacy | `check_legacy_shadows.py` (C++ regions), `check_legacy_glsl.py` (GLSL text) | identical |
| G2, O-4, method switches, window toggle, quit | viewer, see the stage's PR | needs a world with projectors for the lookup parts |

### Known limits of P2

- **Avatar shadows refresh less often than Legacy.** Legacy re-rendered its two slots, avatars included, every frame. P2 renders about two tiles a frame, so with N lamps a tile (and the avatar's shadow in it) refreshes about every N/2 frames (roughly 250 ms in a 30-lamp room). A walking avatar's shadow under the nearest lamps updates less often until P4 splits avatars out.
- The caster LOD (D6, P3) and the avatars' visibility (D7/D8, P4) still follow the camera.
- **Casters outside the main view: draw info is not camera-gated; loading still is.** A tile renders through the same `LLPipeline::renderShadow` as the sun cascades and Legacy's spot slots, with a camera built the same way (`generateSunShadow`, `pipeline.cpp:13073`-`13089`; `renderTile`). `renderShadow` runs `updateCull` and `stateSort`, and `stateSort` ends in `postSort`, which rebuilds the dirty groups it culled (`pipeline.cpp:3921`, `3954`). Beyond that, no group's draw info depends on the main view: a spatial group queues itself for a rebuild when it is created or dirtied (`llspatialpartition.cpp:315`, `437`, `480`, `606`; `LLPipeline::markRebuild`, `pipeline.cpp:3403`), and `rebuildPriorityGroups` (`pipeline.cpp:3120`, run every frame from `postSort`) builds every queued group whether or not any camera sees it. Occlusion is off during a shadow render (`sUseOcclusion = 0`). So a loaded caster the camera never saw has its draw info and appears in the tile. Three inputs still follow the main camera and are P3 work: (1) object loading: an object outside the near radius (`SceneLoadMinRadius`, 32 m) that never entered the main frustum or the rear sphere gets no drawable (`llvocache.cpp:1037`-`1060`; once loaded it stays, since eviction is disabled); (2) mesh and sculpt assets: a caster whose asset was never fetched is skipped (`llvovolume.cpp:6066`); (3) alpha-masked casters: their texture's virtual size comes from the main camera, so a texture never seen may stay low-resolution or unloaded (`llvovolume.cpp:889`-`905`, `6108`). The caster LOD (above) is the fourth.
- A projector whose geometry the pool cannot use (field of view outside (0, 3.1 rad), zero or non-finite scale or radius) is not given a tile.
- A failed atlas allocation is logged once and retried with a back-off (5 s up to 60 s).
- The light set has no count cap: `RenderLocalLightCount` only switches `calcNearbyLights` on (>= 1) and the six-light cap applies
  to the non-deferred path only, so the pool considers every eligible spotlight within reach. When they do not fit even at the
  smallest level, the lights that sort last (smaller tiles first, then key) have no tile and are lit without a shadow.
