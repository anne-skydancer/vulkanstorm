# Projector shadow pool

Spec: `reports/projector-pool-2026-11-13/design-final.md` (sections 3 to 9). This page says what is built, stage by stage. The
pool is the Stochastic method's (`RenderShadowMethod` 2) projector shadow when `RenderShadowDetail` is 2 (sun and projectors).
Legacy (method 0) keeps its two re-rendered slots, byte for byte (`scripts/shadows/check_legacy_shadows.py`,
`scripts/shadows/check_legacy_glsl.py`).

## What it replaces

Today's two slots (`mSpotShadow[0..1]`, window-sized maps) are handed from lamp to lamp by a view-dependent priority and are
re-rendered every frame, so a lamp's shadow can vanish when the camera turns. The pool gives every projector within the shadows'
reach (`min(RenderFarClip, 128 m)`, measured from the camera's position, with a hysteresis band) a tile of its own in a shared
depth atlas. The tile's size and view come from the light alone; the caster LOD is closed by stage P3's pin (below) and, since stage P4 (below), the avatars are a dynamic layer chosen by position, never by what the camera sees.

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
  objects and, until P4, the avatars into S, then S to L for the tile), ordered by `selectStatic`: never drawn first, nearest first; then
  **oldest first by age in frames, the distance only breaking a tie**, so the order is a round robin by staleness.
  - **Budget (P2c; re-divided in P4, see "Stage P4").** A fixed cap of **0.35 ms** of **CPU** time per frame for the tile renders (since P4 `kFrameCapMs`, shared with the dynamic layer). The frame-time basis is
    an assumed 8 ms frame (125 fps), not a measured one: on it the cap is 4.4 % and the whole light and shadow budget (+5 %) is
    about 0.4 ms. The cap fits four measured tiles (0.07 ms each, owner session 2026-11-14) with a margin, so a room of four
    lamps redraws every tile every frame. It leaves 0.05 ms of the 0.4 ms. (P2c and P3 added a second cap of 0.30 ms for the dynamic tiles, which
    did not fit in the 0.05 ms: the two together could reach 0.65 ms in a frame that had both. P4 replaced the two by one combined cap.) The measured cost of a
    tile (`last_cpu_ms`) only predicts how many fit; it does not set the budget. A frame also stops rendering once the measured
    time reaches the budget (the first pick always runs, so a tile dearer than the budget still progresses). Until P3, a static
    scene re-rendered every tile that fits; from P3 it renders none.
  - **The Legacy-cost guarantee is dropped.** P2 and P2b promised that the pool costs at most what Legacy's two slot renders
    cost. With a fixed cap and no change tracking that no longer holds (four lamps cost about 0.28 ms of CPU a frame against
    Legacy's two slots); P3 skips the tiles that did not change, so a static scene costs no tile render (but not nothing: see Stage P3, "What a static scene still costs"). Nothing of this is measured in-world.
  - **Cold start.** A record that was never drawn is predicted at `coldItemMs` (the running average, at most a quarter of the cap),
    not at the average's 0.5 ms start value, which is above the cap and would draw one tile a frame after login or a teleport.
  - *Why P2b starved tiles:* the budget was 2 x the running average of one render (0.14 ms at 0.07 ms), the two nearest tiles filled
    it every frame, and a far tile only got a turn after the 250 ms "stale" deadline (up to 24 frames at 58 fps).
    `ProjectorShadows.stats` `pool.max_stale_frames` is, since P3, the age of the oldest PENDING tile (frames since the first change that touched it; 0 when none waits): a tile that is merely not redrawn is not stale.
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
- **Emitter: a fixed 1 cm light (projector shadow sharpness P2).** Every lamp is a 1 cm light (SL's minimum prim size), whatever its
  prim, at the apex of the lamp's frustum, the projection centre of the pose the tile was drawn with (the lamp's anchor point; the
  uniform `proj_shadow_origin`, which the normal offset's direction to the lamp also uses). The penumbra's light size is the uniform
  `proj_shadow_light_size` = 0.01 m times `RenderShadowLightSize`, on both axes of the map (`emitterExtent`, `llprojectorshadowpool.h`);
  this replaces the prim's own face and its 2 cm .. 15 cm clamp (P2b). `RenderShadowLightSize` is the one softness factor of every
  light: the sun and moon keep their formula (0.53 degrees times the EEP sun or moon scale times the setting), a projector lamp is
  0.01 m times the setting, and point lights will use the same rule. Its default is 0.23 (it was 1.0), so a lamp's emitter is 2.3 mm
  and the sun's disc is 0.23 of its physical size by default. Preferences, Graphics, Rendering 2, has a "Shadow softness" slider
  next to the Shadow method (Stochastic only): 0.05 to 1.0, logarithmic in two halves meeting at 0.23, in 0.01 steps of its
  position, with an editable number box that shows and accepts the real value (`LLShadowMethod::softnessFromPosition` and
  `softnessToPosition`). Graphic presets save and load it. Nothing depends on the camera.
- **Contact hardening and a texel normal offset (P2).** The filter's radius follows the blocker nearest the receiver (the largest
  map depth among the blocking texels of the search), not the blockers' mean: an edge is as sharp as its closest contact allows, and
  a contact (a foot on a floor) is no longer blurred by the farther blockers (head, shoulders). The receiver is offset along its
  geometric normal by one texel of the tile at the receiver, scaled by sin(theta) of the angle to the lamp, the way the sun's
  `shadowNormalOffsetPos` works (texel = w / (k N), `w` the axial distance); it replaces the fixed 4 cm `spot_shadow_offset`, which
  pulled the shadow 5 to 11 mm short of a toe and left it grey under it. Measured against an analytic area light
  (`scripts/shadows/proj_march_bench.py --edges`, 2.3 mm emitter): the toe's x50 lands within 1 mm of the reference on the 2048 and
  1024 tiles (today 5 mm short), the value under the toe is 0 (the study measured 0.04 to 0.19 there with today's lookup and the 6 and 15 cm prims) and the toe's
  RMS falls from 0.17 to 0.05; the isolated lookup costs about 7 % more (three timing runs of the bench's
  `--time`, 2048 and 1024 tiles: today 0.112 to 0.115 ms, P2 0.120 to 0.122 ms for a 2560x1440 full-screen layer, a mean of +6.7 %,
  0.008 ms per layer; the study measured +4 %, and earlier single runs of this change varied between -1 % and +6.5 %, so the
  measurement noise on a shared PC is about 6 %). Static shadows get it too: the lookup is the same for every tile.
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

- **Avatar shadows (P2, history).** Legacy re-rendered its two slots, avatars included, every frame. P2 rendered the tiles that fit the 0.35 ms cap, oldest first, so with N lamps an avatar's shadow refreshed every ceil(N / tiles per frame) frames. P3 made a tile with an avatar in its light a dynamic item (a full tile render every frame). P4 (below) draws the avatars as a layer over the tile instead: S is not drawn again for an avatar.
- The caster LOD no longer follows the camera (P3), and since P4 neither do the avatars.
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
| `edit` | a fixed object in the light was rebuilt (volume, rigged, a position-only rebuild), moved, retextured (a prim with a blended, alpha-masked or alpha-textured face), or its geometry finished a delayed rebuild (a mesh that loaded) |
| `mover` | an object turned into a mover or a physical one, stopped being physical, or became active or static again (one push at each edge) |
| `lamp` | the lamp's own pose changed: position more than 1 mm, rotation 1e-4, scale 1 mm, angle 1e-4, reach 1 cm |
| `avatar` | P4: an avatar is in the light (the avatar layer of the tile is drawn again, S is not), or it left (one layer pass restores L from S) |
| `lowres` | a layout change down-sized the tile (a blit): drawn again at its new size |
| `other` | the dirty list overflowed (65536 boxes in one frame), or a hook ran off the main thread: every tile is stale once |

Texture-coordinate and colour rebuilds (texture animation, tints) never make a tile stale, nor do the whole `REBUILD_GEOMETRY` set (`markForUpdate`, selection, bump changes: it carries `POSITION` only because it rebuilds everything), nor a lamp's colour, intensity or falloff. The rule is tested with the real `LLDrawable` flag values (`llprojectorpool::rebuildDirties`; `llshadowdirtyfeed.cpp` static-asserts that they match `lldrawable.h`). A texture, material, bump or glow change dirties tiles when the prim had a blended, alpha-masked or alpha-textured face before the change or has one after it (`llprojectorpool::texturedDirties(was, now)`): a face turned opaque closes a hole the cached tile still has, so it must push too. `was` is the object's `POOL_ALPHA_FACE` bit, what the feed last saw; `noteTextured` and `noteDrawable` (every put, rebuild and edit of an object that can be a fixed caster) refresh it. The bit is only kept while the feed tracks, so a second bit, `POOL_ALPHA_KNOWN`, says it was written since tracking started (`setTracking(true)` clears it on every object in one walk of the object list, once per switch-on; an object that leaves the fixed layer loses it too): an unknown `was` makes `texturedDirties(was, now, known)` push once, conservatively, and the object is known from then on. A prim with an alpha face that predates tracking (login with Stochastic, a switch from Legacy) and is made opaque by its first edit is therefore covered. A face whose material has alpha mode None or Emissive counts as opaque whatever its texture carries (it goes to the opaque pool and is not masked: `LLPipeline::getPoolTypeFromTE`), like the shadow pass. `LLPipeline::markTextured` callers differ in whether the hook sees the old state (`LLVOVolume::setTEColor` calls it before `LLPrimitive::setTEColor` applies the tint, and so calls it again after an alpha change) or the new one (the `setTE*` and material setters, `changeTEImage`); the rule needs neither. An opaque prim's opaque-to-opaque texture swaps never push. Animated textures never do, on any face (a known limit for alpha faces: the tile keeps the silhouette of its last render). An alpha texture that finishes loading raises `REBUILD_VOLUME` in `LLFace::dirtyTexture` and so dirties; an opaque or still-unloaded texture does not. A texture of any component count that finishes loading also calls `markTextured` at the end of `LLFace::dirtyTexture`, which refreshes the alpha bit (the face counts as alpha only once its texture's components are known) and pushes if it now has an alpha face. A tint's alpha change raises `REBUILD_VOLUME` in `LLVOVolume::setTEColor` and dirties by itself. A 2-component (grey plus alpha) texture arriving later is covered by that `markTextured` call, although `dirtyTexture` raises the rebuild only for 4-component textures; the upstream condition is left as it is.

### The hooks, and the thread each runs on

All hooks push a box (the extents of what changed, agent space) with a cause into one double-buffered list
(`LLShadowDirtyFeed`). `LLProjectorShadows::update` swaps it before any render of the frame (a change a render itself causes lands
in the next frame), tests the boxes against the tile volumes through the 8 m grid (`ChangeIndex`), and keeps the first cause per
tile. Every hook except the classifier is a no-op until the pool holds tiles (`setTracking`); the classifier always runs, so its
sticky bits are right when tiles appear. Every hook runs on the main thread; a hook that runs elsewhere changes nothing and flags
the next frame "everything is stale".

| Hook | File | Runs on | Pushes | Skipped |
|---|---|---|---|---|
| `LLSpatialPartition::put` (after the octree insert) | `llspatialpartition.cpp` | main (`LLDrawable::movePartition`, `LLPipeline::addObject`) | extents, cause `add_remove` | not fixed casters |
| `LLSpatialPartition::remove` (before the removal) | `llspatialpartition.cpp` | main (`LLPipeline::unlinkDrawable`, `move`) | extents, `add_remove` | not fixed casters |
| `LLSpatialPartition::move`, in-group branch | `llspatialpartition.cpp` | main | extents after the update, `edit` | not fixed casters |
| `LLPipeline::markRebuild(drawable, flag)` | `pipeline.cpp` | main (object updates, LOD, the pin) | extents when `rebuildDirties(flag)` (volume, rigged, position alone), `edit` | `TCOORD`, `COLOR`, `REBUILD_GEOMETRY` |
| `LLPipeline::markTextured` | `pipeline.cpp` | main (the `setTE*` family, `markForUpdate`) | extents of a prim with an alpha face, `edit` | opaque prims |
| `LLVOVolume::notifyMeshLoaded` | `llvovolume.cpp` | main (the mesh repository's callback, run from the main loop) | extents now, and again when the rebuild it waits for completes, `edit` | not fixed casters |
| `LLPipeline::markMoved` | `pipeline.cpp` | main | the extents it moves from, `edit` | not fixed casters |
| `LLPipeline::updateGeom` (a delayed volume or rigged rebuild, or a mesh that loaded, completes) | `pipeline.cpp` | main (`display`) | extents, `edit` | not fixed casters |
| `LLPipeline::shiftObjects` | `pipeline.cpp` | main (`LLWorld::updateAgentOffset`) | translates the pending boxes | |
| `LLViewerObject::processUpdateMessage` (the classifier) | `llviewerobject.cpp` | main (the message handlers) | extents of the object (or its bridge) when it turns into a mover or its physics flag flips, `mover` | avatars |
| `LLPipeline::unlinkDrawable` | `pipeline.cpp` | main (`LLDrawable::markDead`) | none: `remove` has pushed; calls `forget` (record, tile and pins) | |

`LLSpatialGroup::addObject`/`removeObject` (the spec's choice) are not used: they run for the octree's own rebalancing
(`handleInsertion`/`handleRemoval`) too, which is not a change of the scene. `put`/`remove` are the real insertions and removals
and see the same drawables. Not fixed casters: avatars, attachments, HUD, animesh, flexible prims, client-spun parts, movers and
physical objects while they are active (and children of an active mover or physical root; a mover at rest is static, drawn, and
its changes push), and render types that cast no lamp shadow (particles, sky, water). Terrain and grass are left out too: their
geometry follows the camera's distance (the terrain patch's render stride, the grass blade count), so counting them would redraw
tiles as the camera moves; a tile keeps the terrain and grass it was drawn with (a known limit: a terraform shows in a tile only
when something else redraws it). `markRebuild(group)` is not hooked: it fires for alpha re-sorts and texture animation.

The hooks added in the review round, with their callers and threads: `LLShadowDirtyFeed::noteTextured` (from `LLPipeline::markTextured`,
main thread only, returns off it), `noteMeshLoaded` / `meshLoaded` / `clearMeshLoaded` (from `LLVOVolume::notifyMeshLoaded` and
`LLPipeline::updateGeom`, main), `setLampRoots` (once a frame from `LLProjectorShadows::update`, main), `LLProjectorShadows::forgetPinned`
(from `forget`, itself from `LLPipeline::unlinkDrawable`, main, not while the viewer exits), `LLVOTree::pinLampLOD` / `unpinLampLOD`
(from a tile render and from a tile's release, main). None frees, allocates GL objects or keeps static state beyond the feed's
own main-thread list.

Nothing else is new at a shared path: `LLPipeline::markVisible` drops a bridge only inside a tile render (`sLocalShadowPass`),
`LLPipeline::renderShadowImpl` calls `pinCasters` only inside a tile render, and `LLVOVolume::calcLOD` reads `mLampLODPin`
(zero for every volume outside a pool tile). No free, no static state beyond the feed's own list was added to a function a worker
thread runs (`LLRender::shutdown`, `LLImageGL`, `LLVertexBuffer::cleanupClass` are untouched).

### The classifier

Sticky for the session, per object (`LLViewerObject::mPoolFlags`): a mover is an object the simulator reported moving, either a
velocity or an angular velocity on a root, or a position or rotation change after its creating update unless the agent has it
selected. A physical object is one with the physics flag. The position and rotation compares use tolerances above the terse-update
quantisation (0.1 m, 1e-3 per quaternion component), so a full update after a terse one cannot read as a move; a real move below
them is not classified, and the object stays a fixed caster whose own moves push. Movers and physical roots are not in the fixed
layer **while they are active**: `markVisible` drops their bridge from a tile's cull, and the hooks skip them in the same
state (`drawable->isActive()`), with one push at the edge where they become active (the tile that holds them at the old pose
drops them) and one where they are static again (they are drawn from then on). A mover at rest, say a scripted door the simulator
moved with no velocity, is a fixed caster whose changes push. The lamp's own linkset is the fixture: it is never dropped by the
cull and its changes always push (`setLampRoots`). A moving part inside an otherwise still linkset (a spinning fan blade, a linked
door, flexi) is batched with its group, so it casts with the pose of the last render and never invalidates a tile: a known limit.

### The LOD pin

`pinCasters` (called from `renderShadowImpl` after the tile's cull and sort) walks the groups the tile is about to draw and, for
every volume or tree inside the light's volume that is not rigged, worn, a HUD or fully transparent (every face alpha 0 and no glTF
material: it casts nothing), takes one pin for the tile (`PinBook`). The light's volume is the tile's render frustum (aspect
included), the same region a change dirties the tile for. The pin is a counter on the object (`mLampLODPin`); `calcLOD` returns
LOD 3 for a pinned volume, in the main view too, and the first pin of a volume below LOD 3 calls `forceLOD(3)` (cause `lod_up`:
the tile is drawn once more with the full detail and then holds; two renders). A pinned tree has its trunk LOD held at 0, the best
(`LLVOTree::idleUpdate`), because its trunk rebuilt with the camera's distance. The books are keyed by object id; an unlinked
drawable drops its object from every tile's books (`forgetPinned`), so an object re-created with the same id is pinned again. A tile gives its pins back when its render no longer meets the object, when the tile is dropped, and at release (not at
exit: the objects go with the world). Cost: the triangles of everything inside a lamp's light, in the main view too, at full detail,
with **no ceiling** (a lamp over a dense scene pins all of it, and a prim that only some other lamp's tile cuts is pinned for it
too); not measured, nothing in-world has been: `pool.pinned_objects` counts the objects held, `pool.pinned_triangles` their triangles at
that detail (read when the stats are asked for), for the owner session to measure.

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
no render. Since P4 the verdict has two outputs: `needs_render` (the fixed layer S is drawn again, and S is copied to L with the
avatars over it) and `needs_layer` (only the avatar layer). Items go to two lists that share one cap (Stage P4, "The combined cap"):

- **static** (`selectStatic`): the first fill nearest first, then stale tiles oldest first (frames since the change that
  touched them, or, for a tile stale only by its lamp's pose or a resize, since it was drawn);
- **dynamic** (`selectDynamic`, at most 24 avatar draws): a lamp that is moving (the whole tile every frame) and the avatar layers
  (a tile with an avatar in its light, or one that just left): the own avatar's faces first (every frame), then items past their
  deadline (2 frames for a face whose nearest other avatar is within 10 m of the camera, else 4), then nearest first, by the
  camera's position.

A lamp that moves is drawn every frame with its current pose, and the lookup
matrix is built from the pose the tile was drawn with, so a lagging picture stays coherent (D10).

### What a static scene still costs

No tile render, no index rebuild and no box test: a frame with no dirty box does one read of every lamp's spot parameters and pose
(`read_spot`, `light_pose`, `poseChanged`: the pose-change detection, at most 256 lamps), one pass over the avatars' extents (every avatar that casts, since P4)
against each placed tile's volume, one `classifyTile` per tile, and the hooks' relaxed loads. A lamp's matrices and volume are rebuilt
only when its pose changed or agent space shifted; the change index is built only on a frame that has boxes (or an overflow). Not
measured in-world.

### Cost of the P3 stage (what is and is not measured)

Nothing is measured in-world. P3 had the caps of the design: static 0.35 ms plus dynamic 0.30 ms of CPU per frame against a light and
shadow budget of about 0.4 ms (an assumed 8 ms frame), which did not fit when a lamp moved or an avatar stood in a
light; P4 replaced them by one combined cap (below). The LOD pin has no ceiling (above). `pool.pinned_triangles` and `pool.mover_objects` are there for the
owner session to measure what the pin and the classifier cost.

### Stats

`ProjectorShadows.stats` (development builds) gained `pool.rerender_causes` (renders by cause, cumulative), `static_renders` and
`dynamic_renders` (cumulative; since P4 a moving lamp redrawn whole), `layer_renders` (cumulative avatar layers), `last_frame_static_renders`, `last_frame_dynamic_renders`, `last_frame_layer_renders`, `last_frame_avatar_draws`, `pinned_objects`, `pinned_triangles`,
`mover_objects` (objects flagged as movers since the feed started), `pending_tiles`, `avatar_tiles`, `dirty_boxes_last_frame`, `frame`,
`dirty_age_ms_max` (the longest a changed tile waited for its render, since the last `reset_ages`), `dirty_renders` (renders a change
caused: all but the first fills, lamp moves, avatars and resizes), `max_stale_frames` (the age of the oldest pending tile) and per tile
`pending`, `avatar`, `pose_stale` and `lamp_moving`.

### Interfaces a second cache (the point lights' faces) reuses

Nothing in these is about projectors; they are keyed by a tile or face key and an agent-space volume.

- `LLShadowDirtyFeed::beginFrame(frame)`: the frame's boxes with causes (idempotent per frame, so two caches may read them).
- `llprojectorpool::ChangeIndex`: `rebuild(volumes)` with one `Pyramid` per tile or face, `apply(boxes, everything, hits)`.
- `llprojectorpool::classifyTile(TileFacts)` and `selectStatic` / `selectDynamic`: the verdict and the order of work; `selectFrame`
  merges both lists of every cache under the one cap (items carry their pool; picks come back packed with it).
- `llprojectorpool::frameLedger()` (what the caches spent in the frame) and `LLLocalShadowGL::Scope` (`lllocalshadows.h`: one save and
  restore of the GL state for the frame's tile renders, the first scope saves and the last restores).
- `LLShadowDirtyFeed::setTracking(owner, on)` and `setLampRoots(owner, roots)`: tracking and lamp roots per owner (P4).
- `llprojectorpool::PinBook` plus `LLVOVolume::pinLampLOD`: the LOD pin's books by key.
- `llprojectorpool::castsFixed`, `hookSkips`, `becomesMover`: the mover rules.

What stays in `LLProjectorShadows`: the light set, the lamp's pose and volume, the lookup matrix and the atlas.

### Known limits of P3

- A moving part inside a still linkset, client spin and flexi cast with the pose of the last render.
- (Closed by P4: the avatars are the dynamic layer, by position.)
- The mover classifier is sticky: a prim another avatar edits (a server-reported move after creation) is a mover for the session,
  which is out of the fixed layer while it is active and a fixed caster while it rests. An object that arrives through the object
  cache and differs from its cached position by more than the quantisation tolerance counts as moved.
- Terrain and grass (their geometry follows the camera) and animated textures on alpha faces do not make a tile stale.
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
- Left for later (P4 shared the GL save and the caps; the rest remains): `renderTile` still takes the record (not `fbo, rect, view, proj, far, light, pass, two_sided`) and saves the GL state once per
  frame, not once around a tile list across caches; `LevelController` is unchanged (no demand callback); the caster faces stay front-only (D12).

## Stage P4: the avatar layer

Avatars were drawn into the static atlas S, and which avatars were drawn followed the camera (`isVisible()`, `isImpostor()`). P4 puts
them into the dynamic layer, `LLPipeline::LOCAL_SHADOW_DYNAMIC` (declared in P3, unused until now), and divides the CPU between the
two layers once.

### S and L

- **S holds the fixed layer only.** A tile render (`LOCAL_SHADOW_STATIC`) no longer culls the avatar partitions (`PARTITION_AVATAR`,
  `PARTITION_CONTROL_AV`) and leaves the worn objects' bridges out (`markVisible`). No avatar change dirties a tile any more, and the
  tile's cache is never invalidated by an avatar walking by.
- **L is S plus the avatars in the light.** The avatar layer of a tile is: copy S to L for the tile (the avatars of the last pass are
  gone), then draw the avatars in the light into L (`LOCAL_SHADOW_DYNAMIC`: the avatar partitions and the worn objects' bridges only, from the
  pose the tile holds, no depth clear, the depth test composes the two). When the avatar has left, one more pass restores L from S.
  A full tile render that holds an avatar does both (S, copy, avatars) in one go.
- **A repack** (`applyPlan`) restores L from S for the tiles that hold an avatar before it copies L to S in full, so no avatar is ever
  copied into S; the avatar layers are drawn again by the dynamic schedule of the same frame.

### Pairing by position

`update` pairs every avatar that casts with every lamp whose volume its extents touch. Nothing in it reads the camera: no
`isVisible()`, no `isImpostor()`, no render distance. The avatars left out are those the avatar pool does not draw either: dead, a UI
avatar, too slow (`isTooSlow`), a jelly doll (which includes an RLV silhouette, a derendered and a too complex avatar) or an invisible one
(`getOverallAppearance() != AOA_NORMAL`; `renderShadow` repeats that test in the dynamic pass), and, with `RenderAvatarFriendsOnly`,
an avatar that is not a friend, the agent or a control avatar. In the dynamic pass the camera
gates of the avatar path are bypassed (all behind `sLocalShadowPass == LOCAL_SHADOW_DYNAMIC`, so every other pass is unchanged):
`stateSort` (render distance, `isVisible`), `markVisible` and `LLSpatialBridge::setVisible` / `updateDistance` for the attachments (the
impostor test and the "visible within a frame" test), and `LLDrawPoolAvatar::renderShadow` (the impostor test). An impostored avatar
casts in full with the pose of its last update; the avatar's own animation still follows the viewer's update rate for avatars out of
view, which is not changed here (not measured: see "Not verified"). The dynamic pass does not stamp the camera's visibility
(`LLDrawable::setVisible` skips `LLViewerOctreeEntryData::setVisible` in it), so an avatar behind the camera does not count as
visible to the animation, the impostor ranking or `updateImpostors`.

**Known limits (owner's walk session).** (1) The layer's LOD is not pinned: `pinCasters` runs for the fixed layer only, so an avatar's
body and its worn objects take the LOD the camera's distance gives them, and their silhouette may change across an LOD boundary as
the camera moves. (2) A face draws at most A_max (24) avatars, the nearest to the lamp (`LLPipeline::sLocalShadowAvatars`, tested by the body in `renderShadow` and by the attachments in `LLSpatialBridge::setVisible`), so a crowd under one lamp is bounded by the count the scheduler predicts; the farther avatars of a crowd do not cast in that face. (3) `updateCull` still culls the whole bridge partition for the layer and `markVisible` filters it; the 0.04 ms a face is an
estimate.

### The combined cap

P3 had two caps that could both be spent in one frame: static 0.35 ms and dynamic 0.30 ms, 0.65 ms, against about 0.4 ms of
the +5 % allowance (an assumed 8 ms frame). P4 has one (`llprojectorpool.h`, `selectFrame`):

| | ms of CPU a frame | meaning |
|---|---|---|
| `kFrameCapMs` | **0.35** | everything the shadow caches' tile work spends in a frame, all caches together (4.4 % of an 8 ms frame; 0.05 ms of the 0.4 ms stays free) |
| `kDynamicCapMs` | **0.20** | the dynamic layer's share while a static tile waits, spent first (a moving lamp's tile, the avatar layers). When no static tile waits, the share is the whole remaining cap (`dynamicShareMs`; hotfix C1) |
| `kStaticCapMs` | **0.15** | what the static tiles keep when the dynamic layer takes its 0.20 ms (`kFrameCapMs - kDynamicCapMs`); with no dynamic work they get all 0.35 |

- **Priority: the dynamic layer first.** An avatar or a moving lamp that lags is seen at once; a dirty static tile shows a stale
  shadow of something that changed, and its wait is bounded by the age order (oldest first). A static tile is never starved: the first
  static candidate has a place every frame when it fits the cap (the dynamic layer gives up its last faces for it); one whose own
  prediction is above the whole cap runs alone as the progress pick once it has waited 4 frames (or was never drawn), and that frame's
  dynamic picks are dropped, so the overrun is that one item and a standing avatar cannot starve it.
- **The sum of the predicted costs of a frame's picks never exceeds the cap**, except for the one progress pick: when nothing was spent
  yet in the frame, a single item whose own prediction is above the cap runs alone. Two caches share the cap through the frame ledger
  (`frameLedger()`): the second takes what the first left, and a frame in which the cap is spent serves nothing more.
- **A_max covers the static tiles too.** A static tile that draws avatars (a first fill, a dirty or low-res tile) is charged `min(avatars, A_max)` draws (`StaticItem::avatar_draws`) against what the dynamic picks left of A_max; one that does not fit waits (the order is kept), and the head tile keeps its place (the dynamic layer gives up its last avatar faces for it, only when the head is served; so for a cold fill of avatar-heavy tiles the dynamic avatar faces yield to the head tile), so a cold fill never draws more than A_max avatars in a frame.
- **In the frame** each render also checks the measured time against the cap (the dynamic list against its share: 0.20 ms while a static tile waits, the whole remaining cap when none does).
- **Cold start** (`coldItemMs`) is at most a quarter of the cap (0.0875 ms): four first fills a frame with no avatar layer, one a frame
  while the dynamic layer is full.
- Unit-tested: the combined cap is never exceeded with both kinds pending, over six costs on each side; the dynamic share, the static
  floor and the whole cap without dynamic work; the progress rule; two caches on one ledger.

### Cost of one avatar face, and the lag table

A face is one lamp's tile with an avatar in its light. **Estimate, not measured here:** an avatar layer costs about **0.04 ms** of CPU per
face (the pool spec's 0.04 to 0.15 ms for a tile with 1 to 4 avatar draws, 0.0375 ms a draw); the scheduler predicts every item at no less
than `kDefaultItemMs` = 0.05 ms and then at the item's own last measured cost (`lastAvatarMs`). The in-world cost per face is owed to the
owner's walk session; `pool.last_frame_avatar_draws`, `last_frame_layer_renders` and `last_frame_render_ms` are the readings.

Faces drawn a frame = **min(A_max left, dynamic CPU share / cost of a face)**, with A_max = 24 avatar draws (one per avatar in a lamp's
volume), less the faces drawn every frame. The share has two regimes (hotfix C1, `dynamicShareMs`):

- **while a static tile waits** it is 0.20 ms: with a 0.05 ms face that is **4 faces a frame**;
- **when none waits** it is the whole remaining cap, 0.35 ms: up to **7 faces a frame** at the 0.05 ms floor.

A_max does not bind below 24 faces. The wait of the last face of a set is `ceil(faces / per frame)` frames. The order of service is:
the faces of the user's own avatar first, then the faces past their deadline (the most overdue first, the safety net), then the camera
POSITION's distance to the face's nearest other avatar (not the lamp's distance, which ties at 0 indoors), then the oldest. The prediction (`lastAvatarMs`) is an EMA
(0.25) seeded by the first sample, each sample clamped to 0.7 ms (2 x the cap).

Faces drawn a frame / worst gap in frames, indoors, one avatar under all lamps, no static tile waiting (a model of `update()`,
`llprojectorshadowpool_test --table`; "before" is the release scheduler before C1, with its fixed 0.20 ms share):

| lamps | cost per face (ms) | before | after |
|---|---|---|---|
| 1 | 0.30 | 0.25 / 4 | 1.00 / 1 |
| 2 | 0.15 | 1.00 / 4 | 2.00 / 1 |
| 3 | 0.07 | 2.00 / 4 | 3.00 / 1 |
| 4 | 0.10 | 2.00 / 4 | 3.00 / 2 |
| 5 | 0.07 | 2.00 / 4 | 5.00 / 1 |
| 5 | 0.15 | 1.00 / 5 | 2.00 / 3 |

(Estimated from the cap and the 0.05 ms floor; the real cost of a face is unmeasured. In a real overload, more face cost than the cap
holds, the deadline turns the service into a round robin: the worst gap is then about the number of lamps, up to 5 in the model.)

### The own avatar and the distance cadence (own-avatar hotfix)

Owner decision 2026-11-16. Only when the dynamic list does not fit the frame budget does the order matter; when everything fits, every
face runs every frame, as in C1.

- **The user's own avatar** (`gAgentAvatarp`, `LLVOAvatar::isSelf()`): every face whose avatar layer holds it (`DynamicItem::own`) is redrawn
  every frame, always. It goes first, is exempt from the budget and from the deadline round robin (it counts against the CPU it spends and
  A_max, so the others get what is left), and runs past an exhausted cap. A heavy own face that alone exceeds the share runs alone
  (one bounded overrun). The static head keeps its place beside it when it fits; an overdue static head that has no room runs beside
  the own faces as the progress pick (a second bounded overrun), so neither guarantee starves the other. Other avatars' faces have no
  such exemption: a heavy face runs over the share only once past its deadline.
- **Other avatars**, by the camera POSITION's distance to the nearest non-own avatar of the face (never the view direction): within
  `kNearAvatarM` = 10 m the deadline is 2 frames, farther it is 4 (`dynamicDeadline`). A face that only restores L from S, and the
  moving lamps' tiles, keep the 4-frame deadline (a moving lamp runs over the share only once overdue, as in C1).

The bound is what the budget gives: the near faces' gap is at most 2 and the far faces' at most 4 where the load (1 own slot + half a slot
per near face + a quarter per far face) fits the slots a frame holds (cap / face cost); beyond that the faces share what the own face
leaves and the gaps grow (the table in the commit message, `llprojectorshadowpool_test --table`). The constants are not settings.

### Footprint copy (hotfix C3)

An avatar layer pass does not copy and draw the whole tile. It copies S to L over the union of the rectangle the last pass drew and
the one the avatars cover now, and scissors the avatar draw to the new one; outside the rectangle the last pass drew, L equals S bit for
bit. The rectangle is the avatars' extents grown by a margin (`footprintMarginM`: 0.3 m + 0.04 m per frame of the extents' refresh
period + 0.15 s of the avatar's speed + 0.15 of the box diagonal), projected through the tile's matrices, plus 2 texels. The margin
covers stale extents: `calculateSpatialExtents` refreshes only every 4 to 20 frames (the period grows with the avatar count, 4 up to
5 avatars) and the box is only translated by the pelvis in between; rigged attachments are inside the extents, with no padding of their
own. The footprint is the whole tile when a corner is behind the eye plane, the matrix is not finite, an avatar is an impostor, or the
rectangle covers more than half of the tile. A full tile render still copies the whole tile. After a new atlas or a repack that moves
tiles the old rectangle is unknown and the next pass copies the whole tile. Gate: the unit test's pass model and
`scripts/shadows/projector_layer_footprint_gate.py` (GPU, every pass) compare L with a whole-tile copy plus the same scissored draw.

### Single-owner pieces made shared

- `LLShadowDirtyFeed::setTracking(owner, on)`: a mask of owners (`Projectors`, `Points`). The hooks record while any owner tracks; the last
  owner to leave drops the pending boxes, and one leaving while another tracks neither switches the hooks off nor drops its boxes.
  Callers: `LLProjectorShadows::ensureAllocated` and `release`, both main-thread only (the setter does nothing off it).
- `LLShadowDirtyFeed::setLampRoots(owner, roots)`: one list per owner, the hooks test the sorted union (`OwnerLists`). Caller: the
  projector pool's `update`, once a frame, main thread.
- The caps (`selectFrame`, `frameLedger`) and the GL save and restore (`LLLocalShadowGL::Scope`, `lllocalshadows.h`) are shared. With
  one pool in the viewer the scope opens and closes inside `update`; the point-light pool's coordinator will open one outer scope around
  both updates, and the counter makes the inner ones neither save nor restore.

### Not verified

No in-world check: the walk under three lamps, the dancer under club lights, the impostored and the behind-the-camera avatar casting,
attachments (rigid and rigged) in the layer, the per-face cost and the off-screen animation of paired avatars. The viewer was built and
the login screen started only. The owner's walk session is owed.
