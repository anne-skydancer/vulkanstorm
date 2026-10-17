# R3 production implementation and acceptance

R2 software-device qualification is complete. R3 is in implementation; this
record does not certify R3 or full rendering parity.

## Persistent texture-cache subscriber

`VSUIImageProvider` now reads encoded UUID entries through the viewer-owned
`LLTextureCache` before its existing HTTP fetch and asynchronous decode path.
It accepts complete J2C entries within the existing 16 MiB transport bound.
Partial entries remain available to the reference streaming fetcher; this
full-resolution subscriber falls back to HTTP. Malformed complete entries and
failed cache decodes are invalidated before retrying over the network. Explicit refresh
invalidates disk content and bypasses a second cache read.

Validated network images are written through `LLTextureCache::writeToCache`
with the decoded raw image and discard level zero. The cache retains its own
version, encoder, size, location, eviction and read-only rules. No additional
disk format, cache directory or raw-image database is introduced. Encoded write
bytes and their CPU reservation survive until the cache worker retires.

The existing native frame pumps the shared cache. Cache callbacks own only a
completion mailbox, not the viewer/provider/account. The subscriber checks
account, region epoch and request identity after suspension and before decoded
publication. Shutdown never calls a retired cache pointer. Shared UUID cache
identity follows the reference viewer; account retirement clears decoded/native
resources rather than inventing an incompatible account-specific disk key.

Resource evidence includes disk hits, misses, writes started, pending writes and
network requests. A write-start counter is not proof of durable completion.

Current checks compile both changed production translation units using the
canonical RelWithDebInfo viewer project. A compiled regression exercises the
actual cache bridge for complete, partial, inconsistent, oversized and wrong-codec
entries, read failure, timeout, late callback and shutdown, plus encoded-write
buffer/reservation lifetime. Existing HTTP-limit tests remain required.

Still required: real cold/warm process-restart replay with identical pixels and
network counts; corrupt/truncated recovery; clear/resize/read-only and pending-I/O
shutdown; grid/account transitions; progressive range/discard demand. These checks
must exercise the staged viewer and real cache, not just the bridge fixture.

## Material and animation production checkpoint

Native scene faces now publish immutable material snapshots with all four PBR
channel identities, factors, alpha state and independent UV transforms; legacy
normal/specular IDs and transforms, fullbright, emissive-alpha, bump/shiny/glow
and specular/gloss factors are retained. Legacy assets use bounded regional
RenderMaterials acquisition and the shared LLMaterial decoder. PBR assets use
the shared material asset loader and LLGLTFMaterial override application.

Object extra parameters supply material asset IDs. GenericStreamingMessage
material overrides are admitted on their simulator circuit, validated before
face mutation, queued with bounded region ownership when necessary, and retired
with objects/regions. Texture animation uses LLViewerTextureAnim's existing
state machine; its transforms are applied to packets, without per-frame mesh
recreation. Material-aware masked selection samples the published transform.

The production shader binds base, normal, ORM/specular and emissive channels,
with explicit encoded/linear conversions, alpha handling and independent UVs.
Geometry now retains UV1, color and tangent channels for glTF. UV transforms
transpose textureUtilV.glsl's handedness and animation order. The headless GHI
suite passes material opacity, linear factors, emissive and UV readbacks, as
well as its existing renderer and negative validation tests. Native Windows
translation units compile. This does not yet establish all R3 acceptance.

## Open R3 work to complete before qualification

- Finish tangent-path/reference comparisons, per-channel sampler/address/mip
  choices, anisotropy and texture-animation variants; qualify all material
  variants including rigged geometry and terrain PBR.
- Progressive mip/discard/range demand, bounded GPU mip residency, compression
  policy, eviction and local reload.
- Dynamic media stride/format/origin and dirty-region publication shared with
  world media and previews.
- Inventory and connect R3 settings listeners to observable native changes.
- Real disk-cache cold/warm restart and pixel/network evidence, corruption,
  clear/resize/read-only, account/grid and pending-I/O shutdown cases.
- Production-viewer replay for material transport, overrides, channel ownership,
  image replacement and settings; cross-platform software qualification.

BRDF and probe-dependent appearance remain R5/R6 obligations. A saved preference
with no production effect does not pass. Retain strict validation and ownership
budgets throughout. Checkpoints are commits, not milestone completion.

## Sampling production checkpoint

Native remote and avatar image generations now carry bounded mip chains. Material
samplers retain independent channel filtering, repeat/clamp/mirror addressing and
glTF mip selection. RenderAnisotropic controls device-clamped anisotropy; sampler
views belong to their immutable image generation. Media and UI remain single-level.
GPU accounting includes every mip level. Headless production-renderer readbacks
pass repeat/clamp/mirror and minification charts, plus descriptor checks for the
actual device anisotropy limit. The affected native viewer sources compile.
Progressive fetch/discard, compression, settings inventory and the remaining R3
acceptance cases above remain open; this checkpoint does not qualify R3.

## Shared media image publication checkpoint

LLMediaCtrl and LLViewerMediaImpl now use one native publication controller.
Initial and resized plugin frames publish complete immutable generations;
subsequent updates copy only the clamped dirty rectangle, respecting padded
stride, RGBA/BGRA order, opaque RGB and plugin coordinate origin. A refused
initial/resize/patch upload preserves the previous generation and plugin dirty
state for retry. Controls tolerate an initial refusal without dereferencing a
missing image. Plugin destruction retires its named native resource; packets
already submitted retain their own generation. Media uses the remote-image
budget and a single mip level, matching the reference media policy.

Compiled regressions execute the production controller for refusal, retry,
shared-consumer reuse, resizing and unsupported formats, plus independent dirty
region/orientation fixtures. All changed viewer translation units compile in
the canonical RelWithDebInfo build. This supplies image publication to world
consumers; ObjectMedia acquisition, native media interest and actual world-face
binding are still required before world media can be qualified.

## Object media production checkpoint

Native object faces now acquire ObjectMedia metadata on their own simulator
circuit and retain the reference LLTextureEntry/LLMediaEntry identities and
LLViewerMedia::updateMediaImpl policy. Metadata refresh preserves plugin identity
and the previous URL, while removal retires the instance's surface interest.
Responses require current account, circuit, object generation and requested
media version. Requests, response size, face count and retained metadata are
bounded. Losing an object/region cancels its metadata owner.

