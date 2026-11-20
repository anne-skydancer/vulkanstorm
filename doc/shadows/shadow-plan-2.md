<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Shadow plan 2 for OpenGL

**Replace camera-driven shadow allocation and deferred caster updates with a receiver-space quality contract and
same-frame publication.** Keep the existing OpenGL depth passes, atlas storage, and stochastic caster rules
where they meet that contract. Qualify the design on one wide projector before extending it to other lights. For
the same scene tick and quality tier, a world point should receive the same shadow through a camera orbit, walk,
or zoom.

This is a design proposal. The renderer changes and the frame-rate qualification belong to the implementation
PRs.

## Requirements and baseline

| Requirement | Current evidence | Proposed contract |
|---|---|---|
| Stable position and detail | Release integration #552 brought in camera-position tile sizing. The devel pool also uses camera distance for admission, pressure and service order. The receiver shader has a camera-distance fade. | For a fixed tier and loaded scene, coverage, density, sampling, and linear RGB visibility at a world receiver are independent of the camera. |
| Firestorm's response on moving casters | Firestorm 7.2.5 and the inspected Linden Lab revision clear and redraw each selected projector slot on a main-view shadow pass. Slot selection has its own fade. Vulkanstorm can serve live layers across multiple frames and choose them by camera distance. | Every affected cell of an admitted lamp uses the same current caster pose, attachments, and light transform as the frame's lighting. |
| Smooth motion across a seam | The pool already has guarded cells and footprint restoration. The documented sparse blocker search can miss a thin bar and jump to fully lit even on fresh, fine maps. | Preserve a shared sampling lattice and make motion and cell boundaries continuous within a declared contour and penumbra tolerance. |
| Fine detail in the lamp | Devel uses stochastic coverage in the depth path and a separate static RGB transmission path. Alpha-mip residency and invalidation can lose pattern detail. | Retain authored cuts, stochastic coverage, and tint. Measure the actual lamp's features and layered cases against a high-density reference. |

