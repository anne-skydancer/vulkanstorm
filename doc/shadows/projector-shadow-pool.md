# Projector shadow pool

Spec: `reports/projector-pool-2026-11-13/design-final.md` (sections 3 to 9). This page says what is built, stage by stage. The
pool is the Stochastic method's (`RenderShadowMethod` 2) projector shadow when `RenderShadowDetail` is 2 (sun and projectors).
Legacy (method 0) keeps its two re-rendered slots, byte for byte (`scripts/shadows/check_legacy_shadows.py`,
`scripts/shadows/check_legacy_glsl.py`).

## What it replaces

Today's two slots (`mSpotShadow[0..1]`, window-sized maps) are handed from lamp to lamp by a view-dependent priority and are
re-rendered every frame, so a lamp's shadow can vanish when the camera turns. The pool gives every projector within the shadows'
reach (`min(RenderFarClip, 128 m)`, measured from the camera's position, with a hysteresis band) a tile of its own in a shared
depth atlas. The tile's size and view come from the light alone; the caster LOD (D6, P3) and the avatars' visibility (D7/D8, P4) still follow the camera until those stages (P3 closes the caster LOD and the redraw on camera moves: see below).

## Stage P2: tiles replace the slots

Code: `llprojectorshadows.h/.cpp` (GL), `llprojectorshadowpool.h` (the rules, stage P1).

- **Atlases.** Two `GL_DEPTH_COMPONENT32F` textures made through `LLStochasticEngine::newTexture2D` (NEAREST, no mips, counted in the
  texture memory): S holds the tiles as drawn, L is S plus whatever is drawn over it and is the only texture a lighting program
  reads. 4096 x 4096 (2 x 64 MiB) by default, 2048 x 2048 below 3 GB of video memory, and 8192 x 8192 (2 x 256 MiB) from 11 GB
  (11264 MB: a "12 GB" card reports a little under 12288) of **dedicated** video memory (the driver's own report,
  `NVX_gpu_memory_info` or `WGL_AMD_gpu_association`; the DXGI budget the viewer falls back to is system RAM on shared-memory
  graphics, so Intel and AMD integrated graphics keep 4096; an AMD APU whose driver reports through neither extension is a known limit),
  never wider than `GL_MAX_TEXTURE_SIZE`. The 8192 tier is there because the owner's in-world report (several lamps, every tile
  shrinking) showed the 4096 atlas (four 2048 tiles, two at the 0.60 shrink threshold) too small; the 8192 one holds sixteen
  (nine). Its extra 384 MiB counts in the viewer's texture budget: on a 12 GB card that budget is about 4.9 GB
  (`RenderTextureVRAMDivisor` 2, then the 0.8 factor), so the tier takes 7.8 % of it (5.2 % on a 24 GB card), not 3 %; watch the
  texture discard bias on 12 GB cards in-world. The chosen tier, the memory it was chosen for and `GL_MAX_TEXTURE_SIZE` are in the one
  log line of a successful allocation. An allocation that fails tries the
  smaller size; if that fails, the projectors have no tile (lit without a shadow), the failure is logged and the allocation is
  tried again with a back-off (5 s, doubling up to 60 s). The method never changes.
- **Tile size** from the light only (`baseSize`: radius and field of view), shrunk together by one power of two when the atlas
  fills (`LevelController`: shrink above 60 % demand, grow back after 3 s at or below 45 %). Placement by a buddy allocator;
  arrivals and departures are incremental, a level step or a size change is one repack (copies for same-size tiles, a nearest
  blit for resized ones, then L to S in full).