Visible native surfaces supply projected interest and distance to the existing
media scheduler; instance/CPU limits, autoplay, enabled, focus and minimize policy
remain shared. Object mute and expired visibility remove interest. Active media
image generations replace the base channel (and PBR emissive channel), while
other material channels keep their own publication requirements. An unavailable
plugin leaves the reference static face texture available. Media pixels use the
shared publication controller above.

Compiled production-block regressions cover metadata identity preservation,
invalid multi-face response rejection, removal and re-addition, and native
interest expiry/muting/proximity. The native sources and existing R2/cache/asset
regressions pass. Actual staged-viewer plugin/world-media replay and complete R3
cross-platform qualification remain open. This is not a claim of R3 completion.
The uncompressed object parser also now rejects malformed render-material data
instead of allowing a later independent geometry result to overwrite failure.

## Shared progressive fetch/cache production checkpoint

The earlier full-file native cache bridge has been replaced by native subscribers
to LLTextureFetch's existing CPU state machine. The shared worker now performs
partial cache reads, range fetches, J2C discard decoding, corruption recovery and
cache writes for native images. Explicit native capability URLs retain UUID
cache identity without entering LLWorld or UDP resolution. OpenGL construction
and request defaults remain unchanged.

The native instance retains the 16 MiB transport/encoded ceiling, maximum 32
requests, 4096-pixel dimensions and a 256 MiB decode admission budget. Oversized
responses terminate rather than entering ordinary HTTP retry handling. Image and
auxiliary-mask publication is immutable, guarded by account/region generation,
and keeps the admitted GPU generation on refusal. Projected world demand is
combined per frame; UI subscribers retain high-resolution demand. Resolution and
full-resolution preferences affect requests and resident replacement, with a
one-level projected-demand hysteresis. Inactive world demand does not perpetually
start requests. Fetch, cache and decode workers are pumped and retired in order.

Native startup now uses the shared cache version, location/relocation, size,
clear, codec-version and read-only policy. Only the three audited CPU disk-cache
size/watermark listeners are additionally admitted; GL listeners remain gated.
Replay profiles set both current and pending cache locations to their isolated
cache directory, so shared relocation policy cannot redirect qualification data.

Native translation units compile. Regressions execute the production request
policy (partial/unknown/full discard, native queue/byte bounds and unchanged GL
defaults), native decode admission/corrupt-header recovery, and decisive HTTP
limits. The obsolete bridge-specific fixture was replaced because that bridge
no longer exists. R2 geometry/asset/account and preference/parity-profile checks
pass. The previous media checkpoint's fully staged viewer passed connected
session replay offscreen in 13.8 seconds with source-qualified evidence.

Still required for this new checkpoint: rebuild/stage and actual-viewer replay,
real cold/warm process-restart evidence with pixel/network comparisons, range
streaming fixtures and clear/resize/read-only/transition/pending-I/O cases.
Compression, tangent/reference charts, complete terrain PBR and the remaining R3
settings/production qualification obligations also remain open. This is not R3
completion or cross-platform qualification.

## Shared-fetch runtime correction and UI sampling contract

The first actual-viewer replay of the shared-fetch checkpoint exited with an
access violation. A first-chance native debugger trace located the call to
LLViewerStatsRecorder::textureFetch: native startup had not created that shared
CPU service. Native startup now creates it before texture fetching, and teardown
retires it after the fetcher. The fetch statistics path remains active.

Reference LLUIImageList::getUIImageByID/loadUIImageByID explicitly uses MIPMAP_NO.
The native UI sampler therefore clamps to level zero even when an image also
contains world mip levels. Explicit nearest material filtering also remains
nearest when global anisotropy is enabled. An independent minified-image pixel
chart and sampler descriptor check pass, along with the six headless production
renderer/validation/negative modes. The corrected app source compiles; the staged
viewer must be rebuilt and replayed before this correction has runtime acceptance.

The rebuilt recorder correction removed the crash, but replay then exposed a
channel-contract error: the native subscriber requested an auxiliary channel for
ordinary four-channel J2C images. The shared decoder correctly rejected the
nonexistent fifth channel, leaving the real inventory texture preview waiting.
Native decode admission now derives the auxiliary request from the validated
header (five channels only). The compiled production-block regression explicitly
checks both RGBA and five-channel mask requests, alongside the existing bounds,
budget and corrupt-header cases. The stalled automated replay was ended after
its cause was established; its evidence remains a failure, not a qualification.

The auxiliary-channel correction was rebuilt and staged at `0989c8061c`.
Actual-viewer connected-session replay passed headlessly in 18.8 seconds with
source-qualified evidence. This closes that particular runtime failure.

## Persistent-cache qualification checkpoint

The viewer now exposes diagnostic-only GPU readbacks for resident image
generations, actual shared-cache startup policy/capacity and a fresh process
identity. A replay exercises projected discard demand, full-resolution preference
changes and maximum-resolution changes against a locally encoded, lossless
1024-square pattern. The loopback transport supplies real partial HTTP ranges;
the independent validator checks original expected pixels, readback files,
checksums, network traffic and process identities.

The bounded restart runner covers cold/warm reuse, read-only access, truncated
body recovery, corrupted cached-header recovery, clear, resize, cache-version
and codec-version changes. Read-only injection exercises the existing shared
sibling cache policy in an isolated headless process; it does not qualify
concurrent live-viewer arbitration. These are qualification paths, not substitute
cache implementations. Selected viewer sources compile, and range/artifact/
mutation regressions pass. Actual restart acceptance requires the committed,
rebuilt and staged viewer; no restart result is claimed yet. The remaining R3
production requirements listed above remain open.

The first actual cold run failed the coarse-resource assertion. It exposed
Windows filesystem extension parsing of explicit capability URLs: hostname dots
were treated as a non-J2C extension, forcing discard zero. Native requests now
strip query/fragment and filesystem-independent URL path components before the
existing codec/size policy. Legacy callers retain their previous behavior. The
compiled request regression includes dotted hosts, dotted parent paths, query
suffixes and actual PNG/J2C extensions. Restart acceptance remains pending the
corrected staged executable; the failed cold-run evidence is retained.