[Release #552](https://github.com/anne-skydancer/vulkanstorm/pull/552), [projector pool and known limits](projector-shadow-pool.md), [admission code](../../indra/newview/llprojectorshadows.cpp#L493), [receiver fade](../../indra/newview/app_settings/shaders/class1/deferred/shadowStochasticUtil.glsl#L553), [Firestorm 7.2.5 projector slots](https://github.com/FirestormViewer/phoenix-firestorm/blob/4ac201fdc56127de27de34567286885f4e75d194/indra/newview/pipeline.cpp#L11465-L11601), [Linden Lab projector slots](https://github.com/secondlife/viewer/blob/a7811bcd/indra/newview/pipeline.cpp#L10522-L10662).

The current implementation is a useful baseline for depth rendering, cache invalidation, and draw cost.
Replacing its scheduling and lookup together lets the coverage, update, and filtering rules agree at the seam.
Keep a full fresh render as a correctness oracle and as a timed fallback candidate for a lamp whose live
footprint covers most of its map.

## Local light anchors

**Accepted decision, 2026-11-20.** Anchor each local light's shadow projection at its point of origin in world space.
For a point light, use the emitting prim's world position. For a projector, derive the optical origin and axis from
the emitting prim's world transform and projector parameters. Camera movement leaves the origin and projection fixed.

Use the emitting prim's UUID to identify the light. A script selects the emitter by setting light properties on the
target prim. When a root script sets a child's light properties, the child's light origin applies. The viewer gets the
per-prim light state in object updates. Lights set in the Build tools use the same rule.

Use the same light origin and projection for lighting and shadows. In the first frame that uses a changed emitter pose
or projection, publish the light transform and affected shadow maps together. A scripted off/on cycle can reuse cached
maps if their projection and caster inputs are still valid.

Select casters by overlap with the light's volume. The emitter's own geometry is excluded from its shadow pass.
Other members of the fixture, including a glass shade, can still cast or transmit, as can unlinked objects and avatars.

[LSL linked-prim updates](https://create.secondlife.com/script/lsl-reference/functions/llsetlinkprimitiveparamsfast/),
[viewer object light parameters](../../indra/newview/llviewerobject.cpp#L6881),
[projector geometry](../../indra/newview/llprojectorshadows.cpp#L81),
[point-light emitter and fixture rule](../../indra/newview/llpointshadows.cpp#L1150).

## Graphics quality presets

| Preset | Default shadow method and definition |
|---|---|
| **High-Ultra and Ultra** | Use the redesigned method at its sharpest qualified definition. Both presets share the highest shadow quality profile. |
| **High** | Use the redesigned method at about 95% of Ultra's edge sharpness for the same light and receiver. |
| **Below High** | Select Legacy by default. The preset's shadow enablement and an explicit choice of method continue to follow the existing settings rules. |

Use fixed receiver-space parameters for each profile. Qualify the sharpest profile against the seam and fine-detail
gates. Hold the light size, caster, receiver and softness setting constant for the tier comparison. Measure
sharpness as the reciprocal of the 10–90% linear visibility transition width across an opaque near-contact edge
in receiver metres. High's target therefore corresponds to an edge about 5.3% wider than Ultra's. Check the same
physical penumbra and lamp features independently so the tier change preserves authored softness and fine detail.

The current High preset enables directional shadows, and High-Ultra and Ultra enable local shadows as well. Compare
directional shadows across the shipping presets and enable local shadows explicitly at High for the controlled
projector comparison. Apply the method defaults through the existing feature tables.

[Current feature-table presets](../../indra/newview/featuretable.txt#L233),
[shadow method settings and preset precedence](shadow-method.md#defaults-presets-migration).

## 1. Fixed receiver density and coverage

Choose a fixed minimum density `T` in texels per metre for the qualified receivers in each redesigned profile.
Measure the lamp's preselected fine features and receiver geometry in the baseline before locking the value. Declare the
supported lamp reach, receiver distance and incidence range, loaded caster set, and alpha asset availability. A
light-space perspective grid can over-sample nearer receivers. The density guarantee is a floor over the
declared range, and the same physical feature and filter-width checks apply throughout that range. The footprint
grows without bound at grazing incidence, which makes the declared range part of the test.

For a candidate cell, let `J` be the derivative mapping normalized cell UV to metres in the receiver tangent
plane. Choose integer interior dimensions `Nu` and `Nv` so the maximum singular value of `J * diag(1/Nu, 1/Nv)`
is at most `1/T` throughout the supported receiver envelope. Bound the footprint over the envelope, including
cell corners and the farthest and most oblique qualified receivers. Start with the pilot's known floor and a
bounded tilted receiver. Round allocations upward, then add guards. Keep all cells in the pilot aligned to one
projector-space lattice scaled for the worst qualified footprint. Test subdivision against a monolithic map at
the same lattice spacing. Caster bounds determine the cells and guard areas that must contain their shadows,
including off-screen casters. The light projection and texel footprint on each caster determine shadow geometry
LOD and alpha-mip demand.

Use the lamp's reach and stable scene IDs for eligibility and capacity decisions. Feed the same pilot lamp to
both local shadow preparation and direct lighting. View culling may skip shading pixels outside the viewport. It
may not change the admitted set, tile resolution, coverage, alpha inputs, or shadow fade for a shared world
receiver. For general scenes, drive requests for relevant off-screen drawables and alpha assets from the light
volume and record unavailable inputs. Invalidate on the actual asset or mip generation arriving, as well as
caster, lamp, material, and geometry changes. A camera-only change must leave those keys alone. Test warm
equal-residency captures first, then account separately for camera-driven object-cache promotion and asset
delivery.

Preflight the complete light's storage before admission. Under pressure admit complete lights at the tier's
density using a deterministic, camera-free scene order. If required coverage will exceed capacity, report which
lamp cannot fit, the requested bytes, and the failed qualification. Atlas compaction can change physical
addresses while preserving lattice coordinates, sample identity, and the published descriptor. A resize must
draw into reserved storage and publish matching transforms with its maps. With an unchanged lamp projection, the
renderer may still redraw at its existing complete layout during a pending resize if that layout meets the
active contract.

[Current caster LOD pin and streaming limits](projector-shadow-pool.md#the-lod-pin), [current tile sizing and pressure](projector-shadow-pool.md#sizes-from-the-lamp), [direct light selection](../../indra/newview/pipeline.cpp#L11114).

## 2. Same-frame casters and coherent publication

Take a frame-owned snapshot after avatar animation, attachment updates, and geometry commits. Reference the same
render transforms, skinning palettes, and geometry generations used by the main draw. Capture stable caster IDs,
bounds, alpha inputs, and the light pose. Use the existing local-shadow call before deferred lighting. Give
auxiliary views their own descriptors or a read-only view of the main frame's published set so a probe cannot
replace the main-view generations. Form connected work groups from every cell touched by a caster's previous or
current footprint expanded for guards. Coalesce groups whose footprints overlap. A lamp pose or layout change
spans the whole lamp.

Keep the cached static depth and the current live layer. Restore each affected region from valid static depth,
then draw **all** current casters intersecting the restored region, including stationary live casters. Mark both
old and new coverage for mover, removal, attachment, and static-to-live transitions. Copy partial rectangles
only when a conservative bound includes the old and new coverage. Maintain the static transmission dependency
alongside depth. A changed tint, alpha mip, or an opaque-depth change that conditions the transmission map must
rebuild the relevant data together. Validate this at the lamp before adding other kinds of light.

Reserve storage, scratch and work for the complete group before mutating data any consumer can see. Submit the
bounded copies and depth draws on the render context, then publish matching depth, valid transmission,
transforms and generations together before the first consumer. Keep current-frame live refresh ahead of optional
cache work. Reuse in-place live storage only when the previously published group cannot be sampled during repair
and the whole repair can finish before lighting. Where partial failure can leave a live group incomplete, stage
the replacement and keep the previous complete generation until commit. Exercise allocation failure and a failed
group member explicitly. A previous generation is a diagnostic fallback and a same-frame test failure if the
light or caster moved. If no complete valid generation or qualified fresh fallback exists, record the failure
for the whole affected light. If same-frame work consistently exhausts the budget, reduce measured work or
restrict the tested scene by the documented camera-free capacity policy before enabling the method.

[Local shadow entry](../../indra/newview/lllocalshadows.cpp#L345), [C6 animation and attachment ordering](../../indra/newview/llavataranimationjobs.h#L7), [display geometry update](../../indra/newview/llviewerdisplay.cpp#L911), [current live restore](../../indra/newview/llprojectorshadows.cpp#L1239), [current tile binding](../../indra/newview/llprojectorshadows.cpp#L1843).

## 3. Continuous lookup and inexpensive transparency filtering

Use continuous light-space receiver coordinates and the same tap positions on both sides of the projectors' cell
boundaries. Cull and rasterize the full guard, with enough extent for blocker search, filter taps, bilinear
support and receiver bias. Sample valid guard texels instead of repeating an interior edge. Base penumbra
support and bias on the light and receiver geometry. A cell boundary must match the unsplit map at the same
resolution within the seam tolerance. Derive a stable geometric receiver plane where needed. Screen-depth
neighbours at silhouettes can change with camera pose, so measure their effect on bias and use a
camera-independent geometric normal path if they fail.

Start with a small spatial percentage-closer filter on **visibility**, with bilinear weights and a fixed
physical support on the test receiver. Compare four bilinear comparison taps against a bounded weighted or
gather variant at comparable measured penumbra width. Raw depth taps allow per-tap receiver-plane depth
comparisons if the hardware comparison path fails at grazing angles. Add contact hardening only after the
fixed-width candidate passes. Compare the existing blocker search against a continuously weighted estimate on
thin bars and sparse alpha before enabling any early-out. A transition from no blocker to a few sparse hits
needs an image test even at high density.

Keep the authored MASK cutoff and the existing world-anchored stochastic BLEND coverage in the depth paths that
use it. The light-space sample footprint and texture residency must agree across guards. Keep the existing RGB
transmission for applicable static blended casters. Compare stochastic depth and deterministic transmission only
with their respective material rules. The current one-depth transmission approximation can apply both layers to
a receiver between panes, so test and document that scope before enabling that case by default. Measure spatial
mean and grain on 3%, 25%, 50%, and 98% alpha, fully clear and opaque endpoints, independently sampled stacked
sheets, and thin mask detail. For two independent 50% stochastic sheets the mean visibility target is 25%. Use
the actual RGB transmission formula for the tint fixture.

Use the smallest qualifying spatial kernel first. It can reuse the shadow lookup without a full-screen denoise
pass. If grain remains, test extra light-space samples only on the relevant material path and report the cost
and pattern loss. Any later history experiment must reject history when a caster, lamp, layout, or material
generation changes under a fixed receiver and camera, as well as at disocclusions. Require a clean first frame
before using accumulated samples.

[Current stochastic caster shader](../../indra/newview/app_settings/shaders/class1/deferred/shadowStochasticCasterF.glsl), [material rules and transmission limits](projector-shadow-pool.md#meeting-559-devels-caster-rule), [cost and filter defects](projector-shadow-pool.md#limits-1), [Enderton et al., Stochastic Transparency](https://research.nvidia.com/publication/2011-08_stochastic-transparency), [Wyman and McGuire, Hashed Alpha Testing](https://research.nvidia.com/labs/rtr/publication/wyman2017hashed/), [Castaño, coverage-preserving alpha mips](https://www.the-witness.net/news/2010/09/computing-alpha-mipmaps/).

## 4. OpenGL integration and staged rollout

Use one lighting interface that returns linear RGB visibility for a world receiver and a prepared light
descriptor. Keep the technique-specific projection, lookup, and coverage in projector, point, sun and moon
modules. Share receiver-plane comparison, alpha rules, and filter code where their inputs and semantics match.
Keep projector cookies and light attenuation separate from shadow visibility. Route the pilot through the
deferred projector path first, then compare forward alpha, water, and probe consumers against the same light and
generation contract.

Keep frame jobs CPU-only. Start with serial preparation and one GL submission path. Then use the existing
cull/submit split for pass-owned cull outputs and stable draw lists. Use AVX2 for batches of bounds or footprint
tests when profiling shows a gain and compare membership and order against the scalar reference, including guard
contacts. Cache immutable descriptors and GL resource bindings and batch compatible draws/copies using the
renderer's available capabilities. Time the render thread and GPU separately. Enforce the active line's OpenGL
feature floor and existing state/lifetime rules. Develop the hot shader family in the existing path and convert
it to Slang in its own follow-up PR after the behavior settles, per the repository's Slang decisions. Generated
GLSL is the OpenGL input. Verify generation, compile/link, binding layout, position/depth/coverage parity, and
the same in-world fixtures after conversion.

| Stage | Deliverable and gate |
|---|---|
| **0. Baseline** | Record the actual lamp, a fixed matte receiver, an opaque edge, patterned alpha, tinted glass and a repeatable avatar route. Compare current Vulkanstorm, a forced-current redraw on the same map layout, and an independent denser raster with a converged visibility reference. Capture Firestorm's selected-slot response with a comparable scene when available. Freeze the sample points, feature patches, paths, and tolerances before testing the candidate. |
| **1. One wide projector** | Bind one projector to direct lighting and the new map path. Establish bounded receiver density, contiguous guarded cells, light-driven LOD/mips, and serial same-frame group publication. Compare split with monolithic at the same lattice and use the existing filter initially for diagnosis. |
| **2. Filter and lamp detail** | Qualify the fixed-width spatial filter, then measure any contact-hardening or extra stochastic samples added. Check near/far and oblique receivers, map-level and final RGB visibility, alpha residency changes, and the actual lamp. |
| **3. Local-light capacity** | Exercise several projectors, pressure, and changing lamp state before expanding to point lights. Use the existing point-face code as storage and draw groundwork. Apply the same density bound and generation rule across cube faces, and test edge/corner routing and cross-face filter footprints. Replace camera-ranked face sizing and cadence in the new path. Profile culling, SIMD and GL batching independently of image changes. |
| **4. Sun and moon** | Study bounded world-anchored pages or fixed world tiles with stable world-space receiver keys and conservative light-space caster extrusion. Keep the keys stable across region-origin shifts and use overlap with normalized visibility weights where page footprints differ. Reuse static/live and filtering primitives. Require physical seam and penumbra parity and current casters for either light. Earlier camera-centred and zoom-stepped cascade designs are baselines for cost and coverage. A directional cap, sky motion, paging or a sun/moon transition needs its own qualification before adoption. |

[Point-light plan and its current camera tiers](point-light-shadows-plan.md#35-tiers-by-camera-position-with-hysteresis-owner-decision-2), [earlier sun and moon study](sun-cascades-refresh.md), [multicore and SIMD study](../perf/multicore-simd-study.md), [Slang conversion workflow](../slang.md#decisions-in-force), [Khronos OpenGL Registry](https://registry.khronos.org/OpenGL/index_gl.php).

## Acceptance and cost

| Gate | Evidence to attach to the implementation PR |
|---|---|
| **Same-frame response** | Frame, pose and map-generation counters plus fixed-world-point visibility for slow, fast, reverse and stop motions. Cross an edge, a corner and a guard-only overlap. Check the vacated cell, a just-arrived caster, a rigged attachment, and a second caster overlapping the restored area. Force an incomplete update and verify that no reader sees a mixed group. |
| **Camera independence** | Replay two camera paths including zoom at the same scene ticks. Compare linear RGB shadow visibility before BRDF, cookie and post-processing at shared world points within the declared numerical tolerance. Compare admission, layout size, physical footprint, caster LOD, alpha mip and stochastic sample identity under normal and forced pressure. Include streaming arrivals and light or atlas address changes in separate, repeatable cases. |
| **No stepping at the seam** | Measure the 50% visibility contour's excess displacement in millimetres against the independent raster across subtexel motion. Compare to the unsplit map at the same resolution and record the 10–90% edge width. Repeat with a thin bar and glazed frame so added blur and a missed blocker are distinguishable. |
| **Fine detail and tint** | Use preselected, reference-resolved lamp features at more than one qualified receiver distance and angle. Report hole retention, feature contrast, mean RGB transmission, and spatial/temporal variation before and after filtering. Inspect the alpha mip and raw map to localize loss. |
| **Quality presets** | Verify the method and shadow enablement on preset application. Compare High-Ultra and Ultra against the same sharpest reference, and High against the 95% sharpness target. Repeat the fixed-world-point and lamp-detail checks at each redesigned tier. Verify Legacy as the default below High. |
| **Frame rate and capacity** | Each implementation PR reports its own CPU preparation, GL submission and GPU raster/copy/filter cost in milliseconds, plus whole-frame ms and fps at the default tier. Report the graphics preset, shadow method and enabled shadow types for each capture. Use paired captures on the same settings, loaded scene and motion path. The owner gate is about 60 fps standing in a crowded room with about 60 avatars and never below 25 fps moving, corresponding to about 16.7 ms and at most 40 ms. Report tails and sustained drops along with warm and cold state. Record allocation, guards, static/live maps, transmission storage, scratch reservations, and fragmentation. |

After the fixed-lamp comparison, use a root script to toggle and move a child emitter while a sibling shade and an
unlinked caster remain in its volume. Check that lighting and shadows follow the child in the first frame that uses
the update, that off/on preserves the emitter identity, and that both the shade and the independent caster participate.

Lock numeric tolerances with the Stage 0 reference and keep them fixed in later comparisons. Report both peak
and 95th-percentile contour errors and both sides of each seam. Report any changed tolerance with a new baseline
and the reason for the change.

The Rooftops Ultra study gives cost centres to measure. It recorded about 1.3–1.5 ms for the projector work in
its specific still capture and flagged the moving comparison as invalid. It does not qualify the default-tier
proposal. Run the implementation stages as staged Autobuild `RelWithDebInfo` feature builds. Keep capture hooks
on devel and use the owner's in-world Release check before calling a rendering stage safe. Hold the default
switch until the image, invariance, capacity, and frame gates pass.
[Rooftops Ultra measurements](rooftops-ultra-shadow-split-2026-11-16.md),
[repository build and qualification rules](../../AGENTS.md).

## Literature and design choices

- *Real-Time Rendering*, fourth edition, Chapter 7 §§7.4–7.7 surveys map sampling, PCF, contact-hardening and filterable alternatives. Its treatment separates shadow-map resolution from reconstruction, which is why the pilot controls layout and filter independently. [Authors' Chapter 7 companion](https://www.realtimerendering.com/#shadows).
- *GPU Gems*, vol. 1, Chapter 11, describes comparison filtering and the growth of projection aliasing on grazing receivers. This supports bounded receiver coverage and comparison filtering of continuous coordinates. [Bunnell and Pellacini](https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing).
- *GPU Gems 2*, Chapter 17, shows a single-map PCF path and adaptive sampling. Use its small comparison kernel as a cost baseline. A sparse uniform probe can miss a thin or lightly covered caster, so qualify that optimization on real alpha detail. [Uralsky](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-17-efficient-soft-edged-shadows-using).
- *GPU Gems 3*, Chapters 8 and 10, cover filterable variance maps and separately rendered cascade sections. Start with depth comparisons for fine, high-contrast structure and a small filter. Leave moments for a measured wide-filter case because moment bounds can leak light and summed-area setup adds work. Use the cascade work as evidence for scoped per-page culling and rendering. Its view-frustum split selection serves a different quality target from a fixed world receiver. [Lauritzen](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-8-summed-area-variance-shadow-maps), [Zhang et al.](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus).