- **What a frame does** (`LLProjectorShadows::update`, from `generateSunShadow`, not for probes): the light set, the level and
  placement, then the tile renders the budget allows. P2 has no change tracking, so every tile is a full render (the fixed
  objects and the avatars into S, then S to L for the tile), ordered by `selectStatic`: never drawn first, nearest first; then
  **oldest first by age in frames, the distance only breaking a tie**, so the order is a round robin by staleness.
  - **Budget (P2c).** A fixed `kStaticCapMs` = **0.35 ms** of **CPU** time per frame for the tile renders. The frame-time basis is
    an assumed 8 ms frame (125 fps), not a measured one: on it the cap is 4.4 % and the whole light and shadow budget (+5 %) is
    about 0.4 ms. The cap fits four measured tiles (0.07 ms each, owner session 2026-11-14) with a margin, so a room of four
    lamps redraws every tile every frame. It leaves 0.05 ms of the 0.4 ms; `kDynamicCapMs` (0.30 ms) does not fit in that, so P4
    must re-divide the cap between the static and the dynamic tiles (nothing is drawn dynamically before P4). The measured cost of a
    tile (`last_cpu_ms`) only predicts how many fit; it does not set the budget. A frame also stops rendering once the measured
    time reaches the budget (the first pick always runs, so a tile dearer than the budget still progresses). Until P3, a static
    scene still re-renders every tile that fits.
  - **The Legacy-cost guarantee is dropped.** P2 and P2b promised that the pool costs at most what Legacy's two slot renders
    cost. With a fixed cap and no change tracking that no longer holds (four lamps cost about 0.28 ms of CPU a frame against
    Legacy's two slots); it returns when P3 skips the tiles that did not change.
  - **Cold start.** A record that was never drawn is predicted at `coldItemMs` (the running average, at most a quarter of the cap),
    not at the average's 0.5 ms start value, which is above the cap and would draw one tile a frame after login or a teleport.
  - *Why P2b starved tiles:* the budget was 2 x the running average of one render (0.14 ms at 0.07 ms), the two nearest tiles filled
    it every frame, and a far tile only got a turn after the 250 ms "stale" deadline (up to 24 frames at 58 fps).
    `ProjectorShadows.stats` `pool.max_stale_frames` is the age of the oldest drawn tile.
- **Lookup.** `setupSpotLight` calls `LLProjectorShadows::setupLight` instead of ranking lights: `proj_shadow_idx` (-1 until the tile
  is first drawn), `proj_shadow_matrix` (the matrix of the pose the tile was drawn with, so a lamp that moved keeps a coherent
  shadow until its tile is redrawn), `proj_shadow_tile` (x, y, N, 1/A). `spotLightF.glsl` looks the
  shadow up itself (permutation `PROJECTOR_SHADOW_POOL`), only inside the projector's frustum and on surfaces that face it, after
  the attenuation test. `sampleProjectorShadow` is the filtered lookup of 261253bd43 on the tile, with an exact `CLAMP_TO_EDGE`
  emulation at the tile's edge (no guard texels). The sun pass writes 1 in its projector channels; the slots stay empty.
- **Ambiance is shadowed (P2c).** The projector's ambiance (the fill light, `getProjectedLightAmbiance`) used to be added without
  the shadow ("unshadowed for consistency between forward and deferred?" in the stock code), so it leaked through walls the beam
  stopped at. Under `PROJECTOR_SHADOW_POOL` only, both material branches (PBR and legacy) multiply it by the pool's shadow; the
  specular and the environment reflection already did. Legacy (no pool define) is textually unchanged. `multiSpotLightF` is the
  same file with `MULTI_SPOTLIGHT`, so it takes the same path. By design a surface facing away from the lamp gets the ambiance (N.L is
  clamped above zero: "lit from any side"), so the facing skip of the lookup no longer skips a non-PBR pixel while `proj_ambiance > 0`
  (as it already kept env-mapped pixels). A PBR pixel keeps the skip: it weights the ambiance by N.L clamped to 1e-6 on a back
  face, so the lookup would be wasted. Cost: back-facing non-PBR pixels inside the cone look the shadow up too, for lamps with an
  ambiance. The strength of the shadow on the ambiance (full, as the beam) is the owner's to decide separately; the stock comment
  "unshadowed for consistency between forward and deferred?" describes the non-pool path only.
  **Look change (Stochastic, pool only):** the ambiance is now shadowed like the beam. A lamp inside a separate shade prim no
  longer fills the room past an opaque shade, and on legacy materials the far side of a closed caster loses the fill it used to
  get from the lamp's ambiance.
- **Filter radius in metres (P2b).** The largest radius of the blocker search and of the filter is a length at the receiver,
  `ST_SPOT_RADIUS_MAX_M` = 4 cm (about the 10 texels of a 2048 tile at 4 m, with a 90 degree field of view), turned into texels of this
  tile per axis in the shader: `clamp(0.04 * k N / w, 1, 12)` (`k N / w` = texels per metre at axial distance `w`). The lower bound
  is one texel (an edge is never narrower than the tile resolves, so a shrunk tile does not alias); the upper bound is 12 texels
  because the cost is fixed (8 search and 32 filter gathers): over a wider disc the same taps are further apart (a 12-texel disc has
  452 texels, and 32 bilinear 2 x 2 taps read 128 of them, 28 % against 41 % at the old 10 texels) and the penumbra turns grainy
  before it turns wrong. A level step no longer changes the radius in metres; it only changes how many texels the 4 cm is.
- **Emitter size from the prim (P2b).** The penumbra's light size is the projector prim's own face, `scale.x` and `scale.y` of the
  pose the tile was drawn with, as the uniform `proj_shadow_light_size` (metres along the map's u and v, each clamped to
  2 cm .. 15 cm; the old constant was 15 cm, at most the near plane's width). Only small emitters get sharper (a bulb prim, a face
  under 15 cm); a face of 15 cm or more gives the penumbra it gave in P2, none is softer. Nothing depends on the camera. The sun keeps `shadow_light_size`.
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

- **Avatar shadows refresh less often than Legacy.** Legacy re-rendered its two slots, avatars included, every frame. P2 renders the tiles that fit the 0.35 ms cap (P2c; four at the measured 0.07 ms), oldest first, so with N lamps a tile (and the avatar's shadow in it) refreshes every ceil(N / tiles per frame) frames in a steady state with a fixed light set (every frame for four lamps). Distance no longer buys freshness: with 30 lamps at 7 tiles per frame, the lamp you stand under refreshes every 5 frames, like the farthest one. A walking avatar's shadow under the nearest lamps updates less often until P4 splits avatars out.
- The caster LOD (D6, P3) and the avatars' visibility (D7/D8, P4) still follow the camera.
- **Casters outside the main view: draw info is not camera-gated; loading still is.** A tile renders through the same `LLPipeline::renderShadow` as the sun cascades and Legacy's spot slots, with a camera built the same way (`generateSunShadow`, `pipeline.cpp:13073`-`13089`; `renderTile`). `renderShadow` runs `updateCull` and `stateSort`, and `stateSort` ends in `postSort`, which rebuilds the dirty groups it culled (`pipeline.cpp:3921`, `3954`). Beyond that, no group's draw info depends on the main view: a spatial group queues itself for a rebuild when it is created or dirtied (`llspatialpartition.cpp:315`, `437`, `480`, `606`; `LLPipeline::markRebuild`, `pipeline.cpp:3403`), and `rebuildPriorityGroups` (`pipeline.cpp:3120`, run every frame from `postSort`) builds every queued group whether or not any camera sees it. Occlusion is off during a shadow render (`sUseOcclusion = 0`). So a loaded caster the camera never saw has its draw info and appears in the tile. Three inputs still follow the main camera and are P3 work: (1) object loading: an object outside the near radius (`SceneLoadMinRadius`, 32 m) that never entered the main frustum or the rear sphere gets no drawable (`llvocache.cpp:1037`-`1060`; once loaded it stays, since eviction is disabled); (2) mesh and sculpt assets: a caster whose asset was never fetched is skipped (`llvovolume.cpp:6066`); (3) alpha-masked casters: their texture's virtual size comes from the main camera, so a texture never seen may stay low-resolution or unloaded (`llvovolume.cpp:889`-`905`, `6108`). The caster LOD (above) is the fourth.
- A projector whose geometry the pool cannot use (field of view outside (0, 3.1 rad), zero or non-finite scale or radius) is not given a tile.
- A failed atlas allocation is logged once and retried with a back-off (5 s up to 60 s).
- The light set has no count cap: `RenderLocalLightCount` only switches `calcNearbyLights` on (>= 1) and the six-light cap applies
  to the non-deferred path only, so the pool considers every eligible spotlight within reach. When they do not fit even at the
  smallest level, the lights that sort last (smaller tiles first, then key) have no tile and are lit without a shadow.

## Stage P3: cache and camera-proof

Code: `llshadowdirtyfeed.h/.cpp` (the scene's change hooks and the mover classifier), `llprojectorshadowpool.h` (the GL-free rules),
`llprojectorshadows.cpp` (the tile scheduler), and a few lines in `pipeline.cpp`, `llspatialpartition.cpp`, `llviewerobject.cpp`,
`llvovolume.cpp`. Unit tests: `indra/newview/tests/llprojectorshadowpool_test.cpp` (the `P3` checks). Gates: `scripts/inworld/projector_orbit_check.py`
(O-1, O-2, O-6; O-4 is P2's).

**What changed for the owner.** A tile is drawn again only when something that shows in it changed, not every frame. A static
scene costs no tile render. What a camera does, where it stands and where it looks, no longer reaches a tile: the casters in a
lamp's light are held at full detail, and no hook looks at the camera.

### What makes a tile stale

One cause per render (the first that made it stale); the stats API counts them (`pool.rerender_causes`).

| Cause | When |
|---|---|
| `new` | never drawn: the first fill, a lamp that entered the range, an atlas that was made again. Drawn nearest first |
| `lod_up` | the LOD pin raised an object in the light to full detail (once per object and tile, then it holds) |
| `add_remove` | an object was put into or taken out of the scene inside the light (`LLSpatialPartition::put`/`remove`) |
| `edit` | a fixed object in the light was rebuilt (volume, position, rigged), moved inside its octree group, or its geometry finished a delayed rebuild (a mesh that loaded) |
| `mover` | an object turned into a mover or a physical one, or stopped being physical |
| `lamp` | the lamp's own pose changed: position more than 1 mm, rotation 1e-4, scale 1 mm, angle 1e-4, reach 1 cm |
| `avatar` | an avatar is in the light (drawn into S until P4, so the tile is redrawn every frame), or it left (one more render) |
| `lowres` | a layout change down-sized the tile (a blit): drawn again at its new size |
| `other` | the dirty list overflowed (65536 boxes in one frame), or a hook ran off the main thread: every tile is stale once |

Texture-coordinate and colour rebuilds (texture animation, tints) never make a tile stale, nor do a lamp's colour, intensity or falloff.

### The hooks, and the thread each runs on

All hooks push a box (the extents of what changed, agent space) with a cause into one double-buffered list
(`LLShadowDirtyFeed`). `LLProjectorShadows::update` swaps it before any render of the frame (a change a render itself causes lands
in the next frame), tests the boxes against the tile volumes through the 8 m grid (`ChangeIndex`), and keeps the first cause per
tile. Every hook is a no-op until the pool holds tiles (`setTracking`), and every one runs on the main thread. A hook that runs
elsewhere changes nothing and flags the next frame "everything is stale".

| Hook | File | Runs on | Pushes | Skipped |
|---|---|---|---|---|
| `LLSpatialPartition::put` (after the octree insert) | `llspatialpartition.cpp` | main (`LLDrawable::movePartition`, `LLPipeline::addObject`) | extents, cause `add_remove` | not fixed casters |
| `LLSpatialPartition::remove` (before the removal) | `llspatialpartition.cpp` | main (`LLPipeline::unlinkDrawable`, `move`) | extents, `add_remove` | not fixed casters |
| `LLSpatialPartition::move`, in-group branch | `llspatialpartition.cpp` | main | extents after the update, `edit` | not fixed casters |
| `LLPipeline::markRebuild(drawable, flag)` | `pipeline.cpp` | main (object updates, LOD, the pin) | extents when `flag & (VOLUME | POSITION | RIGGED)`, `edit` | `TCOORD`, `COLOR` only |
| `LLPipeline::markMoved` | `pipeline.cpp` | main | the extents it moves from, `edit` | not fixed casters |
| `LLPipeline::updateGeom` (a delayed rebuild completes) | `pipeline.cpp` | main (`display`) | extents, `edit` | not fixed casters |
| `LLPipeline::shiftObjects` | `pipeline.cpp` | main (`LLWorld::updateAgentOffset`) | translates the pending boxes | |
| `LLViewerObject::processUpdateMessage` (the classifier) | `llviewerobject.cpp` | main (the message handlers) | extents of the object (or its bridge) when it turns into a mover or its physics flag flips, `mover` | avatars |
| `LLPipeline::unlinkDrawable` | `pipeline.cpp` | main (`LLDrawable::markDead`) | none: `remove` has pushed; calls `forget` (record, tile and pins) | |

`LLSpatialGroup::addObject`/`removeObject` (the spec's choice) are not used: they run for the octree's own rebalancing
(`handleInsertion`/`handleRemoval`) too, which is not a change of the scene. `put`/`remove` are the real insertions and removals
and see the same drawables. Not fixed casters: avatars, attachments, HUD, animesh, flexible prims, client-spun parts, movers,
physical objects (and children of a mover or physical root), and render types that cast no lamp shadow (particles, sky, water).
`markRebuild(group)` is not hooked: it fires for alpha re-sorts and texture animation.

Nothing else is new at a shared path: `LLPipeline::markVisible` drops a bridge only inside a tile render (`sLocalShadowPass`),
`LLPipeline::renderShadowImpl` calls `pinCasters` only inside a tile render, and `LLVOVolume::calcLOD` reads `mLampLODPin`
(zero for every volume outside a pool tile). No free, no static state beyond the feed's own list was added to a function a worker
thread runs (`LLRender::shutdown`, `LLImageGL`, `LLVertexBuffer::cleanupClass` are untouched).

### The classifier

Sticky for the session, per object (`LLViewerObject::mPoolFlags`): a mover is an object the simulator reported moving, either a
velocity or an angular velocity on a root, or a position or rotation change after its creating update unless the agent has it
selected. A physical object is one with the physics flag. Movers and physical roots are not in the fixed layer: `markVisible`
drops their bridge from a tile's cull, and a change of theirs (or of any of their children) never makes a tile stale. The lamp's
own linkset is the fixture and is never dropped. A moving part inside an otherwise still linkset (a spinning fan blade, a linked
door, flexi) is batched with its group, so it casts with the pose of the last render and never invalidates a tile: a known limit.

### The LOD pin

`pinCasters` (called from `renderShadowImpl` after the tile's cull and sort) walks the groups the tile is about to draw and, for
every volume inside the light's volume that is not rigged, worn or a HUD, takes one pin for the tile (`PinBook`). The pin is a
counter on the volume (`mLampLODPin`); `calcLOD` returns LOD 3 for a pinned volume, in the main view too, and the first pin of a
volume below LOD 3 calls `forceLOD(3)` (cause `lod_up`: the tile is drawn once more with the full detail and then holds; two
renders). A tile gives its pins back when its render no longer meets the object, when the tile is dropped, and at release (not at
exit: the objects go with the world). Cost: the triangles of everything inside a lamp's light, in the main view too, at full detail;
not measured here (it needs the in-world scene: `pool.pinned_objects` counts the volumes held).

The four camera-driven inputs the P2 audit listed:

| Input | What P3 does | What stays |
|---|---|---|
| Caster LOD (`calcLOD`, `computeLODDetail`) | closed by the pin for everything inside a tiled light | an object that enters a light is drawn at the camera's LOD until the first render that meets it (one `lod_up` render) |
| Object loading by camera distance (`llvocache.cpp:1037`-`1060`) | nothing | an object outside the near radius that never entered the main frustum or the rear sphere has no drawable: it does not cast until loaded, then it arrives as `add_remove`. Documented limit |
| Mesh asset fetch (`llvovolume.cpp:6066`) | the completion of a delayed rebuild makes the tiles stale (`edit`) | the order and speed of the fetch follow the camera's priority; a mesh that never loads does not cast |
| Alpha-mask texture virtual size (`llvovolume.cpp:889`-`905`, `6108`) | nothing: the mask uses whatever resolution the texture has at the render, and a texture that finishes loading later does not make the tile stale (texture changes are not a rebuild) | the woven shade of a pendant lamp can still show a coarser mask until the tile is next drawn. The masked-caster refinement (D16, stage P5) is the fix: a tile-derived virtual size and a re-render on improvement |

### Scheduler

`classifyTile` gives each placed tile one verdict from the facts: never drawn, a dirty box, the lamp's pose stale, the lamp
moving, a down-sized tile, an avatar in the light or one that just left. A verdict of "nothing" is no item: a static scene costs
no render. Items go to two schedules, each under its own fixed CPU cap:

- **static** (`selectStatic`, 0.35 ms): the first fill nearest first, then stale tiles oldest first (frames since the change that
  touched them);
- **dynamic** (`selectDynamic`, 0.30 ms, at most 24 avatar draws): a lamp that is moving, or a tile with an avatar in its light
  (the avatars are still drawn into S until P4, so such a tile is a full render every frame): items past the 4-frame deadline
  first, then nearest first.

The two caps together exceed the 0.4 ms budget when both are busy; the dynamic one is spent only while an avatar is under a lamp
or a lamp moves, and P4 re-divides it (S/L split). A lamp that moves is drawn every frame with its current pose, and the lookup
matrix is built from the pose the tile was drawn with, so a lagging picture stays coherent (D10).

The avatars a tile holds are the ones the render would draw (visible, not impostored): their visibility still follows the camera
until P4, so an avatar that leaves the view makes its tile stale once.

### Stats

`ProjectorShadows.stats` (development builds) gained `pool.rerender_causes` (renders by cause, cumulative), `static_renders` and
`dynamic_renders` (cumulative), `last_frame_static_renders`, `last_frame_dynamic_renders`, `pinned_objects`, `pending_tiles`,
`avatar_tiles`, `dirty_boxes_last_frame`, `frame`, `dirty_age_ms_max` (the longest a changed tile waited for its render, since the
last `reset_ages`) and per tile `pending`.

### Interfaces a second cache (the point lights' faces) reuses

Nothing in these is about projectors; they are keyed by a tile or face key and an agent-space volume.

- `LLShadowDirtyFeed::beginFrame(frame)`: the frame's boxes with causes (idempotent per frame, so two caches may read them).
- `llprojectorpool::ChangeIndex`: `rebuild(volumes)` with one `Pyramid` per tile or face, `apply(boxes, everything, hits)`.
- `llprojectorpool::classifyTile(TileFacts)` and `selectStatic` / `selectDynamic`: the verdict and the order of work.
- `llprojectorpool::PinBook` plus `LLVOVolume::pinLampLOD`: the LOD pin's books by key.
- `llprojectorpool::castsFixed`, `hookSkips`, `becomesMover`: the mover rules.

What stays in `LLProjectorShadows`: the light set, the lamp's pose and volume, the lookup matrix and the atlas.

### Known limits of P3

- A moving part inside a still linkset, client spin and flexi cast with the pose of the last render.
- Avatars are still drawn into S (a tile with an avatar in its light is a full render every frame, under the dynamic cap) and their
  visibility follows the camera until P4.
- The mover classifier is sticky: a prim another avatar edits (a server-reported move after creation) stops casting lamp shadows
  for the session. An object that arrives through the object cache and differs from its cached position counts as moved.
- Texture detail of alpha-masked casters (above) and object loading by camera distance (above).
- Not measured in-world: the LOD pin's triangle cost, the steady state of `rerender_causes` in the owner's rooms, and the hooks'
  CPU cost (a few relaxed loads per hook while no tile exists; one box push per change while tiles exist).

### For the point-light stages (what is covered)

- One generic pass flag, `LLPipeline::sLocalShadowPass` (`LOCAL_SHADOW_NONE/STATIC/DYNAMIC`), is the check at every site (the mover filter, the pin).
- The light-linkset exemption compares the roots of the bridge and of `RenderSpotLight` (the emitter drawable): nothing spotlight-only.
- Every hook goes through `LLShadowDirtyFeed::push`; several caches read the same per-frame list (`beginFrame` is idempotent per frame) and
  `noteShift` translates it once. A cache with its own list is not needed.
- The mover and physical bits are on the object (`mPoolFlags`, read through `LLShadowDirtyFeed::flagsOf`); transitions push through the feed.
- The pin counter on the volume is shared; each cache has its own `PinBook` keyed by tile, so two caches can pin one object.
- Left for later: `renderTile` still takes the record (not `fbo, rect, view, proj, far, light, pass, two_sided`) and saves the GL state once per
  frame, not once around a tile list across caches; `LevelController` is unchanged (no demand callback); the caster faces stay front-only (D12).