The URL correction was rebuilt/staged at `1a28168cd5`. Connected replay passed in
14.6 seconds, with actual 600-byte initial and overlapping continuation requests.
The cache validator then correctly rejected incomplete evidence: the final
snapshot followed logout/relogin, after account image retirement. Connected image
evidence is now captured before logout, and a separate retired snapshot must
show zero image generations and pending fetches. Partial J2C quality can improve
without changing discard, so the coarse stages compare against independent
reference-codec decodes of their exact partial or complete codestream windows.
Full-resolution pixels still compare against the original lossless pattern.

## Reference tangent basis and separate imported occlusion

Primitive normal bases transpose `textureUtilV.glsl`, including KHR rotation and
scale signs followed by normalized SL texture-animation rotation/scale. Imported
glTF uses its distinct rotation-only reference tangent transform. The native
vertex shader applies these transforms before interpolation. Static primitives
use `LLVolume::genTangents` and the reference normal matrix; rigged geometry
skins tangent vectors through the same bind/palette matrices as the reference
path. The shared geometry description now records actual optional tangent,
UV1 and color channels instead of silently dropping their presence.

Imported glTF occlusion has its own image, UV set, transform and sampler. Ordinary
prim PBR retains packed ORM semantics. Native publication validates all imported
material-channel references and finite factors/transforms, and unbound texture
slots do not require nonexistent UV1 data. Recycled descriptor bindings release
every material image after completion. Shared face classifications expose the
actual sampler and tint encoding.

Production shader pixel charts pass for a constant-UV tangent basis, rotation,
mirroring, independent UV1 occlusion and its transform. Constant UVs prevent a
derivative-only implementation from passing these tangent tests. All six local
headless renderer/validation/negative modes pass; the runner requires the new
material readback artifacts. Selected production sources compile, and basis,
publication, R2 fixture and evidence regressions pass. Full rebuilt-viewer cache
restarts and real production material transport/override acceptance remain
required, alongside the remaining R3 terrain, compression, reload, settings and
lifecycle work. This checkpoint is not complete R3 qualification.

## Measured persistent-cache restart acceptance

The complete nine-case restart matrix passed against the staged viewer at
`9c90683b5f` in 123.1 seconds on the local pinned software Vulkan runtime. Every
case used a fresh process and source-qualified build evidence. Cold startup
issued a real initial partial range and continuation; warm, read-only and resize
runs issued zero texture HTTP requests. Read-only left encoded cache files
unchanged. Truncated bodies, corrupted header bytes, clear and both version
changes recovered through the shared fetcher. Discard/preference stages matched
the exact reference-codec windows; full-resolution GPU bytes matched the original
lossless pattern. Every case verified account resource retirement after relogin.

The dedicated three-platform workflow now runs this bounded matrix and archives
its case logs, settings, encoded fixtures, reference pixels and GPU readbacks.
Evidence packaging independently requires every case, source/executable identity,
unique process identity, network policy and the actual pixel files. Read-only
before/after hash witnesses are retained explicitly. Cross-platform acceptance,
actual concurrent-instance arbitration, distinct-account/grid policy and shutdown
with pending cache I/O remain separate open requirements. This records the
completed cache subset without marking R3 complete.

## Local bitmap file reload production path

Native local bitmap creation/reload now reuses `LLLocalBitmap`'s decoder,
timestamp checks, incomplete-file retries and watcher. Successful publication
keeps a stable UUID/UI facade while replacing an immutable mipmapped native
image. Refused publication leaves the last valid image and file timestamp intact.
Local images do not enter the HTTP fetch/cache queue or expire with an account.
Deleting a local file owner retires its resource and supplies a bounded alias to
the ordinary default asset for existing native consumers; callbacks retain the
normal deletion notifications. Static faces, glTF, sculpting, terrain,
vegetation and avatar consumers resolve that alias at the resource boundary.
OpenGL retains its existing local-texture implementation.

A diagnostic replay uses the actual production file decoder/watcher and held UI
facade, with initial PNG, resized file, incomplete write, recovery and deletion.
Independent GPU quadrant/orientation checks and generation transitions are
required; an old retained generation must remain unchanged. The cold-cache CI
case runs this production reload exercise, and archive acceptance requires its
pixel artifacts. Selected production/replay sources compile and publication/
artifact regressions pass. Rebuilt-viewer runtime acceptance remains pending.
Local glTF material-file reload is a separate remaining production path.

The canonical rebuild/stage at `d39a2fcff7` passed the expanded nine-case matrix
in 132.1 seconds. Its cold run executed the real local PNG reload/refusal/
recovery/deletion checks and independent GPU pixels in 16.2 seconds. All restart
cases remained source-qualified, and the archive's independent R3 completeness
validator accepted the collected case files with no errors. Read-only before/
after cache hashes are present. This qualifies the local bitmap/resource and
restart subset on the local pinned software runtime; it does not qualify local
PBR file reload, all production material/terrain paths, or cross-platform R3.

## Worker-side texture compression checkpoint

World remote images can request BC3 when the device advertises usable BC
textures; unsupported devices keep RGBA. The CPU codec is stb_dxt v1.12 at an
immutable revision and SHA256, with its MIT license staged among application
settings. Mip generation and compression run on the existing General work queue;
GPU creation and publication remain on the rendering thread. Scope, generation
and serial checks reject late account/region results. Preparation has a shared
640 MiB reservation pool, with usage reported in resource evidence.

Core UI, font, media and avatar-composite resources remain exact. A world image
subsequently requested by UI is republished from its local CPU snapshot without
another network fetch. Small images stay uncompressed when BC3 would cost more.
Preference changes retain the last valid generation until replacement succeeds.
New GPU uploads retain charged owners through a completion fence even if their
producer drops them before the first draw; eviction skips externally held images.

All six software harness modes passed; BC3 was supported and used by the new
compression chart. Its separately declared eight-unit bound accounts for RGB565
endpoint quantization; existing charts retain their one-unit bound. Replacement
and protected-role checks passed. An independent CPU BC3 decoder verified color,
alpha and padded edge blocks, and malformed preparation payloads were rejected.
Focused cache/replay regressions passed. Actual viewer worker-path replay is
pending the canonical rebuild. Embedded glTF image preparation, local PBR reload,
terrain and the other remaining R3 acceptance requirements are still open.


