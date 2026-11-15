# Stochastic transparency in the Vulkan renderer: the grain-measuring resolve

Added 2026-11-14 with the OpenGL upgrade (S set back to 8 on 2026-11-15) of the transparency
([../oit/stochastic-transparency.md](../oit/stochastic-transparency.md)). The OpenGL viewer runs the resolve as three
fragment passes (compute dispatches stall the OpenGL frame and OpenGL has no async compute). The Vulkan renderer is where
the compute form belongs.

## The method to carry over

- Targets and masks as in OpenGL: S = 8 samples on the stochastic targets, coverage through the sample mask, the total alpha
  at every stochastic sample (not one sample), the alpha correction (total alpha over accumulated alpha) made at the
  composite.
- The resolve is Aurora Viewer PR 3663's three compute steps (`oit_stochastic.wgsl` `cs_resolve`, `cs_temporal`,
  `cs_smooth`; Gaalrauch, GPL-3.0-or-later), on this engine's history format:
  1. measure: one workgroup tile averages the samples once (all S = 8 samples of a Vulkan target are one pass, so the iterations of the OpenGL
     form, which exist only where a device holds fewer than 8 samples in a pass and which must be measured one by one and their moments combined,
     do not arise; a Vulkan device that holds fewer would have to do the same), with the grain (the standard error of the colour from the
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

## Vulkan-only candidate

Laine and Karras, "Stratified Sampling for Stochastic Transparency", EGSR 2011: stratify the sample masks over the
pixel's samples within a layer and across layers. It needs per-fragment control of which samples of a pixel a
fragment covers, in a way the OpenGL fixed-function sample mask makes awkward at a few samples; evaluate it with the Vulkan
renderer's own coverage output after the line opens. Not measured; no claim here.
