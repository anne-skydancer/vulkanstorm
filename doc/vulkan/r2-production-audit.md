# R2 production audit and closure

R2 is in progress and is **not qualified**. R0/R1 acceptance does not certify
these new production paths. The acceptance requirements remain those in
[the full parity roadmap](full-parity-roadmap.md#r2--geometry-avatars-and-appearance).

The audit follows the actual connected scene, inventory and input routes. A
standalone producer or a passing diagnostic cannot replace a missing viewer
route. Source compilation is distinct from full build, staging and runtime
qualification.

| Requirement | Implementation work and remaining acceptance work |
| --- | --- |
| Prim/linkset geometry and LOD | Native scene now selects the shared volume LOD and admits replacement CPU/GPU generations. Complete topology/LOD comparisons and transformed linkset tests remain. |
| Sculpt topology and image lifetime | Native map fetch/publication feeds the shared tessellator, retaining mirror/invert and bottom-row-first maps. Progressive image, degenerate topology and replacement comparisons remain. |
| Flexible geometry and wind | The flexible solver is extracted into a CPU kernel used by both renderers. Native extra parameters, simulator wind and path generations are wired. Differential solver/LOD, scaling, attachment and retirement checks remain. |
| Trees and grass | Shared CPU species producers are extracted from the reference and used by both renderers. Native simulator decoding, LOD, terrain-change rebuilds, masks and publication are wired. Species/seed/LOD and retirement qualification remains open. |
| GLTF scene ownership | A native owner fetches simulator scene/buffer assets and uses the shared CPU decoder, scene hierarchy, TRS animation and skins. Shared CPU-mode GL calls are removed. Local preview and encoded image routes are wired. Parser allocations and typed metadata are admitted before decoding, and buffer completion is keyed by requested asset identity. Runtime replacement and budget-exhaustion coverage remain open. |
| GLTF topology and animation | Native point/line/triangle pipelines and strip/fan conversion are implemented. Shared single-frame, STEP and cubic animation behavior is corrected. Deterministic topology, accessor and interpolation tests remain. |
| System-avatar body and motion coverage | Shared standing/walking/falling/emote factories are registered. Procedural motion factories, sitting/ground/hover and cloth production paths are implemented. Jellydoll transitions use shared default/reset parameters, simplified motions and restoration. Skeleton reset, stop/resync and authenticated remote reset routes use the real native owners. Remote attention retains the reference lifetime and avatar head/pelvis targeting. Body/visibility, cloth and physics still need runtime qualification. Body branches must be checked individually against the reference. |
| Rigged/fitted attachments | Shared joint position/scale arbitration and pelvis restoration are wired. Linkset-wide overrides, animated bounds, invalid skin data and detach/replacement comparisons remain. |
| HUD attachments | Native screen joints, projection, depth isolation and picking are wired. Resize, zoom, masking, ownership and input parity remain open. |
| Animesh | Real ObjectAnimation transport and an independent CPU skeleton owner are wired. Linked meshes and shared legal offset/size constraints are implemented. Visibility, ordering, animated bounds and retirement qualification remain open. |
| COF/wearables and attachment queues | Production inventory action routes, decoded wearables, rez/detach queues and appearance synchronization are wired. Shared outfit filtering and lock policy, bounded pending COF requests, late-failure rollback, layer ordering, Take Off/Detach All, temporary attachment accounting and attachment labels are wired. Connected action and callback lifecycle qualification remain open. Metadata receipt alone does not satisfy this requirement. |
| Local compositor/bakes | Shared layer/parameter owners now have a CPU compositor, local masks, generation snapshots and native server/legacy bake publication. Pixel comparisons, legacy capability/asset-store upload coverage and codec/resource accounting remain open. |
| Animation/gesture/AO/script controls | Ordinary animation/gesture/AO and script-control routes use the native character/session owners. Delayed AO folder/import callbacks now have account/set/import guards; import abort and failed-link handling are corrected. Connected runtime and callback retirement checks remain open. |
| Changed-generation publication | Avatar pose/morph/shape/LOD keys, rigged face keys and rigid parent transforms are wired. Atomic body, object-face and glTF publications preserve admitted generations on refusal. Complete admitted face topology survives replacement, and terrain commits across region owners together. A rejected body generation retains its complete previous part list; a successful topology change retires removed parts. CPU refusal/retirement checks pass; runtime unchanged-pose and GPU retry checks remain. |
| Animated picking and selection bounds | Bounded CPU snapshots describe exact admitted GPU generations. Native screen picking uses these rather than the GL pipeline. Avatar and glTF face snapshots now track the actual submitted topology, including face removal. Complete selection, HUD and animated geometry checks remain. |

## Current local evidence

The canonical `build-vc170-64` RelWithDebInfo viewer has completed full builds
and staging checks with the connected appearance and reset routes. Changes
remain in progress, so these builds do not establish acceptance of the final
source. This development checkpoint has not been qualified in CI and does not
mark R2 complete. Final qualification and source provenance remain outstanding.

The connected headless replay now fetches and applies the four body wearables
through actual inventory, asset and avatar owners. It passes ordinary shape
replacement, adding layered shirts, changing layer order and removing a shirt.
The compositor failure was caused by charging avatar composites to the UI
image pool. Composites now have a separate bounded pool; unchanged bake
dependencies retain their publications. The replay passes all ten applicable
two-stage capability uploads, appearance publication and skeleton reset without
losing the worn shape. The seed/admission lists now include `UploadBakedTexture`.
Queued removal revalidates the account and source asset while allowing benign
inventory revision changes caused by ordering and label updates.

The replay also fetches a real keyframe asset, requests animation through the
connected transport and receives the simulator animation response. It verifies
that admitted body vertices move and that selection bounds match the actual
published GPU geometry. These are local software-driver results, not complete
R2 qualification. A subsequent connected replay passed all seven retained glTF
topologies, strip/fan preparation, hierarchy and skin animation, rejected and
empty scene replacement, all eight HUD points, rigged/fitted motion and detach
restoration, and independent animesh animation. It also passed the actual
legacy bake xfer protocol, exact GPU readbacks for ten applicable composites,
and uploaded JPEG2000 color/morph-mask comparisons (maximum observed error
three byte units), followed by gesture animation transport. That replay stopped
at AO set loading; AO, script controls and final retirement are not yet qualified.

The connected tests exposed two production gaps now fixed: native glTF consumes
the shared decoder's prepared topology rather than reconverting its original
file mode, and completed mesh skin metadata is admitted before skeleton
overrides and the first rigged publication.

Complete producer/LOD coverage, negative cases, AO and retirement coverage
remain open. The R2 runner rejects missing complete R2 evidence
independently of a passing R1/UI result. It is not enabled as a qualification
job while the complete R2 replay remains unfinished.

The standalone test linked to the viewer's installed libraries passed 136
primitive/sculpt cases, terrain patch/LOD/neighbor-stride cases, all 21 shipped
tree species, grass cases and supported flexible simulation/render resolutions.
CPU fixtures also passed for the compositor, surface picking, wind, control
constraints, replacement ownership, terrain publication transactions, parser
allocation refusal, concurrent wearable admission, native control transport and
geometry artifact completeness. Shared lock-policy, authenticated avatar reset,
whole-body topology refusal/retry and the mutable AIS/bake fixture checks pass. These checks
are useful producer evidence, but do not qualify connected appearance actions,
animated presentation or GPU readback parity.

The original ledger assigned every row in the survey's geometry table to R2,
including the complete particle/composition row. The roadmap explicitly assigns
particle/ribbon/beam simulation and composition to R7; the generator now assigns
that unchanged obligation to R7. No particle behavior is removed from acceptance.
Terrain and vegetation keep R2 geometry obligations, with explicit R3 material
and R6 shadow obligations; their whole survey rows remain unqualified until all
assigned phases are covered. R2 terrain LOD, joins and picking still require
production closure and differential qualification.

Qualification must cover all producers and LODs, topology replacement,
appearance actions on the real connected avatar, attachment lifetimes,
animated visible triangles and selection bounds. Keep material and shadow
classifications available for their subsequent roadmap phases. Run UI checks
headlessly; do not interrupt a user's running viewer or claim physical vendor
driver/performance qualification from software Vulkan results.
