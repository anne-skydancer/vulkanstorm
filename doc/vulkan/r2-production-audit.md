# R2 production audit and closure

R2 remains **in progress, not qualified**. Its requirements are the geometry,
avatar and appearance obligations in [the parity roadmap](full-parity-roadmap.md#r2--geometry-avatars-and-appearance).
R0/R1 acceptance does not certify these changes. The pushed implementation
checkpoint was `e0794c1e92`. The expanded contract passed locally against clean
source `4ab2691708`; three-platform CI and the subsequent compatibility fixes
still require qualification.

## Production paths and acceptance coverage

| Requirement | Production integration and acceptance checks |
| --- | --- |
| Prim/linkset, mesh and sculpt | Native simulator owners use shared tessellation, adaptive LOD, mesh face/skin metadata and sculpt maps, including mirror/invert. Shared volume LOD now preserves the reference close-distance ramp, static mode, distance factor, selection override, HUD detail and animated rigging bounds. The viewer-linked geometry executable covers 136 primitive/sculpt cases and all supported LODs. Publication and surface-intersection regressions exercise actual production methods. |
| Terrain | Shared patch topology covers 16/32-sized patches, every stride and north/east joins. Native publications commit atomically across region owners and preserve the previous complete frame on CPU/GPU refusal. The final audit corrected shared-edge bounds and disabled adaptive LOD behavior. These changes pass the publication regression; their rebuilt-viewer verification remains outstanding. Terrain materials and shadows retain R3/R6 obligations. |
| Flexible geometry, trees, grass and wind | Both renderers use the extracted CPU producers. Native extra parameters, wind, scaling, masks and terrain-change invalidation are wired. Shared tree LOD preserves reference thresholds and distance policy; stopping vegetation retires its publication and resumption rebuilds it. Producer tests cover simulation/render resolutions, forces, all 21 tree species/four LODs, repeat/transforms and grass paired faces. |
| glTF scenes | Native owners use the shared CPU decoder, referenced buffers, hierarchy/TRS, accessors, skins and animation. The connected replay covers seven source topologies, prepared strip/fan triangles, hierarchy and skin motion, rejected/empty replacement and owner retirement. Allocation and publication regressions cover refusal and asynchronous ownership. Materials remain R3. |
| Self/other system avatars | Real skeleton/morph/bake-mask owners publish all eight body branches. Connected checks cover every shipped mesh/LOD, ordinary shape replacement, skeleton reset, all remote body branches, animated vertices and exact selection unions. The shared body LOD policy includes self/remote detail and visible-avatar load. Visibility checks cover limits, friends, explicit overrides, both animesh exclusions and jellydoll restoration. |
| Specialized motion, cloth and physics | Registered standing/walking/falling/emote factories and procedural controllers serve native characters. Connected checks exercise 37 typed cases, female/male and old/new locomotion, sit remapping without another animation packet, all facial emotes, physics morph updates and object/ground sitting with hover. Cloth displacement/normal comparisons use independent reference shader calculations. The audit fixed pending standing-animation loads incorrectly disabling their factory, and active sex-dependent sit remapping. |
| Rigged/fitted, HUD and animesh | Real attachment queues and simulator updates establish fitted joint overrides and animated geometry; ordinary detach retires geometry, COF links, queue ownership and restores joint transforms. Replay covers all eight HUD attachment points/projection/picking and independently owned animesh animation/retirement. Shared control-avatar legal constraints and exact admitted bounds have separate regressions. |
| COF, wearables and compositor | Ordinary inventory actions fetch/decode the four body wearables, replace shape, add/order/remove layered clothing and wear skirt/physics items. The CPU compositor uses local masks and dependency generations; avatar composites have their own bounded image pool. Replay captures all eleven applicable GPU composites and compares their actual bytes, then compares JPEG2000 color/morph-mask uploads for capability and legacy xfer transports. |
| Animation, gesture, AO and script controls | Real asset requests, simulator animation responses, gesture playback, AO inventory state links/overrides and script-control acquisition/release are replayed. The audit fixed asynchronously populated AO sets being skipped after their first creation. Scoped callback/account and import-failure regressions cover retirement. |
| Changed generations and GPU ownership | Atomic publications retain complete prior topology on refusal; unchanged pose reuses geometry. A tiny real Diligent buffer pool proves invalid-input rejection, budget refusal, retained-owner blocking, release/retry and zero byte leases on retirement. Direct vertex/index GPU buffer readbacks compare byte-for-byte with inputs, alongside CPU snapshots used by picking. Logout checks retire scene, assets, skeletons, composites, pending operations and controls. |

## Development evidence and limits

The canonical RelWithDebInfo viewer has completed builds and staging checks.
The complete `.tmp/r2-attachment-gpu-contracts` development replay passed the
ordinary attachment routes, all 31 shipped body mesh/LOD entries across 128
part/viewport cases, eleven bake readbacks/codec comparisons, remote motion and
visibility restoration, glTF/HUD/animesh, AO, script controls and logout.

The expanded `.tmp/r2-specialized-loading-contracts` viewer exited successfully
and recorded all 37 specialized-motion cases, actual physics-driven weights,
object/ground sitting and hover, direct GPU vertex/index byte comparisons and
logout retirement. Its original runner result failed because the expected
emote labels omitted the animation catalog's `_emote` suffixes. Correcting that
explicit expected-name list allows the retained contract to validate; the
original failed result is preserved. A fresh run with the corrected runner and
the subsequent terrain fixes remains required.

These are development-source checks, not final qualification. Motion assets in
the local simulator fixture are explicit synthetic keyframe clips, including
the required standing IK joints. They exercise the actual registered factories
and transport; they do not certify arbitrary live-grid animation content.
Headless replay does not establish native presentation or physical AMD/NVIDIA
performance. Presentation, validation and deterministic drawing remain separate
software-Vulkan workflow gates.

## Final qualification gate

The dedicated R2 workflow step runs headlessly on Windows SwiftShader, Linux
SwiftShader and Linux Lavapipe, separately from R1 and the connected skin matrix.
It must pass together with the viewer-linked geometry executable, staging,
existing native presentation/validation tests and prior acceptance gates.

The clean-source `4ab2691708` build, complete stage and viewer-linked geometry
gate passed. Its full headless R2 replay also passed, retaining 38 hashed
artifacts, all 37 specialized motions, physics/sitting/hover and logout checks.
The independently rebuilt software harness passed offscreen/world rendering
and its validation, synchronization, pixel and orientation negative tests.

The prior R1 replay exposed an outdated fixture assumption: its unsupported
object was a tree, which R2 now implements. That fixture now sends an unknown
wire PCode while retaining the no-substitute/identity assertion. The Ubuntu
source gate also exposed a missing explicit `<optional>` include in the
standalone mesh-decoding fixture. These compatibility corrections do not waive
either gate and require a fresh final-source replay/CI result.

Run [38651748416](https://github.com/anne-skydancer/vulkanstorm/actions/runs/38651748416)
passed the complete Windows SwiftShader job, including R2 and evidence packaging.
Both Linux jobs stopped during the build: GCC rejected an unnecessary terrain
loop copy and mixed point/look enum conditionals; the standalone geometry target
also lacked application-owned settings and name-presentation link symbols under
Linux LTO. The corrections preserve strict compiler flags and the real geometry
libraries. The test supplies its own settings owner and rejects any unexpected
name-presentation call. Linux runtime acceptance remains outstanding.

The replacement run, [38684802492](https://github.com/anne-skydancer/vulkanstorm/actions/runs/38684802492),
passed the Linux builds and reached their connected skin matrices. Windows
crashed with an access violation while opening transcript preview in Firestorm
High Contrast. Inspection exposed worker-thread mutation of preview widgets and
an unguarded preview pointer in the completion callback. Transcript results now
cross a synchronized mailbox scoped to each open preview; the UI thread alone
updates controls, and close/destruction releases the message pointer before
starting deletion. The loader owns its filename independently of the preview,
and workers stop before their synchronization and callback state is destroyed.
History completion uses a synchronized predicate wait to
avoid lost notifications. Threaded regression checks cover publication,
close/reopen/destruction and completion before/during multiple waits. A complete
runtime rerun remains required; a failed skin case is not waived.

Native Windows exception tracing identified an uncaught std::out_of_range
in VSUIResources::fontBatch after initial atlas publication was refused.
The adapter had been replacing a complete GPU texture and CPU snapshot for
successive glyph additions, retaining overlapping generations until GPU
completion. That differs from LLFontFreeType::addGlyphFromFont, which updates
the existing OpenGL atlas with setSubImage.

The Vulkan font adapter now uses the same local FreeType glyph generation,
packing, cache generations and page ownership. Each raw atlas page owns one
mutable Diligent texture. Glyph additions collected during a frame produce at
most one update per changed page before drawing; unchanged pages do not upload.
Diligent resource transitions order previous reads, the transfer and subsequent
reads on the immediate context. Submission fences retain pages through GPU
completion. Cache reset establishes a new page; queued draws retain the old one.
Ordinary skin/world image generations remain immutable. A genuine font-atlas
allocation failure raises an explicit renderer error rather than presenting a
successful frame with omitted text. No image budget or deadline is enlarged.

Compiler-backed production-method regressions cover local grayscale/color
conversion, orientation, DPI, repeated glyph additions, one allocation per page,
coalesced updates, unchanged-page reuse and retained draws across cache reset.
The software Vulkan harness additionally exercises repeated updates interleaved
with GPU draws, stable texture identity and charged residency, deterministic
readback and retirement. The production-method regressions pass locally (2/2), and the headless
SwiftShader harness passes all six modes, including 65 atlas updates with one
texture generation, constant charged residency, pixel readback and retirement.
Final-source connected-viewer and three-platform CI qualification remain
required before this correction is accepted. The earlier retry/capture guard
alone was insufficient; solid-fill refusal still prevents incomplete captures.

The evidence packager requires complete R2 contracts, all readbacks and their
hashes, transport receipts, source revision, build provenance and staged viewer
hash. It revalidates artifacts without rewriting their manifests. A passing UI
or R1 replay cannot substitute for R2. Dirty-source development results cannot
satisfy the final provenance gate. Final-source compatibility runs and CI are
still outstanding; R2 is not marked complete by these development checkpoints.

The font-atlas run `38719485889` failed the Windows and Linux Lavapipe UI/skin
gates at the same stale diagnostic assertion: it required a glyph addition to
replace its GPU generation. The diagnostic now requires reuse of that atlas and
unchanged charged residency. Independent Unicode glyph pixels, ordinary-image
replacement and delayed-completion checks remain required. The production-method
regression also executes these actual diagnostic assertions, so their contract
cannot silently diverge from the adapter again. Fresh graphics CI is required.

The same build exposed a global minimal `VkResult` declaration conflicting with
the installed Vulkan header under Linux LTO. Native builds now use the canonical
header; OpenGL-only loader probes retain private SDK-free declarations. Terrain
grid arithmetic, nearest-power-of-two string-table rounding and coroutine queue
allocation now reject invalid or unrepresentable counts before allocation.
Compiler-backed production-method checks cover their ordinary and extreme
inputs; Linux LTO warning clearance remains subject to the fresh platform build.

Run `38739799255` passed the complete Windows graphics job. Linux Lavapipe
built and staged successfully, then failed startup repeatability: the embedded
login browser changed from blank pixels to a public-DNS error page between the
five captures. OpenGL's five captures were stable. The baseline now serves a
hash-pinned static login page on loopback through the existing `ForceLoginURL`
setting for both renderers. Captures wait for actual CEF paint and publication;
every GPU frame must also contain the background and two positioned colour
witnesses. Blank, unpainted or error pages fail. The original one-unit pixel
tolerance, five consecutive samples, unmasked comparison and deadlines remain.
HTTP request receipts, fixture bytes/hash and observed URL/readiness are retained.
Local regression checks pass; fresh platform runtime qualification is required.

The acceptance ledger retains later material, shadow, particle and composition
obligations. Terrain/vegetation rows span R2/R3/R6; particles/ribbons/beams remain
R7. No later feature is waived or certified by R2 geometry evidence.
