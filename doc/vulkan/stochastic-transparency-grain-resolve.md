# Stochastic transparency in the Vulkan renderer: the grain-measuring resolve

Added 2026-11-14 with the OpenGL upgrade (S set back to 8 on 2026-11-15) of the transparency
([../oit/stochastic-transparency.md](../oit/stochastic-transparency.md)). The OpenGL viewer runs the resolve as two
fragment passes and a composite that smooths (compute dispatches stall the OpenGL frame and OpenGL has no async compute). The Vulkan renderer is where
the compute form belongs.

## The method to carry over

- Targets and masks as in OpenGL: S = 8 samples on the stochastic targets, coverage through the sample mask, the total alpha
  at every stochastic sample (not one sample), the alpha correction (total alpha over accumulated alpha) made at the
  composite.
- The resolve is Aurora Viewer PR 3663's three compute steps (`oit_stochastic.wgsl` `cs_resolve`, `cs_temporal`,
  `cs_smooth`; Gaalrauch, GPL-3.0-or-later), on this engine's history format:
  1. measure: one workgroup tile averages the samples once (all S = 8 samples of a Vulkan target are one pass; the method needs 8 samples per pass, as the OpenGL viewer does on its GL 4.6 base), with the grain (the standard error of the colour from the
     sums of squares of the samples), the nearest and farthest stochastic depth and the total alpha; the 3 x 3
     neighbourhood and the smooth read the tile (shared memory), so the OpenGL form's two RGBA16F targets are not needed;
  2. blend: the frame weight comes from the grain (1 where one layer covers the pixel), the last frame is left out where
     its total alpha differs (0.004 to 0.016) or the far layer slid more than 1/4 px (to 3/4 px) past the near one, the
     history is read with a Catmull-Rom filter and clamped to the neighbourhood box widened by one grain;
  3. smooth: neighbours of a like total alpha and a colour within twice the remaining grain, radius 1 then 2.
- History: the four-vector (alpha x colour, alpha), corrected after the blend, not the corrected colour (the corrected colour
  made the low-frequency error twice as large, measured offline at S = 4), plus the last frame's total alpha.
- Finite values only: a bit test on the exponent, NaN samples count for nothing, infinities clamp to 65504, a NaN
  transparency hides nothing, a non-finite alpha discards the fragment, no history tap, box or composite value is
  non-finite.
- Dither: a 64 x 64 void-and-cluster blue-noise tile (generated offline, stored in two spare rows of the mask table) plus a
  per-face offset plus the golden-ratio step of the frame; the subset of covered samples is drawn afresh per face, pixel
  and frame (a subset fixed over frames, or one rotation stride for all layers, measured 1.8 to 1.9 times worse).

## Tile skip: the resolve only where something transparent was drawn (owner design)

The OpenGL form skips empty pixels with a mask the measuring pass writes (R8, per pixel; the blend and the composite leave at once where it is 0, with the
history written empty there, so the result equals the unskipped one bit for bit). The Vulkan renderer does it by tile and without per-pixel threads, with
compute and an indirect dispatch:

1. The total-alpha pass has written the transparency target (red: the mean of prod(1 - alpha) over the samples; 1 where nothing was drawn). A small compute step,
   one workgroup per 8 x 8 tile (or one thread per tile reading 64 texels, whichever the profile prefers), tests the tile: it is listed when any pixel of it has total alpha
   below 1 (the glow, written in the same target's green, and the accumulation's alpha count too: a tile is listed when any of the lanes the measure reads is not empty,
   so the list is a superset of the tiles the resolve would change, never smaller).
2. The step appends the tile's index to a list in a storage buffer with one atomic add on a counter, and the last workgroup (or a second tiny dispatch) writes the
   counter into an indirect-dispatch argument buffer as (count, 1, 1) workgroups.
3. The measure, blend and composite steps are `vkCmdDispatchIndirect` over that list: each workgroup reads its tile index from the list and processes the 8 x 8 pixels.
   Tiles not in the list are never launched. The history of an unlisted tile has to be written empty (the history ping-pongs, a tile skipped for a frame would keep the frame before
   last): either a clear of the history image before the dispatch (a cheap fast clear), or a first dispatch over the tiles of the previous frame's list that are not in this one.
4. The reach: the blend's 3 x 3 box and the smooth's radius 2 read the neighbours of a listed tile, which may lie in an unlisted one. Those neighbours must read as
   they do unskipped: the measure writes (or the tile's shared memory holds) the aux values for the border of a listed tile, 2 pixels around it, from the same lanes; an
   empty neighbour's history reads as the cleared history. If the border read is costlier than listing the neighbouring tiles, list the tiles within one tile of a hit instead (dilate the list by one tile).

Test it as the OpenGL form is tested: the skipped and the unskipped resolve over a frame sequence with transparency appearing, moving, scattered and gone the next frame, image and history bit for bit.

## Vulkan-only candidate

Laine and Karras, "Stratified Sampling for Stochastic Transparency", EGSR 2011: stratify the sample masks over the
pixel's samples within a layer and across layers. It needs per-fragment control of which samples of a pixel a
fragment covers, in a way the OpenGL fixed-function sample mask makes awkward at a few samples; evaluate it with the Vulkan
renderer's own coverage output after the line opens. Not measured; no claim here.
