<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Ray-traced shadows: an option for the Vulkan line (2026-11-16)

This is a design note, not a plan. The owner's order of 2026-11-16 is to fix and optimise the OpenGL shadow-map path first, and to keep ray tracing as a possibility for the Vulkan renderer. Nothing here changes the OpenGL viewer.

## Why it is interesting

Shadow maps make the cost of an avatar's shadow grow with the number of lights that see it. Every (avatar, light) pair needs its own drawing, scheduling and memory. With 80 avatars under many lamps that becomes most of the work. The 2026-11-16 crowd-shadow study (kept outside the tree) estimates about 11.5 ms at full rate for 80 avatars in range of 4 lamps each, and about 1.9 ms after the planned OpenGL optimisations. Ray-traced visibility removes most of it:

- **No shadow maps:** no tiles, resolution limits, atlas memory, guard bands or cube faces for point lights. Each pixel asks whether anything lies between it and the light.
- **No redraw scheduling:** an avatar costs one acceleration-structure refit a frame, however many lights see it. Shadows cannot lag or step. One published measurement: 0.29 ms for one omni light at 1080p without a BVH rebuild, 5.5 ms with a full rebuild, on an RTX 2080 Ti ([Kobrtek et al. 2022](https://www.fit.vut.cz/research/publication-file/12699/)).
- **Physical softness:** rays to points on the emitter reproduce the area-light penumbra exactly. The OpenGL path's rules, being introduced by the shadow-sharpness PR (`codex/shadow-sharpness-p2`), carry over unchanged:
  - every lamp is a 1 cm light at its anchor;
  - one softness factor, `RenderShadowLightSize` (default 0.23), applies to every light: sun and moon (0.53° × the EEP scale), and lamps (1 cm).

## The open points

1. **Hardware and the no-fallback rule.** Ray queries need ray-tracing hardware: NVIDIA RTX 20 series or newer, AMD RX 6000 or newer, Intel Arc. Under the project's rule that methods never switch at runtime, there are two options:
   - require that hardware for the method;
   - offer it as a separate shadow method next to the shadow-map one, chosen in Preferences, as Legacy and Stochastic are today.
2. **Grain and denoising.** A few rays per pixel give noisy soft shadows that need denoising over frames, which brings back camera-dependent grain. The owner accepts that for the sun and rejected it for lamps on OpenGL (2026-11-13). Any Vulkan design must show the result in-world before it is chosen.
3. **Many lights.** With dozens of lamps per room, rays are not traced to every light for every pixel. Per-pixel light sampling (ReSTIR, or Unreal's MegaLights: 5.5 ms for 941 lights on PS5 at 1080p, [MegaLights 2025](https://www.advances.realtimerendering.com/s2025/content/MegaLights_Stochastic_Direct_Lighting_2025.pdf)) is the known answer, and it is neither cheap nor noise-free.
4. **Acceleration-structure upkeep.** Skinned avatars need a refit every frame. Outfit and mesh changes need full rebuilds, which have to be spread over frames.
5. **Alpha and tinted casters.** Leaves, lace and glass need any-hit shading or the translucency rules decided for shadow maps (the solid cutoff 0.98, tint in proportion below it).

## Status

- Option only, on the Vulkan line, after the Rust port and the new renderer's base (see [direction-2026-10-29.md](direction-2026-10-29.md)).
- To revisit when the Vulkan renderer's lighting design starts. Aurora Viewer's direction (wgpu) is the reference to compare with.
