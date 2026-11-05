<!-- SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later -->
# Shadow model rework: soft shadows as the base (study and design)

Status: study and design, phase 1; design approved by the owner on 2026-11-05 (decisions in §14). Nothing here changes the shadows for users.
The branch `codex/indoor-shadow-study` (from `vkstorm-devel` d36fe19890) carries:

- this document;
- an offline model of the cascade fit, `scripts/shadows/cascade_fit_model.py`;
- a cost bench, `scripts/perf/bench_shadow_costs.py`;
- a group of DEVELOPMENT DIAGNOSTIC controls in Preferences > Graphics > Rendering 2.

It is not for merge as-is. The diagnostics are development hooks and must stay out of
`vkstorm-release` (see `scripts/tests/check_release_hooks.py`).

The owner asked for this:

> Indoors, shadows behave weirdly. This is true upstream and everywhere. They disappear when I move, change with camera angle... that's not how shadows behave RL.

The owner then widened the scope:

- Sun shadows are unstable outdoors too.
- There is dithering outdoors.
- PCSS off is not stock.
- Sunlight must go through transparent surfaces.
- The shadow model needs a complete rework, based on soft shadows and respecting EEP.
- It must not crash FPS.

Code references are file:line at `origin/vkstorm-devel` d36fe19890, without the diagnostic patch.
Measured numbers come from the NVIDIA GeForce RTX 5070 Ti (driver 621.01) in this machine.
Estimates are marked as such.

---

## 1. Summary

**Why shadows are unstable, indoors and out.** The cascades are refitted from scratch every frame to whatever the camera sees, and the fit depends on the camera in four ways:

1. **The split range follows the visible extents, ignoring occlusion.** `far_clip = clamp(2 × farthest visible point, 16, 512)` (pipeline.cpp:11444). Occlusion is off during the fit (11264), so anything inside the view frustum counts, even behind walls.
   - **Indoors:** whether anything is visible through the walls moves the four splits by up to 16×.
   - **Outdoors:** tilting the camera by 15° moves them from 21 m to 256 m.
   - **Effect:** the same floor point jumps between cascades whose texels differ 6–7× and whose bias differs 5×.
2. **No stable fit.** The light view is anchored at the camera (11574), and its "up" is the camera's at-axis (11380). The extents are a tight box of the receiver cloud.
   - Moving the camera slides the texel grid by fractions of a texel. Turning rotates it: a 45° yaw rotates the grid by up to 82°.
   - The result is shadow edges that crawl and swim.
3. **Ortho and perspective-warped projections switch per cascade, per frame** (11678, 11769). In the model, half of all camera yaws change at least one cascade's projection type.
4. **The bias is constant in each cascade's [0,1] depth.** In metres it therefore scales with the depth range the receivers happen to span, from 0.4 cm to 19 cm in the model. An altitude term (bindDeferredShader, 9720) multiplies it by 4.4 at 3000 m, which matters for skyboxes.
   - Contacts leak light or lose small shadows, and the leaks change with the view.

**What does not cause it.** Off-screen roofs and ceilings (hypotheses 1 and 2 of the brief) are **not culled or clipped**. The fit covers the receivers in light space, and casters project onto them. The cull frustum's near plane is moved 2 × RenderFarClip towards the light (11371, 11819). Casters in front of the projection's near plane are kept by `GL_DEPTH_CLAMP` (10853). Main-camera occlusion is off for the whole fit and for the shadow passes (11264, 10830). The offline model confirms this: an off-screen roof is inside the cull frustum of every cascade whose receivers it shades.

**Other findings:**

- **Glass casts dithered shadows.** Semi-transparent glass with alpha 0.6–0.88 casts a 50 % column-dithered shadow keyed to the shadow-map grid, so it crawls with the instability above. Below 0.6 it casts nothing (shadowAlphaMaskF.glsl:40-58).
- **Projector shadows depend on the camera.** Only 2 projectors cast, chosen from the camera's view, and they fade at a rate scaled by the camera's speed (11865).
- **The sun-to-moon switch pops** at the horizon (11364).

**PCSS off versus stock.** With PCSS off, the shadow path is byte-identical to Firestorm 7.2.5, and the offline bench confirms it bit for bit.

- The owner's jagged, dithered edges are stock `pcfShadow`'s screen-row jitter (shadowUtil.glsl:60). Stock relies on the light-map blur to hide it, and runs that blur only when SSAO is on.
- With PCSS on, the blur always runs. Hence "on looks right, off looks broken".
- Two settings-level differences also exist; see §6.

**Recommendation.** Build a new sun/moon/projector shadow model with soft shadows as its base. It is a **feature**: `codex/` branches from devel in stages, then integration PRs per line. The one exception is the bias altitude term, which is a small bug and could be a hotfix (§6.4). The model has:

- **Stable fitting:**
  - ortho cascades from bounding spheres, snapped to texels in a world-anchored light grid;
  - split distances that do not depend on what is visible;
  - per-cascade caster culling from the light;
  - no clearing of empty cascades.
- **PCSS everywhere**, with a well-filtered kernel and a denoiser that is correct by construction.
- **A bias in world units:** normal offset plus receiver-plane bias.
- **Coloured transmittance through transparent casters.**
- **EEP-driven light sizes and a cross-faded sun/moon switch.**

It replaces the perspective-warped cascades. Cascade caching pays for the larger, stable fits.

---

## 2. Performance: baseline and budget

The owner's hard requirement: *the design must be performant; it can't crash FPS because shadows are on.*

### 2.1 Baseline today (measured)

All measurements are offline GL benches with `GL_TIME_ELAPSED` medians, run in a hidden window (gl_test_context.py creates a window with style 0, which is never shown).

- **Sun light pass:** `scripts/perf/bench_shadow_pcss.py`, the real `sunLightF.glsl` and `shadowUtil.glsl` over a synthetic G-buffer, 4 cascades and 2 spots at the viewer's map sizes, 40 frames.
- **Light-map blur:** `scripts/perf/bench_shadow_costs.py`, the real `blurLightF.glsl`, 2 passes, RenderShadowGaussian kernel.
- **Caster rendering:** procedural boxes into the 4 cascade depth maps.

| GPU (5070 Ti) | 1920x1080 | 2560x1440 |
|---|---|---|
| Sun light pass, shadows off | 0 (the pass runs; no shadow taps) | 0 |
| Sun light pass, stock PCF, detail 2 (sun + spots) | 0.105 ms | 0.185 ms |
| Sun light pass, stock PCF, detail 1 (sun) | 0.055 ms | 0.116 ms |
| Sun light pass, PCSS on, detail 2 | 0.188 ms | 0.329 ms |
| Sun light pass, PCSS on, detail 1 | 0.079 ms | 0.150 ms |
| Light-map blur, full res (stock: SSAO on; PCSS: always) | 0.154 ms | 0.270 ms |
| Light-map blur, half res (proposal) | 0.035 ms | 0.054 ms |
| Caster raster, 4 cascades, per million triangles (proxy) | 0.32 ms | 0.33 ms |

The caster rows are a **proxy**: one draw call, no textures, no alpha-mask fetches. In-world caster cost is dominated by triangle count and by the CPU side. The CPU side is `updateCull` + `stateSort` for each of up to 6 shadow cameras, plus thousands of batches. The offline benches cannot see it; it must be measured in-world (§2.5).

**Weaker GPU class: not measured.** No second GPU is available. Zink is withdrawn on this line. A reduced-resolution proxy only rescales fill-bound passes. Every number for a "mid" class below is an **estimate**: the 5070 Ti figures × 3 for bandwidth- and ALU-bound full-screen passes, and × 2.5 for geometry-bound caster passes (an RTX 3060 / RX 6600 class card).

### 2.2 Today's total, PCSS on, default detail 2 (the budget ceiling)

At 1080p on the 5070 Ti, the GPU cost is:

| Component | Cost |
|---|---|
| Sun pass | 0.188 ms |
| Blur | 0.154 ms |
| Casters | C_today |
| **Total** | **≈ 0.342 ms + C_today** |

C_today is the scene's caster cost, which is 4 cascades × caster triangles + 2 spots. With a typical SL scene's 1–3 Mtri inside the cascades, it is ≈ 0.3–1.0 ms on the proxy (0.32 ms per Mtri for 4 cascades), scene-dependent.

