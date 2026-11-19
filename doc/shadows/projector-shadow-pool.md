# Projector shadow pool

Spec: `reports/projector-pool-2026-11-13/design-final.md` (sections 3 to 9). This page says what is built, stage by stage. The
pool is the Stochastic method's (`RenderShadowMethod` 2) projector shadow when `RenderShadowDetail` is 2 (sun and projectors).
Legacy (method 0) keeps its two re-rendered slots, byte for byte (`scripts/shadows/check_legacy_shadows.py`,
`scripts/shadows/check_legacy_glsl.py`).

## What it replaces

Today's two slots (`mSpotShadow[0..1]`, window-sized maps) are handed from lamp to lamp by a view-dependent priority and are
re-rendered every frame, so a lamp's shadow can vanish when the camera turns. The pool gives every projector within the shadows'
reach (`min(RenderFarClip, 128 m)`, measured from the camera's position, with a hysteresis band) a tile of its own in a shared
depth atlas. The tile's view, content and size come from the light alone (lamp shadows L2: the camera's position only picks which tiles
shrink first when the atlas is over its budget, and a size change is drawn before it is shown; see "Tile resolution"); the caster LOD is
closed by stage P3's pin (below) and, since stage P4 (below), the avatars are a dynamic layer chosen by position, never by what the
camera sees. The lookup is a soft-shadow filter at the lamp's physical penumbra (lamp shadows L1, "The filter" below).

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
- **Tile size** (P2: from the light only, radius and field of view; the tile-resolution PR sized every tile by the camera
  position's distance to it; since lamp shadows L2 again from the lamp alone, `tileBaseSize`, with the pressure valve over the budget:
  see "Tile resolution" at the end). Placement by a buddy allocator; arrivals and departures are incremental; a size change is drawn by
  the tile's own render (draw-then-swap), and a repack is only a compaction at the tiles' sizes (bit-exact copies, then L to S in full)
  when an arriving tile finds no block although the free texels hold it, or a growth has found none for 3 s although they would.
