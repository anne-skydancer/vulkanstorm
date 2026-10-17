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

## Remaining R3 production paths

| Area | Existing path | Missing work and acceptance |
| --- | --- | --- |
| Legacy materials | TE diffuse tint, UVs, fullbright and basic alpha classification in `VSWorldScene` | Shared material asset ownership; normal/specular/bump/shiny/emissive channels, tangent conventions, independent UV transforms and exact alpha semantics |
| PBR | glTF scenes supply base color, alpha, unlit and double-sided flags | Object material assets and overrides; all channel textures/factors/color encodings; rigged and terrain material variants. Lighting/probe appearance remains R5/R6 |
| Streaming/sampling | Immutable native image publication, bounded full-image HTTP/decode, shared disk-cache entry point | Demand-driven mip/discard/range progression, mip generation/residency, sampler/address/filter/anisotropy and compression policy, eviction/reload and TE/PBR animation |
| Media | Shared UI/CEF owners and image publication | Plugin stride/format/origin, dirty-region generations and common world-media/preview resources |
| Settings | Native controllers exist for earlier-stage services | Inventory rendering listeners in `llviewercontrol.cpp`; connect each R3 preference to observable native resource changes and persisted reload. Later lighting/postprocess controllers belong to their roadmap stages |

Material, UV, color and sampling charts must compare reference semantics.
A saved preference without a production effect does not satisfy acceptance.
All stages retain existing ownership budgets, strict validation and explicit
qualification limits. This record must be updated as production paths land.
