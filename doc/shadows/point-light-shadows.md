<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Point-light shadows (lamp shadows L4)

Point lights cast shadows with the Stochastic shadow method at shadow detail 2 (sun and projectors). Every point light within the
shadows' reach of the camera's position has six faces (a cube shadow map around its centre) as tiles of the point lights' own depth
atlases, and the lighting programs multiply each lamp's light by its own visibility, read with the projectors' soft filter. Two lamps
give two shadows of one person; the weaker lamp's shadow is the fainter, since its light is the smaller term.

Code: `llpointshadowpool.h` (the GL-free rules), `llpointshadows.h/.cpp` (the GL half), `lllocalshadows.h/.cpp` (what both local
shadow pools share, and the frame's one selection over both), `shadowStochasticUtil.glsl` (`samplePointShadow`), `pointLightF.glsl`
and `multiPointLightF.glsl` (`POINT_SHADOW_POOL`), `llvertexbuffer.cpp` and `llindexskip.h` (the emitter left out). Plan:
`reports/point-light-l4-2026-11-18/plan.md` (kept on the owner's machine). The projector pool it reuses: `projector-shadow-pool.md`.

## Decisions it follows

- **Physics first** (owner, 2026-11-18): a point light is a small source at the light's centre; its shadow is the projectors'
  formula: one filter (`stSpotFilterTile`: a blocker search, then a percentage-closer filter whose radius is the physical penumbra's,
  between a floor of 1.5 texels and the type's cap), the emitter 4.5 cm times the Shadow softness (`kPointLamp`, the light-size study's
  base; the slider's default 0.23 gives 1.0 cm), no screen, frame or view term, the kernel in metres. No user slider of its own.
- **D3**: a separate atlas for point lights, 4096 from 3 GB of video memory, 2048 below (+128 MiB, +32 MiB). The projectors' atlas is
  untouched: a projector never shrinks because of a point light.
- **D4**: the prim that emits the light is left out of its own faces by identity; the rest of its linkset casts.
- **D6**: forward alpha (blended surfaces lit in the forward pass) is lit by the point lights without their shadow, until stage I3.
- **D7 = D′** (owner, 2026-11-16): a lamp's faces instanced in one submit with the face from a per-instance attribute (atlas mode), and
  tile mode for a one-face draw. This stage draws every face in tile mode (one cull and one submit per face, D′'s own one-face path, the
  same picture); the atlas mode changes the CPU cost of a whole lamp's fill only and is the next step (see "Not done").
- **D8**: on at shadow detail 2, plus the debug setting `RenderShadowPointLights` (default on, no Preferences entry).
- **P1 (a)**, as recommended: one filter for every lamp, no lookup tiers: a lamp's shadow looks the same wherever the camera stands.
- **P2 (a)**, as recommended: the own avatar's faces run every frame only under the four lamps that light it most (the deferred pass's
  attenuation at the avatar's centre times the light's luminance); under the others they go by the near avatars' two-frame deadline,
  the strongest first.

## The faces

- **Geometry** (`llpointshadowpool.h`): six 90-degree faces, +X -X +Y -Y +Z -Z in agent axes, each a rotation (`kFaceAxes`). The
  centre is the light's position (`LLDrawable::getPositionAgent`, the point the deferred pass lights from). Near plane 2 cm, far plane
  the lit size, radius x 1.5. The lamp's rotation never enters: a prim spinning about any axis keeps its faces.
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
- **The valve** (the projectors' `LevelController`, `BuddyAtlas`, `GrowthHold`, compaction): while the faces fit (demand at most 0.60),
  every face has its lamp's size; over it, the lamps farthest from the camera's position shrink first, in octaves, to the floor; a size
  change is drawn into a new rect before it is shown (draw-then-swap, per face). In the 4096 atlas 170 lamps fit at the floor; 59 lamps
  at the floor use 35 %. A lamp whose six faces find no room waits (its faces are placed all or none); one the atlas cannot hold even at
  the floor stays unshadowed, logged once.

## Records, changes and scheduling

- **A lamp** is a point light (a light that is not a projector) in `mNearbyLights` within the shadows' reach (128 m) of the camera's
  position, with the projectors' hysteresis, not worn, not on an avatar or HUD. A lamp switched off, turned black or given a radius of 0
  keeps its faces for 5 s (S2: a blinker is shadowed again at once), then gives them back.
- **One record per face** (key `lamp << 3 | face`); the scheduler packs it with `kPoolPoints`. The change feed has the point pool as its
  second owner (`Owner::Points`: tracking and lamp roots); a dirty box marks only the faces whose volumes it touches; the LOD pin holds
  the casters a face draws at full detail (its own `PinBook`, the counter on the volume shared with the projectors).
- **A whole lamp** is one item (key `lamp << 3 | 7`): its first fill, and a redraw after it moved, draw its six faces in one render from
  one pose, which the lookup reads only once all six are drawn. Until then the lookup reads the faces of the pose they were drawn with (or
  nothing, for a new lamp: lit). A lamp that moves every frame is a dynamic item; a face that a change touched, or that is resized, is a
  static item of its own, drawn from the pose its lamp's faces hold; an avatar in a face's light is that face's dynamic layer item.
- **One selection over both pools** (`LLLocalShadows::updateFrame`, from `generateSunShadow`): each pool prepares its frame and lists its
  work; `selectFrame` picks under the one cap (0.5 ms a frame; the dynamic layer first, up to 0.30 ms while a static tile waits; one
  progress pick may run past it); each pick is rendered by its pool; one GL save and restore for the frame. With the point pool off or
  idle, the projectors' lists and picks are those of the projector pool alone (`llpointshadowpool_test` P-10). The frame's casting
  avatars are collected once for both pools (`LLLocalShadows::collectCasters`).

## Drawing a face

- **Tile mode**: a face is one `renderShadow` with the face's view and projection, the viewport and scissor on its tile, `LOCAL_SHADOW_STATIC`
  (the fixed layer: no avatars, no movers but the lamp's own linkset), the depth cleared in the tile, then S -> L and the avatars over it
  in L (`LOCAL_SHADOW_DYNAMIC`), as the projectors do it.
- **Both sides**: `LLPipeline::sLocalShadowTwoSided` makes `renderShadow` draw both sides of every caster while a point face renders (a
  light inside a room sees the back of a one-sided mesh wall); every other shadow render culls back faces as before.
- **The emitter left out by identity** (D4): `RenderSpotLight` is the lamp's drawable, as for projectors, but the pipeline's skip of it in
  `stateSort` reaches only a drawable drawn on its own: a prim in a spatial group is drawn from the group's batches, which hold its faces
  between its neighbours' (the projectors are not affected: the emitter lies behind their near plane). So after the face's cull and sort,
  `LLPointShadows::skipEmitter` lists the emitter's faces' index ranges in their vertex buffers, and `LLVertexBuffer::drawRange` draws
  what is left of any draw that holds them (`llindexskip::split`; null outside a point face render, one test per draw). The other prims of
  the linkset (a shade around the bulb) cast.
- The dithered semi-transparent casters take the face's tile origin (`LLLocalShadowGL::setTileOrigin`), as a projector tile's.

## The lookup

`samplePointShadow(lamp, faces_a, faces_b, pos, norm, pos_screen)` (`shadowStochasticUtil.glsl`): the receiver plane from the depth
buffer (S2), the normal offset of one face texel at the receiver (scaled by the sine of the angle to the light's centre), the face of the
offset receiver, its tile coordinates and depth, the receiver plane's gradient in the face, then `stSpotFilterTile` on the point atlas
with the point cap (14 texels) and the emitter `point_shadow_params.x`. A lamp without a complete set of drawn faces reads as lit; so
does a receiver inside the near plane or beyond the lit size. The projectors' reach fade applies (the last 20 % of 128 m).

- **Uniforms**: per pass `point_shadow_atlas`, `point_view_to_agent` (this frame's view rotation, transposed) and `point_shadow_params`
  (the emitter in metres, 1 / atlas size), set with the atlas in `bindShadowMaps`; per lamp `point_shadow_lamp` (the drawn centre in view
  space, the far plane; 0: no lookup) and `point_shadow_faces` (the six face rects packed one per float: `x / 64 + 64 (y / 64) + 4096
  log2(N / 64)`, exact; two vec4 per lamp), in `pointLightF.glsl` for the light drawn as a box and in arrays of the batch's size in
  `multiPointLightF.glsl` (48 vec4 at 16 lights, beside the 32 of the lights themselves).
- **Where**: `pointLightF.glsl` after the size discard, where the surface faces the light; `multiPointLightF.glsl` per light inside its
  lit size, where it faces the light. The light term (diffuse and specular) is multiplied by the visibility. Probes and mirrors use the
  same programs, so the same lookup. Water is not lit by the lamps.
- **Camera independence**: nothing in the lookup reads the screen, the frame or the view direction; the disc of the filter is fixed in the
  face's uv. `point_faces_g1.py` (d) finds four camera poses giving the same visibility to within the float rounding of the view transform.

## Debug setting and permutations

`LLPointShadows::wanted()` is the projector pool's test (Stochastic, detail 2) and `RenderShadowPointLights`. The setting reloads the
shaders (`handleSetShaderChanged`) and so reallocates the buffers: on, the point light programs compile with `POINT_SHADOW_POOL` (they
read the depth buffer then: `STOCHASTIC_SHADOW_COMMON_DEPTH_KEY`) and the atlases are made; off, the programs compile without it (their
text is then release's: `check_legacy_glsl.py`) and the atlases are freed, so the point light passes are today's.

## Threads, lifetime, failure

Main render thread only, like the projector pool: every entry returns at once off it and while the viewer exits. Nothing is reached from
`LLRender::shutdown`, `LLImageGL` or `LLVertexBuffer::cleanupClass`. New shared state, all main-thread and set only inside a point face
render: `LLPipeline::sLocalShadowTwoSided`, `LLVertexBuffer::sSkipRanges`. The atlases are released with the context alive
(`destroyGL`, `cleanup`, a method, detail or setting change); a failed allocation tries the next smaller atlas, then leaves the point
lights unshadowed and tries again after 5 s, doubling to 60 s.

Callers of what changed (all on the main thread):
- `LLPipeline::renderShadow`: the sun cascades (`generateSunCascades*`), the projector tiles and avatar layers, the Legacy spot slots,
  and the point faces and their avatar layers. Only the last set `sLocalShadowTwoSided`; the pins and the emitter skip return at once
  unless their pool is drawing.
- `LLVertexBuffer::drawRange`: every indexed draw of every pass; `sSkipRanges` is null outside a point face render.
- `LLProjectorShadows::update` became `prepare`, `beginRenders`, `renderPick` and `endFrame`, called by `LLLocalShadows::updateFrame`
  only; the projectors' selection, caps and order of service are the same code.

## Gates

- `indra/newview/tests/llpointshadowpool_test.cpp` (CI step): the face pick against a brute force on 10^6 directions and the exact
  ties; the render's matrices against the lookup's uv and depth; every tap at the cap inside its tile for N = 128 .. 1024; the sizes and
  the valve's floors (the projectors' 64 unchanged); the capacity; draw-then-swap per face; the emitter skip against a brute force; the
  pose; the merged selection (projector-only lists pick as before, the cap holds but for one progress pick, neither pool starves); the own
  avatar's lamps; the packed rects for every rect of the 4096 atlas. The projector pool's test passes unchanged.
- `scripts/shadows/point_faces_g1.py` (G1-P, GL): the shader's pick, face coordinates, uv and depth against the C++ twin on 10^6
  directions (the face exactly, uv and depth within 1e-7); the packed rects (exact); the real lookup on six ray-cast faces of a lamp
  behind a 1 m wall at every face size (the umbra 0, the open receivers 1, no read past a face's tile, the penumbra against an analytic
  disc emitter); four camera poses (the same visibility to the view transform's rounding); `--time` for the lookup's cost.
- `scripts/shadows/projector_pool_g1.py --base b62d0fca18`: the projectors' lookup after the filter took its atlas as a parameter, max abs 0
  against the code before at 4096 and 8192; G1 (a) and (b) and `projector_cells_check.py` pass.
- `scripts/tests/test_projector_lamp_filter.py`: the shader's point constants are `kPointLamp`'s and the faces' (guard, near plane).
- `check_legacy_glsl.py` (the point light shaders with `POINT_SHADOW_POOL` undefined are release's), `check_legacy_shadows.py`,
  `check_release_hooks.py`.

## Costs

The frame-rate floor: about 60 fps standing in a crowded room, never below 25 fps moving.

| Item | GPU | CPU | Memory |
|---|---|---|---|
| Point atlases S and L | 0 | 0 | +128 MiB (4096), +32 MiB (2048) |
| Static faces, after the fill | 0 | 0 a frame | |
| A fill (first faces, a teleport) | the faces' draws | inside the shared 0.5 ms a frame (about 0.07 ms a face measured for projector tiles: about seven faces a frame) | |
| Avatars in a lamp's light | their draws per face | inside the dynamic share (0.30 ms while a static tile waits); the own avatar's faces under four lamps every frame | |
| Lookup | 0.050 to 0.074 ms per lit megapixel per lamp (below) | the uniforms | |

The lookup's cost scales with the pixels each lamp lights (inside its lit sphere, facing it), summed over the lamps. Measured
2026-11-18 on the build machine (RTX 5070 Ti, NVIDIA 621.14; the CPU at 100 % from a parallel build, the GPU otherwise idle) with
`point_faces_g1.py --time`: a 2560 x 1440 pass with every pixel a receiver in the lamp's light, a third of them behind the wall (in its
umbra and penumbra), neighbouring pixels on neighbouring points; two interleaved runs (sizes ascending, then descending), spread at most
7 %:

| Face size | ms per 2560 x 1440 pass | ms per lit megapixel per lamp |
|---|---|---|
| 128 | 0.270, 0.271 | 0.073, 0.074 |
| 256 | 0.228, 0.228 | 0.062, 0.062 |
| 512 | 0.198, 0.197 | 0.054, 0.053 |
| 1024 | 0.185, 0.197 | 0.050, 0.054 |

A coarse face costs the most: its 1.5-texel floor is wider in metres, so more pixels run the 32-tap filter. Estimates from these: a lamp
lighting a quarter of a 1440p screen, 0.05 to 0.07 ms; the owner's living room, 53 point lamps each lighting 20 to 40 % of a 1440p screen
(plan #513's guess, not measured) with their faces at the floor of 128, 2.9 to 5.8 ms GPU a frame. A GPU-bound frame at the 60 fps floor
(16.7 ms) would drop to 51 to 46 fps there: the in-world check in the living room is the gate, with `RenderShadowPointLights` off and on for
the difference. If it is over the floor, the next step keeps the image exact: a per-face minimum and maximum depth (a small mip of each
face) that answers fully lit and fully blocked pixels with one fetch (plan #513 Q3a).

## Not done, and limits

- **D′ atlas mode** (several faces of a lamp in one instanced submit with the face from a per-instance attribute, group masks): a cost
  lever for whole-lamp fills; faces are drawn one at a time (tile mode, which D′ keeps for one-face draws).
- Forward alpha is unshadowed by point lights (D6) until I3; light through glass (tint) is L3; movers in the dynamic layer are L5.
- A seam between two faces of different sizes shows only during the frames a lamp's resize is being drawn (each face swaps when it is
  drawn).
- Not measured: AMD and Intel; the fill and the lookup in the owner's rooms (the in-world check is the frame-level gate).