The canonical worker-path build at `94c1433fa9` passed all nine headless restart
cases in 121.1 seconds, including the real local PNG reload exercise. Deferred
preparation is counted alongside fetch work, so capture waits for publication.

Embedded glTF images now prepare mips/compression on the same bounded General
queue, retain the last publication during policy changes and reject retired
scene callbacks. UUID image sources use projected world demand rather than UI
full-resolution demand; projected size uses the admitted mesh bounds and actual
viewport height. Decoder errors release pending prepared payloads. Selected
sources compile; production glTF image replacement runtime acceptance is pending.
The codec license now has an explicit native manifest entry and a mandatory
staging-file check, with missing-license regressions for both platforms.


The staged `e15d79a2b2` viewer completed all 52 production R2 replay steps and
normal cleanup in 75.2 seconds. Its evidence checker exposed stale R2 constants:
three R3 vertices now occupy 216 bytes (72 each), plus 12 index bytes, rather
than the old 96+12 layout. The fixed expected sizes are corrected to 216/12 and
228 total. Revalidating the retained real GPU readbacks and all R2 archive files
passes with those explicit sizes; byte equality, two refusal checks, two invalid
input checks and zero retained allocations remain required. The original replay
result remains recorded as failed by the old validator; a fresh source-qualified
run with the corrected checker is still required.

## Native local PBR material reload

Local PBR files now retain `LLLocalGLTFMaterial`'s parser, timestamp watcher,
material registry, factors, overrides and retry behavior. The shared CPU
preparation function performs the reference normal/emissive alpha removal and
occlusion-to-ORM packing before the selected backend creates textures. Native
loads use stable channel UUIDs and construct a candidate material without GL
fetched textures. All GPU channels are admitted before any resource-map update;
upload refusal preserves the complete previous channel set and material. Deleted
channels/materials retire native resources while held packets retain their own
generations. Local-texture replacement on a native fetched material uses shared
CPU identity replacement rather than the GL fetch helper.

Native TinyGLTF loads limit individual files to 16 MiB, aggregate file reads to
64 MiB, image dimensions to 2048 and decoded image admission to 64 MiB before
calling the existing image decoder. These guards apply only to native sessions.
The ordinary OpenGL backend continues to create its existing fetched textures.

Production sources compile. A regression executes the actual batch transaction
with refusal on its second upload, duplicate identities and invalid roles; old
publications remain intact. The actual viewer diagnostic exercises material file
load, four GPU channel readbacks, distinct occlusion packing, stable-identity
reload, incomplete image refusal, recovery and deletion. Independent artifact
validation requires every channel's expected pixels and generation transitions.
This runtime exercise is staged for the next canonical rebuild; it is not yet a
runtime qualification claim. Remaining terrain/material transport, settings,
media and cache-lifecycle acceptance are still open.


The canonical `a26275af31` viewer passed the actual local PBR replay in 11.9
seconds, the complete production R2 replay in 74.7 seconds, and all nine expanded
cache restart cases in 129.0 seconds. The cold case includes both local bitmap
and local PBR material file/GPU evidence. These are local software-runtime
results; full R3 and cross-platform acceptance remain open.

Image evidence now separately reports GPU payload bytes, retained CPU snapshot
bytes, shared fetch/decode reservations and worker preparation reservations.
Existing combined admission limits are unchanged. Accounting follows each actual
image generation through held packets and queued GPU work, rather than counting
only cache map entries. The six software harness modes pass, including measured
BC3 GPU-versus-CPU byte deltas. A last-owner regression covers retained and
replaced generations. Archive checks explicitly require every local material
readback among the collected files, as well as validating its disk bytes.

The archive validator accepted the actual complete nine-case file set and rejected
it when a recovered ORM material readback was omitted. Viewer accounting sources
compile; their staged viewer runtime evidence remains pending the next rebuild.


The `2612303cfd` accounting build passed a combined production R2 and local-file
material replay in 75.0 seconds, with source/executable qualification intact.

## Legacy bump publication

Native faces now bind generated normal images for legacy brightness/darkness and
standard bump effects when no explicit legacy normal-map channel is supplied.
The generation pass transposes `normgenF.glsl`'s luminance/inverse-luminance
cross-product kernel and the standard alpha-height kernel, with canonical row
conversion and the reference standard-map U8 truncation. It consumes the actual
admitted GPU source and builds its normal mip chain on the native context.
Standard bump sources use the existing `std_bump.ini` identities and preserve
exact source components rather than compressing their height data.

Generations are cached by source generation, effect, height component and
`RenderNormalMapScale`, with 1024 bounded entries. Refused replacement retains
the complete prior face publication; account reset clears derived generations.
GPU work retains source, destination, bindings and constants through its fence.
Derived normals charge GPU bytes without claiming a retained CPU snapshot.
Native staging now explicitly requires the standard bump source configuration.

All six software harness modes passed, including four new independent central-
gradient pixel charts for brightness, darkness, alpha-height and scalar-height
sources. The existing one-unit pixel bound is unchanged; source images remain
immutable. Viewer face/publication sources compile. Actual viewer bump-face and
account-lifecycle replay remains required. This adds the production path without
claiming full R3 acceptance or implementing the later R5/R6 lighting/BRDF work.

The canonical `9b6a3eba03` legacy-bump build passed ordinary connected UI and
local bitmap/material replay in 13.1 seconds. A production world-bump replay now
sends actual ObjectUpdate/KillObject messages, inspects all six admitted faces,
checks cache sharing, replaces a local source, changes normal scale and records
independent gradient-oracle/GPU readbacks. Account reset must release every
retained derived generation. The cold cache CI case requires this evidence and
its actual archived source/oracle/readback files. Source compilation and evidence
failure regressions pass; runtime acceptance of this added world-face exercise
remains pending its canonical rebuild.


The source-qualified `3d4a3fe532` viewer passed the real world-bump exercise in
12.9 seconds, including all six ObjectUpdate faces, independent GPU pixels,
source replacement, the normal-scale setting, KillObject and account retirement.