- **What a frame does** (`LLProjectorShadows::update`, from `generateSunShadow`, not for probes): the light set, the level and
  placement, then the tile renders the budget allows. P2 has no change tracking, so every tile is a full render (the fixed
  objects and, until P4, the avatars into S, then S to L for the tile), ordered by `selectStatic`: never drawn first, nearest first; then
  **oldest first by age in frames, the distance only breaking a tie**, so the order is a round robin by staleness.
  - **Budget (P2c; re-divided in P4, see "Stage P4"; set from the fps floor in L2).** A fixed cap of **CPU** time per frame for the tile
    renders (since P4 `kFrameCapMs`, shared with the dynamic layer): 0.35 ms until L2, on the basis of an assumed 8 ms frame (125 fps, not
    a measured one) and the former light and shadow budget (+5 %, retired 2026-11-16); since L2 **0.5 ms**, 3 % of the owner's standing
    floor frame (60 fps, AGENTS.md; "The combined cap" in Stage P4 has the numbers). The paragraphs below describe P2's 0.35 ms. The cap fits four measured tiles (0.07 ms each, owner session 2026-11-14) with a margin, so a room of four
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
- **The filter (lamp shadows L1; P2b's radius cap in metres is gone).** After the blocker search the lookup filters at the physical
  penumbra radius, from the nearest blocker, between a floor of 1.5 tile texels and a cap of 12 (see "The filter" below). P2b capped the
  radius at 4 cm at the receiver; that made lamps above about 3 cm sharper than their size and is removed. The cap of 12 texels stays
  because the cost is fixed (9 search and 32 filter gathers): over a wider disc the same taps are further apart (a 12-texel disc has
  452 texels, and 32 bilinear 2 x 2 taps read 128 of them) and the penumbra turns grainy before it turns wrong.
- **Emitter: 4.5 cm times the Shadow softness (lamp shadows L1; was 1 cm in P2).** Every lamp is a disc of 4.5 cm times
  `RenderShadowLightSize`, whatever its prim, at the apex of the lamp's frustum, the projection centre of the pose the tile was drawn with
  (the lamp's anchor point; the uniform `proj_shadow_origin`, which the normal offset's direction to the lamp also uses). The
  penumbra's light size is the uniform `proj_shadow_light_size` = `emitterExtent(kProjectorLamp, RenderShadowLightSize)`, on both axes
  of the map (`llprojectorshadowpool.h`: one function, a `LampFilter` per lamp type with its base, floor and cap; `kProjectorLamp` is
  used, `kPointLamp` is prepared for the point-light stage). No script or build parameter carries a light size, so the base is a
  constant (P2b took the prim's face, clamped 2 to 15 cm; P2 a fixed 1 cm). The base comes from the light-size study
  (`reports/lamp-light-size-2026-11-18`, kept on the owner's machine): the largest size whose avatar shadows keep every body feature of
  2.5 cm or more readable, so the default softness 0.23 gives 1.035 cm and the slider's top 4.5 cm, short of the 6 cm blobs.
  `RenderShadowLightSize` sets how sharp the edge of every shadow is: it scales only the penumbra's emitter (for the sun and moon their
  formula, 0.53 degrees times the EEP sun or moon scale times the setting; for a lamp the 4.5 cm base), never the visible sun, moon or
  light. Its default is 0.23. Preferences, Graphics, Rendering 2, has a "Shadow softness" slider next to the Shadow method (Stochastic
  only): 0 (the sharpest) to 1.0 with 0.23 at its centre, on one offset-logarithmic curve over its position p, v = 0.23 (r^2p - 1) /
  (r - 1) with r = 1 / 0.23 - 1 (about linear below 0.1, where a narrower penumbra no longer shows; from the centre up a 0.01 step of
  the position changes the value by 3.5 % at the centre down to 2.7 % at the top, the largest step 0.026; the old logarithmic right half
  took 3.0 % everywhere, so the new curve is finer only above about 0.45), with an editable number box that shows and accepts the real value
  (`LLShadowMethod::softnessFromPosition` and `softnessToPosition`). At 0 the 8 sun rays share one direction and a lamp's edge is the
  filter's 1.5-texel floor: as sharp as each map's texels draw. Graphic presets save and load it. Nothing depends on the camera.
- **Contact hardening and a texel normal offset (P2).** The filter's radius follows the blocker nearest the receiver (the largest
  map depth among the blocking texels of the search), not the blockers' mean: an edge is as sharp as its closest contact allows, and
  a contact (a foot on a floor) is no longer blurred by the farther blockers (head, shoulders). The receiver is offset along its
  geometric normal by one texel of the tile at the receiver, scaled by sin(theta) of the angle to the lamp, the way the sun's
  `shadowNormalOffsetPos` works (texel = w / (k N), `w` the axial distance); it replaces the fixed 4 cm `spot_shadow_offset`, which
  pulled the shadow 5 to 11 mm short of a toe and left it grey under it. Measured against an analytic area light, with P2's lookup
  (`scripts/shadows/proj_march_bench.py --edges`; 2.3 mm emitter, before L1): the toe's x50 lands within 1 mm of the reference on the 2048 and
  1024 tiles (today 5 mm short), the value under the toe is 0 (the study measured 0.04 to 0.19 there with today's lookup and the 6 and 15 cm prims) and the toe's
  RMS falls from 0.17 to 0.05; the isolated lookup costs about 7 % more (three timing runs of the bench's
  `--time`, 2048 and 1024 tiles: today 0.112 to 0.115 ms, P2 0.120 to 0.122 ms for a 2560x1440 full-screen layer, a mean of +6.7 %,
  0.008 ms per layer; the study measured +4 %, and earlier single runs of this change varied between -1 % and +6.5 %, so the
  measurement noise on a shared PC is about 6 %). Static shadows get it too: the lookup is the same for every tile.
- **Caster blocks.** The Stochastic casters' rule (`shadowStochasticCasterF.glsl`, see `shadow-method.md`) takes its 2 x 2 blocks
  from `gl_FragCoord.xy - shadow_tile_origin` (zero outside the pool and the sun cascades), so a tile's pattern does not depend on
  where the tile sits in the atlas. The Stochastic sun cascades set the same uniform to `-(k mod 2^16)`, k the map corner's index on
  the world grid (`LLShadowCascadeFit::alphaDitherOrigin`), so their blocks follow the world texel while the camera walks.
- **Lifetime.** Allocated in `allocateShadowBuffer` (the place of the spot maps, not for probe snapshots), released when the method
  or detail leaves Stochastic detail 2, in `destroyGL` and in `cleanup` (while the context lives). Every entry point is on the main
  thread and returns at once off it or while the viewer exits. Nothing is added to `LLRender::shutdown`, `LLImageGL` or
  `LLVertexBuffer::cleanupClass`.

## Gates

| Gate | How | Result |
|---|---|---|
| G1 lookup equivalence | `python scripts/shadows/projector_pool_g1.py` (offline GL, no viewer) | tile in the atlas equals the same code on a standalone map, max abs 0 (atlas handling only: the reference carries the same filter rule, so it is not an independent check of the lookup; the independent evidence is the analytic scans of `proj_edges_bench.py` and the CPU twin) |
| The filter (L1) | `scripts/shadows/lamp_filter_twin.py` (CPU, no GL): no hard square on glass, lace or a lattice at tile texels of 8 to 0.5 cm, a straight edge's width within 20 % of a disc's; `proj_edges_bench.py` (offline GL) `--gates` (the avatar), `--stairs --softness 1 --plate-h 1.2 --fovs 2.0 --sizes 1024,2048` (the width), `--grazing` and `--time` (reported) | see "The filter" |
| G4 Legacy | `check_legacy_shadows.py` (C++ regions), `check_legacy_glsl.py` (GLSL text) | identical |
| G2, O-4, method switches, window toggle, quit | viewer, see the stage's PR | needs a world with projectors for the lookup parts |

### Known limits of P2

- **Avatar shadows (P2, history).** Legacy re-rendered its two slots, avatars included, every frame. P2 rendered the tiles that fit the 0.35 ms cap, oldest first, so with N lamps an avatar's shadow refreshed every ceil(N / tiles per frame) frames. P3 made a tile with an avatar in its light a dynamic item (a full tile render every frame). P4 (below) draws the avatars as a layer over the tile instead: S is not drawn again for an avatar.
- The caster LOD no longer follows the camera (P3), and since P4 neither do the avatars. (Since L2 the camera's position picks only which tiles shrink first when the atlas is over its budget, never what a tile holds: "Tile resolution".)
- **Casters outside the main view: draw info is not camera-gated; loading still is.** A tile renders through the same `LLPipeline::renderShadow` as the sun cascades and Legacy's spot slots, with a camera built the same way (`generateSunShadow`, `pipeline.cpp:13073`-`13089`; `renderTile`). `renderShadow` runs `updateCull` and `stateSort`, and `stateSort` ends in `postSort`, which rebuilds the dirty groups it culled (`pipeline.cpp:3921`, `3954`). Beyond that, no group's draw info depends on the main view: a spatial group queues itself for a rebuild when it is created or dirtied (`llspatialpartition.cpp:315`, `437`, `480`, `606`; `LLPipeline::markRebuild`, `pipeline.cpp:3403`), and `rebuildPriorityGroups` (`pipeline.cpp:3120`, run every frame from `postSort`) builds every queued group whether or not any camera sees it. Occlusion is off during a shadow render (`sUseOcclusion = 0`). So a loaded caster the camera never saw has its draw info and appears in the tile. Three inputs still follow the main camera and are P3 work: (1) object loading: an object outside the near radius (`SceneLoadMinRadius`, 32 m) that never entered the main frustum or the rear sphere gets no drawable (`llvocache.cpp:1037`-`1060`; once loaded it stays, since eviction is disabled); (2) mesh and sculpt assets: a caster whose asset was never fetched is skipped (`llvovolume.cpp:6066`); (3) alpha-masked casters: their texture's virtual size comes from the main camera, so a texture never seen may stay low-resolution or unloaded (`llvovolume.cpp:889`-`905`, `6108`). The caster LOD (above) is the fourth.
- A projector whose geometry the pool cannot use (field of view outside (0, 3.1 rad), zero or non-finite scale or radius) is not given a tile.
- A failed atlas allocation is logged once and retried with a back-off (5 s up to 60 s).
- The light set has no count cap: `RenderLocalLightCount` only switches `calcNearbyLights` on (>= 1) and the six-light cap applies
  to the non-deferred path only, so the pool considers every eligible spotlight within reach. When they do not fit even at the
  smallest level, the lights that sort last (smaller tiles first, then key) have no tile and are lit without a shadow.

## The filter (lamp shadows L1)

The owner's pictures of 2026-11-18 showed a projector's shadow broken into hard squares through a lattice or lace and dotted under
glass. Three things multiplied (`reports/lamp-shadow-method-2026-11-18`, kept on the owner's machine): tiles sized by the camera's
position (coarse when the camera stood back), see-through casters written as on/off texels, and a lookup that rebuilt a sharp edge
wherever the physical penumbra was narrower than 2.5 texels: with a 2.3 mm lamp that was everywhere, so every texel of a lattice bar or a
dithered pane was drawn as a square. L1 replaces the lookup's edge by the physical one; L2 (below, "Tile resolution") takes the camera
out of the tile's size. The see-through casters keep today's rule (the stage L3 of the study changes them).

### The lookup (`shadowStochasticUtil.glsl`, `stSpotFilterTile`, inside the G1 block; fragment code only)

1. The blocker search: 8 gathers on a Vogel disc whose radius is the penumbra a blocker at the near plane would cast, between the floor
   and the cap, plus one gather at the receiver's own 2 x 2 texels. The disc starts a quarter of its radius out; at the 12-texel cap
   that is 3 texels, and without the receiver's gather a blocker thinner than that right over the receiver (a lattice bar on a coarse
   tile) went unseen, the pixel read as lit beside filtered neighbours. Nothing blocked returns 1, all 36 texels blocked return 0.
2. The filter: 32 bilinear gathers on a Vogel disc weighted 1 - 0.9 r^2, at the physical penumbra radius from the nearest blocker
   (contact hardening), `pen * (c.z - z_near)` texels per axis, at least `ST_SPOT_KERNEL_MIN` = 1.5 texels (a narrower penumbra is drawn
   soft, never as the tile's squares) and at most `ST_SPOT_KERNEL_TEXELS_MAX` = 12 (the taps' reach; a cell's guard of 16 texels covers
   it, `static_assert(kCellGuard >= kProjectorLamp.kernel_max + 2)`).
3. No edge reconstruction (`stRv*`, `ST_RV_*`: gone, with the bench gates that tested it) and no cap in metres (`ST_SPOT_RADIUS_MAX_M`:
   gone). A coarser tile gives a softer edge, never a blockier one. The value of a receiver depends on its tile (or its cell's tile)
   alone: no screen, frame or pixel term.

The floor and the cap are the projector set of `llprojectorpool::LampFilter` (`kProjectorLamp`: 4.5 cm, 1.5, 12);
`scripts/tests/test_projector_lamp_filter.py` checks that the shader's numbers are the C++ ones and that nothing of the reconstruction or
the 4 cm cap is left. The constants are `#define`s and C++ constants: no uniform, no setting, no Preferences entry. The sun and Legacy
are untouched: everything from `shadowMap0` to the G1 block of the util is byte-identical, and the Legacy checks pass.

### Measured

CPU twin of the lookup (`scripts/shadows/lamp_filter_twin.py`; the lamp-shadow study's scenes: a window frame with glass at alpha 0.5,
an alpha-masked lace and an opaque lattice, a floor 4.5 m from the lamp, receivers 5 mm apart; the casters as today):

| | the shipped lookup (2.3 mm lamp, the study's twin) | L1, softness 0.23 (1.035 cm) | L1, softness 1.0 (4.5 cm) |
|---|---|---|---|
| neighbour pairs over 0.25 where the truth is smooth (the study's measure), tile texels 8 to 0.5 cm | 1.0 to 20.9 % | 0.00 to 0.03 % | 0.00 to 0.01 % |
| hard squares (a neighbour jump over 0.5 where the truth is smooth) | yes (texel squares) | none | none |
| largest jump where the truth is smooth | 1.0 | 0.32 (the floor's own ramp on 0.5 cm texels) | 0.27 |
| glass at 1 cm texels, RMS against the truth of the same lamp | 0.123 | 0.075 | 0.069 |

Before the receiver's gather was added to the search, the lattice at 1 cm texels had 54 hard squares at the default softness (pixels read
lit beside neighbours at 0.15 to 0.34): the 4 cm cap had kept the search small on coarse tiles; with it gone the search runs at its cap.

Straight opaque edges on a 2.5 mm tile (twin): the 10-90 % width is 0.82 to 0.90 of a uniform disc's wherever the radius is physical (the
filter's weights draw an edge about 16 % narrower than a disc of the same radius); at the floor it is wider (1.88 to 1.98 texels for a
1.035 cm lamp, physics 1.42), at the cap narrower. The 50 % contour wanders 0.01 to 0.87 texel inside that ramp.

Offline GL (`proj_edges_bench.py`, RTX 5070 Ti, driver 621.14, 2026-11-18, no build running):

| test | result |
|---|---|
| staircase width, fov 2.0, tiles 1024 and 2048, 2 to 45 degrees, a 4.5 cm lamp (`--softness 1 --plate-h 1.2`) | 23.7 to 25.0 mm against 28.5 mm (within 20 %: worst 17.1 % narrower, 12 scans) |
| avatar, tiles 2048 / 1024 (texels 2.3 / 4.6 mm), softness 0.23 | toe x50 -0.6 mm, nothing under the toe, shoulder x50 -0.9 mm, shoulder width 84 % / 89 % of the reference: PASS |
| toe contact RMS (reported) | 0.054 / 0.072 (P2: 0.049 / 0.062): the contact's own penumbra (0.4 mm) is far under the 1.5-texel floor |
| acne: a lit plane tilted 0 to 75 degrees, tiles 2048 / 1024 / 512 | no darkening |
| G1, the 4096 and 8192 atlases; the cells check | max abs 0 tile against standalone; PASS |

The light-size study's avatar outline (its `avatar_twin.py` with L1's lookup, `reports/lamp-light-size-2026-11-18/bench/l1_edge_twin.py`):
at the default softness the outline is set by the 1.5-texel floor on 2.5 and 5 mm tiles (0.5 and 1.0 cm in the figure plane); at the
slider's top (4.5 cm) the head's and shoulder's edges are 0.6 to 0.85 of physics, because the 12-texel cap binds on a 1 mm tile and the
nearest blocker and the weights narrow a curved silhouette.

### The default edge against the release (for the owner's in-world check)

Removing the edge reconstruction changes the default look, and the in-world check should expect it (the light-size audit's correction 3,
its finding A2). The figures are for a head's shadow on the floor in a 2.5 mm tile, the lamp 2.5 m above the head and the head 1.7 m
above the floor, measured from 10 to 90 %:

- **The release** draws the default edge at its physical width where the reconstruction is complete (`stRvVisibility`: below a physical
  radius of 1.5 texels, fading out by 2.5, `ST_RV_LO` and `ST_RV_HI`). There the head's physical penumbra is about 0.3 texel in radius,
  so the edge is almost hard: **about 0.4 texel**.
- **This branch** draws the head edge at **about 1.8 to 1.9 texels**, and each of its two changes alone would. The default edge's physical
  width there is now about 1.9 texels (a 1.4-texel radius; a disc emitter's 10 to 90 % width is 0.687 of the full width), which the
  release's reconstruction would also draw, being under 1.5 texels; and the filter's 1.5-texel floor draws about 1.8 texels (the twin's
  straight edges at the floor: 1.88 to 1.98). The edge-width number of the shadow code behind these is 2.3 mm on the release (1 cm times
  0.23) and 1.035 cm on this branch (4.5 cm times 0.23).
- **So on fine tiles the default head edge widens from about 0.4 to about 1.8 texels**: about 1 mm to 4.5 mm on a 2.5 mm tile, visibly
  softer than today's release. On a coarser tile the same 1.8 texels are wider in millimetres (about 9 mm at the model room's 5.1 mm
  texel on the 8192 atlas). Against the #527/#528 build (no reconstruction, a 1-texel floor) the step is from a 1-texel to a 1.5-texel
  floor.
- The floor is why the slider cannot bring the release's 0.4 texel back: at 0 a lamp's edge is the 1.5-texel floor as well. Where the
  default edge is already under the floor, as at this head on 2.5 mm and coarser tiles, the slider's left half (0 to 0.23) draws the same
  lamp edge; it still sharpens sun and moon shadows.

### Cost (`proj_edges_bench.py --time`: 2560 x 1440 floor, every pixel inside the lamp, 24 plate edges plus the avatar, softness 0.23)

| lamp, tile | release (the reconstruction, 1 cm lamp) ms | L1 ms | difference |
|---|---|---|---|
| fov 1.5, 2048 / 1024 / 512 | 0.1484 / 0.1450 / 0.1431 | 0.1095 / 0.1124 / 0.1202 | -26 / -22 / -16 % |
| fov 2.5, 2048 / 1024 / 512 | 0.2139 / 0.2129 / 0.2100 | 0.1785 / 0.2028 / 0.2431 | -17 / -5 / +16 % |

The fov 2.5 lamp on a 512 tile is dearer: the 1.5-texel floor on a coarse tile puts 5.6 % of the pixels in the penumbra (the release
lookup 0.9 %), and each runs the 32 filter gathers. The receiver's gather costs about 8 % of the lookup (P2 against L1 at fov 1.5, 2048:
0.1015 against 0.1095 ms, the same penumbra share). Against the owner's floor: four lamps each covering 60 % of a 1440p screen cost
about 0.26 to 0.58 ms on this card (the release lookup 0.34 to 0.51 ms). On a frame that is GPU-bound exactly at the floor that is
60 -> 59.1 to 58.0 fps standing and 25 -> 24.8 to 24.6 fps moving (the release lookup 58.8 to 58.2 and 24.8 to 24.7); against the
release lookup L1 costs -0.08 to +0.07 ms, +0.3 to -0.25 fps at 60. AMD and Intel are not measured.

### Limits

- **Contacts are wider than physics.** A contact's penumbra is narrower than the 1.5-texel floor; on a 2.5 mm tile a toe's edge is about
  4 mm (10-90 %) where physics has 0.4 mm. Its position is right (within 1 mm).
- **Big lamps on fine tiles are sharper than physics:** the 12-texel cap is 12 mm on a 1 mm tile, so above about 3 cm the slider's top
  depends on the tile there (the light-size audit's A5).
- **The umbra shortcut** (all 36 search texels blocked returns 0) can skip a hole of a lace within the filter disc: on a lace at 1 cm
  texels with the 4.5 cm lamp, three neighbour pairs in 36,000 differ by 0.27.
- **See-through casters** in L1 kept the old rule: glass was a dither (grain under the filter: the twin's gradient energy is 6 to 9
  times the truth's at 1 and 0.5 cm texels), lace takes the creator's cutoff on the mip-averaged mask (one solid shadow on coarse
  tiles). Lamp shadows L3 (below, "The light let through see-through casters") replaces the dither of blended faces in the fixed layer;
  mask faces keep the cutoff.
- **A moving caster** on a coarse tile moves in soft steps of the texel (L2 keeps the tiles at their lamp's size while the atlas has room).

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
| `lowres` | a resize (L2): the valve or the lamp's cone or reach changed the tile's size; its render draws the new size into a new rect and swaps it in (draw-then-swap; the name is kept for the stats) |
| `other` | the dirty list overflowed (65536 boxes in one frame), or a hook ran off the main thread: every tile is stale once |

Texture-coordinate and colour rebuilds (texture animation) never make a tile stale, nor does a request for the whole `REBUILD_GEOMETRY` set (`markForUpdate`, selection: it carries `POSITION` only because it rebuilds everything), nor a lamp's colour, intensity or falloff. `markForUpdate` also sets `mVolumeChanged`, so the prim's rebuild moves it in the octree once and that move pushes (selecting or deselecting a prim redraws the tiles that see it once). The rule is tested with the real `LLDrawable` flag values (`llprojectorpool::rebuildDirties`; `llshadowdirtyfeed.cpp` static-asserts that they match `lldrawable.h`). A tint and the material setters ask for `REBUILD_ALL`, which the rule counts; they are handled as texture-entry rebuilds (under "Lamps that scripts switch" below), so a tint, an emissive, a PBR colour or a material that leaves the silhouette alone pushes nothing at its request or at its rebuild, in a static linkset or an active one (since lamp shadows L3 the colour of a face the blend pass draws is part of the silhouette: it colours the light the face lets through, so its tint pushes; see "The light let through see-through casters"). A texture, material, bump, glow or colour change dirties tiles only when it changes the prim's silhouette key (`llprojectorpool::texturedDirties(was_key, now_key, known)`; what goes into the key is under "Lamps that scripts switch" below): a face turned opaque closes a hole the cached tile still has and an opaque face turned alpha cuts one, so both change the key and push. `was_key` is the object's `mPoolAlphaKey`, what the feed last saw; `noteTextured` and `noteDrawable` (every put, rebuild and edit of an object that can be a fixed caster) refresh it. The key is only kept while the feed tracks, so a bit, `POOL_ALPHA_KNOWN`, says it was written since tracking started (`setTracking(true)` clears it on every object in one walk of the object list, once per switch-on; an object that leaves the fixed layer loses it too): an unknown key makes `texturedDirties` push once, conservatively, and the object is known from then on. A prim with an alpha face that predates tracking (login with Stochastic, a switch from Legacy) and is made opaque by its first edit is therefore covered. A face whose material has alpha mode None or Emissive counts as opaque whatever its texture carries (it goes to the opaque pool and is not masked: `LLPipeline::getPoolTypeFromTE`), like the shadow pass. `LLPipeline::markTextured` callers differ in whether the hook sees the old state (`LLVOVolume::setTEColor` calls it before `LLPrimitive::setTEColor` applies the tint) or the new one (the `setTE*` and material setters, `changeTEImage`). A call that sees the old state finds the key it stored and pushes nothing, so the call after the change is the one that counts: `setTEColor` calls `markTextured` again after an alpha change, and that second call is required (its `REBUILD_VOLUME` pushes as well). An opaque prim's opaque-to-opaque texture swaps never push. Animated textures never do, on any face (a known limit for alpha faces: the tile keeps the silhouette of its last render). An alpha texture that finishes loading raises `REBUILD_VOLUME` in `LLFace::dirtyTexture` and so dirties; an opaque or still-unloaded texture does not. A texture of any component count that finishes loading also calls `markTextured` at the end of `LLFace::dirtyTexture`, which refreshes the key (the face counts as alpha only once its texture's components are known, and the channel count is in the key) and pushes when the key changed. A tint's alpha change raises `REBUILD_VOLUME` in `LLVOVolume::setTEColor` and dirties by itself. A 2-component (grey plus alpha) texture arriving later is covered by that `markTextured` call, although `dirtyTexture` raises the rebuild only for 4-component textures; the upstream condition is left as it is.

### The hooks, and the thread each runs on

All hooks push a box (the extents of what changed, agent space) with a cause into one double-buffered list
(`LLShadowDirtyFeed`). `LLProjectorShadows::update` swaps it before any render of the frame (a change a render itself causes lands
in the next frame), tests the boxes against the tile volumes by a flat scan of the tile volumes (`ChangeIndex`), and keeps the first cause per
tile. Every hook except the classifier is a no-op until the pool holds tiles (`setTracking`); the classifier always runs, so its
sticky bits are right when tiles appear. Every hook runs on the main thread; a hook that runs elsewhere changes nothing and flags
the next frame "everything is stale".

| Hook | File | Runs on | Pushes | Skipped |
|---|---|---|---|---|
| `LLSpatialPartition::put` (after the octree insert) | `llspatialpartition.cpp` | main (`LLDrawable::movePartition`, `LLPipeline::addObject`) | extents, cause `add_remove` | not fixed casters; inside the prim's own rebuild with the texture-entry mark (a move to another group), unless the silhouette key changed; in the bridge pass, a bridge on the quiet record |
| `LLSpatialPartition::remove` (before the removal) | `llspatialpartition.cpp` | main (`LLPipeline::unlinkDrawable`, `move`) | extents, `add_remove` | as `put` |
| `LLSpatialPartition::move`, in-group branch | `llspatialpartition.cpp` | main | extents after the update, `edit` | not fixed casters; inside the prim's own rebuild with the texture-entry mark, unless the silhouette key changed; in the bridge pass, a bridge on the quiet record |
| `LLPipeline::markRebuild(drawable, flag)` | `pipeline.cpp` | main (object updates, LOD, the pin) | extents when `rebuildDirties(flag)` (volume, rigged, position alone), `edit` | `TCOORD`, `COLOR`, `REBUILD_GEOMETRY`; a texture-entry setter's `REBUILD_ALL` (`TextureEntryRebuild`: it marks the object instead) |
| `LLPipeline::markTextured` | `pipeline.cpp` | main (the `setTE*` family, `markForUpdate`) | extents when the silhouette key changed (`texturedDirties`), or at the edge where a mover becomes active or static again, `edit` | not fixed casters; a change that leaves the key alone |
| `LLVOVolume::notifyMeshLoaded` | `llvovolume.cpp` | main (the mesh repository's callback, run from the main loop) | extents now, and again when the rebuild it waits for completes, `edit` | not fixed casters |
| `LLPipeline::markMoved` (`LLShadowDirtyFeed::noteMoved`) | `pipeline.cpp` | main | the extents it moves from, `edit`; a bridge marked moved by the own rebuild of a prim inside it (`LLSpatialBridge::move`) under that prim's rule, and on the quiet record when that keeps it quiet | not fixed casters; that own rebuild's push with the texture-entry mark, unless the prim's silhouette key changed |
| `LLPipeline::updateGeom` (a delayed volume or rigged rebuild, or a mesh that loaded, completes; `LLShadowDirtyFeed::OwnRebuild`) | `pipeline.cpp` | main (`display`) | extents, `edit`; the texture-entry mark ends | not fixed casters; with the texture-entry mark, unless the silhouette key changed |
| `LLPipeline::updateGeom`'s bridge pass (`updateMovedList(mMovedBridge)`, after the rebuilds; `LLShadowDirtyFeed::BridgeMoves`) | `pipeline.cpp` | main (`display`) | the bridges' moves reach the partition hooks above; the quiet record ends | a live bridge on the quiet record |
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
the former +5 % allowance (an assumed 8 ms frame, never measured). P4 has one (`llprojectorshadowpool.h`, `selectFrame`). Lamp shadows
L2 expresses it against the owner's frame-rate floor (AGENTS.md, since 2026-11-16: about 60 fps standing in a crowded room, never below
25 fps moving), which replaced the +5 % rule: the shadow caches' tile work may take 3 % of the standing floor's frame (`kFloorShare`,
`kFloorStandingFps`). **The 3 % is an engineering choice, not derived:** the lamp-shadow study planned to set the budget from stage L0
(the owner's rooms measured), which has not run. It is 43 % more CPU a frame than the 0.35 ms before, unmeasured in-world; whether it
stays 0.5 ms or goes back to 0.35 ms until L0 is the owner's call.

| | ms of CPU a frame | meaning |
|---|---|---|
| `kFrameCapMs` | **0.5** (was 0.35) | everything the shadow caches' tile work spends in a frame, all caches together: 0.03 x 1000 / 60 fps. Spent in full by a frame that is CPU-bound exactly at the floor it takes that frame below the floor, 60 -> 58.3 fps standing (16.67 -> 17.17 ms) and 25 -> 24.7 fps moving: the floor holds only with that much headroom elsewhere. A static scene spends nothing, and a GPU-bound frame hides it |
| `kDynamicCapMs` | **0.30** (was 0.20) | the dynamic layer's share while a static tile waits, spent first (a moving lamp's tile, the avatar layers). When no static tile waits, the share is the whole remaining cap (`dynamicShareMs`; hotfix C1) |
| `kStaticCapMs` | **0.20** (was 0.15) | what the static tiles keep when the dynamic layer takes its 0.30 ms (`kFrameCapMs - kDynamicCapMs`); with no dynamic work they get all 0.5 |

- **Priority: the dynamic layer first.** An avatar or a moving lamp that lags is seen at once; a dirty static tile shows a stale
  shadow of something that changed, and its wait is bounded by the age order (oldest first). A static tile is never starved: the first
  static candidate has a place every frame when it fits the cap (the dynamic layer gives up its last faces for it); one whose own
  prediction is above the whole cap runs alone as the progress pick once it has waited 4 frames (or was never drawn), and that frame's
  dynamic picks are dropped, so the overrun is that one item and a standing avatar cannot starve it.
- **The sum of the predicted costs of a frame's picks never exceeds the cap**, except for the one progress pick: when nothing was spent
  yet in the frame, a single item whose own prediction is above the cap runs alone. Two caches share the cap through the frame ledger
  (`frameLedger()`): the second takes what the first left, and a frame in which the cap is spent serves nothing more.
- **A_max covers the static tiles too.** A static tile that draws avatars (a first fill, a dirty or low-res tile) is charged `min(avatars, A_max)` draws (`StaticItem::avatar_draws`) against what the dynamic picks left of A_max; one that does not fit waits (the order is kept), and the head tile keeps its place (the dynamic layer gives up its last avatar faces for it, only when the head is served; so for a cold fill of avatar-heavy tiles the dynamic avatar faces yield to the head tile), so a cold fill never draws more than A_max avatars in a frame.
- **In the frame** each render also checks the measured time against the cap (the dynamic list against its share: 0.30 ms while a static tile waits, the whole remaining cap when none does).
- **Cold start** (`coldItemMs`) is at most a quarter of the cap (0.125 ms): four first fills a frame with no avatar layer, one a frame
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

- **while a static tile waits** it is 0.30 ms: with a 0.05 ms face that is **6 faces a frame** (4 before L2);
- **when none waits** it is the whole remaining cap, 0.5 ms: up to **10 faces a frame** at the 0.05 ms floor (7 before L2).

A_max does not bind below 24 faces. The wait of the last face of a set is `ceil(faces / per frame)` frames. The order of service is:
the faces of the user's own avatar first, then the faces past their deadline (the most overdue first, the safety net), then the camera
POSITION's distance to the face's nearest other avatar (not the lamp's distance, which ties at 0 indoors), then the oldest. The prediction (`lastAvatarMs`) is an EMA
(0.25) seeded by the first sample, each sample clamped to 1 ms (2 x the cap).

Faces drawn a frame / worst gap in frames, indoors, one avatar under all lamps, no static tile waiting (a model of `update()`,
`llprojectorshadowpool_test --table`; "before C1" is the release scheduler before C1, with its fixed 0.20 ms share of 0.35 ms; "C1" the
scheduler with the 0.35 ms cap; "L2" with the floor's 0.5 ms):

| lamps | cost per face (ms) | before C1 | C1 | L2 |
|---|---|---|---|---|
| 1 | 0.30 | 0.25 / 4 | 1.00 / 1 | 1.00 / 1 |
| 2 | 0.15 | 1.00 / 4 | 2.00 / 1 | 2.00 / 1 |
| 3 | 0.07 | 2.00 / 4 | 3.00 / 1 | 3.00 / 1 |
| 3 | 0.15 | 1.00 / 4 | 2.00 / 2 | 3.00 / 1 |
| 4 | 0.10 | 2.00 / 4 | 3.00 / 2 | 4.00 / 1 |
| 5 | 0.07 | 2.00 / 4 | 5.00 / 1 | 5.00 / 1 |
| 5 | 0.15 | 1.00 / 5 | 2.00 / 3 | 3.00 / 2 |

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

## Tile resolution: cells of a wide lamp, sizes from the lamp and the pressure valve (lamp shadows L2)

Owner report 2026-11-16: a projector's shadow on the floor had stepped edges, the step being the tile's texel; the tile-resolution PR
gave wide lamps cells and sized every tile by the camera position's distance to it. Owner report 2026-11-18: hard squares that grow as
the camera backs away, and the owner's own shadow stepping while walking. The camera-sized tiles made the picture depend on where the
camera stood and resized tiles 7 to 24 times a second while walking (the earlier sizing model), each resize showing a nearest-stretched
tile until its redraw. L2 sizes every tile from its lamp, lets the camera's position decide only who shrinks first when the atlas is over
its budget, and draws a new size before it is shown. Rules kept: OpenGL 4.6, no compute; a tile's image depends on the lamp, the scene
and its size only (the camera's direction enters nowhere); Legacy and the sun are untouched; no Preferences entry (the constants are in
`llprojectorshadowpool.h`). The design files are on the owner's machine (`reports/projector-edges-2026-11-16/design.md`,
`reports/lamp-shadow-method-2026-11-18`): the rules and constants they motivate are inline here and in the header.

### Cells (wide lamps)

- A lamp wider than about 93 degrees (tan of the half-angle above 1.05) is split over its own image plane into `gu x gv` cells (`gridOf`, per axis: 1 up to tan = 1.05, then
  `ceil(tan - 0.05)`, at most 4). A cell is a tile of its own: an off-axis sub-frustum with the lamp's projection centre, near and far
  planes (`glm::frustum` over the cell's tangent rectangle, `tile_proj`), so a cell's depth is the lamp's depth and the lookup keeps the
  lamp's matrix. A lamp up to about 93 degrees keeps one tile, no guard and its old projection: its tile is drawn as before.
- **Guard.** Every cell's frustum is widened by `kCellGuard` = 16 texels of its interior on each side (`cellTanRect`), so the filter's taps
  at a seam read the neighbour's content (the filter's largest radius is 12 texels). A tile of `Nc` texels has an interior of
  `Ni = Nc - 32` texels. The lamp's own border has no neighbour: the guard there is the lamp's frustum widened (real geometry).
- **Lookup** (`shadowStochasticUtil.glsl`, `stCellPick` in the G1 block): the receiver's lamp-image position `c` (after the normal offset)
  picks the cell `floor(c * g)`, the tile uv is `((c g - cell) Ni + G) / Nc`, the texel size, the bias and the penumbra's scale use the
  virtual resolution `g Ni` (what one tile of the same texels would have), and the receiver plane's gradient is divided by `g Ni / Nc`.
  Every tap reads that cell's tile. A cell without a drawn tile (`Nc` 0) lights without a shadow, like a lamp without one. The cell's
  guard must cover the filter's widest tap plus the bilinear footprint: `static_assert(kCellGuard >= kProjectorLamp.kernel_max + 2)`.
  `stSpotQuadTile` and `stSpotFilterTile` take the tile as a parameter (`vec4`: atlas origin, size, 1 / atlas), and the filter's cap is
  a parameter too (a point-light face will pass its own); the atlas stays the uniform; G1 stays max abs 0.
- **Uniforms**: `proj_shadow_grid` (gu, gv, G; 1, 1, 0 for one tile) and `proj_shadow_cells[16]` (x, y, Nc, Ni; index = row * 4 +
  column), set per lamp in `setupLight`. The lookup matrix is the lamp's, built from the pose of the most recently drawn cell; a cell drawn
  from another pose (the lamp moved since) reads as not drawn until it is drawn again, so a lookup never mixes two poses.
- **One record per tile.** `Record` is a tile: key = lamp << 4 | cell index, so the scheduler, the change index, the LOD pins, the layers
  and the stats work per cell unchanged. A cell's change volume (`Record::volume`) is its widened frustum at its current size (an object
  that only a neighbour's guard holds dirties both cells); `Record::core` is its interior, for the camera distance and the own avatar.
- **A lamp whose cone crosses the split** (or whose aspect moves a cell boundary) gets a new grid (light-size study N3). Its old tiles
  retire under the lamp's old id (`mRetiringOf`) and keep serving the lookup, as pose-stale tiles always do, until every tile of the new
  grid is drawn; then they go (draw-then-swap; before L2 they were dropped and the lamp was lit without a shadow until the new cells were
  drawn). A retiring tile is not drawn, resized or tested against the scene's changes, and counts at its size in the valve. A second grid
  change while the new set is incomplete drops that set and keeps the complete old one; a lamp switched off drops its retiring tiles.
  When the tiles do not fit the atlas even at the valve's ceiling (a shortfall no render frees, below), every lamp's retiring tiles go at
  once: their space is what the new cells need, so without that a new cell that never finds room would keep them for as long as the lamp
  is in range (audit finding 3).

### Sizes from the lamp

- Every tile's size comes from its lamp alone (`tileBaseSize`): its interior spans `tan_w * d_ref` metres at the reference depth
  `d_ref = min(1.5 r, 4 m)` and wants 4 mm texels there, plus the cell's two guards; a power of two in `[128, 2048]`. `tan_w` is the
  tile's wider axis as a span of tangents (`2 tan(fov / 2)` times the aspect on the wider axis for one tile, its share of that for a
  cell). It is computed with the tile's volume, only when the lamp's pose changes (cone, aspect, reach, grid). P2's `baseSize` was its
  one-tile, square case (now a twin in the unit test). The per-tile size request and its hysteresis are gone from the frame loop
  (`SizeHysteresis`, `cellSizeRequest` and their constants); what each tile still does every frame is one distance to its volume and one
  band test (comparisons, no logarithm).
- **While the atlas has room** (the tiles' demand at most 0.60 of the atlas) every tile keeps its lamp's size, whatever the camera does:
  walking and orbiting change no size.

### The pressure valve (over the budget)

- Each tile sits in a **distance band**: the camera POSITION's distance to the tile's interior volume (`pyramidDistance`, a lower bound;
  never the view direction), under 8 m, 8 to 16, 16 to 32, 32 to 64, 64 m and beyond (`bandOf`). A band changes only half an octave past
  its bound (`nextBand`), so a camera standing at a bound changes nothing.
- A **pressure** P shifts the tiles of band b down by `clamp(P - (4 - b), 0, 5)` octaves, never below 64 (`valveShift`): one step of P
  halves the farthest shrinking band once more and starts the next nearer one, so a tile one band farther ends one octave coarser, its
  texel growing with its distance as the screen's does. P rises at once (as many steps as it takes to be at or below 0.60) and falls one
  step after 3 s at or below 0.45: the hysteresis of the old global level, which is the case of every tile in the farthest band
  (`LevelController`, the same class). P never passes the point where every tile is at its floor (`pressureCeiling`), and a step down
  skips the steps that change nothing (a band with no tile), so no hold is spent on them.
- **P follows the demand alone.** An arriving tile that finds no room, even after a compaction, waits for the shrinks being drawn and
  moves no pressure. Until its render a shrinking tile holds its old rect, so a lamp switched on near the camera while farther tiles fill
  the atlas finds the room only once their shrinks are drawn. The room is always there once they are: the free texels plus what the
  pending shrinks give back exceed the waiting tiles by A² (1 - U) plus the growths not drawn yet, at least 0.4 of the atlas below the
  ceiling (`arrivalShortfall`). Before this fix a waiting arrival stepped the pressure up the next frame, and the 0.45 rule then kept it
  there: the review's replay (38 far tiles, a near lamp of a 2048 and three 1024 tiles) ended at P 5 for good where the rule gives 4, the
  near tiles an octave coarser; now it ends at 4 after the arrivals waited 1 to 2 frames (`testPlacement`). A shortfall that no render
  frees exists only at the ceiling (U > 1: the tiles do not fit even at their floors); then the lamps' retiring tiles go (N3, above).
- **Why the near band starts at 8 m:** within 8 m a lamp's 4 mm texel is about half a pixel of a 1440p, 60 degree view (0.8 mm a metre)
  or coarser, so every near tile matters alike: a room is one band and shrinks evenly, with no size changes as the camera walks in it.
  Measured with the sizing model (below): bands from 2 m changed 7.9 tile sizes a second in the model room and 28 in the club, from 8 m
  0.23 and 7.6, with the same floor texel in the room and a finer one in the club; a 1 s hold on band changes bought only 7.6 -> 7.0 in
  the club and is not built.
- A lamp that is off (S2, below) and a lamp's retiring tiles count at their sizes; the valve does not size them.

### Draw-then-swap

- A tile whose wanted size differs from its drawn one is resized by its own render (`renderTile`): its rect is freed, the new size is
  allocated and drawn in the same call, and the lookup reads it only afterwards (the lighting pass runs after the frame's tile work), so
  it never shows a stretched or half-drawn tile. Nothing else can take the block in between, and the old texels are never read again.
- A shrink always finds a block (its own); a growth waits until `BuddyAtlas::largestAfterFree` (the block it would get once its own rect
  is freed and merged) holds the new size (`resizeDue`). A resize is a static item (cause `lowres`), under the frame budget like any.
  The frame's picks are classified before any render, so several growths can be picked for one free block: the first takes it, and a
  pick whose only cause was its growth and whose block is gone is not drawn (its avatar layer still is), instead of a full render of an
  unchanged tile (audit finding 4).
- A repack is now only a **compaction** (`compact`): every placed tile at its own size in a fresh layout (descending sizes pack a buddy
  atlas without a gap, so what fitted fits again), the drawn ones moved by bit-exact copies, then L to S in full. After one, the free
  texels are held in at most three blocks of each size, so any arrival set or growth the free texels hold has its blocks
  (`growthBlocked`; property-tested on fragmented atlases). It runs when an arriving tile finds no block although the free texels would
  hold it, and when a growth has found no block for `kGrowHoldSeconds` (3 s, `GrowthHold`) although the free texels alone would hold its
  new size: fragmentation that tiles of mixed sizes leave as they come, go and are resized. Without that a growth could wait for good in a
  static scene (audit finding 2, review Q1): a replay that leaves one tile in each quadrant of a 4096 atlas kept a 2048 lamp at 512 for
  the whole minute with 97 % of the atlas free; with it the lamp grows after 3 s and one compaction (`testPlacement`). The case is rare:
  in a scratch replay with the same rules, random walks past 400 lamps, 600 random burst-and-leave sessions, 6,000 bursts into a nearly
  full atlas and a dense scene of 2,466 tiles left behind never left a growth blocked (the allocator's smallest-fit, lowest-address
  choice keeps the tiles together; the constructed case keeps one tile in every quadrant). A compaction copies the whole atlas (its GPU
  cost is not measured, see "Not verified"), hence the hold: one at most every 3 s, and only while a growth is blocked.

### Lamps that scripts switch (lamp shadows S2)

- A projector that a script switches off, turns black or gives a radius of 0 (blinkers and strobes toggle every 0.03 to 1 s, a broken
  tube sleeps up to 2 s) keeps its tiles for `kOffHoldSeconds` = 5 s (`holdOff`): back on in that time it is shadowed at once with no
  redraw, since a tile depends on neither colour nor intensity. While off its tiles are not drawn or resized, keep their atlas space and
  keep collecting the scene's changes (their last pose and volume stay); after the hold they and their LOD pins go like a lamp out of
  range (a day and night switch). Before L2 such a lamp lost its tiles at once and was lit without a shadow until redrawn.
- **The change feed ignores what cannot change a silhouette.** A blinker toggles `PRIM_FULLBRIGHT` and `PRIM_GLOW` with its light; on a
  prim with any alpha face that used to push its extents and redraw every tile that saw it at the blink rate. The feed now keeps a
  **silhouette key** per prim (`LLViewerObject::mPoolAlphaKey`, in place of the `POOL_ALPHA_FACE` bit): which faces are blended, masked or
  alpha-textured and, for each, its alpha, texture and channel count, mapping (repeats, offset, rotation, planar) and alpha mode
  (legacy or glTF, with the base colour texture and its transform); and which glTF faces are double-sided, since the shadow pass draws
  their back faces too (`lldrawpool.cpp` disables the cull for them). `noteTextured` pushes only when the key changes
  (`llprojectorpool::texturedDirties(was_key, now_key, known)`); fullbright, glow, shine, bump, emissive, the RGB of an opaque or
  alpha-masked face and the light's own colour never do. A face turned opaque or alpha, a new alpha texture, a moved mask or a material
  turned double-sided still push. Every caller is a main-thread hook, as before.
- **Since lamp shadows L3 the colour of a blended face is a silhouette input.** The light-through layer colours a lamp's light by the
  vertex colour of the faces the blend pass draws (`LLPipeline::getPoolTypeFromTE` gives the alpha pool: the face's tint, or a glTF
  material's base colour, the bytes `LLFace` writes), so the key carries those three bytes (`SilhouetteFace::tint`, `rgb`): a red pane
  recoloured blue, by an edit, a script's `PRIM_COLOR` or a glTF override, redraws the tiles that see it. The trade-off against S2: a
  blinker that toggles the colour of a blended face (not of an opaque or masked one) pushes once a toggle (the rebuild's first own push
  finds the new key: `testTextureEntryRebuilds`, 600 pushes in a minute of 0.1 s toggles, static or in an active linkset), and its
  tiles are redrawn at the blink rate, within the frame caps. A tint of an opaque or masked face and every other blinker input push
  nothing, as before.
- **Texture-entry rebuilds.** A tint (`LLVOVolume::setTEColor`'s `dirtyMesh`) and the material setters (`setTEMaterialID`,
  `setTEMaterialParams` and `setTEGLTFMaterialOverride`, which carries a script's emissive and PBR colour) ask for `REBUILD_ALL`: the
  renderer rebuilds the whole prim, not because its shape moved. `rebuildDirties` counts that, so a blinker that toggles a colour or an
  emissive with its light pushed three times per toggle: at the request, at the rebuild's octree move (`LLVOVolume::genBBoxes` ->
  `movePartition`) and at the rebuild's completion. Now each of those setters wraps its request in `LLShadowDirtyFeed::TextureEntryRebuild`:
  the request pushes nothing and marks the object (`POOL_TE_REBUILD`); the rebuild's own pushes, inside `LLShadowDirtyFeed::OwnRebuild`
  in `LLPipeline::updateGeom`, then go out only when the silhouette key changed. A silhouette change pushes once, at the setter's
  `markTextured`, which stores the new key, whether that call runs before the setter's scope opens (`LLViewerObject::setTEMaterialParams`'
  normal and specular maps, a cached material's synchronous callback in `setTEMaterialID`) or inside it; the rebuild's own pushes find
  the key stored. One push is enough: the setters run in idle (or in the UI's events), before the same frame's `updateGeom`, which
  completes every prim's rebuild, and the pool takes the frame's boxes after it, in `generateSunShadow`, so a tile drawn for the push
  draws the faces in their new passes. The mark is not set when a rebuild with a shape flag was already queued; it ends at any other
  push (a move, a put or a remove, a counted rebuild, a mesh arriving), at any request with a shape flag (a new LOD, a scale, rigging,
  `markForUpdate`) and when the rebuild completes (`llprojectorpool::textureEntryMarkAfterRequest`, `textureEntryMarkAfterRebuild`,
  `ownRebuildPushes`). `REBUILD_MATERIAL` from `LLDrawable::updateTexture`, which `LLPipeline::updateMove` raises later in the same idle
  for every `markTextured` of the setters, keeps it.
- **Blinkers in an active linkset.** A blinker becomes active after its ninth colour or material update from the server
  (`LLVOVolume::onDrawableUpdateFromServer`) and stays so; its prims then live in the octree of the linkset's bridge (`LLSpatialBridge`).
  A prim's octree move there marks the bridge moved (`LLSpatialBridge::move` -> `LLPipeline::markMoved`), and `updateGeom` moves the
  bridge in the region's octree after the rebuilds (its bridge pass, `updateMovedList(mMovedBridge)`): two more pushes of the linkset's
  box per toggle, and the first ended the root's own mark, so a root lamp's completion pushed as well (the follow-up review's B1:
  two pushes a toggle for a child lamp, three for a root). The `markMoved` hook, `LLShadowDirtyFeed::noteMoved`, now takes a
  bridge's `markMoved` during the own rebuild of a prim inside it as that rebuild's push, under the prim's mark and key (the box is the
  bridge's either way; the root's mark is left alone), and puts the bridge on a quiet record when that push stays quiet; in the bridge
  pass (`LLShadowDirtyFeed::BridgeMoves`) a live bridge on the record moves without a push. The record holds only while every
  `markMoved` of the bridge since its last move was such a quiet push: the linkset's own move (`LLDrawable::updateXform`, in idle before
  the rebuilds), `updatePartition` or another prim's move takes the bridge off (`llprojectorpool::quietBridgeAfterMove`), and the record
  ends with the pass. Only the prim's own drawable moves in its rebuild (`LLVOVolume::updateGeometry`), so a bridge's `markMoved` inside
  that rebuild is the prim's.
- Threads: the setters, the hooks and `updateGeom` run on the main thread; the scopes do nothing off it (the request is then counted as
  before), and they and the record only compare drawable pointers (a recorded bridge is held by the pipeline's moved list until the pass).
- What remains is once per prim or per camera crossing, not per toggle: a prim's first texture or colour change re-bins it in the octree
  (`shrinkWrap`), and its ninth update from the server makes it active, which rebuilds it; each pushes once. After `shrinkWrap` the
  prim's octree bin also follows the camera's distance (`LLVOVolume::getBinRadius` shrink-wraps only within 64 m,
  `OctreeAlphaDistanceFactor`), so `LLVOVolume::updateLOD` re-bins it, and the partition's move pushes once, when the camera has crossed
  64 m from it (at its next distance update, while it is in view); so does any prim with a blended face that cannot be drawn as a mask.
  Those pushes come from the P3 partition hooks, predate S2 and redraw tiles in which nothing changed (a known limit; the in-world `edit`
  causes while walking show them).
- Light colour and intensity changes from the server reach no tile (`LLVOVolume::parameterChanged` calls no hook); the reach (radius)
  changes the tile's frustum, so a change of more than 1 cm redraws it, but a radius of 0 is "off" and holds the tiles.

### Stats

`Stats` keeps its fields and names (the stats API's keys); `resizes` counts size changes (draw-then-swap), `resizeRepacks` the
compactions (arrivals and blocked growths), `resizeDeferred` the tile-frames a growth waited for a block (once a frame per waiting tile,
whether the atlas was tight or fragmented), and `level` is the valve's pressure. The owner's walk reads: tile sizes against the camera
distance (nothing should change while the atlas has room), resizes per second, compactions per second, the CPU per tile render, and the
fps standing (60) and moving (25).

### Gates

| gate | how |
|---|---|
| rules | `llprojectorshadowpool_test` (`testCells`, `testScriptLamps`, `testTextureEntryRebuilds`, `testPlacement`): the grid table, the cells partition the lamp's pyramid, a widened pyramid holds its interior, an object in a neighbour's guard dirties both cells, `pyramidDistance`; the size from the lamp (the guard term, bad input); the bands and their hysteresis; the valve (no size change in a minute of random bands while the atlas has room; the far tiles shrink first and never end larger than a near one; the pressure falls back; the ceiling; no hold on steps that change nothing; fixed tiles; the old level is the all-far case); `largestAfterFree`; the hold of a switched-off lamp (`holdOffFrame`: blinkers keep their tiles, a long switch gives them back once); the silhouette key's inputs (`silhouetteFaceMix`: each moves the key, an opaque face only by being double-sided, an opaque single-sided one not at all, the RGB of a face the blend pass draws since L3 and of no other face); the texture-entry mark and the quiet record over the hooks' order (a tint, emissive or material blinker pushes nothing in a minute of 0.1 s toggles, static or as the child or the root of an active linkset (L3: a tint blinker on a blended face pushes once a toggle), where the wiring before the follow-up review's B1 pushed 1,200 and 1,800 times; a silhouette change pushes once, wherever the setter's `markTextured` runs; a new LOD, a scale, `markForUpdate`, a move around a tint and the linkset's own move still push; two quiet prims of one linkset keep the record, a counted move before or after ends it; each of six mutations of these rules fails a check); placement, by a GL-free replay of `layout()` and the resize built from the same rules: a lamp switched on near the camera while far tiles hold the atlas waits for their shrinks and the pressure ends at the rule's (review B1), a shortfall only at the ceiling, after a compaction every arrival set and growth the free texels hold fits (random fragmented atlases), a growth blocked by fragmentation grows after the hold with one compaction (Q1). The light set's bookkeeping around the hold and the retiring cells (records marked seen, skipped by the scheduler, counted as fixed) is the GL class's and has no unit test |
| sizing model | `scripts/shadows/projector_sizing_model.py` (CPU): the rules written out, a 60 s walk in the model room (four wide lamps), a room whose lamps fit and the club, on the 4096 and 8192 atlases; gates: the size changes a second within the scene's limit (room 0.5, club 10, the room that fits 0; bands from 2 m fail it, 8.8 a second in the room and 21 and 31 in the club on a 20 s walk), and no tile more than twice as far as another of the same lamp size is larger (an inverted band order fails it). No allocator: placement is the unit test's |
| G1 | `projector_pool_g1.py` (tile in the atlas against the standalone map, max abs 0) |
| cells | `scripts/shadows/projector_cells_check.py [--grid n,n --ni N --time]`: the production `stCellPick` and `stSpotFilterTile` through cells against one virtual tile of the same texel grid, receivers over the whole lamp image, seams included; an undrawn cell is reported; with `--time` the cost of the pick |
| Legacy, sun | `check_legacy_glsl.py`, `check_legacy_shadows.py` and their tests; `stSpotFilterTile` is reached only through `sampleProjectorShadow`, the sun programs call `sampleSpotShadow` |

Sizing model (2026-11-18, 60 s walk at 1.4 m/s; the floor texel under the camera from the tile's interior, which the earlier model took as
the whole tile):

| scene | atlas | tiles | demand at the lamps' sizes | pressure | size changes a second (gate) | farther-and-larger pairs | floor texel under the camera, median / p90 |
|---|---|---|---|---|---|---|---|
| room, four wide lamps (fov 2.5, 3 x 3 cells) and four wall spots | 4096 | 40 | 9.25 | 6 | 0.23 (0.5) | 0 | 10.4 / 10.4 mm |
| same | 8192 | 40 | 2.31 | 5 | 0.23 (0.5) | 0 | 5.1 / 5.1 mm |
| room whose lamps fit | 4096 / 8192 | 8 | 0.50 / 0.12 | 0 | 0 (0) | 0 | 3.3 / 3.3 mm |
| club, 20 lamps (fov 1.2 to 3.0) | 4096 | 193 | 43.4 | 7 | 7.5 (10) | 0 | 40.2 / 72.4 mm |
| same | 8192 | 193 | 10.8 | 6 | 7.6 (10) | 0 | 18.7 / 33.8 mm |

The model room is over the budget all the time (pressure 6 on 4096, 5 on 8192): its 0.23 changes a second are under pressure, as the
camera crosses a band bound, so the owner's walk at home should expect an occasional size change, not none.

The camera-sized tiles before L2 (the earlier model, without the guard correction, so optimistic): room 6.67 changes a second and 4.9 mm
(8192) / 9.8 mm (4096); club 24.5 a second and 17.6 mm (8192) / 35.1 mm (4096). With lamp-sized tiles a room of wide lamps is over the
budget on either atlas, so its tiles are shrunk evenly by the valve; only a farther band shrinks first.

### Not verified (this section)

In-world look and the real CPU cost of a tile render, a resize and a compaction; the owner's rooms (their lamps' cones and reaches, which
L0 of the study measures); AMD and Intel; the seam between two cells of different sizes (a shift of the contour of at most 0.07 texel of
the coarser in the offline model, not measured on this implementation); a wide lamp that moves (its cells are drawn over several frames:
a cell from the older pose is lit without a shadow until redrawn, only for lamps wider than about 93 degrees that move, rotate or are
edited); the own avatar's guard-only neighbour cell lagging by about 2 frames at a seam; the retiring cells of a lamp whose cone crosses
the split and the off-hold's bookkeeping in the light set (no unit test: the logic is in the GL class; their rules are tested); the
wiring of the texture-entry mark and the quiet record to the feed's hooks (`TextureEntryRebuild`, `OwnRebuild`, `noteMoved`,
`BridgeMoves`; the rules are tested in the hooks' order, the hooks themselves need the viewer: in-world, a `PRIM_COLOR` or emissive
blinker should add no `edit` cause to `pool.rerender_causes`, also after more than 9 toggles, when its linkset is active, both as a
single-prim lamp, the root, and as a child prim); the GPU
cost of a compaction (a whole-atlas copy: one at most every 3 s for a blocked growth, and one for an arrival that finds no block); the
CPU of the per-frame valve work in the viewer's "projector sweep" and "projector layout" zones (offline, on contiguous arrays with the
same rules: the band test, the valve's ceiling and demand passes, the growth check and `resizeDue` cost about 0.02 ms a frame for 2,466
tiles and 0.002 ms for 193; the viewer walks a map of large records, so expect more), and the silhouette key's hashing on every rebuild
of a prim (offline about 18 ns per alpha face).

## The light let through see-through casters (lamp shadows L3)

Owner decision D11 (a light-let-through layer in lamp tiles), with D4 (a) coloured. Plan: `reports/light-through-l3-2026-11-18/plan.md`
(kept on the owner's machine). Before L3 a lamp shadow was all or nothing: a blended caster below 0.98 alpha was a hashed per-texel
dither in the depth tile, frozen grain in a cached tile with no history to average it, and stacked sheers passed the most opaque layer's
light instead of the product. L3 lets light through such casters in proportion and coloured by them, deterministically.

### The method

Beside each tile's depth, a second cached picture: per texel, the share of the lamp's light the see-through casters in front of the
tile's opaque depth let through, per colour channel (tau), and the depth of the nearest of them (zt). The lookup multiplies the depth's
visibility by it. This is the single-layer coloured translucency map (Gosselin et al. 2004; Filion & McNaughton 2008, StarCraft II;
Crytek 2011): deterministic, so a cached tile holds no grain, camera-independent, and cheap. Stacked casters multiply, in any order (the
blend is a product). Rejected: coloured stochastic shadow maps (McGuire & Enderton 2011) and the Aurora Viewer's caster rule freeze grain
into tiles that have no history; deep shadow maps, opacity and Fourier opacity maps, moment-based and adaptive volumetric maps are built
for smoke and hair and ring or blur on thin sheets (`research_notes/Shadow methods for avatars and many lights/transmission.md`, kept on
the owner's machine). The known limit of one stored depth: a receiver between two see-through casters gets both.

**The light through one face** (`lllightthrough.h`, `transmittance`): `tau = (1 - a)(1 + a C)` per channel, `a` the face's opacity (texture
alpha times face alpha), `C` its linear colour (classic: texture times face colour in sRGB, then linear, as `alphaF.glsl`; glTF: the base
colour texture in linear times the factor, as `pbralphaF.glsl`). It is McGuire & Enderton's `rho = a (1 - t)` with `t = C (1 - a)`, the
lowest-order `t` that keeps a faint face a pure filter of its colour and an `a = 1` face opaque, as SL draws it. It lies between `1 - a`
(a black or neutral face) and `1 - a^2` (white): a red pane at 0.6 lets (0.64, 0.40, 0.40) through, clear glass at 0.3 lets 0.91. A texel
at 0.98 or more stays solid in the depth (the 2026-11-14 decision), where the layer would pass under 0.04: the switch cannot be seen. A
face at or below 1/255 is left out (invisible), so it records no depth either.

### The layer (`lllightthroughgl.h`, `LLLightThroughLayer`)

- **Textures.** RGBA8 tau (LINEAR) and R32F zt (NEAREST), at the pool's atlas edge and with its texels (a tile's rect is the same in
  both), counted in the viewer's texture memory: 32 MiB at the 2048 atlas, 128 MiB at 4096, 512 MiB at 8192 (as much as the pool's two
  depth atlases; the 8192 tier is only chosen from 11 GB of dedicated video memory). With its layer the pool counts twice its atlases in
  the texture budget: at 8192 1 GiB, about 21 % of a 12 GB card's 4.9 GB budget (10 % of a 24 GB card's), at 4096 256 MiB, 8 % of an
  8 GB card's 3.2 GB (watch the texture discard bias in-world, as for the 8192 tier above). The first build's 16-bit depth took a quarter
  less; the exact depth is the reason for the difference (below). Made the first time a tile meets a see-through
  caster (`ensure`, from `LLPipeline::renderShadow` inside the tile's static render); freed with the atlases (`freeAtlases`: `release` and
  a failed `allocateAtlases`, before S, its framebuffer's depth attachment), and once no tile has held a see-through caster for 30 s
  (`kThroughIdleSeconds`, checked once a second in `update` while the layer exists: the lamps' glass is gone, or the debug setting was
  turned off and every tile drawn again; a tile of a lamp that is off keeps its flag, so its layer stays). OpenGL 4.4 (`glClearTexImage`);
  below it, or when the textures cannot be made (logged once, not tried again until the atlases are made again), the tile's blend faces
  dither into the depth as before.
- **The depth as it is.** zt holds the caster's window depth in 32-bit float (`gl_FragCoord.z`, no conversion, no rounding), as the depth
  atlas does (D32F): a receiver is behind a pane by the depth lookup's own test (its depth less one texel of bias, lowered by the receiver
  plane: `stThroughRef`). The first L3 build stored 16 bits of window depth one step farther, which left a receiver up to two steps
  behind a pane untinted; with the near plane at the prim's face that band was about 9 cm at 10 m and 20 cm at 15 m for a 5 cm prim (and
  60 cm at 20 m for a point light's 2 cm near plane): a sill or a floor right behind stained glass got white light in a strip along it.
- **Depth-tested against the tile's opaque casters.** The layer's framebuffer has the pool's static depth atlas S as its depth
  attachment, read only (`GL_LEQUAL`, no depth writes): a see-through caster is recorded only where it lies in front of the nearest opaque
  caster of its texel. A pane hidden behind a wall or a floor (glass in the room below, a window behind a wall, blended leaves outside)
  tints nothing in front of it. The first build drew every caster whatever lay in front of it, so the floor under a lamp's glass globe
  (or under any pane in front of it) got the product of every blended face down to the far plane (the twin's hidden pane: the floor got
  0.40 / 0.25 / 0.25 where the truth is 0.625; now 0.624, within the 8 bits). The frame of a window is handled in the lookup (the open
  weight, below), so this costs the frame's penumbra nothing.
- **Its own face rule.** The caster pass culls back faces (`DrawScope`'s `LLGLEnable(GL_CULL_FACE)`), whatever the caller's depth pass
  does: a one-sided face lets the light through once, from the side a camera at the lamp would see it, as it shows its alpha once to the
  viewer (a thin glass box prim filters the light once, not twice); a double-sided glTF material is drawn from both sides by its own batch
  (`LLRenderPass::pushGLTFBatch` disables the cull for it). The projector's depth pass culls back faces too; the point-light faces' depth
  pass draws both sides (L4), and the rule keeps a glass box from filtering their light twice.
- **Drawing** (`renderTile`): the tile's rect is cleared to "no caster" (`clearRect`, `glClearTexSubImage`), then the pool names the layer
  and the rect (`LLLocalShadowGL::setThroughTarget`) and `renderShadow` runs. There, when the static cull holds a blend face that is not
  rigged and both caster programs are built (`hasLightThroughCasters`) and the layer exists or can be made, the depth pass's blend faces
  set `shadow_through` (`renderAlphaObjects`): `shadowAlphaMaskF.glsl` and `pbrShadowAlphaBlendF.glsl` discard every texel below the solid
  cut instead of dithering it. After every depth caster (the last glTF mask batches), `renderLightThroughObjects` draws those faces again
  with `shadowLightThroughF.glsl` (`gDeferredShadowLightThroughProgram`, indexed textures; `gDeferredShadowGLTFLightThroughProgram`,
  `LIGHT_THROUGH_GLTF`) inside `LLLightThroughLayer::DrawScope`: the layer's framebuffer with both targets, `glBlendFunc(GL_ZERO,
  GL_SRC_COLOR)` (tau: the product), `glBlendEquationi(1, GL_MIN)` (zt: the nearest), the depth test against S, the face rule, colour
  writes on, the scissor at the tile's rect (the rect must be the render's viewport: asserted, so the casters land on the same texels as
  in S). The scope puts back the framebuffer, the blend equation (GL_FUNC_ADD) and function (LLRender's, `BT_ALPHA`, which
  `LLGLState::checkStates` expects), the shadow pass's colour mask, and the blend, depth, cull and scissor states LLGLState knew; the
  scissor box stays at the rect, the box every static tile render sets. `endThroughTarget` tells the pool whether the render ran the caster
  pass (`Record::through`). If a caster program does not build, a development build stops (`llassert`); a Release build logs it and the
  lamps dither as before.
- **Every drawn tile's rect is valid** once the layer exists (cleared at each static render, carried through a repack), so the lookup's
  flag is per lamp (`proj_through`: one of its drawn tiles holds a caster) and a wide lamp's cell without a caster reads exactly 1.
- **Repacks** (`applyPlan`): `lllightthrough::planCarry` lists the moved tiles that hold a caster and keep their size (copied), and the
  other moved ones (their new rect cleared: a resized tile has no content at its new size, so its layer is drawn again with its depth). The
  moves can form cycles, so `LLLightThroughLayer::carry` reads every source into a transient texture pair first, then clears, then writes
  the copies; a copy between rects of two sizes is refused there too. The transients hold every copied tile at once: at most one more
  layer, for the length of the repack (a rare event: an arriving tile that finds no block, or a fragmented atlas). A tile whose transient
  cannot be made reads no caster and is drawn again (`markStale`).
- **What does not take part**: the avatars' layer (P4) keeps the dither; a rigged face that reaches the fixed layer (an animesh's, if
  its faces do: not verified) keeps the depth rule (its binds set `shadow_through` 0, and the layer's draw skips it); mask faces keep the
  creator's cutoff (below, "Mask faces"); the sun's cascades are untouched.
- **A recolour redraws.** The colour of a face the blend pass draws is part of the prim's silhouette key ("Lamps that scripts switch",
  above), so an edit, a script's `PRIM_COLOR` or a glTF base-colour override of a pane redraws the tiles that see it: the cached tint
  follows. A blinker that toggles the colour of a blended face pushes once a toggle.
- **Debug setting** `RenderShadowLightThrough` (on), for A/B only: off, no tile is named a target and the blend faces dither as before. A
  change draws every tile again (`Cause::Other`); until a tile is drawn again its lookup follows the rule it was drawn with. No
  Preferences entry.
- **Threads.** Main render thread only, like the pool: the layer is made inside a static tile render, freed with the atlases or from
  `update` (the pool's `release` runs from `LLPipeline::destroyGL`, `allocateShadowBuffer` and `releaseShadowBuffers`, all on the main
  thread with the context alive), and nothing is reached from `LLRender::shutdown`, `LLImageGL` or `LLVertexBuffer::cleanupClass`. The
  destructor makes no GL call. The feed's colour input is read in the same main-thread hooks as the rest of the key.

### The lookup (`shadowStochasticUtil.glsl`, inside the G1 block)

`sampleProjectorShadow` returns a colour: the depth's visibility, times `stThroughFilter` when the lamp's tiles hold a caster and the
pixel is not in the umbra. `spotLightF.glsl` keeps its six multiplies (a colour times a colour now). `stThroughFilter(tau, zt, depth,
tile, c, g, z_bias, z_tex, pen, kmax)` takes the projector's atlas as `depth` (the one the depth filter reads, with the avatars) and the
depth filter's receiver plane, bias and cap:

1. **The reference** at each texel is the depth lookup's (`stThroughRef`, the C++ `texelRef`): the receiver's depth less one texel of
   bias, lowered (never raised) by the receiver plane over the texel's offset, at most the depth filter's `rp_max`, set up once per
   receiver (`stThroughPlane`: the same sum as `stSpotQuadTile`'s in another order). A pane flush in a wall lit obliquely no longer reads
   as in front of the wall beside it: the first build's flat reference tinted a fringe of 0.017, 0.12 and 0.21 there at 30, 45 and 60
   degrees (the twin's flush window); now exactly 1.
2. **The search**: the depth search's Vogel disc and radius (`pen * c.z`, between the floor and the cap) plus the receiver's own 2 x 2
   texels, 9 gathers of zt, for the nearest caster in front of the receiver (the largest stored depth below its reference). None: exactly
   1 (a receiver in front of a window, such as an avatar between the lamp and the glass, gets no tinted rim).
3. **The filter**: 16 taps on a Vogel disc at the physical radius from that caster (`pen * (c.z - z_near)`, between 1.5 and 12 texels:
   the depth filter's rule), weighted 1 - 0.9 r^2 as the depth filter's taps. Each tap (`stThroughTap`) gathers zt and the depth atlas at
   the same four texels (its uv clamped half a texel inside the tile: the tile's own CLAMP_TO_EDGE, no texel of a neighbour tile enters)
   and reads their bilinear tau. Its weight is the bilinear weight of the texels the depth does not block (the depth filter's lit test):
   a texel behind an opaque caster lets nothing through, and its tint must not count. Its light is that weight less `r (1 - tb)`, `tb` the
   bilinear tau and `r` the share of the caster texels' weight that is unblocked with its caster in front of the receiver: exact where
   every caster of the footprint is unblocked and in front of the receiver, exactly its weight where none is in front. The filter's value is the taps' light over
   their weight (the light through the unblocked part of the disc), 1 where every tap is blocked (the depth's own value is about 0 there).
   So a window frame does not brighten the inside of its penumbra, whether the pane runs on under the frame or stops at it (painted
   leading, a cut pane: the first build brightened those, RMS 0.045 against 0.034 at 1 cm texels, 0.033 against 0.013 at the slider's top).

The samplers are arguments, so every lamp pool reads its own layer and atlas through the same functions. 16 taps draw the tint's edge as
wide as the depth filter draws a shadow's edge, at any angle; 8 left gaps between the taps at wide radii (the twin: 0.70 to 0.92 of a
disc's 10-90 % width, depending on the edge's angle, against 0.85 to 0.91 with 16).

### For the point-light faces (L4)

L4 (`codex/point-light-shadows-l4`, reviewed at 94fccc14c1, at d7a2d2b310 when this was written) plugs into the same parts; nothing in
L3 is the projector's alone except its uniforms. What the point pool adds, and where a rebase onto L4 meets:

- **Owns a layer**: an `LLLightThroughLayer` at its own atlas edge with its own static depth atlas (`setAtlas(edge, S)` when its atlases
  are made; `free` with them, before S; `freeStorage` when idle, as `LLProjectorShadows::update` does), `clearRect` before each face's
  static render, `carry` if it repacks (with `planCarry`; a face's size change is cleared, not copied), `unbindUnits` before its tile work
  and `bind(shader, tau_uniform, depth_uniform)` with its own two sampler uniforms (`bind` no longer names the projector's).
- **Per face**: a `through` flag in its face record (the projector's is `Record::through`), set from `LLLocalShadowGL::endThroughTarget()`
  after `LLLocalShadowGL::setThroughTarget(layer, face_rect)` around the face's static render (the rect is the render's viewport: the
  scope asserts it); its lost carries and the debug toggle (`RenderShadowLightThrough`: a change draws every face again) mark faces stale.
  `LLPipeline::renderShadow` needs no change: the caster pass brings its own depth test, face rule and scissor, so L4's two-sided depth pass
  (`sLocalShadowTwoSided`) does not make a glass box filter twice.
- **Per lamp**: a flag uniform for the point programs (the projector's `proj_through`: one of the lamp's faces holds a caster).
- **The lookup**: `samplePointShadow` returns a float in L4, so `pointLightF.glsl` and `multiPointLightF.glsl` take a colour visibility
  (as `spotLightF.glsl` does here) and the face's lookup calls `stThroughFilter(tau, zt, depth, tile, c, g, z_bias, z_tex, pen, kmax)` after
  its own depth filter with its pool's samplers (its far tier's one-tap form can call `stThroughTap` at the receiver and divide by its
  weight). If its six faces are drawn in one instanced submit, the target must name the cube's six rects and the caster programs need the
  depth's per-instance face mapping (the same vertex shader and instance attribute).
- **Where the rebase meets** (L4 at d7a2d2b310): `sampleProjectorShadow`, where L4 passes the atlas into `stSpotFilterTile` on the line
  the L3 `vis` lines follow; `LLProjectorShadows::renderTile`, whose static render L4 rewrites around the lines where L3 clears the rect
  and names the target; `LLProjectorShadows::update`, whose frame work L4 moves into `LLLocalShadows::updateFrame`, around the change
  index's hit loop (which L3 now runs through `markStale`) and the unbind loop (where L3 adds the toggle redraw, the idle free and
  `mThrough.unbindUnits()`); `lllocalshadows.{h,cpp}`, where both add code (L4 about 400 lines; L3's seam is `setThroughTarget`,
  `throughTarget`, `endThroughTarget` and `LLLightThroughTarget`); `llprojectorshadowpool.h` and `llshadowdirtyfeed.cpp` (L4's dirty
  boxes carry their source drawable, L3's silhouette key a blended face's colour: different lines); the shader and uniform lists, the G1
  block's neighbourhood, `ci-checks` and these docs. L3 does not touch `stSpotQuadTile`, `stSpotFilterTile` or `stSpotPCF`.

### Meeting #559 (devel's caster rule)

Devel's #559 (902cde39ac, merged as 4f077cdb73; owner decision D4 of 2026-11-18) replaces the blend faces' dither in these same two
shaders (`shadowAlphaMaskF.glsl`, `pbrShadowAlphaBlendF.glsl`, their vertex shaders, `lllocalshadows.{h,cpp}` and
`test_shadow_alpha_permutation.py`) with the Aurora Viewer's caster rule, drops the 0.98 cut and the 0.05 floor for blend faces
(`SHADOW_BLEND_SOLID_ALPHA` 1.0, `alpha < 1.0`), and says lamps use the same rule. When this branch meets it:

- **The order**: in a static lamp tile with a layer the `shadow_through` discard comes first, before `shadow_caster_keeps` (the layer
  holds every texel below the cut; the caster rule stays for every other pass: the sun's cascades, the avatars' layer, a tile without a
  layer).
- **The cut is one number** in `kSolidAlpha`, `LIGHT_THROUGH_SOLID`, `SHADOW_BLEND_SOLID_ALPHA` and the glTF blend shader's literal;
  `test_light_through_twin.py` fails when one moves alone. A mechanical merge that puts the discard in #559's `alpha < 1.0` branch while
  the layer keeps 0.98 would leave faces between 0.98 and 1.0 casting nothing on lamps, so they move together.
- **For the owner, two decisions conflict**: the 2026-11-14 decision keeps a texel solid from 0.98 (the brief for L3), D4 drops every cut
  (a 98 % pane lets 2 % through). With the layer the cut is invisible either way (under 0.04 of the lamp's light), so D4's 1.0 costs
  nothing but the decision. And the sun and the lamps then use different transmittances: #559's coverage passes `1 - a` (grey: a white
  pane at 0.5 passes 0.5), L3 passes `(1 - a)(1 + a C)` (coloured: the same pane passes 0.75 of a lamp's light). One rule for both is the
  owner's call; L3 follows the brief here.

### Mask faces (the plan's stage L3.3: not built)

The plan's rule for alpha-mask lace (D5 a) was a sharpened coverage `a' = clamp((a - cutoff) / max(fwidth(a), 1e-4) + 0.5)`: depth where
`a' >= 0.98`, the layer times `1 - a'` below. The twin shows it does not give the open share where the tile does not resolve the holes:
the shadow pass samples the mask's mipmaps, whose average is smooth there, so `fwidth` is small and `a'` thresholds the average like the
cutoff does (the lace of the twin, open share 0.442: the cutoff gives 0.000 / 0.073 / 0.265 / 0.291 / 0.403 at 8 / 4 / 2 / 1 / 0.5 cm
texels, the plan's rule 0.007 / 0.104 / 0.267 / 0.343 / 0.413). An open share under minification needs the texture's level of detail in
the rule (a mask that the tile minifies passes `1 - a` of its mip average, the cutoff where the tile resolves it), which the indexed
textures of the classic mask draws do not give the shader; that is a design change, so mask faces keep the creator's cutoff here and the
question goes back to the plan.

### Measured

| gate | result |
|---|---|
| `indra/newview/tests/lllightthrough_test.cpp` (ci-checks; g++ 13.2 -O0/-O2 with -Wshadow, clang, MSVC) | the rule's limits and bounds, the plan's examples, the step under 0.04 at the solid cut, stacking; the reference (lowered, never raised, capped); the tap: behind every caster the bilinear tau, in front of them all or with none exactly its open weight, a frame with no pane under it exact over the open texels (the penumbra passes the pane's 0.5, not 0.75), the combine within [0, 1], an empty filter multiplying a visibility bit for bit; the memory figures; the carry (swap, chain, cycle; a resized tile cleared, never copied; 200 random repacks of the pool's planner with a quarter of the tiles shrinking): PASS |
| `llprojectorshadowpool_test` (ci-checks) | the RGB of a face the blend pass draws moves the silhouette key, that of a masked or opaque face does not; a tint blinker on a blended face pushes once a toggle (600 in a minute of 0.1 s toggles, static, active child, active root); every S2 check as before: PASS |
| `scripts/tests/test_light_through_twin.py` (ci-checks) | the GLSL pinned to its C++ twins (the rule, the tap, the combine, the reference against `stSpotQuadTile`'s, the plane cap against `stSpotFilterTile`'s), the limits one number everywhere, the samplers arguments, the caster pass's state (S attached, `GL_LEQUAL` without writes, the cull, the scissor, the viewport asserted), the carry's size guard, the pipeline's flag on every blend bind before its draw, rigged faces on the depth rule, the programs only with the pool and stopping a development build, the feed's colour input: 13/13 |
| `scripts/shadows/lamp_filter_twin.py --l3` (CPU) | uniform glass at 0.5: one value over the patch (spread 0, the dither's 0.33 to 0.83); glass behind its frame, RMS against the truth: 0.127 / 0.095 / 0.059 / 0.034 / 0.016 at 8 to 0.5 cm texels and the default softness (the dither 0.150 / 0.119 / 0.091 / 0.075 / 0.068), 0.116 to 0.007 at the top; painted leading: the same (the first build 0.135 / 0.115 / 0.074 / 0.045 / 0.026); two stacked panes: the product within 0.001; a receiver in front of both: exactly 1; a pane hidden under the floor: the front pane's 0.624 (truth 0.625; the first build 0.40 / 0.25 / 0.25), a glass globe over it 0.890 (truth 0.889); a red pane's edge: 0.85 to 0.91 of a uniform disc's 10-90 % width where the radius is physical; a window flush in a wall at 30 to 60 degrees: no tint on the wall (the flat reference 0.017 to 0.21) |
| `scripts/shadows/light_through_gate.py` (GPU, RTX 5070 Ti, NVIDIA 621.14) | link: both caster programs as the viewer composes them (the loader's header, the indexed-texture code, `textureUtilV`, `indexedTextureV` or `nonindexedTextureV`, `srgbF`) compile and link; casters: tau the product of the two see-through faces within 0.0034, the solid, invisible, hidden and back-facing ones left out, the depth the nearest's exactly, S untouched; identity: `sampleProjectorShadow` against b62d0fca18, 1024 x 1024 receivers, softness 0.23 and 1.0, `proj_through` 0 and 1 on an empty layer, max abs difference 0; spot: class3 `spotLightF.glsl`'s pool branch with a colour visibility against the base's float, the same stubs, classic and PBR pixels, both `classic_mode`s: 0 differing words of 262,144 pixels; twin: `stThroughFilter` against the CPU twin on seven layers with their depth tiles (frame, painted leading, stacked, hidden, red edge, flush window at 60 degrees, 40 random casters over random opaque depths), weighted by the open share, largest 0.00069 (a receiver on the edge of a decision, a caster within float rounding of its reference, is left out: 0 to 18 per scene) |
| `projector_pool_g1.py`, `projector_cells_check.py`, `proj_march_bench.py`, `proj_edges_bench.py` | G1 PASS, cells PASS, P2 gates PASS; both benches compile the checkout's lookup (its red channel) |

GPU cost of the lookup (`light_through_gate.py --time --first e082aadcf6`: 2560 x 1440 floor receivers, every one lit by the lamp, a
1024 tile; interleaved rounds, median of 9 x 20 draws, the first L3 build's lookup in the same rounds; six runs, each with a build
compiling or linking beside it, three of them with the layer's depth also timed in R16 and R16F; the three steadiest, those three, with
a spread within the run of a few per cent, first, then the range of all six):

| lamp | ms per full-screen lamp, steady runs | against L1 + L2 | the first L3 build, same rounds | all six runs |
|---|---|---|---|---|
| L1 + L2 (b62d0fca18) | 0.187 (0.0507 per lit megapixel) | | | 0.169 to 0.191 |
| no see-through caster in its tiles (`proj_through` 0; the value bit for bit) | 0.195 to 0.196 | +4.3 to +4.8 % | (+2.8 to +3.4 % when it was measured) | 0.177 to 0.196, +4.1 to +4.8 % where the base read steady |
| a caster in its tiles, none in front of the pixel (the search alone) | 0.234 to 0.235 | +25 to +26 % | 0.217 to 0.218, +16 to +17 % | 0.232 to 0.235; the first build 0.210 to 0.227 |
| every pixel behind a pane (the search and the 16 taps) | 0.400 to 0.409 | +114 to +118 % | 0.335 to 0.336, +79 to +80 % | 0.400 to 0.625; the first build 0.335 to 0.443 (this build 1.19 to 1.44 times the first) |

The search's extra cost is the receiver plane's references (9 gathers of 4 references), the taps' is the depth atlas's gather (3 reads a
tap instead of 2, as the read count predicts): the price of the frame rim, painted leading, the hidden casters and the flush window. The
layer's format does not matter here (zt in R16, R16F or R32F timed the same). The +4 % on every lamp is the light-through code's presence
in the program (only a second program variant per lamp kind removes it). Against the floor, from the steady runs: the living room at
1440p with four projectors, each lighting 60 % of the screen, two of them through a window with 30 % of their pixels behind the glass:
+0.011 ms for the two without glass, +0.040 ms of searches and +0.077 ms behind the glass for the two with, about 0.13 ms, so 60 -> 59.5
fps standing and 25 -> 24.9 fps moving on a frame that is GPU-bound exactly at the floor (the noisiest runs' pane figure would make it
about 0.21 ms: 60 -> 59.2 fps). A lamp inside a glass globe or cover that is a separate prim of its linkset has every lit pixel behind
glass: eight such lamps each lighting half of a 1440p screen add about 0.87 ms (60 -> 57.0 fps, 25 -> 24.5 fps). The caster pass adds
one draw per blended batch to a static tile render (a change, a lamp edit, a first fill; none in a static scene) and a clear of the
tile's rect; its CPU and GPU cost in the viewer is not measured.

### Limits

- **One depth.** A receiver between two see-through casters gets both tints (a pane in front of a curtain lit from the lamp's side, or
  double glazing with nothing between the panes, are right).
- **Pale blended lace** that the tile minifies comes out brighter than its open share (up to about 0.25 at a = 0.5 by the plan's
  estimate, not measured here): the rule is applied to the mip-averaged alpha, and it is not linear in it. Mask lace keeps the cutoff
  (above).
- **A step at the outer edge of a glazed frame's penumbra** (L1's): L1's search has 8 gathers and the receiver's own texels; a thin bar
  between them reads as no blocker (exactly 1), so neighbouring receivers jump where the truth is smooth. The dithered glass used to keep
  the search finding blockers; with the glass in the layer the step shows at every glazed frame and every came of stained glass: the
  largest neighbour step there is 0.13 / 0.18 / 0.18 / 0.12 at 4 / 2 / 1 / 0.5 cm texels and the default softness (the dither's 0.04 /
  0.09 / 0.17 / 0.27; L1's own table records 0.27 to 0.32 on other scenes). It belongs to the L1 filter's search.
- **A see-through caster within about a texel of depth of the receiver** is lost as an opaque caster that close is (the depth bias): a
  sheer 5 cm in front of a wall on a 4 cm tile lets 0.60 instead of 0.5 through, on an 8 cm tile all of it (1 cm and 2 cm tiles: 0.502).
- **The texture's level of detail at render time**: a tile's tint is drawn from the mip and discard level the caster's texture had when
  the tile was drawn, which follows the camera; nothing redraws the tile when the texture sharpens (the key tracks the channel count
  only). Masks have the same limit; for stained glass it is the whole pattern (a blurred pattern until something else redraws the tile).
- **A lamp whose tiles meet a blend face that lets no light through** (a material in blend mode with a solid texture, or one hidden
  behind walls: the flag is set when the static cull holds a blended face, whether or not a texel of it reached the layer) pays the search
  on its lit pixels and makes the layer. An occlusion query around the caster pass could clear the flag a frame later.
- **A glass shade or globe in the lamp's own linkset** (only the emitter prim is skipped) now tints and dims the lamp's whole light by its
  tau, as a real one does; before L3 it dithered.
- **Hardware filtering**: a tap's bilinear tau has the hardware's 8 bits of sub-texel weight, and the filter divides by the unblocked
  share of its disc, so where only a sliver of the disc is unblocked its value carries that error over the sliver's share (up to 0.5 on
  the GPU gate's frame scene). The depth's own visibility is small there too, which keeps the lit result's error small (the gate weighs
  its comparison by the open share, within 1/255; the product with the depth's visibility is not compared).

### Not verified (this section)

In-world: the look (a lamp behind tinted glass, a sheer curtain, stacked panes, stained glass with leading, an opaque prim beside them,
the slider at 0, 0.23 and 1), the step at a glazed frame's penumbra, a lamp in a glass shade of its linkset, a recolour of a pane, the CPU
and GPU cost of the caster pass and of covered lamps in a real scene, the carry under memory pressure, the idle free, whether an
animesh's blended faces reach the fixed layer (they keep the dither if they do), AMD and Intel. No viewer was launched for this stage: the
caster programs were linked as the viewer composes them, and the spot program compared bit for bit, on NVIDIA only.
