<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Point-light shadows (lamp shadows L4)

Point lights cast shadows with the Stochastic shadow method at shadow detail 2 (sun and projectors). Every point light within the
shadows' reach of the camera's position has six faces (a cube shadow map around its centre) as tiles of the point lights' own depth
atlases, and the lighting programs multiply each lamp's light by its own visibility, read with the projectors' soft filter. Two lamps
give two shadows of one person; the weaker lamp's shadow is the fainter, since its light is the smaller term.

Code: `llpointshadowpool.h` (the GL-free rules), `llpointshadows.h/.cpp` (the GL half), `lllocalshadows.h/.cpp` (what both local
shadow pools share: the frame's one selection over both, the casting avatars, the LOD pin and the avatars of a tile),
`shadowStochasticUtil.glsl` (`samplePointShadow`, `samplePointShadowNg`), `pointLightF.glsl` and `multiPointLightF.glsl`
(`POINT_SHADOW_POOL`), `llvertexbuffer.cpp` and `llindexskip.h` (the emitter left out). Plan: `reports/point-light-l4-2026-11-18/plan.md`
(kept on the owner's machine). The projector pool it reuses: `projector-shadow-pool.md`.

## Decisions it follows

- **Physics first** (owner, 2026-11-18): a point light is a small source at the light's centre; its shadow is the projectors'
  formula: one filter (`stSpotFilterTile`: a blocker search, then a percentage-closer filter whose radius is the physical penumbra's,
  between a floor of 1.5 texels and the type's cap), the emitter 4.5 cm times the Shadow softness (`kPointLamp`, the light-size study's
  base; the slider's default 0.23 gives 1.0 cm), no screen, frame or view term, the kernel in metres. No user slider of its own.
- **D3**: a separate atlas for point lights, 4096 from 3 GB of video memory, 2048 below (+128 MiB, +32 MiB; `pointAtlasTier`). The
  projectors' atlas is untouched: a projector never shrinks because of a point light.
- **D4**: the prim that emits the light is left out of its own faces by identity; the rest of its linkset casts.
- **D6**: forward alpha (blended surfaces lit in the forward pass) is lit by the point lights without their shadow, until stage I3.
- **D7 = D′** (owner, 2026-11-16): a lamp's faces instanced in one submit with the face from a per-instance attribute (atlas mode), and
  tile mode for a one-face draw. This stage draws every face in tile mode (one cull and one submit per face, the same picture); the atlas
  mode and one cull per lamp change the CPU cost of a whole lamp's render only and are the next step (see "Not done").
- **D8**: on at shadow detail 2 on an OpenGL 4.6 context, plus the debug setting `RenderShadowPointLights` (default on, no Preferences
  entry).
- **Avatars** (owner decision 4, 2026-11-14): every lamp casts the avatars; the nearest every frame, the others under the shared cap. The
  other avatars' faces are ordered by the camera position's distance to their nearest avatar (the projectors' P4 rule), not by the lamp's
  light at the avatar as plan section 4.5 proposed: the owner's decision orders them by nearness, and one rule serves both pools.
- **P1 (a)**, as recommended: one filter for every lamp, no lookup tiers: a lamp's shadow looks the same wherever the camera stands.
- **P2 (a)**, as recommended: the own avatar's faces run every frame only under the four lamps that light it most (the deferred pass's
  attenuation at the avatar's centre times the light's luminance): every face of such a lamp that the avatar's box meets (the body in
  one face, the floor shadow of the legs often in the next), together drawing at most 12 avatars a frame (half of A_max, so the other
  avatars' layers keep the other half); under the other lamps, and for a face past those 12 draws (a crowd under the lamp), they go by
  the near avatars' two-frame deadline, the strongest lamp first.

## The faces

- **Geometry** (`llpointshadowpool.h`): six 90-degree faces, +X -X +Y -Y +Z -Z in agent axes, each a rotation (`kFaceAxes`). The
  centre is the light's position (`LLDrawable::getPositionAgent`, the point the deferred pass lights from). Near plane 2 cm, far plane
  the lit size, radius x 1.5. The lamp's rotation never enters: a prim spinning about any axis keeps its faces. A lamp whose lit size is
  at or below the near plane lights nothing past it and is treated as switched off.
- **Guard.** Every face's interior (90 degrees) is widened by 16 texels on each side at every size, so its tangent is
  `(N / 2) / (N / 2 - 16)`: 106.3 degrees at 128, 91.8 at 1024. A receiver's face is the one its direction from the light falls in (the
  major axis; ties to x, then y; a zero to the positive face: `pickFace` and the shader's `stPointPick`, the same comparisons), picked
  from the normal-offset receiver, so the receiver lies in the face's interior and every tap of the filter at its cap (14 texels, plus
  the bilinear footprint) reads the same face's tile, its guard holding the neighbour's content past the seam. The tile's clamped edge
  is never reached (`llpointshadowpool_test` P-2), so the lookup equals that of an unbounded map.
- **Stored depth**: D32F, the perspective depth `z = c0 - c1 / w` of the face's projection (the projectors' form), with
  `c0 = f / (f - n)`, `c1 = c0 n`: the filter's numbers apply unchanged (`k = 1 / (2 t)` uv per metre at unit axial distance).
- **Size from the lamp** (`faceBaseSize`, L2's rule): the interior spans the face's two tangents at the reference depth
  `min(1.5 r, 4 m)` with 4 mm texels, plus the guards, a power of two in [128, 1024]. A lamp's six faces have one size: the valve sees
  one band per lamp (the camera position's distance to the lamp's lit sphere). The floor of a face is 128 (`ValveTile::floor`; a
  projector tile's stays 64).
- **The radius the faces are drawn for** (`faceRadius`): the lookup decodes a face's depth with the far plane it was drawn with and reads
  nothing beyond it. A radius more than 1 cm above the drawn one redraws the lamp at once (the faces end before its light); one more than
  1 cm below it keeps the drawn faces (the light ends sooner, inside them) and redraws them at the smaller radius only once it has stayed
  below for 5 s. A candle script that varies its radius never redraws; a lamp dimmed for good gets faces of its size.
- **The valve** (the projectors' `LevelController`, `BuddyAtlas`, `GrowthHold`, compaction): while the faces fit (demand at most 0.60),
  every face has its lamp's size; over it, the lamps farthest from the camera's position shrink first, in octaves, to the floor; a size
  change is drawn into a new rect before it is shown (draw-then-swap, per face). In the 4096 atlas 170 lamps fit at the floor, 42 in the
  2048 one; 59 lamps at the floor use 35 % of 4096. A lamp whose six faces find no room waits (its faces are placed all or none).
- **A full atlas** (`evictFor`): when the faces do not fit even at the floor (every pending shrink drawn would still leave no room), a
  waiting lamp takes the faces of the placed lamp farthest from the camera's position, if that one is farther by the reach's hysteresis
  (max(2 m, 10 %) of the waiting lamp's distance: `evicts`), so the lamps nearest the camera always have faces and two lamps at about the
  same distance never trade back and forth. The evicted lamp waits like any arrival, lit without a shadow; logged once.

## Records, changes and scheduling

- **A lamp** is a point light (a light that is not a projector) in `mNearbyLights` within the shadows' reach, min(draw distance, 128 m),
  of the camera's position, with the projectors' hysteresis, not worn, not on an avatar or HUD. A lamp switched off, turned black or given
  a radius too small to light past the near plane keeps its faces for 5 s (S2: a blinker is shadowed again at once), then gives them back.
- **One record per face** (key `lamp << 3 | face`); the scheduler packs it with `kPoolPoints` (a point face and a projector tile with the
  same raw key are two items: P-10). The change feed has the point pool as its second owner (`Owner::Points`: tracking and lamp roots); a
  dirty box marks only the faces whose volumes it touches, except the lamp's own emitter's boxes (`DirtyBox::source`,
  `TileVolume::ignore`: its faces never draw it, so a LOD switch of the bulb as the camera walks, or a blinker's alpha, redraws nothing of
  its own lamp). The LOD pin holds the casters a face draws at full detail (its own `PinBook`, the counter on the volume shared with the
  projectors, one rule for both pools: `LLLocalShadows::pinCasters`).
- **The set of faces**: the lookup reads a lamp once all six faces are drawn for its current set: one pose (the centre) and one face
  radius. A lamp's **first fill** draws the faces one item each (a face's cost, never drawn first, nearest lamp first), from the set
  started at the fill's first frame; a fill whose lamp moved and stopped, or whose radius grew, before the sixth face starts a new set. A
  lamp that **moved** draws its six faces as one item from its new pose (a lamp moving every frame: a dynamic item; one that moved and
  stopped, or whose radius grew past its faces: a static item, oldest first), so no lookup ever mixes two poses: until that render the
  lookup reads the faces of the set they hold. A face that a change touched, or that is resized, is a static item of its own, drawn from
  its set's pose; an avatar in a face's light is that face's dynamic layer item.
- **A moving lamp's linkset** is no lamp root while the lamp moves (the feed skips its pushes as any mover's): the lamp is drawn whole
  from its new pose anyway, and every other lamp leaves a moving linkset out of what it draws, so a boat or a car carrying a lamp marks no
  face or tile it passes.
- **One selection over both pools** (`LLLocalShadows::updateFrame`, from `generateSunShadow`): each pool prepares its frame and lists its
  work; `selectFrame` picks under the one cap (0.5 ms a frame; the dynamic layer first, up to 0.30 ms while a static tile waits; one
  progress pick may run past it); each pick is rendered by its pool; one GL save and restore for the frame. The frame's casting avatars
  are collected once for both pools (`LLLocalShadows::collectCasters`). A pick that draws nothing (a face already drawn for its set, a
  growth whose block an earlier resize took) does not count as the frame's first pick.
- **The progress pick beside the own avatar's faces** (shared with the projectors): the own avatar's every-frame faces are exempt from the
  cap and come first; the first pick after them that is past its deadline may run over the dynamic share as the frame's progress pick
  (before this, such an item could not run while the own avatar stood under a lamp: a moving point lamp's six faces, about 0.42 ms, are
  above the share and never ran beside the own faces, P-14). The static schedule's head still has its place first: while static work
  waits, a moving lamp above what is left waits too.

## Drawing a face

- **Tile mode**: a face is one `renderShadow` with the face's view and projection, the viewport and scissor on its tile, `LOCAL_SHADOW_STATIC`
  (the fixed layer: no avatars, no movers but the lamp's own linkset), the depth cleared in the tile, then S -> L and the avatars over it
  in L (`LOCAL_SHADOW_DYNAMIC`), as the projectors do it.
- **Both sides**: `LLPipeline::sLocalShadowTwoSided` makes `renderShadow` and `renderGeomShadow` (its terrain, trees and avatar bodies)
  draw both sides of every caster while a point face renders (a light inside a room sees the back of a one-sided mesh wall); every other
  shadow render culls back faces as before.
- **The emitter left out by identity** (D4): `RenderSpotLight` is the lamp's drawable, as for projectors, but the pipeline's skip of it in
  `stateSort` reaches only a drawable drawn on its own: a prim in a spatial group is drawn from the group's batches, which hold its faces
  between its neighbours' (the projectors are not affected: the emitter lies behind their near plane). So after the face's cull and sort,
  `LLPointShadows::skipEmitter` lists the emitter's faces' index ranges in their vertex buffers, and `LLVertexBuffer::drawRange` draws
  what is left of any triangle-list draw that holds them (`llindexskip::split`; null outside a point face render, one test per draw;
  another mode is drawn whole). The other prims of the linkset (a shade around the bulb) cast.
- The dithered semi-transparent casters take the face's tile origin (`LLLocalShadowGL::setTileOrigin`), as a projector tile's.

## The lookup

`samplePointShadowNg(lamp, faces_a, faces_b, pos, ng)` (`shadowStochasticUtil.glsl`): the normal offset of one face texel at the receiver
(scaled by the sine of the receiver plane's angle to the light's centre), the face of the offset receiver, its tile coordinates and depth,
the receiver plane's gradient in the face, then `stSpotFilterTile` on the point atlas with the point cap (14 texels) and the emitter
`point_shadow_params.x`. The receiver plane `ng` is the geometric normal from the depth buffer (S2, four depth reads:
`stPointReceiverNormal`), the same for every light: `pointLightF.glsl` (one light a pass) calls `samplePointShadow`, which computes it;
`multiPointLightF.glsl` computes it once per pixel (only when a light reaches the pixel) and then each light's visibility from one call
site before its two shading branches. A lamp without a complete set of drawn faces reads as lit; so does a receiver inside the near plane
or beyond the lit size. The projectors' reach fade applies (the last 20 % of 128 m).

- **Uniforms**: per pass `point_shadow_atlas`, `point_view_to_agent` (this frame's view rotation, transposed) and `point_shadow_params`
  (the emitter in metres, 1 / atlas size), set with the atlas in `bindShadowMaps`; per lamp `point_shadow_lamp` (the set's centre in view
  space, the far plane; 0: no lookup) and `point_shadow_faces` (the six face rects packed one per float: `x / 64 + 64 (y / 64) + 4096
  log2(N / 64)`, exact, the size unpacked by an integer shift; two vec4 per lamp), in `pointLightF.glsl` for the light drawn as a box and
  in arrays of the batch's size in `multiPointLightF.glsl` (48 vec4 at 16 lights, beside the 32 of the lights themselves).
- **Where**: `pointLightF.glsl` after the size discard, where the surface faces the light; `multiPointLightF.glsl` per light inside its
  lit size, where it faces the light. The light term (diffuse and specular) is multiplied by the visibility (1 where the light does not
  face the surface, which leaves the term as it was). Probes and mirrors use the same programs, so the same lookup. Water is not lit by the
  lamps.
- **Camera independence**: nothing in the lookup reads the screen, the frame or the view direction; the disc of the filter is fixed in the
  face's uv. `point_faces_g1.py` (d) finds four camera poses giving the same visibility to within the float rounding of the view transform.

## Debug setting and permutations

`LLPointShadows::wanted()` is the projector pool's test (Stochastic, detail 2), an OpenGL 4.6 context and `RenderShadowPointLights`; the
shader manager's `use_point_shadow_pool()` makes the same test. The setting reloads the shaders (`handleSetShaderChanged`) and so
reallocates the buffers: on, the point light programs compile with `POINT_SHADOW_POOL` (they read the depth buffer then:
`STOCHASTIC_SHADOW_COMMON_DEPTH_KEY`) and the atlases are made; off (or an older context), the programs compile without it (their text is
then release's: `check_legacy_glsl.py`) and the atlases are freed, so the point light passes are today's.

## Threads, lifetime, failure

Main render thread only, like the projector pool: every entry returns at once off it and while the viewer exits. Nothing is reached from
`LLRender::shutdown`, `LLImageGL` or `LLVertexBuffer::cleanupClass`. New shared state, all main-thread and set only inside a point face
render: `LLPipeline::sLocalShadowTwoSided`, `LLVertexBuffer::sSkipRanges`. The atlases are released with the context alive
(`destroyGL`, `cleanup`, a method, detail or setting change); a failed allocation tries the next smaller atlas, then leaves the point
lights unshadowed and tries again after 5 s, doubling to 60 s.

Callers of what changed (all on the main thread):
- `LLPipeline::renderShadow` and `renderGeomShadow` (its only caller is `renderShadow`): the sun cascades (`generateSunCascades*`), the
  projector tiles and avatar layers, the Legacy spot slots, and the point faces and their avatar layers. Only the last set
  `sLocalShadowTwoSided`; the pins and the emitter skip return at once unless their pool is drawing.
- `LLVertexBuffer::drawRange`: every indexed draw of every pass; `sSkipRanges` is null outside a point face render.
- `LLProjectorShadows::update` became `prepare`, `beginRenders`, `renderPick` and `endFrame`, called by `LLLocalShadows::updateFrame`
  only. The projectors' LOD pin and the avatars of a tile are the shared `LLLocalShadows::pinCasters` and `tileAvatars` (the same rules,
  moved). `selectDynamic` and `selectFrame` (both pools) gained the progress pick beside the own avatar's faces; `DirtyBox` and the
  change index gained the source a tile may leave out (null for every projector tile: their hits are unchanged).
- `LLShadowDirtyFeed::push` (every hook) records the drawable whose hook fired; nothing follows the pointer.

## Gates

- `indra/newview/tests/llpointshadowpool_test.cpp` (CI step): the face pick against a brute force on 10^6 directions and the exact
  ties; the render's matrices against the lookup's uv and depth; every tap at the cap inside its tile for N = 128 .. 1024; the sizes and
  the valve's floors (the projectors' 64 unchanged); the capacity and the atlas tier (`pointAtlasTier`, never 8192); draw-then-swap per
  face; the emitter skip against a brute force; the pose and the face radius (a candle never redraws); the merged selection (clashing raw
  keys stay two items, the cap holds but for one progress pick, neither pool starves); the own avatar's lamps and its faces' draws; the
  eviction rule (never back and forth); a fill beside six avatar layers (they keep their share) and a moving lamp beside the own faces (it
  runs every fourth frame; before this change it never ran, P-14); the emitter's own boxes left out of its faces (P-15); the packed rects
  for every rect of the 4096 atlas. `llprojectorshadowpool_test` passes (its T7 oracle follows the progress rule beside the own faces).
- `scripts/shadows/point_faces_g1.py` (G1-P, GL): the shader's pick, face coordinates, uv and depth against the C++ twin on 10^6
  directions (the face exactly; uv within 16 ulp of 1 and depth within 4 ulp: measured 9.6e-8 and 7.9e-8 on NVIDIA); the packed rects
  (exact); the real lookup on six ray-cast faces of a lamp behind a 1 m wall at every face size (the umbra 0, the open receivers 1, no read
  past a face's tile: each tile has a border of blockers, so a leak would shadow the open receivers; the penumbra against an analytic disc
  emitter); four camera poses (the same visibility to the view transform's rounding); `--time` for the lookup's cost.
- `scripts/shadows/projector_pool_g1.py --base b62d0fca18`: the projectors' lookup after the filter took its atlas as a parameter, max abs 0
  against the code before at 4096 and 8192; G1 (a) and (b) and `projector_cells_check.py` pass.
- `scripts/tests/test_projector_lamp_filter.py`: the shader's point constants are `kPointLamp`'s and the faces' (guard, near plane).
- `check_legacy_glsl.py` (the point light shaders with `POINT_SHADOW_POOL` undefined are release's), `check_legacy_shadows.py`,
  `check_release_hooks.py`.
- Not a gate here: "two lamps, two shadows, the weaker fainter" (plan section 5) holds by construction (each light's term is multiplied
  by its own visibility in both programs) and was checked in the code; no GL image test of two lamps exists.

## Costs

The frame-rate floor: about 60 fps standing in a crowded room, never below 25 fps moving.

| Item | GPU | CPU | Memory |
|---|---|---|---|
| Point atlases S and L | 0 | 0 | +128 MiB (4096), +32 MiB (2048) |
| Static faces, after the fill | 0 | 0 a frame (the bookkeeping below) | |
| A fill (first faces, a teleport) | the faces' draws | one item per face inside the shared 0.5 ms a frame (about 0.07 ms a face measured for projector tiles: about seven faces a frame, the avatar layers keeping their share); each face is drawn about twice, the second time after the LOD pin raised its casters (their rebuild marks it, cause LodUp) | |
| A lamp that moved | its six faces' draws | one item of six faces, about 0.42 ms: alone it fits the cap every frame; beside other dynamic work it runs as the overdue progress pick (every fourth frame); while static work waits, it waits | |
| Avatars in a lamp's light | their draws per face | inside the dynamic share (0.30 ms while a static tile waits) | |
| The own avatar's every-frame faces | their draws | past the cap, by design (P2): at most 12 avatar draws a frame for all of them (half of A_max) | |
| The LOD pin | the casters at full detail in every view (main view, sun cascades, probes) | the rebuilds when a caster is first pinned | not measured: every caster in the lit sphere of every lamp within the reach is held at full detail |
| Bookkeeping (`prepare`) | 0 | each lamp's pose, and the avatars against its faces' volumes (a box filter per lamp, then six tests per avatar it lets through), every frame: the pairing measured offline (the header's rules, 2026-11-19, the build machine) at 0.012 ms for 55 lamps and 30 avatars, 0.020 ms with 60 avatars, 0.043 ms for 170 lamps and 30 avatars, 0.072 ms with 60 (over the plan's 0.05 ms gate only past 100 or so lamps in a crowd); the rest of `prepare` in the viewer not measured | |
| Lookup | 0.059 to 0.083 ms per lit megapixel per lamp (below) | the uniforms | |
| Programs | | compile and link of the 16-light batch 44 to 97 ms (one call site; 87 to 197 ms with the two it had) | |

The lookup's cost scales with the pixels each lamp lights (inside its lit sphere, facing it), summed over the lamps. Measured
2026-11-19 on the build machine (RTX 5070 Ti, NVIDIA 621.14; the CPU at about 37 % from another agent's build, the GPU otherwise idle) with
`point_faces_g1.py --time`: a 2560 x 1440 pass with every pixel a receiver in the lamp's light, a third of them behind the wall (in its
umbra and penumbra), neighbouring pixels on neighbouring points; two interleaved runs (sizes ascending, then descending). The viewer's
variant rebuilds the receiver plane from four position reads (as `getPosition` reads the depth buffer); the batch is 16 lights at every
pixel in `multiPointLightF.glsl`'s form (the plane once, one call site):

| Face size | ms per pass, shading normal | ms per pass, viewer's variant | ms per lit megapixel per lamp, viewer's | batch of 16, per lamp |
|---|---|---|---|---|
| 128 | 0.268, 0.271 | 0.304, 0.306 | 0.082, 0.083 | 0.079, 0.078 |
| 256 | 0.227, 0.238 | 0.262, 0.274 | 0.071, 0.074 | 0.065, 0.065 |
| 512 | 0.196, 0.214 | 0.232, 0.249 | 0.063, 0.068 | 0.056, 0.057 |
| 1024 | 0.183, 0.199 | 0.217, 0.244 | 0.059, 0.066 | 0.053, 0.063 |

The receiver plane from the depth buffer adds 0.034 to 0.045 ms a pass for one lamp (13 to 23 %); in the batch it is computed once for all
lights, so a batch's lamp costs about what the shading-normal lookup does. A coarse face costs the most: its 1.5-texel floor is wider in
metres, so more pixels run the 32-tap filter. Estimates from these: a lamp lighting a quarter of a 1440p screen, 0.05 to 0.08 ms; the
owner's living room, 53 point lamps each lighting 20 to 40 % of a 1440p screen (plan #513's guess, not measured) with their faces at the
floor of 128 (0.078 to 0.083 ms per lit megapixel per lamp), 3.0 to 6.5 ms GPU a frame. A GPU-bound frame at the 60 fps floor (16.7 ms)
would drop to 51 to 43 fps there: the in-world
check in the living room is the gate, with `RenderShadowPointLights` off and on for the difference. If it is over the floor, the next step
keeps the image exact: a per-face minimum and maximum depth (a small mip of each face) that answers fully lit and fully blocked pixels with
one fetch (plan #513 Q3a).

## Not done, and limits

- **The face renders' form** (plan commits 3, 4 and 7): each face is a full `renderShadow` with its own cull, sort and submit (#513's
  method B). Not done: the split of `renderShadow` into cull and submit with one sphere cull per lamp, the group masks (a spatial group's
  faces from its box), and D′'s atlas mode (several faces of a lamp in one instanced submit). They lower the CPU of a whole lamp's render
  (2.2 to 4.7 times by the Q0 bench) and so let a moving lamp redraw every frame beside the avatar layers; the picture is the same.
- The fill's and a moving lamp's CPU, the bookkeeping per frame in the viewer (only its pairing was measured, offline), the LOD pin's
  triangles and the programs' link time in the viewer are not measured; the owner's in-world check (the living room, a club,
  `RenderShadowPointLights` off and on) is the frame-level gate.
- **A light inside another object**: the faces draw both sides of every caster and leave out only the emitter prim, so a light whose
  centre lies more than the near plane (2 cm) inside another prim (a downlight sunk into a ceiling slab, a bulb inside a closed opaque
  shade) is blocked in every direction that prim's surfaces cover. That is what the physics says; content built while no light cast a
  shadow may expect the light to pass. How common it is was not counted; the choice (as is, back faces culled except double-sided
  materials, or back faces near the light left out) is the owner's.
- In a crowd of more than A_max (24) avatars in one face, the 24 nearest the light are drawn into it (both pools' rule), the own avatar
  among the others.
- Forward alpha is unshadowed by point lights (D6) until I3; light through glass (tint) is L3; movers in the dynamic layer are L5.
- A seam between two faces of different sizes shows only during the frames a lamp's resize is being drawn (each face swaps when it is
  drawn).
- Not measured: AMD and Intel; the fill and the lookup in the owner's rooms.
