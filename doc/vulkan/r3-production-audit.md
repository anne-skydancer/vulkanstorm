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