## Inactive rendering resource reclamation

The audit found that distance culling dropped primitive meshes while retaining
face image/channel references. Those references are now released with the culled
publication. Inactive glTF scenes release their GPU meshes and embedded images
while preserving the CPU asset; returning to view rebuilds them. An image epoch
rejects late preparation completions, including when an image index is reused
on return. A regression executes the actual preparation/release methods through
that race, preserving externally retained generations and balancing work leases.

Derived bump allocation now reclaims unused derived images and eligible shared
world images under budget pressure, without raising existing limits or releasing
queued/held image owners. The real viewer replay moves its live test object out
of range and must observe that only the cache and its observer retain the source
before deleting it. Compilation and focused regressions pass; runtime acceptance
of the new reclamation check is pending the next canonical build. The glTF
publication fixture also now supplies the actual viewport argument introduced by
the projected-demand integration.

The source-qualified `075ab43575` canonical viewer passed the combined R2 and
local/world R3 replay in 76.9 seconds. All nine persistent-cache restart cases
passed with that executable, including the actual culled-face ownership check
in the cold case. Archive completeness accepted the collected files and rejected
an omitted world-bump GPU readback. This qualifies those paths on the local
Windows software device; it does not establish full R3 or cross-platform parity.

## Hidden UI image residency

Remote image facades retained by hidden controls no longer pin GPU images and
CPU snapshots indefinitely. Under pressure the cache may retire an inactive
payload while retaining its facade, dimensions and subscription identity. A
subsequent draw renews interest and the shared fetch/cache path reloads it.
Hidden retained controls do not automatically restart evicted requests. Metadata
capacity eviction remains separate: it must not remove a subscribed facade.
Local images, recent draws, pending preparation/fetch work and externally held
GPU generations remain protected. Core skin assets and font atlases are outside
this remote-image policy.

A regression executes the production eviction and pending-demand selection
against hidden/recent controls, queued GPU owners, local images, active work and
metadata capacity. It proves stable facade identity and payload retirement,
followed by renewed demand when the control becomes visible. The existing local
image publication regression also passes. Canonical runtime qualification of this
new checkpoint remains pending its rebuild.

The source-qualified `88182641c7` viewer passed the combined R2 and R3 replay in
75.8 seconds after the hidden-control policy change.

The same audit found retained world publications behind hidden attachment/HUD,
invalid-parent and disabled-vegetation routes. They now use the same rendering
resource release as distance culling, while preserving CPU geometry/material
state for return. A production-method regression verifies that queued image
owners survive, idle face/channel/mesh owners retire, both glTF publications
release, and CPU geometry remains intact. Vegetation now renews projected world
texture demand every visible frame and accepts replacement texture generations
without rebuilding an unchanged mesh; it no longer requests its texture as a
full-resolution UI image. Focused regressions and viewer compilation pass;
canonical runtime qualification of these additional world paths is pending.

The source-qualified `4561ab9138` viewer passed the combined R2 and R3 replay in
77.7 seconds, including vegetation, attachment/HUD visibility and the actual
distance-culling image ownership exercise.

## Actual concurrent cache ownership

Qualification now adds two real headless viewer processes sharing a dedicated,
credential-free profile and the same seeded texture cache. The primary completes
its ordinary replay but retains its real APR execution-marker lock behind a
bounded diagnostic completion gate. The sibling must detect the other instance
through ordinary startup, open the texture cache read-only, publish identical
GPU pixels from disk without texture network traffic, and leave the cache files
unchanged while the primary remains alive. No read-only override is passed.
Releasing the gate lets the primary perform its ordinary shutdown.

Shared profiles are accepted only as dedicated evidence siblings containing the
replay ownership marker; ordinary user profiles are rejected. Both viewers remain
headless. The gate is limited to 90 seconds and the existing process deadlines
remain decisive. The dedicated CI archives both executions, identities, cache
hash witnesses and image readbacks, and rejects missing or mismatched evidence.
Ownership/evidence/profile regressions and viewer compilation pass. The first
Windows attempt identified a test-runner signal-zero probe error before sibling
launch; a read-only Windows process-handle query corrected that probe without
changing rendering behavior.

The source-qualified `bebbc07028` canonical viewer passed actual concurrent
execution: primary PID 17096 remained alive with its marker lock while sibling
PID 27052 completed read-only cache use. The primary took 29.6 seconds including
the hold, and the sibling took 16.1 seconds. Both read their textures from disk,
the GPU pixels matched, and every shared texture-cache file hash remained
unchanged during the sibling run. Actual archive completeness passed and an
omitted sibling GPU readback failed it. Evidence is retained under
`.tmp/r3-concurrent-cache-qualified`; dedicated CI now requires this case on all
three software jobs. This is local Windows software-device acceptance of actual
multiple-instance cache arbitration, not complete R3 or cross-platform parity.

## Remote PBR preparation and account ownership

Remote native materials continue to use the shared asset service, disk cache,
TinyGLTF parser, LLGLTFMaterial conversion and material registry. Native requests
now carry account guards through background decoding and main-thread publication.
Account reset removes only tracked remote entries, preserving local file-watcher
replacements in that registry. Late work cannot publish or invoke subscribers
from a retired account. A failed decode or refused worker post remains unloaded;
three bounded attempts with backoff can recover transient failure.

Native metadata is limited to 4096 entries; only unreferenced inactive entries
may be reclaimed at capacity. Material input is limited to 1 MiB and 64 JSON
nesting levels before the shared parser. Preparation reservations total at most
128 MiB and survive queued publication. Native remote material parsing accepts
texture UUID references and refuses external filesystem reads, embedded image
decoding and geometry payloads; those have their own production owners. Existing
OpenGL material loading retains its original path.

Production-method regressions exercise valid/failed publication, refused work,
decode exceptions, finite retries, delayed completion after account reset,
registry replacement, disconnected ownership and a fully pinned metadata cache.
Viewer compilation passes. A viewer replay now serves valid, malformed and
oversized material assets through the actual ViewerAsset HTTP path and admits
six ObjectUpdate PBR faces using the fetched material. It checks shared factors,
native image owners, object retirement and account-registry retirement. The
cold-cache CI case requires those actual HTTP bytes and archived fixture inputs.
Runtime acceptance of this new remote material checkpoint remains pending its
canonical rebuild.