The new model's default tier must cost ≤ this total, preferably less, **including transmittance**.

### 2.3 Budget per component, new model (1080p, 5070 Ti measured basis; mid class ≈ ×3 / ×2.5)

| Component | Low | Medium (default mid) | High (default high) | Ultra | How |
|---|---|---|---|---|---|
| Caster raster (GPU), relative to today's C_today | ≤ 0.5 × | ≤ 0.8 × | ≤ 1.0 × | ≤ 1.3 × | sphere fit +~40 % area, offset by caching cascades 2–3 (§9.3) and light-space culling |
| Fit + cull + stateSort (CPU, main thread) | ≤ today | ≤ today | ≤ today | ≤ today | cached cascades skip cull and stateSort on their off frames |
| Filtering (sun + spots) | 0.10 ms | 0.15 ms | 0.19 ms | 0.25 ms | PCSS taps by tier, early-outs |
| Transmittance (casters + fetch) | 0 (off) | ≤ 0.10 ms | ≤ 0.12 ms | ≤ 0.15 ms | only cascades that contain transparent casters |
| Denoise / blur | 0.04 ms | 0.04 ms | 0.04 ms | 0.15 ms | half-res joint-bilateral (today's blur measured at half res: 0.035 ms); full res at Ultra (0.154 ms) |
| Screen-space contact shadows | – | – | – | 0.15 ms (est.) | Ultra only |
| **Total excluding casters, 1080p** | **≈0.14 ms** | **≈0.29 ms** | **≈0.35 ms** | **≈0.70 ms** | |
| Today excluding casters, 1080p | stock PCF + SSAO blur 0.259 ms; PCSS on 0.342 ms | | | | |
| Today excluding casters, 1440p | stock PCF + SSAO blur 0.455 ms; PCSS on 0.599 ms | | | | |

The 1440p totals scale with the pixel count, about 1.75×: Low ≈ 0.25, Medium ≈ 0.50, High ≈ 0.60, Ultra ≈ 1.2 ms. For the mid class, multiply by ≈ 3.

**Caster cost is geometry-bound, not resolution-bound.** In the raster proxy, half-resolution maps cost the same as full ones: 0.32 against 0.32 ms per Mtri for 4 cascades at 1080p, and the same holds for 1440p-sized maps. So lowering map resolution saves memory and filtering, but hardly any caster time. **Caster savings must come from rendering fewer casters into fewer cascade updates**: caching, light-space culling, shadow distance. That is why caching (S5) is part of the default tier.

The shadow-map memory per tier is in §9.6.

**The FPS drop from turning shadows on must be stated per tier at the gate (§12).** The target at the default tier is no larger than today's PCSS-on drop, measured in-world with Frame Profile on the reference scenes (§11).

### 2.4 Where the savings come from, and what scales badly

**Savings:**

- **Cascade caching.** With a world-snapped, stable cascade, the map stays valid while the camera moves. Re-render it only when it scrolls by more than a margin, or every N frames: cascade 2 every 2nd frame, cascade 3 every 4th, staggered so that at most one far cascade renders per frame. This saves ≈ 35–45 % of the far-cascade caster cost and their CPU cull/stateSort.
  - A later step can split static and dynamic casters: cache static casters, re-render avatars and moving objects on top.
- **Light-space culling per cascade.** The cull frustum stays tight in xy, as today, and the depth range extends only towards the light. Today's warped cascades sometimes cull less.
- **Cheaper filtering where the penumbra is small.** PCSS falls back to one 2×2 hardware PCF tap where the blocker search finds nothing (already done) and where the penumbra is under a texel (new).
- **Blocker-search early-outs.** If the search finds all-lit or all-shadow, stop. Most pixels are either fully lit or fully in umbra.
- **Half-resolution denoise.** Measured: 0.06 ms against 0.16 ms at 1080p.
- **Transmittance only where transparent casters exist.** The pass runs only for cascades whose cull result has PASS_ALPHA casters. The colour target is cleared once and is not sampled when empty: a per-cascade "has transmittance" flag in a uniform.

**What scales badly, and the mitigation for each:**

- **Crowded sims (many avatars).** Avatars are rigged, and each avatar is re-skinned per shadow camera.
  - Mitigation: avatars render only into cascades 0–1 unless they are large. Avatars beyond the cascade-1 range cast only at Medium and above. Respect the existing avatar complexity limits (`isTooSlow`, impostors already cast nothing; lldrawpoolavatar.cpp:303).
- **Large draw distances.** Today, far_clip follows the visible extents up to 512 m.
  - Mitigation: the shadow distance becomes its own setting with a per-tier default (Low 64 m, Medium 128 m, High 192 m, Ultra 256 m). It never depends on what is visible, and is capped by the draw distance. Beyond it there are no sun shadows, with a fade.
- **Many projector lights.** Today only 2 cast. Keep 2, but choose them stably (§8.4).
  - Each extra projector costs one caster pass; any increase is per tier and gated.
- **Transmittance in alpha-heavy sims** (forests of alpha-blend leaves, particle-like decor).
  - Mitigation: alpha-blend faces with a mask-like alpha distribution are classified once per face, and those go to the alpha-mask path, not to transmittance. Transmittance is capped per cascade by a triangle budget, and the cap falls back to coverage-only.

### 2.5 In-world measurement protocol (the gate for every stage)

1. Use Develop > Render Tests > Frame Profile. It writes JSON with per-program GPU times. `scripts/perf/profile_csv.py` and `profile_pretty.py` convert it. Read the shadow programs, the sun program, the blur and the frame total.
2. Use the reference scenes of §11: a sandbox ground parcel, a skybox at 2500 m, a dense forest, and a crowded club. Test at 1920x1080 and 2560x1440, with a fixed camera from a saved camera position, 3 runs each.
3. Run these cases:
   - shadows off;
   - detail 1 stock PCF;
   - detail 2 stock PCF;
   - detail 2 PCSS;
   - each new tier.
4. For the CPU, take the frame time with the GPU unbound: set a low resolution scale and compare the frame time with and without shadows.

---

## 3. Findings: why sun shadows are unstable (ranked)

Ranked by contribution, indoors and outdoors. The evidence is the offline model `scripts/shadows/cascade_fit_model.py`, a line-by-line port of the fit (§3.10), plus code reading. In-world confirmation is the owner's bisection (§10).

### 3.1 Split distances follow the visible extents, ignoring occlusion: MAJOR, everywhere

- **Code:**
  - pipeline.cpp:11264, `LLDisableOcclusionCulling` for the whole fit;
  - 11401, `getVisiblePointCloud(main_camera, …)`;
  - getVisibleExtents:2615 → `LLOctreeCullVisExtents` (llspatialpartition.cpp:1151), whose occlusion check is off because `sUseOcclusion` is 0;
  - 11443-11466, near/far and splits.
- **Mechanism:**
  - The near split is the nearest visible point, clamped to 0.01–4 m.
  - The far split is twice the farthest visible point, clamped to 16–512 m and capped by the draw distance.
  - The four splits are `near + range × ((i+1)/4)^3`, and the first is bumped by 1.25.
  - Anything inside the view frustum counts, including geometry hidden behind walls and the terrain to the horizon.
- **Evidence from the model** (sun at 35° elevation; maps 1920×1080):
  - **Indoors on the ground:** the outdoor scene behind the walls sets far = 256 m, with splits 5.3 / 32 / 108 / 256 m. The same room as a skybox at 2500 m with nothing outside gets far = 16–20 m, with splits 0.6 / 2.3 / 7 / 17 m.
  - **Outdoors, pitch only:** at −45° far = 21 m (splits 0.7 / 2.9 / 9.2 / 21 m). At −30° far = 256 m (5.3 / 32 / 108 / 256 m). A 15° tilt moves every split 7–12×.
  - **A fixed floor point 6.5 m away**, camera walking 1 m: it jumps from cascade 1 (texel 4.5 cm, bias 18.5 cm) to cascade 0 (texel 0.75 cm, bias 3.5 cm). That is a 6× resolution pop and a 5× bias pop.
  - In the skybox room, turning in place moves the same point between cascades 0, 1 and 2 (texel 0.10 / 0.33 / 0.67 cm, bias 3 / 9 / 13 cm), and at one yaw the cascade is cleared.
- **What the owner sees:**
  - Small shadows (chair legs, frames, plants, fingers) appear and vanish as their cascade's texel crosses their size.
  - Light leaks at contacts open and close.
  - Everything pops when walking or turning.
- **Verdict: CONFIRMED.** This is the largest single contributor, indoors and outdoors.

### 3.2 No stable fit: the grid slides and turns with the camera. MAJOR, everywhere

- **Code:**
  - pipeline.cpp:11574, `view[j] = look(camera.getOrigin(), lightDir, -up)`, anchored at the camera;
  - 11380, `up = camera.getAtAxis()`, switched to the up-axis when |at·L| > 0.75;
  - the ortho box from the receivers' light-space min/max (11678);
  - no rounding of sizes, no snapping of the origin.
- **Mechanism:**
  - The texel grid's origin is the camera, so every camera translation slides the grid by a non-integer number of texels. The shadow is resampled at a new phase each frame.
  - "Up" follows the camera, so turning rotates the grid about the light axis.
  - The box size follows the receivers, so the texel size changes continuously.
- **Evidence from the model:**
  - Walking in 4 cm steps, a fixed world point moves through the cascade-0 grid with fractional texel phases 0.50, 0.25, 0.01, 0.76, 0.51, 0.27. Every frame it lands somewhere else in its texel.
  - Turning from yaw 0° to 10° / 20° / 45° / 90° rotates the grid by 1.9° / 3.8° / 8.5° / 90.6°.
  - In the yaw tables, cascade 0's texel size varies by 1.5× (0.68–1.07 cm) while turning in place.
- **What the owner sees:** shimmering, crawling stair-step edges that move when the camera moves, even when nothing in the world moves. This is worst with stock PCF, whose `floor()` snapping makes each texel a hard step (§4.1).
- **Verdict: CONFIRMED.**

### 3.3 Ortho and warped projections switch per frame: MAJOR

- **Code:** pipeline.cpp:11678 (`mShadowError > RenderShadowErrorCutoff` → ortho) and 11769 (`fovx > RenderShadowFOVCutoff` → ortho), otherwise the perspective "warp" with its origin from a least-squares line fit.
- **Mechanism:**
  - The choice depends on how the receivers' point cloud is spread in light space, so it depends on the view.
  - The two projections distribute texels very differently.
- **Evidence from the model:** turning in place in 30° steps outdoors, the mode string per cascade varies: `oooo`, `owoo`, `owww`, `wwoo`. Seven of the twelve yaws differ from the neighbouring yaw.
- **Verdict: CONFIRMED.** It adds discrete pops on top of 3.1–3.2. The warp cannot be made stable: its origin and fov move with the camera by construction.

### 3.4 The bias is in depth units, so in metres it follows the cascade depth range and the altitude: MEDIUM-HIGH

- **Code:**
  - bindDeferredShader, pipeline.cpp:9720: `shadow_bias = RenderShadowBias (−0.002) + RenderShadowBiasError (−0.007) × |camera z| / 3000`;
  - shadowUtil.glsl:58-59, `stc.z += shadow_bias × 2`, in [0,1] depth of the cascade.
- **Mechanism:**
  - [0,1] depth spans the cascade's light-space depth range, so the bias in metres is 2 |bias| × range.
  - The range comes from the receivers in the frustum section (3.1), so it changes with the view.
  - The altitude term multiplies the bias by 1.5× at 500 m, 3.3× at 2000 m and 4.4× at 3000 m.
- **Evidence from the model:**
  - Bias per cascade is 0.4–6.3 cm in cascade 0 and up to 18.5 cm in cascade 1, depending only on the view.
  - At z = 3000 m, a 100 m deep cascade gets 1.8 m of bias.
  - A wall's shadow on the floor then starts ≈ bias × cos(elevation) away from the wall: a lit strip under walls of 3–15 cm on the ground, more in skyboxes.
- **What the owner sees:** light leaking under walls and at furniture contacts, shadows detaching from feet, and small casters losing their shadow. All of it varies with the view.
- **Verdict: CONFIRMED.**
  - The altitude term is unphysical, because the shadow maps are camera-relative and their precision does not depend on altitude. It is a separate small bug (§6.4).

### 3.5 Empty cascades are cleared, and points change cascade: MEDIUM

- **Code:** pipeline.cpp:11538-11559.
- **Mechanism:** a cascade with no receivers in its frustum section is cleared and skipped. When the receivers come back, the fit restarts from nothing.
- **Evidence from the model:** in the skybox room facing a wall, cascades 3 (and 2) are cleared at some yaws (`ooo-`, `oo--`), and the near split jumps from 0.25 m to 1.84 m.
- **Verdict:** CONFIRMED as a contributor to popping, though smaller than 3.1. On its own, a cleared cascade has no receivers to shadow, so nothing visible is lost that frame. The pop comes at the transition.

### 3.6 Dithered shadows from semi-transparent glass: MEDIUM indoors (windows)

- **Code:**
  - shadowAlphaMaskF.glsl:40 (alpha < `ALPHA_BLEND_CUTOFF` 0.598 → discard);
  - shadowAlphaMaskF.glsl:54 (alpha < 0.88 → discard every other shadow-map column, `fract(0.5 × floor(target_pos_x / post_pos.w)) < 0.25`);
  - pipeline.cpp:7526 `renderAlphaObjects` binds this program for blended faces.
- **Mechanism:** glass with alpha 0.6–0.88 casts a striped 50 % shadow keyed to the shadow-map columns. The columns move with the grid (3.2), so the stripes crawl. With PCF the stripes average to a mottled half-shadow.
- **Verdict: CONFIRMED** by code. It is a strong candidate for "weird" indoor shadows behind windows. See §7.

### 3.7 Projector shadows are chosen from the camera and fade with camera speed: MEDIUM where projectors exist

- **Code:**
  - only `mShadowSpotLight[2]` exist;
  - targets come from `getSpotLightPriority` while the visible lights are gathered (pipeline.cpp:10592-10610);
  - `fade_amt` scales with the camera's velocity statistic (11865-11880).
- **Mechanism:** when a third projector becomes higher-priority from a new viewpoint, one of the two fades out. The fade is faster the faster the camera moves.
- **Verdict:** CONFIRMED by code. Projector shadows "disappear when I move".

### 3.8 Objects are created only when the main camera sees them: LOW to MEDIUM, transient

- **Code:**
  - llvocache.cpp:1005 (`LLVOCachePartition::cull` returns early for any camera but `CAMERA_WORLD`);
  - UseObjectCacheOcclusion (default on);
  - `isAnyVisible` is disabled (llvocache.cpp:566-568 `#if 0`), so objects are no longer killed when out of view.
- **Mechanism:** after a teleport or login, cached objects outside the view and outside the rear sphere (`SceneLoadMinRadius` 32 m, `SceneLoadRearPixelThreshold`) are not instantiated. A roof never looked at may not exist yet, so it casts no shadow until it is first seen.
- **Verdict:** POSSIBLE, transient (until each object is first seen). Bisection step B7 checks it with the existing UseObjectCacheOcclusion setting.

### 3.9 Sun/moon switch pop: LOW (twice a day)

- **Code:** pipeline.cpp:11364. The caster direction is the sun if `getIsSunUp()`, otherwise the moon, and it switches in one frame.
- See §8.2 for the EEP-driven cross-fade.

### 3.10 Rejected: off-screen occluders outside the shadow frustum, or culled (brief hypotheses 1–2)

- **Code:**
  - shadow_near_clip, pipeline.cpp:11366-11372, a plane 2 × RenderFarClip towards the light, replaces each shadow camera's near plane (11819);
  - the cull is `LLOctreeCullShadow` with that frustum (llspatialpartition.cpp:1134, 1464);
  - the casters render with `GL_DEPTH_CLAMP` (10853, `renderShadow(…, true)`);
  - occlusion is off for the fit and the shadow passes (11264, 10830, 10915).
- **Mechanism:**
  - The ortho box covers the receivers' light-space xy, and a caster that shades a receiver has the same light-space xy.
  - Its depth may be in front of the projection's near plane; depth clamp rasterises it at depth 0, so it still occludes.
  - The perspective warp bounds x and the warp axis y independently of the light-axis z, so the same holds there.
- **Evidence from the model:** a camera pitched −35° in the room, roof not in the main frustum. The roof is inside the cull frustum of cascades 0–2, which hold the floor that it shades; cascade 3 holds no floor under it. In every cascade the roof is nearer the light than the near plane, so it is depth-clamped, not clipped.
- **Verdict: REJECTED** as a cause. One caveat: a draw distance below ≈ 8 m puts the near plane at 2 × draw distance, closer than tall roofs. That is unrealistic.
- **The new design still culls from the light**, because the warp is being removed and the fit becomes scene-based (§9.1).

### 3.11 Other items checked

- **Main-camera occlusion culling in the shadow pass:** none. sUseOcclusion is 0 in generateSunShadow and renderShadow, and the `doOcclusion(shadow_cam)` branch at 10915 never runs.
- **LOD:** shadow passes reuse the LOD chosen for the main camera. Distance-based LOD switching of meshes changes caster shape as you walk; this is a minor contributor, and stock. Not addressed beyond noting it.
- **Water clip in the shadow cull:** `isWaterClip()` is false with transparent water (updateCull:2668). No effect.
- **Avatars:** avatars that are impostors, too slow or invisible cast nothing (lldrawpoolavatar.cpp:290). Rigged alpha casts as below (§7.1).
- **PCSS blocker search range:**
  - The search is limited to blockers within 4 m (cascade 0) and 8 m (cascade 1), shadowUtil.glsl:218.
  - Beyond that, a roof 3 m above a floor still darkens, but its penumbra is not widened. Correct for the sun (2.8 cm penumbra at 3 m), but a limitation for a high EEP sun_scale.
  - Cascades 2–3 are fixed-radius.

---

## 4. Dithering: sources, how to see each one, and how the new model removes it

### 4.1 Stock pcfShadow's screen-row jitter (PCSS off)

- **Code:** shadowUtil.glsl:60, `stc.x = floor(stc.x × res + fract(pos_screen.y × res.y)) / res`.
- The sample snaps to a texel column with a per-screen-row phase, which turns each map texel into a dithered stair-step. Stock hides it only with the light-map blur, which stock runs only with SSAO (§6).
- **Diagnostic:** "Stock PCF: no jitter" (flag 4) removes the jitter. "Light-map blur: Always on / Always off" isolates the blur.

### 4.2 PCSS's per-pixel kernel rotation

- **Code:** pcssNoise / pcssRotation, shadowUtil.glsl:128, 167. Interleaved gradient noise rotates the 12-tap Poisson disc.
- It trades banding for high-frequency noise. The blur (13 taps each way, edge-stopping on plane distance, blurLightF.glsl) removes most of it, but not where the edge-stop rejects neighbours: thin geometry, silhouettes, grazing surfaces. There the noise shows.
- **Diagnostic:** "PCSS: no kernel rotation" (flag 1) shows the unrotated kernel. Banding replaces noise; anything left is not from the rotation. "Light-map blur: Always off" shows the raw noise.

### 4.3 Texel aliasing

- **Sources:** resolution (§3.1), the warp (§3.3) and the sliding or turning grid (§3.2).
- **Diagnostic:** "Show shadow texels" (flag 2) replaces the sun shadow by a checkerboard of 8×8 map texels, darker per cascade. It shows:
  - the texel density everywhere on screen;
  - the cascade boundaries;
  - the grid crawling when you move.
- Together with "Lock light up" and "Snap cascades to texels", the crawl should stop.

### 4.4 Bias acne

- The depth bias is negative (towards the light), so acne is rare. Leaks are the usual artefact (§3.4).
- Where the bias in metres becomes small (shallow cascades), self-shadowing acne appears on surfaces at grazing light.
- **Diagnostic:** "No altitude bias term" isolates the altitude part. The log (§10) prints the bias in cm per cascade.

### 4.5 Cascade blend

- sampleDirectionalShadow blends neighbouring cascades linearly over the overlap (shadowUtil.glsl:380 ff.). There is no dithered blend.
- It does show as a band where the two cascades disagree (resolution or bias), which "Show shadow texels" makes visible.

### 4.6 Temporal instability

All of §3.1–3.3: the shadow is a different sampling of the world each frame. "Freeze cascade fit" removes it entirely. If the dither and crawl stop while frozen, the fit is the cause, not the filter.

### 4.7 How the new model removes the dither, rather than hiding it

1. **Stable fit (§9.1).**
   - The grid is fixed in the world, so a static scene samples the same texels every frame and nothing crawls.
   - Cascade sizes are quantised, so the density changes only in rare steps.
2. **Enough density.**
   - Cascade 0 is sized for the near field (≈ 0.5–1 cm texels at 1080p, §9.6).
   - Cascade selection uses a fixed shadow distance, not the visible extents.
3. **A well-filtered kernel.**
   - PCF with bilinear-weighted taps (hardware 2×2 PCF per tap, as PCSS stage 1 does) over a disc sized in world units.
   - No `floor()` snapping. The noise rotation is per pixel but fixed in screen space and stable over time. The texels underneath no longer move, so the pattern does not crawl.
4. **A denoiser that is correct by construction.**
   - A joint-bilateral filter on the penumbra term only: blur the shadow factor, never the normals or albedo.
   - Weights from depth and normal at half resolution.
   - The kernel radius is set from the same penumbra width that sized the PCF, so it never blurs across a hard contact.
   - The blur is part of the shadow model, so it never depends on SSAO.

---

## 5. Diagnostics added (DEVELOPMENT DIAGNOSTIC, not for release)

### 5.1 Where they are

The diagnostics are in Preferences > Graphics > Rendering 2 > "Shadow diagnostics – DEVELOPMENT DIAGNOSTIC, not for release", below "Soft shadows (PCSS)".

- Every control is a non-persistent `ShadowDiag*` setting (settings.xml, Persist 0), read live through `LLCachedControl`. There is no restart and no shader reload.
- All controls off means stock behaviour; the shader path with the flags off is bit-identical to the base in the bench.
- They act on the main view only; reflection-probe snapshots are untouched.

| Control | Setting | What it does | Decides |
|---|---|---|---|
| No caster culling | ShadowDiagNoCasterCull | the sun shadow cameras' six cull planes move 100 km out | 3.10 (culling) |
| Extend to light (m) | ShadowDiagExtendToLight | the receiver cloud is duplicated N m towards the light, which moves each cascade's near plane | 3.10 (depth clamp) |
| Freeze cascade fit | ShadowDiagFreezeFit | keeps the last fit (splits, matrices, cull cameras) while the camera moves | 3.1–3.3, 4.6 |
| Never clear empty cascades | ShadowDiagKeepEmptyCascades | an empty cascade is fitted to its frustum section | 3.5 |
| Fixed split range (m) | ShadowDiagFixedFar | split range = camera near … N m, independent of the visible extents | 3.1 |
| Lock light 'up' to north | ShadowDiagLockUp | the light view's up is world north | 3.2 (rotation) |
| Orthographic cascades (no warp) | ShadowDiagForceOrtho | no perspective warp | 3.3 |
| Snap cascades to texels | ShadowDiagSnapTexels | ortho; extents rounded up to powers of two metres; origin moved in whole texels of a world-anchored grid | 3.2 (translation); a preview of §9.1 |
| No altitude bias term | ShadowDiagNoAltitudeBias | drops RenderShadowBiasError × altitude | 3.4 |
| Show shadow texels | ShadowDiagShowTexels | 8×8-texel checkerboard per cascade instead of the shadow | 4.3, 3.1 |
| Stock PCF: no jitter | ShadowDiagNoPCFJitter | PCSS off: no screen-row jitter | 4.1 |
| PCSS: no kernel rotation | ShadowDiagNoKernelRotation | PCSS on: fixed kernel | 4.2 |
| Light-map blur | ShadowDiagLightMapBlur | Stock / Always on / Always off | 4.1, 4.2, §6 |
| Log cascade fit (1/s) | ShadowDiagLog | once a second, "ShadowDiag" log lines: splits, mode, extents, texel cm, depth m, bias cm | all |
| Object cache occlusion (stock on) | UseObjectCacheOcclusion (existing) | off: cached objects hidden from the camera are created too | 3.8 |

### 5.2 Existing tools

- Develop > Render Metadata > Shadow Frusta draws the shadow frusta. While it is on, the stored frusta stop updating; that is how it works.
- There is no shadow-map display in this viewer's menus. "Show shadow texels" serves that purpose.

### 5.3 Code

- pipeline.cpp: `generateSunShadow`, `bindDeferredShader`, `renderDeferredLighting`; marked `<Vulkanstorm> DEVELOPMENT DIAGNOSTIC`.
- shadowUtil.glsl: `uniform int shadow_diag`; flags 1 / 2 / 4.
- settings.xml.
- panel_preferences_graphics1.xml.

---

## 6. PCSS off is not "stock": audit against Firestorm 7.2.5

The base is upstream `8ffe70659f`, which `vkstorm-release` was built on (Firestorm 7.2.5 plus upstream to that commit, branch-layout notes). Diffs are `git diff 8ffe70659f origin/vkstorm-devel`.

### 6.1 Shadow path differences

| # | Difference | file:line | Gated by PCSS? | Visible effect with PCSS off |
|---|---|---|---|---|
| 1 | PCSS functions, PCSS branches in sampleDirectionalShadow / sampleSpotShadow | shadowUtil.glsl:96-379, 409-541 | `#if defined(PCSS_SHADOW)` | none: the non-PCSS paths (pcfShadow, pcfSpotShadow, both sample functions) are byte-identical; bench: "tree (PCSS off) equals base bit for bit" |
| 2 | PCSS build of shadowUtil only for gDeferredSunProgram | llviewershadermgr.cpp: loadBasicShaders, `use_pcss_shadows()`; sun program permutation | `RenderShadowPCSS && RenderShadowDetail > 0` | none. The permutation is in `mDefines`, which `unload()` clears on every reload; `mFragmentShaderObjects` is cleared in setShaders (llviewershadermgr.cpp:718); the binary cache key hashes the defines (llglslshader.cpp:2056) |
| 3 | Light-map blur also without SSAO | pipeline.cpp:9884-9887 | yes: with PCSS off, the condition is stock `RenderDeferredSSAO && !gCubeSnapshot` | none |
| 4 | shadowDepth0/1/4/5 fetch units and the sampler object | pipeline.cpp:9861-9872 (bindShadowDepthFetch), llrender.cpp (`bindSampler`, `refreshState`) | `RenderShadowPCSS && !gCubeSnapshot && GL ≥ 3.3` | none. The sampler is bound to the shadowDepth units only, which no other program has, and is unbound right after the sun draw. With PCSS toggled off at runtime, nothing binds a sampler; `refreshState` rebinds only a non-zero `mCurrSampler`. **No stale sampler can remain on a shadowMap unit** |
| 5 | Sun light size uniform | pipeline.cpp:9849-9856 | yes | none |
| 6 | Stochastic transparency's alpha-only variants: no `HAS_SUN_SHADOW` | class2/deferred/alphaF.glsl:188, pbralphaF, class3/materialF | RenderAlphaSortMethod = 1 | none on shadows: the shaded accumulation pass uses the stock programs with the shadow maps bound (lldrawpoolalpha.cpp, `bindDeferredShaderFast`). The shadow pass for alpha casters (renderAlphaObjects) is unchanged |
| 7 | GLTF depth pre-pass gets an explicit depth-write state in the stock path | lldrawpoolalpha.cpp, forwardRender | no | GLTF scenes only, not shadows |
| 8 | Shadow-map rendering: generateSunShadow, renderShadow, splits, bias, polygon offset, resolution | pipeline.cpp | – | **no difference** from the base in these functions |
| 9 | Water, probes, alpha forward consumers of shadows | lldrawpoolwater.cpp, llreflectionmapmanager.cpp, class2 alphaF | – | none (llreflectionmapmanager.cpp: two integer-cast fixes only) |

### 6.2 Settings and feature table differences

| Setting | Firestorm 7.2.5 | Ours | Effect on shadows |
|---|---|---|---|
| RenderShadow* (all, including Bias, Offset, BlurSize, SplitExponent, ResolutionScale, NearDist) | – | identical | none |
| RenderDeferredSSAO | 1 in settings; 1 in High / HighUltra / Ultra | identical | none, but see 6.3 |
| **RenderShadowPCSS in featuretable.txt** | – | `list all` 1; **Ultra 1**; others 0 | **picking the Ultra preset or moving the quality slider to Ultra turns PCSS back on**; a GPU-change feature reset can too. Owner check: confirm PCSS is really off after any preset change |
| RenderGLContextCoreProfile | 0 (compatibility) | **1 (core)** | no shadow code depends on it (llimagegl.cpp only); not a cause |
| CameraOffsetRearView / FocusOffsetRearView | −3,0,0.75 / 1,0,1 | **−2.25,0,−0.2 / 1,0,0.2** | the default third-person camera is lower and closer. The cascade fit depends on the camera (§3), so the same scene gets different splits and texel densities than stock. Not a code difference, but a visible one |
| RenderAlphaSortMethod | – | 1 (stochastic) | not on sun-shadow casting (6.1 #6) |

### 6.3 The owner's report, explained

- **Report:** PCSS off gives blocky, jagged, dithered stair-step edges on the skirt, top and arms (avatar self-shadowing, outdoors). PCSS on is smooth.
- **Cause:** that is stock `pcfShadow` (4.1). It is unblurred when SSAO is off, because stock runs the light-map blur only with SSAO, while PCSS runs it always.
- With SSAO off, "PCSS off" is exactly Firestorm 7.2.5 with SSAO off.
- **In-world check:** with PCSS off, set "Light-map blur: Always on". The edges become stock-with-SSAO.
- If SSAO is already on and the edges are still jagged, the remaining differences are:
  - the camera offsets (6.2), which change cascade 0's density on the avatar;
  - the instability of §3 (crawl), which stock shares.

  Compare using "Show shadow texels" on the avatar.
- **Runtime toggle versus a session started with PCSS off:** identical by code (6.1 #2–#4). After PCSS is turned off, the reload unloads every program (clearing defines), clears the fragment objects, and rebuilds without the PCSS key. LLPipeline::RenderShadowPCSS is refreshed through `connectRefreshCachedSettingsSafe`, and the blur gate reads it per frame.

### 6.4 Hotfix candidates found by the audit

- **Bias altitude term (§3.4).**
  - RenderShadowBiasError × |z| / 3000 has no basis, because the maps are camera-relative. It makes skybox shadows leak 3–4× more.
  - It is stock behaviour, so removing it changes the look from stock. It is a decision for the owner (§14), not an automatic hotfix.
- **featuretable re-enabling PCSS at Ultra.** Intended at the time, but it surprises a user who turned PCSS off and then changes preset. A decision (§14).

---

## 7. Sunlight through transparent surfaces

### 7.1 Today (file:line)

| Caster | Program | Rule | Notes |
|---|---|---|---|
| Opaque (simple, bump, shiny, material, PBR) | gDeferredShadowProgram, shadowF.glsl | full | pipeline.cpp:10838-10900 |
| Alpha-mask (legacy) | gDeferredShadowAlphaMaskProgram, shadowAlphaMaskF.glsl | alpha < minimum_alpha (the material's cutoff) → none | renderMaskedObjects |
| Fullbright alpha-mask | gDeferredShadowFullbrightAlphaMaskProgram | as above, no vertex alpha | |
| **Alpha-blend (legacy)**, static and rigged | gDeferredShadowAlphaMaskProgram with `setMinimumAlpha(ALPHA_BLEND_CUTOFF = 0.598)` | **alpha < 0.598 → no shadow; 0.598–0.88 → 50 % column dither; ≥ 0.88 → full** | pipeline.cpp:251, 7526-7597; shadowAlphaMaskF.glsl:40-58 |
| **PBR alpha-blend**, static and rigged | gDeferredShadowGLTFAlphaBlendProgram, pbrShadowAlphaBlendF.glsl | **alpha < 0.05 → none; 0.05–0.88 → 50 % dither; ≥ 0.88 → full**. No minimum_alpha test, although the cutoff is set | pbrShadowAlphaBlendF.glsl:41-52 |
| PBR alpha-mask | gDeferredShadowGLTFAlphaMaskProgram | alpha < cutoff → none | pbrShadowAlphaMaskF.glsl:40 |
| Trees / grass | gDeferredTreeShadowProgram | alpha < 0.598 → none | treeShadowF.glsl:41 |
| Avatar alpha-blend / alpha-mask (system avatar) | avatarAlphaShadowF / avatarAlphaMaskShadowF | < 0.05 → none; < sMinimumAlpha (0.2) → 50 % dither; else full | lldrawpoolavatar.cpp:71, 247 |
| Water surface | – | not a caster (the water pool has no shadow pass) | |

**The result:**

- Legacy glass casts nothing or a striped half-shadow.
- PBR glass with alpha 0.1 casts a striped 50 % shadow.
- No colour.
- The legacy and PBR rules disagree.

### 7.2 Options for partial, coloured transmittance

**Model.** A transparent face with opacity α and colour c transmits T = (1 − α) · (1 + α c) per channel.

- 1 at α = 0 and 0 at α = 1.
- A black 50 % fabric gives 0.5.
- A white 50 % fabric gives 0.75: some light scatters through.
- A blue glass with α 0.3 and c = (0.2, 0.4, 1.0) gives (0.74, 0.78, 0.91).
- Layers multiply.

The mapping is a proposal and a decision for the owner (§14). The alternative is the plainer T = 1 − α(1 − c), but it lets an opaque white face transmit fully.

| | A. Coloured transmittance map | B. Stochastic shadow maps (Enderton 2010) | C. Deep / Fourier opacity maps |
|---|---|---|---|
| Idea | after the opaque depth, render transparent casters with depth test (no write) into an RGBA8 target per cascade with multiplicative blending: rgb = Π T_i; a = nearest transparent depth (min blend into a second R16 target) | transparent casters write depth with stochastic coverage (hashed alpha per texel, or MSAA with a sample mask from alpha, like our stochastic transparency); PCF over the map gives fractional shadow | per texel, a function of opacity along depth (4–8 layers or Fourier coefficients) |
| Colour | yes | no (grey only; coloured needs 3 maps) | grey; colour × 3 |
| Order | order-independent (product) | order-independent | order-independent |
| Transparent casters behind opaque ones | culled by the depth test | correct | correct |
| Receivers between two transparent layers | gets the product of all layers in front of the nearest opaque caster, including those behind the receiver unless the stored nearest depth gates it: **one-layer depth gate**, approximate for receivers between layers | correct in depth | correct in depth (volumetric) |
| PCSS | the depth map is unchanged, so the blocker search and penumbra are unchanged; the transmittance is filtered with the same disc (bilinear RGBA8 taps, 4–8 of the 12) and multiplied in | natural: just depth, but noisy; needs ≥ 8 samples per texel or a wide filter, and the blocker search sees stochastic blockers | not a depth map; PCSS needs a separate opaque map |
| Memory, 4 cascades of 2048² | +16.8 MB (RGBA8) + 8.4 MB (R16) | ×4–8 depth (MSAA): +67–134 MB | +67–270 MB |
| GPU cost (measured proxy, 1080p, 2 Mtri, 10 % transparent) | +0.09–0.10 ms casters, +0.016–0.028 ms receiver fetch | ≈ MSAA raster ×1.3–2 (est.) + wider filter | 4–8 MRT writes per transparent fragment (est. 2–4× A) |
| Artefacts | layered transparency at different depths over a receiver is approximated; thin alpha layers on avatars need the depth gate (below) | noise; grey | ringing (Fourier); cost |
| Fits our pipeline | yes; OpenGL 3.3-plain, ports to Vulkan directly | matches the stochastic transparency method, but its value is colourless | overkill for SL content |

**Recommendation: A**, with a depth gate. T applies only where receiver depth > nearest transparent depth + ε, with ε = max(3 cm, 2 texels of depth slope).

- This stops coplanar or near-coplanar layers (avatar alpha layers over skin, decals) from darkening what they sit on.
- B remains the fallback for Low tier, without colour: it replaces today's column dither with a hashed per-texel coverage that PCF filters into a fractional shadow, at no extra memory.

### 7.3 Interactions

- **Glass with near-zero alpha but glare** (the recent stochastic-transparency fix): transmittance uses the diffuse alpha × vertex alpha, never the glare-boosted alpha. Glare is reflected light, not opacity, so clear shiny glass transmits ≈ fully.
- **Emissive and fullbright faces:** emission does not affect transmittance. Fullbright alpha uses the same T. Fullbright alpha-mask stays a mask.
- **Water:** not a caster, and no shadows under water (unchanged, §8.5).
- **Avatars' blended layers:** handled by the depth gate. Their own transmittance is still cast onto the ground, so a translucent dress gives a lighter shadow.
- **PBR:** use baseColor alpha and colour. SL has no KHR_materials_transmission. If it ever arrives, its transmission factor replaces α in T.
- **EEP light colour:** T multiplies the direct sun or moon colour only, never the ambient. So a red glass tints the sunlit patch red, and at sunset an orange sun through blue glass gives the physical product.

### 7.4 Tests

- **Offline:** an extension of bench_shadow_costs.py.
  - A window pane (α 0.2, white) gives 0.96 transmission.
  - A coloured pane (α 0.5, red) gives a red-tinted patch.
  - Three curtains (α 0.5 black each) give 0.125.
  - A pane coplanar with a receiver gives no darkening.
- **In-world:** a room with:
  - a clear window;
  - a stained-glass panel;
  - layered curtains;
  - an avatar wearing a semi-transparent dress, standing in the sun.

---

## 8. Respecting EEP

The model takes its inputs from the EEP sky and day cycle and adds no artistic multipliers of its own. RenderShadowPCSSLightSize stays at 1 by default, as a developer knob.

### 8.1 Parameters

| EEP parameter (LLSettingsSky) | Drives | Today |
|---|---|---|
| sun_rotation / moon_rotation (day cycle position) | the shadow direction | yes |
| sun_scale | angular size of the sun: L = 2 tan(0.265° × sun_scale) → PCSS penumbra width = blocker distance × L | yes (PCSS) |
| moon_scale | angular size of the moon (0.52° × moon_scale) | **no**: the moon uses the sun size today (pipeline.cpp:9849 comment) |
| sunlight_color (and the atmospheric attenuation the sky model applies) | the direct light the shadow removes; shadow depth follows from direct vs ambient, with no extra term | yes (lighting) |
| moon_brightness | the moon's direct light, so moon shadows are as faint as the moonlight | partly: mSunDiffuse is black-checked only |
| ambient | the light that remains in shadow | yes (lighting) |
| sun/moon "is up" and their light intensities | which light casts, cross-faded (8.2) | hard switch |
| haze_density, haze_horizon, density_multiplier, distance_multiplier | the sunlight colour reaching the ground, via the existing atmospherics. Optional: none on the shadow itself | – |
| cloud_shadow, cloud coverage | **optional**: overcast would widen the penumbra, a larger effective light. That is a new mapping, so it is offered, not proposed (§14) | – |
| star_brightness, glow | no | – |

### 8.2 Transitions without pops

- **Sun/moon switch.** The shadow light is the one whose direct illuminance (colour luminance × visibility) is larger.
  - The switch happens inside a window around the crossing where both are below 2 % of the noon sun. In that window, the shadow strength fades to 0 over ≈ 2 s of sky time before the switch and back up after.
  - When both lights are bright (EEP allows it), the sun keeps the shadow.
- **Sunrise and sunset.** As the sun approaches the horizon, the light-space depth range grows. The fit is stable and caps that depth (§9.1), so nothing pops.
  - At very low elevation (< 3°), the sun shadow's strength follows the sunlight colour, which EEP already darkens. No special case.
- **Light size changes over the day cycle** (sun_scale keyframes) are continuous, so the penumbra changes continuously.

---

## 9. Design: the new shadow model

### 9.1 Stable cascades, fitted from the light and the scene

1. **Split distances** come from a shadow distance setting (§2.4 defaults per tier) and a fixed practical split scheme. It is a logarithmic/uniform mix with λ = 0.85, recomputed only when the setting or the camera fov changes. No visible-extents input.
2. **Fit.** Each cascade covers the bounding sphere of its camera-frustum slice.
   - The radius is quantised up to 1/16 of a power-of-two step, so the size changes only when the fov or the split changes.
   - Rotation-invariant: turning does not change the size.
   - Light view "up" is fixed to the world (north, or east when the sun is near-vertical north/south).
3. **Snapping.** The ortho centre is snapped to whole texels of a grid anchored at the region origin in light space. Translation therefore moves the grid in whole texels, and a static scene samples identical texels every frame.
4. **Depth range from the scene, towards the light.**
   - The near plane goes at the light-space minimum of the casters that the cascade's xy box intersects. That is a light-space AABB of the region's object partitions (cheap: the octree node bounds), capped at 2 × the shadow distance.
   - The far plane goes at the slice sphere's far side.
   - Depth clamp stays as the safety net.
   - Depth precision is then fixed per cascade, which gives a stable bias (9.4).
5. **Caster culling** uses the same light-space box. No dependence on the main camera's visibility or occlusion.
6. **No clearing of empty cascades.** A cascade with nothing in it renders nothing (an empty cull) but keeps its matrix. No special path.
7. **Caching** (§2.4). Re-render only when the snapped centre has moved more than the guard band (5 % of the extent), or every N frames, or when a caster in the box moved. The last uses the object-update dirty flag of the partitions.

### 9.2 Perspective-warped cascades: replace

**Evidence:**

- The warp's origin and fov move with the camera by construction, so it cannot be snapped or cached.
- It flips with ortho per frame (§3.3).
- Today's PCSS must estimate the uv scale per pixel because of it (pcssUVScale).

With 4 snapped ortho cascades at the densities in §9.6, the near-field density meets the target without it. Remove the warp, and keep `ShadowDiagForceOrtho` behaviour as the only path.

The owner's 2026-11-03 decision kept the warp. This reverses it, so it is a decision (§14).

### 9.3 Soft shadows as the only model

1. **PCSS for every sun cascade and both projectors**, with one code path and no stock hard-PCF path. Tiers reduce taps and resolution only.
   - **Blocker search:** a disc sized by light size × maximum blocker distance. The cascades are stable and ortho, so the uv scale is constant per cascade. Early-out when all taps agree.
   - **Penumbra:** w = (d_receiver − d_blocker) × L for the sun and moon, with L from EEP (§8.1). For projectors, w = S (d_r − d_b) / d_b, where S is the projector's emitter size. In SL that is the light's radius parameter, clamped.
   - **Contact hardening** follows from the per-pixel penumbra.
   - **Filter:** a rotated Poisson disc of hardware-PCF taps, sized to the penumbra in world units, at least one texel.
2. **Bias in world units.**
   - Normal offset: the receiver is moved along the normal by k × texel size, with k ≈ 1–1.5, scaled by sin(θ).
   - Plus the receiver-plane depth bias that PCSS already has. Both are in metres or texels of the cascade, independent of the depth range.
   - No altitude term.
3. **Denoise** (§4.7): half-resolution joint-bilateral on the shadow term, with a radius from the penumbra; full resolution at Ultra.
   - It never depends on SSAO.
   - The SSAO blur keeps its own pass, which, when both are on, can be the same pass.
4. **Screen-space contact shadows.** These are short screen-space rays (≤ 0.3 m, 12–16 steps) towards the light. They catch sub-texel contacts: feet, small props, fingers.
   - Needed only at Ultra.
   - At Medium and High, the stable cascade 0 at ≤ 1 cm texels is enough for avatar-scale contacts.
   - Recommendation: Ultra only, later stage.

### 9.4 Projector lights

- Keep 2 shadow projectors. Choose them by a priority that is stable over time: hysteresis, with the score from light power and distance to the avatar, not to the camera.
- Fade over a fixed 0.5 s, not with camera speed.
- Same PCSS path.

### 9.5 Integration

- **Water:** no shadows under water stays. Underwater receivers keep today's behaviour, and the water surface is not a caster.
- **Alpha and stochastic transparency:**
  - Alpha surfaces receive the same model through the forward programs, including the stochastic accumulation pass. The alpha-only passes take no shadow (as today).
  - Transparent faces cast through §7.
  - Alpha-mask stays a binary caster.
- **Rigged meshes and avatars:**
  - Rigged faces cast like static ones, through the same passes.
  - Avatar transmittance is subject to the depth gate (§7.3).
  - Crowds are limited per §2.4.
- **Reflection probes and cube snapshots:** gCubeSnapshot keeps a cheap path. It uses 2 cascades (as today), a fixed light-size PCF (no blocker search), and no transmittance, contact shadows or denoise.
- **Night sky:** unaffected. The moon cast follows §8.
- **OpenGL baseline:**
  - GL 3.3 plus sampler objects, already the baseline.
  - The transmittance target is plain RGBA8 with multiplicative blending.
  - Everything is expressible in Vulkan 1.0 core: depth array, sampler with compare, blending.
  - OpenGL results are the reference. Shared logic (fit, snapping, split scheme, bias in world units) lives in a renderer-free C++ unit with equivalence tests, so the future Vulkan renderer can reuse it.

### 9.6 Quality tiers

`RenderShadowDetail` keeps its meaning: 0 off, 1 sun and moon, 2 plus projectors. A new `RenderShadowQuality` (0–3) selects the tier. The texel figures below are for cascade 0.

| | Low | Medium | High | Ultra |
|---|---|---|---|---|
| Shadow distance | 64 m | 128 m | 192 m | 256 m |
| Cascades | 3 | 4 | 4 | 4 |
| Map size (texture array, square) | 1536² | 2048² | 2048² | 3072² (0–1), 2048² (2–3) |
| Cascade-0 texel at 1080p, fov 60° | ≈ 1.0 cm | ≈ 0.8 cm | ≈ 0.8 cm | ≈ 0.5 cm |
| Search / filter taps | 8 / 8 | 12 / 12 | 16 / 16 | 16 / 24 |
| Transmittance | coverage only (hashed) | colour | colour | colour |
| Caching | cascades 1–2 every 2nd / 4th frame | cascades 2–3 every 2nd / 4th | cascade 3 every 2nd | none |
| Denoise | half res | half res | half res | full res |
| Contact shadows | – | – | – | yes |
| Memory (depth24 in 32 bit + RGBA8 + R16) | 27 MB | 84 MB | 84 MB | 172 MB |
| Today, for comparison | 4 × screen-sized depth: 34 MB at 1080p, 60 MB at 1440p | | | |

Cascade-0 texel figures are estimates for a slice 0.25–6 m at 1080p with fov 60°, sphere radius ≈ 3.3 m, divided by the map size.

Costs per tier are in §2.3. Mid-GPU class estimates are ≈ ×3 for the full-screen parts.

---

## 10. In-world bisection (for the owner)

### 10.1 Setup

1. Use the staged RelWithDebInfo viewer from this branch (§13).
2. Set shadows to Sun/Moon + Projectors and PCSS off, unless a step says otherwise.
3. Open Preferences > Graphics > Rendering 2 and keep it open beside the world. Every control applies at once.
4. Turn on "Log cascade fit (1/s)". The viewer log (`ShadowDiag` lines) then records the splits, mode, texel size and bias per cascade once a second. Attach the log to the report.

### 10.2 Scenes

- **Indoors:** a closed room with a window, sun at mid elevation, on the ground (outdoors visible through the window). Then the same in a skybox above 1000 m.
- **Outdoors:** open land with trees and a house, the avatar in the open. Look at the avatar's own shadow, and at a tree's.

### 10.3 Steps

**B1. Is it the fit at all?**

1. Find a spot where shadows misbehave when you move or turn.
2. Stand still, tick "Freeze cascade fit", then move and turn a little.
3. Read the result:
   - If the shadows stop changing with the camera (they stay put in the world), the fit causes the instability (§3.1–3.3).
   - If they still change, the cause is elsewhere: LOD, projectors, objects appearing (B6, B7).
4. Untick when done. Moving far while frozen loses coverage; that is expected.

**B2. Split range.**

1. Set "Fixed split range" to 64.
2. Walk and turn indoors, then outdoors, tilting the camera up and down.
3. Read the result:
   - If the vanishing and popping mostly stop, the visible-extents split is the cause (§3.1).
   - Try 32 and 128: the shadow detail changes, but it should no longer change with the view.

**B3. Swimming and rotation.**

1. Tick "Show shadow texels".
2. Walk slowly. If the checkerboard crawls, the grid is not snapped.
3. Turn. If it rotates, "up" follows the camera.
4. Tick "Lock light 'up' to north". The rotation should stop.
5. Tick "Snap cascades to texels". The crawl should stop: whole-texel jumps only.
6. If both stop, §3.2 is confirmed. Then untick "Show shadow texels" and look at the real shadow edges with those two on.

**B4. Projection switching.**

1. Tick "Orthographic cascades".
2. Turn in place. If sudden changes of shadow sharpness at some angles disappear, §3.3 is confirmed.

**B5. Bias and leaks.**

1. Find light leaking under a wall or a shadow detached from feet. Note the cascade's bias in the log.
2. In a skybox, tick "No altitude bias term". If the leaks shrink, §3.4's altitude part is confirmed.
3. With B2's fixed range on, the leaks should stop changing with the view.

**B6. Off-screen occluders** (the original hypothesis; expected: rejected).

1. Indoors, with sunlight leaking in through a roof or ceiling when you look down, tick "No caster culling".
   - If the leak stays, culling is not the cause (expected).
2. Then set "Extend to light" to 50.
   - If the leak stays, depth clamping works (expected).
   - If either step fixes it, report it: the code reading says it should not.

**B7. Objects not yet created.**

1. After a teleport, if a roof's shadow is missing until you look at the roof, untick "Object cache occlusion".
2. Teleport away and back. If the shadow is now there at once, §3.8 is confirmed.

**B8. Cleared cascades.**

1. Tick "Never clear empty cascades".
2. If the pops when facing a wall closely go away, §3.5 is confirmed.

**B9. Dithering.**

1. With PCSS off, tick "Stock PCF: no jitter". The dither becomes clean stair-steps.
2. Set "Light-map blur: Always on". The edges become stock-with-SSAO. This explains the PCSS-off report (§6.3).
3. With PCSS on, tick "PCSS: no kernel rotation". The noise becomes banding.
4. Set "Light-map blur: Always off" to see the raw noise. If noise remains where the blur is on, it is the blur's edge-stop (§4.2).

**B10. Glass.**

1. Look at the sunlit floor behind a window with semi-transparent glass. A striped or mottled half-shadow confirms §3.6. It crawls with B3's movement.

**B11. Projectors.**

1. With 3 or more shadow-casting projectors near you, walk around. If projector shadows fade in and out, §3.7 is confirmed.

Report each step's outcome (yes/no) and the log. The ranking in §3 then becomes measured in-world.

---

## 11. Test scenes and benches

**Offline:**

- `python scripts/shadows/cascade_fit_model.py`: the fit model. Its scenes are a 10 × 10 m room on the ground with an outdoor scene to 190 m, the same room as a skybox at 2500 m, and open land with terrain patches, trees, houses and an avatar. It prints every table used in §3.
- `python scripts/perf/bench_shadow_pcss.py`: the sun pass cost and the PCSS penumbra checks.
- `python scripts/perf/bench_shadow_costs.py`: the blur, the caster raster proxy, and the transmittance proxy.
- The new model will add the following to the fit model, as checks:
  - the snapped grid's phase is constant under translation;
  - the size is constant under rotation;
  - every off-screen caster that shades a visible receiver is in the cull box;
  - the bias in metres is constant across views.

**In-world reference scenes** (the gates in §12 measure on these):

1. Sandbox ground parcel: a room with a window, stained glass and curtains (§7.4), trees outside.
2. The same room in a skybox at 2500 m.
3. Dense forest (alpha-mask leaves).
4. Crowded club (30+ avatars, projectors).
5. Open land at sunset and moonrise (EEP transitions).

---

## 12. Staged implementation plan, with performance gates

The stages are feature PRs from `vkstorm-devel` on `codex/shadow-*` branches, built RelWithDebInfo and benched. After qualification come integration PRs per line: release, canary and rust, as for PCSS. A stage lands only after its gate passes.

| Stage | Content | Effort | Gate (all at 1080p and 1440p, 5070 Ti; plus the in-world protocol §2.5) |
|---|---|---|---|
| S0 | this study: diagnostics, model, benches (not for merge as-is; diagnostics never reach release) | done | – |
| S1 | **Stable fit**: fixed split scheme from a shadow-distance setting; sphere fit, world-locked up, texel snapping; scene-based depth range and light-space culling; no clearing; warp removed. Fit and snapping in a renderer-free unit with tests; fit model extended as tests | 4–5 days | caster GPU ≤ 1.15 × today's (before caching) on scenes 1–4; CPU fit ≤ 0.1 ms; zero phase drift in the model; in-world B1–B4 behaviour without the diagnostics |
| S2 | **Bias in world units** (normal offset + receiver plane), altitude term gone | 1–2 days | no acne and no leak > 1 texel on the reference contacts; bench edge checks |
| S3 | **PCSS as the only path**: all cascades and projectors; early-outs; tiers' tap counts; moon_scale; EEP sun/moon cross-fade | 3–4 days | sun pass ≤ today's PCSS-on pass at Medium/High; penumbra checks in bench_shadow_pcss.py for all cascades |
| S4 | **Denoise**: half-res joint-bilateral on the shadow term, independent of SSAO; light-map blur no longer needed for shadows | 2 days | ≤ 0.08 ms at 1080p; no residual noise in B9 |
| S5 | **Caching** of far cascades, staggered; dirty flags from moving casters | 3 days | total ≤ today's PCSS-on total at the default tier on scenes 1–4; CPU ≤ today's |
| S6 | **Transmittance** (§7, option A + depth gate; hashed coverage at Low) | 4–5 days | ≤ 0.12 ms extra at 1080p on scene 1; 0 extra when no transparent casters; §7.4 checks |
| S7 | **Projector stability** (priority with hysteresis, fixed fade) | 1 day | no fade-out while walking in scene 4 |
| S8 | **Tiers and Preferences** (RenderShadowQuality, presets, featuretable) | 1–2 days | every tier within its §2.3 budget; FPS drop shadows off→on stated per tier |
| S9 | Optional: **screen-space contact shadows** at Ultra | 2 days | ≤ 0.15 ms at 1080p |

The total is ≈ 21–26 days of focused work, plus integration per line.

**Per stage, the owner checks in-world once on the installed build after release, as agreed.**

---

## 13. Builds

The staged development viewer (RelWithDebInfo, LTO, staged, no installer) is
`C:\Users\annes\source\repos\vulkanstorm\work\indoor-shadow-study\build-vc180-64\newview\RelWithDebInfo\vulkanstorm-bin.exe`.

The smoke test was a minimised launch without login, for 75 s, with a scratch APPDATA and LOCALAPPDATA.

- Every shader set loaded.
- The log has no shader warnings.
- The process was stopped and is gone.

The diagnostics are development hooks. `scripts/tests/check_release_hooks.py` does not know them yet: Check PR runs that script on every PR, so adding the names here would only fail this draft. Whatever stage first lands code from this branch must add `ShadowDiag\w*` and `shadow_diag` to its forbidden list, or drop the diagnostics.

---

## 14. Decisions (owner, 2026-11-05: "proceed as per your recommendations")

1. **The warped cascades are replaced** by stable ortho cascades (§9.2). This reverses the 2026-11-03 decision to keep the warp.
2. **Shadow distance becomes its own setting**, with a default per tier (§2.4).
3. **Transmittance is T = (1 − α)(1 + α c)** (§7.2).
4. **The bias altitude term is dropped now, as a hotfix** to release and every line (§6.4), ahead of S2.
5. **The featuretable entry that turns PCSS on at Ultra stays** (§6.2).
6. **No cloud-cover penumbra.** The light size comes from sun_scale and moon_scale only (§8.1).
7. **Low may use 3 cascades.** Per-GPU-class defaults are tuned in S8.
8. **Screen-space contact shadows at Ultra only** (S9).
9. **The default camera offsets differ from stock:** noted, no change.

## 15. Could this be proposed to Aurora Viewer later?

Aurora Viewer is a Rust/wgpu SL viewer, GPL-3. This design was made on Vulkanstorm's own code and evidence; Aurora's code was not consulted.

The design is renderer-agnostic. It uses:

- stable sphere-fitted, texel-snapped ortho cascades from a scene-based light-space box;
- PCSS as the only filter, with EEP-driven light sizes;
- world-unit bias;
- a coloured transmittance map with a depth gate;
- per-cascade caching.

All of it maps onto wgpu: depth texture arrays, comparison samplers, RGBA8 with multiplicative blend, and compute or fragment denoise. The fit and snapping are being written as a renderer-free unit (S1). They can be shared as a description and test vectors, the model in `cascade_fit_model.py`, without sharing code.

Our new files are dual-licensed LGPL-2.1-or-later OR GPL-3.0-or-later, which is compatible with Aurora's GPL-3, so an Aurora implementation could even reuse the unit if the owner wishes. A proposal would be the design document and the offline model, after S1–S3 are qualified here.
