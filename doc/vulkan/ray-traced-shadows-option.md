<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Ray-traced shadows: an option for the new renderer (2026-11-16)

This is a design note, not a plan. Owner decision of 2026-11-16 (recorded in this note): fix and optimise the OpenGL shadow-map path first, and keep ray tracing as a possibility for the new renderer of the Vulkan line. Nothing here changes the OpenGL viewer.

## Why it is interesting

With shadow maps, the cost of an avatar's shadow grows with the number of lights that see it. Every (avatar, light) pair needs its own drawing and scheduling; the memory belongs to each light's tiles and faces, which all its casters share. With many avatars under many lamps, that becomes a large share of the frame.

A crowd-shadow literature study of 2026-11-16 (kept outside the tree) made a rough estimate for 80 avatars, each in range of 4 lamps:
- about 11.5 ms of shadow drawing a frame when every pair is redrawn at full rate;
- about 1.9 ms with the planned OpenGL optimisations (a lighter shadow body, and the two most important lamps per avatar).

These are estimates scaled from offline benches, not measurements.

Ray-traced visibility changes the shape of that cost:

- **No shadow maps.** There are no tiles, resolution limits, atlas memory, guard bands, or cube faces for point lights. Each pixel asks whether anything lies between it and the light.
- **No redraw scheduling.** An avatar costs one acceleration-structure update a frame, however many lights see it, so its shadow follows it every frame instead of waiting for a redraw slot.
  - For scale, Kobrtek et al. (2022, FIT VUT Brno) report ray-traced shadows from one omni light at 1080p on an RTX 2080 Ti: 0.29 ms when the BVH is not rebuilt and 5.5 ms with a full rebuild.
  - That is a measurement of shadow rays and BVH builds in their test scenes, not of refitting skinned avatars. The avatar refit cost has to be measured.
- **Physical softness.** Rays to points on the emitter reproduce the area-light penumbra without the blocker-search approximation of PCSS, apart from the sampling noise (point 2 below). The softness rules the shadow-sharpness PR (#527) introduces on the OpenGL path would carry over:
  - every lamp counts as a 1 cm light at its anchor, scaled by the softness factor;
  - the softness factor `RenderShadowLightSize` (0.23 by default once #527 lands; devel still has 1.0) applies to every light: the sun and moon (0.53° × the EEP scale × the factor) and the lamps (1 cm × the factor).

## The open points

1. **API and hardware.**
   - Ray queries need ray-tracing hardware: NVIDIA RTX 20 series or newer, AMD RX 6000 or newer, Intel Arc.
   - They also need an API that exposes them. The new renderer's API is raw Vulkan through ash ([decisions-2026-11-17.md](decisions-2026-11-17.md)), which exposes ray queries.
   - The project's rule is that methods never switch at runtime, so this would be one of two things:
     - a method that requires that hardware;
     - a separate shadow method next to the shadow-map one, chosen in Preferences, as Legacy and Stochastic are today.
2. **Grain and denoising.**
   - A few rays per pixel give noisy soft shadows, which need denoising over frames. That brings back grain that follows the camera.
   - On OpenGL the owner accepted that for the sun and rejected it for lamps (2026-11-13), which is why the lamp shadows use a filtered shadow-map lookup.
   - Any design for the new renderer must be shown in-world before it is chosen.
3. **Many lights.**
   - With dozens of lamps per room, rays are not traced to every light for every pixel. Per-pixel light sampling is the known answer: ReSTIR, or Unreal's MegaLights.
   - MegaLights reports about 5.5 ms for all direct lighting with 941 lights on PS5 at 1080p ([MegaLights 2025](https://www.advances.realtimerendering.com/s2025/content/MegaLights_Stochastic_Direct_Lighting_2025.pdf)). That figure covers shading as well as shadows.
   - It is neither cheap nor noise-free.
4. **Acceleration-structure upkeep.** Skinned avatars need a refit every frame. Outfit and mesh changes need full rebuilds, and those have to be spread over frames.
5. **Alpha and tinted casters.**
   - Leaves, lace and glass need any-hit shading.
   - So far only the solid cutoff is decided: blended texels with alpha ≥ 0.98 block like solids.
   - How the lighter texels tint the light is still open on the shadow-map side too.

## Status

- An option only, for the Vulkan line's new renderer, after the Rust port and the renderer's base ([direction-2026-10-29.md](direction-2026-10-29.md)).
- To revisit when that renderer's lighting design starts. Aurora Viewer's renderer is the reference to compare with.