The source-qualified `0cef4cb0e3` canonical viewer passed the actual remote
material/image replay in 14.5 seconds and the combined R2/R3 production replay in
76.2 seconds. The valid HTTP material produced all six native PBR faces;
malformed and oversized HTTP assets remained unloaded. Object and account
registry retirement passed. These results qualify this checkpoint on the local
Windows software device. Recovery of a corrupt material asset-cache entry and
the remaining R3 obligations still require implementation/qualification.

## Corrupt material asset-cache recovery

Native decode now distinguishes malformed encoded data from temporary
preparation refusal. Only malformed cached assets are invalidated through the
existing LLFileSystem cache API; refused work and allocation failure preserve
valid encoded bytes. The next bounded retry can consequently fetch corrected
server data instead of repeatedly parsing the same corrupt file.

Native registry markers now retain their material owner rather than keeping a
raw pointer that an allocator could reuse after a shared-cache removal. Local
watcher replacements remain protected. Inactive world faces also release their
parsed PBR asset reference, allowing unreferenced metadata to be reclaimed under
pressure while CPU topology and material identities remain available.

Production regressions cover corrupt-data invalidation/retry, preparation and
publication exceptions, cache-owner retention and inactive PBR references. The
actual viewer replay now includes an asset that initially arrives malformed,
recovers through a second HTTP response, and replaces all six admitted PBR faces
with a different base-color factor. HTTP body hashes and archived input
files are decisive; success flags alone cannot qualify it. Focused tests and
viewer compilation pass; actual recovery runtime acceptance is pending rebuild.

The first runtime attempt exposed a fixture error: its corrected asset was still
encoded as malformed. The viewer correctly invalidated and fetched that asset
three times. Correcting only the fixture, with the same deadlines and acceptance
checks, produced a source-qualified `285ac63f33` recovery pass in 18.2 seconds.

That canonical viewer subsequently passed all nine cache restart cases in 125.4
seconds, including actual malformed-asset recovery and all six PBR face
replacements in the cold case. Its actual concurrent primary/sibling execution
also passed (31.9 seconds for the primary including its hold, 17.8 seconds for
the sibling). Both complete archive checks passed; omitting the corrected remote
material input or a sibling GPU readback correctly failed qualification. Evidence
is retained in `.tmp/r3-material-cache-final` and
`.tmp/r3-material-cache-final-instances`. These are local Windows software-device
results; remaining R3 production work and three-platform acceptance remain open.

## Actual legacy material capability and variants

The viewer replay now acquires a real compressed `RenderMaterials` response
through its requested seed capability, the native regional material owner and
the shared LLMaterial decoder. One ObjectUpdate carries six distinct face
contracts: unmaterialed diffuse/shiny, normal/specular opaque, masked fullbright,
emissive alpha, blended alpha and default-alpha fullbright. All three nonzero
legacy shiny values are checked. Independent normal and specular source images
must reach their correct native bindings and GPU readbacks.

The evidence verifier checks actual HTTP body hashes, compressed-to-binary LLSD
equivalence, independent per-channel UV calculations, specular/environment and
roughness/cutoff values, all six alpha/fullbright/emissive/shiny variants and
object/account retirement. The archive must retain the capability body, binary
payload and both channel readbacks. Negative evidence tests reject altered UVs,
missing HTTP proof, changed payloads and omitted GPU files. The expanded image
exercise also accepts the clearer `--r3-image-contracts` alias. Compiler and
focused evidence/replay regressions pass; actual capability runtime acceptance
is pending its canonical rebuild. Final BRDF/probe appearance remains R5/R6.

The actual legacy run exposed a production grant-path gap: the primary region
requested `RenderMaterials` but its capability reply filter discarded it, so no
regional material request could be sent. That primary filter also omitted
`ObjectMedia`, `ObjectMediaNavigate` and `ModifyMaterialParams`, which the
neighboring-region path already retained. All four now pass through the same
validated web-URL admission as the other requested services. A regression
executes the actual primary admission loop, proving those fields survive while
non-web and unrequested capabilities remain rejected. The fix compiles; the
legacy runtime acceptance must be rerun with it.

The capability correction is now verified by the actual staged viewer at
`343e202e13`: the headless software-Vulkan session passed in 21.0 seconds,
including the real compressed legacy material HTTP response, all six face
variants, independent channel GPU readbacks and object/account retirement.
Evidence is retained in `.tmp/r3-legacy-grants-qualified`. This establishes the
local legacy-material path; it does not certify unfinished terrain, settings,
media-plugin or pressure-control work, or three-platform R3 acceptance.

## Shared media-plugin publication

The UI control now releases its own image handle without erasing a publication
owned by the shared LLViewerMediaImpl. That source already retires publication
in destroyMediaSource(). This preserves a world/UI consumer when a preview
closes and preserves queued GPU generations after the last source retires.
The peer OpenGL path is unchanged. A regression executes the actual control
destructor with retained and final source ownership.

The image-contract replay now opens the actual media plugin on a loopback HTML
canvas with independently specified quadrant colors. It requires initial and
updated 64x64 GPU pixels, resized 78x46 pixels, canonical orientation/stride,
increasing immutable generations, shared-manager identity, publication survival
after UI close with a retained surface consumer, final-source retirement and
account retirement. HTTP receipts, independent pixel checks and mandatory
archive readbacks reject absent or substituted evidence. The cases remain
headless with bounded stage deadlines. Source regressions and real viewer
translation-unit compilation pass; actual plugin runtime acceptance is pending
its rebuilt canonical executable.

Actual plugin execution at `5c274b45fd` passed the full headless image replay
in 32.6 seconds (`.tmp/r3-media-resize-observed`). The real CEF plugin supplied
BGRA/OpenGL-origin buffers at 64x64 and 78x46; all independently expected GPU
pixels, increasing immutable generations, shared-manager/surface ownership,
last-source retirement and account retirement passed. The first attempt at
`9fb65c6d97` had passed initial/update pixels but timed out at resize; the UI
control's temporary-invalid-buffer path still erased source-owned publication.
That erase is now removed as well. Stage diagnostics record actual plugin,
stride and publication dimensions rather than extending deadlines or relaxing
pixel checks. This local result does not certify all R3 work or platform CI.

## Settings registration inventory and shared CPU effects

`r3-settings-listeners.csv` records every one of the 208 source registration
sites, including both dynamically expanded local-terrain registration sites;
those loop sites represent multiple runtime listeners. The inventory separates
56 reviewed shared callback registrations (53 setting names), ten directly
polled native owners, ten open R3 terrain/pressure controllers and 132 later
phase callback registrations. A recorded later owner is not a claim that its
effect is implemented. Velocity interpolation's native polling does not certify
its separately noted simulator preference notification.

Both setting wrappers now admit the reviewed CPU/UI/audio/voice callbacks while
preserving the block on legacy rendering/scene callbacks and unknown settings.
Their actual C++ wrappers are executed by a regression proving both overloads
route shared effects, suppress native GL cascades and preserve OpenGL callbacks.
An inventory regression requires every source registration to have a disposition.

The actual viewer replay also checks navbar internal visibility changes, debug
rectangle state, both chat-font change/restoration notifications and a real
settings-file round trip. Independent evidence validation checks the persisted
types and values; missing files, unchanged values or missing effects fail. The
archive must retain the settings file. Translation-unit compilation and focused
regressions pass; this new runtime check remains pending a canonical rebuild.

Actual shared-settings replay at `0666377b6b` passed in 29.6 seconds
(`.tmp/r3-settings-registration-qualified`). The first runtime attempt at
`81c36d2300` correctly failed because the native startup omitted listener
registration entirely. Native startup now registers the reviewed shared
callbacks after their owners exist; deferred callbacks are omitted at
registration rather than becoming admissible after account teardown.

The subsequent cache matrix passed cold execution but rejected warm execution:
one default-object texture performed HTTP despite its saved cold cache body.
The full matrix is consequently not qualified at this checkpoint. Dedicated
worker tracing then reproduced successful zero-network warm reads, including
a fresh cold/restart pair whose saved headers and bodies were retained in
`.tmp/r3-cache-restart-trace`. This does not explain or waive the original
failure. Native-only miss/header-rejection diagnostics now record the actual
cached, desired and known byte sizes at the failed transition, without global
DEBUG logging that could mask a timing problem. Pending investigation retains
the existing zero-network, pixel and archive requirements.

## Exact disk-operation completion ownership

The cache responder audit found a concrete native ownership gap independent of
the unresolved warm-fetch trace: read/write completions looked up a worker by
texture UUID and checked only its current state. A replacement worker or later
operation for that same UUID could receive an older completion, including a
failed read. Native cache operations now carry unique operation identities;
the callbacks validate those identities under the worker mutex before changing
any state. OpenGL operations retain their existing untagged behavior. The actual
responder and callback implementations are exercised against stale read/write
successes and failures, replacement operations, absent workers and the legacy
path. Viewer compilation and these regressions pass; runtime qualification is
pending the canonical rebuild.

One diagnostic cache run also lost its Python driver's result before the matrix
could record that driver's exit status. The matrix now streams and saves driver
output and records the actual exit/timeout status before reading viewer results.
Those artifacts are mandatory in the qualification archive. Actual subprocess
regressions retain nonzero exit and timeout failures, and missing output fails
archive qualification. This corrects failure evidence rather than accepting an
unexplained successful rerun or changing the cache/network oracle.

The operation-scoped cache build reached a different decisive failure in the
cold case: the retained media observer proved that the resized plugin CPU
buffer itself was entirely black, and Vulkan uploaded those same bytes. Thus
this case does not demonstrate a Vulkan pixel/orientation mismatch. The
fixture now uses a static four-cell CSS layout instead of an imperative canvas
resize callback; the exact three independent pixel oracles, dimensions, HTTP
receipts, immutable generations and lifetime checks are unchanged. No shared
CEF/OpenGL code was changed to fit the test.

Separately, native media publication now follows the existing OpenGL consumer's
paint gating: an initial/resize acknowledgement without dirty pixels retains
the previous generation rather than publishing an unpainted shared buffer.
The actual publication regression covers both initial and resize acknowledgements,
as well as refused uploads and subsequent dirty publication. Viewer compilation
and focused evidence/replay tests pass; the full runtime/cache checks are pending
the rebuilt checkpoint. The original warm-request cause remains unclaimed.


## Expanded demand during an outstanding cache read

At source `826953839e`, four independent headless read-only restarts reused the
saved cache with zero texture HTTP requests (`.tmp/r3-cache-body-repeat`). This
successful repetition does not establish the cause of the earlier intermittent
miss. Native body lookups now retain APR status, actual 64-bit length and an
independent file-stat diagnostic; oversized cached bodies are rejected before
narrowing or allocation. Legacy file queries retain their existing path.

A separate state-machine audit reproduced a concrete pending-demand gap: a
successful 600-byte disk read can complete after a subscriber raises demand to
16 MiB. Previously `CACHE_POST` treated this as a miss and proceeded to HTTP,
even though the requested remainder could already exist on disk. Native reads
now record their submitted end offset and continue the shared cache read when
a completed bounded read satisfied that old demand. Short reads at the submitted
bound, disabled caches and OpenGL consumers retain ordinary recovery behavior.
The actual production `CACHE_POST` block is executed by a C++ regression covering
all those transitions and final full-cache completion. This corrects an observed
state-machine defect; attribution of the historical intermittent misses remains
unproven. Full R3 runtime and platform qualification remain open.


## Native texture-pressure controller

Native presentation now polls the same VRAM budget/override, system-memory,
manual-limit and background/minimized settings used by the reference texture
policy. The CPU policy is a transposition of `LLViewerTexture::updateClass`,
including its conservative GPU allocation estimate, delayed pressure ramp,
recovery headroom and discard-area scaling. Actual GPU image and geometry
allocations feed that estimate; decoded snapshots and preparation leases stay
separately reported. The existing native payload admission bound supplies a
separate pressure signal, latched through allocation refusal until residency
falls, rather than misreporting those CPU bytes as GPU memory.

Ordinary world subscribers send coarser pressure demand through the existing
fetch/cache decoder; UI, exact-channel and forced-full-resolution demand retain
their existing treatment. A queued over-detailed world publication can yield
to a coarser decode after the shared worker finishes its cache write; the last
published generation remains intact. The override notification uses the audited
shared CPU callback while the native controller polls its rendering effect.

The regression executes 1,000 frames of the actual extracted OpenGL memory
calculation against the native policy, covering pressure, manual budgets,
system-memory pressure and recovery. It also executes the actual native
resource controller against allocation refusal/recovery and verifies override
and background policy. Changed translation units compile. This checkpoint is
not full pressure acceptance: minimized scene-owner rebinding/retirement,
actual viewer pressure readbacks and imported glTF/terrain consumers remain
open and must be completed before R3 qualification.


The canonical staged source `a3ddce4148` passed all nine cache cases in 170.1
seconds (`.tmp/r3-pressure-cache-matrix`), including zero-network warm,
read-only and resize cases. Actual concurrent primary/read-only sibling
ownership also passed (`.tmp/r3-pressure-cache-instances`). The cold case
includes the media, local/remote material and shared-settings contracts.
These results qualify those checkpoints; they do not certify the remaining
terrain, imported-image, pressure-transition or pending-I/O requirements.

## Imported glTF worker decoding and progressive publication

Imported embedded/local images previously performed file reads, data-URI
expansion and full decode on the frame thread, independently of native texture
resolution preferences. These now run on the existing General work queue with
separate global encoded (64 MiB), decoded (256 MiB) and existing preparation
(640 MiB) admission. Actual dimensions/components are validated before decoded
allocation; transient admission refusal retries without discarding the visible
publication. Existing image decoders and `LLImageRaw::scale` implement the
selected discard; a new resampling algorithm is not introduced.

The native image owner polls resolution/full-resolution/compression preferences
and the shared pressure policy, aggregates projected demand per image and frame,
and retains immutable generations through queued preference changes. Worker
results hold their budget until preparation takes ownership; epoch checks reject
late completions after scene retirement or index reuse. Decoded working memory
is separately reported from GPU residency. The actual asynchronous decode and
retirement methods pass regressions covering discard, queued retirement/reuse,
refused decoded admission and oversized headers, alongside the previous
preparation-lifecycle test. Viewer translation-unit compilation passes. Actual
five-channel imported-scene GPU qualification is the next required checkpoint.


The actual-scene replay now imports a deterministic skinned, animated glTF
through `VSWorldScene::loadGLTF` after a real object update. Its five independent
PNG channels exercise base color, normal, ORM, emissive and separate occlusion;
material factors, UV0/UV1 selection, transforms and sampler policy accompany
that same production path. It requires limited, forced-full-resolution,
limited-again and asset-replacement generations, with complete independent GPU
pixel oracles and retained-generation immutability. Object/account retirement
must release the recorded image owners. Both exact input glTF files and all 20
readbacks are mandatory archive evidence. Missing/corrupt pixels, changed inputs
and reused generations fail the independent validator. Compilation and focused
replay/archive regressions pass; canonical runtime execution follows this
checkpoint and is not yet claimed here.


## Linux public-header macro collision

Linux viewer compilation exposed `ImageCompression::None` to the `None` macro
from Xlib, included by the existing GLX/precompiled headers. The macro expands
to `0L`, breaking the enum declaration and causing downstream native contract
and renderer types to appear missing. The compression member is now named
`Uncompressed` throughout the viewer and standalone harness; its value remains
zero, BC3 remains one, and compression selection is unchanged.

A permanent source-CI regression compiles the actual public rendering headers
with platform headers before and after them. It uses Xlib when available on
Linux and reproduces its exact macro otherwise. Both include orders reproduce
the pre-fix failure and pass after the rename, without undefining platform
macros. Native texture preparation and material-basis regressions also pass.
The full Linux CI rebuild remains the platform qualification step.


The same revision's qualification workflow also failed before graphics jobs:
its extracted native cleanup fixture lacked the current texture-fetch drain,
shutdown and statistics-recorder interfaces. The fixture now executes that
sequence and asserts pending workers drain before deletion. A full 98-script
source-CI sweep identified three further stale fixtures: scene replacement
material/media/animation ownership, avatar bake alias resolution and extracted
image-role classification. These fixtures now model the current dependencies
and retain/extend ownership, alias-generation and pool-capacity assertions;
viewer behavior was not changed to satisfy those tests.

After these corrections, 97 of the 98 source-CI scripts pass locally. The
remaining Windows-only process-isolation script cannot create its private
window station under the current process token (WinError 5); its Windows-only
cases are skipped on the Linux source-CI runner. One initial parallel sweep
also saw a loopback connection reset in the parity baseline; that unchanged
script passes standalone. This does not establish the reset's cause or waive
its assertions. Actual Linux builds/graphics still require the pushed CI run.


The next Linux source run passed X11 header coexistence and the corrected
fixtures, then exposed a codec-wrapper include dependency hidden by Windows
standard-library/PCH includes: STB implementation mode calls global `memcpy`
but does not include its declaration. Our `vstextureupload.h` wrapper now
includes `<cstring>` before the pinned STB header; the vendor source, pin and
codec policy remain unchanged. Public-header regression coverage also compiles
the wrapper first in implementation mode. Linux CI remains decisive for the
platform's standard-library include behavior.


### Four-layer legacy terrain checkpoint (2026-10-17)

Replaced the native patch-wide single-texture selection with the reference
four-detail-layer composition. Native mesh UV1 carries shared height/noise
composition coordinates. The production shader samples all four independently
published images and the shipped alpha-gradient asset, using the reference
blend order. World demand remains projected rather than forcing UI resolution.
RenderTerrainScale changes constants without rebuilding geometry.

The native coordinate producer matches the actual reference generateHeights
method over 65,536 samples with asymmetric corner parameters. Headless
SwiftShader production-renderer charts verify all four layers, three blend
seams and live scale changes; every chart is mandatory runner evidence.
Ground admission/refusal and prior-publication ownership regressions pass.
This checkpoint does not close terrain PBR, cache lifecycle, pressure runtime
qualification or final three-platform R3 acceptance.
